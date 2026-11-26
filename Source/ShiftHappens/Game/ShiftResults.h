// Rate for Rate - Game Off 2026

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Game/ShiftRun.h"
#include "HAL/Platform.h"
#include "Math/RandomStream.h"
#include "Sim/ShiftSim.h"

/** What the results blurb is chosen from, taken from a finished run (the game and the balance commandlet build it the same way). */
struct FShiftResultFacts
{
	bool bDisqualified = false;
	bool bWon = false;
	int32 Rank = 1;                 // the player's final rank by votes, 1 = first
	int32 QualityRank = 1;          // by quality, 1 = the best game
	int32 NumEntries = 9;
	double RunFraction = 1.0;       // how far into the jam the run ended (a disqualification ends it early)
	double Margin = 0.0;            // vote share between the winner and the runner-up (a win), or between the leader and the player (a loss)
	bool bCheated = false;          // bought at least one sockpuppet vote
	bool bRivalCollapsed = false;   // the rival started in the top two and ended 4th or lower, or the player shifted the blame onto it
};

/** The blurb conditions that fit a run: Outcome ("Win", "Lose", "DQ"), the general condition and every more specific one that also applies. */
struct FShiftResultConditions
{
	FString Outcome;
	FString General;
	TArray<FString> Specific;
};

namespace ShiftResults
{
	/** Rank by quality, 1 = the best game in this world. */
	int32 QualityRankOf(const ShiftSim::Sim& S);

	FShiftResultFacts MakeFacts(const ShiftSim::Sim& S, const FShiftFlipStats& Flips, bool bBlameTaken, const TArray<float>& InitialShares);

	FShiftResultConditions Classify(const FShiftResultFacts& Facts);

	/** The blurb of a run: a seeded pick among its specific conditions and the general one, falling back to the general condition and then to any line of the outcome.
	 *  OutCondition names the condition the line came from. */
	FString PickBlurb(const FShiftResultConditions& Conditions, FRandomStream& Rng, FString* OutCondition = nullptr);
}
