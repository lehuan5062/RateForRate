// Shift Happens - Game Off 2026

#pragma once

#include "CoreMinimal.h"
#include "Game/ShiftRun.h"
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
	/** Loads the trees and builds the instances. Returns null (and logs why) when an asset is missing, so the run can fall back to the native policies. */
	static TSharedPtr<FShiftBehavior> Create(FShiftRun& Run);

	virtual void Tick(float DeltaTime) override;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FShiftBehavior"); }

	int32 InstanceCount() const { return Instances.Num(); }

private:
	struct FInstance
	{
		FString Name;
		UShiftBehaviorContext* Context = nullptr;
		UStateTree* Tree = nullptr;
		FStateTreeInstanceData Data;
		int32 Seed = 0;
		bool bStarted = false;
		bool bFailed = false;
	};

	TArray<TUniquePtr<FInstance>> Instances;
};
