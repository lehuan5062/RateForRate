// Shift Happens - Game Off 2026

#pragma once

#include "Containers/Array.h"
#include "HAL/Platform.h"
#include "Sim/ShiftBuildRules.h"
#include "Sim/ShiftParams.h"

/** The 12 hours before the deadline, split across the three things a jam entry can invest in. */
struct FShiftBuild
{
	int32 Quality = 4;        // make the game itself better
	int32 Thumbnail = 4;      // thumbnail and title: how many people even consider it
	int32 Network = 4;        // Discord, rate-for-rate rings, followers: start nodes and Posts regeneration
	static constexpr int32 TotalHours = 12;
	int32 Spent() const { return Quality + Thumbnail + Network; }
};

/** Predicted standings for a casual player (a Plug every couple of seconds) with this build, against the stand-in rival and other entries. */
struct FShiftForecast
{
	int32 PlayerRank = 0;                 // 1-based by forecast share
	float PlayerShare = 0.0f;
	int32 QualityRank = 0;                // 1-based by quality
	TArray<float> Quality;                // per entry id
	TArray<int32> QualityOrder;           // entry ids, best quality first
	TArray<float> Share;                  // per entry id, average over the samples
	TArray<int32> ShareOrder;             // entry ids, best forecast first
};

namespace ShiftBuildRules
{
	/** Applies the build to the sim parameters (neutral at 4/4/4): see Sim/ShiftBuildRules.h for what each lever does. */
	void Apply(const FShiftBuild& Build, ShiftSim::Params& Params, ShiftSim::JamDef& Jam);

	/** What the build comes to for a jam, in the terms the player feels (Post bank and refill, start voters, Plug cost, voter loyalty, appeal, clout). */
	ShiftSim::BuildSummary Summary(int32 JamIndex, const FShiftBuild& Build);

	/** Casual-player forecast: a few full sims with the stand-in rival and others (milliseconds each). */
	FShiftForecast Forecast(int32 JamIndex, uint32 Seed, const FShiftBuild& Build, int32 Samples = 4);
}

/** Progress and settings, stored in the user settings ini. */
namespace ShiftSave
{
	int32 BestRank(int32 JamIndex);                 // 0 = never finished
	void RecordResult(int32 JamIndex, int32 Rank);  // keeps the best (lowest) rank
	bool IsMuted();
	void SetMuted(bool bMuted);
	int32 TutorialSeenMask();                       // bit per tutorial card already shown
	void SetTutorialSeenMask(int32 Mask);
	int32 EdgeMode();                               // relation lines: 0 faint, 1 off, 2 full
	void SetEdgeMode(int32 Mode);
	int32 LeaderboardMode();                        // 0 compact, 1 full, 2 hidden
	void SetLeaderboardMode(int32 Mode);
}
