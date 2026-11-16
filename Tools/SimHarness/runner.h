// Harness: single-run driver, threaded batch runner and statistics.
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "config.h"
#include "policies.h"
#include "ShiftSim.h"

namespace ShiftSim
{

struct RunSpec
{
    int jam = 0;
    uint32_t seed = 1;
    BotKind bot = BotKind::Idle;
    double regen = -1.0;                // < 0: use the jam's regen
    bool rival = true;
    bool others = true;
    bool modEye = true;
};

struct RunResult
{
    int entries = 9;
    int winner = 0;                     // Sim::Leader()
    bool playerWon = false;
    bool rivalWon = false;
    bool dq = false;
    int strikes = 0;
    int playerRank = 0;                 // 1-based by raw tally
    double share[kMaxEntries] = {};
    double votes[kMaxEntries] = {};     // final tally (sum of V)
    double quality[kMaxEntries] = {};
    double seedFollowers[kMaxEntries] = {};
    double playerShare = 0.0;
    double rivalShare = 0.0;
    double othersShare = 0.0;
    double undecidedShare = 0.0;
    double sockWeightShare = 0.0;
    uint64_t hash = 0;
};

inline RunResult RunOne(const Config& Cfg, const RunSpec& Spec)
{
    JamDef Jam = Cfg.jams[static_cast<size_t>(Spec.jam)];
    if (Spec.regen >= 0.0)
    {
        Jam.regen = Spec.regen;
    }
    Sim S;
    RunResult Out;
    if (!S.Init(Jam, Cfg.params, Spec.seed))
    {
        return Out;
    }
    StandInRival Rival;
    StandInOthers Others;
    StandInModEye Mod;
    PlayerBot Bot;
    Rival.Reset(S, Cfg.policy);
    Others.Reset(S, Cfg.policy);
    Mod.Reset(S, Spec.seed, Cfg.policy);
    Bot.Reset(Spec.bot, Spec.seed);

    while (!S.Finished())
    {
        if (Spec.rival) { Rival.Update(S, Cfg.policy); }
        if (Spec.others) { Others.Update(S, Cfg.policy); }
        if (Spec.modEye) { Mod.Update(S, Cfg.policy); }
        Bot.Update(S, Cfg.policy);
        S.Step();
    }

    Out.entries = S.NumEntries();
    Out.winner = S.Leader();
    Out.dq = S.Disqualified();
    Out.strikes = S.Strikes();
    Out.playerWon = (Out.winner == kPlayerId) && !Out.dq;
    Out.rivalWon = (Out.winner == S.RivalId());
    Out.playerRank = S.RankOf(kPlayerId) + 1;
    double Others_ = 0.0;
    for (int E = 0; E < Out.entries; ++E)
    {
        Out.share[E] = S.Share(E);
        Out.votes[E] = S.Tally(E);
        Out.quality[E] = S.GetEntry(E).quality;
        Out.seedFollowers[E] = S.GetEntry(E).seedFollowers;
        if (E != kPlayerId && E != S.RivalId())
        {
            Others_ += Out.share[E];
        }
    }
    Out.playerShare = Out.share[kPlayerId];
    Out.rivalShare = Out.share[S.RivalId()];
    Out.othersShare = Others_;
    Out.undecidedShare = S.UndecidedShare();
    Out.sockWeightShare = S.TotalWeight() > 0.0 ? S.SockpuppetWeight() / S.TotalWeight() : 0.0;
    Out.hash = S.StateHash();
    return Out;
}

struct BatchStats
{
    int runs = 0;
    double rivalWin = 0.0;              // fractions
    double playerWin = 0.0;
    double dq = 0.0;
    double playerShare = 0.0;
    double rivalShare = 0.0;
    double othersShare = 0.0;
    double undecidedShare = 0.0;
    double avgRank = 0.0;
    double avgStrikes = 0.0;
    double sockShare = 0.0;
    double othersWin = 0.0;
    std::vector<RunResult> results;
};

inline uint32_t SeedFor(int Index, uint32_t Base = 1000u)
{
    return Rng::Derive(Base, static_cast<uint32_t>(Index) + 1u);
}

inline BatchStats RunBatch(const Config& Cfg, int Jam, BotKind Bot, int Seeds, double Regen, int Threads, bool KeepResults,
                           uint32_t SeedBase = 1000u)
{
    BatchStats B;
    B.runs = Seeds;
    std::vector<RunResult> Res(static_cast<size_t>(Seeds));
    std::atomic<int> Next(0);
    int T = Threads < 1 ? 1 : Threads;
    if (T > Seeds)
    {
        T = Seeds;
    }
    auto Work = [&]() {
        for (;;)
        {
            const int I = Next.fetch_add(1);
            if (I >= Seeds)
            {
                break;
            }
            RunSpec Spec;
            Spec.jam = Jam;
            Spec.seed = SeedFor(I, SeedBase);
            Spec.bot = Bot;
            Spec.regen = Regen;
            Res[static_cast<size_t>(I)] = RunOne(Cfg, Spec);
        }
    };
    std::vector<std::thread> Pool;
    for (int I = 1; I < T; ++I)
    {
        Pool.emplace_back(Work);
    }
    Work();
    for (size_t I = 0; I < Pool.size(); ++I)
    {
        Pool[I].join();
    }

    for (int I = 0; I < Seeds; ++I)
    {
        const RunResult& R = Res[static_cast<size_t>(I)];
        B.rivalWin += R.rivalWon ? 1.0 : 0.0;
        B.playerWin += R.playerWon ? 1.0 : 0.0;
        B.othersWin += (!R.playerWon && !R.rivalWon) ? 1.0 : 0.0;
        B.dq += R.dq ? 1.0 : 0.0;
        B.playerShare += R.playerShare;
        B.rivalShare += R.rivalShare;
        B.othersShare += R.othersShare;
        B.undecidedShare += R.undecidedShare;
        B.avgRank += static_cast<double>(R.playerRank);
        B.avgStrikes += static_cast<double>(R.strikes);
        B.sockShare += R.sockWeightShare;
    }
    const double N = Seeds > 0 ? static_cast<double>(Seeds) : 1.0;
    B.rivalWin /= N; B.playerWin /= N; B.othersWin /= N; B.dq /= N;
    B.playerShare /= N; B.rivalShare /= N; B.othersShare /= N; B.undecidedShare /= N;
    B.avgRank /= N; B.avgStrikes /= N; B.sockShare /= N;
    if (KeepResults)
    {
        B.results.swap(Res);
    }
    return B;
}

// Pearson r^2 of y on x.
inline double RSquared(const std::vector<double>& X, const std::vector<double>& Y)
{
    const size_t N = X.size();
    if (N < 3 || Y.size() != N)
    {
        return 0.0;
    }
    double Mx = 0.0, My = 0.0;
    for (size_t I = 0; I < N; ++I) { Mx += X[I]; My += Y[I]; }
    Mx /= static_cast<double>(N);
    My /= static_cast<double>(N);
    double Sxx = 0.0, Syy = 0.0, Sxy = 0.0;
    for (size_t I = 0; I < N; ++I)
    {
        const double Dx = X[I] - Mx;
        const double Dy = Y[I] - My;
        Sxx += Dx * Dx;
        Syy += Dy * Dy;
        Sxy += Dx * Dy;
    }
    if (!(Sxx > 0.0) || !(Syy > 0.0))
    {
        return 0.0;
    }
    return (Sxy * Sxy) / (Sxx * Syy);
}

struct R2Stats
{
    double pooledQuality = 0.0;         // all entries of all runs in one regression
    double pooledFollowers = 0.0;
    double meanQuality = 0.0;           // mean of per-run (9-point) regressions
    double meanFollowers = 0.0;
};

inline R2Stats ComputeR2(const std::vector<RunResult>& Results)
{
    R2Stats S;
    std::vector<double> Q, F, V;
    double SumQ = 0.0, SumF = 0.0;
    int Runs = 0;
    for (size_t R = 0; R < Results.size(); ++R)
    {
        const RunResult& Run = Results[R];
        std::vector<double> Rq, Rf, Rv;
        for (int E = 0; E < Run.entries; ++E)
        {
            Q.push_back(Run.quality[E]);
            F.push_back(Run.seedFollowers[E]);
            V.push_back(Run.votes[E]);
            Rq.push_back(Run.quality[E]);
            Rf.push_back(Run.seedFollowers[E]);
            Rv.push_back(Run.votes[E]);
        }
        SumQ += RSquared(Rq, Rv);
        SumF += RSquared(Rf, Rv);
        ++Runs;
    }
    S.pooledQuality = RSquared(Q, V);
    S.pooledFollowers = RSquared(F, V);
    if (Runs > 0)
    {
        S.meanQuality = SumQ / static_cast<double>(Runs);
        S.meanFollowers = SumF / static_cast<double>(Runs);
    }
    return S;
}

} // namespace ShiftSim
