// Shift Happens - Game Off 2026

#include "Game/ShiftBuild.h"

#include "Containers/UnrealString.h"
#include "CoreGlobals.h"
#include "HAL/Platform.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/ConfigCacheIni.h"
#include "Sim/ShiftBuildRules.h"
#include "Sim/ShiftParams.h"

namespace
{
	const TCHAR* const SaveSection = TEXT("ShiftHappens");
}

void ShiftBuildRules::Apply(const FShiftBuild& Build, ShiftSim::Params& Params, ShiftSim::JamDef& Jam)
{
	ShiftSim::BuildHours Hours;
	Hours.quality = Build.Quality;
	Hours.thumbnail = Build.Thumbnail;
	Hours.network = Build.Network;
	ShiftSim::ApplyBuild(Hours, Params, Jam);
}

ShiftSim::BuildSummary ShiftBuildRules::Summary(int32 JamIndex, const FShiftBuild& Build)
{
	ShiftSim::Params Params;
	ShiftSim::JamDef Jam = ShiftSim::DefaultJam(FMath::Clamp(JamIndex, 0, 2));
	ShiftBuildRules::Apply(Build, Params, Jam);
	return ShiftSim::SummarizeBuild(Params, Jam);
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
