// Shift Happens - Game Off 2026

#include "Game/ShiftBuild.h"

#include "Behavior/ShiftPolicies.h"
#include "Containers/UnrealString.h"
#include "CoreGlobals.h"
#include "HAL/Platform.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/ConfigCacheIni.h"
#include "Sim/ShiftParams.h"
#include "Sim/ShiftSim.h"

namespace
{
	const TCHAR* const SaveSection = TEXT("ShiftHappens");
}

void ShiftBuildRules::Apply(const FShiftBuild& Build, ShiftSim::Params& Params, ShiftSim::JamDef& Jam)
{
	// Hours relative to an even 4/4/4 split. Each lever is worth something different:
	//   quality   : how good the game actually is (the player's appeal); matters, but fame outweighs it by design
	//   thumbnail : how many undecided voters even look at the entry (the player's clout)
	//   network   : followers on day one (start nodes) and how fast Posts come back
	const double Q = static_cast<double>(Build.Quality - 4);
	const double T = static_cast<double>(Build.Thumbnail - 4);
	const double N = static_cast<double>(Build.Network - 4);

	Params.PlayerBuildBonus = 0.45 * Q;
	Params.PlayerClout = FMath::Max(0.15, 1.0 + 0.5 * T) * Params.PlayerClout;
	Params.PlayerStartNodes = FMath::Clamp(Params.PlayerStartNodes + FMath::RoundToDouble(N / 3.0), 1.0, 4.0);
	Jam.regen *= 1.0 + 0.01 * N;
}

FShiftForecast ShiftBuildRules::Forecast(int32 JamIndex, uint32 Seed, const FShiftBuild& Build, int32 Samples)
{
	FShiftForecast Out;
	const int32 NumSamples = FMath::Clamp(Samples, 1, 8);
	ShiftSim::PolicyParams Policy;
	Policy.BotInterval = 2.5;   // a casual player: one Plug every couple of seconds, well below what the Posts economy allows

	for (int32 S = 0; S < NumSamples; ++S)
	{
		ShiftSim::Params Params;
		ShiftSim::JamDef Jam = ShiftSim::DefaultJam(FMath::Clamp(JamIndex, 0, 2));
		ShiftBuildRules::Apply(Build, Params, Jam);

		ShiftSim::Sim Sim;
		if (!Sim.Init(Jam, Params, Seed + static_cast<uint32>(S)))
		{
			continue;
		}
		ShiftSim::StandInRival Rival;
		ShiftSim::StandInOthers Others;
		ShiftSim::PlayerBot Casual;                 // a casual player: greedy targets, a Plug every couple of seconds
		Rival.Reset(Sim, Policy);
		Others.Reset(Sim, Policy);
		Casual.Reset(ShiftSim::BotKind::Greedy, Seed + static_cast<uint32>(S));
		while (!Sim.Finished())
		{
			Rival.Update(Sim, Policy);
			Others.Update(Sim, Policy);
			Casual.Update(Sim, Policy);
			Sim.Step();
		}

		if (S == 0)
		{
			Out.Quality.SetNumZeroed(Sim.NumEntries());
			Out.Share.SetNumZeroed(Sim.NumEntries());
			for (int32 E = 0; E < Sim.NumEntries(); ++E)
			{
				Out.Quality[E] = static_cast<float>(Sim.GetEntry(E).quality);
			}
		}
		for (int32 E = 0; E < Sim.NumEntries(); ++E)
		{
			Out.Share[E] += static_cast<float>(Sim.Share(E)) / static_cast<float>(NumSamples);
		}
	}

	const int32 N = Out.Share.Num();
	for (int32 E = 0; E < N; ++E)
	{
		Out.QualityOrder.Add(E);
		Out.ShareOrder.Add(E);
	}
	Out.QualityOrder.Sort([&Out](int32 A, int32 B) { return Out.Quality[A] > Out.Quality[B]; });
	Out.ShareOrder.Sort([&Out](int32 A, int32 B) { return Out.Share[A] > Out.Share[B]; });
	Out.QualityRank = Out.QualityOrder.IndexOfByKey(ShiftSim::kPlayerId) + 1;
	Out.PlayerRank = Out.ShareOrder.IndexOfByKey(ShiftSim::kPlayerId) + 1;
	Out.PlayerShare = N > ShiftSim::kPlayerId ? Out.Share[ShiftSim::kPlayerId] : 0.0f;
	return Out;
}

int32 ShiftSave::BestRank(int32 JamIndex)
{
	int32 Value = 0;
	if (GConfig)
	{
		GConfig->GetInt(SaveSection, *FString::Printf(TEXT("BestRank%d"), JamIndex), Value, GGameUserSettingsIni);
	}
	return Value;
}

void ShiftSave::RecordResult(int32 JamIndex, int32 Rank)
{
	if (!GConfig || Rank <= 0)
	{
		return;
	}
	const int32 Best = BestRank(JamIndex);
	if (Best == 0 || Rank < Best)
	{
		GConfig->SetInt(SaveSection, *FString::Printf(TEXT("BestRank%d"), JamIndex), Rank, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

bool ShiftSave::IsMuted()
{
	bool bValue = false;
	if (GConfig)
	{
		GConfig->GetBool(SaveSection, TEXT("Muted"), bValue, GGameUserSettingsIni);
	}
	return bValue;
}

void ShiftSave::SetMuted(bool bMuted)
{
	if (GConfig)
	{
		GConfig->SetBool(SaveSection, TEXT("Muted"), bMuted, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

int32 ShiftSave::TutorialSeenMask()
{
	int32 Value = 0;
	if (GConfig)
	{
		GConfig->GetInt(SaveSection, TEXT("TutorialSeen"), Value, GGameUserSettingsIni);
	}
	return Value;
}

void ShiftSave::SetTutorialSeenMask(int32 Mask)
{
	if (GConfig)
	{
		GConfig->SetInt(SaveSection, TEXT("TutorialSeen"), Mask, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

int32 ShiftSave::EdgeMode()
{
	int32 Value = 0;
	if (GConfig)
	{
		GConfig->GetInt(SaveSection, TEXT("EdgeMode"), Value, GGameUserSettingsIni);
	}
	return FMath::Clamp(Value, 0, 2);
}

void ShiftSave::SetEdgeMode(int32 Mode)
{
	if (GConfig)
	{
		GConfig->SetInt(SaveSection, TEXT("EdgeMode"), Mode, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

int32 ShiftSave::LeaderboardMode()
{
	int32 Value = 0;
	if (GConfig)
	{
		GConfig->GetInt(SaveSection, TEXT("LeaderboardMode"), Value, GGameUserSettingsIni);
	}
	return FMath::Clamp(Value, 0, 2);
}

void ShiftSave::SetLeaderboardMode(int32 Mode)
{
	if (GConfig)
	{
		GConfig->SetInt(SaveSection, TEXT("LeaderboardMode"), Mode, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}
