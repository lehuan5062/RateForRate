// Rate for Rate - Game Off 2026

#pragma once

#include "Game/ShiftResults.h"
#include "Game/ShiftRun.h"
#include "HAL/Platform.h"
#include "Sim/ShiftSim.h"

/** The achievements, in the order of Data/Achievements.csv. Append only: the save stores one bit per entry. */
enum class EShiftAch : uint8
{
	HonestRun,
	NightOwl,
	Snowball,
	BannedSpeedrun,
	BestGameWorstRank,
	ShiftedTheBlame,
	GateCrasher,
	AudienceCapture,
	GiantSlayer,
	StreamersPet,
	EyeContact,
	StickySituation,
	Schadenfreude,
	NobodyPlayedIt,
	FairAndSquare,
	// appended in 0.3 (the save stores one bit per entry: append only)
	NotOnMyWatch,
	WallOfSupport,
	OffTheClock,
	TwoStrikesStanding,
	GrandSlam,
	TopOfTheClass,
	Count
};

namespace ShiftAch
{
	constexpr int32 Num = static_cast<int32>(EShiftAch::Count);

	/** The row Id of an achievement in Data/Achievements.csv. */
	const TCHAR* IdOf(int32 Index);
}

/** Watches one run of a human player and records which achievements it earned. It only reads the sim and the run's events; it never touches either. */
class FShiftAchTracker
{
public:
	void Reset();
	void OnEvent(const FShiftEvent& E, const ShiftSim::Sim& S);
	/** Once per frame while the run is live; Seconds is real time, bNightHeld whether SHIFT (or the N latch) is down. */
	void Tick(const ShiftSim::Sim& S, const FShiftFlipStats& Flips, float Seconds, bool bNightHeld);
	void OnNightLatch();
	/** The run is over: the achievements that depend on how it ended. */
	void OnFinished(const FShiftResultFacts& Facts, const FShiftFlipStats& Flips, int32 JamIndex);
	/** The achievements that depend on the career, asked once a run has ended: this win completes the set of jams won, and the guided lessons are finished. */
	void OnCareer(bool bWonEveryJam, bool bLessonsFinished);

	uint32 Mask() const { return Earned; }
	float NightSeconds() const { return NightHeld; }
	int32 ParryCount() const { return Parries; }
	int32 BiggestRally() const { return RallyBest; }
	int32 StrikesMax() const { return MaxStrikes; }

private:
	void Earn(EShiftAch A) { Earned |= (1u << static_cast<uint32>(A)); }

	uint32 Earned = 0;
	float NightHeld = 0.0f;
	bool bSnowActive = false;
	double SnowStart = 0.0;
	int32 SnowGains0 = 0;
	int32 SnowClicks = 0;
	int32 Parries = 0;          // announced attacks the player answered this run
	int32 MaxStrikes = 0;       // the most strikes the player had at once this run
	int32 RallyBest = 0;        // the most voters one Rally boosted this run
};
