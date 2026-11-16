// Shift Happens - Game Off 2026

#include "Game/ShiftGameMode.h"
#include "Game/ShiftPlayerController.h"

AShiftGameMode::AShiftGameMode()
{
	PlayerControllerClass = AShiftPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
	HUDClass = nullptr;
}
