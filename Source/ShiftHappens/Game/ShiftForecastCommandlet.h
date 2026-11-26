// Rate for Rate - Game Off 2026

#pragma once

#include "Commandlets/Commandlet.h"
#include "Containers/UnrealString.h"
#include "HAL/Platform.h"
#include "UObject/ObjectMacros.h"
#include "ShiftForecastCommandlet.generated.h"

/**
 * Headless runs of the build screen's forecast (Game/ShiftForecast.h): what a steady player gets in a world with a build, over a set of sample variations.
 *
 *   UnrealEditor-Cmd.exe RateForRate.uproject -run=ShiftForecast -Study [-Jam=0|1|2|all] [-Worlds=16] [-K=16] [-Seed0=1000] [-Modes=1,2,4,6] [-Noise=0.3] [-Tag=x]
 *       plays every world (seed Seed0 + w * 7919) under the one-lever-at-a-time builds (4/4/4 and each lever at 0, 2, 6, 8 with the others at 4) with K samples per mode
 *       and writes every play to Saved/Forecast/study_<Tag>.csv (the analysis lives in Tools/forecast_check.ps1 and the scratch scripts).
 *   UnrealEditor-Cmd.exe RateForRate.uproject -run=ShiftForecast -Jam=1 -Seed=1000 -Build=4,4,4 [-Samples=8] [-Mode=7]
 *       prints the forecast of one world and build.
 *   (Tools/forecast.ps1 wraps both.)
 */
UCLASS()
class UShiftForecastCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UShiftForecastCommandlet();
	virtual int32 Main(const FString& Params) override;
};
