// Shift Happens - Game Off 2026

#include "Behavior/StateTree/ShiftStateTreeBuilder.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Behavior/ShiftTags.h"
#include "Behavior/StateTree/ShiftStateTreeNodes.h"
#include "Behavior/StateTree/ShiftStateTreeSchema.h"
#include "Conditions/StateTreeGameplayTagConditions.h"
#include "Containers/UnrealString.h"
#include "GameplayTagContainer.h"
#include "HAL/Platform.h"
#include "Logging/LogMacros.h"
#include "Misc/PackageName.h"
#include "ShiftHappens.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeState.h"
#include "StateTreeTypes.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR

namespace
{
	// ---- the trees ---------------------------------------------------------------------------------------------------------

	/** A stock "Has Tags" enter condition on `State`: succeeds while the evaluator's tag container holds `Tag`. */
	void AddTagGate(UStateTreeEditorData& ED, UStateTreeState& State, const FStateTreeEditorNode& TagsEvaluator, const FGameplayTag& Tag)
	{
		TStateTreeEditorNode<FGameplayTagContainerMatchCondition>& Gate = State.AddEnterCondition<FGameplayTagContainerMatchCondition>();
		Gate.GetInstanceData().OtherContainer.AddTag(Tag);                                                   // parameter: the tag that must be present
		const bool bBound = ED.AddPropertyBinding(TagsEvaluator, TEXT("Tags"), Gate, TEXT("TagContainer"));   // input: the evaluator's current tags
		UE_LOG(LogShift, Log, TEXT("StateTree gate %s on %s bound to the game tags: %d"), *Tag.ToString(), *State.Name.ToString(), bBound ? 1 : 0);
	}

	/** Rival: wait for the turn, then (in order) Mega Campaign pulse, defend a weak hub, or attack the best target; then wait again.
	 *  The two gates are the stock "Has Tags" condition reading Shift.Rival.MegaPending / Shift.Rival.HasWeakHub from the game-tags evaluator. */
	void BuildRival(UStateTreeEditorData& ED)
	{
		TStateTreeEditorNode<FShiftEvaluator_GameTags>& GameTags = ED.AddEvaluator<FShiftEvaluator_GameTags>();
		UStateTreeState& Root = ED.AddSubTree(FName(TEXT("Rival")));
		Root.SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;

		UStateTreeState& Wait = Root.AddChildState(FName(TEXT("Wait")));
		Wait.AddTask<FShiftTask_RivalWait>();
		Wait.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::NextState);

		UStateTreeState& Act = Root.AddChildState(FName(TEXT("Act")));
		Act.SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;

		UStateTreeState& Mega = Act.AddChildState(FName(TEXT("MegaCampaign")));
		AddTagGate(ED, Mega, GameTags, ShiftTags::RivalMegaPending.GetTag());
		Mega.AddTask<FShiftTask_RivalMega>();
		Mega.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Root);

		UStateTreeState& Defend = Act.AddChildState(FName(TEXT("Defend")));
		AddTagGate(ED, Defend, GameTags, ShiftTags::RivalHasWeakHub.GetTag());
		Defend.AddTask<FShiftTask_RivalDefend>();
		Defend.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Root);

		UStateTreeState& Attack = Act.AddChildState(FName(TEXT("Attack")));
		Attack.AddTask<FShiftTask_RivalAttack>();
		Attack.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Root);
	}

	/** Other entries (one instance each): wait for the turn, plug a weak low-weight node, repeat. */
	void BuildOthers(UStateTreeEditorData& ED)
	{
		UStateTreeState& Root = ED.AddSubTree(FName(TEXT("OtherEntry")));
		Root.SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;

		UStateTreeState& Wait = Root.AddChildState(FName(TEXT("Wait")));
		Wait.AddTask<FShiftTask_OtherWait>();
		Wait.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::NextState);

		UStateTreeState& Act = Root.AddChildState(FName(TEXT("Plug")));
		Act.AddTask<FShiftTask_OtherAct>();
		Act.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Root);
	}

	/** Mod Eye: a global heat guard, then idle -> telegraph -> sweep, forever. */
	void BuildDirector(UStateTreeEditorData& ED)
	{
		ED.AddGlobalTask<FShiftTask_ModHeatGuard>();

		UStateTreeState& Root = ED.AddSubTree(FName(TEXT("ModEye")));
		Root.SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;

		UStateTreeState& Idle = Root.AddChildState(FName(TEXT("Idle")));
		Idle.AddTask<FShiftTask_ModIdle>();
		Idle.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::NextState);

		UStateTreeState& Telegraph = Root.AddChildState(FName(TEXT("Telegraph")));
		Telegraph.AddTask<FShiftTask_ModTelegraph>();
		Telegraph.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::NextState);

		UStateTreeState& Sweep = Root.AddChildState(FName(TEXT("Sweep")));
		Sweep.AddTask<FShiftTask_ModSweep>();
		Sweep.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Root);
	}

	/** Ambient events: wait, then one of three events picked at random (seeded), then wait again. */
	void BuildAmbient(UStateTreeEditorData& ED)
	{
		UStateTreeState& Root = ED.AddSubTree(FName(TEXT("Ambient")));
		Root.SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;

		UStateTreeState& Wait = Root.AddChildState(FName(TEXT("Wait")));
		Wait.AddTask<FShiftTask_AmbientWait>();
		Wait.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::NextState);

		UStateTreeState& Pick = Root.AddChildState(FName(TEXT("PickEvent")));
		Pick.SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandom;

		const TCHAR* Names[3] = { TEXT("AlgorithmBoost"), TEXT("StreamerDiscovers"), TEXT("DramaThread") };
		for (int32 Kind = 0; Kind < 3; ++Kind)
		{
			UStateTreeState& Event = Pick.AddChildState(FName(Names[Kind]));
			TStateTreeEditorNode<FShiftTask_AmbientEvent>& Task = Event.AddTask<FShiftTask_AmbientEvent>();
			Task.GetInstanceData().Kind = Kind;
			Event.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Root);
		}
	}

	// ---- asset plumbing ----------------------------------------------------------------------------------------------------

	bool MakeTree(const TCHAR* Name, void (*Build)(UStateTreeEditorData&))
	{
		const FString PackageName = FString::Printf(TEXT("/Game/Shift/StateTrees/%s"), Name);
		UPackage* Package = CreatePackage(*PackageName);
		UStateTree* Tree = FindObject<UStateTree>(Package, Name);
		if (!Tree)
		{
			Tree = NewObject<UStateTree>(Package, FName(Name), RF_Public | RF_Standalone | RF_Transactional);
		}
		UStateTreeEditorData* ED = NewObject<UStateTreeEditorData>(Tree, FName(), RF_Transactional);
		Tree->EditorData = ED;
		ED->Schema = NewObject<UShiftStateTreeSchema>(ED);
		Build(*ED);

		FStateTreeCompilerLog Log;
		const bool bCompiled = UStateTreeEditingSubsystem::CompileStateTree(Tree, Log);
		Log.DumpToLog(LogShift);
		UE_LOG(LogShift, Log, TEXT("StateTree %s: compiled=%d ready=%d"), Name, bCompiled ? 1 : 0, Tree->IsReadyToRun() ? 1 : 0);
		if (!bCompiled || !Tree->IsReadyToRun())
		{
			return false;
		}

		Package->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Tree);
		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const bool bSaved = UPackage::SavePackage(Package, Tree, *Filename, Args);
		UE_LOG(LogShift, Log, TEXT("StateTree %s saved=%d -> %s"), Name, bSaved ? 1 : 0, *Filename);
		return bSaved;
	}
}
#endif

int32 UShiftStateTreeBuilder::BuildAll()
{
#if WITH_EDITOR
	int32 Saved = 0;
	Saved += MakeTree(TEXT("ST_Rival"), &BuildRival) ? 1 : 0;
	Saved += MakeTree(TEXT("ST_Others"), &BuildOthers) ? 1 : 0;
	Saved += MakeTree(TEXT("ST_Director"), &BuildDirector) ? 1 : 0;
	Saved += MakeTree(TEXT("ST_Ambient"), &BuildAmbient) ? 1 : 0;
	return Saved;
#else
	return 0;
#endif
}
