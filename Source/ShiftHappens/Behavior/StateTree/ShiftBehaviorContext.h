// Rate for Rate - Game Off 2026

#pragma once

#include "Game/ShiftRun.h"
#include "HAL/Platform.h"
#include "Sim/ShiftRng.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "ShiftBehaviorContext.generated.h"

/** What the behaviour tasks did; every action task bumps one, FShiftBehavior logs the totals when the run ends (proof the trees drive the behaviour). */
enum class EShiftBehaviorCounter : uint8
{
	RivalMega,
	RivalDefend,
	RivalAttack,
	OtherPlug,
	ModTelegraph,
	ModSweep,
	AmbientBoost,
	AmbientStreamer,
	AmbientDrama,
	Count
};

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

	int32 Counters[static_cast<int32>(EShiftBehaviorCounter::Count)] = {};
	void Bump(EShiftBehaviorCounter Counter) { ++Counters[static_cast<int32>(Counter)]; }
};
