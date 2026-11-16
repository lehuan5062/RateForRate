// Shift Happens - Game Off 2026

#include "Game/ShiftBalanceCommandlet.h"

#include "Behavior/ShiftPolicies.h"
#include "Behavior/StateTree/ShiftBehavior.h"
#include "Containers/Array.h"
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
#include "Templates/SharedPointer.h"
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
	};

	bool ParseBot(const FString& Name, ShiftSim::BotKind& Out)
	{
		if (Name.Equals(TEXT("idle"), ESearchCase::IgnoreCase)) { Out = ShiftSim::BotKind::Idle; return true; }
		if (Name.Equals(TEXT("random"), ESearchCase::IgnoreCase)) { Out = ShiftSim::BotKind::Random; return true; }
		if (Name.Equals(TEXT("greedy"), ESearchCase::IgnoreCase)) { Out = ShiftSim::BotKind::Greedy; return true; }
		if (Name.Equals(TEXT("sock"), ESearchCase::IgnoreCase)) { Out = ShiftSim::BotKind::GreedySock; return true; }
		return false;
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
	FParse::Value(*Params, TEXT("Bot="), BotArg);
	FParse::Value(*Params, TEXT("Build="), BuildArg, false);
	const bool bNoStateTree = FParse::Param(*Params, TEXT("NoStateTree"));
	const bool bMods = !FParse::Param(*Params, TEXT("NoMods"));

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
	if (BotArg.Equals(TEXT("all"), ESearchCase::IgnoreCase)) { BotNames = { TEXT("idle"), TEXT("random"), TEXT("greedy"), TEXT("sock") }; }
	else { BotArg.ParseIntoArray(BotNames, TEXT(",")); }

	UE_LOG(LogShift, Display, TEXT("Balance: %d runs per cell, build %d/%d/%d, behaviour %s, mods %d"), Runs, Build.Quality, Build.Thumbnail, Build.Network,
		bNoStateTree ? TEXT("native") : TEXT("StateTree"), bMods ? 1 : 0);

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
				if (R % 40 == 39)
				{
					CollectGarbage(RF_NoFlags);
				}
			}
			const double N = FMath::Max(1, St.Runs);
			UE_LOG(LogShift, Display, TEXT("jam=%d bot=%-6s runs=%d  win %5.1f%%  dq %5.1f%%  rank %.2f  share %5.1f%%  sock %5.1f%%  strikes %.2f  rival %5.1f%%  R2(quality) %.2f  R2(fame) %.2f"),
				Jam, *BotName, St.Runs, 100.0 * St.Wins / N, 100.0 * St.Dq / N, St.RankSum / N, 100.0 * St.ShareSum / N, 100.0 * St.SockSum / N,
				St.StrikeSum / N, 100.0 * St.RivalShare / N, St.R2Quality / N, St.R2Fame / N);
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
