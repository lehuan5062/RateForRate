// Rate for Rate - Game Off 2026

#pragma once

#include "Commandlets/Commandlet.h"
#include "Containers/UnrealString.h"
#include "HAL/Platform.h"
#include "UObject/ObjectMacros.h"
#include "ShiftBalanceCommandlet.generated.h"

/**
 * Headless balance runs against the real behaviour layer (the StateTrees): many full jam runs per jam and scripted player, with win rate,
 * disqualification rate, rank, share, sockpuppet share and the thesis metrics (R squared of final votes against quality and against starting fame).
 *
 *   UnrealEditor-Cmd.exe RateForRate.uproject -run=ShiftBalance -Runs=200 [-Jam=0|1|2|all] [-Bot=idle|random|greedy|sock|all]
 *       [-Build=Q,T,N] [-Seed0=1000] [-NoStateTree] [-NoMods]        (Tools/balance.ps1 wraps this)
 */
UCLASS()
class UShiftBalanceCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UShiftBalanceCommandlet();
	virtual int32 Main(const FString& Params) override;
};
