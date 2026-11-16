// Shift Happens - Game Off 2026

#pragma once

#include "CoreMinimal.h"
#include "Game/ShiftRun.h"
#include "Behavior/StateTree/ShiftBehaviorContext.h"
#include "StateTreeInstanceData.h"
#include "UObject/GCObject.h"

class UShiftBehaviorContext;
class UStateTree;

/**
 * Runs the behaviour StateTrees for one jam run, ticked by hand once per fixed sim step (no components, controllers or world ticking):
 *   ST_Rival     the rival's turn cycle: wait, then Mega Campaign pulse / defend a weak hub / attack the best target
 *   ST_Others    one instance per other entry: wait, then plug a weak low-weight node
 *   ST_Director  the Mod Eye: heat guard (global task), idle, telegraph, sweep
 *   ST_Ambient   algorithm boosts, streamers and drama threads, paced by the phase of the jam
 * Each instance has its own UShiftBehaviorContext and random stream derived from the run seed, so runs reproduce exactly.
 */
class FShiftBehavior : public IShiftBehaviorDriver, public FGCObject
{
public:
	/**
	 * Loads the trees and builds the instances. Returns null when an asset is missing or not compiled, and logs an ERROR saying so: the
	 * shipped game must always run on the StateTrees. The caller then falls back to the native stand-in policies so the run stays playable
	 * (QA also asks for them on purpose with -ShiftNoStateTree).
	 */
	static TSharedPtr<FShiftBehavior> Create(FShiftRun& Run);

	virtual ~FShiftBehavior() override;

	virtual void Tick(float DeltaTime) override;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FShiftBehavior"); }

	int32 InstanceCount() const { return Instances.Num(); }

	/** Total of one action counter over all instances (see EShiftBehaviorCounter). */
	int32 ActionCount(EShiftBehaviorCounter Counter) const;

private:
	struct FInstance
	{
		FString Name;
		TObjectPtr<UShiftBehaviorContext> Context;
		TObjectPtr<UStateTree> Tree;
		FStateTreeInstanceData Data;
		int32 Seed = 0;
		bool bStarted = false;
		bool bFailed = false;
	};

	TArray<TUniquePtr<FInstance>> Instances;
};
