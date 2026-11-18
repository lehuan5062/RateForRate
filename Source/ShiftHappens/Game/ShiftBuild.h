// Shift Happens - Game Off 2026

#pragma once

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

namespace ShiftBuildRules
{
	/** Applies the build to the sim parameters (neutral at 4/4/4): see Sim/ShiftBuildRules.h for what each lever does. */
	void Apply(const FShiftBuild& Build, ShiftSim::Params& Params, ShiftSim::JamDef& Jam);

	/** What the build comes to for a jam, in the terms the player feels (Post bank and refill, start voters, Plug cost, voter loyalty, appeal, clout). */
	ShiftSim::BuildSummary Summary(int32 JamIndex, const FShiftBuild& Build);
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
