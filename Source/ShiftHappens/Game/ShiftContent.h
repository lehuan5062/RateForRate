// Rate for Rate - Game Off 2026

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Engine/DataTable.h"
#include "HAL/Platform.h"
#include "Math/RandomStream.h"
#include "Sim/ShiftSim.h"

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
	/** The jam the chatter is for (set when a run starts): a pool also holds the lines tagged "<Context>@<JamKey>" for it. Jams without tagged lines draw exactly as before. */
	static void SetActiveJam(int32 JamIndex);
	static FString PickComment(const FString& Context, FRandomStream& Rng);
	/** Like PickComment (one draw from Rng), and the line's Sentiment: "Positive", "Negative", "Neutral" or "Suspicious" (empty when there is no line). */
	static FString PickCommentLine(const FString& Context, FRandomStream& Rng, FString& OutSentiment);
	/** A random handle of a Kind ("Bot", "Mod", "Lurker", ...); empty if there is none. Not unique within a run: for chatter speakers, not for voters. */
	static FString PickHandleOfKind(const FString& Kind, FRandomStream& Rng);
	static FString PickHeadline(const FString& Event, FRandomStream& Rng);
	static FString PickBlurb(const FString& Outcome, const FString& Condition, FRandomStream& Rng);
	/** Any line of an Outcome ("Win", "Lose", "DQ"), whatever its condition: the fallback when no condition has a line. */
	static FString PickBlurbAny(const FString& Outcome, FRandomStream& Rng);
	static FString PickTutorial(const FString& Trigger);

	/** The guided tutorial: text of one step of a chapter (0 = the "How to play" reference page, 1 = the basics, 2 = night shift), and how many steps a chapter has. */
	static FString LessonText(int32 Chapter, int32 Step);
	static int32 LessonStepCount(int32 Chapter);
	/** An achievement by its row Id (Data/Achievements.csv); false if there is none. */
	static bool Achievement(const FString& Id, FString& OutName, FString& OutDescription, bool& bOutHidden);
	static int32 AchievementCount();
};
