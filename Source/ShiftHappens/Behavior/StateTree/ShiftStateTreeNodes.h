// Shift Happens - Game Off 2026

#pragma once

#include "Behavior/StateTree/ShiftBehaviorContext.h"
#include "GameplayTagContainer.h"
#include "HAL/Platform.h"
#include "StateTreeConditionBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeLinker.h"
#include "StateTreeTaskBase.h"
#include "UObject/Class.h"
#include "UObject/ObjectMacros.h"
#include "ShiftStateTreeNodes.generated.h"

// The tasks and conditions are thin: they sequence and decide, and call the policy building blocks in Behavior/ShiftPolicies.h (the
// numbers live in the sim core and were calibrated there). Every node reads the run through the external UShiftBehaviorContext.

USTRUCT()
struct FShiftNoInstanceData
{
	GENERATED_BODY()
};

// ---------------------------------------------------------------------------------------------------------------------------------
// Game tags: the state of the run as Gameplay Tags, tested by the stock "Has Tags" condition
// ---------------------------------------------------------------------------------------------------------------------------------

USTRUCT()
struct FShiftEvaluator_GameTagsData
{
	GENERATED_BODY()

	/** The tags that hold right now (see Behavior/ShiftTags.h). Bound to the "Has Tags" conditions' Tag Container. */
	UPROPERTY(EditAnywhere, Category = "Output")
	FGameplayTagContainer Tags;
};

USTRUCT(meta = (DisplayName = "Shift: Game Tags", Category = "Shift"))
struct FShiftEvaluator_GameTags : public FStateTreeEvaluatorCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FShiftEvaluator_GameTagsData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override { Linker.LinkExternalData(ContextHandle); return true; }
	virtual void TreeStart(FStateTreeExecutionContext& Context) const override;
	virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	TStateTreeExternalDataHandle<UShiftBehaviorContext> ContextHandle;
};

// ---------------------------------------------------------------------------------------------------------------------------------
// Rival
// ---------------------------------------------------------------------------------------------------------------------------------

USTRUCT(meta = (DisplayName = "Rival: Wait For Turn", Category = "Shift"))
struct FShiftTask_RivalWait : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FShiftNoInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override { Linker.LinkExternalData(ContextHandle); return true; }
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	TStateTreeExternalDataHandle<UShiftBehaviorContext> ContextHandle;
};

USTRUCT(meta = (DisplayName = "Rival: Mega Campaign Pulse", Category = "Shift"))
struct FShiftTask_RivalMega : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FShiftNoInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override { Linker.LinkExternalData(ContextHandle); return true; }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	TStateTreeExternalDataHandle<UShiftBehaviorContext> ContextHandle;
};

USTRUCT(meta = (DisplayName = "Rival: Defend", Category = "Shift"))
struct FShiftTask_RivalDefend : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FShiftNoInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override { Linker.LinkExternalData(ContextHandle); return true; }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	TStateTreeExternalDataHandle<UShiftBehaviorContext> ContextHandle;
};

USTRUCT(meta = (DisplayName = "Rival: Attack", Category = "Shift"))
struct FShiftTask_RivalAttack : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FShiftNoInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override { Linker.LinkExternalData(ContextHandle); return true; }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	TStateTreeExternalDataHandle<UShiftBehaviorContext> ContextHandle;
};

USTRUCT(meta = (DisplayName = "Rival: Mega Campaign Pending", Category = "Shift"))
struct FShiftCondition_RivalMegaPending : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FShiftNoInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override { Linker.LinkExternalData(ContextHandle); return true; }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
	TStateTreeExternalDataHandle<UShiftBehaviorContext> ContextHandle;
};

USTRUCT(meta = (DisplayName = "Rival: Has Weak High-Value Node", Category = "Shift"))
struct FShiftCondition_RivalHasWeakHub : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FShiftNoInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override { Linker.LinkExternalData(ContextHandle); return true; }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
	TStateTreeExternalDataHandle<UShiftBehaviorContext> ContextHandle;
};

// ---------------------------------------------------------------------------------------------------------------------------------
// Other entries
// ---------------------------------------------------------------------------------------------------------------------------------

USTRUCT(meta = (DisplayName = "Other Entry: Wait For Turn", Category = "Shift"))
struct FShiftTask_OtherWait : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FShiftNoInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override { Linker.LinkExternalData(ContextHandle); return true; }
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	TStateTreeExternalDataHandle<UShiftBehaviorContext> ContextHandle;
};

USTRUCT(meta = (DisplayName = "Other Entry: Plug A Weak Node", Category = "Shift"))
struct FShiftTask_OtherAct : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FShiftNoInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override { Linker.LinkExternalData(ContextHandle); return true; }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	TStateTreeExternalDataHandle<UShiftBehaviorContext> ContextHandle;
};

// ---------------------------------------------------------------------------------------------------------------------------------
// Jam director: the Mod Eye
// ---------------------------------------------------------------------------------------------------------------------------------

USTRUCT(meta = (DisplayName = "Mods: Heat Guard", Category = "Shift"))
struct FShiftTask_ModHeatGuard : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FShiftNoInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override { Linker.LinkExternalData(ContextHandle); return true; }
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	TStateTreeExternalDataHandle<UShiftBehaviorContext> ContextHandle;
};

USTRUCT(meta = (DisplayName = "Mods: Idle Until Telegraph", Category = "Shift"))
struct FShiftTask_ModIdle : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FShiftNoInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override { Linker.LinkExternalData(ContextHandle); return true; }
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	TStateTreeExternalDataHandle<UShiftBehaviorContext> ContextHandle;
};

USTRUCT(meta = (DisplayName = "Mods: Telegraph Sweep", Category = "Shift"))
struct FShiftTask_ModTelegraph : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FShiftNoInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override { Linker.LinkExternalData(ContextHandle); return true; }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	TStateTreeExternalDataHandle<UShiftBehaviorContext> ContextHandle;
};

USTRUCT(meta = (DisplayName = "Mods: Sweep", Category = "Shift"))
struct FShiftTask_ModSweep : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FShiftNoInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override { Linker.LinkExternalData(ContextHandle); return true; }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	TStateTreeExternalDataHandle<UShiftBehaviorContext> ContextHandle;
};

// ---------------------------------------------------------------------------------------------------------------------------------
// Jam director: ambient events (algorithm boosts, streamers, drama), paced by the phase of the jam
// ---------------------------------------------------------------------------------------------------------------------------------

USTRUCT()
struct FShiftAmbientEventData
{
	GENERATED_BODY()

	/** 0 = algorithm boost, 1 = streamer discovers an entry, 2 = drama thread. */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	int32 Kind = 0;
};

USTRUCT(meta = (DisplayName = "Director: Wait For Next Event", Category = "Shift"))
struct FShiftTask_AmbientWait : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FShiftNoInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override { Linker.LinkExternalData(ContextHandle); return true; }
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	TStateTreeExternalDataHandle<UShiftBehaviorContext> ContextHandle;
};

USTRUCT(meta = (DisplayName = "Director: Ambient Event", Category = "Shift"))
struct FShiftTask_AmbientEvent : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FShiftAmbientEventData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override { Linker.LinkExternalData(ContextHandle); return true; }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	TStateTreeExternalDataHandle<UShiftBehaviorContext> ContextHandle;
};
