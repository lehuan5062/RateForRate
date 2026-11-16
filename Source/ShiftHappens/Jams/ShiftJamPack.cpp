// Shift Happens - Game Off 2026

#include "Jams/ShiftJamPack.h"
#include "Game/ShiftContent.h"
#include "ShiftHappens.h"

#include "Engine/DataTable.h"

void UGameFeatureAction_ShiftJam::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
	Super::OnGameFeatureActivating(Context);
	if (UShiftJamPackData* Data = Pack.LoadSynchronous())
	{
		FShiftJamRegistry::Get().Register(Data);
		UE_LOG(LogShift, Log, TEXT("Jam pack activated: %s (jam %d, %s)"), *Data->GetName(), Data->JamIndex, *Data->Key);
	}
	else
	{
		UE_LOG(LogShift, Warning, TEXT("Jam pack action could not load its pack data (%s)"), *Pack.ToString());
	}
}

void UGameFeatureAction_ShiftJam::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	if (UShiftJamPackData* Data = Pack.Get())
	{
		FShiftJamRegistry::Get().Unregister(Data);
		UE_LOG(LogShift, Log, TEXT("Jam pack deactivated: %s"), *Data->GetName());
	}
}

FShiftJamRegistry& FShiftJamRegistry::Get()
{
	static FShiftJamRegistry Instance;
	return Instance;
}

void FShiftJamRegistry::Register(UShiftJamPackData* Pack)
{
	if (!Pack)
	{
		return;
	}
	for (const TStrongObjectPtr<UShiftJamPackData>& Existing : Packs)
	{
		if (Existing.Get() == Pack)
		{
			return;
		}
	}
	Packs.Add(TStrongObjectPtr<UShiftJamPackData>(Pack));
	++VersionCounter;
	if (UDataTable* Table = Pack->Entries.LoadSynchronous())
	{
		FShiftContent::OverrideEntries(Pack->Key, Table);   // the pack's own rows replace the built-in fallback rows for its jam
	}
}

void FShiftJamRegistry::Unregister(const UShiftJamPackData* Pack)
{
	const int32 Removed = Packs.RemoveAll([Pack](const TStrongObjectPtr<UShiftJamPackData>& P) { return P.Get() == Pack; });
	if (Removed > 0)
	{
		++VersionCounter;
	}
}

TArray<const UShiftJamPackData*> FShiftJamRegistry::Active() const
{
	TArray<const UShiftJamPackData*> Out;
	for (const TStrongObjectPtr<UShiftJamPackData>& P : Packs)
	{
		Out.Add(P.Get());
	}
	Out.Sort([](const UShiftJamPackData& A, const UShiftJamPackData& B) { return A.JamIndex < B.JamIndex; });
	return Out;
}
