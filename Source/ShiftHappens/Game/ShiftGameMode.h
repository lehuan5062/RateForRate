// Rate for Rate - Game Off 2026

#pragma once

#include "GameFramework/GameModeBase.h"
#include "UObject/ObjectMacros.h"
#include "ShiftGameMode.generated.h"

/** Game mode: no pawn, our own player controller (which owns the Slate UI and the unattended-QA hooks). */
UCLASS()
class SHIFTHAPPENS_API AShiftGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AShiftGameMode();
};
