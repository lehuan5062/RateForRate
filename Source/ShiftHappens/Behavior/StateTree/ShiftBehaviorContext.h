// Shift Happens - Game Off 2026

#pragma once

#include "CoreMinimal.h"
#include "Sim/ShiftRng.h"
#include "UObject/Object.h"
#include "ShiftBehaviorContext.generated.h"

class FShiftRun;

/**
 * What a behaviour StateTree instance can reach: the run (sim, events, policies) plus the instance's own schedule and random stream.
 * Provided to the trees as external data (see UShiftStateTreeSchema), one object per tree instance.
 */
UCLASS()
class UShiftBehaviorContext : public UObject
{
	GENERATED_BODY()

public:
	FShiftRun* Run = nullptr;
	int32 EntryId = -1;          // the entry this instance plays (rival tree: the rival; others trees: one other entry)
	double NextTime = 0.0;       // sim time this instance acts next (rival turn, other entry turn, next ambient event)
	ShiftSim::Rng Rng;           // this instance's own stream: derived from the run seed, so runs reproduce exactly
};
