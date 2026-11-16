// Shift Happens - Game Off 2026

#pragma once

#include "CoreMinimal.h"
#include "StateTreeSchema.h"
#include "ShiftStateTreeSchema.generated.h"

/**
 * Schema of the behaviour trees (rival, other entries, jam director, ambient events). They run on a UShiftBehaviorContext that the
 * game ticks by hand every fixed 20 Hz sim step, so there is no AI controller, pawn or scheduled ticking involved.
 * Tasks, conditions and evaluators must derive from the common bases; the only external data is the behaviour context.
 */
UCLASS()
class UShiftStateTreeSchema : public UStateTreeSchema
{
	GENERATED_BODY()

public:
	virtual bool IsStructAllowed(const UScriptStruct* InScriptStruct) const override;
	virtual bool IsClassAllowed(const UClass* InClass) const override { return false; }
	virtual bool IsExternalItemAllowed(const UStruct& InStruct) const override;
	virtual bool IsScheduledTickAllowed() const override { return false; }
};
