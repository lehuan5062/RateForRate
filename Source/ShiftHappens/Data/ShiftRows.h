// Shift Happens - Game Off 2026

#pragma once

#include "Containers/UnrealString.h"
#include "Engine/DataTable.h"
#include "HAL/Platform.h"
#include "UObject/ObjectMacros.h"
#include "ShiftRows.generated.h"

// Row structs for the text tables in Data/*.csv. Property names match the CSV headers (case-insensitive); the first CSV column
// (Id) becomes the row name. Tools/py/build_data.py imports each CSV into /Game/Shift/Data/DT_<Name>.

USTRUCT(BlueprintType)
struct SHIFTHAPPENS_API FShiftHandleRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Handle;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Kind;
};

USTRUCT(BlueprintType)
struct SHIFTHAPPENS_API FShiftBioRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Kind;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Text;
};

USTRUCT(BlueprintType)
struct SHIFTHAPPENS_API FShiftCommentRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Sentiment;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Context;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Text;
};

USTRUCT(BlueprintType)
struct SHIFTHAPPENS_API FShiftHeadlineRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Event;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Text;
};

USTRUCT(BlueprintType)
struct SHIFTHAPPENS_API FShiftEntryRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Jam;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Role;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Author;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Blurb;
};

USTRUCT(BlueprintType)
struct SHIFTHAPPENS_API FShiftBlurbRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Outcome;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Condition;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Text;
};

USTRUCT(BlueprintType)
struct SHIFTHAPPENS_API FShiftAchievementRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Description;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") bool Hidden = false;
};

USTRUCT(BlueprintType)
struct SHIFTHAPPENS_API FShiftTutorialRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") int32 Step = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Trigger;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shift") FString Text;
};
