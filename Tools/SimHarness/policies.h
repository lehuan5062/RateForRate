// Harness-only stand-in behaviour: scripted rival, other entries, Mod Eye and player bots.
// NOT the final StateTree layer. These call the core's public API exactly as a StateTree task would.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "ShiftSim.h"

namespace ShiftSim
{

struct PolicyParams
{
    // stand-in rival
    double RivalDefendBelow = 0.75;      // defend the weakest high-value rival node when its conviction is below this
    double RivalPlayerMult = 2.0;        // attack score multiplier for player-owned nodes
    double RivalCapShare = 0.65;         // at/above this share the rival only attacks player-owned nodes (1 = never complacent)
    double RivalBandBase = 1.0;          // strength = base + slope * max(0, playerShare - threshold)
    double RivalBandSlope = 4.0;
    double RivalBandThreshold = 0.15;
    double MegaDays = 3.0;               // last N days: Mega Campaign
    double MegaConvBoost = 0.3;          // +conviction on all rival nodes at Mega start
    double MegaAttackMult = 2.0;         // actions per rival tick during Mega
    // stand-in others
    double OthersInterval = 10.0;        // each other entry acts this often
    double OthersMaxV = 100.0;           // weak: only low-weight nodes
    // stand-in Mod Eye (director)
    double ModFirstSweepMin = 0.0;       // the Eye's cycle is already running at t=0: first sweep ~ U[this, jam ModIntervalMax]
    // player bots
    double BotInterval = 0.2;            // minimum seconds between bot clicks
    double SockMaxHeat = 45.0;           // greedy+sock socks only when Heat <= this
    double SockWindup = 0.5;             // seconds the bot holds night before the sock click
};

enum class BotKind
{
    Idle = 0,
    Random,
    Greedy,
    GreedySock,
};

inline const char* BotName(BotKind K)
{
    switch (K)
    {
    case BotKind::Idle: return "idle";
    case BotKind::Random: return "random";
    case BotKind::Greedy: return "greedy";
    case BotKind::GreedySock: return "greedy+sock";
    }
    return "?";
}

inline int PolicyRound(double V)
{
    return static_cast<int>(V < 0.0 ? V - 0.5 : V + 0.5);
}

// ---------------------------------------------------------------------------------------
// Stand-in rival: every Jam::rivalInterval seconds defend the weakest high-value rival node,
// else attack the best value-per-effort target (player nodes weighted x2), strength rubber-banded
// on the player's share. Last MegaDays days: +conviction pulse and doubled actions.
// ---------------------------------------------------------------------------------------
class StandInRival
{
public:
    // Interval: the jam definition's rivalInterval (Jams.csv), 2 s if unset.
    static double IntervalFor(const Sim& S, const PolicyParams& /*PP*/)
    {
        return S.Jam().rivalInterval > 0.1 ? S.Jam().rivalInterval : 2.0;
    }

    void Reset(const Sim& S, const PolicyParams& PP)
    {
        Next_ = IntervalFor(S, PP);
        MegaStarted_ = false;
    }

    void Update(Sim& S, const PolicyParams& PP)
    {
        if (S.Time() + 1.0e-9 < Next_)
        {
            return;
        }
        Next_ += IntervalFor(S, PP);
        const int Rival = S.RivalId();
        const bool Mega = S.DayIndex() >= S.Jam().days - PolicyRound(PP.MegaDays);
        if (Mega && !MegaStarted_)
        {
            MegaStarted_ = true;
            S.AddConvictionToEntry(Rival, PP.MegaConvBoost);
        }
        const int Actions = Mega ? PolicyRound(PP.MegaAttackMult) : 1;
        for (int A = 0; A < Actions; ++A)
        {
            const int Weak = S.WeakestOwned(Rival, S.GetParams().HighValueV, PP.RivalDefendBelow);
            if (Weak >= 0)
            {
                S.Plug(Rival, Weak, 1.0);
                ++Defends;
                continue;
            }
            const double Over = S.Share(kPlayerId) - PP.RivalBandThreshold;
            const double Strength = PP.RivalBandBase + PP.RivalBandSlope * (Over > 0.0 ? Over : 0.0);
            TargetFilter F;
            F.strength = Strength;
            F.ownerMult[kPlayerId] = PP.RivalPlayerMult;
            if (S.Share(Rival) >= PP.RivalCapShare)
            {
                // Comfortably ahead: leave the other entries alone, only squash the player.
                for (int E = 0; E < S.NumEntries(); ++E)
                {
                    if (E != kPlayerId)
                    {
                        F.ownerMult[E] = 0.0;
                    }
                }
                F.undecidedMult = 0.0;
            }
            Target T;
            if (S.BestTarget(Rival, F, &T))
            {
                const ActionOutcome O = S.Plug(Rival, T.node, Strength);
                ++Attacks;
                if (O.flipped)
                {
                    ++Flips;
                }
            }
        }
    }

    int Defends = 0;   // statistics (debug)
    int Attacks = 0;
    int Flips = 0;

private:
    double Next_ = 0.0;
    bool MegaStarted_ = false;
};

// ---------------------------------------------------------------------------------------
// Stand-in other entries: weak, low-weight nodes only, every OthersInterval seconds each.
// ---------------------------------------------------------------------------------------
class StandInOthers
{
public:
    void Reset(const Sim& S, const PolicyParams& PP)
    {
        Entries_.clear();
        Next_.clear();
        for (int E = 0; E < S.NumEntries(); ++E)
        {
            if (E != kPlayerId && E != S.RivalId())
            {
                Entries_.push_back(E);
            }
        }
        const int N = static_cast<int>(Entries_.size());
        for (int I = 0; I < N; ++I)
        {
            Next_.push_back(PP.OthersInterval * (static_cast<double>(I) + 1.0) / static_cast<double>(N));
        }
    }

    void Update(Sim& S, const PolicyParams& PP)
    {
        for (size_t I = 0; I < Entries_.size(); ++I)
        {
            if (S.Time() + 1.0e-9 < Next_[I])
            {
                continue;
            }
            Next_[I] += PP.OthersInterval;
            TargetFilter F;
            F.maxV = PP.OthersMaxV;
            F.roleMask = (1u << static_cast<uint32_t>(NodeRole::Member)) | (1u << static_cast<uint32_t>(NodeRole::Bridge));
            Target T;
            if (S.BestTarget(Entries_[I], F, &T))
            {
                S.Plug(Entries_[I], T.node, 1.0);
            }
        }
    }

private:
    std::vector<int> Entries_;
    std::vector<double> Next_;
};

// ---------------------------------------------------------------------------------------
// Stand-in Mod Eye: sweeps every U[min,max] s; in the red zone when it passes -> strike.
// Heat at the cap -> immediate strike. (The real schedule lives in the StateTree director.)
// ---------------------------------------------------------------------------------------
class StandInModEye
{
public:
    void Reset(const Sim& S, uint32_t Seed, const PolicyParams& PP)
    {
        R_.Seed(Rng::Derive(Seed, 0x4D4F44u));
        Next_ = R_.Uniform(PP.ModFirstSweepMin, S.Jam().modIntervalMax);
        Strikes_ = 0;
    }

    void Update(Sim& S, const PolicyParams& /*PP*/)
    {
        if (S.HeatMaxed())
        {
            S.ApplyStrike();
            ++Strikes_;
        }
        if (S.Time() + 1.0e-9 >= Next_)
        {
            Next_ += R_.Uniform(S.Jam().modIntervalMin, S.Jam().modIntervalMax);
            if (S.InRedZone())
            {
                S.ApplyStrike();
                ++Strikes_;
            }
        }
    }

    int Strikes() const { return Strikes_; }

private:
    Rng R_;
    double Next_ = 0.0;
    int Strikes_ = 0;
};

// ---------------------------------------------------------------------------------------
// Player bots.
// ---------------------------------------------------------------------------------------
class PlayerBot
{
public:
    void Reset(BotKind K, uint32_t Seed)
    {
        Kind_ = K;
        R_.Seed(Rng::Derive(Seed, 0x424F54u));
        Next_ = 0.0;
        Winding_ = false;
        WindupEnd_ = 0.0;
    }

    BotKind Kind() const { return Kind_; }

    void Update(Sim& S, const PolicyParams& PP)
    {
        if (Kind_ == BotKind::Idle)
        {
            return;
        }
        if (S.Disqualified())
        {
            S.SetNightHeld(false);
            return;
        }
        const double Now = S.Time();

        if (Winding_)
        {
            if (S.ShadowbanRemaining() > 0.0)
            {
                S.SetNightHeld(false);
                Winding_ = false;
                return;
            }
            if (Now + 1.0e-9 >= WindupEnd_)
            {
                int Node = -1;
                if (FindGatedHub(S, &Node))
                {
                    LastOutcome = S.Sock(Node);
                    ++Actions;
                }
                S.SetNightHeld(false);
                Winding_ = false;
                Next_ = Now + PP.BotInterval;
            }
            return;
        }

        if (Now + 1.0e-9 < Next_ || S.Posts() < 1.0 || S.ShadowbanRemaining() > 0.0)
        {
            return;
        }
        Next_ = Now + PP.BotInterval;

        if (Kind_ == BotKind::Random)
        {
            LastOutcome = S.Plug(kPlayerId, R_.Range(S.NodeCount()), 1.0);
            ++Actions;
            return;
        }

        if (Kind_ == BotKind::GreedySock && S.Heat() <= PP.SockMaxHeat)
        {
            int Node = -1;
            if (FindGatedHub(S, &Node))
            {
                Winding_ = true;
                WindupEnd_ = Now + PP.SockWindup;
                S.SetNightHeld(true);
                return;
            }
        }

        TargetFilter F;
        Target T;
        if (S.BestTarget(kPlayerId, F, &T))
        {
            LastOutcome = S.Plug(kPlayerId, T.node, 1.0);
            ++Actions;
        }
    }

    ActionOutcome LastOutcome;   // statistics (debug)
    int Actions = 0;

private:
    // Best (highest value) hub or celebrity the player cannot Plug yet and that is not immune.
    static bool FindGatedHub(const Sim& S, int* OutNode)
    {
        int Best = -1;
        double BestValue = 0.0;
        for (int I = 0; I < S.NodeCount(); ++I)
        {
            if (!S.GetGraph().IsGateNode(I) || S.Opinion(I) == kPlayerId || S.LockRemaining(I) > 0.0)
            {
                continue;
            }
            if (!S.IsGated(I))
            {
                continue;
            }
            const double V = S.NodeValue(kPlayerId, I);
            if (Best < 0 || V > BestValue)
            {
                Best = I;
                BestValue = V;
            }
        }
        if (Best < 0)
        {
            return false;
        }
        *OutNode = Best;
        return true;
    }

    BotKind Kind_ = BotKind::Idle;
    Rng R_;
    double Next_ = 0.0;
    bool Winding_ = false;
    double WindupEnd_ = 0.0;
};

// ---------------------------------------------------------------------------------------
// PolicyParams name table
// ---------------------------------------------------------------------------------------
namespace PolicyDetail
{
struct Field
{
    const char* Name;
    double PolicyParams::*Member;
};
#define SHIFT_PP(field) { #field, &PolicyParams::field }
inline const Field* Fields(int* Count)
{
    static const Field kFields[] = {
        SHIFT_PP(RivalDefendBelow), SHIFT_PP(RivalPlayerMult), SHIFT_PP(RivalCapShare), SHIFT_PP(RivalBandBase),
        SHIFT_PP(RivalBandSlope), SHIFT_PP(RivalBandThreshold), SHIFT_PP(MegaDays), SHIFT_PP(MegaConvBoost),
        SHIFT_PP(MegaAttackMult), SHIFT_PP(OthersInterval), SHIFT_PP(OthersMaxV),
        SHIFT_PP(ModFirstSweepMin), SHIFT_PP(BotInterval), SHIFT_PP(SockMaxHeat), SHIFT_PP(SockWindup),
    };
    *Count = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));
    return kFields;
}
#undef SHIFT_PP
} // namespace PolicyDetail

inline bool SetPolicyParam(PolicyParams& P, const std::string& Name, double Value)
{
    int N = 0;
    const PolicyDetail::Field* F = PolicyDetail::Fields(&N);
    for (int I = 0; I < N; ++I)
    {
        if (Name == F[I].Name)
        {
            P.*(F[I].Member) = Value;
            return true;
        }
    }
    return false;
}

inline bool GetPolicyParam(const PolicyParams& P, const std::string& Name, double* Out)
{
    int N = 0;
    const PolicyDetail::Field* F = PolicyDetail::Fields(&N);
    for (int I = 0; I < N; ++I)
    {
        if (Name == F[I].Name)
        {
            *Out = P.*(F[I].Member);
            return true;
        }
    }
    return false;
}

inline int PolicyParamCount()
{
    int N = 0;
    PolicyDetail::Fields(&N);
    return N;
}

inline const char* PolicyParamNameAt(int Index)
{
    int N = 0;
    const PolicyDetail::Field* F = PolicyDetail::Fields(&N);
    return (Index >= 0 && Index < N) ? F[Index].Name : "";
}

} // namespace ShiftSim
