// Rate for Rate - Game Off 2026

#include "Audio/ShiftAudioMeter.h"

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Audio/ShiftAudio.h"
#include "Audio/ShiftAudioStats.h"
#include "Components/AudioComponent.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "CoreGlobals.h"
#include "Delegates/Delegate.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/Platform.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HarmonixMetasound/Components/MusicClockComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Logging/LogMacros.h"
#include "Math/UnrealMathUtility.h"
#include "MetasoundGeneratorHandle.h"
#include "MetasoundSource.h"
#include "Misc/CommandLine.h"
#include "Misc/CString.h"
#include "Misc/Parse.h"
#include "ShiftHappens.h"
#include "Sound/SoundBase.h"
#include "Templates/Casts.h"
#include "Templates/TupleFwd.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	constexpr float PeakCeiling = 0.891f;     // -1 dBFS
	constexpr float PeakFloor = 0.02f;        // about -34 dBFS: anything quieter is inaudible
	constexpr float StepSeconds = 60.0f / 112.5f / 4.0f;
	constexpr float MeterBeatSeconds = 60.0f / 112.5f;
	constexpr float LoopSeconds = 128.0f * StepSeconds;
	constexpr TCHAR WarmUpLabel[] = TEXT("(warm-up)");
	constexpr TCHAR WarmUpAsset[] = TEXT("MS_Sfx_Upload");   // the longest one-shot (about 2 s): it outlives a short stall of the game thread
	constexpr double WarmUpMaxSeconds = 20.0;                // wall time the warm-up may take before the run gives up
	constexpr int32 MaxCaseAttempts = 3;                     // a one-shot without any meter data is retried twice before it counts as a failure

	float ToDb(float V) { return 20.0f * FMath::LogX(10.0f, FMath::Max(V, 1.0e-6f)); }

	const TCHAR* DominantBand(float Low, float Mid, float High)
	{
		return (Low >= Mid && Low >= High) ? TEXT("low") : ((Mid >= High) ? TEXT("mid") : TEXT("high"));
	}
}

void UShiftAudioMeter::Begin(UWorld* InWorld, const FString& Spec)
{
	World = InWorld;
	bCpuMode = FParse::Param(FCommandLine::Get(), TEXT("ShiftAudioCpu"));
	BuildCases(Spec);
	if (bCpuMode)
	{
		// the cost does not need a loop and a half: a few bars of each music scenario are enough, and the clock check is not part of this run
		for (int32 i = Cases.Num() - 1; i >= 0; --i)
		{
			if (Cases[i].bClock)
			{
				Cases.RemoveAt(i);
			}
			else if (Cases[i].bLooping)
			{
				Cases[i].Seconds = 9.0f;
			}
		}
		UE_LOG(LogShift, Log, TEXT("AudioMeter: CPU mode: the QA taps stay off, the generator's render timing is read"));
		// -ShiftMusicBench=<kind>:<count>[:loop],...: one case per spec, the music's day layer replaced by <count> copies of one node kind (see ShiftMusicNode.cpp)
		FString BenchList;
		if (FParse::Value(FCommandLine::Get(), TEXT("ShiftMusicBench="), BenchList, false))
		{
			Cases.Reset();
			TArray<FString> Specs;
			BenchList.ParseIntoArray(Specs, TEXT(","));
			for (int32 i = 0; i < Specs.Num(); ++i)
			{
				FCase C;
				C.Asset = TEXT("MS_Music");
				C.Label = FString(TEXT("bench ")) + Specs[i];
				C.bLooping = true;
				C.Seconds = 6.0f;
				C.Bench = i;
				Cases.Add(C);
			}
		}
	}
	UE_LOG(LogShift, Log, TEXT("AudioMeter: %d cases (spec '%s')"), Cases.Num(), *Spec);

	// QA hooks, both take start numbers (counted over every Play() of the run, warm-up and retries included):
	//   -ShiftAudioMeterStall=<ms>@<starts>  stalls the game thread as the generator of those starts appears, like a cold first start does; whether the race is
	//                                        then lost depends on timing, so it is a stress test (for example 600@1,2,3)
	//   -ShiftAudioMeterDrop=<starts>        ignores every meter update of those starts, which forces the zero-update path deterministically
	auto ParseStarts = [](const FString& Text, TArray<int32>& Out)
		{
			TArray<FString> Parts;
			Text.ParseIntoArray(Parts, TEXT(","));
			for (const FString& Part : Parts)
			{
				Out.Add(FCString::Atoi(*Part));
			}
		};
	FString HookSpec;
	if (FParse::Value(FCommandLine::Get(), TEXT("ShiftAudioMeterStall="), HookSpec, false))
	{
		FString MsText, StartsText;
		if (HookSpec.Split(TEXT("@"), &MsText, &StartsText))
		{
			StallMs = FCString::Atoi(*MsText);
			ParseStarts(StartsText, StallStarts);
			UE_LOG(LogShift, Log, TEXT("AudioMeter: QA stall hook: %d ms at start(s) %s"), StallMs, *StartsText);
		}
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("ShiftAudioMeterDrop="), HookSpec, false))
	{
		ParseStarts(HookSpec, DropStarts);
		UE_LOG(LogShift, Log, TEXT("AudioMeter: QA drop hook: no meter data at start(s) %s"), *HookSpec);
	}

	CaseIndex = 0;
	if (Cases.Num() == 0)
	{
		bDone = true;
		return;
	}

	// Load every sound now: a case then never starts with a synchronous LoadObject (a stall of the game thread right when a generator is being created), and the
	// MetaSound graphs are registered before the first Play.
	auto Preload = [this](const FString& Asset) -> bool
		{
			const FString Path = FString::Printf(TEXT("/Game/Shift/Audio/%s.%s"), *Asset, *Asset);
			USoundBase* Sound = LoadObject<USoundBase>(this, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (Sound)
			{
				Preloaded.AddUnique(Sound);
			}
			return Sound != nullptr;
		};
	for (const FCase& C : Cases)
	{
		Preload(C.Asset);
	}

	// The throw-away warm-up. The first MetaSound generator of a process takes a while to come up and the output analyzers attach one game-thread hop after the
	// generator exists, so a short one-shot that starts while the game thread is busy can be over before any meter data arrives. The warm-up is replayed (see Tick)
	// until data actually arrives, so the real cases start on a live pipeline instead of on a guess about how long a cold start takes.
	FCase WarmCase;
	WarmCase.bWarmUp = true;
	WarmCase.Label = WarmUpLabel;
	WarmCase.Seconds = 4.0f;
	if (Preload(WarmUpAsset))
	{
		WarmCase.Asset = WarmUpAsset;
	}
	else
	{
		WarmCase = Cases[0];
		WarmCase.bWarmUp = true;
		WarmCase.Label = WarmUpLabel;
		WarmCase.bClock = false;
		WarmCase.Seconds = FMath::Min(WarmCase.Seconds, 4.0f);
	}
	Cases.Insert(WarmCase, 0);
	// The first case starts on the first Tick: Begin runs inside BeginPlay, in the middle of engine init.
}

void UShiftAudioMeter::BuildCases(const FString& Spec)
{
	auto AddSfx = [this]()
		{
			for (int32 i = 0; i < static_cast<int32>(EShiftSfx::Count); ++i)
			{
				FCase C;
				C.Asset = FString(TEXT("MS_Sfx_")) + ShiftSfxName(static_cast<EShiftSfx>(i));
				C.Label = C.Asset;
				C.Seconds = 4.0f;
				Cases.Add(C);
			}
		};
	auto AddMusic = [this]()
		{
			// One 8-bar loop is 17.07 s; the stems that are calibrated against a peak level (day, night, tension alone) run a whole loop plus
			// the 1 s warm-up so the loudest bar (the snare roll and the riser sit in bar 8) is included. The last scenario runs two loops with
			// the Harmonix clock bound, to check the 128-step wrap, the beat events and every lane.
			struct FScenario { const TCHAR* Label; float Day, Night, Tension, Seconds; bool bClock; };
			const FScenario Scenarios[] = {
				{ TEXT("music day"), 1.0f, 0.0f, 0.0f, 19.0f, false },
				{ TEXT("music night"), 0.0f, 1.0f, 0.0f, 19.0f, false },
				{ TEXT("music tension only"), 0.0f, 0.0f, 1.0f, 19.0f, false },
				{ TEXT("music day+tension"), 1.0f, 0.0f, 1.4f, 19.0f, false },
				{ TEXT("music night+tension"), 0.0f, 1.0f, 1.4f, 19.0f, false },
				// night and tension at -60 dB, not 0: a layer whose gain is 0 is not rendered at all, and this case checks every lane and voice
				{ TEXT("music day x2 loops"), 1.0f, 0.001f, 0.001f, 36.0f, true },
				{ TEXT("music day->night"), 1.0f, 0.0f, 0.0f, 14.0f, false },   // the night layer is skipped until the gain changes at 5 s
			};
			for (const FScenario& S : Scenarios)
			{
				FCase C;
				C.Asset = TEXT("MS_Music");
				C.Label = S.Label;
				C.bLooping = true;
				C.bClock = S.bClock;
				C.Seconds = S.Seconds;
				C.Day = S.Day;
				C.Night = S.Night;
				C.Tension = S.Tension;
				if (FCString::Strcmp(S.Label, TEXT("music day->night")) == 0)
				{
					C.SwitchAt = 5.0f;
					C.SwitchDay = 0.0f;
					C.SwitchNight = 1.0f;
				}
				Cases.Add(C);
			}
		};

	TArray<FString> Tokens;
	Spec.ParseIntoArray(Tokens, TEXT(","));
	for (const FString& Token : Tokens)
	{
		if (Token.Equals(TEXT("all"), ESearchCase::IgnoreCase)) { AddSfx(); AddMusic(); }
		else if (Token.Equals(TEXT("sfx"), ESearchCase::IgnoreCase)) { AddSfx(); }
		else if (Token.Equals(TEXT("music"), ESearchCase::IgnoreCase)) { AddMusic(); }
		else if (!Token.IsEmpty())
		{
			FCase C;
			C.Asset = Token;
			C.Label = Token;
			C.bLooping = Token.Contains(TEXT("Music"));
			C.Seconds = C.bLooping ? 8.0f : 4.0f;
			Cases.Add(C);
		}
	}
}

void UShiftAudioMeter::ReleaseComponents()
{
	FShiftAudioStats::Get().bShadowGating = false;
	CpuHandle = nullptr;
	FShiftAudioStats::Get().bEnabled = false;
	FShiftAudioStats::Get().SelectedInstance = ~uint64(0);
	if (SelectListener.IsValid())
	{
		if (UMetaSoundSource* MetaSound = SelectSource.Get())
		{
			MetaSound->OnGeneratorInstanceInfoCreated.Remove(SelectListener);
		}
		SelectListener.Reset();
		SelectSource.Reset();
	}
	if (StallListener.IsValid())
	{
		if (UMetaSoundSource* MetaSound = StallSource.Get())
		{
			MetaSound->OnGeneratorInstanceInfoCreated.Remove(StallListener);
		}
		StallListener.Reset();
		StallSource.Reset();
	}
	if (Clock)
	{
		Clock->BeatEvent.RemoveAll(this);
		Clock->Stop();
		Clock->DestroyComponent();
		Clock = nullptr;
	}
	if (Component)
	{
		Component->OnAudioFinished.RemoveAll(this);
		Component->Stop();
		Component->DestroyComponent();
		Component = nullptr;
	}
}

void UShiftAudioMeter::StartCase()
{
	UWorld* W = World.Get();
	const FCase& C = Cases[CaseIndex];
	Peak = Rms = Low = Mid = High = Cpu = FStat();
	OutputUpdates = 0;
	StepTimes.Reset();
	StepValues.Reset();
	BeatTimes.Reset();
	LaneHits.Reset();
	VoiceLevels.Reset();
	LastStep = 0;
	StepWraps = 0;
	MaxStep = 0;
	CaseClock = 0.0f;
	FinishedAt = 0.0f;
	WarmUp = C.bLooping ? 1.0f : 0.0f;
	bCaseFinished = false;
	bSwitched = false;
	FShiftAudioStats::Get().BenchIndex = C.Bench;
	FShiftAudioStats::Get().bShadowGating = C.bClock && !bCpuMode;   // the long music case proves the gated voices' windows (see Nodes/ShiftMusicNode.cpp)
	Component = nullptr;
	Clock = nullptr;

	const FString Path = FString::Printf(TEXT("/Game/Shift/Audio/%s.%s"), *C.Asset, *C.Asset);
	USoundBase* Sound = LoadObject<USoundBase>(this, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Sound || !W)
	{
		UE_LOG(LogShift, Error, TEXT("AudioMeter: [FAIL] %s: asset missing (%s)"), *C.Label, *Path);
		++Failures;
		bCaseFinished = true;
		return;
	}

	// Component volume nearly zero: the sound is inaudible, but the generator still renders and the graph's pre-volume meters read real levels.
	// Created stopped so the output watchers are attached before the first audio block is rendered.
	Component = UGameplayStatics::CreateSound2D(W, Sound, 0.0005f, 1.0f, 0.0f, nullptr, false, false);
	if (!Component)
	{
		UE_LOG(LogShift, Error, TEXT("AudioMeter: [FAIL] %s: could not create the sound (no audio device? run with -Sound)"), *C.Label);
		++Failures;
		bCaseFinished = true;
		return;
	}
	Component->bIsUISound = true;

	// QA stall hook: stall the game thread at the moment this sound's generator appears (a cold first start is busiest then). The handle that attaches the
	// output analyzers hops to the game thread when the generator is created, so a stall longer than a short one-shot reproduces the lost-meter-data failure.
	// The listener is added before the first WatchOutput so it runs before the handle's own listener.
	++StartCount;
	if (StallMs > 0 && StallStarts.Contains(StartCount))
	{
		if (UMetaSoundSource* MetaSound = Cast<UMetaSoundSource>(Sound))
		{
			const uint64 ComponentId = Component->GetAudioComponentID();
			const int32 Ms = StallMs;
			const int32 StartNumber = StartCount;
			StallSource = MetaSound;
			StallListener = MetaSound->OnGeneratorInstanceInfoCreated.AddLambda([ComponentId, Ms, StartNumber](const Metasound::FGeneratorInstanceInfo& Info)
				{
					if (Info.AudioComponentID != ComponentId)
					{
						return;
					}
					const bool bOnGameThread = IsInGameThread();
					auto Stall = [Ms, StartNumber, bOnGameThread]()
						{
							UE_LOG(LogShift, Log, TEXT("AudioMeter: QA hook: game thread stalled %d ms as the generator of start %d appeared (created %s the game thread)"), Ms, StartNumber,
								bOnGameThread ? TEXT("on") : TEXT("off"));
							FPlatformProcess::Sleep(static_cast<float>(Ms) / 1000.0f);
						};
					if (bOnGameThread)
					{
						Stall();
					}
					else
					{
						AsyncTask(ENamedThreads::GameThread, MoveTemp(Stall));
					}
				});
		}
	}

	if (C.Asset == TEXT("MS_Music"))
	{
		Component->SetFloatParameter(TEXT("DayGain"), C.Day);
		Component->SetFloatParameter(TEXT("NightGain"), C.Night);
		Component->SetFloatParameter(TEXT("TensionGain"), C.Tension);
		Component->SetFloatParameter(TEXT("Volume"), 1.0f);
	}
	Component->OnAudioFinished.AddDynamic(this, &UShiftAudioMeter::HandleFinished);

	// the composite nodes publish their numbers while the meter has this switched on
	FShiftAudioStats::Get().ResetValues();
	FShiftAudioStats::Get().SelectedInstance = ~uint64(0);
	if (UMetaSoundSource* MetaSound = Cast<UMetaSoundSource>(Sound))
	{
		const uint64 ComponentId = Component->GetAudioComponentID();
		SelectSource = MetaSound;
		SelectListener = MetaSound->OnGeneratorInstanceInfoCreated.AddLambda([ComponentId](const Metasound::FGeneratorInstanceInfo& Info)
			{
				if (Info.AudioComponentID == ComponentId)
				{
					FShiftAudioStats::Get().SelectedInstance = Info.InstanceID;
				}
			});
	}
	FShiftAudioStats::Get().bEnabled = !bCpuMode;
	bAccumulating = false;
	if (bCpuMode)
	{
		CpuHandle = UMetasoundGeneratorHandle::CreateMetaSoundGeneratorHandle(Component);
		if (CpuHandle)
		{
			CpuHandle->EnableRuntimeRenderTiming(true);
		}
	}
	LastStepChanges = 0;
	if (C.bClock)
	{
		Clock = UMusicClockComponent::CreateMetasoundDrivenMusicClock(W, Component, TEXT("MIDI Clock"), true, false);
		if (Clock)
		{
			Clock->BeatEvent.AddDynamic(this, &UShiftAudioMeter::HandleBeat);
		}
		else
		{
			UE_LOG(LogShift, Warning, TEXT("AudioMeter: %s: CreateMetasoundDrivenMusicClock returned null"), *C.Label);
		}
	}
	Component->Play();
}

void UShiftAudioMeter::PollStats()
{
	if (CaseIndex < 0 || CaseIndex >= Cases.Num() || !Component || DropStarts.Contains(StartCount))
	{
		return;
	}
	if (bCpuMode)
	{
		const double Use = CpuHandle ? CpuHandle->GetCPUCoreUtilization() : 0.0;
		if (Use > 0.0)
		{
			++OutputUpdates;
			if (CaseClock >= WarmUp)
			{
				Cpu.Max = FMath::Max(Cpu.Max, static_cast<float>(Use));
				Cpu.Sum += Use;
				++Cpu.Count;
			}
		}
		return;
	}
	FShiftAudioStats& Stats = FShiftAudioStats::Get();
	OutputUpdates = Stats.Master.BlocksTotal;
	if (!bAccumulating && CaseClock >= WarmUp)
	{
		Stats.Master.ResetAccumulators();   // the first second of a looping case is not measured
		bAccumulating = true;
	}
	if (bAccumulating)
	{
		const int32 Blocks = Stats.Master.Blocks;
		Peak.Max = Stats.Master.PeakMax;
		Rms.Max = Stats.Master.RmsMax;
		Rms.Sum = Stats.Master.RmsSum;
		Rms.Count = Blocks;
		Low.Max = Stats.Master.LowMax;
		Low.Sum = Stats.Master.LowSum;
		Low.Count = Blocks;
		Mid.Max = Stats.Master.MidMax;
		Mid.Sum = Stats.Master.MidSum;
		Mid.Count = Blocks;
		High.Max = Stats.Master.HighMax;
		High.Sum = Stats.Master.HighSum;
		High.Count = Blocks;
	}
	const int32 StepChanges = Stats.StepChanges;
	if (StepChanges != LastStepChanges)
	{
		LastStepChanges = StepChanges;
		const int32 Value = Stats.Step;
		if (Value != LastStep)
		{
			StepWraps += Value < LastStep ? 1 : 0;
			MaxStep = FMath::Max(MaxStep, Value);
			LastStep = Value;
			StepTimes.Add(FPlatformTime::Seconds());
			StepValues.Add(Value);
		}
	}
}

void UShiftAudioMeter::HandleFinished()
{
	bCaseFinished = true;
	FinishedAt = CaseClock;
}

void UShiftAudioMeter::HandleBeat(int32 BeatNumber, int32 BeatInBar)
{
	BeatTimes.Add(FPlatformTime::Seconds());
}

bool UShiftAudioMeter::Tick(float DeltaTime)
{
	if (bDone)
	{
		return false;
	}
	if (!bStarted)
	{
		bStarted = true;
		WarmStartWall = FPlatformTime::Seconds();
		StartCase();
		return true;
	}
	CaseClock += DeltaTime;
	PollStats();
	const FCase& C = Cases[CaseIndex];
	if (C.SwitchAt >= 0.0f && !bSwitched && CaseClock >= C.SwitchAt && Component)
	{
		bSwitched = true;
		Component->SetFloatParameter(TEXT("DayGain"), C.SwitchDay);
		Component->SetFloatParameter(TEXT("NightGain"), C.SwitchNight);
		Component->SetFloatParameter(TEXT("TensionGain"), C.SwitchTension);
	}
	const bool bTimeUp = CaseClock >= C.Seconds;
	const bool bOneShotDone = !C.bLooping && bCaseFinished && CaseClock >= FinishedAt + 0.3f;
	if (bTimeUp || bOneShotDone || (!Component && bCaseFinished))
	{
		// A one-shot that ended without a single meter update lost the race against the game thread (see Begin). That is not a verdict on the sound: play it again.
		// The warm-up repeats until data arrives or its wall-clock budget is gone; a real case gets MaxCaseAttempts tries. Looping sources are not retried.
		if (Component && !C.bLooping && OutputUpdates == 0)
		{
			if (C.bWarmUp)
			{
				const double WarmWall = FPlatformTime::Seconds() - WarmStartWall;
				if (WarmWall < WarmUpMaxSeconds)
				{
					++Attempt;
					ReleaseComponents();
					StartCase();
					return true;
				}
				UE_LOG(LogShift, Error, TEXT("AudioMeter: [FAIL] warm-up: no meter data after %.0f s and %d attempt(s) (no audio device? run with -Sound)"), WarmWall, Attempt);
				++Failures;
				ReleaseComponents();
				LogSummary();
				bDone = true;
				return false;
			}
			if (Attempt < MaxCaseAttempts)
			{
				UE_LOG(LogShift, Log, TEXT("AudioMeter: %s: no meter data, retry %d/%d"), *C.Label, Attempt, MaxCaseAttempts - 1);
				++Attempt;
				ReleaseComponents();
				StartCase();
				return true;
			}
		}
		FinishCase();
		++CaseIndex;
		Attempt = 1;
		if (CaseIndex >= Cases.Num())
		{
			LogSummary();
			bDone = true;
			return false;
		}
		StartCase();
	}
	return true;
}

void UShiftAudioMeter::CheckMusicClock(const FCase& C)
{
	// The Harmonix 16th-note clock must run steadily across the loop wrap: 128 steps per 8 bars, the counter wraps 128 -> 1.
	const int32 ExpectedWraps = FMath::FloorToInt(C.Seconds / LoopSeconds);
	if (StepTimes.Num() >= 8)
	{
		const int32 TotalSteps = StepWraps * 128 + (StepValues.Last() - StepValues[0]);
		const double Mean = TotalSteps > 0 ? (StepTimes.Last() - StepTimes[0]) / TotalSteps : 0.0;
		const bool bOk = StepWraps == ExpectedWraps && MaxStep == 128 && FMath::Abs(Mean - StepSeconds) < 0.0015;
		UE_LOG(LogShift, Log, TEXT("AudioMeter:   music clock: %d step updates, %d wrap(s) (expected %d), max step %d, %.2f ms per step (expected %.2f) [%s]"),
			StepValues.Num(), StepWraps, ExpectedWraps, MaxStep, Mean * 1000.0, StepSeconds * 1000.0, bOk ? TEXT("PASS") : TEXT("FAIL"));
		Failures += bOk ? 0 : 1;
	}
	else
	{
		UE_LOG(LogShift, Error, TEXT("AudioMeter:   [FAIL] music clock: only %d step updates"), StepTimes.Num());
		++Failures;
	}
	if (C.bClock)
	{
		if (BeatTimes.Num() >= 8)
		{
			const double Mean = (BeatTimes.Last() - BeatTimes[0]) / (BeatTimes.Num() - 1);
			const bool bOk = FMath::Abs(Mean - MeterBeatSeconds) < 0.004;
			UE_LOG(LogShift, Log, TEXT("AudioMeter:   Harmonix clock component: %d beat events, %.2f ms apart (expected %.2f) [%s]"), BeatTimes.Num(), Mean * 1000.0,
				MeterBeatSeconds * 1000.0, bOk ? TEXT("PASS") : TEXT("FAIL"));
			Failures += bOk ? 0 : 1;
		}
		else
		{
			UE_LOG(LogShift, Error, TEXT("AudioMeter:   [FAIL] only %d beat events from the Harmonix clock component"), BeatTimes.Num());
			++Failures;
		}
	}
}

void UShiftAudioMeter::CheckMusicLanes()
{
	// Every pattern lane must fire its pattern (with one loop's worth of slack for the start-up latency) and every voice must be audible.
	LaneHits.Reset();
	VoiceLevels.Reset();
	int32 TailVoices = 0;
	int32 TailBad = 0;
	FString TailText;
	{
		FShiftAudioStats& Stats = FShiftAudioStats::Get();
		for (int32 i = 0; i < Stats.NumTaps(); ++i)
		{
			if (const FShiftAudioStats::FTap* Tap = Stats.TapAt(i))
			{
				if (Tap->bTail)
				{
					// a gated voice made sound after the window the music gives it: -80 dBFS is the most that may be left
					++TailVoices;
					if (Tap->Level > 1.0e-4f)
					{
						++TailBad;
						TailText += FString::Printf(TEXT(" %s(%.1f dB outside its window)"), *Tap->Name.ToString(), 20.0f * FMath::LogX(10.0f, Tap->Level.load()));
					}
					continue;
				}
				if (Tap->bCounter)
				{
					FLaneHits Lane;
					Lane.Output = Tap->Name;
					Lane.Lane = Tap->Name.ToString();
					Lane.Expected = Tap->Expected;
					Lane.Count = Tap->Count;
					LaneHits.Add(Lane);
				}
				else
				{
					VoiceLevels.Add(Tap->Name, Tap->Level);
				}
			}
		}
	}
	int32 BadLanes = 0;
	FString Bad;
	for (const FLaneHits& Lane : LaneHits)
	{
		const int32 Min = FMath::FloorToInt((CaseClock - 1.5f) / LoopSeconds) * Lane.Expected;
		const int32 Max = FMath::CeilToInt(CaseClock / LoopSeconds) * Lane.Expected;
		if (Lane.Count < Min || Lane.Count > Max || Lane.Count == 0)
		{
			++BadLanes;
			Bad += FString::Printf(TEXT(" %s(%d hits, expected %d..%d)"), *Lane.Lane, Lane.Count, Min, Max);
		}
	}
	int32 Silent = 0;
	for (const TPair<FName, float>& Voice : VoiceLevels)
	{
		if (Voice.Value < 0.001f)
		{
			++Silent;
			Bad += FString::Printf(TEXT(" %s(silent, peak %.4f)"), *Voice.Key.ToString(), Voice.Value);
		}
	}
	const bool bOk = BadLanes == 0 && Silent == 0 && LaneHits.Num() > 0 && TailBad == 0 && TailVoices > 0;
	UE_LOG(LogShift, Log, TEXT("AudioMeter:   music lanes: %d of %d lanes fire their patterns, %d of %d voices audible, %d of %d gated voices ring out inside their windows [%s]%s%s"), LaneHits.Num() - BadLanes, LaneHits.Num(),
		VoiceLevels.Num() - Silent, VoiceLevels.Num(), TailVoices - TailBad, TailVoices, bOk ? TEXT("PASS") : TEXT("FAIL"), *Bad, *TailText);
	Failures += bOk ? 0 : 1;
}

void UShiftAudioMeter::CheckWake()
{
	// the night layer was not rendered at all until its gain came up: from then on its kick has to fire and sound, so a layer wakes up cleanly
	FShiftAudioStats& Stats = FShiftAudioStats::Get();
	int32 Hits = 0;
	float Level = 0.0f;
	for (int32 i = 0; i < Stats.NumTaps(); ++i)
	{
		if (const FShiftAudioStats::FTap* Tap = Stats.TapAt(i))
		{
			if (Tap->bCounter && Tap->Name == FName(TEXT("NightKick")))
			{
				Hits = Tap->Count;
			}
			else if (!Tap->bCounter && Tap->Name == FName(TEXT("Lvl_NightKick")))
			{
				Level = Tap->Level;
			}
		}
	}
	const bool bOk = Hits >= 10 && Level > 0.05f;
	UE_LOG(LogShift, Log, TEXT("AudioMeter: [%s] a skipped layer wakes up: the night kick fired %d times and peaked at %.1f dB after the gain came up"), bOk ? TEXT("PASS") : TEXT("FAIL"), Hits, ToDb(Level));
	Failures += bOk ? 0 : 1;
}

void UShiftAudioMeter::FinishCase()
{
	const FCase& C = Cases[CaseIndex];
	if (C.bWarmUp)
	{
		ReleaseComponents();
		UE_LOG(LogShift, Log, TEXT("AudioMeter: warm-up done (%d meter updates, %d attempt(s), %.2f s)"), OutputUpdates, Attempt, FPlatformTime::Seconds() - WarmStartWall);
		return;
	}

	PollStats();
	if (bCpuMode)
	{
		UE_LOG(LogShift, Log, TEXT("AudioMeter: [CPU] %-22s mean %5.2f%%  max %5.2f%% of one core  (%d samples)%s"), *C.Label, Cpu.Mean() * 100.0f, Cpu.Max * 100.0f, Cpu.Count,
			Cpu.Count == 0 ? TEXT("  <- no timing data") : TEXT(""));
		Failures += Cpu.Count == 0 ? 1 : 0;
		ReleaseComponents();
		return;
	}
	FResult R;
	R.Label = C.Label;
	R.PeakMax = Peak.Max;
	R.RmsMax = Rms.Max;
	R.RmsMean = Rms.Mean();
	R.Low = C.bLooping ? Low.Mean() : Low.Max;
	R.Mid = C.bLooping ? Mid.Mean() : Mid.Max;
	R.High = C.bLooping ? High.Mean() : High.Max;
	R.Duration = CaseClock;
	R.bFinished = bCaseFinished;
	R.bLooping = C.bLooping;

	FString Why;
	if (OutputUpdates == 0)
	{
		Why = TEXT("no meter data (audio device missing, or the composite node has no meter tap)");
	}
	else if (!C.bLooping && !bCaseFinished)
	{
		Why = TEXT("one-shot did not finish");
	}
	else   // every case, the stacked layers too: nothing limits the mix behind this tap
	{
		if (R.PeakMax >= PeakCeiling)
		{
			Why = FString::Printf(TEXT("peak %.1f dB is not below -1 dBFS"), ToDb(R.PeakMax));
		}
		else if (R.PeakMax < PeakFloor)
		{
			Why = FString::Printf(TEXT("peak %.1f dB is inaudible"), ToDb(R.PeakMax));
		}
		else if (C.bLooping && R.RmsMean < 0.02f)
		{
			Why = FString::Printf(TEXT("mean rms %.1f dB is too quiet"), ToDb(R.RmsMean));
		}
	}
	if (!Why.IsEmpty())
	{
		++Failures;
	}
	UE_LOG(LogShift, Log, TEXT("AudioMeter: [%s] %-22s %5.2fs  peak %6.1f dB  rms max %6.1f dB mean %6.1f dB  low %.3f mid %.3f high %.3f (%s)%s%s"),
		Why.IsEmpty() ? TEXT("PASS") : TEXT("FAIL"), *R.Label, R.Duration, ToDb(R.PeakMax), ToDb(R.RmsMax), ToDb(R.RmsMean), R.Low, R.Mid, R.High,
		DominantBand(R.Low, R.Mid, R.High), Why.IsEmpty() ? TEXT("") : TEXT("  <- "), *Why);

	if (C.Asset == TEXT("MS_Music") && C.Seconds >= 30.0f)
	{
		CheckMusicClock(C);
		CheckMusicLanes();
	}
	if (C.SwitchAt >= 0.0f)
	{
		CheckWake();
	}
	Results.Add(R);
	ReleaseComponents();
}

void UShiftAudioMeter::LogSummary()
{
	auto Find = [this](const TCHAR* Label) -> const FResult*
		{
			for (const FResult& R : Results)
			{
				if (R.Label == Label)
				{
					return &R;
				}
			}
			return nullptr;
		};
	auto Share = [](float Part, const FResult& R) { return Part / FMath::Max(R.Low + R.Mid + R.High, 1.0e-6f); };
	auto Check = [this](bool bOk, const FString& Text)
		{
			UE_LOG(LogShift, Log, TEXT("AudioMeter: [%s] %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *Text);
			Failures += bOk ? 0 : 1;
		};

	const FResult* Day = Find(TEXT("music day"));
	const FResult* Night = Find(TEXT("music night"));
	const FResult* DayT = Find(TEXT("music day+tension"));
	if (Day && Night)
	{
		Check(Share(Night->Low, *Night) > Share(Day->Low, *Day) + 0.02f,
			FString::Printf(TEXT("night shifts the spectrum down (low share %.0f%% vs day %.0f%%)"), Share(Night->Low, *Night) * 100.0f, Share(Day->Low, *Day) * 100.0f));
		const float DiffDb = FMath::Abs(ToDb(Night->RmsMean) - ToDb(Day->RmsMean));
		Check(DiffDb < 4.0f, FString::Printf(TEXT("day and night are about equally loud (%.1f dB apart)"), DiffDb));
	}
	if (Day && DayT)
	{
		Check(Share(DayT->High, *DayT) > Share(Day->High, *Day) + 0.02f,
			FString::Printf(TEXT("tension adds high-frequency energy (high share %.0f%% vs %.0f%%)"), Share(DayT->High, *DayT) * 100.0f, Share(Day->High, *Day) * 100.0f));
	}
	UE_LOG(LogShift, Log, TEXT("AudioMeter: done, %d case(s), %d failure(s)"), Results.Num(), Failures);
}
