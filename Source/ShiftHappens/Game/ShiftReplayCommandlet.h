// Shift Happens - Game Off 2026

#pragma once

#include "Commandlets/Commandlet.h"
#include "Containers/UnrealString.h"
#include "HAL/Platform.h"
#include "UObject/ObjectMacros.h"
#include "ShiftReplayCommandlet.generated.h"

/**
 * Replays the recordings of played jams (Game/ShiftRecording.h) headlessly and reports how each went: the recorded result against the replayed one (the same build and tuning end in the
 * same state hash), the clicks by kind and what they did, when the first celebrity was taken, and where the shares stood along the jam. This is the playtest data a balance change starts from.
 *
 *   UnrealEditor-Cmd.exe ShiftHappens.uproject -run=ShiftReplay -Rec=<file.shiftrec.txt> | -Dir=<folder> [-Set=Name=Value,...] [-NoStateTree]        (Tools/replay.ps1 wraps this)
 *
 * -Set replays the recorded clicks under other tuning ("what would this player's jam have been with my change"): the run then diverges from the recording, and the report says so.
 */
UCLASS()
class UShiftReplayCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UShiftReplayCommandlet();
	virtual int32 Main(const FString& Params) override;
};
