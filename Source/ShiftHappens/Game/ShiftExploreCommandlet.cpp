// Shift Happens - Game Off 2026

#include "Game/ShiftExploreCommandlet.h"

#include "Behavior/ShiftPolicies.h"
#include "Behavior/StateTree/ShiftBehavior.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Game/ShiftBuild.h"
#include "Game/ShiftRun.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/FileManager.h"
#include "HAL/Platform.h"
#include "Logging/LogMacros.h"
#include "Math/RandomStream.h"
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
#include "Templates/TupleFwd.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

UShiftExploreCommandlet::UShiftExploreCommandlet()
{
	IsClient = false;
	IsEditor = true;    // see UShiftBalanceCommandlet
	IsServer = false;
	LogToConsole = true;
}

namespace
{
	/** The outcome of one configuration on one jam over a fixed set of seeds. */
	struct FExploreCell
	{
		int32 Runs = 0;
		int32 Wins = 0;
		int32 Dq = 0;
		double ShareSum = 0.0;
		double SockSum = 0.0;

		double WinPct() const { return Runs > 0 ? 100.0 * Wins / Runs : 0.0; }
		double DqPct() const { return Runs > 0 ? 100.0 * Dq / Runs : 0.0; }
		double SharePct() const { return Runs > 0 ? 100.0 * ShareSum / Runs : 0.0; }
		double SockPct() const { return Runs > 0 ? 100.0 * SockSum / Runs : 0.0; }
		/** What the search maximises: the win rate, the share breaks ties. */
		double Score() const { return WinPct() + 0.01 * SharePct(); }
	};

	/** A configuration of the Explorer bot: the PolicyParams knobs by name. */
	using FExploreConfig = TMap<FString, double>;

	struct FExploreKnob
	{
		const TCHAR* Name;
		double Lo;
		double Hi;
		bool bSwitch;      // 0 or 1
		bool bMayBeZero;   // a continuous knob that is also allowed to be off (0)
	};

	const FExploreKnob ExploreKnobs[] = {
		{ TEXT("XReact"), 0.0, 1.0, true, false },
		{ TEXT("XRally"), 0.0, 1.0, true, false },
		{ TEXT("XSock"), 0.0, 1.0, true, false },
		{ TEXT("XBurst"), 0.0, 1.0, true, false },
		{ TEXT("XIdle"), 0.0, 1.0, true, false },
		{ TEXT("XReinforceBelow"), 2.0, 12.0, false, true },
		{ TEXT("XPlugMinV"), 10.0, 60.0, false, true },
		{ TEXT("XSockMinV"), 20.0, 300.0, false, true },
		{ TEXT("XSockShare"), 0.01, 0.12, false, true },
		{ TEXT("ReactDelay"), 0.3, 1.0, false, false },
		{ TEXT("ReactMinValue"), 20.0, 200.0, false, false },
		{ TEXT("ReactReserve"), 0.0, 3.0, false, false },
		{ TEXT("SockMaxHeat"), 15.0, 55.0, false, false },
		{ TEXT("LateSeconds"), 6.0, 24.0, false, false },
	};

	double ExploreSample(const FExploreKnob& K, FRandomStream& Rng)
	{
		if (K.bSwitch)
		{
			return Rng.FRand() < 0.5 ? 0.0 : 1.0;
		}
		if (K.bMayBeZero && Rng.FRand() < 0.5)
		{
			return 0.0;
		}
		return K.Lo + (K.Hi - K.Lo) * Rng.FRand();
	}

	FExploreConfig ExploreRandomConfig(FRandomStream& Rng)
	{
		FExploreConfig C;
		for (const FExploreKnob& K : ExploreKnobs)
		{
			C.Add(K.Name, ExploreSample(K, Rng));
		}
		return C;
	}

	FExploreConfig ExploreMutate(const FExploreConfig& From, FRandomStream& Rng)
	{
		FExploreConfig C = From;
		const FExploreKnob& K = ExploreKnobs[Rng.RandRange(0, UE_ARRAY_COUNT(ExploreKnobs) - 1)];
		if (K.bSwitch)
		{
			C.Add(K.Name, C.FindRef(K.Name) > 0.5 ? 0.0 : 1.0);
		}
		else if (Rng.FRand() < 0.3)
		{
			C.Add(K.Name, ExploreSample(K, Rng));
		}
		else
		{
			const double Cur = C.FindRef(K.Name);
			const double Step = (K.Hi - K.Lo) * 0.2 * (Rng.FRand() * 2.0 - 1.0);
			C.Add(K.Name, FMath::Clamp((Cur <= 0.0 ? K.Lo : Cur) + Step, K.Lo, K.Hi));
		}
		return C;
	}

	FExploreConfig ExplorePreset(bool bReact, bool bRally, bool bSock, bool bBurst, bool bIdle)
	{
		FExploreConfig C;
		C.Add(TEXT("XReact"), bReact ? 1.0 : 0.0);
		C.Add(TEXT("XRally"), bRally ? 1.0 : 0.0);
		C.Add(TEXT("XSock"), bSock ? 1.0 : 0.0);
		C.Add(TEXT("XBurst"), bBurst ? 1.0 : 0.0);
		C.Add(TEXT("XIdle"), bIdle ? 1.0 : 0.0);
		return C;
	}

	FString ExploreConfigText(const FExploreConfig& C)
	{
		FString Out;
		for (const FExploreKnob& K : ExploreKnobs)
		{
			if (const double* V = C.Find(K.Name))
			{
				Out += FString::Printf(TEXT("%s%s=%.4g"), Out.IsEmpty() ? TEXT("") : TEXT(","), K.Name, *V);
			}
		}
		return Out;
	}

	struct FExploreSetup
	{
		FShiftBuild Build;
		TMap<FString, double> BaseOverrides;
		bool bMods = true;
		bool bStateTree = true;
	};

	/** Plays Runs full jams with the given bot kind and overrides, seeds Seed0 + R * 7919 (the balance commandlet's seeds), and sums the outcomes. */
	FExploreCell ExploreEvaluate(const FExploreSetup& Setup, int32 Jam, ShiftSim::BotKind Kind, const FExploreConfig& Config, int32 Runs, int32 Seed0)
	{
		TMap<FString, double> Overrides = Setup.BaseOverrides;
		for (const TPair<FString, double>& Pair : Config)
		{
			Overrides.Add(Pair.Key, Pair.Value);
		}
		FExploreCell Cell;
		for (int32 R = 0; R < Runs; ++R)
		{
			FShiftRun Run;
			Run.SetOverrides(Overrides);
			if (!Run.Start(Jam, static_cast<uint32>(Seed0 + R * 7919), Setup.Build, Setup.bMods))
			{
				continue;
			}
			Run.SetBot(Kind);
			Run.SetTimeScale(60.0);
			if (Setup.bStateTree)
			{
				TSharedPtr<FShiftBehavior> Behavior = FShiftBehavior::Create(Run);
				if (!Behavior.IsValid())
				{
					UE_LOG(LogShift, Error, TEXT("StateTrees unavailable: run Tools/py/build_statetrees.py or pass -NoStateTree"));
					return Cell;
				}
				Run.SetDriver(Behavior);
			}
			while (!Run.IsOver())
			{
				Run.Tick(0.25);
			}
			const ShiftSim::Sim& S = Run.GetSim();
			++Cell.Runs;
			Cell.Dq += S.Disqualified() ? 1 : 0;
			Cell.Wins += (!S.Disqualified() && S.Leader() == ShiftSim::kPlayerId) ? 1 : 0;
			Cell.ShareSum += S.Share(ShiftSim::kPlayerId);
			Cell.SockSum += S.SockpuppetWeight() / FMath::Max(1.0, S.TotalWeight());
			if (R % 40 == 39)
			{
				CollectGarbage(RF_NoFlags);
			}
		}
		return Cell;
	}
}

int32 UShiftExploreCommandlet::Main(const FString& Params)
{
	int32 Samples = 30, Climb = 10, Runs = 40, Seed0 = 1000, SearchSeed = 1;
	FString JamArg = TEXT("all"), BuildArg, SetArg;
	FParse::Value(*Params, TEXT("Samples="), Samples);
	FParse::Value(*Params, TEXT("Climb="), Climb);
	FParse::Value(*Params, TEXT("Runs="), Runs);
	FParse::Value(*Params, TEXT("Seed0="), Seed0);
	FParse::Value(*Params, TEXT("Seed="), SearchSeed);
	FParse::Value(*Params, TEXT("Jam="), JamArg);
	FParse::Value(*Params, TEXT("Build="), BuildArg, false);
	FExploreSetup Setup;
	Setup.bStateTree = !FParse::Param(*Params, TEXT("NoStateTree"));
	Setup.bMods = !FParse::Param(*Params, TEXT("NoMods"));
	if (FParse::Value(*Params, TEXT("Set="), SetArg, false))
	{
		TArray<FString> Parts;
		SetArg.ParseIntoArray(Parts, TEXT(","));
		for (const FString& Part : Parts)
		{
			FString Key, Value;
			if (Part.Split(TEXT("="), &Key, &Value))
			{
				Setup.BaseOverrides.Add(Key.TrimStartAndEnd(), FCString::Atod(*Value));
			}
		}
	}
	if (!BuildArg.IsEmpty())
	{
		TArray<FString> Parts;
		BuildArg.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 3)
		{
			Setup.Build.Quality = FCString::Atoi(*Parts[0]);
			Setup.Build.Thumbnail = FCString::Atoi(*Parts[1]);
			Setup.Build.Network = FCString::Atoi(*Parts[2]);
		}
	}
	TArray<int32> Jams;
	if (JamArg.Equals(TEXT("all"), ESearchCase::IgnoreCase)) { Jams = { 0, 1, 2 }; }
	else { Jams.Add(FCString::Atoi(*JamArg)); }

	UE_LOG(LogShift, Display, TEXT("Explore: %d random samples + %d climb steps per jam, %d runs each, seeds from %d, search seed %d, behaviour %s, mods %d"),
		Samples, Climb, Runs, Seed0, SearchSeed, Setup.bStateTree ? TEXT("StateTree") : TEXT("native"), Setup.bMods ? 1 : 0);

	FString Csv = TEXT("jam,phase,win_pct,dq_pct,share_pct,sock_pct,config\n");
	auto AddRow = [&Csv](int32 Jam, const TCHAR* Phase, const FExploreCell& Cell, const FExploreConfig& Config)
		{
			Csv += FString::Printf(TEXT("%d,%s,%.2f,%.2f,%.2f,%.2f,\"%s\"\n"), Jam, Phase, Cell.WinPct(), Cell.DqPct(), Cell.SharePct(), Cell.SockPct(), *ExploreConfigText(Config));
		};

	for (const int32 Jam : Jams)
	{
		FRandomStream Rng(SearchSeed * 7919 + Jam * 104729 + 17);
		const FExploreConfig GreedyConfig = ExplorePreset(false, false, false, false, false);

		// the references: the honest bot and the reactor, on the search seeds
		const FExploreCell Greedy = ExploreEvaluate(Setup, Jam, ShiftSim::BotKind::Greedy, FExploreConfig(), Runs, Seed0);
		AddRow(Jam, TEXT("greedy"), Greedy, GreedyConfig);
		UE_LOG(LogShift, Display, TEXT("Explore: jam=%d greedy        win %5.1f%%  dq %4.1f%%  share %5.1f%%  sock %4.1f%%"), Jam, Greedy.WinPct(), Greedy.DqPct(), Greedy.SharePct(), Greedy.SockPct());

		FExploreConfig BestConfig = GreedyConfig;
		FExploreCell Best = ExploreEvaluate(Setup, Jam, ShiftSim::BotKind::Explorer, GreedyConfig, Runs, Seed0);
		FString BestName = TEXT("greedy");
		int32 Evaluated = 1;
		auto Consider = [&](const TCHAR* Name, const FExploreConfig& Config)
			{
				const FExploreCell Cell = ExploreEvaluate(Setup, Jam, ShiftSim::BotKind::Explorer, Config, Runs, Seed0);
				++Evaluated;
				AddRow(Jam, Name, Cell, Config);
				if (Cell.Score() > Best.Score())
				{
					Best = Cell;
					BestConfig = Config;
					BestName = Name;
					UE_LOG(LogShift, Display, TEXT("Explore: jam=%d new best (%s)  win %5.1f%%  dq %4.1f%%  share %5.1f%%  %s"), Jam, Name, Cell.WinPct(), Cell.DqPct(), Cell.SharePct(), *ExploreConfigText(Config));
				}
			};

		// the hand-written strategies first, so the search can only improve on them
		Consider(TEXT("preset:react"), ExplorePreset(true, false, false, false, false));
		Consider(TEXT("preset:reactor"), ExplorePreset(true, true, false, false, false));
		Consider(TEXT("preset:sock"), ExplorePreset(false, false, true, false, false));
		Consider(TEXT("preset:finisher"), ExplorePreset(false, false, false, true, false));
		Consider(TEXT("preset:late"), ExplorePreset(false, false, false, true, true));
		Consider(TEXT("preset:everything"), ExplorePreset(true, true, true, true, false));
		for (int32 I = 0; I < Samples; ++I)
		{
			Consider(TEXT("random"), ExploreRandomConfig(Rng));
		}
		for (int32 I = 0; I < Climb; ++I)
		{
			Consider(TEXT("climb"), ExploreMutate(BestConfig, Rng));
		}

		// validate on seeds the search never saw: the winner against greedy, twice as many runs
		const int32 ValRuns = Runs * 2;
		const int32 ValSeed = Seed0 + 1000003;
		const FExploreCell ValGreedy = ExploreEvaluate(Setup, Jam, ShiftSim::BotKind::Greedy, FExploreConfig(), ValRuns, ValSeed);
		const FExploreCell ValBest = ExploreEvaluate(Setup, Jam, ShiftSim::BotKind::Explorer, BestConfig, ValRuns, ValSeed);
		AddRow(Jam, TEXT("validate:greedy"), ValGreedy, GreedyConfig);
		AddRow(Jam, TEXT("validate:best"), ValBest, BestConfig);
		UE_LOG(LogShift, Display, TEXT("Explore: jam=%d best-response (%s, %d configurations)  search win %5.1f%%  validated win %5.1f%% vs greedy %5.1f%% = %+.1f points  dq %4.1f%%  share %5.1f%%  sock %4.1f%%"),
			Jam, *BestName, Evaluated, Best.WinPct(), ValBest.WinPct(), ValGreedy.WinPct(), ValBest.WinPct() - ValGreedy.WinPct(), ValBest.DqPct(), ValBest.SharePct(), ValBest.SockPct());
		UE_LOG(LogShift, Display, TEXT("Explore: jam=%d config %s"), Jam, *ExploreConfigText(BestConfig));
	}

	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Balance"), FString::Printf(TEXT("explore_%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	FFileHelper::SaveStringToFile(Csv, *Path);
	UE_LOG(LogShift, Display, TEXT("Explore table written to %s"), *Path);
	return 0;
}
