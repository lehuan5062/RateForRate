// Shift Happens - Game Off 2026

#include "Audio/ShiftAudioStats.h"

#include "Containers/UnrealString.h"
#include "HAL/Platform.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreMiscDefines.h"
#include "Misc/Parse.h"
#include "Misc/ScopeLock.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/NameTypes.h"

FShiftAudioStats& FShiftAudioStats::Get()
{
	static FShiftAudioStats Instance;
	return Instance;
}

bool FShiftAudioStats::TapsWanted()
{
	static const bool bWanted = []()
		{
			FString Spec;
			return FParse::Value(FCommandLine::Get(), TEXT("ShiftAudioMeter="), Spec, false) && !FParse::Param(FCommandLine::Get(), TEXT("ShiftAudioCpu"));
		}();
	return bWanted;
}

FShiftAudioStats::FTap* FShiftAudioStats::FindOrAddTap(const FName& Name, bool bCounter, int32 Expected)
{
	FScopeLock Guard(&Lock);
	for (const TUniquePtr<FTap>& Tap : Taps)
	{
		if (Tap->Name == Name)
		{
			return Tap.Get();
		}
	}
	TUniquePtr<FTap> Tap = MakeUnique<FTap>();
	Tap->Name = Name;
	Tap->bCounter = bCounter;
	Tap->Expected = Expected;
	FTap* Raw = Tap.Get();
	Taps.Add(MoveTemp(Tap));
	return Raw;
}

FShiftAudioStats::FTap* FShiftAudioStats::FindOrAddTailTap(const FName& Name)
{
	FTap* Tap = FindOrAddTap(Name, false, 0);
	Tap->bTail = true;
	return Tap;
}

FShiftAudioStats::FTap* FShiftAudioStats::TapAt(int32 Index) const
{
	FScopeLock Guard(&Lock);
	return Taps.IsValidIndex(Index) ? Taps[Index].Get() : nullptr;
}

int32 FShiftAudioStats::IndexOf(const FTap* Tap) const
{
	FScopeLock Guard(&Lock);
	for (int32 i = 0; i < Taps.Num(); ++i)
	{
		if (Taps[i].Get() == Tap)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 FShiftAudioStats::NumTaps() const
{
	FScopeLock Guard(&Lock);
	return Taps.Num();
}

void FShiftAudioStats::FLevels::ResetAccumulators()
{
	PeakMax = 0.0f;
	RmsMax = 0.0f;
	LowMax = 0.0f;
	MidMax = 0.0f;
	HighMax = 0.0f;
	RmsSum = 0.0;
	LowSum = 0.0;
	MidSum = 0.0;
	HighSum = 0.0;
	Blocks = 0;
}

void FShiftAudioStats::ResetValues()
{
	Master.Peak = 0.0f;
	Master.Rms = 0.0f;
	Master.Low = 0.0f;
	Master.Mid = 0.0f;
	Master.High = 0.0f;
	Master.BlocksTotal = 0;
	Master.ResetAccumulators();
	Step = 0;
	StepChanges = 0;
	FScopeLock Guard(&Lock);
	for (const TUniquePtr<FTap>& Tap : Taps)
	{
		Tap->Count = 0;
		Tap->Level = 0.0f;
	}
}
