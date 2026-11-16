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
                if (N.x < static_cast<float>(Cfg.params.LayoutMargin) - 0.01f || N.x > static_cast<float>(Cfg.params.LayoutWidth - Cfg.params.LayoutMargin) + 0.01f ||
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

inline std::vector<uint64_t> ReplayLog(const JamDef& Jam, const Params& P, uint32_t Seed, const std::vector<LogAction>& Log, int Steps)
{
    Sim S;
    S.Init(Jam, P, Seed);
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
    T_CHECK(Near(S.Conviction(M), P.PlugFlipConv));
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
    T_CHECK(Late.revertedNodes.empty());      // nothing left to revert
    T_CHECK(S3.Opinion(Hub3) == kPlayerId);
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
        // The rival is not gated.
        S.SetNodeState(Hub, 3, 0.7, 0.0);
        O = S.Plug(S.RivalId(), Hub);
        T_CHECK(O.result != ActionResult::Gated);

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
    TestRng();
    TestGraph(Cfg);
    TestPlugAndSock(Cfg);
    TestGates(Cfg);
    TestStrike(Cfg);
    TestEconomy(Cfg);
    TestQueries(Cfg);
    TestDeterminism(Cfg);
    TestInvariants(Cfg);
    std::printf("tests: %d passed, %d failed\n", Count().Passed, Count().Failed);
}

} // namespace Tests
} // namespace ShiftSim
