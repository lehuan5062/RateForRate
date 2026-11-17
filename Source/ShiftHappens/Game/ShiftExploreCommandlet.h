// Shift Happens - Game Off 2026

#pragma once

#include "Commandlets/Commandlet.h"
#include "Containers/UnrealString.h"
#include "HAL/Platform.h"
#include "UObject/ObjectMacros.h"
#include "ShiftExploreCommandlet.generated.h"

/**
 * Looks for dominant strategies before a player does. One parameterised bot (the Explorer: every habit of the other bots as a switch or a threshold, see PolicyParams X*)
 * is searched over those knobs inside one process (random search seeded with the hand-written strategies, then a hill climb), and the best configuration per jam is
 * validated on fresh seeds against the honest greedy bot. A jam where the best response beats greedy by more than the gate's limit has a strategy that is too strong.
 *
 *   UnrealEditor-Cmd.exe ShiftHappens.uproject -run=ShiftExplore [-Samples=30] [-Climb=10] [-Runs=40] [-Jam=0|1|2|all] [-Seed0=1000] [-Seed=1]
 *       [-Set=Name=Value,...] [-Build=Q,T,N] [-NoStateTree] [-NoMods]        (Tools/explore.ps1 wraps this)
 */
UCLASS()
class UShiftExploreCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UShiftExploreCommandlet();
	virtual int32 Main(const FString& Params) override;
};
