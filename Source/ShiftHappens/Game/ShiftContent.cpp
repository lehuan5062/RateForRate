// Shift Happens - Game Off 2026

#include "Game/ShiftContent.h"

#include "Containers/AllowShrinking.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/SparseSet.h"
#include "Containers/UnrealString.h"
#include "Data/ShiftRows.h"
#include "Engine/DataTable.h"
#include "HAL/Platform.h"
#include <initializer_list>
#include "Logging/LogMacros.h"
#include "Math/RandomStream.h"
#include "ShiftHappens.h"
#include "Sim/ShiftGraph.h"
#include "Sim/ShiftSim.h"
#include "Templates/TupleFwd.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/NameTypes.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	struct FContentCache
	{
		bool bTried = false;
		bool bHaveCore = false;
		TMap<FString, TArray<FString>> HandlesByKind;
		TMap<FString, TArray<FString>> BiosByKind;
		TMap<FString, TArray<FString>> CommentsByContext;
		TMap<FString, TArray<FString>> HeadlinesByEvent;
		TMap<FString, TArray<FString>> BlurbsByKey;       // key = Outcome + "/" + Condition
		TMap<FString, FString> TutorialByTrigger;
		TMap<FString, TArray<FShiftEntryRow>> EntriesByJam; // key = jam key (Other and Rival rows)
		TArray<FShiftEntryRow> YouRows;
	};

	FContentCache& Cache()
	{
		static FContentCache C;
		return C;
	}

	template <typename RowType>
	UDataTable* LoadTable(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("/Game/Shift/Data/%s.%s"), Name, Name);
		UDataTable* Table = LoadObject<UDataTable>(nullptr, *Path);
		if (!Table)
		{
			UE_LOG(LogShift, Warning, TEXT("DataTable %s not found (run Tools/py/build_data.py); using fallbacks."), *Path);
		}
		else if (Table->GetRowStruct() != RowType::StaticStruct())
		{
			UE_LOG(LogShift, Warning, TEXT("DataTable %s has row struct %s, expected %s."), *Path,
				Table->GetRowStruct() ? *Table->GetRowStruct()->GetName() : TEXT("none"), *RowType::StaticStruct()->GetName());
			return nullptr;
		}
		return Table;
	}

	uint32 HashSeed(uint32 Seed, int32 A, int32 B = 0)
	{
		uint32 H = Seed * 2654435761u ^ static_cast<uint32>(A) * 40503u ^ static_cast<uint32>(B) * 9973u;
		H ^= H >> 15;
		H *= 0x2c1b3c6du;
		H ^= H >> 12;
		return H;
	}

	FString PickFrom(const TArray<FString>* Pool, FRandomStream& Rng)
	{
		return (Pool && Pool->Num() > 0) ? (*Pool)[Rng.RandRange(0, Pool->Num() - 1)] : FString();
	}
}

bool FShiftContent::EnsureLoaded()
{
	FContentCache& C = Cache();
	if (C.bTried)
	{
		return C.bHaveCore;
	}
	C.bTried = true;

	if (UDataTable* T = LoadTable<FShiftHandleRow>(TEXT("DT_Handles")))
	{
		T->ForeachRow<FShiftHandleRow>(TEXT("Handles"), [&C](const FName&, const FShiftHandleRow& Row)
			{
				C.HandlesByKind.FindOrAdd(Row.Kind).Add(Row.Handle);
			});
	}
	if (UDataTable* T = LoadTable<FShiftBioRow>(TEXT("DT_Bios")))
	{
		T->ForeachRow<FShiftBioRow>(TEXT("Bios"), [&C](const FName&, const FShiftBioRow& Row)
			{
				C.BiosByKind.FindOrAdd(Row.Kind).Add(Row.Text);
			});
	}
	if (UDataTable* T = LoadTable<FShiftCommentRow>(TEXT("DT_Comments")))
	{
		T->ForeachRow<FShiftCommentRow>(TEXT("Comments"), [&C](const FName&, const FShiftCommentRow& Row)
			{
				C.CommentsByContext.FindOrAdd(Row.Context).Add(Row.Text);
			});
	}
	if (UDataTable* T = LoadTable<FShiftHeadlineRow>(TEXT("DT_Headlines")))
	{
		T->ForeachRow<FShiftHeadlineRow>(TEXT("Headlines"), [&C](const FName&, const FShiftHeadlineRow& Row)
			{
				C.HeadlinesByEvent.FindOrAdd(Row.Event).Add(Row.Text);
			});
	}
	if (UDataTable* T = LoadTable<FShiftBlurbRow>(TEXT("DT_Blurbs")))
	{
		T->ForeachRow<FShiftBlurbRow>(TEXT("Blurbs"), [&C](const FName&, const FShiftBlurbRow& Row)
			{
				C.BlurbsByKey.FindOrAdd(Row.Outcome + TEXT("/") + Row.Condition).Add(Row.Text);
			});
	}
	if (UDataTable* T = LoadTable<FShiftTutorialRow>(TEXT("DT_Tutorial")))
	{
		T->ForeachRow<FShiftTutorialRow>(TEXT("Tutorial"), [&C](const FName&, const FShiftTutorialRow& Row)
			{
				if (!C.TutorialByTrigger.Contains(Row.Trigger))
				{
					C.TutorialByTrigger.Add(Row.Trigger, Row.Text);
				}
			});
	}
	if (UDataTable* T = LoadTable<FShiftEntryRow>(TEXT("DT_Entries")))
	{
		T->ForeachRow<FShiftEntryRow>(TEXT("Entries"), [&C](const FName&, const FShiftEntryRow& Row)
			{
				if (Row.Role == TEXT("You"))
				{
					C.YouRows.Add(Row);
				}
				else
				{
					C.EntriesByJam.FindOrAdd(Row.Jam).Add(Row);
				}
			});
	}

	C.bHaveCore = C.HandlesByKind.Num() > 0 && C.EntriesByJam.Num() > 0;
	UE_LOG(LogShift, Log, TEXT("Content: handles=%d kinds, bios=%d kinds, comments=%d contexts, headlines=%d events, blurbs=%d keys, entries=%d jams, you=%d"),
		C.HandlesByKind.Num(), C.BiosByKind.Num(), C.CommentsByContext.Num(), C.HeadlinesByEvent.Num(), C.BlurbsByKey.Num(),
		C.EntriesByJam.Num(), C.YouRows.Num());
	return C.bHaveCore;
}

void FShiftContent::OverrideEntries(const FString& JamKey, const UDataTable* Table)
{
	EnsureLoaded();
	if (!Table || Table->GetRowStruct() != FShiftEntryRow::StaticStruct())
	{
		return;
	}
	TArray<FShiftEntryRow> Rows;
	Table->ForeachRow<FShiftEntryRow>(TEXT("PackEntries"), [&Rows](const FName&, const FShiftEntryRow& Row)
		{
			if (Row.Role != TEXT("You"))
			{
				Rows.Add(Row);
			}
		});
	if (Rows.Num() > 0)
	{
		UE_LOG(LogShift, Log, TEXT("Content: jam pack supplies %d entries for %s"), Rows.Num(), *JamKey);
		Cache().EntriesByJam.FindOrAdd(JamKey) = MoveTemp(Rows);
	}
}

const TCHAR* FShiftContent::JamKey(int32 JamIndex)
{
	switch (JamIndex)
	{
	case 0: return TEXT("TinyJam7");
	case 1: return TEXT("Rumble48");
	default: return TEXT("GameOff2026");
	}
}

namespace
{
	/** The row describing an entry, or nullptr for the fallback. */
	const FShiftEntryRow* FindEntryRow(int32 JamIndex, int32 EntryId, uint32 Seed)
	{
		FShiftContent::EnsureLoaded();
		const FContentCache& C = Cache();
		if (EntryId == 0)
		{
			return C.YouRows.Num() > 0 ? &C.YouRows[static_cast<int32>(Seed % static_cast<uint32>(C.YouRows.Num()))] : nullptr;
		}
		const TArray<FShiftEntryRow>* Rows = C.EntriesByJam.Find(FShiftContent::JamKey(JamIndex));
		if (!Rows)
		{
			return nullptr;
		}
		if (EntryId == 1)
		{
			for (const FShiftEntryRow& Row : *Rows)
			{
				if (Row.Role == TEXT("Rival"))
				{
					return &Row;
				}
			}
			return nullptr;
		}
		TArray<const FShiftEntryRow*> Others;
		for (const FShiftEntryRow& Row : *Rows)
		{
			if (Row.Role == TEXT("Other"))
			{
				Others.Add(&Row);
			}
		}
		if (Others.Num() == 0)
		{
			return nullptr;
		}
		const int32 Index = ((EntryId - 2) + static_cast<int32>(Seed % static_cast<uint32>(Others.Num()))) % Others.Num();
		return Others[Index];
	}
}

FString FShiftContent::EntryName(int32 JamIndex, int32 EntryId, uint32 Seed)
{
	if (const FShiftEntryRow* Row = FindEntryRow(JamIndex, EntryId, Seed))
	{
		return Row->Name;
	}
	return EntryId == 0 ? TEXT("Your Game") : (EntryId == 1 ? TEXT("The Famous Game") : FString::Printf(TEXT("Entry %d"), EntryId));
}

FString FShiftContent::EntryAuthor(int32 JamIndex, int32 EntryId, uint32 Seed)
{
	if (const FShiftEntryRow* Row = FindEntryRow(JamIndex, EntryId, Seed))
	{
		return Row->Author;
	}
	return EntryId == 0 ? TEXT("you") : TEXT("someone");
}

FString FShiftContent::EntryBlurb(int32 JamIndex, int32 EntryId, uint32 Seed)
{
	if (const FShiftEntryRow* Row = FindEntryRow(JamIndex, EntryId, Seed))
	{
		return Row->Blurb;
	}
	return FString();
}

void FShiftContent::BuildIdentities(const ShiftSim::Sim& Sim, uint32 Seed, TArray<FString>& OutHandles, TArray<FString>& OutBios)
{
	EnsureLoaded();
	const FContentCache& C = Cache();
	const ShiftSim::Graph& Graph = Sim.GetGraph();
	const int32 N = Graph.NodeCount();
	OutHandles.SetNum(N);
	OutBios.SetNum(N);

	// Shuffled pools per kind, drawn without replacement so handles are unique within a run.
	FRandomStream Rng(static_cast<int32>(HashSeed(Seed, 17)));
	TMap<FString, TArray<FString>> HandlePools = C.HandlesByKind;
	for (TPair<FString, TArray<FString>>& Pair : HandlePools)
	{
		for (int32 i = Pair.Value.Num() - 1; i > 0; --i)
		{
			Pair.Value.Swap(i, Rng.RandRange(0, i));
		}
	}
	TSet<FString> Used;
	auto TakeHandle = [&](std::initializer_list<const TCHAR*> Kinds) -> FString
		{
			for (const TCHAR* Kind : Kinds)
			{
				TArray<FString>* Pool = HandlePools.Find(Kind);
				while (Pool && Pool->Num() > 0)
				{
					const FString H = Pool->Pop(EAllowShrinking::No);
					if (!Used.Contains(H))
					{
						Used.Add(H);
						return H;
					}
				}
			}
			return FString();
		};

	for (int32 i = 0; i < N; ++i)
	{
		const ShiftSim::NodeRole Role = Graph.nodes[i].role;
		FString Handle;
		const TCHAR* BioKind = TEXT("Lurker");
		switch (Role)
		{
		case ShiftSim::NodeRole::Celebrity:
			Handle = TakeHandle({ TEXT("Celebrity"), TEXT("Streamer") });
			BioKind = TEXT("Celebrity");
			break;
		case ShiftSim::NodeRole::LocalHub:
			Handle = TakeHandle({ (i % 2 == 0) ? TEXT("Streamer") : TEXT("Dev"), TEXT("Dev"), TEXT("Streamer") });
			BioKind = (i % 2 == 0) ? TEXT("Streamer") : TEXT("Dev");
			break;
		case ShiftSim::NodeRole::Bridge:
			Handle = TakeHandle({ TEXT("Mod"), TEXT("Dev") });
			BioKind = TEXT("Mod");
			break;
		default:
			Handle = TakeHandle({ (HashSeed(Seed, i) % 3 == 0) ? TEXT("Dev") : TEXT("Lurker"), TEXT("Lurker"), TEXT("Dev"), TEXT("Bot") });
			BioKind = (HashSeed(Seed, i) % 3 == 0) ? TEXT("Dev") : TEXT("Lurker");
			break;
		}
		OutHandles[i] = Handle.IsEmpty() ? FString::Printf(TEXT("voter_%03d"), i) : Handle;

		FRandomStream BioRng(static_cast<int32>(HashSeed(Seed, i, 31)));
		const TArray<FString>* BioPool = C.BiosByKind.Find(BioKind);
		if (!BioPool || BioPool->Num() == 0)
		{
			BioPool = C.BiosByKind.Find(TEXT("Lurker"));
		}
		OutBios[i] = PickFrom(BioPool, BioRng);
	}
}

FString FShiftContent::PickComment(const FString& Context, FRandomStream& Rng)
{
	EnsureLoaded();
	return PickFrom(Cache().CommentsByContext.Find(Context), Rng);
}

FString FShiftContent::PickHeadline(const FString& Event, FRandomStream& Rng)
{
	EnsureLoaded();
	return PickFrom(Cache().HeadlinesByEvent.Find(Event), Rng);
}

FString FShiftContent::PickBlurb(const FString& Outcome, const FString& Condition, FRandomStream& Rng)
{
	EnsureLoaded();
	return PickFrom(Cache().BlurbsByKey.Find(Outcome + TEXT("/") + Condition), Rng);
}

FString FShiftContent::PickTutorial(const FString& Trigger)
{
	EnsureLoaded();
	const FString* Text = Cache().TutorialByTrigger.Find(Trigger);
	return Text ? *Text : FString();
}
