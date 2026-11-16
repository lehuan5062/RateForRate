// Shift Happens - Game Off 2026

#include "Jams/ShiftJamPack.h"
#include "Game/ShiftContent.h"
#include "ShiftHappens.h"

#include "Engine/DataTable.h"
#include "GameFeatureData.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

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

// ---------------------------------------------------------------------------------------------------------------------------------

bool UShiftJamPackBuilder::EnsureMounted(const FString& PluginName)
{
#if WITH_EDITOR
	IPluginManager& Manager = IPluginManager::Get();
	TSharedPtr<IPlugin> Plugin = Manager.FindPlugin(PluginName);
	if (!Plugin.IsValid())
	{
		UE_LOG(LogShift, Error, TEXT("Plugin %s was not discovered (is Plugins/GameFeatures/%s/%s.uplugin there?)"), *PluginName, *PluginName, *PluginName);
		return false;
	}
	if (!Plugin->IsEnabled() || !FPackageName::MountPointExists(FString::Printf(TEXT("/%s/"), *PluginName)))
	{
		FText Reason;
		if (!Manager.MountExplicitlyLoadedPlugin(PluginName) && !FPackageName::MountPointExists(FString::Printf(TEXT("/%s/"), *PluginName)))
		{
			UE_LOG(LogShift, Error, TEXT("Could not mount plugin %s"), *PluginName);
			return false;
		}
	}
	UE_LOG(LogShift, Log, TEXT("Plugin %s mounted (content: %s)"), *PluginName, *Plugin->GetContentDir());
	return true;
#else
	return false;
#endif
}

bool UShiftJamPackBuilder::BuildPack(const FString& PluginName, int32 JamIndex, const FString& Key, const FString& Title, const FString& Subtitle, const FString& RivalFollowers)
{
#if WITH_EDITOR
	if (!EnsureMounted(PluginName))
	{
		return false;
	}

	auto Save = [](UPackage* Package, UObject* Asset) -> bool
	{
		Package->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Asset);
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const bool bSaved = UPackage::SavePackage(Package, Asset, *Filename, Args);
		UE_LOG(LogShift, Log, TEXT("saved=%d %s"), bSaved ? 1 : 0, *Filename);
		return bSaved;
	};

	// DA_Jam: the pack data
	const FString PackPackageName = FString::Printf(TEXT("/%s/DA_Jam"), *PluginName);
	UPackage* PackPackage = CreatePackage(*PackPackageName);
	UShiftJamPackData* Data = FindObject<UShiftJamPackData>(PackPackage, TEXT("DA_Jam"));
	if (!Data)
	{
		Data = NewObject<UShiftJamPackData>(PackPackage, FName(TEXT("DA_Jam")), RF_Public | RF_Standalone);
	}
	Data->JamIndex = JamIndex;
	Data->Key = Key;
	Data->Title = Title;
	Data->Subtitle = Subtitle;
	Data->RivalFollowers = RivalFollowers;
	Data->Entries = TSoftObjectPtr<UDataTable>(FSoftObjectPath(FString::Printf(TEXT("/%s/DT_Entries.DT_Entries"), *PluginName)));
	if (!Save(PackPackage, Data))
	{
		return false;
	}

	// GameFeatureData: named like the plugin, with the Shift Jam Pack action
	const FString FeaturePackageName = FString::Printf(TEXT("/%s/%s"), *PluginName, *PluginName);
	UPackage* FeaturePackage = CreatePackage(*FeaturePackageName);
	UGameFeatureData* Feature = FindObject<UGameFeatureData>(FeaturePackage, *PluginName);
	if (!Feature)
	{
		Feature = NewObject<UGameFeatureData>(FeaturePackage, FName(*PluginName), RF_Public | RF_Standalone);
	}
	Feature->GetMutableActionsInEditor().Reset();
	UGameFeatureAction_ShiftJam* Action = NewObject<UGameFeatureAction_ShiftJam>(Feature, FName(TEXT("Action_ShiftJam")), RF_Transactional);
	Action->Pack = TSoftObjectPtr<UShiftJamPackData>(FSoftObjectPath(FString::Printf(TEXT("/%s/DA_Jam.DA_Jam"), *PluginName)));
	Feature->GetMutableActionsInEditor().Add(Action);
	return Save(FeaturePackage, Feature);
#else
	return false;
#endif
}
