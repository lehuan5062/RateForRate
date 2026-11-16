// Shift Happens - Game Off 2026

#include "Audio/ShiftAudioMeter.h"
#include "Audio/ShiftAudio.h"
#include "ShiftHappens.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "HarmonixMetasound/Components/MusicClockComponent.h"
#include "HarmonixMetasound/MusicClock/MusicClockState.h"
#include "Kismet/GameplayStatics.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundOutput.h"
#include "MetasoundOutputSubsystem.h"
#include "Sound/SoundBase.h"

namespace
{
	constexpr float PeakCeiling = 0.891f;     // -1 dBFS
	constexpr float PeakFloor = 0.02f;        // about -34 dBFS: anything quieter is inaudible
	constexpr float StepSeconds = 60.0f / 112.5f / 4.0f;
	constexpr float BeatSeconds = 60.0f / 112.5f;
	constexpr float LoopSeconds = 128.0f * StepSeconds;
	constexpr TCHAR WarmUpLabel[] = TEXT("(warm-up)");

	float ToDb(float V) { return 20.0f * FMath::LogX(10.0f, FMath::Max(V, 1.0e-6f)); }

	const TCHAR* DominantBand(float Low, float Mid, float High)
	{
		return (Low >= Mid && Low >= High) ? TEXT("low") : ((Mid >= High) ? TEXT("mid") : TEXT("high"));
	}
}

void UShiftAudioMeter::Begin(UWorld* InWorld, const FString& Spec)
{
	World = InWorld;
	BuildCases(Spec);
	UE_LOG(LogShift, Log, TEXT("AudioMeter: %d cases (spec '%s')"), Cases.Num(), *Spec);
	CaseIndex = 0;
	if (Cases.Num() == 0)
	{
		bDone = true;
		return;
	}
	// The first MetaSound generator of a process takes a few hundred milliseconds to come up (node registry, graph build), long enough for a
	// short one-shot to be over before its meters report. A throw-away copy of the first case absorbs that.
	FCase WarmCase = Cases[0];
	WarmCase.Label = WarmUpLabel;
	WarmCase.bClock = false;
	WarmCase.Seconds = FMath::Min(WarmCase.Seconds, 2.0f);
	Cases.Insert(WarmCase, 0);
	StartCase();
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
			{ TEXT("music day x2 loops"), 1.0f, 0.0f, 0.0f, 36.0f, true },
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
	Peak = Rms = Low = Mid = High = FStat();
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
	if (C.Asset == TEXT("MS_Music"))
	{
		Component->SetFloatParameter(TEXT("DayGain"), C.Day);
		Component->SetFloatParameter(TEXT("NightGain"), C.Night);
		Component->SetFloatParameter(TEXT("TensionGain"), C.Tension);
		Component->SetFloatParameter(TEXT("Volume"), 1.0f);
	}
	Component->OnAudioFinished.AddDynamic(this, &UShiftAudioMeter::HandleFinished);

	TArray<FName> Names = { TEXT("Peak"), TEXT("Rms"), TEXT("Low"), TEXT("Mid"), TEXT("High") };
	if (C.Asset == TEXT("MS_Music"))
	{
		Names.Add(TEXT("StepCount"));
		if (C.Seconds >= 30.0f)
		{
			// Find the lane taps in the source's own interface: Hits_<lane>_<expected hits per loop> and Lvl_<voice>.
			if (const IMetaSoundDocumentInterface* Doc = Cast<IMetaSoundDocumentInterface>(Sound))
			{
				for (const FMetasoundFrontendClassOutput& Out : Doc->GetConstDocument().RootGraph.GetDefaultInterface().Outputs)
				{
					const FString Name = Out.Name.ToString();
					if (Name.StartsWith(TEXT("Hits_")))
					{
						int32 Underscore = INDEX_NONE;
						Name.FindLastChar(TEXT('_'), Underscore);
						FLaneHits Lane;
						Lane.Output = Out.Name;
						Lane.Lane = Name.Mid(5, Underscore - 5);
						Lane.Expected = FCString::Atoi(*Name.Mid(Underscore + 1));
						LaneHits.Add(Lane);
						Names.Add(Out.Name);
					}
					else if (Name.StartsWith(TEXT("Lvl_")))
					{
						VoiceLevels.Add(Out.Name, 0.0f);
						Names.Add(Out.Name);
					}
				}
			}
		}
	}
	if (UMetaSoundOutputSubsystem* Sub = W->GetSubsystem<UMetaSoundOutputSubsystem>())
	{
		for (const FName& Name : Names)
		{
			Sub->WatchOutput(Component, Name, FOnMetasoundOutputValueChangedNative::CreateUObject(this, &UShiftAudioMeter::OnOutput));
		}
	}
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

void UShiftAudioMeter::OnOutput(FName OutputName, const FMetaSoundOutput& Output)
{
	if (CaseIndex < 0 || CaseIndex >= Cases.Num())
	{
		return;
	}
	if (VoiceLevels.Num() > 0)
	{
		if (float* Level = VoiceLevels.Find(OutputName))
		{
			float Value = 0.0f;
			if (Output.Get<float>(Value))
			{
				*Level = FMath::Max(*Level, Value);
			}
			return;
		}
	}
	for (FLaneHits& Lane : LaneHits)
	{
		if (Lane.Output == OutputName)
		{
			int32 Value = 0;
			if (Output.Get<int32>(Value))
			{
				Lane.Count = FMath::Max(Lane.Count, Value);
			}
			return;
		}
	}
	if (OutputName == TEXT("StepCount"))
	{
		int32 Value = 0;
		if (Output.Get<int32>(Value) && Value != LastStep)
		{
			StepWraps += Value < LastStep ? 1 : 0;
			MaxStep = FMath::Max(MaxStep, Value);
			LastStep = Value;
			StepTimes.Add(FPlatformTime::Seconds());
			StepValues.Add(Value);
		}
		return;
	}
	float Value = 0.0f;
	if (!Output.Get<float>(Value))
	{
		return;
	}
	++OutputUpdates;
	if (CaseClock < WarmUp)
	{
		return;
	}
	FStat* Target = nullptr;
	if (OutputName == TEXT("Peak")) { Target = &Peak; }
	else if (OutputName == TEXT("Rms")) { Target = &Rms; }
	else if (OutputName == TEXT("Low")) { Target = &Low; }
	else if (OutputName == TEXT("Mid")) { Target = &Mid; }
	else if (OutputName == TEXT("High")) { Target = &High; }
	if (Target)
	{
		Target->Max = FMath::Max(Target->Max, Value);
		Target->Sum += Value;
		++Target->Count;
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
	CaseClock += DeltaTime;
	const FCase& C = Cases[CaseIndex];
	const bool bTimeUp = CaseClock >= C.Seconds;
	const bool bOneShotDone = !C.bLooping && bCaseFinished && CaseClock >= FinishedAt + 0.3f;
	if (bTimeUp || bOneShotDone || (!Component && bCaseFinished))
	{
		FinishCase();
		++CaseIndex;
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
			const bool bOk = FMath::Abs(Mean - BeatSeconds) < 0.004;
			UE_LOG(LogShift, Log, TEXT("AudioMeter:   Harmonix clock component: %d beat events, %.2f ms apart (expected %.2f) [%s]"), BeatTimes.Num(), Mean * 1000.0,
				BeatSeconds * 1000.0, bOk ? TEXT("PASS") : TEXT("FAIL"));
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
	const bool bOk = BadLanes == 0 && Silent == 0 && LaneHits.Num() > 0;
	UE_LOG(LogShift, Log, TEXT("AudioMeter:   music lanes: %d of %d lanes fire their patterns, %d of %d voices audible [%s]%s"), LaneHits.Num() - BadLanes, LaneHits.Num(),
		VoiceLevels.Num() - Silent, VoiceLevels.Num(), bOk ? TEXT("PASS") : TEXT("FAIL"), *Bad);
	Failures += bOk ? 0 : 1;
}

void UShiftAudioMeter::FinishCase()
{
	const FCase& C = Cases[CaseIndex];
	if (C.Label == WarmUpLabel)
	{
		ReleaseComponents();
		UE_LOG(LogShift, Log, TEXT("AudioMeter: warm-up done (%d meter updates)"), OutputUpdates);
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
		Why = TEXT("no meter data (audio device missing, or the graph has no meter outputs)");
	}
	else if (!C.bLooping && !bCaseFinished)
	{
		Why = TEXT("one-shot did not finish");
	}
	else if (!C.Label.Contains(TEXT("+tension")))   // the stacked layers are limited on purpose; only the stems are held to -1 dBFS
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
