// Shift Happens - Game Off 2026

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFeatureAction.h"
#include "UObject/StrongObjectPtr.h"
#include "ShiftJamPack.generated.h"

class UDataTable;

/** What a jam pack plugin ships: which built-in jam it plays (sim parameters live in Data/Jams.csv), its card text, and its own entry table. */
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

/** Editor-side authoring of the jam pack assets (callable from Python: Tools/py/build_jam_packs.py). */
UCLASS()
class UShiftJamPackBuilder : public UObject
{
	GENERATED_BODY()

public:
	/** Mounts the Game Feature plugin's content so its assets can be created in this editor session. */
	UFUNCTION(BlueprintCallable, Category = "Shift")
	static bool EnsureMounted(const FString& PluginName);

	/** Creates DA_Jam and the GameFeatureData (with the Shift Jam Pack action) inside the plugin and saves them. */
	UFUNCTION(BlueprintCallable, Category = "Shift")
	static bool BuildPack(const FString& PluginName, int32 JamIndex, const FString& Key, const FString& Title, const FString& Subtitle, const FString& RivalFollowers);
};
