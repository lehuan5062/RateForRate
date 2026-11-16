// Shift Happens - Game Off 2026

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ShiftTestRow.generated.h"

/** S1 spike row: proves CSV -> DataTable import from headless Python. Replaced by the real row structs in M5. */
USTRUCT(BlueprintType)
struct SHIFTHAPPENS_API FShiftTestRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift")
	FString Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift")
	int32 Weight = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift")
	float Factor = 0.0f;
};
