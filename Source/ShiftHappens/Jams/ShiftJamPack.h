// Shift Happens - Game Off 2026

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "GameFeatureAction.h"
#include "HAL/Platform.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/StrongObjectPtrTemplates.h"
#include "ShiftJamPack.generated.h"

/** What a jam pack plugin ships: which built-in jam it plays (the sim parameters are DefaultJam in Sim/ShiftParams.cpp; Data/Jams.csv is only a harness export), its card text, and its own entry table. */
UCLASS(BlueprintType)
class SHIFTHAPPENS_API UShiftJamPackData : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Which built-in jam definition (0..2) this pack plays. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shift")
	int32 JamIndex = 0;

	/** Content key used by the text tables (TinyJam7, Rumble48, GameOff2026). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shift")
	FString Key;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shift")
	FString Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shift")
	FString Subtitle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shift")
	FString RivalFollowers;

	/** The rival and the other entries of this jam (row struct FShiftEntryRow). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shift")
	TSoftObjectPtr<UDataTable> Entries;
};

/** Game Feature action: registers a jam pack with the game when its plugin activates and removes it when the plugin deactivates. */
UCLASS(MinimalAPI, meta = (DisplayName = "Shift Jam Pack"))
class UGameFeatureAction_ShiftJam : public UGameFeatureAction
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Shift")
	TSoftObjectPtr<UShiftJamPackData> Pack;

	virtual void OnGameFeatureActivating(FGameFeatureActivatingContext& Context) override;
	virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;
};

/** The jam packs that are active right now. The jam select screen is built from this, so dropping in another pack plugin adds a jam. */
class SHIFTHAPPENS_API FShiftJamRegistry
{
public:
	static FShiftJamRegistry& Get();

	void Register(UShiftJamPackData* Pack);
	void Unregister(const UShiftJamPackData* Pack);

	/** Active packs sorted by JamIndex. */
	TArray<const UShiftJamPackData*> Active() const;
	/** Bumps whenever a pack registers or unregisters. */
	uint32 Version() const { return VersionCounter; }

private:
	TArray<TStrongObjectPtr<UShiftJamPackData>> Packs;
	uint32 VersionCounter = 0;
};
