// Shift Happens - Game Off 2026

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "HAL/Platform.h"

/**
 * A recording of one jam as the player played it: the world (jam, seed, build) and every click and night-shift change with the sim step it was applied in. The sim is deterministic,
 * so these few kilobytes replay the whole run (FShiftRun::SetReplay), and the samples (every entry's share every 5 s of the jam) show how it went without replaying it.
 *
 * The file (Saved/Recordings/shift_jam<n>_<seed>_<time>_<result>.shiftrec.txt: a .txt so that GitHub and chat programs take it as an attachment) is plain text, one line per item, so it can be read, diffed and sent in a message:
 *
 *   SHIFTREC 1
 *   tag round13            the game's build tag (the rules version: bumped when a change makes an older recording replay differently)
 *   tuning 3f9a...         hash of every tuning value the run was played under (a replay under other tuning diverges, and says so)
 *   jam 2 / seed 123 / build 4 4 4
 *   E <step> P|S|R|B|N|n <node>     Plug, Sock, Rally, Shift the Blame, night shift on, night shift off
 *   S <step> <Posts> <Heat> <share of entry 0> <share of entry 1> ...
 *   result won|lost|disqualified|abandoned, endstep, endtime, rank, strikes, hash, shares ...
 */
namespace ShiftRecordingInfo
{
	inline const TCHAR* const BuildTag = TEXT("round13");
	constexpr int32 FormatVersion = 1;
	constexpr int32 SampleEverySteps = 100;   // 5 s of jam time at 20 Hz
}

struct FShiftRecEvent
{
	int32 Step = 0;                  // the sim step (Sim::StepCount() before the step ran) the event was applied in
	TCHAR Type = TEXT('P');          // P Plug, S Sock (a click in night shift), R Rally, B Shift the Blame, N night shift on, n night shift off
	int32 Node = -1;
};

struct FShiftRecSample
{
	int32 Step = 0;
	float Posts = 0.0f;
	float Heat = 0.0f;
	TArray<float> Shares;            // every entry's share of all vote weight (0..1)
};

struct FShiftRecording
{
	int32 Format = ShiftRecordingInfo::FormatVersion;
	FString Tag;
	FString Tuning;
	int32 Jam = 0;
	uint32 Seed = 0;
	int32 Quality = 4;
	int32 Thumbnail = 4;
	int32 Network = 4;
	TArray<FShiftRecEvent> Events;
	TArray<FShiftRecSample> Samples;

	FString Result;                  // won, lost, disqualified, abandoned
	int32 EndStep = 0;
	double EndTime = 0.0;
	int32 Rank = 0;                  // 1-based
	int32 Strikes = 0;
	FString Hash;                    // the sim's state hash at the end (a replay of the same build and tuning ends in the same hash)
	TArray<float> FinalShares;

	FString ToText() const;
	static bool FromText(const FString& Text, FShiftRecording& Out, FString& Error);
	bool Save(const FString& Path) const;
	static bool Load(const FString& Path, FShiftRecording& Out, FString& Error);
};
