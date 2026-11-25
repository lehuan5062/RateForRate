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
	ShiftSim::JamDef Jam;
	ShiftSim::SetupJam(FMath::Clamp(JamIndex, 0, ShiftSim::kJamCount - 1), Params, Jam);
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

namespace
{
	/** A sound switch: its own key, else the old single Muted key (muted = off), else on. */
	bool ReadSoundSwitch(const TCHAR* Key)
	{
		bool bValue = true;
		if (GConfig)
		{
			if (!GConfig->GetBool(SaveSection, Key, bValue, GGameUserSettingsIni))
			{
				bool bMuted = false;
				if (GConfig->GetBool(SaveSection, TEXT("Muted"), bMuted, GGameUserSettingsIni))
				{
					bValue = !bMuted;
				}
				else
				{
					bValue = true;
				}
			}
		}
		return bValue;
	}

	void WriteSoundSwitch(const TCHAR* Key, bool bOn)
	{
		if (GConfig)
		{
			GConfig->SetBool(SaveSection, Key, bOn, GGameUserSettingsIni);
			GConfig->Flush(false, GGameUserSettingsIni);
		}
	}
}

bool ShiftSave::IsEffectsOn()
{
	return ReadSoundSwitch(TEXT("EffectsOn"));
}

void ShiftSave::SetEffectsOn(bool bOn)
{
	WriteSoundSwitch(TEXT("EffectsOn"), bOn);
}

bool ShiftSave::IsMusicOn()
{
	return ReadSoundSwitch(TEXT("MusicOn"));
}

void ShiftSave::SetMusicOn(bool bOn)
{
	WriteSoundSwitch(TEXT("MusicOn"), bOn);
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

int32 ShiftSave::LessonsDone()
{
	int32 Value = 0;
	if (GConfig)
	{
		GConfig->GetInt(SaveSection, TEXT("LessonsDone"), Value, GGameUserSettingsIni);
	}
	return Value;
}

void ShiftSave::SetLessonsDone(int32 Mask)
{
	if (GConfig)
	{
		GConfig->SetInt(SaveSection, TEXT("LessonsDone"), Mask, GGameUserSettingsIni);
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

uint32 ShiftSave::Achievements()
{
	int32 Value = 0;
	if (GConfig)
	{
		GConfig->GetInt(SaveSection, TEXT("Achievements"), Value, GGameUserSettingsIni);
	}
	return static_cast<uint32>(Value);
}

void ShiftSave::SetAchievements(uint32 Mask)
{
	if (GConfig)
	{
		GConfig->SetInt(SaveSection, TEXT("Achievements"), static_cast<int32>(Mask), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

static int32 ReadCareerInt(const TCHAR* Key, int32 JamIndex)
{
	int32 Value = 0;
	if (GConfig)
	{
		GConfig->GetInt(SaveSection, *FString::Printf(TEXT("%s%d"), Key, JamIndex), Value, GGameUserSettingsIni);
	}
	return Value;
}

void ShiftSave::RecordCareer(int32 JamIndex, bool bWon, bool bDisqualified, float Share)
{
	if (!GConfig)
	{
		return;
	}
	GConfig->SetInt(SaveSection, *FString::Printf(TEXT("CareerRuns%d"), JamIndex), ReadCareerInt(TEXT("CareerRuns"), JamIndex) + 1, GGameUserSettingsIni);
	if (bWon)
	{
		GConfig->SetInt(SaveSection, *FString::Printf(TEXT("CareerWins%d"), JamIndex), ReadCareerInt(TEXT("CareerWins"), JamIndex) + 1, GGameUserSettingsIni);
	}
	if (bDisqualified)
	{
		GConfig->SetInt(SaveSection, *FString::Printf(TEXT("CareerDq%d"), JamIndex), ReadCareerInt(TEXT("CareerDq"), JamIndex) + 1, GGameUserSettingsIni);
	}
	else
	{
		const int32 Permille = FMath::Clamp(FMath::RoundToInt(Share * 1000.0f), 0, 1000);
		if (Permille > ReadCareerInt(TEXT("CareerBest"), JamIndex))
		{
			GConfig->SetInt(SaveSection, *FString::Printf(TEXT("CareerBest%d"), JamIndex), Permille, GGameUserSettingsIni);
		}
	}
	GConfig->Flush(false, GGameUserSettingsIni);
}

int32 ShiftSave::CareerRuns(int32 JamIndex) { return ReadCareerInt(TEXT("CareerRuns"), JamIndex); }
int32 ShiftSave::CareerWins(int32 JamIndex) { return ReadCareerInt(TEXT("CareerWins"), JamIndex); }
int32 ShiftSave::CareerDq(int32 JamIndex) { return ReadCareerInt(TEXT("CareerDq"), JamIndex); }
float ShiftSave::CareerBestShare(int32 JamIndex) { return static_cast<float>(ReadCareerInt(TEXT("CareerBest"), JamIndex)) / 1000.0f; }

bool ShiftSave::GetFlag(const TCHAR* Key)
{
	bool bValue = false;
	if (GConfig)
	{
		GConfig->GetBool(SaveSection, Key, bValue, GGameUserSettingsIni);
	}
	return bValue;
}

void ShiftSave::SetFlag(const TCHAR* Key, bool bOn)
{
	if (GConfig)
	{
		GConfig->SetBool(SaveSection, Key, bOn, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

bool ShiftSave::GlassWindow()
{
	bool bValue = true;   // on by default (the same default as ShiftGlass::IsRequested)
	if (GConfig)
	{
		GConfig->GetBool(SaveSection, TEXT("GlassWindow"), bValue, GGameUserSettingsIni);
	}
	return bValue;
}

void ShiftSave::SetGlassWindow(bool bOn)
{
	SetFlag(TEXT("GlassWindow"), bOn);
	if (!GConfig)
	{
		return;
	}
	// The see-through window needs an alpha channel all the way to the screen. These are read before the renderer starts, so they go into the player's own saved
	// Engine.ini (Saved/Config), and come out again when the window is switched off.
	static const TCHAR* const Keys[] = { TEXT("r.PostProcessing.PropagateAlpha"), TEXT("r.TransparentViewportClear"), TEXT("r.DefaultBackBufferPixelFormat"), TEXT("r.D3D12.UseAllowTearing") };
	static const TCHAR* const Values[] = { TEXT("1"), TEXT("1"), TEXT("0"), TEXT("0") };
	for (int32 i = 0; i < 4; ++i)
	{
		if (bOn)
		{
			GConfig->SetString(TEXT("SystemSettings"), Keys[i], Values[i], GEngineIni);
		}
		else
		{
			GConfig->RemoveKey(TEXT("SystemSettings"), Keys[i], GEngineIni);
		}
	}
	GConfig->Flush(false, GEngineIni);
}

int32 ShiftSave::GlassOpacity()
{
	int32 Value = 85;
	if (GConfig)
	{
		GConfig->GetInt(SaveSection, TEXT("GlassPaper"), Value, GGameUserSettingsIni);   // (the first version saved the real opacity under GlassOpacity: not read any more)
	}
	return FMath::Clamp(Value, 0, 100);
}

float ShiftSave::GlassPaperAlpha(int32 Percent)
{
	return static_cast<float>(FMath::Clamp(Percent, 0, 100)) / 100.0f;
}

void ShiftSave::SetGlassOpacity(int32 Percent)
{
	if (GConfig)
	{
		GConfig->SetInt(SaveSection, TEXT("GlassPaper"), FMath::Clamp(Percent, 0, 100), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}
