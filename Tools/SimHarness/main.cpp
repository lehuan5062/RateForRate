// Shift Happens - standalone numeric-core harness.
//   harness.exe test                       unit + invariant + determinism tests
//   harness.exe batch [--seeds 200]        balance table: bots x jams
//   harness.exe sweep [--seeds 100]        Posts-regen sweep 0.25..0.9 (tipping point)
//   harness.exe r2    [--seeds 200]        R^2 of final votes vs quality / followers
//   harness.exe calib [--seeds 200]        bisect per-jam regen for the target greedy win rates
//   harness.exe trace --jam 1 --seed 5 --bot greedy
//   harness.exe all                        test + batch + sweep + r2
//   harness.exe export-data                write built-in defaults to Data/Tuning.csv and Data/Jams.csv
// Options: --data <dir>  --threads N  --set Name=Value (repeatable, Params or policy)  --jamset N.Field=Value
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "policies.h"
#include "runner.h"
#include "tests.h"

using namespace ShiftSim;

namespace
{

struct Args
{
    std::string command = "all";
    std::map<std::string, std::string> opts;
    std::vector<std::string> sets;
    std::vector<std::string> jamsets;
};

Args ParseArgs(int Argc, char** Argv)
{
    Args A;
    int I = 1;
    if (I < Argc && Argv[I][0] != '-')
    {
        A.command = Argv[I++];
    }
    for (; I < Argc; ++I)
    {
        const std::string K = Argv[I];
        if (K.size() > 2 && K[0] == '-' && K[1] == '-')
        {
            const std::string Name = K.substr(2);
            std::string Val = "1";
            if (I + 1 < Argc && Argv[I + 1][0] != '-')
            {
                Val = Argv[++I];
            }
            else if (I + 1 < Argc && (Name == "set" || Name == "jamset"))
            {
                Val = Argv[++I];
            }
            if (Name == "set") { A.sets.push_back(Val); }
            else if (Name == "jamset") { A.jamsets.push_back(Val); }
            else { A.opts[Name] = Val; }
        }
    }
    return A;
}

int OptInt(const Args& A, const char* Name, int Default)
{
    const std::map<std::string, std::string>::const_iterator It = A.opts.find(Name);
    return It == A.opts.end() ? Default : std::atoi(It->second.c_str());
}

std::string OptStr(const Args& A, const char* Name, const char* Default)
{
    const std::map<std::string, std::string>::const_iterator It = A.opts.find(Name);
    return It == A.opts.end() ? std::string(Default) : It->second;
}

bool ApplyOverrides(const Args& A, Config* Cfg)
{
    for (size_t I = 0; I < A.sets.size(); ++I)
    {
        const size_t Eq = A.sets[I].find('=');
        double V = 0.0;
        if (Eq == std::string::npos || !ParseDouble(A.sets[I].substr(Eq + 1), &V))
        {
            std::printf("bad --set '%s'\n", A.sets[I].c_str());
            return false;
        }
        const std::string Name = A.sets[I].substr(0, Eq);
        if (!SetParam(Cfg->params, Name, V) && !SetPolicyParam(Cfg->policy, Name, V))
        {
            std::printf("unknown parameter '%s'\n", Name.c_str());
            return false;
        }
    }
    for (size_t I = 0; I < A.jamsets.size(); ++I)
    {
        // N.Field=Value  (N = 1-based jam; N omitted or 0 = all jams)
        const std::string& S = A.jamsets[I];
        const size_t Dot = S.find('.');
        const size_t Eq = S.find('=');
        double V = 0.0;
        if (Dot == std::string::npos || Eq == std::string::npos || !ParseDouble(S.substr(Eq + 1), &V))
        {
            std::printf("bad --jamset '%s' (use N.Field=Value)\n", S.c_str());
            return false;
        }
        const int N = std::atoi(S.substr(0, Dot).c_str());
        const std::string Field = S.substr(Dot + 1, Eq - Dot - 1);
        for (size_t J = 0; J < Cfg->jams.size(); ++J)
        {
            if (N == 0 || static_cast<int>(J) + 1 == N)
            {
                if (!SetJamField(Cfg->jams[J], Field, V))
                {
                    std::printf("unknown jam field '%s'\n", Field.c_str());
                    return false;
                }
            }
        }
    }
    return true;
}

std::vector<int> ParseJamList(const Args& A, const Config& Cfg)
{
    std::vector<int> L;
    const std::string S = OptStr(A, "jam", "");
    if (S.empty())
    {
        for (size_t J = 0; J < Cfg.jams.size(); ++J) { L.push_back(static_cast<int>(J)); }
        return L;
    }
    for (size_t I = 0; I < S.size(); ++I)
    {
        if (S[I] >= '1' && S[I] <= '9' && static_cast<size_t>(S[I] - '1') < Cfg.jams.size()) { L.push_back(S[I] - '1'); }
    }
    return L;
}

std::vector<BotKind> ParseBotList(const Args& A, const char* Default)
{
    std::vector<BotKind> L;
    const std::string S = OptStr(A, "bots", Default);
    size_t Pos = 0;
    while (Pos <= S.size())
    {
        size_t Comma = S.find(',', Pos);
        if (Comma == std::string::npos) { Comma = S.size(); }
        const std::string Tok = S.substr(Pos, Comma - Pos);
        if (Tok == "idle") { L.push_back(BotKind::Idle); }
        else if (Tok == "random") { L.push_back(BotKind::Random); }
        else if (Tok == "greedy") { L.push_back(BotKind::Greedy); }
        else if (Tok == "sock" || Tok == "greedy+sock") { L.push_back(BotKind::GreedySock); }
        Pos = Comma + 1;
    }
    return L;
}

int Threads(const Args& A)
{
    const int Def = static_cast<int>(std::thread::hardware_concurrency());
    return OptInt(A, "threads", Def > 0 ? Def : 4);
}

const char* BarFor(double Frac, char* Buf, int Width)
{
    int N = static_cast<int>(Frac * static_cast<double>(Width) + 0.5);
    if (N < 0) { N = 0; }
    if (N > Width) { N = Width; }
    for (int I = 0; I < Width; ++I) { Buf[I] = I < N ? '#' : '.'; }
    Buf[Width] = '\0';
    return Buf;
}

void PrintConfigSummary(const Config& Cfg)
{
    std::printf("data: %s\n", Cfg.dataDir.empty() ? "(built-in defaults)" : Cfg.dataDir.c_str());
    for (size_t J = 0; J < Cfg.jams.size(); ++J)
    {
        const JamDef& D = Cfg.jams[J];
        std::printf("  jam %zu %-18s nodes %d comm %d bridges %d celebs %d  %dd x %.0fs  regen %.3f  gates %.2f/%.2f  celebExtra %.0f\n",
                    J + 1, D.name.c_str(), D.nodes, D.communities, D.bridges, D.celebs, D.days, D.secPerDay, D.regen,
                    D.localGate, D.celebGate, D.celebExtraFollowers);
    }
}

void CmdBatch(const Config& Cfg, const Args& A)
{
    const int Seeds = OptInt(A, "seeds", 200);
    const std::vector<int> Jams = ParseJamList(A, Cfg);
    const std::vector<BotKind> Bots = ParseBotList(A, "idle,random,greedy,sock");
    std::printf("\n== batch: %d seeds per cell (Normal, stand-in rival/others/Mod Eye) ==\n", Seeds);
    std::printf("%-4s %-12s | %7s %7s %7s %6s | %6s %6s %7s %6s | %5s %6s\n", "jam", "player bot", "rival%", "you%", "other%", "DQ%",
                "you", "rival", "others", "undec", "rank", "strike");
    for (size_t Ji = 0; Ji < Jams.size(); ++Ji)
    {
        for (size_t Bi = 0; Bi < Bots.size(); ++Bi)
        {
            const BatchStats B = RunBatch(Cfg, Jams[Ji], Bots[Bi], Seeds, -1.0, Threads(A), false);
            std::printf("%-4d %-12s | %6.1f%% %6.1f%% %6.1f%% %5.1f%% | %5.1f%% %5.1f%% %6.1f%% %5.1f%% | %5.2f %6.2f\n", Jams[Ji] + 1,
                        BotName(Bots[Bi]), 100.0 * B.rivalWin, 100.0 * B.playerWin, 100.0 * B.othersWin, 100.0 * B.dq,
                        100.0 * B.playerShare, 100.0 * B.rivalShare, 100.0 * B.othersShare, 100.0 * B.undecidedShare, B.avgRank,
                        B.avgStrikes);
        }
        std::printf("\n");
    }
}

void CmdSweep(const Config& Cfg, const Args& A)
{
    const int Seeds = OptInt(A, "seeds", 100);
    const std::vector<int> Jams = ParseJamList(A, Cfg);
    const std::vector<BotKind> Bots = ParseBotList(A, "greedy,sock");
    std::printf("\n== Posts-regen sweep, %d seeds per point ==\n", Seeds);
    for (size_t Bi = 0; Bi < Bots.size(); ++Bi)
    {
        for (size_t Ji = 0; Ji < Jams.size(); ++Ji)
        {
            std::printf("\njam %d (%s), bot %s: you-win%% vs regen (jam default %.2f)\n", Jams[Ji] + 1,
                        Cfg.jams[static_cast<size_t>(Jams[Ji])].name.c_str(), BotName(Bots[Bi]),
                        Cfg.jams[static_cast<size_t>(Jams[Ji])].regen);
            double Prev = -1.0;
            double Tip = -1.0;
            double SteepestJump = 0.0;
            for (int Step = 0; Step <= 13; ++Step)
            {
                const double Regen = 0.25 + 0.05 * static_cast<double>(Step);
                const BatchStats B = RunBatch(Cfg, Jams[Ji], Bots[Bi], Seeds, Regen, Threads(A), false);
                char Bar[41];
                std::printf("  regen %.2f  win %5.1f%%  share %5.1f%%  rival %5.1f%%  |%s|\n", Regen, 100.0 * B.playerWin,
                            100.0 * B.playerShare, 100.0 * B.rivalShare, BarFor(B.playerWin, Bar, 40));
                if (Prev >= 0.0 && B.playerWin - Prev > SteepestJump)
                {
                    SteepestJump = B.playerWin - Prev;
                    Tip = Regen - 0.025;
                }
                Prev = B.playerWin;
            }
            if (Tip < 0.0)
            {
                std::printf("  no tipping point (win rate is flat across the sweep)\n");
            }
            else
            {
                std::printf("  tipping point ~ regen %.3f (steepest rise %.0f points per 0.05)\n", Tip, 100.0 * SteepestJump);
            }
        }
    }
}

void CmdR2(const Config& Cfg, const Args& A)
{
    const int Seeds = OptInt(A, "seeds", 200);
    const std::vector<int> Jams = ParseJamList(A, Cfg);
    const std::vector<BotKind> Bots = ParseBotList(A, "idle,greedy");
    std::printf("\n== R^2 of final votes vs entry quality / followers (%d seeds) ==\n", Seeds);
    std::printf("followers = sum of follower counts of the nodes an entry owns at t=0; votes = final V tally\n");
    std::printf("%-4s %-12s | %-24s | %-24s\n", "jam", "player bot", "pooled  quality  followers", "per-run mean quality followers");
    for (size_t Ji = 0; Ji < Jams.size(); ++Ji)
    {
        for (size_t Bi = 0; Bi < Bots.size(); ++Bi)
        {
            const BatchStats B = RunBatch(Cfg, Jams[Ji], Bots[Bi], Seeds, -1.0, Threads(A), true);
            const R2Stats R = ComputeR2(B.results);
            std::printf("%-4d %-12s |        %6.1f%% %9.1f%% |             %6.1f%% %9.1f%%\n", Jams[Ji] + 1, BotName(Bots[Bi]),
                        100.0 * R.pooledQuality, 100.0 * R.pooledFollowers, 100.0 * R.meanQuality, 100.0 * R.meanFollowers);
        }
    }
    std::printf("targets: quality < 10%%, followers > 70%%\n");
}

BotKind ParseBot(const std::string& S, BotKind Default)
{
    if (S == "idle") { return BotKind::Idle; }
    if (S == "random") { return BotKind::Random; }
    if (S == "greedy") { return BotKind::Greedy; }
    if (S == "sock" || S == "greedy+sock") { return BotKind::GreedySock; }
    return Default;
}

// Regen at which the bot's win rate crosses Target (bisection; win rate rises with regen).
double RegenForWinRate(const Config& Cfg, int Jam, BotKind Bot, double Target, int Seeds, int Thr, double* OutWin)
{
    double Lo = 0.15;
    double Hi = 1.6;
    for (int It = 0; It < 8; ++It)
    {
        const double Mid = 0.5 * (Lo + Hi);
        const BatchStats B = RunBatch(Cfg, Jam, Bot, Seeds, Mid, Thr, false);
        if (B.playerWin < Target) { Lo = Mid; } else { Hi = Mid; }
    }
    const double Regen = 0.5 * (Lo + Hi);
    if (OutWin != nullptr)
    {
        *OutWin = RunBatch(Cfg, Jam, Bot, Seeds, Regen, Thr, false).playerWin;
    }
    return Regen;
}

void CmdCalib(Config Cfg, const Args& A)
{
    const int Seeds = OptInt(A, "seeds", 100);
    const BotKind Bot = ParseBot(OptStr(A, "bot", "greedy"), BotKind::Greedy);
    const std::vector<int> Jams = ParseJamList(A, Cfg);
    const double Targets[3] = { 0.20, 0.50, 0.80 };
    std::printf("\n== calibration (%s, %d seeds): regen at which win rate crosses 20%% / 50%% / 80%% ==\n", BotName(Bot), Seeds);
    for (size_t Ji = 0; Ji < Jams.size(); ++Ji)
    {
        const int J = Jams[Ji];
        double R[3];
        for (int T = 0; T < 3; ++T)
        {
            R[T] = RegenForWinRate(Cfg, J, Bot, Targets[T], Seeds, Threads(A), nullptr);
        }
        std::printf("  jam %d: 20%% @ %.3f   50%% @ %.3f   80%% @ %.3f   (cliff width %.3f; jam default %.3f)\n", J + 1, R[0], R[1], R[2],
                    R[2] - R[0], Cfg.jams[static_cast<size_t>(J)].regen);
    }
}

// Solve the per-jam regen for the plan's target greedy win rates (85/55/30) and print the Jams.csv values.
// Solve the per-jam rival interval (jam regen fixed) for the plan's target win rates.
void CmdSolveInterval(Config Cfg, const Args& A)
{
    const int Seeds = OptInt(A, "seeds", 200);
    const BotKind Bot = ParseBot(OptStr(A, "bot", "greedy"), BotKind::Greedy);
    const double Targets[3] = { 0.85, 0.55, 0.30 };
    std::printf("\n== solve rival interval for target %s win rates 85/55/30%% at the jams' current regen (%d seeds) ==\n", BotName(Bot), Seeds);
    for (size_t J = 0; J < Cfg.jams.size() && J < 3; ++J)
    {
        double Lo = 1.0;
        double Hi = 14.0;
        for (int It = 0; It < 9; ++It)
        {
            const double Mid = 0.5 * (Lo + Hi);
            Cfg.jams[J].rivalInterval = Mid;
            const BatchStats B = RunBatch(Cfg, static_cast<int>(J), Bot, Seeds, -1.0, Threads(A), false);
            if (B.playerWin < Targets[J]) { Lo = Mid; } else { Hi = Mid; }
        }
        const double Interval = 0.5 * (Lo + Hi);
        Cfg.jams[J].rivalInterval = Interval;
        const BatchStats B = RunBatch(Cfg, static_cast<int>(J), Bot, Seeds, -1.0, Threads(A), false);
        std::printf("  jam %zu: regen %.3f  target %.0f%%  RivalInterval %.2f  -> win %.1f%%\n", J + 1, Cfg.jams[J].regen, 100.0 * Targets[J],
                    Interval, 100.0 * B.playerWin);
    }
}

void CmdSolve(Config Cfg, const Args& A)
{
    const int Seeds = OptInt(A, "seeds", 200);
    const BotKind Bot = ParseBot(OptStr(A, "bot", "greedy"), BotKind::Greedy);
    const double Targets[3] = { 0.85, 0.55, 0.30 };
    std::printf("\n== solve: per-jam regen for target %s win rates 85/55/30%% (%d seeds) ==\n", BotName(Bot), Seeds);
    for (size_t J = 0; J < Cfg.jams.size() && J < 3; ++J)
    {
        double Win = 0.0;
        const double Regen = RegenForWinRate(Cfg, static_cast<int>(J), Bot, Targets[J], Seeds, Threads(A), &Win);
        std::printf("  jam %zu: target %.0f%%  regen %.3f  -> win %.1f%%\n", J + 1, 100.0 * Targets[J], Regen, 100.0 * Win);
    }
}

void CmdInit(const Config& Cfg, const Args& A)
{
    const int Seeds = OptInt(A, "seeds", 100);
    std::printf("\n== initial position, mean over %d seeds ==\n", Seeds);
    std::printf("%-4s | %7s %7s %7s %7s | %8s %8s %8s %8s | %6s\n", "jam", "rival%", "others%", "you%", "undec%", "totalV", "celebV", "hubV", "memberV", "edges");
    for (size_t J = 0; J < Cfg.jams.size(); ++J)
    {
        double Rv = 0, Ot = 0, Pl = 0, Un = 0, Tot = 0, CelV = 0, HubV = 0, MemV = 0, Edges = 0;
        double CelN = 0, HubN = 0, MemN = 0;
        for (int I = 0; I < Seeds; ++I)
        {
            Sim S;
            S.Init(Cfg.jams[J], Cfg.params, SeedFor(I));
            Rv += S.Share(S.RivalId());
            Pl += S.Share(0);
            Un += S.UndecidedShare();
            Ot += 1.0 - S.Share(S.RivalId()) - S.Share(0) - S.UndecidedShare();
            Tot += S.TotalWeight();
            for (int N = 0; N < S.NodeCount(); ++N)
            {
                const GraphNode& G = S.GetGraph().nodes[static_cast<size_t>(N)];
                Edges += static_cast<double>(G.follows.size());
                if (G.role == NodeRole::Celebrity) { CelV += G.weight; CelN += 1; }
                else if (G.role == NodeRole::LocalHub) { HubV += G.weight; HubN += 1; }
                else { MemV += G.weight; MemN += 1; }
            }
        }
        const double N = static_cast<double>(Seeds);
        std::printf("%-4zu | %6.1f%% %6.1f%% %6.1f%% %6.1f%% | %8.0f %8.0f %8.0f %8.0f | %6.0f\n", J + 1, 100 * Rv / N, 100 * Ot / N, 100 * Pl / N,
                    100 * Un / N, Tot / N, CelN > 0 ? CelV / CelN : 0.0, HubN > 0 ? HubV / HubN : 0.0, MemN > 0 ? MemV / MemN : 0.0, Edges / N);
    }
}

void CmdTrace(const Config& Cfg, const Args& A)
{
    const int Jam = OptInt(A, "jam", 1) - 1;
    const int Seed = OptInt(A, "seed", 1);
    const std::string BotS = OptStr(A, "bot", "idle");
    BotKind Kind = BotKind::Idle;
    if (BotS == "random") { Kind = BotKind::Random; }
    else if (BotS == "greedy") { Kind = BotKind::Greedy; }
    else if (BotS == "sock") { Kind = BotKind::GreedySock; }
    JamDef J = Cfg.jams[static_cast<size_t>(Jam)];
    Sim S;
    S.Init(J, Cfg.params, static_cast<uint32_t>(Seed));
    StandInRival Rival; StandInOthers Others; StandInModEye Mod; PlayerBot Bot;
    Rival.Reset(S, Cfg.policy); Others.Reset(S, Cfg.policy); Mod.Reset(S, static_cast<uint32_t>(Seed), Cfg.policy); Bot.Reset(Kind, static_cast<uint32_t>(Seed));
    std::printf("trace jam %d seed %d bot %s. entries: Q / clout / seedFollowers\n", Jam + 1, Seed, BotName(Kind));
    for (int E = 0; E < S.NumEntries(); ++E)
    {
        const Entry& En = S.GetEntry(E);
        std::printf("  e%d%s Q %.2f clout %.2f seed %.0f/%.0f\n", E, E == 0 ? "(you)" : (E == S.RivalId() ? "(rival)" : ""), En.quality, En.clout, En.seedFollowers, En.seedWeight);
    }
    std::printf("total V %.0f, nodes %d\n", S.TotalWeight(), S.NodeCount());
    int NextPrint = 0;
    int FlipsLeft = OptInt(A, "flips", 0);
    while (!S.Finished())
    {
        if (FlipsLeft > 0)
        {
            for (size_t K = 0; K < S.LastStepChanges().size() && FlipsLeft > 0; ++K)
            {
                const NodeChange& C = S.LastStepChanges()[K];
                const GraphNode& G = S.GetGraph().nodes[static_cast<size_t>(C.node)];
                static const char* RoleName[4] = { "hub", "member", "celeb", "bridge" };
                std::printf("  t=%6.2f node %2d %-6s comm %2d V %5.0f fol %2d : e%d -> e%d (conv %.2f)\n", S.Time(), C.node,
                            RoleName[static_cast<int>(G.role)], G.community, G.weight, static_cast<int>(G.followers.size()), C.prevOwner,
                            C.newOwner, S.Conviction(C.node));
                --FlipsLeft;
            }
        }
        if (S.DayIndex() >= NextPrint)
        {
            std::printf("day %2d  posts %.1f heat %5.1f  ", S.DayIndex(), S.Posts(), S.Heat());
            for (int E = 0; E < S.NumEntries(); ++E) { std::printf("%5.1f ", 100.0 * S.Share(E)); }
            std::printf(" undec %4.1f  owned(you) %d\n", 100.0 * S.UndecidedShare(), S.OwnedCount(0));
            NextPrint += 3;
        }
        Rival.Update(S, Cfg.policy); Others.Update(S, Cfg.policy); Mod.Update(S, Cfg.policy);
        const int ActionsBefore = Bot.Actions;
        Bot.Update(S, Cfg.policy);
        if (OptInt(A, "bot-log", 0) > 0 && Bot.Actions != ActionsBefore)
        {
            const ActionOutcome& O = Bot.LastOutcome;
            const GraphNode& G = S.GetGraph().nodes[static_cast<size_t>(O.node)];
            static const char* RoleName2[4] = { "hub", "member", "celeb", "bridge" };
            std::printf("  t=%6.2f bot %-9s node %2d %-6s V %5.0f  e%d conv %.2f -> e%d %.2f\n", S.Time(), ActionResultName(O.result), O.node,
                        RoleName2[static_cast<int>(G.role)], G.weight, O.prevOwner, O.convBefore, O.newOwner, O.convAfter);
        }
        S.Step();
    }
    std::printf("rival actions: %d attacks (%d flips), %d defends\n", Rival.Attacks, Rival.Flips, Rival.Defends);
    std::printf("final   ");
    for (int E = 0; E < S.NumEntries(); ++E) { std::printf("%5.1f ", 100.0 * S.Share(E)); }
    std::printf("  leader e%d strikes %d\n", S.Leader(), S.Strikes());
}

} // namespace

int main(int Argc, char** Argv)
{
    const Args A = ParseArgs(Argc, Argv);

    std::string DataDir = OptStr(A, "data", "");
    if (DataDir.empty())
    {
        DataDir = FindDataDirFrom(".");
        if (DataDir.empty() && Argc > 0)
        {
            std::string Exe = Argv[0];
            const size_t Slash = Exe.find_last_of("/\\");
            DataDir = FindDataDirFrom(Slash == std::string::npos ? std::string(".") : Exe.substr(0, Slash));
        }
    }

    Config Cfg;
    std::string Err;
    // export-data always writes the built-in struct defaults (never reads the CSVs it is about to replace).
    if (!LoadConfig(A.command == "export-data" ? std::string() : DataDir, &Cfg, &Err))
    {
        std::printf("config error: %s\n", Err.c_str());
        return 2;
    }
    if (A.command == "export-data")
    {
        const std::string Dir = DataDir.empty() ? std::string("Data") : DataDir;
        const bool Ok = WriteTuningCsv(Dir + "/Tuning.csv", Cfg.params, Cfg.policy) && WriteJamsCsv(Dir + "/Jams.csv", Cfg.jams);
        std::printf("%s %s/{Tuning,Jams}.csv\n", Ok ? "wrote" : "FAILED to write", Dir.c_str());
        return Ok ? 0 : 1;
    }
    if (!ApplyOverrides(A, &Cfg))
    {
        return 2;
    }
    PrintConfigSummary(Cfg);

    if (A.command == "test")
    {
        Tests::RunAll(Cfg);
        return Tests::Count().Failed == 0 ? 0 : 1;
    }
    if (A.command == "batch") { CmdBatch(Cfg, A); return 0; }
    if (A.command == "sweep") { CmdSweep(Cfg, A); return 0; }
    if (A.command == "r2") { CmdR2(Cfg, A); return 0; }
    if (A.command == "calib") { CmdCalib(Cfg, A); return 0; }
    if (A.command == "solve") { CmdSolve(Cfg, A); return 0; }
    if (A.command == "solve-interval") { CmdSolveInterval(Cfg, A); return 0; }
    if (A.command == "trace") { CmdTrace(Cfg, A); return 0; }
    if (A.command == "init") { CmdInit(Cfg, A); return 0; }
    if (A.command == "all")
    {
        Tests::RunAll(Cfg);
        CmdBatch(Cfg, A);
        CmdSweep(Cfg, A);
        CmdR2(Cfg, A);
        return Tests::Count().Failed == 0 ? 0 : 1;
    }
    std::printf("unknown command '%s'\n", A.command.c_str());
    return 2;
}
