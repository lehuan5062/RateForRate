// Shift Happens - Game Off 2026

#include "Game/ShiftBalanceCommandlet.h"

#include "Behavior/ShiftPolicies.h"
#include "Behavior/StateTree/ShiftBehavior.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/StringConv.h"
#include "Containers/UnrealString.h"
#include "Game/ShiftBuild.h"
#include "Game/ShiftRun.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/FileManager.h"
#include "HAL/Platform.h"
#include "Logging/LogMacros.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/CString.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShiftHappens.h"
#include "Sim/ShiftParams.h"
#include "Sim/ShiftSim.h"
#include <string>
#include "Templates/SharedPointer.h"
#include "Templates/TupleFwd.h"
#include "UI/ShiftHudModel.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

UShiftBalanceCommandlet::UShiftBalanceCommandlet()
{
	IsClient = false;
	IsEditor = true;    // the editor modules the StateTree plugin loads in editor builds need GEditor
	IsServer = false;
	LogToConsole = true;
}

namespace
{
	struct FStats
	{
		int32 Runs = 0;
		int32 Wins = 0;
		int32 Dq = 0;
		double RankSum = 0.0;
		double ShareSum = 0.0;
		double SockSum = 0.0;
		double StrikeSum = 0.0;
		double R2Quality = 0.0;
		double R2Fame = 0.0;
		double RivalShare = 0.0;
		FShiftFlipStats Flips;      // summed over the runs of the cell
		ShiftSim::AttackCounters Threats;   // announced attacks on the player's voters, summed
		double MegaStartShare = 0.0;        // the player's share when the rival's final push begins, and at the end, summed over the runs
		double MegaEndShare = 0.0;
		int32 MegaLeadAtStart = 0;          // runs in which the player led when the final push began, and still led at the end
		int32 MegaLeadKept = 0;
		int32 RivalAttacks = 0;             // what the rival did, summed over the runs: attacks, re-plugs of its own weak hubs, and actions its Post bank refused
		int32 RivalDefends = 0;
		int32 RivalStarved = 0;
		int32 PushAttacks = 0;              // the rival's final push alone, summed over the runs: its attacks, the announced attacks on the player and how they ended, voters lost, Posts spent
		int32 PushStarved = 0;
		int32 PushQueued = 0;
		int32 PushLanded = 0;
		int32 PushFlipped = 0;
		int32 PushParried = 0;
		int32 PushBlocked = 0;
		int32 PushLost = 0;
		double PushPostsRally = 0.0;
		double PushPostsParry = 0.0;
		int32 PushRuns = 0;
		int32 BlameStrikes = 0;             // Shift the Blame, summed over the runs: strikes, their fake share, the fatal ones and what became of them
		double BlameFakeSum = 0.0;
		int32 BlameFatal = 0;
		int32 BlameOffered = 0;
		int32 BlameTaken = 0;
		int32 BlameRefused = 0;
		int32 BlameLapsed = 0;
		int32 FocusAttacks = 0;             // the rival's attacks per voter, summed over the runs: all of them, on how many different voters, the most on one voter, the three most, and the runs that put 10 or more on one voter
		int32 FocusDistinct = 0;
		int32 FocusMax = 0;
		int32 FocusTop3 = 0;
		int32 FocusHeavyRuns = 0;
		FShiftFlowStats Flow;               // who moved the votes, summed over the runs
		double BestOtherShare = 0.0;        // the board at the end: the best other entry's share, how many other entries hold a top-3 place, the runs an other entry led
		int32 OthersOnPodium = 0;
		int32 OtherLeads = 0;
		int32 CelebAny = 0;                 // the celebrities (the big circles in the middle): runs in which the player held one at some point and when it first did (summed) ...
		double CelebFirstSum = 0.0;
		int32 CelebEndRuns[3] = { 0, 0, 0 };    // ... runs by how many of them the player held at the end (0 / 1 / 2), their wins and final shares ...
		int32 CelebEndWins[3] = { 0, 0, 0 };
		double CelebEndShare[3] = { 0.0, 0.0, 0.0 };
		int32 CelebRivalBoth = 0;           // ... runs in which the rival kept both, and the share of the run the player held a celebrity (summed) ...
		double CelebHeldFraction = 0.0;
		int32 CelebMidRuns[3] = { 0, 0, 0 };    // ... and the same by what the player held half way through the run (the end is a result, half way is a position)
		int32 CelebMidWins[3] = { 0, 0, 0 };
		int32 FatalFakeBins[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };   // fatal strikes by the share the Blame's cap is measured on: <4%, 4-8, 8-12, 12-16, 16-20, 20-30, 30-40, 40+
	};

	bool ParseBot(const FString& Name, ShiftSim::BotKind& Out)
	{
		return ShiftSim::BotKindFromName(std::string(TCHAR_TO_UTF8(*Name)), &Out);
	}

	/** Named strategies that are the explorer bot with a fixed configuration: castle (the playtester's snowball: biggest voter first, Rally kept on it), parry (answers attacks by
	 *  reinforcing only), rallyonly (answers by Rally only) and, from round 6, the sockpuppet strategies the playtester and the search found: snowball (sockpuppet the biggest gated
	 *  hubs and celebrities whenever Heat allows, no share cap of its own, answer attacks), steady (the same under a 6% fake share, the most Heat can hold steady) and gambler (the
	 *  search's best on GAME OFF: sockpuppets on hubs, a burst in the last 21 s, answers attacks). Returns false for any other name. */
	bool BalancePreset(const FString& Name, ShiftSim::BotKind& Kind, TMap<FString, double>& Overrides)
	{
		TMap<FString, double> P;
		if (Name.Equals(TEXT("castle"), ESearchCase::IgnoreCase))
		{
			P.Add(TEXT("XReact"), 0.0); P.Add(TEXT("XRally"), 1.0); P.Add(TEXT("XValueFirst"), 1.0); P.Add(TEXT("XKeepRally"), 1.0);
		}
		else if (Name.Equals(TEXT("parry"), ESearchCase::IgnoreCase))
		{
			P.Add(TEXT("XReact"), 1.0); P.Add(TEXT("XRally"), 0.0); P.Add(TEXT("XParry"), 1.0);
		}
		else if (Name.Equals(TEXT("window"), ESearchCase::IgnoreCase))
		{
			P.Add(TEXT("XReact"), 1.0); P.Add(TEXT("XRally"), 1.0); P.Add(TEXT("XWindow"), 1.0);
		}
		else if (Name.Equals(TEXT("snowball"), ESearchCase::IgnoreCase))
		{
			P.Add(TEXT("XReact"), 1.0); P.Add(TEXT("XRally"), 1.0); P.Add(TEXT("XSock"), 1.0); P.Add(TEXT("XBurst"), 0.0); P.Add(TEXT("XSockMinV"), 250.0); P.Add(TEXT("SockMaxHeat"), 35.0); P.Add(TEXT("XSockShare"), 0.0);
		}
		else if (Name.Equals(TEXT("steady"), ESearchCase::IgnoreCase))
		{
			P.Add(TEXT("XReact"), 1.0); P.Add(TEXT("XRally"), 1.0); P.Add(TEXT("XSock"), 1.0); P.Add(TEXT("XBurst"), 0.0); P.Add(TEXT("XSockMinV"), 250.0); P.Add(TEXT("SockMaxHeat"), 35.0); P.Add(TEXT("XSockShare"), 0.06);
		}
		else if (Name.Equals(TEXT("gambler"), ESearchCase::IgnoreCase))
		{
			P.Add(TEXT("XReact"), 1.0); P.Add(TEXT("XRally"), 1.0); P.Add(TEXT("XSock"), 1.0); P.Add(TEXT("XBurst"), 1.0); P.Add(TEXT("XIdle"), 0.0); P.Add(TEXT("XReinforceBelow"), 0.0);
			P.Add(TEXT("XPlugMinV"), 57.32); P.Add(TEXT("XParry"), 1.0); P.Add(TEXT("XValueFirst"), 0.0); P.Add(TEXT("XKeepRally"), 0.0); P.Add(TEXT("XWindow"), 1.0); P.Add(TEXT("XWindowBank"), 6.38);
			P.Add(TEXT("ReactRallyThreats"), 2.633); P.Add(TEXT("ReactRallyValue"), 489.9); P.Add(TEXT("XSockMinV"), 44.06); P.Add(TEXT("XSockShare"), 0.0); P.Add(TEXT("ReactDelay"), 0.7969);
			P.Add(TEXT("ReactMinValue"), 129.8); P.Add(TEXT("ReactReserve"), 0.6859); P.Add(TEXT("SockMaxHeat"), 24.07); P.Add(TEXT("LateSeconds"), 20.98);
		}
		else if (Name.Equals(TEXT("rallyonly"), ESearchCase::IgnoreCase))
		{
			P.Add(TEXT("XReact"), 1.0); P.Add(TEXT("XRally"), 1.0); P.Add(TEXT("XParry"), 0.0);
		}
		else
		{
			return false;
		}
		Kind = ShiftSim::BotKind::Explorer;
		for (const TPair<FString, double>& Pair : P)
		{
			Overrides.Add(Pair.Key, Pair.Value);
		}
		return true;
	}
}

int32 UShiftBalanceCommandlet::Main(const FString& Params)
{
	int32 Runs = 100;
	int32 Seed0 = 1000;
	FString JamArg = TEXT("all"), BotArg = TEXT("all"), BuildArg;
	FParse::Value(*Params, TEXT("Runs="), Runs);
	FParse::Value(*Params, TEXT("Seed0="), Seed0);
	FParse::Value(*Params, TEXT("Jam="), JamArg);
	FParse::Value(*Params, TEXT("Bot="), BotArg, false);          // false: keep the commas of -Bot=greedy,sock
	FParse::Value(*Params, TEXT("Build="), BuildArg, false);
	const bool bNoStateTree = FParse::Param(*Params, TEXT("NoStateTree"));
	const bool bMods = !FParse::Param(*Params, TEXT("NoMods"));

	// -Set=Name=Value,Name=Value overrides a sim Params, JamDef or PolicyParams field by name (for example SockLaunderConv=0.8,LateSeconds=24), so balance
	// tuning needs no rebuild. The game never reads Data/Tuning.csv; the C++ defaults are authoritative.
	FString SetArg;
	TMap<FString, double> Overrides;
	if (FParse::Value(*Params, TEXT("Set="), SetArg, false))
	{
		TArray<FString> SetParts;
		SetArg.ParseIntoArray(SetParts, TEXT(","));
		for (const FString& Part : SetParts)
		{
			FString Key, Value;
			if (Part.Split(TEXT("="), &Key, &Value))
			{
				Overrides.Add(Key.TrimStartAndEnd(), FCString::Atod(*Value));
			}
		}
	}

	FShiftBuild Build;
	if (!BuildArg.IsEmpty())
	{
		TArray<FString> Parts;
		BuildArg.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 3)
		{
			Build.Quality = FCString::Atoi(*Parts[0]);
			Build.Thumbnail = FCString::Atoi(*Parts[1]);
			Build.Network = FCString::Atoi(*Parts[2]);
		}
	}

	TArray<int32> Jams;
	if (JamArg.Equals(TEXT("all"), ESearchCase::IgnoreCase)) { Jams = { 0, 1, 2 }; }
	else { Jams.Add(FCString::Atoi(*JamArg)); }
	TArray<FString> BotNames;
	if (BotArg.Equals(TEXT("all"), ESearchCase::IgnoreCase)) { BotNames = { TEXT("idle"), TEXT("random"), TEXT("greedy"), TEXT("sock"), TEXT("finisher"), TEXT("late"), TEXT("rally"), TEXT("reactor") }; }
	else { BotArg.ParseIntoArray(BotNames, TEXT(",")); }

	FString OverrideText;
	for (const TPair<FString, double>& Override : Overrides)
	{
		OverrideText += FString::Printf(TEXT("%s%s=%g"), OverrideText.IsEmpty() ? TEXT("") : TEXT(","), *Override.Key, Override.Value);
	}
	UE_LOG(LogShift, Display, TEXT("Balance: %d runs per cell, build %d/%d/%d, behaviour %s, mods %d, overrides %s"), Runs, Build.Quality, Build.Thumbnail, Build.Network,
		bNoStateTree ? TEXT("native") : TEXT("StateTree"), bMods ? 1 : 0, OverrideText.IsEmpty() ? TEXT("none") : *OverrideText);

	FString Csv = TEXT("jam,bot,runs,win_pct,dq_pct,mean_rank,mean_share_pct,mean_sock_pct,mean_strikes,rival_share_pct,r2_quality,r2_fame\n");
	for (const int32 Jam : Jams)
	{
		for (const FString& BotName : BotNames)
		{
			ShiftSim::BotKind Kind = ShiftSim::BotKind::Idle;
			TMap<FString, double> BotOverrides = Overrides;
			if (!BalancePreset(BotName, Kind, BotOverrides) && !ParseBot(BotName, Kind))
			{
				UE_LOG(LogShift, Error, TEXT("Unknown bot %s"), *BotName);
				continue;
			}
			FStats St;
			for (int32 R = 0; R < Runs; ++R)
			{
				const uint32 Seed = static_cast<uint32>(Seed0 + R * 7919);
				FShiftRun Run;
				Run.SetOverrides(BotOverrides);
				if (!Run.Start(Jam, Seed, Build, bMods))
				{
					continue;
				}
				if (Kind != ShiftSim::BotKind::Idle)
				{
					Run.SetBot(Kind);
				}
				Run.SetTimeScale(60.0);
				if (!bNoStateTree)
				{
					TSharedPtr<FShiftBehavior> Behavior = FShiftBehavior::Create(Run);
					if (!Behavior.IsValid())
					{
						UE_LOG(LogShift, Error, TEXT("StateTrees unavailable: run Tools/py/build_statetrees.py or pass -NoStateTree"));
						return 2;
					}
					Run.SetDriver(Behavior);
				}

				const ShiftSim::Sim& S = Run.GetSim();
				TArray<FShiftHudDot> QualityDots, FameDots;
				for (int32 E = 0; E < S.NumEntries(); ++E)
				{
					FShiftHudDot Dot;
					Dot.X = static_cast<float>(S.GetEntry(E).quality);
					QualityDots.Add(Dot);
					Dot.X = static_cast<float>(S.Share(E));       // starting share = fame
					FameDots.Add(Dot);
				}
				const double MegaStartTime = S.TotalSeconds() - Run.GetPolicy().MegaDays * S.Jam().secPerDay;
				bool bMegaSeen = false;
				bool bMegaLead = false;
				double FirstCeleb = -1.0;
				double CelebTime = 0.0;
				double CelebPrevTime = 0.0;
				int32 CelebMid = -1;
				while (!Run.IsOver())
				{
					Run.Tick(0.25);
					{
						int32 Held = 0;
						for (const int Celeb : S.GetGraph().celebrities)
						{
							Held += S.Opinion(Celeb) == ShiftSim::kPlayerId ? 1 : 0;
						}
						if (Held > 0 && FirstCeleb < 0.0)
						{
							FirstCeleb = S.Time();
						}
						CelebTime += Held > 0 ? S.Time() - CelebPrevTime : 0.0;
						CelebPrevTime = S.Time();
						if (CelebMid < 0 && S.Time() >= 0.5 * S.TotalSeconds())
						{
							CelebMid = FMath::Min(Held, 2);
						}
					}
					if (!bMegaSeen && S.Time() >= MegaStartTime)
					{
						bMegaSeen = true;
						St.MegaStartShare += S.Share(ShiftSim::kPlayerId);
						bMegaLead = !S.Disqualified() && S.Leader() == ShiftSim::kPlayerId;
						St.MegaLeadAtStart += bMegaLead ? 1 : 0;
					}
				}
				{
					int32 HeldEnd = 0;
					int32 RivalEnd = 0;
					for (const int Celeb : S.GetGraph().celebrities)
					{
						HeldEnd += S.Opinion(Celeb) == ShiftSim::kPlayerId ? 1 : 0;
						RivalEnd += S.Opinion(Celeb) == S.RivalId() ? 1 : 0;
					}
					HeldEnd = FMath::Min(HeldEnd, 2);
					St.CelebAny += FirstCeleb >= 0.0 ? 1 : 0;
					St.CelebFirstSum += FirstCeleb >= 0.0 ? FirstCeleb : 0.0;
					++St.CelebEndRuns[HeldEnd];
					St.CelebEndWins[HeldEnd] += (!S.Disqualified() && S.Leader() == ShiftSim::kPlayerId) ? 1 : 0;
					St.CelebEndShare[HeldEnd] += S.Share(ShiftSim::kPlayerId);
					St.CelebRivalBoth += (RivalEnd >= 2) ? 1 : 0;
					if (CelebMid >= 0)
					{
						++St.CelebMidRuns[CelebMid];
						St.CelebMidWins[CelebMid] += (!S.Disqualified() && S.Leader() == ShiftSim::kPlayerId) ? 1 : 0;
					}
					St.CelebHeldFraction += CelebTime / FMath::Max(1.0, S.TotalSeconds());
				}
				St.MegaEndShare += S.Share(ShiftSim::kPlayerId);
				St.MegaLeadKept += (bMegaLead && !S.Disqualified() && S.Leader() == ShiftSim::kPlayerId) ? 1 : 0;
				const double Total = FMath::Max(1.0, S.TotalWeight());
				for (int32 E = 0; E < S.NumEntries(); ++E)
				{
					QualityDots[E].Y = static_cast<float>(S.Share(E));
					FameDots[E].Y = static_cast<float>(S.Share(E));
				}
				++St.Runs;
				St.Dq += S.Disqualified() ? 1 : 0;
				St.Wins += (!S.Disqualified() && S.Leader() == ShiftSim::kPlayerId) ? 1 : 0;
				St.RankSum += S.RankOf(ShiftSim::kPlayerId) + 1;
				St.ShareSum += S.Share(ShiftSim::kPlayerId);
				St.SockSum += S.SockpuppetWeight() / Total;
				St.StrikeSum += S.Strikes();
				St.RivalShare += S.Share(S.RivalId());
				St.R2Quality += FShiftHudFit::Compute(QualityDots).R2;
				St.R2Fame += FShiftHudFit::Compute(FameDots).R2;
				const FShiftFlipStats& Flips = Run.GetFlipStats();
				const ShiftSim::AttackCounters& Atk = S.Attacks();
				St.Threats.queued += Atk.queued;
				St.Threats.landed += Atk.landed;
				St.Threats.flipped += Atk.flipped;
				St.Threats.blocked += Atk.blocked;
				St.Threats.fizzled += Atk.fizzled;
				St.Threats.shieldBlocked += Atk.shieldBlocked;
				St.Threats.parried += Atk.parried;
				St.Threats.postsRally += Atk.postsRally;
				St.Threats.postsParry += Atk.postsParry;
				St.Flips.Gains += Flips.Gains;
				St.Flips.SockGains += Flips.SockGains;
				St.Flips.Losses += Flips.Losses;
				St.Flips.LossHerd += Flips.LossHerd;
				St.Flips.LossRival += Flips.LossRival;
				St.Flips.LossOthers += Flips.LossOthers;
				St.Flips.StrikeReverts += Flips.StrikeReverts;
				St.Flips.AuditPurges += Flips.AuditPurges;
				St.Flips.PlugDwellCount += Flips.PlugDwellCount;
				St.Flips.PlugDwellSum += Flips.PlugDwellSum;
				St.Flips.SockDwellCount += Flips.SockDwellCount;
				St.Flips.SockDwellSum += Flips.SockDwellSum;
				{
					TArray<int32> On;
					for (const int Count : Run.RivalPolicy().AttacksOn)
					{
						On.Add(Count);
					}
					On.Sort([](const int32 A, const int32 B) { return A > B; });
					for (const int32 Count : On)
					{
						St.FocusAttacks += Count;
						St.FocusDistinct += Count > 0 ? 1 : 0;
					}
					St.FocusMax += On.Num() > 0 ? On[0] : 0;
					St.FocusTop3 += (On.Num() > 0 ? On[0] : 0) + (On.Num() > 1 ? On[1] : 0) + (On.Num() > 2 ? On[2] : 0);
					St.FocusHeavyRuns += (On.Num() > 0 && On[0] >= 10) ? 1 : 0;
					const FShiftFlowStats& Flow = Run.GetFlowStats();
					for (int32 From = 0; From < 4; ++From)
					{
						for (int32 To = 0; To < 4; ++To)
						{
							St.Flow.PlugMoves[From][To] += Flow.PlugMoves[From][To];
							St.Flow.PlugWeight[From][To] += Flow.PlugWeight[From][To];
							St.Flow.HerdMoves[From][To] += Flow.HerdMoves[From][To];
							St.Flow.HerdWeight[From][To] += Flow.HerdWeight[From][To];
							St.Flow.BigMoves[From][To] += Flow.BigMoves[From][To];
						}
					}
					double BestOther = 0.0;
					for (int32 E = 0; E < S.NumEntries(); ++E)
					{
						if (E != ShiftSim::kPlayerId && E != S.RivalId())
						{
							BestOther = FMath::Max(BestOther, S.Share(E));
							St.OthersOnPodium += S.RankOf(E) < 3 ? 1 : 0;
						}
					}
					St.BestOtherShare += BestOther;
					const int32 Leader = S.Leader();
					St.OtherLeads += (Leader != ShiftSim::kPlayerId && Leader != S.RivalId()) ? 1 : 0;
				}
				St.RivalAttacks += Run.RivalPolicy().Attacks;
				St.RivalDefends += Run.RivalPolicy().Defends;
				St.RivalStarved += Run.RivalPolicy().Starved;
				if (Run.GetPushStart().bSet)
				{
					const FShiftPushSnapshot& Start = Run.GetPushStart();
					const ShiftSim::AttackCounters& Now = S.Attacks();
					++St.PushRuns;
					St.PushAttacks += Run.RivalPolicy().Attacks - Start.RivalAttacks;
					St.PushStarved += Run.RivalPolicy().Starved - Start.RivalStarved;
					St.PushQueued += Now.queued - Start.Threats.queued;
					St.PushLanded += Now.landed - Start.Threats.landed;
					St.PushFlipped += Now.flipped - Start.Threats.flipped;
					St.PushParried += Now.parried - Start.Threats.parried;
					St.PushBlocked += Now.blocked - Start.Threats.blocked;
					St.PushLost += Run.GetFlipStats().LossRivalPush;
					St.PushPostsRally += Now.postsRally - Start.Threats.postsRally;
					St.PushPostsParry += Now.postsParry - Start.Threats.postsParry;
				}
				const FShiftBlameStats& Blame = Run.GetBlameStats();
				St.BlameStrikes += Blame.Strikes;
				St.BlameFakeSum += Blame.StrikeFakeSum;
				St.BlameFatal += Blame.Fatal;
				St.BlameOffered += Blame.Offered;
				St.BlameTaken += Blame.Taken;
				St.BlameRefused += Blame.Refused;
				St.BlameLapsed += Blame.Lapsed;
				for (const float Basis : Blame.FatalBasis)
				{
					const float Pct = Basis * 100.0f;
					St.FatalFakeBins[Pct < 20.0f ? FMath::FloorToInt(Pct / 4.0f) : (Pct < 30.0f ? 5 : (Pct < 40.0f ? 6 : 7))]++;
				}
				if (R % 40 == 39)
				{
					CollectGarbage(RF_NoFlags);
				}
			}
			const double N = FMath::Max(1, St.Runs);
			UE_LOG(LogShift, Display, TEXT("jam=%d bot=%-6s runs=%d  win %5.1f%%  dq %5.1f%%  rank %.2f  share %5.1f%%  sock %5.1f%%  strikes %.2f  rival %5.1f%%  R2(quality) %.2f  R2(fame) %.2f"),
				Jam, *BotName, St.Runs, 100.0 * St.Wins / N, 100.0 * St.Dq / N, St.RankSum / N, 100.0 * St.ShareSum / N, 100.0 * St.SockSum / N,
				St.StrikeSum / N, 100.0 * St.RivalShare / N, St.R2Quality / N, St.R2Fame / N);
			// A separate line (it does not start with "jam=", so the determinism compare in Tools/verify.ps1 ignores it): how the player's voters came and went, per run.
			// Dwell = seconds a flipped voter stayed the player's before being lost again; the voters still held at the end are not counted.
			UE_LOG(LogShift, Display, TEXT("Balance: flips jam=%d bot=%-8s per run: gains %.1f (sock %.1f)  lost %.1f (herd %.1f rival %.1f others %.1f)  strike-reverts %.1f  audit-purged %.1f  dwell plug %.1fs (%d)  sock %.1fs (%d)  threats %.1f (landed %.1f flipped %.1f blocked %.1f fizzled %.1f shield-blocked %.1f parried %.1f)  posts: rally %.1f parry %.1f"),
				Jam, *BotName, St.Flips.Gains / N, St.Flips.SockGains / N, St.Flips.Losses / N, St.Flips.LossHerd / N, St.Flips.LossRival / N, St.Flips.LossOthers / N, St.Flips.StrikeReverts / N, St.Flips.AuditPurges / N,
				St.Flips.PlugDwellCount > 0 ? St.Flips.PlugDwellSum / St.Flips.PlugDwellCount : 0.0, St.Flips.PlugDwellCount,
				St.Flips.SockDwellCount > 0 ? St.Flips.SockDwellSum / St.Flips.SockDwellCount : 0.0, St.Flips.SockDwellCount,
				St.Threats.queued / N, St.Threats.landed / N, St.Threats.flipped / N, St.Threats.blocked / N, St.Threats.fizzled / N, St.Threats.shieldBlocked / N, St.Threats.parried / N, St.Threats.postsRally / N, St.Threats.postsParry / N);
			// The rival (a separate line): per run, its attacks, its re-plugs of weak hubs and the actions its Post bank refused (the final push drains it).
			UE_LOG(LogShift, Display, TEXT("Balance: rival jam=%d bot=%-8s per run: attacks %.1f  defends %.1f  starved %.1f"), Jam, *BotName, St.RivalAttacks / N, St.RivalDefends / N, St.RivalStarved / N);
			// Where the rival's attacks went (a separate line): per run, how many, on how many different voters, the most on one voter, the three most (and their share of the attacks), and in how many runs
			// one voter took 10 or more (the playtester: "it'll try to plug that voter for the entire game").
			UE_LOG(LogShift, Display, TEXT("Balance: focus jam=%d bot=%-8s per run: rival attacks %.1f on %.1f different voters, the most on one voter %.1f, the three most %.1f (%.0f%% of its attacks)  runs with 10 or more on one voter %d"),
				Jam, *BotName, St.FocusAttacks / N, St.FocusDistinct / N, St.FocusMax / N, St.FocusTop3 / N, St.FocusAttacks > 0 ? 100.0 * St.FocusTop3 / St.FocusAttacks : 0.0, St.FocusHeavyRuns);
			// Who moved the votes (a separate line): per run, voters and (in brackets) points of all vote weight. The other entries' plugs, the rival's, the player's, and opinion pressure.
			{
				const auto Plugs = [&St, N](const int32 To, const int32 From) { return FString::Printf(TEXT("%.1f (%.1f)"), St.Flow.PlugMoves[From][To] / N, 100.0 * St.Flow.PlugWeight[From][To] / N); };
				const auto Herd = [&St, N](const int32 To) {
					int32 Count = 0;
					double Weight = 0.0;
					for (int32 From = 0; From < 4; ++From)
					{
						Count += From != To ? St.Flow.HerdMoves[From][To] : 0;
						Weight += From != To ? St.Flow.HerdWeight[From][To] : 0.0;
					}
					return FString::Printf(TEXT("%.1f (%.1f)"), Count / N, 100.0 * Weight / N);
				};
				UE_LOG(LogShift, Display, TEXT("Balance: flow jam=%d bot=%-8s per run, voters (points of the vote): other entries' plugs took from you %s, from the rival %s, from each other %s, from undecided %s | rival's plugs took from you %s, from others %s, from undecided %s | your plugs took from the rival %s, from others %s, from undecided %s | opinion pressure moved to the rival %s, to others %s, to you %s"),
					Jam, *BotName, *Plugs(2, 0), *Plugs(2, 1), *Plugs(2, 2), *Plugs(2, 3), *Plugs(1, 0), *Plugs(1, 2), *Plugs(1, 3), *Plugs(0, 1), *Plugs(0, 2), *Plugs(0, 3), *Herd(1), *Herd(2), *Herd(0));
			}
			// The celebrities (a separate line): how often the player holds one, how soon, and what holding them is worth (wins and final share by how many it holds at the end).
			{
				const auto Pct = [&St](const int32 Count) { return 100.0 * Count / FMath::Max(1, St.Runs); };
				const auto Won = [&St](const int32 Held) { return St.CelebEndRuns[Held] > 0 ? 100.0 * St.CelebEndWins[Held] / St.CelebEndRuns[Held] : -1.0; };
				const auto Share = [&St](const int32 Held) { return St.CelebEndRuns[Held] > 0 ? 100.0 * St.CelebEndShare[Held] / St.CelebEndRuns[Held] : -1.0; };
				UE_LOG(LogShift, Display, TEXT("Balance: celeb jam=%d bot=%-8s player held a celebrity at some point in %.0f%% of runs (first at %.0f s, held %.0f%% of the run), at the end both %.0f%% one %.0f%% none %.0f%% | wins with both %.0f%% one %.0f%% none %.0f%% | final share with both %.1f%% one %.1f%% none %.1f%% | the rival keeps both in %.0f%% | half way both %.0f%% (wins %.0f%%) none %.0f%% (wins %.0f%%)"),
					Jam, *BotName, Pct(St.CelebAny), St.CelebAny > 0 ? St.CelebFirstSum / St.CelebAny : 0.0, 100.0 * St.CelebHeldFraction / N, Pct(St.CelebEndRuns[2]), Pct(St.CelebEndRuns[1]), Pct(St.CelebEndRuns[0]),
					Won(2), Won(1), Won(0), Share(2), Share(1), Share(0), Pct(St.CelebRivalBoth), Pct(St.CelebMidRuns[2]), St.CelebMidRuns[2] > 0 ? 100.0 * St.CelebMidWins[2] / St.CelebMidRuns[2] : -1.0,
					Pct(St.CelebMidRuns[0]), St.CelebMidRuns[0] > 0 ? 100.0 * St.CelebMidWins[0] / St.CelebMidRuns[0] : -1.0);
			}
			// The big voters (hubs and celebrities, Params::HighValueV and up) moved by plugs (a separate line), per run: what the other entries, the rival and you took from whom.
			UE_LOG(LogShift, Display, TEXT("Balance: big jam=%d bot=%-8s per run, voters worth %.0f+ moved by plugs: other entries took from the rival %.2f, from you %.2f, from each other %.2f | the rival took from you %.2f, from others %.2f | you took from the rival %.2f, from others %.2f"),
				Jam, *BotName, ShiftSim::Params().HighValueV, St.Flow.BigMoves[1][2] / N, St.Flow.BigMoves[0][2] / N, St.Flow.BigMoves[2][2] / N, St.Flow.BigMoves[0][1] / N, St.Flow.BigMoves[2][1] / N, St.Flow.BigMoves[1][0] / N, St.Flow.BigMoves[2][0] / N);
			// The board at the end (a separate line): the player's and the rival's share, the best other entry's, how many other entries hold a top-3 place on average, and in how many runs an other entry leads.
			UE_LOG(LogShift, Display, TEXT("Balance: field jam=%d bot=%-8s final share: you %.1f%%  rival %.1f%%  best other %.1f%%   other entries in the top 3: %.2f   an other entry leads: %d of %d runs"),
				Jam, *BotName, 100.0 * St.ShareSum / N, 100.0 * St.RivalShare / N, 100.0 * St.BestOtherShare / N, St.OthersOnPodium / N, St.OtherLeads, St.Runs);
			// The rival's final push alone (a separate line): per run, its attacks, the announced attacks on the player (queued; landed, of which flipped; parried; blocked by a shield or a fresh flip),
			// the voters it took, and the Posts the player spent on Rallies and parries.
			{
				const double R = FMath::Max(1, St.PushRuns);
				UE_LOG(LogShift, Display, TEXT("Balance: push jam=%d bot=%-8s per run: rival attacks %.1f (starved %.1f)  announced on you %.1f: landed %.1f (flipped %.1f) parried %.1f blocked %.1f  voters lost to the rival %.1f  your Posts: rally %.1f parry %.1f"),
					Jam, *BotName, St.PushAttacks / R, St.PushStarved / R, St.PushQueued / R, St.PushLanded / R, St.PushFlipped / R, St.PushParried / R, St.PushBlocked / R, St.PushLost / R, St.PushPostsRally / R, St.PushPostsParry / R);
			}
			// Shift the Blame (a separate line, like the others): strikes per run and the fake share they took, the strikes that would disqualify and what became of them (offered, taken,
			// refused for too many fake votes, let lapse), and how many fatal strikes fell in each band of fake share (the cap, Params::BlameMaxFake, sits between two of them).
			UE_LOG(LogShift, Display, TEXT("Balance: blame jam=%d bot=%-8s per run: strikes %.2f (fake %.1f%% each)  fatal %.2f  offered %.2f  taken %.2f  refused %.2f  lapsed %.2f   fatal fake share <4%% %d  4-8 %d  8-12 %d  12-16 %d  16-20 %d  20-30 %d  30-40 %d  40+ %d"),
				Jam, *BotName, St.BlameStrikes / N, St.BlameStrikes > 0 ? 100.0 * St.BlameFakeSum / St.BlameStrikes : 0.0, St.BlameFatal / N, St.BlameOffered / N, St.BlameTaken / N, St.BlameRefused / N, St.BlameLapsed / N,
				St.FatalFakeBins[0], St.FatalFakeBins[1], St.FatalFakeBins[2], St.FatalFakeBins[3], St.FatalFakeBins[4], St.FatalFakeBins[5], St.FatalFakeBins[6], St.FatalFakeBins[7]);
			// The rival's final push (the last MegaDays days): the player's share when it starts and when the run ends, and how often a lead at the start survives.
			UE_LOG(LogShift, Display, TEXT("Balance: mega jam=%d bot=%-8s share at start %.1f%% at end %.1f%% swing %+.1f  led at start %d, kept %d"),
				Jam, *BotName, 100.0 * St.MegaStartShare / N, 100.0 * St.MegaEndShare / N, 100.0 * (St.MegaEndShare - St.MegaStartShare) / N, St.MegaLeadAtStart, St.MegaLeadKept);
			Csv += FString::Printf(TEXT("%d,%s,%d,%.2f,%.2f,%.3f,%.2f,%.2f,%.3f,%.2f,%.3f,%.3f\n"), Jam, *BotName, St.Runs, 100.0 * St.Wins / N, 100.0 * St.Dq / N,
				St.RankSum / N, 100.0 * St.ShareSum / N, 100.0 * St.SockSum / N, St.StrikeSum / N, 100.0 * St.RivalShare / N, St.R2Quality / N, St.R2Fame / N);
		}
	}

	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Balance"), FString::Printf(TEXT("balance_%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	FFileHelper::SaveStringToFile(Csv, *Path);
	UE_LOG(LogShift, Display, TEXT("Balance table written to %s"), *Path);
	return 0;
}
