// Rate for Rate - Game Off 2026

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
	bool IsEffectsOn();                             // sound effects (the settings menu's Effects row); an old Muted=True save reads as off
	void SetEffectsOn(bool bOn);
	bool IsMusicOn();                               // the music (the settings menu's Music row)
	void SetMusicOn(bool bOn);
	int32 TutorialSeenMask();                       // bit per tutorial card already shown
	void SetTutorialSeenMask(int32 Mask);
	int32 LessonsDone();                            // bit per guided lesson finished (bit 0 = the basics, bit 1 = night shift)
	void SetLessonsDone(int32 Mask);
	int32 EdgeMode();                               // relation lines: 0 faint, 1 off, 2 full
	void SetEdgeMode(int32 Mode);
	bool GetFlag(const TCHAR* Key);                // a saved on / off switch (accessibility settings)
	void SetFlag(const TCHAR* Key, bool bOn);
	bool GlassWindow();                             // the see-through window is switched on (default: on; takes effect on the next launch)
	void SetGlassWindow(bool bOn);                  // also writes (or removes) the renderer settings the window needs into the player's own Engine.ini
	int32 GlassOpacity();                           // the Paper opacity slider of the see-through window, percent (default 85; key GlassPaper)
	void SetGlassOpacity(int32 Percent);
	float GlassPaperAlpha(int32 Percent);           // what the slider means: the paper's real opacity 0..1, linear (95% shows 5% of the desktop, like an opacity slider in an image editor)
	uint32 Achievements();                          // bit per achievement earned (EShiftAch)
	void SetAchievements(uint32 Mask);
	void RecordCareer(int32 JamIndex, bool bWon, bool bDisqualified, float Share);   // one finished run of a human player
	int32 CareerRuns(int32 JamIndex);
	int32 CareerWins(int32 JamIndex);
	int32 CareerDq(int32 JamIndex);
	float CareerBestShare(int32 JamIndex);          // 0..1
	int32 LeaderboardMode();                        // 0 compact, 1 full, 2 hidden
	void SetLeaderboardMode(int32 Mode);
}
