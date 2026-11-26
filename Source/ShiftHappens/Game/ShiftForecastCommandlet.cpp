// Rate for Rate - Game Off 2026

#include "Game/ShiftForecastCommandlet.h"

#include "Async/ParallelFor.h"
#include <atomic>
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Game/ShiftBuild.h"
#include "Game/ShiftForecast.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/FileManager.h"
#include "HAL/Platform.h"
#include "HAL/PlatformTime.h"
#include "Logging/LogMacros.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/CString.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShiftHappens.h"
#include "Templates/SharedPointer.h"

UShiftForecastCommandlet::UShiftForecastCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

namespace
{
	constexpr int32 MaxEntries = 9;

	FShiftBuild MakeBuild(int32 Q, int32 T, int32 N)
	{
		FShiftBuild B;
		B.Quality = Q;
		B.Thumbnail = T;
		B.Network = N;
		return B;
	}

	/** The builds a player can reach (at most 12 hours): every split in steps of two hours that spends all twelve, and each lever alone from 2 / 2 / 2 up and down. */
	TArray<FShiftBuild> StudyBuilds(const FString& Set)
	{
		TArray<FShiftBuild> Out;
		auto AddUnique = [&Out](const FShiftBuild& B)
			{
				for (const FShiftBuild& O : Out)
				{
					if (O.Quality == B.Quality && O.Thumbnail == B.Thumbnail && O.Network == B.Network)
					{
						return;
					}
				}
				Out.Add(B);
			};
		if (Set == TEXT("grid"))
		{
			for (int32 Q = 0; Q <= 8; Q += 2)
			{
				for (int32 T = 0; T <= 8; T += 2)
				{
					for (int32 N = 0; N <= 8; N += 2)
					{
						if (Q + T + N <= FShiftBuild::TotalHours)
						{
							AddUnique(MakeBuild(Q, T, N));
						}
					}
				}
			}
			return Out;
		}
		if (Set != TEXT("levers"))
		{
			for (int32 Q = 0; Q <= 8; Q += 2)
			{
				for (int32 T = 0; T <= 8; T += 2)
				{
					const int32 N = 12 - Q - T;
					if (N >= 0 && N <= 8)
					{
						AddUnique(MakeBuild(Q, T, N));
					}
				}
			}
		}
		if (Set != TEXT("splits"))
		{
			for (int32 H = 0; H <= 8; H += 2)
			{
				AddUnique(MakeBuild(H, 2, 2));
				AddUnique(MakeBuild(2, H, 2));
				AddUnique(MakeBuild(2, 2, H));
			}
		}
		return Out;
	}
}

int32 UShiftForecastCommandlet::Main(const FString& Params)
{
	int32 Seed0 = 1000, Worlds = 16, K = 16, Samples = 8, Mode = 7, Seed = 1000;
	double Noise = 0.3;
	FString JamArg = TEXT("all"), ModesArg = TEXT("1,2,4,6"), BuildArg, Tag = TEXT("last"), BuildSet = TEXT("all");
	FParse::Value(*Params, TEXT("Seed0="), Seed0);
	FParse::Value(*Params, TEXT("Seed="), Seed);
	FParse::Value(*Params, TEXT("Worlds="), Worlds);
	FParse::Value(*Params, TEXT("K="), K);
	FParse::Value(*Params, TEXT("Samples="), Samples);
	FParse::Value(*Params, TEXT("Mode="), Mode);
	FParse::Value(*Params, TEXT("Noise="), Noise);
	FParse::Value(*Params, TEXT("Jam="), JamArg);
	FParse::Value(*Params, TEXT("Modes="), ModesArg, false);
	FParse::Value(*Params, TEXT("Build="), BuildArg, false);
	FParse::Value(*Params, TEXT("Tag="), Tag);
	FParse::Value(*Params, TEXT("BuildSet="), BuildSet);

	TArray<int32> Jams;
	if (JamArg.Equals(TEXT("all"), ESearchCase::IgnoreCase)) { for (int32 J = 0; J < ShiftSim::kJamCount; ++J) { Jams.Add(J); } }
	else { Jams.Add(FCString::Atoi(*JamArg)); }

	if (FParse::Param(*Params, TEXT("Model")))
	{
		// The forecast model of every world (what the build screen uses): the smoothed share of every entry for every build the screen allows, one CSV row per build.
		FString Csv = TEXT("jam,world,seed,q,t,n,spread,s0,s1,s2,s3,s4,s5,s6,s7,s8\n");
		for (const int32 Jam : Jams)
		{
			for (int32 World = 0; World < Worlds; ++World)
			{
				const uint32 WorldSeed = static_cast<uint32>(Seed0 + World * 7919);
				std::atomic<bool> bCancel(false);
				std::atomic<int32> Done(0);
				const double T0 = FPlatformTime::Seconds();
				const TSharedPtr<const FShiftForecastModel> Model = ShiftForecastRun::BuildModel(Jam, WorldSeed, bCancel, Done);
				const double Ms = 1000.0 * (FPlatformTime::Seconds() - T0);
				if (!Model.IsValid())
				{
					UE_LOG(LogShift, Error, TEXT("Forecast: model failed for jam %d seed %u"), Jam, WorldSeed);
					continue;
				}
				const FShiftForecast Neutral = Model->Evaluate(FShiftBuild());
				UE_LOG(LogShift, Display, TEXT("Forecast: model jam=%d world=%d seed=%u  %d plays in %.0f ms  spread %.1f  at 4/4/4 the player gets %.1f%% (rank %d-%d)"), Jam, World, WorldSeed, Done.load(), Ms,
					100.0 * Model->PlayerSpread, 100.0 * Neutral.PlayerShare, Neutral.RankLo, Neutral.RankHi);
				for (int32 Q = 0; Q <= FShiftForecastModel::MaxHours; ++Q)
				{
					for (int32 T = 0; T <= FShiftForecastModel::MaxHours; ++T)
					{
						for (int32 N = 0; N <= FShiftForecastModel::MaxHours; ++N)
						{
							if (!FShiftForecastModel::IsLegal(Q, T, N))
							{
								continue;
							}
							Csv += FString::Printf(TEXT("%d,%d,%u,%d,%d,%d,%.5f"), Jam, World, WorldSeed, Q, T, N, Model->PlayerSpread);
							const int32 Cell = FShiftForecastModel::CellOf(Q, T, N);
							for (int32 E = 0; E < FMath::Min(MaxEntries, Model->NumEntries); ++E)
							{
								Csv += FString::Printf(TEXT(",%.5f"), Model->Table[Cell * Model->NumEntries + E]);
							}
							Csv += TEXT("\n");
						}
					}
				}
			}
		}
		const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Forecast"), FString::Printf(TEXT("model_%s.csv"), *Tag));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveStringToFile(Csv, *Path);
		UE_LOG(LogShift, Display, TEXT("Forecast: model table written to %s"), *Path);
		return 0;
	}

	if (!FParse::Param(*Params, TEXT("Study")))
	{
		FShiftBuild Build;
		TArray<FString> Parts;
		BuildArg.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 3)
		{
			Build = MakeBuild(FCString::Atoi(*Parts[0]), FCString::Atoi(*Parts[1]), FCString::Atoi(*Parts[2]));
		}
		const int32 Jam = Jams.Num() > 0 ? Jams[0] : 0;
		TArray<double> Sum;
		int32 Done = 0;
		for (int32 S = 0; S < Samples; ++S)
		{
			TArray<float> Share;
			if (!ShiftForecastRun::PlaySample(Jam, static_cast<uint32>(Seed), Build, ShiftForecastRun::MakeVariant(static_cast<uint32>(Seed), S, Mode, Noise), Share))
			{
				continue;
			}
			Sum.SetNumZeroed(Share.Num());
			FString Line;
			for (int32 E = 0; E < Share.Num(); ++E)
			{
				Sum[E] += Share[E];
				Line += FString::Printf(TEXT(" %5.1f"), 100.0f * Share[E]);
			}
			++Done;
			UE_LOG(LogShift, Display, TEXT("Forecast: jam=%d seed=%d build=%d/%d/%d sample %d:%s"), Jam, Seed, Build.Quality, Build.Thumbnail, Build.Network, S, *Line);
		}
		FString Line;
		for (int32 E = 0; E < Sum.Num(); ++E)
		{
			Line += FString::Printf(TEXT(" %5.1f"), 100.0 * Sum[E] / FMath::Max(1, Done));
		}
		UE_LOG(LogShift, Display, TEXT("Forecast: jam=%d seed=%d build=%d/%d/%d mean of %d (mode %d):%s"), Jam, Seed, Build.Quality, Build.Thumbnail, Build.Network, Done, Mode, *Line);
		return 0;
	}

	TArray<int32> Modes;
	{
		TArray<FString> Parts;
		ModesArg.ParseIntoArray(Parts, TEXT(","));
		for (const FString& Part : Parts)
		{
			Modes.Add(FCString::Atoi(*Part));
		}
	}
	const TArray<FShiftBuild> Builds = StudyBuilds(BuildSet);
	const int32 NumJams = Jams.Num(), NumBuilds = Builds.Num(), NumModes = Modes.Num();
	const int64 Total = static_cast<int64>(NumJams) * Worlds * NumBuilds * NumModes * K;
	UE_LOG(LogShift, Display, TEXT("Forecast study: %d jams x %d worlds x %d builds x %d modes x %d samples = %lld plays"), NumJams, Worlds, NumBuilds, NumModes, K, Total);

	TArray<float> Shares;
	Shares.SetNumZeroed(static_cast<int32>(Total) * MaxEntries);
	TArray<uint8> Ok;
	Ok.SetNumZeroed(static_cast<int32>(Total));
	const double Start = FPlatformTime::Seconds();
	ParallelFor(static_cast<int32>(Total), [&](int32 Index)
		{
			int32 Rest = Index;
			const int32 Sample = Rest % K;               Rest /= K;
			const int32 ModeIndex = Rest % NumModes;     Rest /= NumModes;
			const int32 BuildIndex = Rest % NumBuilds;   Rest /= NumBuilds;
			const int32 World = Rest % Worlds;           Rest /= Worlds;
			const int32 Jam = Jams[Rest];
			const uint32 WorldSeed = static_cast<uint32>(Seed0 + World * 7919);
			TArray<float> Share;
			if (ShiftForecastRun::PlaySample(Jam, WorldSeed, Builds[BuildIndex], ShiftForecastRun::MakeVariant(WorldSeed, Sample, Modes[ModeIndex], Noise), Share))
			{
				Ok[Index] = 1;
				for (int32 E = 0; E < FMath::Min(MaxEntries, Share.Num()); ++E)
				{
					Shares[Index * MaxEntries + E] = Share[E];
				}
			}
		});
	const double Seconds = FPlatformTime::Seconds() - Start;
	UE_LOG(LogShift, Display, TEXT("Forecast study: %lld plays in %.1f s (%.1f ms each, on the worker pool)"), Total, Seconds, 1000.0 * Seconds / FMath::Max<int64>(1, Total));

	FString Csv = TEXT("jam,world,seed,q,t,n,mode,k,rank,s0,s1,s2,s3,s4,s5,s6,s7,s8\n");
	for (int32 Index = 0; Index < Total; ++Index)
	{
		if (Ok[Index] == 0)
		{
			continue;
		}
		int32 Rest = Index;
		const int32 Sample = Rest % K;               Rest /= K;
		const int32 ModeIndex = Rest % NumModes;     Rest /= NumModes;
		const int32 BuildIndex = Rest % NumBuilds;   Rest /= NumBuilds;
		const int32 World = Rest % Worlds;           Rest /= Worlds;
		const int32 Jam = Jams[Rest];
		const float* Row = &Shares[Index * MaxEntries];
		int32 Rank = 1;
		for (int32 E = 1; E < MaxEntries; ++E)
		{
			Rank += Row[E] > Row[0] ? 1 : 0;
		}
		const FShiftBuild& B = Builds[BuildIndex];
		Csv += FString::Printf(TEXT("%d,%d,%d,%d,%d,%d,%d,%d,%d"), Jam, World, Seed0 + World * 7919, B.Quality, B.Thumbnail, B.Network, Modes[ModeIndex], Sample, Rank);
		for (int32 E = 0; E < MaxEntries; ++E)
		{
			Csv += FString::Printf(TEXT(",%.5f"), Row[E]);
		}
		Csv += TEXT("\n");
	}
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Forecast"), FString::Printf(TEXT("study_%s.csv"), *Tag));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	FFileHelper::SaveStringToFile(Csv, *Path);
	UE_LOG(LogShift, Display, TEXT("Forecast study written to %s"), *Path);
	return 0;
}
