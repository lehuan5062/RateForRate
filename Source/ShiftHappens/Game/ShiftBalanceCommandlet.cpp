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
	};

	bool ParseBot(const FString& Name, ShiftSim::BotKind& Out)
	{
		return ShiftSim::BotKindFromName(std::string(TCHAR_TO_UTF8(*Name)), &Out);
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
			if (!ParseBot(BotName, Kind))
			{
				UE_LOG(LogShift, Error, TEXT("Unknown bot %s"), *BotName);
				continue;
			}
			FStats St;
			for (int32 R = 0; R < Runs; ++R)
			{
				const uint32 Seed = static_cast<uint32>(Seed0 + R * 7919);
				FShiftRun Run;
				Run.SetOverrides(Overrides);
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
				while (!Run.IsOver())
				{
					Run.Tick(0.25);
				}
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
			UE_LOG(LogShift, Display, TEXT("Balance: flips jam=%d bot=%-8s per run: gains %.1f (sock %.1f)  lost %.1f (herd %.1f rival %.1f others %.1f)  strike-reverts %.1f  audit-purged %.1f  dwell plug %.1fs (%d)  sock %.1fs (%d)  threats %.1f (landed %.1f flipped %.1f blocked %.1f fizzled %.1f)"),
				Jam, *BotName, St.Flips.Gains / N, St.Flips.SockGains / N, St.Flips.Losses / N, St.Flips.LossHerd / N, St.Flips.LossRival / N, St.Flips.LossOthers / N, St.Flips.StrikeReverts / N, St.Flips.AuditPurges / N,
				St.Flips.PlugDwellCount > 0 ? St.Flips.PlugDwellSum / St.Flips.PlugDwellCount : 0.0, St.Flips.PlugDwellCount,
				St.Flips.SockDwellCount > 0 ? St.Flips.SockDwellSum / St.Flips.SockDwellCount : 0.0, St.Flips.SockDwellCount,
				St.Threats.queued / N, St.Threats.landed / N, St.Threats.flipped / N, St.Threats.blocked / N, St.Threats.fizzled / N);
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
