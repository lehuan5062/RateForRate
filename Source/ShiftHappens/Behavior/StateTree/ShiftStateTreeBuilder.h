// Shift Happens - Game Off 2026

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ShiftStateTreeBuilder.generated.h"

/**
 * Editor-side authoring of the behaviour StateTree assets (ST_Rival, ST_Others, ST_Director, ST_Ambient) from code, so the trees are
 * reproducible outputs like every other generated asset. Run through Tools/py/build_statetrees.py (BuildAll is callable from Python).
 * In non-editor builds it does nothing.
 */
UCLASS()
class UShiftStateTreeBuilder : public UObject
{
	GENERATED_BODY()

public:
	/** Builds, compiles and saves all trees under /Game/Shift/StateTrees. Returns how many were saved. */
	UFUNCTION(BlueprintCallable, Category = "Shift")
	static int32 BuildAll();
};
