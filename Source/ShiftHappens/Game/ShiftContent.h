// Shift Happens - Game Off 2026

#pragma once

#include "CoreMinimal.h"

namespace ShiftSim { class Sim; }
class UDataTable;

/**
 * Text content from the imported DataTables (/Game/Shift/Data/DT_*). Every accessor has a built-in fallback, so the game still
 * runs (with plain names) before Tools/py/build_data.py has imported the CSVs.
 */
class FShiftContent
{
public:
	/** Loads the tables once. Returns true if at least the handles and entries tables were found. */
	static bool EnsureLoaded();

	/** A jam pack's own entry table (the rival and the other entries) replaces the built-in rows for that jam key. */
	static void OverrideEntries(const FString& JamKey, const UDataTable* Table);

	/** "TinyJam7", "Rumble48", "GameOff2026" for jam index 0..2. */
	static const TCHAR* JamKey(int32 JamIndex);

	/** Entry 0 = the player's game, 1 = the rival's, 2.. = the other entries (order rotated by the seed). */
	static FString EntryName(int32 JamIndex, int32 EntryId, uint32 Seed);
	static FString EntryAuthor(int32 JamIndex, int32 EntryId, uint32 Seed);
	static FString EntryBlurb(int32 JamIndex, int32 EntryId, uint32 Seed);

	/** Unique handle and bio per node, by role (members are lurkers/devs, hubs are streamers/devs, celebrities are celebrities). */
	static void BuildIdentities(const ShiftSim::Sim& Sim, uint32 Seed, TArray<FString>& OutHandles, TArray<FString>& OutBios);

	/** A random line for a comment context (FlipToYou, Dent, Strike, ...); empty if the table is missing. */
	static FString PickComment(const FString& Context, FRandomStream& Rng);
	static FString PickHeadline(const FString& Event, FRandomStream& Rng);
	static FString PickBlurb(const FString& Outcome, const FString& Condition, FRandomStream& Rng);
	static FString PickTutorial(const FString& Trigger);
};
