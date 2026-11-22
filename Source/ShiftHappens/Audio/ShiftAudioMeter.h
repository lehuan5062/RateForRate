// Shift Happens - Game Off 2026

#pragma once

#include "Components/AudioComponent.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Delegates/IDelegateInstance.h"
#include "Engine/World.h"
#include "HAL/Platform.h"
#include "HarmonixMetasound/Components/MusicClockComponent.h"
#include "MetasoundGeneratorHandle.h"
#include "MetasoundSource.h"
#include "Sound/SoundBase.h"
#include "UObject/NameTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "ShiftAudioMeter.generated.h"

/**
 * QA only (-ShiftAudioMeter=<spec>): verifies the audio without hearing it and without recording anything.
 *
 * Every MetaSound Source is one composite node whose invisible QA taps publish the level of its mix before the master volume and the limiter (Peak, Rms and
 * the band levels Low, Mid, High), the hits of every pattern lane and the peak of every voice to FShiftAudioStats.
 * This plays the sources with the component volume at nearly zero (inaudible, but the generator still renders), reads those numbers every frame
 * and logs one PASS/FAIL line per sound:
 *   - every effect finishes, is audible (above about -34 dBFS) and stays below -1 dBFS
 *   - the music stems are audible and below -1 dBFS, night shifts the spectrum down, tension adds high frequencies, day and night are about
 *     equally loud
 *   - the Harmonix clock steps every 16th note, wraps 128 -> 1 with the loop, and delivers beat events
 *   - every pattern lane of MS_Music fires exactly its pattern (the hit taps) and every voice is audible (the level taps)
 * Tools/py/ms_calibrate.py reads the same log to trim each sound to its target level.
 *
 *   spec = all | sfx | music | comma-separated asset names under /Game/Shift/Audio (for example MS_Sfx_Click,MS_Music)
 * Needs a real audio device (-Sound), but nothing is audible.
 *
 * -ShiftAudioCpu (with -ShiftAudioMeter=<spec>): measures what the sounds cost instead of how they sound. The QA taps stay off (they are not part of the shipped
 * cost), the generator's own render timing is read (UMetasoundGeneratorHandle::GetCPUCoreUtilization, the number the MetaSound editor shows as "CPU Core")
 * and one line per case logs the mean and maximum, as a share of one core.
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
		bool bWarmUp = false;        // the throw-away case that proves a generator delivers meter data before the real cases run
		float Day = 1.0f;
		float Night = 0.0f;
		float Tension = 0.0f;
		int32 Bench = -1;            // -ShiftMusicBench: index of the benchmark spec this case plays instead of the real music
		float SwitchAt = -1.0f;      // >= 0: at this second the gains change to the ones below (a layer that was silent has to wake up)
		float SwitchDay = 0.0f;
		float SwitchNight = 0.0f;
		float SwitchTension = 0.0f;
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
	void CheckWake();
	void LogSummary();
	/** Reads the numbers the composite audio nodes publish (Audio/ShiftAudioStats.h). */
	void PollStats();

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
	bool bSwitched = false;
	bool bDone = false;
	int32 Failures = 0;

	// The generator's output analyzers attach one game-thread hop after the generator exists, so a short one-shot that starts while the game thread is busy (a cold
	// first start) can be over before any meter data arrives. The warm-up is replayed until data arrives, and a one-shot without data is retried before it fails.
	bool bStarted = false;           // the first case starts on the first Tick: Begin runs inside engine init
	double WarmStartWall = 0.0;      // FPlatformTime::Seconds() at the first Tick
	int32 Attempt = 1;               // tries of the current case so far, counting this one

	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> Preloaded;   // every case asset is loaded up front, so no case starts with a synchronous LoadObject

	// QA hooks: start numbers count every Play() of the run, warm-up and retries included.
	//   -ShiftAudioMeterStall=<ms>@<starts>  stalls the game thread as the generator of those starts appears (a stress test: whether the race is lost depends on timing)
	//   -ShiftAudioMeterDrop=<starts>        ignores every meter update of those starts (forces the zero-update path deterministically)
	int32 StartCount = 0;
	int32 StallMs = 0;
	TArray<int32> StallStarts;
	TArray<int32> DropStarts;
	FDelegateHandle StallListener;
	TWeakObjectPtr<UMetaSoundSource> StallSource;
	FDelegateHandle SelectListener;     // tells the taps which generator instance is this case's
	TWeakObjectPtr<UMetaSoundSource> SelectSource;

	FStat Peak, Rms, Low, Mid, High;

	// -ShiftAudioCpu: the generator's render cost as a fraction of one core
	bool bCpuMode = false;
	FStat Cpu;
	UPROPERTY()
	TObjectPtr<UMetasoundGeneratorHandle> CpuHandle;
	int32 OutputUpdates = 0;
	bool bAccumulating = false;      // the warm-up second of a looping case is over: the accumulators were reset
	int32 LastStepChanges = 0;

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
