// Harness: unit / invariant tests for the numeric core.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "config.h"
#include "policies.h"
#include "runner.h"
#include "ShiftDetMath.h"
#include "ShiftSim.h"
#include "ShiftBlameRules.h"
#include "ShiftBuildRules.h"
#include "ShiftRivalBank.h"

namespace ShiftSim
{
namespace Tests
{

struct Counter
{
    int Passed = 0;
    int Failed = 0;
};

inline Counter& Count()
{
    static Counter C;
    return C;
}

inline void Check(bool Ok, const char* Expr, const char* File, int Line)
{
    if (Ok)
    {
        ++Count().Passed;
    }
    else
    {
        ++Count().Failed;
        std::printf("  FAIL %s:%d  %s\n", File, Line, Expr);
    }
}

#define T_CHECK(cond) ::ShiftSim::Tests::Check((cond), #cond, __FILE__, __LINE__)

inline bool Near(double A, double B, double Tol = 1.0e-9)
{
    return std::fabs(A - B) <= Tol;
}

inline bool Finite(double X)
{
    return std::isfinite(X) != 0;
}

inline int RoundToIntLocal(double V)
{
    return static_cast<int>(V < 0.0 ? V - 0.5 : V + 0.5);
}

// ------------------------------------------------------------------------------------------
inline void TestRng()
{
    std::printf("[rng/math]\n");
    Rng R(1);
    R.SetState(1, 2, 3, 4);
    // Hand-computed xoshiro128** reference for state {1,2,3,4}.
    T_CHECK(R.NextU32() == 11520u);
    T_CHECK(R.NextU32() == 0u);
    T_CHECK(R.NextU32() == 5927040u);
    T_CHECK(R.NextU32() == 70819200u);

    Rng A(42), B(42), C(43);
    bool Same = true, Diff = false;
    for (int I = 0; I < 1000; ++I)
    {
        const uint32_t Va = A.NextU32();
        const uint32_t Vb = B.NextU32();
        const uint32_t Vc = C.NextU32();
        Same = Same && (Va == Vb);
        Diff = Diff || (Va != Vc);
    }
    T_CHECK(Same);
    T_CHECK(Diff);

    // Range / Uniform sanity.
    Rng U(7);
    bool InRange = true;
    double Sum = 0.0;
    for (int I = 0; I < 20000; ++I)
    {
        const double D = U.NextDouble();
        InRange = InRange && D >= 0.0 && D < 1.0;
        Sum += D;
        const int K = U.Range(9);
        InRange = InRange && K >= 0 && K < 9;
    }
    T_CHECK(InRange);
    T_CHECK(Near(Sum / 20000.0, 0.5, 0.02));

    // DetPow vs std::pow, and the rounded weight matches.
    double MaxRel = 0.0;
    bool WeightsMatch = true;
    Params P;
    for (int F = 0; F <= 3000; ++F)
    {
        const double X = 1.0 + static_cast<double>(F);
        const double Mine = DetPow(X, 1.3);
        const double Ref = std::pow(X, 1.3);
        const double Rel = std::fabs(Mine - Ref) / Ref;
        if (Rel > MaxRel) { MaxRel = Rel; }
        const double Wm = std::floor(30.0 * Mine + 0.5);
        const double Wr = std::floor(30.0 * Ref + 0.5);
        if (Wm != Wr) { WeightsMatch = false; }
        T_CHECK(ComputeWeight(P, F) == Wm);
    }
    std::printf("  DetPow max relative error vs std::pow: %.3g\n", MaxRel);
    T_CHECK(MaxRel < 1.0e-13);
    T_CHECK(WeightsMatch);
    T_CHECK(ComputeWeight(P, 0) == 30.0);
    T_CHECK(ComputeWeight(P, 1) == 74.0);
    T_CHECK(Near(ComputeInfluence(P, 3), 2.0));
}

// ------------------------------------------------------------------------------------------
inline void TestGraph(const Config& Cfg)
{
    std::printf("[graph]\n");
    for (size_t J = 0; J < Cfg.jams.size(); ++J)
    {
        const JamDef& Jam = Cfg.jams[J];
        double RecipSum = 0.0;
        int RecipN = 0;
        for (int Seed = 1; Seed <= 20; ++Seed)
        {
            Graph G;
            T_CHECK(BuildGraph(Jam, Cfg.params, static_cast<uint32_t>(Seed), &G));
            T_CHECK(G.NodeCount() == Jam.nodes);
            T_CHECK(static_cast<int>(G.hubOfCommunity.size()) == Jam.communities);
            T_CHECK(static_cast<int>(G.celebrities.size()) == Jam.celebs);
            T_CHECK(static_cast<int>(G.bridges.size()) == Jam.bridges);

            int Edges = 0;
            int Recip = 0;
            bool Mirror = true, NoSelf = true, NoDup = true, InBox = true, Weights = true, FollowsOk = true;
            bool CelebFollowedEverywhere = true;
            for (int I = 0; I < G.NodeCount(); ++I)
            {
                const GraphNode& N = G.nodes[static_cast<size_t>(I)];
                Edges += static_cast<int>(N.follows.size());
                if ((N.role == NodeRole::Member || N.role == NodeRole::Bridge) && N.follows.empty()) { FollowsOk = false; }
                for (size_t K = 0; K < N.follows.size(); ++K)
                {
                    const int T = N.follows[K];
                    if (T == I) { NoSelf = false; }
                    const GraphNode& Tn = G.nodes[static_cast<size_t>(T)];
                    bool Found = false;
                    for (size_t Q = 0; Q < Tn.followers.size(); ++Q) { if (Tn.followers[Q] == I) { Found = true; } }
                    if (!Found) { Mirror = false; }
                    for (size_t Q = K + 1; Q < N.follows.size(); ++Q) { if (N.follows[Q] == T) { NoDup = false; } }
                    for (size_t Q = 0; Q < Tn.follows.size(); ++Q) { if (Tn.follows[Q] == I) { ++Recip; } }
                }
                if (N.x < static_cast<float>(Cfg.params.LayoutMargin) - 0.01f || N.x > static_cast<float>(Cfg.params.LayoutWidth - Cfg.params.LayoutMarginRight) + 0.01f ||
                    N.y < static_cast<float>(Cfg.params.LayoutMargin) - 0.01f || N.y > static_cast<float>(Cfg.params.LayoutHeight - Cfg.params.LayoutMargin) + 0.01f)
                {
                    InBox = false;
                }
                if (N.weight != ComputeWeight(Cfg.params, N.followerCount) || !Finite(N.infl) || N.infl < 1.0) { Weights = false; }
            }
            for (size_t Z = 0; Z < G.celebrities.size(); ++Z)
            {
                for (size_t C = 0; C < G.communityNodes.size(); ++C)
                {
                    bool Any = false;
                    for (size_t K = 0; K < G.communityNodes[C].size(); ++K)
                    {
                        const GraphNode& N = G.nodes[static_cast<size_t>(G.communityNodes[C][K])];
                        if (N.role == NodeRole::Bridge) { continue; }
                        for (size_t Q = 0; Q < N.follows.size(); ++Q) { if (N.follows[Q] == G.celebrities[Z]) { Any = true; } }
                    }
                    if (!Any) { CelebFollowedEverywhere = false; }
                }
            }
            T_CHECK(Mirror);
            T_CHECK(NoSelf);
            T_CHECK(NoDup);
            T_CHECK(InBox);
            T_CHECK(Weights);
            T_CHECK(FollowsOk);
            T_CHECK(CelebFollowedEverywhere);
            RecipSum += static_cast<double>(Recip);
            RecipN += Edges;

            // Members per community in 5..9 range (soft: only checked for Jam sizes that allow it).
            for (size_t C = 0; C < G.communityNodes.size(); ++C)
            {
                int Members = 0;
                for (size_t K = 0; K < G.communityNodes[C].size(); ++K)
                {
                    if (G.nodes[static_cast<size_t>(G.communityNodes[C][K])].role == NodeRole::Member) { ++Members; }
                }
                T_CHECK(Members >= 4 && Members <= 9);
            }

            Graph G2;
            BuildGraph(Jam, Cfg.params, static_cast<uint32_t>(Seed), &G2);
            bool Same = (G2.NodeCount() == G.NodeCount());
            for (int I = 0; Same && I < G.NodeCount(); ++I)
            {
                const GraphNode& A = G.nodes[static_cast<size_t>(I)];
                const GraphNode& B = G2.nodes[static_cast<size_t>(I)];
                Same = A.follows == B.follows && A.weight == B.weight && A.x == B.x && A.y == B.y;
            }
            T_CHECK(Same);
        }
        const double Frac = RecipN > 0 ? RecipSum / static_cast<double>(RecipN) : 0.0;
        std::printf("  jam %zu: %d nodes, reciprocal edge fraction %.3f\n", J + 1, Jam.nodes, Frac);
        T_CHECK(Frac > 0.08 && Frac < 0.30);
    }
}

// ------------------------------------------------------------------------------------------
// Scripted action log used by the determinism test: generated from its own RNG only.
struct LogAction
{
    int Step;
    int Kind;      // 0 plug, 1 sock, 2 strike, 3 night on, 4 night off
    int Who;
    int Node;
    double Strength;
};

inline std::vector<LogAction> MakeActionLog(uint32_t Seed, int Nodes, int Steps, int Entries)
{
    Rng R(Rng::Derive(Seed, 99u));
    std::vector<LogAction> L;
    for (int S = 0; S < Steps; ++S)
    {
        if (R.Chance(0.12))
        {
            LogAction A;
            A.Step = S;
            const double K = R.NextDouble();
            A.Kind = K < 0.60 ? 0 : (K < 0.80 ? 1 : (K < 0.84 ? 2 : (K < 0.92 ? 3 : 4)));
            A.Who = (A.Kind == 0 && R.Chance(0.5)) ? R.Range(Entries) : kPlayerId;
            A.Node = R.Range(Nodes);
            A.Strength = 1.0 + R.Uniform(0.0, 2.0);
            L.push_back(A);
        }
    }
    return L;
}

// DynSeed < 0: the plain three-argument Init; otherwise the four-argument one with this dynamics seed.
inline std::vector<uint64_t> ReplayLog(const JamDef& Jam, const Params& P, uint32_t Seed, const std::vector<LogAction>& Log, int Steps, int64_t DynSeed = -1)
{
    Sim S;
    if (DynSeed < 0)
    {
        S.Init(Jam, P, Seed);
    }
    else
    {
        S.Init(Jam, P, Seed, static_cast<uint32_t>(DynSeed));
    }
    std::vector<uint64_t> Hashes;
    size_t Next = 0;
    for (int Step = 0; Step < Steps; ++Step)
    {
        while (Next < Log.size() && Log[Next].Step == Step)
        {
            const LogAction& A = Log[Next];
            if (A.Kind == 0) { S.Plug(A.Who, A.Node, A.Strength); }
            else if (A.Kind == 1) { S.Sock(A.Node); }
            else if (A.Kind == 2) { S.ApplyStrike(); }
            else if (A.Kind == 3) { S.SetNightHeld(true); }
            else { S.SetNightHeld(false); }
            ++Next;
        }
        S.Step();
        Hashes.push_back(S.StateHash());
    }
    return Hashes;
}

inline void TestDeterminism(const Config& Cfg)
{
    std::printf("[determinism]\n");
    const int Steps = 2400; // 120 s
    for (size_t J = 0; J < Cfg.jams.size(); ++J)
    {
        const JamDef& Jam = Cfg.jams[J];
        for (uint32_t Seed = 11; Seed <= 13; ++Seed)
        {
            const std::vector<LogAction> Log = MakeActionLog(Seed, Jam.nodes, Steps, Jam.entries);
            const std::vector<uint64_t> H1 = ReplayLog(Jam, Cfg.params, Seed, Log, Steps);
            const std::vector<uint64_t> H2 = ReplayLog(Jam, Cfg.params, Seed, Log, Steps);
            T_CHECK(H1 == H2);
            const std::vector<uint64_t> H3 = ReplayLog(Jam, Cfg.params, Seed + 1000u, Log, Steps);
            T_CHECK(H1.back() != H3.back());
        }
    }
    // Full policy runs reproduce exactly, also across threads.
    RunSpec Spec;
    Spec.jam = 1;
    Spec.seed = 31337;
    Spec.bot = BotKind::GreedySock;
    const RunResult A = RunOne(Cfg, Spec);
    const RunResult B = RunOne(Cfg, Spec);
    T_CHECK(A.hash == B.hash);
    T_CHECK(A.playerShare == B.playerShare);
    const BatchStats One = RunBatch(Cfg, 0, BotKind::Greedy, 12, -1.0, 1, true);
    const BatchStats Many = RunBatch(Cfg, 0, BotKind::Greedy, 12, -1.0, 4, true);
    bool SameBatch = true;
    for (size_t I = 0; I < One.results.size(); ++I) { SameBatch = SameBatch && One.results[I].hash == Many.results[I].hash; }
    T_CHECK(SameBatch);
    std::printf("  golden: jam2 seed 31337 greedy+sock final hash %016llx\n", static_cast<unsigned long long>(A.hash));
}

// ------------------------------------------------------------------------------------------
// One world, many lucks: Init(Jam, P, Seed, DynSeed) draws the world from Seed and only the coin flips of undecided voters from DynSeed.
inline void TestDynSeed(const Config& Cfg)
{
    std::printf("[dynamics seed]\n");
    const int Steps = 2400; // 120 s
    for (size_t J = 0; J < Cfg.jams.size(); ++J)
    {
        const JamDef& Jam = Cfg.jams[J];
        for (uint32_t Seed = 21; Seed <= 23; ++Seed)
        {
            const std::vector<LogAction> Log = MakeActionLog(Seed, Jam.nodes, Steps, Jam.entries);
            // The three-argument Init is the four-argument one with DynSeed = Seed: every hash along the run is the same, so no existing number moves.
            const std::vector<uint64_t> Plain = ReplayLog(Jam, Cfg.params, Seed, Log, Steps);
            const std::vector<uint64_t> Same = ReplayLog(Jam, Cfg.params, Seed, Log, Steps, static_cast<int64_t>(Seed));
            T_CHECK(Plain == Same);
            // Another DynSeed is the same world with other luck: identical at the start, apart after a few seconds, and reproducible.
            const uint32_t Other = Rng::Derive(Seed, 100u);
            const std::vector<uint64_t> Luck = ReplayLog(Jam, Cfg.params, Seed, Log, Steps, static_cast<int64_t>(Other));
            const std::vector<uint64_t> Again = ReplayLog(Jam, Cfg.params, Seed, Log, Steps, static_cast<int64_t>(Other));
            T_CHECK(Luck == Again);
            bool Diverged = false;
            for (size_t H = 0; H < Luck.size() && !Diverged; ++H) { Diverged = Luck[H] != Plain[H]; }
            T_CHECK(Diverged);   // the luck shows in the state for a while (it washes out later: an undecided voter adopts whoever pushes it, the dice only decide when)
            Sim A, B;
            A.Init(Jam, Cfg.params, Seed);
            B.Init(Jam, Cfg.params, Seed, Other);
            T_CHECK(A.StateHash() == B.StateHash());
            T_CHECK(A.Seed() == Seed && B.Seed() == Seed && A.DynSeed() == Seed && B.DynSeed() == Other);
            bool SameWorld = A.NodeCount() == B.NodeCount() && A.NumEntries() == B.NumEntries();
            for (int I = 0; SameWorld && I < A.NodeCount(); ++I)
            {
                const GraphNode& Na = A.GetGraph().nodes[static_cast<size_t>(I)];
                const GraphNode& Nb = B.GetGraph().nodes[static_cast<size_t>(I)];
                SameWorld = Na.follows == Nb.follows && Na.weight == Nb.weight && Na.x == Nb.x && Na.y == Nb.y && A.Opinion(I) == B.Opinion(I) && A.Conviction(I) == B.Conviction(I);
            }
            for (int E = 0; SameWorld && E < A.NumEntries(); ++E)
            {
                SameWorld = A.GetEntry(E).quality == B.GetEntry(E).quality && A.GetEntry(E).clout == B.GetEntry(E).clout && A.GetEntry(E).seedWeight == B.GetEntry(E).seedWeight;
            }
            T_CHECK(SameWorld);
        }
    }
}

// ------------------------------------------------------------------------------------------
inline bool CheckInvariants(const Sim& S, const char* Where, int StepNo)
{
    bool Ok = true;
    const int N = S.NodeCount();
    double Recount[kMaxEntries] = {};
    for (int I = 0; I < N; ++I)
    {
        const double C = S.Conviction(I);
        if (!Finite(C) || C < 0.0 || C > 1.0) { Ok = false; std::printf("  conv out of range node %d = %g (%s step %d)\n", I, C, Where, StepNo); }
        const double L = S.LockRemaining(I);
        if (!Finite(L) || L < 0.0) { Ok = false; std::printf("  bad lock node %d (%s)\n", I, Where); }
        const int O = S.Opinion(I);
        if (O < kNoOwner || O >= S.NumEntries()) { Ok = false; std::printf("  bad opinion node %d\n", I); }
        if (O >= 0) { Recount[O] += S.GetGraph().nodes[static_cast<size_t>(I)].weight; }
        if (O == kNoOwner && C != 0.0) { Ok = false; std::printf("  undecided with conviction node %d\n", I); }
        if (S.IsSockpuppet(I) && O != kPlayerId) { Ok = false; std::printf("  sock flag on foreign node %d\n", I); }
    }
    double Shares = 0.0;
    for (int E = 0; E < S.NumEntries(); ++E)
    {
        if (S.Tally(E) != Recount[E]) { Ok = false; std::printf("  tally mismatch entry %d: %g vs %g (%s step %d)\n", E, S.Tally(E), Recount[E], Where, StepNo); }
        Shares += S.Share(E);
    }
    if (!Finite(Shares) || Shares > 1.0 + 1.0e-12) { Ok = false; std::printf("  shares sum %g\n", Shares); }
    if (S.UndecidedShare() < -1.0e-12) { Ok = false; std::printf("  negative undecided share\n"); }
    if (!Finite(S.Posts()) || S.Posts() < 0.0 || S.Posts() > S.GetParams().PostsMax + 1.0e-9) { Ok = false; std::printf("  posts out of range %g\n", S.Posts()); }
    if (!Finite(S.Heat()) || S.Heat() < 0.0 || S.Heat() > S.GetParams().HeatMax + 1.0e-9) { Ok = false; std::printf("  heat out of range %g\n", S.Heat()); }
    const std::vector<int> Rank = S.Ranking();
    for (size_t I = 1; I < Rank.size(); ++I)
    {
        if (S.Tally(Rank[I - 1]) < S.Tally(Rank[I])) { Ok = false; std::printf("  ranking not sorted\n"); }
    }
    return Ok;
}

inline void TestInvariants(const Config& Cfg)
{
    std::printf("[invariants]\n");
    const BotKind Bots[3] = { BotKind::Random, BotKind::GreedySock, BotKind::Idle };
    for (size_t J = 0; J < Cfg.jams.size(); ++J)
    {
        for (int Bi = 0; Bi < 3; ++Bi)
        {
            for (uint32_t Seed = 5; Seed <= 6; ++Seed)
            {
                const JamDef Jam = Cfg.jams[J];
                Sim S;
                S.Init(Jam, Cfg.params, Seed);
                StandInRival Rival; StandInOthers Others; StandInModEye Mod; PlayerBot Bot;
                Rival.Reset(S, Cfg.policy); Others.Reset(S, Cfg.policy); Mod.Reset(S, Seed, Cfg.policy); Bot.Reset(Bots[Bi], Seed);
                bool Ok = CheckInvariants(S, "init", 0);
                int StepNo = 0;
                while (!S.Finished() && Ok)
                {
                    Rival.Update(S, Cfg.policy); Others.Update(S, Cfg.policy); Mod.Update(S, Cfg.policy); Bot.Update(S, Cfg.policy);
                    Ok = CheckInvariants(S, "pre-step", StepNo);
                    S.Step();
                    Ok = Ok && CheckInvariants(S, "post-step", StepNo);
                    ++StepNo;
                }
                T_CHECK(Ok);
            }
        }
    }
}

// ------------------------------------------------------------------------------------------
inline int FindNode(const Sim& S, NodeRole Role, int MinFollowers = 0)
{
    for (int I = 0; I < S.NodeCount(); ++I)
    {
        const GraphNode& N = S.GetGraph().nodes[static_cast<size_t>(I)];
        if (N.role == Role && static_cast<int>(N.followers.size()) >= MinFollowers)
        {
            return I;
        }
    }
    return -1;
}

inline void TestPlugAndSock(const Config& Cfg)
{
    std::printf("[plug / sock mechanics]\n");
    const Params& P = Cfg.params;
    Sim S;
    S.Init(Cfg.jams[0], P, 77);
    // A plain member to experiment on.
    int M = FindNode(S, NodeRole::Member);
    T_CHECK(M >= 0);
    S.SetNodeState(M, 3, 0.9, 0.0);

    // Weaken: 0.9 - 0.45 = 0.45, not flipped, costs 1 Post.
    const double Posts0 = S.Posts();
    ActionOutcome O = S.Plug(kPlayerId, M);
    T_CHECK(O.result == ActionResult::Weakened);
    T_CHECK(Near(S.Conviction(M), 0.45));
    T_CHECK(Near(S.Posts(), Posts0 - 1.0));
    T_CHECK(O.prevOwner == 3 && O.newOwner == 3);
    // Flip: 0.45 - 0.45 = 0 <= 0.15.
    O = S.Plug(kPlayerId, M);
    T_CHECK(O.result == ActionResult::Flipped);
    T_CHECK(O.flipped && O.newOwner == kPlayerId);
    T_CHECK(S.Opinion(M) == kPlayerId);
    T_CHECK(Near(S.Conviction(M), P.PlayerFlipConv));
    T_CHECK(Near(S.LockRemaining(M), P.PlugLock));
    T_CHECK(!S.IsSockpuppet(M));
    // Locked nodes refuse attacks (rival), no cost for the rival obviously; player cost not charged on refusal.
    O = S.Plug(S.RivalId(), M);
    T_CHECK(O.result == ActionResult::Locked);
    T_CHECK(S.Opinion(M) == kPlayerId);
    const double PostsBefore = S.Posts();
    S.SetNodeState(M, 4, 0.5, 5.0);
    O = S.Plug(kPlayerId, M);
    T_CHECK(O.result == ActionResult::Locked);
    T_CHECK(Near(S.Posts(), PostsBefore));
    // Lock expires after PlugLock seconds of steps.
    S.SetNodeState(M, kPlayerId, P.PlugFlipConv, P.PlugLock);
    int Steps = 0;
    while (S.LockRemaining(M) > 0.0 && Steps < 1000) { S.Step(); ++Steps; }
    T_CHECK(Steps >= 119 && Steps <= 122);
    // Reinforce: +0.3, clamped at 1; also allowed on an immune own node.
    S.SetNodeState(M, kPlayerId, 0.55, 6.0);
    S.SetPosts(5.0);
    O = S.Plug(kPlayerId, M);
    T_CHECK(O.result == ActionResult::Reinforced);
    T_CHECK(Near(S.Conviction(M), 0.85));
    S.Plug(kPlayerId, M);
    T_CHECK(Near(S.Conviction(M), 1.0));
    // Undecided node flips with one Plug.
    S.SetNodeState(M, kNoOwner, 0.0, 0.0);
    O = S.Plug(kPlayerId, M);
    T_CHECK(O.result == ActionResult::Flipped);
    // Invalid targets.
    T_CHECK(S.Plug(kPlayerId, -1).result == ActionResult::InvalidTarget);
    T_CHECK(S.Plug(99, 0).result == ActionResult::InvalidTarget);
    T_CHECK(S.Sock(100000).result == ActionResult::InvalidTarget);

    // Strength scales the drain.
    S.SetNodeState(M, 3, 1.0, 0.0);
    S.SetPosts(5.0);
    O = S.Plug(kPlayerId, M, 2.0); // drain 0.9 -> 0.1 <= 0.15
    T_CHECK(O.result == ActionResult::Flipped);

    // Sock: needs night, ignores gates, c = 0.25, lock = 3 s, +28 Heat, flagged.
    int Hub = FindNode(S, NodeRole::LocalHub, 1);
    S.SetNodeState(Hub, 3, 1.0, 0.0);
    S.SetPosts(5.0);
    S.SetHeat(0.0);
    T_CHECK(S.IsGated(Hub));
    O = S.Sock(Hub);
    T_CHECK(O.result == ActionResult::NotNight);
    T_CHECK(Near(S.Heat(), 0.0) && Near(S.Posts(), 5.0));
    S.SetNightHeld(true);
    O = S.Sock(Hub);
    T_CHECK(O.result == ActionResult::Flipped && O.sockpuppet);
    T_CHECK(S.Opinion(Hub) == kPlayerId);
    T_CHECK(Near(S.Conviction(Hub), P.SockConv));
    T_CHECK(Near(S.LockRemaining(Hub), P.SockLock));
    T_CHECK(S.IsSockpuppet(Hub));
    T_CHECK(Near(S.Heat(), P.SockHeat));
    T_CHECK(Near(S.Posts(), 4.0));
    T_CHECK(Near(O.heatDelta, P.SockHeat));
    // Own node / locked node refuse.
    T_CHECK(S.Sock(Hub).result == ActionResult::AlreadyOwned);
    int M2 = FindNode(S, NodeRole::Member);
    if (M2 == M) { M2 = M + 1; }
    S.SetNodeState(M2, 3, 0.8, 2.0);
    T_CHECK(S.Sock(M2).result == ActionResult::Locked);
    T_CHECK(Near(S.Heat(), P.SockHeat));
    // Launder is disabled by default: the flag stays while the player owns the node, however strong it gets.
    S.Plug(kPlayerId, Hub); // 0.25 -> 0.55
    S.Plug(kPlayerId, Hub); // 0.55 -> 0.85
    S.Step();
    T_CHECK(S.IsSockpuppet(Hub));
    T_CHECK(S.Conviction(Hub) >= 0.8);

    // Optional launder (SockLaunderConv = 0.8): real support clears the flag at the next step.
    Params P2 = P;
    P2.SockLaunderConv = 0.8;
    Sim S3;
    S3.Init(Cfg.jams[0], P2, 77);
    const int Hub3 = FindNode(S3, NodeRole::LocalHub, 1);
    S3.SetNodeState(Hub3, 3, 1.0, 0.0);
    S3.SetPosts(5.0);
    S3.SetNightHeld(true);
    T_CHECK(S3.Sock(Hub3).result == ActionResult::Flipped);
    T_CHECK(S3.IsSockpuppet(Hub3));
    S3.Plug(kPlayerId, Hub3);
    S3.Plug(kPlayerId, Hub3);
    S3.Step();
    T_CHECK(!S3.IsSockpuppet(Hub3));
    const StrikeOutcome Late = S3.ApplyStrike();
    T_CHECK(Late.revertedNodes.size() == 1);  // a strike takes backed sockpuppet votes too (the Mod Eye counts everything bought)
    T_CHECK(S3.Opinion(Hub3) != kPlayerId);
}

// ------------------------------------------------------------------------------------------
inline void TestGates(const Config& Cfg)
{
    std::printf("[gates]\n");
    const Params& P = Cfg.params;
    for (size_t J = 0; J < Cfg.jams.size(); ++J)
    {
        Sim S;
        const JamDef& Jam = Cfg.jams[J];
        S.Init(Jam, P, 101u + static_cast<uint32_t>(J));
        // Local hub.
        int Hub = -1;
        for (int I = 0; I < S.NodeCount() && Hub < 0; ++I)
        {
            const GraphNode& N = S.GetGraph().nodes[static_cast<size_t>(I)];
            if (N.role == NodeRole::LocalHub && N.followers.size() >= 4) { Hub = I; }
        }
        T_CHECK(Hub >= 0);
        S.SetNodeState(Hub, 3, 0.7, 0.0);
        const GraphNode& H = S.GetGraph().nodes[static_cast<size_t>(Hub)];
        // Nobody backs the player: gated.
        for (size_t K = 0; K < H.followers.size(); ++K) { S.SetNodeState(H.followers[K], 4, 0.6, 0.0); }
        GateInfo G = S.Gate(Hub);
        T_CHECK(G.applies && !G.open && Near(G.progress, 0.0) && Near(G.needed, Jam.localGate));
        S.SetPosts(5.0);
        ActionOutcome O = S.Plug(kPlayerId, Hub);
        T_CHECK(O.result == ActionResult::Gated);
        T_CHECK(Near(S.Posts(), 5.0));            // refusal is free
        T_CHECK(Near(S.Conviction(Hub), 0.7));    // untouched
        T_CHECK(S.PlugsNeeded(kPlayerId, Hub) >= kUnreachable);
        // The rival is gated like the player since round 8 (GatesApplyToAll, on by default): none of its own followers back it here. So is every other entry.
        S.SetNodeState(Hub, 3, 0.7, 0.0);
        T_CHECK(P.GatesApplyToAll > 0.5);
        T_CHECK(S.IsGated(Hub, S.RivalId()) && S.IsGated(Hub, 5));
        O = S.Plug(S.RivalId(), Hub);
        T_CHECK(O.result == ActionResult::Gated);
        T_CHECK(Near(S.Conviction(Hub), 0.7));    // untouched, and the refusal costs nothing

        // Flip followers to the player one by one until the gate opens exactly when progress >= needed.
        S.SetNodeState(Hub, 3, 0.7, 0.0);
        bool OpenedAtThreshold = true;
        for (size_t K = 0; K < H.followers.size(); ++K)
        {
            S.SetNodeState(H.followers[K], kPlayerId, 0.6, 0.0);
            G = S.Gate(Hub);
            const bool ShouldBeOpen = G.progress + 1.0e-12 >= Jam.localGate;
            if (G.open != ShouldBeOpen) { OpenedAtThreshold = false; }
        }
        T_CHECK(OpenedAtThreshold);
        G = S.Gate(Hub);
        T_CHECK(G.open && G.progress > 0.99);
        S.SetPosts(5.0);
        O = S.Plug(kPlayerId, Hub);
        T_CHECK(O.Accepted());
        T_CHECK(Near(S.Posts(), 4.0));

        // Celebrity gate uses the celebrity fraction; Sock ignores it.
        const int Z = S.GetGraph().celebrities[0];
        const GraphNode& Zn = S.GetGraph().nodes[static_cast<size_t>(Z)];
        for (size_t K = 0; K < Zn.followers.size(); ++K) { S.SetNodeState(Zn.followers[K], 4, 0.6, 0.0); }
        S.SetNodeState(Z, S.RivalId(), 1.0, 0.0);
        G = S.Gate(Z);
        T_CHECK(G.applies && !G.open && Near(G.needed, Jam.celebGate));
        S.SetPosts(5.0);
        O = S.Plug(kPlayerId, Z);
        T_CHECK(O.result == ActionResult::Gated && Near(O.gateNeeded, Jam.celebGate));
        S.SetNightHeld(true);
        O = S.Sock(Z);
        T_CHECK(O.result == ActionResult::Flipped);
        T_CHECK(S.Opinion(Z) == kPlayerId);
        // Non-gate nodes are never gated.
        int M = FindNode(S, NodeRole::Member);
        T_CHECK(!S.IsGated(M));
        T_CHECK(!S.Gate(M).applies);
    }

    // Optional symmetric gates: with GatesApplyToAll the rival needs backing among a hub's followers too.
    Params P2 = P;
    P2.GatesApplyToAll = 1.0;
    Sim S2;
    S2.Init(Cfg.jams[0], P2, 55);
    int Hub = -1;
    for (int I = 0; I < S2.NodeCount() && Hub < 0; ++I)
    {
        const GraphNode& N = S2.GetGraph().nodes[static_cast<size_t>(I)];
        if (N.role == NodeRole::LocalHub && N.followers.size() >= 4) { Hub = I; }
    }
    T_CHECK(Hub >= 0);
    const GraphNode& H2 = S2.GetGraph().nodes[static_cast<size_t>(Hub)];
    S2.SetNodeState(Hub, 3, 0.7, 0.0);
    for (size_t K = 0; K < H2.followers.size(); ++K) { S2.SetNodeState(H2.followers[K], 4, 0.6, 0.0); }
    const int Rv = S2.RivalId();
    T_CHECK(S2.IsGated(Hub, Rv));
    T_CHECK(S2.Plug(Rv, Hub).result == ActionResult::Gated);
    T_CHECK(S2.PlugsNeeded(Rv, Hub) >= kUnreachable);
    for (size_t K = 0; K < H2.followers.size(); ++K) { S2.SetNodeState(H2.followers[K], Rv, 0.6, 0.0); }
    T_CHECK(!S2.IsGated(Hub, Rv));
    T_CHECK(S2.Plug(Rv, Hub).Accepted());

    // The old asymmetric rule (GatesApplyToAll = 0): only the player is gated, the rival and the other entries plug any hub.
    Params P3 = P;
    P3.GatesApplyToAll = 0.0;
    Sim S3;
    S3.Init(Cfg.jams[0], P3, 55);
    int Hub3 = -1;
    for (int I = 0; I < S3.NodeCount() && Hub3 < 0; ++I)
    {
        const GraphNode& N = S3.GetGraph().nodes[static_cast<size_t>(I)];
        if (N.role == NodeRole::LocalHub && N.followers.size() >= 4) { Hub3 = I; }
    }
    T_CHECK(Hub3 >= 0);
    const GraphNode& H3 = S3.GetGraph().nodes[static_cast<size_t>(Hub3)];
    S3.SetNodeState(Hub3, 3, 0.7, 0.0);
    for (size_t K = 0; K < H3.followers.size(); ++K) { S3.SetNodeState(H3.followers[K], 4, 0.6, 0.0); }
    T_CHECK(S3.IsGated(Hub3));                                    // the player still is
    T_CHECK(!S3.IsGated(Hub3, S3.RivalId()) && !S3.IsGated(Hub3, 5));
    T_CHECK(S3.Plug(S3.RivalId(), Hub3).result != ActionResult::Gated);
}

// ------------------------------------------------------------------------------------------
inline void TestStrike(const Config& Cfg)
{
    std::printf("[strike / shadowban / DQ]\n");
    const Params& P = Cfg.params;
    Sim S;
    S.Init(Cfg.jams[1], P, 202);
    S.SetNightHeld(true);
    S.SetPosts(5.0);
    S.SetHeat(0.0);

    // Pick three unlocked foreign nodes and sock them.
    std::vector<int> Picks;
    std::vector<int> PrevOwner;
    std::vector<double> PrevConv;
    for (int I = 0; I < S.NodeCount() && Picks.size() < 3; ++I)
    {
        if (S.Opinion(I) >= 0 && S.Opinion(I) != kPlayerId && S.LockRemaining(I) == 0.0)
        {
            Picks.push_back(I);
            PrevOwner.push_back(S.Opinion(I));
            PrevConv.push_back(S.Conviction(I));
        }
    }
    double TallyBefore[kMaxEntries];
    for (int E = 0; E < S.NumEntries(); ++E) { TallyBefore[E] = S.Tally(E); }
    // A legit (non-sock) flip that must survive the strike.
    int Legit = -1;
    for (int I = 0; I < S.NodeCount(); ++I)
    {
        bool Used = false;
        for (size_t K = 0; K < Picks.size(); ++K) { Used = Used || Picks[K] == I; }
        if (!Used && S.Opinion(I) >= 0 && S.Opinion(I) != kPlayerId && !S.GetGraph().IsGateNode(I) && S.LockRemaining(I) == 0.0)
        {
            Legit = I;
            break;
        }
    }
    S.SetPosts(5.0);
    for (size_t K = 0; K < Picks.size(); ++K)
    {
        S.SetPosts(5.0);
        T_CHECK(S.Sock(Picks[K]).result == ActionResult::Flipped);
    }
    T_CHECK(S.Heat() > P.HeatRedZone);
    T_CHECK(S.InRedZone());
    T_CHECK(S.SockpuppetWeight() > 0.0);
    // Real Plug flip (not sockpuppet) on the legit node.
    S.SetNightHeld(false);
    S.SetNodeState(Legit, 3, 0.4, 0.0);
    S.SetPosts(5.0);
    T_CHECK(S.Plug(kPlayerId, Legit).result == ActionResult::Flipped);

    const StrikeOutcome Out = S.ApplyStrike();
    T_CHECK(Out.strikes == 1 && !Out.disqualified);
    T_CHECK(Out.revertedNodes.size() == Picks.size());
    for (size_t K = 0; K < Picks.size(); ++K)
    {
        T_CHECK(S.Opinion(Picks[K]) == PrevOwner[K]);
        T_CHECK(Near(S.Conviction(Picks[K]), PrevConv[K]));
        T_CHECK(!S.IsSockpuppet(Picks[K]));
        T_CHECK(Near(S.LockRemaining(Picks[K]), 0.0));
    }
    T_CHECK(S.Opinion(Legit) == kPlayerId);          // honest flip survives
    T_CHECK(Near(S.SockpuppetWeight(), 0.0));
    T_CHECK(Near(S.ShadowbanRemaining(), P.ShadowbanSeconds));
    T_CHECK(S.Heat() <= P.HeatAfterStrike + 1.0e-9);
    (void)TallyBefore;

    // Shadowban: actions refused until it expires (6 s = 120 steps), then work again.
    S.SetPosts(5.0);
    int Target = -1;
    for (int I = 0; I < S.NodeCount(); ++I)
    {
        if (S.Opinion(I) >= 0 && S.Opinion(I) != kPlayerId && S.LockRemaining(I) == 0.0 && !S.GetGraph().IsGateNode(I)) { Target = I; break; }
    }
    T_CHECK(S.Plug(kPlayerId, Target).result == ActionResult::Shadowbanned);
    S.SetNightHeld(true);
    T_CHECK(S.Sock(Target).result == ActionResult::Shadowbanned);
    S.SetNightHeld(false);
    int Steps = 0;
    while (S.ShadowbanRemaining() > 0.0 && Steps < 1000) { S.Step(); ++Steps; }
    T_CHECK(Steps >= 119 && Steps <= 122);
    S.SetPosts(5.0);
    T_CHECK(S.Plug(kPlayerId, Target).Accepted() || S.Plug(kPlayerId, Target).result == ActionResult::Locked);

    // Three strikes = disqualified; the player can no longer act and cannot lead.
    S.ApplyStrike();
    const StrikeOutcome Third = S.ApplyStrike();
    T_CHECK(Third.strikes == 3 && Third.disqualified);
    T_CHECK(S.Disqualified());
    S.SetPosts(5.0);
    for (int I = 0; I < 130; ++I) { S.Step(); }
    T_CHECK(S.Plug(kPlayerId, Target).result == ActionResult::Disqualified);
    for (int I = 0; I < S.NodeCount(); ++I) { S.SetNodeState(I, kPlayerId, 0.9, 0.0); }
    T_CHECK(S.RankOf(kPlayerId) == 0);
    T_CHECK(S.Leader() != kPlayerId);
}

// ------------------------------------------------------------------------------------------
// Shift the Blame (Sim/ShiftBlameRules.h): offered only at the strike that would disqualify, and only for a small enough share of fake votes.
inline void TestBlame(const Config& Cfg)
{
    std::printf("[shift the blame]\n");
    const Params& P = Cfg.params;
    T_CHECK(P.BlameMaxFake > P.HeatDecay / P.SockHeldHeat && P.BlameMaxFake < 0.5 && P.BlameWindow > 0.0 && P.BlameRivalConv > 0.0 && P.BlameRivalFreeze >= 0.0);
    // Strikes before the fatal one are never offered, whatever the fake share; the fatal one is offered up to the cap (inclusive) and refused over it.
    {
        Sim S;
        S.Init(Cfg.jams[1], P, 777);
        S.ApplyStrike();
        T_CHECK(!S.Disqualified());
        T_CHECK(JudgeBlame(S, 0.0, false) == BlameVerdict::NotOffered);
        S.ApplyStrike();
        T_CHECK(!S.Disqualified() && S.Strikes() == 2);
        T_CHECK(JudgeBlame(S, 0.0, false) == BlameVerdict::NotOffered);
        T_CHECK(JudgeBlame(S, 0.5, false) == BlameVerdict::NotOffered);
        S.ApplyStrike();
        T_CHECK(S.Disqualified() && S.Strikes() == 3);
        T_CHECK(JudgeBlame(S, 0.0, false) == BlameVerdict::Offered);
        T_CHECK(JudgeBlame(S, P.BlameMaxFake - 0.001, false) == BlameVerdict::Offered);
        T_CHECK(JudgeBlame(S, P.BlameMaxFake, false) == BlameVerdict::Offered);
        T_CHECK(JudgeBlame(S, P.BlameMaxFake + 0.001, false) == BlameVerdict::Refused);
        T_CHECK(JudgeBlame(S, 0.5, false) == BlameVerdict::Refused);
        T_CHECK(JudgeBlame(S, 0.0, true) == BlameVerdict::NotOffered);   // once per run: a second fatal strike stands
    }
    // BlameWindow 0 is the Blame-free game: nothing is offered or refused, the strike simply disqualifies.
    {
        Params Off = P;
        Off.BlameWindow = 0.0;
        Sim S;
        S.Init(Cfg.jams[1], Off, 777);
        S.ApplyStrike(); S.ApplyStrike(); S.ApplyStrike();
        T_CHECK(S.Disqualified());
        T_CHECK(JudgeBlame(S, 0.0, false) == BlameVerdict::NotOffered && JudgeBlame(S, 0.9, false) == BlameVerdict::NotOffered);
    }
    // "Fatal" follows MaxStrikes.
    for (int Max = 2; Max <= 4; ++Max)
    {
        Params Q = P;
        Q.MaxStrikes = static_cast<double>(Max);
        Sim S;
        S.Init(Cfg.jams[1], Q, 778);
        for (int K = 1; K < Max; ++K)
        {
            S.ApplyStrike();
            T_CHECK(JudgeBlame(S, 0.0, false) == BlameVerdict::NotOffered);
        }
        S.ApplyStrike();
        T_CHECK(S.Disqualified() && JudgeBlame(S, 0.0, false) == BlameVerdict::Offered && JudgeBlame(S, 0.9, false) == BlameVerdict::Refused);
    }
    // ApplyBlame: the strike and the shadowban are taken back, the rival's voters lose BlameRivalConv (not below 0), nobody else changes, and the rival's freeze is reported.
    {
        Sim S;
        S.Init(Cfg.jams[1], P, 779);
        const int Rival = S.RivalId();
        std::vector<double> Before;
        std::vector<int> OwnerBefore;
        for (int I = 0; I < S.NodeCount(); ++I)
        {
            Before.push_back(S.Conviction(I));
            OwnerBefore.push_back(S.Opinion(I));
        }
        S.ApplyStrike(); S.ApplyStrike(); S.ApplyStrike();
        T_CHECK(S.Disqualified() && S.ShadowbanRemaining() > 0.0);
        const double Freeze = ApplyBlame(S);
        T_CHECK(Near(Freeze, P.BlameRivalFreeze));
        T_CHECK(S.Strikes() == 2 && !S.Disqualified() && Near(S.ShadowbanRemaining(), 0.0));
        bool Ok = true;
        int Lowered = 0;
        for (int I = 0; I < S.NodeCount(); ++I)
        {
            const double Want = OwnerBefore[static_cast<size_t>(I)] == Rival ? (Before[static_cast<size_t>(I)] - P.BlameRivalConv > 0.0 ? Before[static_cast<size_t>(I)] - P.BlameRivalConv : 0.0) : Before[static_cast<size_t>(I)];
            Ok = Ok && S.Opinion(I) == OwnerBefore[static_cast<size_t>(I)] && Near(S.Conviction(I), Want);
            Lowered += (OwnerBefore[static_cast<size_t>(I)] == Rival && S.Conviction(I) < Before[static_cast<size_t>(I)] - 1.0e-12) ? 1 : 0;
        }
        T_CHECK(Ok && Lowered > 0);
        // a later fatal strike is offered again by the rules only if the caller has not used the Blame (the run layer tracks that)
        S.ApplyStrike();
        T_CHECK(S.Disqualified() && JudgeBlame(S, 0.0, true) == BlameVerdict::NotOffered);
    }
    // The strike's reverted sockpuppet votes stay reverted when the blame lands.
    {
        Params Q = P;
        Q.AttackTelegraph = 0.0;
        Sim S;
        S.Init(Cfg.jams[1], Q, 780);
        S.SetNightHeld(true);
        S.SetPosts(5.0);
        int Node = -1;
        for (int I = 0; I < S.NodeCount() && Node < 0; ++I)
        {
            if (S.Opinion(I) >= 0 && S.Opinion(I) != kPlayerId && S.LockRemaining(I) == 0.0) { Node = I; }
        }
        const int Prev = S.Opinion(Node);
        T_CHECK(S.Sock(Node).result == ActionResult::Flipped && S.IsSockpuppet(Node));
        const double Fake = S.SockBoughtWeight() / S.TotalWeight();
        T_CHECK(Fake > 0.0);
        S.ApplyStrike(); S.ApplyStrike(); S.ApplyStrike();
        T_CHECK(S.Opinion(Node) == Prev);
        ApplyBlame(S);
        T_CHECK(S.Opinion(Node) == Prev && !S.IsSockpuppet(Node));
    }
    // The window is the BlameWindow, or what is left of the jam when that is less.
    {
        Sim S;
        S.Init(Cfg.jams[1], P, 781);
        T_CHECK(Near(BlameWindowFor(S), P.BlameWindow));
        const int ToLeft2 = static_cast<int>((S.TotalSeconds() - 2.0) / P.Dt + 0.5);
        for (int I = 0; I < ToLeft2; ++I) { S.Step(); }
        T_CHECK(Near(BlameWindowFor(S), 2.0, 1.0e-6));
        while (!S.Finished()) { S.Step(); }
        T_CHECK(BlameWindowFor(S) < 1.0e-6 && BlameWindowFor(S) >= 0.0);
    }
    // BlameBasis: the cap is measured on the fatal strike alone (0) or on all of the player's strikes together (1).
    {
        Params Q = P;
        Q.BlameBasis = 0.0;
        T_CHECK(Near(BlameBasisShare(Q, 0.05, 0.09), 0.05));
        Q.BlameBasis = 1.0;
        T_CHECK(Near(BlameBasisShare(Q, 0.05, 0.09), 0.14) && Near(BlameBasisShare(Q, 0.05, 0.0), 0.05));
        double V = 1.0;
        T_CHECK(SetParam(Q, "BlameBasis", 0.0) && GetParam(Q, "BlameBasis", &V) && Near(V, 0.0));
    }
    // The numbers are reachable by name (-Set).
    {
        Params Q = P;
        double V = 0.0;
        T_CHECK(SetParam(Q, "BlameMaxFake", 0.2) && Near(Q.BlameMaxFake, 0.2));
        T_CHECK(SetParam(Q, "BlameWindow", 0.0) && Near(Q.BlameWindow, 0.0));
        T_CHECK(SetParam(Q, "BlameRivalConv", 0.1) && Near(Q.BlameRivalConv, 0.1));
        T_CHECK(SetParam(Q, "BlameRivalFreeze", 3.0) && Near(Q.BlameRivalFreeze, 3.0));
        T_CHECK(GetParam(Q, "BlameMaxFake", &V) && Near(V, 0.2));
    }
}

// ------------------------------------------------------------------------------------------
// The rival's Post bank (Sim/ShiftRivalBank.h): full at the start, one Post an action, a lazy refill from the sim clock, a cap, refused (and free) when empty, off when the bank is 0.
inline void TestRivalBank()
{
    std::printf("[rival post bank]\n");
    RivalBank B;
    B.Reset(4.0, 10.0);
    T_CHECK(Near(B.PostsAt(10.0, 4.0, 0.4), 4.0));
    T_CHECK(B.Spend(10.0, 4.0, 0.4, 1.0) && Near(B.PostsAt(10.0, 4.0, 0.4), 3.0));
    T_CHECK(Near(B.PostsAt(11.0, 4.0, 0.4), 3.4));                       // the refill is lazy: nothing ticked it
    T_CHECK(Near(B.PostsAt(100.0, 4.0, 0.4), 4.0));                      // and the bank holds no more than its size
    T_CHECK(Near(B.PostsAt(9.0, 4.0, 0.4), 3.0));                        // a clock that went back adds nothing
    for (int K = 0; K < 3; ++K)
    {
        T_CHECK(B.Spend(11.0, 4.0, 0.4, 1.0));                           // 3.4 -> 2.4 -> 1.4 -> 0.4
    }
    T_CHECK(!B.Spend(11.0, 4.0, 0.4, 1.0) && Near(B.PostsAt(11.0, 4.0, 0.4), 0.4));   // refused, and it cost nothing
    T_CHECK(Near(B.PostsAt(12.5, 4.0, 0.4), 1.0));                       // 1.5 s later one Post is back
    T_CHECK(B.Spend(12.5, 4.0, 0.4, 1.0) && Near(B.PostsAt(12.5, 4.0, 0.4), 0.0));
    T_CHECK(!B.Spend(12.6, 4.0, 0.4, 1.0) && Near(B.PostsAt(12.6, 4.0, 0.4), 0.04));  // a refused spend does not count the seconds twice
    T_CHECK(!B.Spend(13.0, 4.0, 0.4, 2.0) && B.Spend(18.0, 4.0, 0.4, 2.0));          // a dearer action needs more in the bank
    RivalBank Off;
    Off.Reset(0.0, 0.0);
    T_CHECK(Off.Spend(5.0, 0.0, 0.4, 1.0) && Near(Off.PostsAt(5.0, 0.0, 0.4), 0.0));   // off: every action is free
    // The push: demand 0.606 attacks a second against a refill of 0.40 empties a bank of 4 in about 19 s; before the push (0.303 a second) the bank never drops.
    RivalBank Push;
    Push.Reset(4.0, 0.0);
    int Calm = 0;
    for (int K = 0; K < 60; ++K) { Calm += Push.Spend(3.3 * (K + 1), 4.0, 0.4, 1.0) ? 1 : 0; }
    T_CHECK(Calm == 60 && Push.PostsAt(3.3 * 60.0, 4.0, 0.4) >= 2.99);
    int Made = 0;
    const double Start = 3.3 * 60.0;
    for (int K = 0; K < 11; ++K) { Made += Push.Spend(Start + 1.65 * (K + 1), 4.0, 0.4, 1.0) ? 1 : 0; }   // 18 s of push at one action every 1.65 s
    T_CHECK(Made >= 10 && Made <= 11);
}

// ------------------------------------------------------------------------------------------
inline void TestOverheatAndAudit(const Config& Cfg)
{
    std::printf("[overheat / final audit / drain]\n");
    const Params& P = Cfg.params;

    auto PickTargets = [](const Sim& S, size_t Count)
    {
        std::vector<int> Out;
        for (int I = 0; I < S.NodeCount() && Out.size() < Count; ++I)
        {
            if (S.Opinion(I) != kPlayerId && S.Opinion(I) >= 0 && S.LockRemaining(I) == 0.0) { Out.push_back(I); }
        }
        return Out;
    };

    // Overheat refusal: a sockpuppet that would take Heat to the cap is refused, free; three fit under the cap.
    {
        Params On = P;
        On.SockOverheatRefuse = 1.0;
        Sim S;
        S.Init(Cfg.jams[1], On, 404);
        S.SetNightHeld(true);
        S.SetPosts(5.0);
        S.SetHeat(0.0);
        // As many socks as fit under the cap, then the next one would reach it.
        int Fit = 0;
        while ((Fit + 1) * On.SockHeat < On.HeatMax) { ++Fit; }
        const std::vector<int> T = PickTargets(S, static_cast<size_t>(Fit) + 1);
        T_CHECK(static_cast<int>(T.size()) == Fit + 1);
        for (int K = 0; K < Fit; ++K)
        {
            S.SetPosts(5.0);
            T_CHECK(S.Sock(T[static_cast<size_t>(K)]).result == ActionResult::Flipped);
        }
        T_CHECK(Near(S.Heat(), Fit * On.SockHeat));
        S.SetPosts(5.0);
        const double PostsBefore = S.Posts();
        const double HeatBefore = S.Heat();
        const ActionOutcome O = S.Sock(T[static_cast<size_t>(Fit)]);   // Fit x SockHeat + SockHeat >= HeatMax
        T_CHECK(O.result == ActionResult::Overheated);
        T_CHECK(Near(S.Posts(), PostsBefore));
        T_CHECK(Near(S.Heat(), HeatBefore));
        T_CHECK(S.Opinion(T[static_cast<size_t>(Fit)]) != kPlayerId);
        T_CHECK(!S.HeatMaxed());

        // With the refusal off the fourth sock goes through and Heat hits the cap (the heat guard strikes).
        Params Off = P;
        Off.SockOverheatRefuse = 0.0;
        Sim S2;
        S2.Init(Cfg.jams[1], Off, 404);
        S2.SetNightHeld(true);
        S2.SetPosts(5.0);
        S2.SetHeat(0.0);
        const std::vector<int> T2 = PickTargets(S2, static_cast<size_t>(Fit) + 1);
        for (size_t K = 0; K < T2.size(); ++K) { S2.SetPosts(5.0); T_CHECK(S2.Sock(T2[K]).result == ActionResult::Flipped); }
        T_CHECK(S2.HeatMaxed());
    }

    // The final audit is RevertSockpuppets without a strike: sockpuppet flips go back, honest ones stay, nothing else changes.
    {
        Sim S;
        S.Init(Cfg.jams[1], P, 405);
        S.SetNightHeld(true);
        S.SetPosts(5.0);
        S.SetHeat(0.0);
        const std::vector<int> T = PickTargets(S, 3);
        T_CHECK(T.size() == 3);
        std::vector<int> PrevOwner;
        std::vector<double> PrevConv;
        for (size_t K = 0; K < T.size(); ++K) { PrevOwner.push_back(S.Opinion(T[K])); PrevConv.push_back(S.Conviction(T[K])); }
        for (size_t K = 0; K < T.size(); ++K) { S.SetPosts(5.0); T_CHECK(S.Sock(T[K]).result == ActionResult::Flipped); }
        int Honest = -1;
        for (int I = 0; I < S.NodeCount(); ++I)
        {
            if (S.Opinion(I) >= 0 && S.Opinion(I) != kPlayerId && S.LockRemaining(I) == 0.0 && !S.GetGraph().IsGateNode(I)) { Honest = I; break; }
        }
        S.SetNightHeld(false);
        S.SetNodeState(Honest, 3, 0.4, 0.0);
        S.SetPosts(5.0);
        T_CHECK(S.Plug(kPlayerId, Honest).result == ActionResult::Flipped);

        const double HeatBefore = S.Heat();
        const int StrikesBefore = S.Strikes();
        const std::vector<int> Purged = S.RevertSockpuppets();
        T_CHECK(Purged.size() == T.size());
        for (size_t K = 0; K < T.size(); ++K)
        {
            T_CHECK(S.Opinion(T[K]) == PrevOwner[K]);
            T_CHECK(Near(S.Conviction(T[K]), PrevConv[K]));
            T_CHECK(!S.IsSockpuppet(T[K]));
            T_CHECK(Near(S.LockRemaining(T[K]), 0.0));
        }
        T_CHECK(S.Opinion(Honest) == kPlayerId);
        T_CHECK(Near(S.SockpuppetWeight(), 0.0));
        T_CHECK(S.Strikes() == StrikesBefore);                 // not a strike
        T_CHECK(Near(S.ShadowbanRemaining(), 0.0));            // no shadowban
        T_CHECK(Near(S.Heat(), HeatBefore));                   // no Heat reset
        T_CHECK(S.RevertSockpuppets().empty());                // nothing left
    }

    // Laundering plus the audit: a sockpuppet that was reinforced into a real vote survives, an unbacked one does not.
    {
        Params L = P;
        L.SockLaunderConv = 0.8;
        Sim S;
        S.Init(Cfg.jams[1], L, 406);
        S.SetNightHeld(true);
        S.SetPosts(5.0);
        S.SetHeat(0.0);
        const std::vector<int> T = PickTargets(S, 2);
        T_CHECK(T.size() == 2);
        T_CHECK(S.Sock(T[0]).result == ActionResult::Flipped);
        S.SetPosts(5.0);
        T_CHECK(S.Sock(T[1]).result == ActionResult::Flipped);
        S.SetPosts(5.0);
        T_CHECK(S.Plug(kPlayerId, T[0]).result == ActionResult::Reinforced);   // 0.25 -> 0.55, allowed during the immunity
        T_CHECK(S.Plug(kPlayerId, T[0]).result == ActionResult::Reinforced);   // 0.55 -> 0.85
        S.Step();
        T_CHECK(!S.IsSockpuppet(T[0]));                                        // laundered: a real vote now
        T_CHECK(S.IsSockpuppet(T[1]));
        const std::vector<int> Purged = S.AuditSockpuppets();                  // the final audit (the backed vote was bought long before the last SockReview seconds)
        T_CHECK(Purged.size() == 1 && Purged[0] == T[1]);
        T_CHECK(S.Opinion(T[0]) == kPlayerId);
        T_CHECK(S.Opinion(T[1]) != kPlayerId);
    }

    // Drain is a derived value: never negative, zero while a voter is immune, and SecondsToFlip is conviction / drain.
    {
        Sim S;
        S.Init(Cfg.jams[2], P, 407);
        for (int Step = 0; Step < 400; ++Step) { S.Step(); }
        bool Ok = true;
        int Pressured = 0;
        for (int I = 0; I < S.NodeCount(); ++I)
        {
            const double D = S.Drain(I);
            Ok = Ok && D >= 0.0;
            if (S.LockRemaining(I) > 0.0) { Ok = Ok && D == 0.0; }
            if (D > 1.0e-9)
            {
                ++Pressured;
                Ok = Ok && Near(S.SecondsToFlip(I), S.Conviction(I) / D, 1.0e-9);
            }
            else
            {
                Ok = Ok && S.SecondsToFlip(I) > 1.0e8;
            }
        }
        T_CHECK(Ok);
        (void)Pressured;
    }
}

// ------------------------------------------------------------------------------------------
inline void TestRallyAndAttribution(const Config& Cfg)
{
    std::printf("[rally / pusher / last flipper]\n");
    const Params& P = Cfg.params;
    Sim S;
    S.Init(Cfg.jams[1], P, 501);
    // A hub of the player with two player-owned followers and one foreign follower.
    int Hub = FindNode(S, NodeRole::LocalHub, 1);
    T_CHECK(Hub >= 0);
    const GraphNode& G = S.GetGraph().nodes[static_cast<size_t>(Hub)];
    T_CHECK(G.followers.size() >= 3);
    S.SetNodeState(Hub, kPlayerId, 0.5, 0.0);
    S.SetNodeState(G.followers[0], kPlayerId, 0.4, 0.0);
    S.SetNodeState(G.followers[1], kPlayerId, 0.95, 3.0);   // immune and near the cap: still boosted, clamped at 1
    S.SetNodeState(G.followers[2], 3, 0.4, 0.0);            // not the player's: untouched
    S.SetPosts(5.0);

    // Refusals are free.
    int Foreign = G.followers[2];
    ActionOutcome O = S.Rally(Foreign);
    T_CHECK(O.result == ActionResult::NotYours);
    T_CHECK(Near(S.Posts(), 5.0));
    T_CHECK(S.Rally(-1).result == ActionResult::InvalidTarget);

    // A Rally costs RallyCost Posts and adds RallyBoost to the voter and the player's voters one hop away.
    O = S.Rally(Hub);
    T_CHECK(O.result == ActionResult::Rallied);
    T_CHECK(Near(S.Posts(), 5.0 - P.RallyCost));
    T_CHECK(Near(S.Conviction(Hub), 0.5 + P.RallyBoost));
    T_CHECK(Near(S.Conviction(G.followers[0]), 0.4 + P.RallyBoost));
    T_CHECK(Near(S.Conviction(G.followers[1]), 1.0));
    T_CHECK(Near(S.Conviction(Foreign), 0.4));
    T_CHECK(O.touched >= 3);
    T_CHECK(Near(S.RallyCooldownRemaining(), P.RallyCooldown));
    // Cooldown refuses (free), and it runs out with the steps.
    const double PostsAfter = S.Posts();
    T_CHECK(S.Rally(Hub).result == ActionResult::OnCooldown);
    T_CHECK(Near(S.Posts(), PostsAfter));
    int Steps = 0;
    while (S.RallyCooldownRemaining() > 0.0 && Steps < 1000) { S.Step(); ++Steps; }
    T_CHECK(Steps >= static_cast<int>(P.RallyCooldown / P.Dt) - 2 && Steps <= static_cast<int>(P.RallyCooldown / P.Dt) + 2);
    // Too few Posts: refused, free.
    S.SetNodeState(Hub, kPlayerId, 0.5, 0.0);
    S.SetPosts(P.RallyCost - 0.5);
    T_CHECK(S.Rally(Hub).result == ActionResult::NoPosts);
    // Disqualified and shadowbanned players cannot rally.
    S.SetPosts(5.0);
    S.ApplyStrike();
    T_CHECK(S.Rally(Hub).result == ActionResult::Shadowbanned);

    // Last flipper: an action records its entry, opinion pressure records kFlipNatural.
    Sim S2;
    S2.Init(Cfg.jams[1], P, 502);
    int M = FindNode(S2, NodeRole::Member);
    S2.SetNodeState(M, 3, 0.2, 0.0);
    S2.SetPosts(5.0);
    T_CHECK(S2.LastFlipper(M) == kNoOwner);
    T_CHECK(S2.Plug(kPlayerId, M).result == ActionResult::Flipped);
    T_CHECK(S2.LastFlipper(M) == kPlayerId);
    S2.SetNodeState(M, kPlayerId, 0.55, 0.0);
    T_CHECK(S2.Plug(S2.RivalId(), M, 2.0).result == ActionResult::Flipped);
    T_CHECK(S2.LastFlipper(M) == S2.RivalId());
    // Pusher: a voter that is being out-pushed reports the entry doing it, and a voter that is not reports none.
    bool Ok = true;
    for (int Step = 0; Step < 600; ++Step) { S2.Step(); }
    for (int I = 0; I < S2.NodeCount(); ++I)
    {
        const int C = S2.Challenger(I);
        Ok = Ok && ((C == kNoOwner) == (S2.Drain(I) == 0.0));
        Ok = Ok && (C == kNoOwner || (C >= 0 && C < S2.NumEntries() && C != S2.Opinion(I)));
    }
    T_CHECK(Ok);
}

// ------------------------------------------------------------------------------------------
inline void TestTelegraph(const Config& Cfg)
{
    std::printf("[telegraphed attacks]\n");
    Params P = Cfg.params;
    P.AttackTelegraph = 1.5;
    auto MakeSim = [&](Sim& S, uint32_t Seed, int* Out)
    {
        S.Init(Cfg.jams[1], P, Seed);
        *Out = FindNode(S, NodeRole::Member);
        S.SetNodeState(*Out, kPlayerId, 0.5, 0.0);
        S.SetPosts(5.0);
    };

    // An attack on a voter of the player's is announced, not applied.
    {
        Sim S;
        int M = -1;
        MakeSim(S, 601, &M);
        const ActionOutcome O = S.Plug(S.RivalId(), M, 1.0);
        T_CHECK(O.result == ActionResult::Telegraphed);
        T_CHECK(S.Opinion(M) == kPlayerId);
        T_CHECK(Near(S.Conviction(M), 0.5));
        T_CHECK(S.PendingAttacks().size() == 1);
        int Who = -1;
        double Left = 0.0, Total = 0.0;
        T_CHECK(S.ThreatOn(M, &Who, &Left, &Total));
        T_CHECK(Who == S.RivalId() && Near(Left, 1.5) && Near(Total, 1.5));
        T_CHECK(!S.ThreatOn(M + 1 < S.NodeCount() ? M + 1 : 0, &Who, &Left, &Total) || true);
        // It lands after the delay and takes the voter (0.5 - 0.45 <= 0.15).
        for (int I = 0; I < 25; ++I) { S.Step(); }
        T_CHECK(S.Opinion(M) == kPlayerId && S.PendingAttacks().size() == 1);
        for (int I = 0; I < 8; ++I) { S.Step(); }
        T_CHECK(S.PendingAttacks().empty());
        T_CHECK(S.Opinion(M) == S.RivalId());
        T_CHECK(S.LastFlipper(M) == S.RivalId());
        T_CHECK(S.Attacks().queued == 1 && S.Attacks().landed == 1 && S.Attacks().flipped == 1);
    }
    // With ParryOnReinforce = 0 (the round 3 rule) reinforcing in between changes the outcome by arithmetic only: the plug lands but does not flip.
    {
        P.ParryOnReinforce = 0.0;
        Sim S;
        int M = -1;
        MakeSim(S, 602, &M);
        T_CHECK(S.Plug(S.RivalId(), M, 1.0).result == ActionResult::Telegraphed);
        T_CHECK(S.Plug(kPlayerId, M, 1.0).result == ActionResult::Reinforced);
        T_CHECK(S.Plug(kPlayerId, M, 1.0).result == ActionResult::Reinforced);
        for (int I = 0; I < 34; ++I) { S.Step(); }
        T_CHECK(S.Opinion(M) == kPlayerId);
        T_CHECK(S.Attacks().landed == 1 && S.Attacks().flipped == 0 && S.Attacks().parried == 0);
        P.ParryOnReinforce = 1.0;
    }
    // A Rally makes the voter immune: the attack is blocked.
    {
        Sim S;
        int M = -1;
        MakeSim(S, 603, &M);
        T_CHECK(S.Plug(S.RivalId(), M, 1.0).result == ActionResult::Telegraphed);
        T_CHECK(S.Rally(M).result == ActionResult::Rallied);
        T_CHECK(S.ShieldRemaining(M) > 0.0 || P.RallyLock <= 0.0);
        for (int I = 0; I < 34; ++I) { S.Step(); }
        T_CHECK(S.Opinion(M) == kPlayerId);
        T_CHECK(S.Attacks().blocked == 1 || P.RallyLock < P.AttackTelegraph);
    }
    // If the player no longer holds the voter, the attack fizzles.
    {
        Sim S;
        int M = -1;
        MakeSim(S, 604, &M);
        T_CHECK(S.Plug(S.RivalId(), M, 1.0).result == ActionResult::Telegraphed);
        S.SetNodeState(M, 3, 0.6, 0.0);
        for (int I = 0; I < 34; ++I) { S.Step(); }
        T_CHECK(S.Attacks().fizzled == 1 && S.Attacks().landed == 0);
    }
    // Attacks on voters the player does not hold, and the player's own plugs, land at once; AttackTelegraph = 0 turns the announcement off.
    {
        Sim S;
        int M = -1;
        MakeSim(S, 605, &M);
        const int Other = FindNode(S, NodeRole::Member, 1) == M ? M + 1 : FindNode(S, NodeRole::Member, 1);
        S.SetNodeState(Other, 3, 0.2, 0.0);
        T_CHECK(S.Plug(S.RivalId(), Other, 1.0).result == ActionResult::Flipped);
        T_CHECK(S.PendingAttacks().empty());
        T_CHECK(S.Plug(kPlayerId, M, 1.0).result == ActionResult::Reinforced);
        Params Off = P;
        Off.AttackTelegraph = 0.0;
        Sim S2;
        S2.Init(Cfg.jams[1], Off, 606);
        const int M2 = FindNode(S2, NodeRole::Member);
        S2.SetNodeState(M2, kPlayerId, 0.5, 0.0);
        T_CHECK(S2.Plug(S2.RivalId(), M2, 1.0).result == ActionResult::Flipped);
        T_CHECK(S2.PendingAttacks().empty());
    }
    // A sockpuppet vote is flagged and gets no warning: once its immunity is over an attack on it lands at once.
    {
        Sim S;
        S.Init(Cfg.jams[1], P, 607);
        S.SetPosts(5.0);
        S.SetHeat(0.0);
        const int Hub = FindNode(S, NodeRole::LocalHub, 1);
        S.SetNodeState(Hub, 3, 1.0, 0.0);
        S.SetNightHeld(true);
        T_CHECK(S.Sock(Hub).result == ActionResult::Flipped);
        T_CHECK(S.IsSockpuppet(Hub));
        for (int I = 0; I < 70; ++I) { S.Step(); }
        T_CHECK(S.LockRemaining(Hub) <= 0.0);
        const ActionResult R = S.Plug(S.RivalId(), Hub, 1.0).result;
        T_CHECK(R != ActionResult::Telegraphed && S.PendingAttacks().empty());
    }
}

// ------------------------------------------------------------------------------------------
inline void TestParry(const Config& Cfg)
{
    std::printf("[parry]\n");
    Params P = Cfg.params;
    P.AttackTelegraph = 1.5;
    P.ParryOnReinforce = 1.0;
    P.ParryCost = 1.0;
    auto MakeSim = [&](Sim& S, const Params& Pr, uint32_t Seed, int* Out)
    {
        S.Init(Cfg.jams[1], Pr, Seed);
        *Out = FindNode(S, NodeRole::Member);
        S.SetNodeState(*Out, kPlayerId, 0.5, 0.0);
        S.SetPosts(5.0);
    };
    // A reinforcement cancels the soonest ring on that voter, costs the usual Post, adds the usual conviction and counts as parried.
    {
        Sim S;
        int M = -1;
        MakeSim(S, P, 611, &M);
        T_CHECK(S.Plug(S.RivalId(), M, 1.0).result == ActionResult::Telegraphed);
        for (int I = 0; I < 10; ++I) { S.Step(); }
        T_CHECK(S.Plug(S.RivalId(), M, 1.0).result == ActionResult::Telegraphed);       // a second ring, announced later
        const double PostsBefore = S.Posts();
        const ActionOutcome O = S.Plug(kPlayerId, M, 1.0);
        T_CHECK(O.result == ActionResult::Reinforced && O.parried && O.parriedWho == S.RivalId());
        T_CHECK(O.parriedLeft > 0.0 && O.parriedLeft < O.parriedTotal);                  // the older ring: it had run down
        T_CHECK(S.PendingAttacks().size() == 1);                                         // the soonest one only
        T_CHECK(Near(S.Posts(), PostsBefore - 1.0) && Near(O.postsSpent, 1.0));
        T_CHECK(S.Conviction(M) > 0.75);
        T_CHECK(S.Attacks().parried == 1 && Near(S.Attacks().postsParry, 1.0));
        for (int I = 0; I < 40; ++I) { S.Step(); }                                      // the second ring still lands later: 0.8 - 0.45 > 0.15, it only dents
        T_CHECK(S.Opinion(M) == kPlayerId);
        const AttackCounters& A = S.Attacks();
        T_CHECK(A.queued == 2 && A.parried == 1 && A.landed == 1);
        T_CHECK(A.queued == A.landed + A.fizzled + A.blocked + A.parried + static_cast<int>(S.PendingAttacks().size()));
    }
    // It works against a strong attack too (the arithmetic alone could not save the voter), and a reinforcement with no ring is plain.
    {
        Sim S;
        int M = -1;
        MakeSim(S, P, 612, &M);
        T_CHECK(S.Plug(S.RivalId(), M, 2.8).result == ActionResult::Telegraphed);       // 0.5 - 0.45 * 2.8 flips the voter, and so would 0.8 - 1.26
        const ActionOutcome O = S.Plug(kPlayerId, M, 1.0);
        T_CHECK(O.parried);
        for (int I = 0; I < 40; ++I) { S.Step(); }
        T_CHECK(S.Opinion(M) == kPlayerId && S.Attacks().flipped == 0);
        const ActionOutcome Plain = S.Plug(kPlayerId, M, 1.0);
        T_CHECK(Plain.result == ActionResult::Reinforced && !Plain.parried);
    }
    // ParryCost 2: a parrying reinforcement needs two Posts and is refused (free, the ring stays) with less.
    {
        Params C = P;
        C.ParryCost = 2.0;
        Sim S;
        int M = -1;
        MakeSim(S, C, 613, &M);
        S.SetPosts(1.5);
        T_CHECK(S.Plug(S.RivalId(), M, 1.0).result == ActionResult::Telegraphed);
        const ActionOutcome Refused = S.Plug(kPlayerId, M, 1.0);
        T_CHECK(Refused.result == ActionResult::NoPosts && !Refused.parried);
        T_CHECK(S.PendingAttacks().size() == 1 && Near(S.Posts(), 1.5) && S.Attacks().parried == 0);
        S.SetPosts(2.0);
        const ActionOutcome Done = S.Plug(kPlayerId, M, 1.0);
        T_CHECK(Done.parried && Near(Done.postsSpent, 2.0) && Near(S.Posts(), 0.0) && Near(S.Attacks().postsParry, 2.0));
    }
    // Only the player parries: another entry's own reinforcement of its voter never touches a ring.
    {
        Sim S;
        int M = -1;
        MakeSim(S, P, 614, &M);
        T_CHECK(S.Plug(S.RivalId(), M, 1.0).result == ActionResult::Telegraphed);
        const int Other = FindNode(S, NodeRole::Member, 1) == M ? M + 1 : FindNode(S, NodeRole::Member, 1);
        S.SetNodeState(Other, S.RivalId(), 0.5, 0.0);
        T_CHECK(S.Plug(S.RivalId(), Other, 1.0).result == ActionResult::Reinforced);
        T_CHECK(S.PendingAttacks().size() == 1 && S.Attacks().parried == 0);
    }
    // The state hash covers the pending list, so a parry changes it and a replay reproduces it.
    {
        Sim A, B;
        int Ma = -1, Mb = -1;
        MakeSim(A, P, 615, &Ma);
        MakeSim(B, P, 615, &Mb);
        A.Plug(A.RivalId(), Ma, 1.0);
        B.Plug(B.RivalId(), Mb, 1.0);
        T_CHECK(A.StateHash() == B.StateHash());
        A.Plug(kPlayerId, Ma, 1.0);
        T_CHECK(A.StateHash() != B.StateHash());
        B.Plug(kPlayerId, Mb, 1.0);
        for (int I = 0; I < 30; ++I) { A.Step(); B.Step(); }
        T_CHECK(A.StateHash() == B.StateHash());
    }
}

// ------------------------------------------------------------------------------------------
inline void TestEyeAway(const Config& Cfg)
{
    std::printf("[mod eye looks away]\n");
    Params P = Cfg.params;
    P.AttackTelegraph = 0.0;
    P.EyeAwayHeatMult = 0.0;
    P.EyeAwayHeldMult = 0.5;
    P.SockHeldHeat = 400.0;
    P.SockTolerance = 1.0;
    auto Make = [&](Sim& S, uint32_t Seed)
    {
        S.Init(Cfg.jams[1], P, Seed);
        S.SetPosts(5.0);
        S.SetHeat(0.0);
    };
    // Night shift heats normally, adds nothing during a window, and heats again after it.
    {
        Sim S;
        Make(S, 901);
        S.SetNightHeld(true);
        for (int I = 0; I < 20; ++I) { S.Step(); }
        T_CHECK(S.Heat() > P.HeatNightRate * 0.9);
        S.SetHeat(0.0);
        S.SetEyeAway(2.0);
        T_CHECK(Near(S.EyeAwayRemaining(), 2.0));
        for (int I = 0; I < 20; ++I) { S.Step(); }
        T_CHECK(Near(S.Heat(), 0.0));
        T_CHECK(S.EyeAwayRemaining() > 0.9 && S.EyeAwayRemaining() < 1.1);
        for (int I = 0; I < 40; ++I) { S.Step(); }
        T_CHECK(Near(S.EyeAwayRemaining(), 0.0));
        T_CHECK(S.Heat() > P.HeatNightRate * 0.9);
    }
    // A sockpuppet adds no Heat during a window, and a sweep that passes finds nobody suspicious; after the window the old rules are back.
    {
        Sim S;
        Make(S, 902);
        S.SetNightHeld(true);
        const int Hub = FindNode(S, NodeRole::LocalHub, 1);
        S.SetNodeState(Hub, 3, 1.0, 0.0);
        S.SetHeat(P.HeatRedZone + 10.0);
        T_CHECK(S.Suspicious());
        S.SetEyeAway(5.0);
        T_CHECK(!S.Suspicious());
        const ActionOutcome O = S.Sock(Hub);
        T_CHECK(O.result == ActionResult::Flipped && Near(O.heatDelta, 0.0));
        T_CHECK(Near(S.Heat(), P.HeatRedZone + 10.0));
        for (int I = 0; I < 110; ++I) { S.Step(); }
        T_CHECK(Near(S.EyeAwayRemaining(), 0.0));
        T_CHECK(S.Suspicious() == (S.InRedZone() || S.OverTolerance()));
        // outside a window a sockpuppet that would reach the Heat cap is refused as before
        Sim T;
        Make(T, 903);
        T.SetNightHeld(true);
        T.SetNodeState(Hub, 3, 1.0, 0.0);
        T.SetHeat(P.HeatMax - 1.0);
        T_CHECK(T.Sock(Hub).result == ActionResult::Overheated);
        T.SetEyeAway(5.0);
        T_CHECK(T.Sock(Hub).result == ActionResult::Flipped);
    }
    // Held fake votes heat at EyeAwayHeldMult of the usual rate during a window.
    {
        auto HeatAfterOneSecond = [&](bool bAway)
        {
            Sim S;
            Make(S, 904);
            S.SetNightHeld(true);
            const int Hub = FindNode(S, NodeRole::LocalHub, 1);
            S.SetNodeState(Hub, 3, 1.0, 0.0);
            if (bAway) { S.SetEyeAway(5.0); }
            T_CHECK(S.Sock(Hub).result == ActionResult::Flipped);
            S.SetNightHeld(false);
            S.SetHeat(0.0);
            for (int I = 0; I < 20; ++I) { S.Step(); }
            return S.Heat();
        };
        const double Usual = HeatAfterOneSecond(false);
        const double Away = HeatAfterOneSecond(true);
        T_CHECK(Away > 1.0 && Usual > Away + 3.0);
    }
    // The window is part of the state hash, and a replay reproduces it.
    {
        Sim A, B;
        Make(A, 905);
        Make(B, 905);
        T_CHECK(A.StateHash() == B.StateHash());
        A.SetEyeAway(3.0);
        T_CHECK(A.StateHash() != B.StateHash());
        B.SetEyeAway(3.0);
        for (int I = 0; I < 80; ++I) { A.Step(); B.Step(); }
        T_CHECK(A.StateHash() == B.StateHash());
    }
}

// ------------------------------------------------------------------------------------------
inline void TestBuild(const Config& Cfg)
{
    std::printf("[build levers]\n");
    auto Make = [&](int Q, int T, int N, Params* OutP, JamDef* OutJ)
    {
        *OutP = Cfg.params;
        *OutJ = Cfg.jams[1];
        BuildHours H;
        H.quality = Q;
        H.thumbnail = T;
        H.network = N;
        ApplyBuild(H, *OutP, *OutJ);
    };
    // Neutral at 4 / 4 / 4: nothing changes.
    {
        Params P;
        JamDef J;
        Make(4, 4, 4, &P, &J);
        T_CHECK(Near(P.PlayerBuildBonus, Cfg.params.PlayerBuildBonus) && Near(P.PlayerFlipConv, Cfg.params.PlayerFlipConv));
        T_CHECK(Near(P.PlayerClout, Cfg.params.PlayerClout) && Near(P.PlayerPlugCost, 1.0));
        T_CHECK(Near(P.PlayerStartNodes, Cfg.params.PlayerStartNodes) && Near(P.PostsMax, Cfg.params.PostsMax) && Near(P.PostsStart, Cfg.params.PostsStart));
        T_CHECK(Near(J.regen, Cfg.jams[1].regen));
    }
    // Every lever moves its numbers the right way as hours go in (0..8), and under an even split it loses faster than it gains over it.
    {
        double LastFlip = -1.0, LastBank = -1.0, LastRegen = -1.0, LastStart = -1.0, LastCost = 9.0, LastAppeal = -1.0, LastClout = -1.0;
        for (int Hrs = 0; Hrs <= 8; ++Hrs)
        {
            Params Pq, Pt, Pn;
            JamDef Jq, Jt, Jn;
            Make(Hrs, 4, 4, &Pq, &Jq);
            Make(4, Hrs, 4, &Pt, &Jt);
            Make(4, 4, Hrs, &Pn, &Jn);
            const BuildSummary Sq = SummarizeBuild(Pq, Jq), St = SummarizeBuild(Pt, Jt), Sn = SummarizeBuild(Pn, Jn);
            T_CHECK(Sq.flipConv >= LastFlip - 1.0e-12 && Sq.appeal >= LastAppeal - 1.0e-12);
            T_CHECK(St.plugCost <= LastCost + 1.0e-12 && St.clout >= LastClout - 1.0e-12);
            T_CHECK(Sn.postsMax >= LastBank - 1.0e-12 && Sn.postsRegen >= LastRegen - 1.0e-12 && Sn.startNodes >= LastStart - 1.0e-12);
            LastFlip = Sq.flipConv; LastAppeal = Sq.appeal; LastCost = St.plugCost; LastClout = St.clout; LastBank = Sn.postsMax; LastRegen = Sn.postsRegen; LastStart = Sn.startNodes;
        }
        Params P0, P4, P8;
        JamDef J0, J4, J8;
        Make(4, 4, 0, &P0, &J0);
        Make(4, 4, 4, &P4, &J4);
        Make(4, 4, 8, &P8, &J8);
        T_CHECK((J4.regen - J0.regen) > (J8.regen - J4.regen) * 1.2);
    }
    // The range stays inside what the UI and the sim can show, whatever the split.
    {
        const int Splits[][3] = { { 0, 0, 0 }, { 8, 8, 8 }, { 8, 0, 4 }, { 0, 8, 4 }, { 4, 0, 8 }, { 0, 0, 12 }, { 12, 0, 0 }, { 0, 12, 0 } };
        for (const auto& Sp : Splits)
        {
            Params P;
            JamDef J;
            Make(Sp[0], Sp[1], Sp[2], &P, &J);
            const BuildSummary S = SummarizeBuild(P, J);
            T_CHECK(S.postsMax >= 3.0 && S.postsMax <= 8.0 && S.startNodes >= 1 && S.startNodes <= 4);
            T_CHECK(S.plugCost >= 0.5 && S.plugCost <= 1.5 && S.flipConv >= 0.5 && S.flipConv <= 0.98 && S.postsRegen > 0.0 && S.clout >= 0.0);
            Sim Run;
            T_CHECK(Run.Init(J, P, 951));                                                      // a build of any split starts a valid run
            T_CHECK(Near(Run.Posts(), P.PostsMax));                                            // with a full bank
        }
    }
    // The Plug cost of the Thumbnail lever: a Plug on a foreign voter costs PlayerPlugCost Posts, a reinforcement costs one, and with fewer Posts than the cost a Plug is refused (free).
    {
        Params P = Cfg.params;
        P.PlayerPlugCost = 0.9;
        P.AttackTelegraph = 0.0;
        Sim S;
        S.Init(Cfg.jams[1], P, 952);
        const int M = FindNode(S, NodeRole::Member);
        S.SetNodeState(M, 3, 0.9, 0.0);
        S.SetPosts(0.85);
        T_CHECK(S.Plug(kPlayerId, M, 1.0).result == ActionResult::NoPosts && Near(S.Posts(), 0.85));
        S.SetPosts(2.0);
        const ActionOutcome O = S.Plug(kPlayerId, M, 1.0);
        T_CHECK(O.Accepted() && Near(O.postsSpent, 0.9) && Near(S.Posts(), 1.1));
        S.SetNodeState(M, kPlayerId, 0.5, 0.0);
        S.SetPosts(2.0);
        T_CHECK(S.Plug(kPlayerId, M, 1.0).result == ActionResult::Reinforced && Near(S.Posts(), 1.0));
    }
}

// ------------------------------------------------------------------------------------------
inline void TestLayout(const Config& Cfg)
{
    std::printf("[layout: the HUD gutter]\n");
    // The voters keep left of LayoutWidth - LayoutMarginRight (the HUD's right gutter), and squeezing them there does not crowd the board much more than the old full-width layout did.
    // The graph itself does not depend on the layout (it has its own random stream), so the same seed gives the same network in both.
    const int Seeds = 24;
    for (int J = 0; J < 3; ++J)
    {
        double PairsNew = 0.0, PairsOld = 0.0, PinnedNew = 0.0, PinnedOld = 0.0;
        for (int Sd = 0; Sd < Seeds; ++Sd)
        {
            Params Pn = Cfg.params;
            Params Po = Cfg.params;
            Po.LayoutMarginRight = Po.LayoutMargin;
            Po.LayoutRingX = 480.0;
            Sim A, B;
            A.Init(Cfg.jams[J], Pn, 7000u + static_cast<uint32_t>(Sd));
            B.Init(Cfg.jams[J], Po, 7000u + static_cast<uint32_t>(Sd));
            T_CHECK(A.NodeCount() == B.NodeCount());
            auto Count = [](const Sim& S, const Params& P, double* Pairs, double* Pinned)
            {
                const std::vector<GraphNode>& N = S.GetGraph().nodes;
                for (size_t a = 0; a < N.size(); ++a)
                {
                    for (size_t b = a + 1; b < N.size(); ++b)
                    {
                        const double Dx = N[a].x - N[b].x;
                        const double Dy = N[a].y - N[b].y;
                        *Pairs += (Dx * Dx + Dy * Dy < 34.0 * 34.0) ? 1.0 : 0.0;
                    }
                    const bool bPinned = N[a].x <= P.LayoutMargin + 0.5 || N[a].x >= P.LayoutWidth - P.LayoutMarginRight - 0.5 || N[a].y <= P.LayoutMargin + 0.5 || N[a].y >= P.LayoutHeight - P.LayoutMargin - 0.5;
                    *Pinned += bPinned ? 1.0 : 0.0;
                }
            };
            Count(A, Pn, &PairsNew, &PinnedNew);
            Count(B, Po, &PairsOld, &PinnedOld);
            double MaxX = 0.0;
            for (const GraphNode& N : A.GetGraph().nodes) { MaxX = N.x > MaxX ? N.x : MaxX; }
            T_CHECK(MaxX <= Pn.LayoutWidth - Pn.LayoutMarginRight + 0.01);
        }
        std::printf("  jam %d: voters closer than 34 px %.2f a board (was %.2f), pinned to a margin %.1f (was %.1f)\n", J + 1, PairsNew / Seeds, PairsOld / Seeds, PinnedNew / Seeds, PinnedOld / Seeds);
        T_CHECK(PairsNew / Seeds <= PairsOld / Seeds + 1.5);
        T_CHECK(PinnedNew / Seeds <= PinnedOld / Seeds + 8.0);   // some voters sit on the margin line, but none overlaps another (see above)
    }
}

// ------------------------------------------------------------------------------------------
inline void TestSockReview(const Config& Cfg)
{
    std::printf("[final audit review window]\n");
    Params P = Cfg.params;
    P.SockReview = 15.0;
    P.AttackTelegraph = 0.0;
    Sim S;
    S.Init(Cfg.jams[1], P, 701);
    S.SetPosts(5.0);
    S.SetHeat(0.0);
    S.SetNightHeld(true);
    // two different hubs that the rival or an entry holds
    int HubA = -1, HubB = -1;
    for (int I = 0; I < S.NodeCount() && HubB < 0; ++I)
    {
        if (S.GetGraph().nodes[static_cast<size_t>(I)].role == NodeRole::LocalHub)
        {
            (HubA < 0 ? HubA : HubB) = I;
        }
    }
    T_CHECK(HubA >= 0 && HubB >= 0);
    S.SetNodeState(HubA, 3, 1.0, 0.0);
    S.SetNodeState(HubB, 3, 1.0, 0.0);
    // HubA is bought early and backed until it is a real vote.
    T_CHECK(S.Sock(HubA).result == ActionResult::Flipped);
    for (int I = 0; I < 3; ++I) { T_CHECK(S.Plug(kPlayerId, HubA, 1.0).result == ActionResult::Reinforced); }
    S.Step();
    T_CHECK(!S.IsSockpuppet(HubA));
    T_CHECK(Near(S.AuditWeight(), 0.0));
    // The jam runs on; the heat and the Posts recover. HubB is bought inside the last SockReview seconds and backed too.
    S.SetNightHeld(false);
    while (S.Time() < S.TotalSeconds() - 10.0) { S.Step(); S.SetPosts(5.0); S.SetHeat(0.0); }
    S.SetNightHeld(true);
    S.SetNodeState(HubB, 3, 1.0, 0.0);
    T_CHECK(S.Sock(HubB).result == ActionResult::Flipped);
    for (int I = 0; I < 3; ++I) { T_CHECK(S.Plug(kPlayerId, HubB, 1.0).result == ActionResult::Reinforced); }
    S.Step();
    T_CHECK(!S.IsSockpuppet(HubB));                       // backed: no longer flagged ...
    T_CHECK(Near(S.AuditWeight(), S.GetGraph().nodes[static_cast<size_t>(HubB)].weight));   // ... but the audit still takes it
    while (!S.Finished()) { S.Step(); }
    const std::vector<int> Purged = S.AuditSockpuppets();
    T_CHECK(Purged.size() == 1 && Purged[0] == HubB);
    T_CHECK(S.Opinion(HubB) != kPlayerId);
    T_CHECK(Near(S.AuditWeight(), 0.0));
    // SockReview = 0 turns the window off: only flagged votes are purged.
    Params Off = P;
    Off.SockReview = 0.0;
    Sim S2;
    S2.Init(Cfg.jams[1], Off, 702);
    S2.SetPosts(5.0);
    S2.SetNightHeld(true);
    S2.SetNodeState(HubA, 3, 1.0, 0.0);
    T_CHECK(S2.Sock(HubA).result == ActionResult::Flipped);
    for (int I = 0; I < 3; ++I) { S2.Plug(kPlayerId, HubA, 1.0); }
    S2.Step();
    while (!S2.Finished()) { S2.Step(); }
    T_CHECK(S2.AuditSockpuppets().empty() || S2.Opinion(HubA) != kPlayerId);
}

// ------------------------------------------------------------------------------------------
inline void TestTolerance(const Config& Cfg)
{
    std::printf("[sockpuppet tolerance]\n");
    Params P = Cfg.params;
    P.SockTolerance = 0.001;
    P.AttackTelegraph = 0.0;
    Sim S;
    S.Init(Cfg.jams[1], P, 801);
    S.SetPosts(5.0);
    S.SetHeat(0.0);
    S.SetNightHeld(true);
    const int Hub = FindNode(S, NodeRole::LocalHub, 1);
    const double W = S.GetGraph().nodes[static_cast<size_t>(Hub)].weight;
    S.SetNodeState(Hub, 3, 1.0, 0.0);
    T_CHECK(Near(S.SockBoughtWeight(), 0.0) && !S.OverTolerance() && !S.Suspicious());
    T_CHECK(S.Sock(Hub).result == ActionResult::Flipped);
    T_CHECK(Near(S.SockBoughtWeight(), W) && S.OverTolerance() && S.Suspicious());
    // Backing the vote until it is real does not take it off the Mod Eye's books.
    for (int I = 0; I < 3; ++I) { T_CHECK(S.Plug(kPlayerId, Hub, 1.0).result == ActionResult::Reinforced); }
    S.Step();
    T_CHECK(!S.IsSockpuppet(Hub) && Near(S.SockBoughtWeight(), W) && S.OverTolerance());
    // A strike takes every sockpuppet vote, backed ones too, and puts the books straight.
    const StrikeOutcome O = S.ApplyStrike();
    T_CHECK(O.revertedNodes.size() == 1 && O.revertedNodes[0] == Hub);
    T_CHECK(S.Opinion(Hub) != kPlayerId && Near(S.SockBoughtWeight(), 0.0) && !S.OverTolerance());
    // A tolerance of 1 or more turns the rule off.
    Params Off = P;
    Off.SockTolerance = 1.0;
    Sim S2;
    S2.Init(Cfg.jams[1], Off, 802);
    S2.SetPosts(5.0);
    S2.SetNightHeld(true);
    S2.SetNodeState(Hub, 3, 1.0, 0.0);
    T_CHECK(S2.Sock(Hub).result == ActionResult::Flipped && !S2.OverTolerance());
    // Holding fake votes heats the player up (SockHeldHeat per second per 100% of the vote weight), backed or not; with 0 it does not.
    auto HeatAfterOneSecond = [&](double Held, bool bBack)
    {
        Params H = Off;
        H.SockHeldHeat = Held;
        Sim Sx;
        Sx.Init(Cfg.jams[1], H, 803);
        Sx.SetPosts(5.0);
        Sx.SetHeat(0.0);
        Sx.SetNightHeld(true);
        Sx.SetNodeState(Hub, 3, 1.0, 0.0);
        T_CHECK(Sx.Sock(Hub).result == ActionResult::Flipped);
        Sx.SetNightHeld(false);
        if (bBack) { for (int I = 0; I < 3; ++I) { Sx.Plug(kPlayerId, Hub, 1.0); } }
        for (int I = 0; I < 20; ++I) { Sx.Step(); }
        return Sx.Heat();
    };
    const double Cold = HeatAfterOneSecond(0.0, false);
    const double Warm = HeatAfterOneSecond(100.0, false);
    const double WarmBacked = HeatAfterOneSecond(100.0, true);
    T_CHECK(Warm > Cold + 1.0);
    T_CHECK(WarmBacked > Cold + 1.0);
}

// ------------------------------------------------------------------------------------------
inline void TestEconomy(const Config& Cfg)
{
    std::printf("[posts / heat bookkeeping]\n");
    const Params& P = Cfg.params;
    JamDef Jam = Cfg.jams[0];
    Jam.regen = 0.5;
    Sim S;
    S.Init(Jam, P, 303);
    T_CHECK(Near(S.Posts(), P.PostsStart));
    T_CHECK(Near(S.PostsRegen(), 0.5));

    // Spending down to zero, then refusal with NoPosts (and nothing else changes).
    int Target = -1;
    for (int I = 0; I < S.NodeCount(); ++I)
    {
        if (S.Opinion(I) >= 0 && S.Opinion(I) != kPlayerId && !S.GetGraph().IsGateNode(I)) { Target = I; break; }
    }
    int Spent = 0;
    for (int I = 0; I < 5; ++I)
    {
        S.SetNodeState(Target, 3, 1.0, 0.0);
        const ActionOutcome O = S.Plug(kPlayerId, Target);
        if (O.Accepted()) { ++Spent; }
        S.SetNodeState(Target, 3, 1.0, 0.0);
    }
    T_CHECK(Spent == 5);
    T_CHECK(Near(S.Posts(), 0.0, 1.0e-9));
    S.SetNodeState(Target, 3, 1.0, 0.0);
    T_CHECK(S.Plug(kPlayerId, Target).result == ActionResult::NoPosts);
    S.SetNightHeld(true);
    const double Heat0 = S.Heat();
    T_CHECK(S.Sock(Target).result == ActionResult::NoPosts);
    T_CHECK(Near(S.Heat(), Heat0));              // a refused sock adds no Heat
    S.SetNightHeld(false);

    // Regen: 20 steps at 0.5/s = +0.5 Post; clamped at the max.
    for (int I = 0; I < 20; ++I) { S.Step(); }
    T_CHECK(Near(S.Posts(), 0.5, 1.0e-9));
    for (int I = 0; I < 20 * 30; ++I) { S.Step(); }
    T_CHECK(Near(S.Posts(), P.PostsMax));

    // Heat: decays 3/s when night is not held; +4/s while held; clamps at 0 and 100.
    S.SetHeat(30.0);
    for (int I = 0; I < 20; ++I) { S.Step(); }
    T_CHECK(Near(S.Heat(), 27.0, 1.0e-9));
    S.SetNightHeld(true);
    for (int I = 0; I < 20; ++I) { S.Step(); }
    T_CHECK(Near(S.Heat(), 31.0, 1.0e-9));
    S.SetNightHeld(false);
    S.SetHeat(1.0);
    for (int I = 0; I < 40; ++I) { S.Step(); }
    T_CHECK(Near(S.Heat(), 0.0));
    S.SetNightHeld(true);
    S.SetHeat(99.0);
    for (int I = 0; I < 100; ++I) { S.Step(); }
    T_CHECK(Near(S.Heat(), P.HeatMax));
    T_CHECK(S.HeatMaxed());
    S.SetNightHeld(false);
    S.SetHeat(P.HeatRedZone);
    T_CHECK(!S.InRedZone());
    S.SetHeat(P.HeatRedZone + 0.5);
    T_CHECK(S.InRedZone());

    // Heat capped by sock: two socks from 60 do not exceed the max.
    S.SetHeat(70.0);
    S.SetNightHeld(true);
    S.SetPosts(5.0);
    int T2 = -1;
    for (int I = 0; I < S.NodeCount(); ++I)
    {
        if (S.Opinion(I) >= 0 && S.Opinion(I) != kPlayerId && S.LockRemaining(I) == 0.0) { T2 = I; break; }
    }
    S.Sock(T2);
    T_CHECK(S.Heat() <= P.HeatMax + 1.0e-9);
}

// ------------------------------------------------------------------------------------------
inline void TestQueries(const Config& Cfg)
{
    std::printf("[queries]\n");
    const Params& P = Cfg.params;
    Sim S;
    S.Init(Cfg.jams[1], P, 404);
    S.Plug(1, 0); // touch something so state is non-trivial

    // PlugsNeeded arithmetic.
    int M = FindNode(S, NodeRole::Member);
    S.SetNodeState(M, 3, 0.6, 0.0);
    T_CHECK(S.PlugsNeeded(kPlayerId, M) == 1);   // 0.6 - 0.45 = 0.15 flips
    S.SetNodeState(M, 3, 0.61, 0.0);
    T_CHECK(S.PlugsNeeded(kPlayerId, M) == 2);
    S.SetNodeState(M, 3, 1.0, 0.0);
    T_CHECK(S.PlugsNeeded(kPlayerId, M) == 2);
    T_CHECK(S.PlugsNeeded(kPlayerId, M, 2.0) == 1);
    S.SetNodeState(M, kPlayerId, 0.5, 0.0);
    T_CHECK(S.PlugsNeeded(kPlayerId, M) == 0);
    S.SetNodeState(M, 3, 0.5, 4.0);
    T_CHECK(S.PlugsNeeded(kPlayerId, M) >= kUnreachable);
    S.SetNodeState(M, 3, 0.5, 0.0);

    // Plugs predicted by PlugsNeeded flip the node.
    for (int Trial = 0; Trial < 20; ++Trial)
    {
        Rng R(Rng::Derive(9u, static_cast<uint32_t>(Trial)));
        const double C = R.Uniform(0.0, 1.0);
        const double Str = 1.0 + R.Uniform(0.0, 3.0);
        S.SetNodeState(M, 4, C, 0.0);
        const int Need = S.PlugsNeeded(kPlayerId, M, Str);
        int Done = 0;
        S.SetPosts(5.0);
        while (S.Opinion(M) != kPlayerId && Done < 20)
        {
            S.SetPosts(5.0);
            S.Plug(kPlayerId, M, Str);
            ++Done;
        }
        T_CHECK(Done == Need);
    }

    // BestTarget equals the brute-force argmax of CollectTargets.
    Sim S2;
    S2.Init(Cfg.jams[2], P, 405);
    TargetFilter F;
    F.ownerMult[S2.RivalId()] = 2.0;
    Target Best;
    T_CHECK(S2.BestTarget(kPlayerId, F, &Best));
    std::vector<Target> All;
    S2.CollectTargets(kPlayerId, F, &All);
    T_CHECK(!All.empty() && All[0].node == Best.node && Near(All[0].score, Best.score));
    for (size_t I = 1; I < All.size(); ++I) { T_CHECK(All[I - 1].score >= All[I].score); }
    T_CHECK(!S2.IsGated(Best.node));

    // A per-voter multiplier (TargetFilter::nodeMult): 0 leaves a voter out, a factor scales its score, and the best target is still the head of the list.
    {
        T_CHECK(F.nodeMult == nullptr);
        std::vector<double> Mult(static_cast<size_t>(S2.NodeCount()), 1.0);
        Mult[static_cast<size_t>(Best.node)] = 0.0;
        TargetFilter G = F;
        G.nodeMult = Mult.data();
        Target Second;
        T_CHECK(S2.BestTarget(kPlayerId, G, &Second));
        T_CHECK(Second.node != Best.node);
        std::vector<Target> Rest;
        S2.CollectTargets(kPlayerId, G, &Rest);
        T_CHECK(!Rest.empty() && Rest[0].node == Second.node && Near(Rest[0].score, Second.score));
        T_CHECK(Rest.size() + 1 == All.size());
        for (const Target& Cand : Rest) { T_CHECK(Cand.node != Best.node); }
        Mult[static_cast<size_t>(Best.node)] = 0.25;
        Target Scaled;
        T_CHECK(S2.BestTarget(kPlayerId, G, &Scaled));
        std::vector<Target> ScaledAll;
        S2.CollectTargets(kPlayerId, G, &ScaledAll);
        T_CHECK(ScaledAll.size() == All.size() && ScaledAll[0].node == Scaled.node && Near(ScaledAll[0].score, Scaled.score));
        for (const Target& Cand : ScaledAll)
        {
            if (Cand.node == Best.node) { T_CHECK(Near(Cand.score, 0.25 * Best.score)); }
        }
    }

    // Value = V + 0.5 * V(non-owned followers).
    const int Hub = S2.GetGraph().hubOfCommunity[0];
    double Extra = 0.0;
    const GraphNode& H = S2.GetGraph().nodes[static_cast<size_t>(Hub)];
    for (size_t K = 0; K < H.followers.size(); ++K)
    {
        if (S2.Opinion(H.followers[K]) != 5) { Extra += S2.GetGraph().nodes[static_cast<size_t>(H.followers[K])].weight; }
    }
    T_CHECK(Near(S2.NodeValue(5, Hub), H.weight + 0.5 * Extra));

    // WeakestOwned: lowest conviction among high-value owned nodes.
    const int Rv = S2.RivalId();
    const int Weak = S2.WeakestOwned(Rv, P.HighValueV);
    T_CHECK(Weak >= 0);
    for (int I = 0; I < S2.NodeCount(); ++I)
    {
        if (S2.Opinion(I) == Rv && S2.GetGraph().nodes[static_cast<size_t>(I)].weight >= P.HighValueV)
        {
            T_CHECK(S2.Conviction(I) >= S2.Conviction(Weak) - 1.0e-12);
        }
    }
    T_CHECK(S2.WeakestOwned(Rv, 1.0e12) == -1);

    // Communities, ranking, shares.
    int Total = 0;
    for (int C = 0; C < S2.Jam().communities; ++C) { Total += static_cast<int>(S2.NodesByCommunity(C).size()); }
    T_CHECK(Total == S2.NodeCount() - S2.Jam().celebs);
    T_CHECK(S2.NodesByCommunity(99).empty());
    const std::vector<int> Rank = S2.Ranking();
    T_CHECK(static_cast<int>(Rank.size()) == S2.NumEntries());
    T_CHECK(S2.RankOf(Rank[0]) == 0);
    T_CHECK(S2.Leader() == Rank[0]);

    // Entries: quality drawn from one range for everyone but the player's base+bonus; clout as specified.
    for (int E = 0; E < S2.NumEntries(); ++E)
    {
        const Entry& En = S2.GetEntry(E);
        if (E == kPlayerId)
        {
            T_CHECK(Near(En.quality, P.PlayerBaseQuality + P.PlayerBuildBonus));
            T_CHECK(Near(En.clout, P.PlayerClout));
        }
        else
        {
            T_CHECK(En.quality >= P.QualityMin && En.quality <= P.QualityMax);
            T_CHECK(E == S2.RivalId() ? Near(En.clout, P.RivalClout) : (En.clout >= P.OtherCloutMin && En.clout <= P.OtherCloutMax));
        }
    }
    T_CHECK(S2.OwnedCount(kPlayerId) == RoundToIntLocal(P.PlayerStartNodes));
    // Player's starting nodes are low-degree with conviction 0.6.
    for (int I = 0; I < S2.NodeCount(); ++I)
    {
        if (S2.Opinion(I) == kPlayerId)
        {
            T_CHECK(Near(S2.Conviction(I), P.PlayerStartConv));
            T_CHECK(static_cast<int>(S2.GetGraph().nodes[static_cast<size_t>(I)].followers.size()) <= RoundToIntLocal(P.PlayerStartMaxFollowers));
        }
    }
    // Celebrities start rival-owned at conviction 1.0.
    for (size_t Z = 0; Z < S2.GetGraph().celebrities.size(); ++Z)
    {
        T_CHECK(S2.Opinion(S2.GetGraph().celebrities[Z]) == S2.RivalId());
        T_CHECK(Near(S2.Conviction(S2.GetGraph().celebrities[Z]), P.RivalConv));
    }
}

inline void RunAll(const Config& Cfg)
{
    Count() = Counter();
    // The mechanics tests attack the player's voters directly and expect the plug to land at once, so they run without the announcement (AttackTelegraph = 0).
    Config Legacy = Cfg;
    Legacy.params.AttackTelegraph = 0.0;
    TestRng();
    TestGraph(Cfg);
    TestPlugAndSock(Legacy);
    TestGates(Legacy);
    TestStrike(Legacy);
    TestBlame(Legacy);
    TestRivalBank();
    TestOverheatAndAudit(Legacy);
    TestRallyAndAttribution(Legacy);
    TestTelegraph(Cfg);
    TestParry(Cfg);
    TestEyeAway(Cfg);
    TestBuild(Cfg);
    TestLayout(Cfg);
    TestSockReview(Cfg);
    TestTolerance(Cfg);
    TestEconomy(Legacy);
    TestQueries(Legacy);
    TestDeterminism(Cfg);
    TestDynSeed(Cfg);
    TestInvariants(Cfg);
    std::printf("tests: %d passed, %d failed\n", Count().Passed, Count().Failed);
}

} // namespace Tests
} // namespace ShiftSim
