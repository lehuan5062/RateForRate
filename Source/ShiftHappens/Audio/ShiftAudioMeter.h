// Shift Happens - Game Off 2026

#pragma once

#include "Components/AudioComponent.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Engine/World.h"
#include "HAL/Platform.h"
#include "HarmonixMetasound/Components/MusicClockComponent.h"
#include "MetasoundOutput.h"
#include "UObject/NameTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "ShiftAudioMeter.generated.h"

/**
 * QA only (-ShiftAudioMeter=<spec>): verifies the audio without hearing it and without recording anything.
 *
 * Every MetaSound Source exposes graph outputs tapped before its master volume and limiter: Peak, Rms and the band levels Low, Mid, High.
 * This plays the sources with the component volume at nearly zero (inaudible, but the generator still renders), reads those outputs through
 * UMetaSoundOutputSubsystem::WatchOutput and logs one PASS/FAIL line per sound:
 *   - every effect finishes, is audible (above about -34 dBFS) and stays below -1 dBFS
 *   - the music stems are audible and below -1 dBFS, night shifts the spectrum down, tension adds high frequencies, day and night are about
 *     equally loud
 *   - the Harmonix clock steps every 16th note, wraps 128 -> 1 with the loop, and delivers beat events
 *   - every pattern lane of MS_Music fires exactly its pattern (Hits_<lane>_<n> outputs) and every voice is audible (Lvl_<voice> outputs)
 * Tools/py/ms_calibrate.py reads the same log to trim each sound to its target level.
 *
 *   spec = all | sfx | music | comma-separated asset names under /Game/Shift/Audio (for example MS_Sfx_Click,MS_Music)
 * Needs a real audio device (-Sound), but nothing is audible.
 */
UCLASS()
class UShiftAudioMeter : public UObject
{
	GENERATED_BODY()

public:
	void Begin(UWorld* InWorld, const FString& Spec);

	/** Advances the run; returns false once every case has been measured and the summary is logged. */
	bool Tick(float DeltaTime);

	int32 GetFailures() const { return Failures; }

private:
	struct FCase
	{
		FString Asset;
		FString Label;
		float Seconds = 4.0f;        // one-shots: maximum wait for OnFinished; looping sources: how long to measure
		bool bLooping = false;
		bool bClock = false;         // bind a Harmonix music clock to the "MIDI Clock" output and count its beat events
		float Day = 1.0f;
		float Night = 0.0f;
		float Tension = 0.0f;
	};

	struct FStat
	{
		float Max = 0.0f;
		double Sum = 0.0;
		int32 Count = 0;
		float Mean() const { return Count > 0 ? static_cast<float>(Sum / Count) : 0.0f; }
	};

	struct FResult
	{
		FString Label;
		float PeakMax = 0.0f;
		float RmsMax = 0.0f;
		float RmsMean = 0.0f;
		float Low = 0.0f;
		float Mid = 0.0f;
		float High = 0.0f;
		float Duration = 0.0f;
		bool bFinished = false;
		bool bLooping = false;
	};

	/** MS_Music pattern lane tap: Hits_<lane>_<hits per loop> (Int32). */
	struct FLaneHits
	{
		FName Output;
		FString Lane;
		int32 Expected = 0;
		int32 Count = 0;
	};

	void BuildCases(const FString& Spec);
	void StartCase();
	void FinishCase();
	void ReleaseComponents();
	void CheckMusicClock(const FCase& C);
	void CheckMusicLanes();
	void LogSummary();
	void OnOutput(FName OutputName, const FMetaSoundOutput& Output);

	UFUNCTION()
	void HandleFinished();

	UFUNCTION()
	void HandleBeat(int32 BeatNumber, int32 BeatInBar);

	TWeakObjectPtr<UWorld> World;
	TArray<FCase> Cases;
	int32 CaseIndex = -1;
	TArray<FResult> Results;

	UPROPERTY()
	TObjectPtr<UAudioComponent> Component;

	UPROPERTY()
	TObjectPtr<UMusicClockComponent> Clock;

	float CaseClock = 0.0f;
	float FinishedAt = 0.0f;
	float WarmUp = 0.0f;
	bool bCaseFinished = false;
	bool bDone = false;
	int32 Failures = 0;

	FStat Peak, Rms, Low, Mid, High;
	int32 OutputUpdates = 0;

	// music clock: the StepCount output (Int32, 1..128) and the Harmonix beat events
	TArray<double> StepTimes;
	TArray<int32> StepValues;
	TArray<double> BeatTimes;
	int32 LastStep = 0;
	int32 StepWraps = 0;
	int32 MaxStep = 0;

	TArray<FLaneHits> LaneHits;
	TMap<FName, float> VoiceLevels;    // Lvl_<voice>: peak of each music voice
};
