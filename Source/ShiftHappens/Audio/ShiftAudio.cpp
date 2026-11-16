// Shift Happens - Game Off 2026

#include "Audio/ShiftAudio.h"
#include "ShiftHappens.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace
{
	constexpr double BeatSeconds = 60.0 / 112.5;   // the stems are 112.5 BPM

	const TCHAR* SfxName(EShiftSfx Sfx)
	{
		switch (Sfx)
		{
		case EShiftSfx::Click: return TEXT("Click");
		case EShiftSfx::Hover: return TEXT("Hover");
		case EShiftSfx::Slider: return TEXT("Slider");
		case EShiftSfx::Upload: return TEXT("Upload");
		case EShiftSfx::Transition: return TEXT("Transition");
		case EShiftSfx::Tip: return TEXT("Tip");
		case EShiftSfx::Plug: return TEXT("Plug");
		case EShiftSfx::Flip: return TEXT("Flip");
		case EShiftSfx::Reinforce: return TEXT("Reinforce");
		case EShiftSfx::Weaken: return TEXT("Weaken");
		case EShiftSfx::Gated: return TEXT("Gated");
		case EShiftSfx::Locked: return TEXT("Locked");
		case EShiftSfx::NoPosts: return TEXT("NoPosts");
		case EShiftSfx::NightOn: return TEXT("NightOn");
		case EShiftSfx::NightOff: return TEXT("NightOff");
		case EShiftSfx::Sock: return TEXT("Sock");
		case EShiftSfx::Heartbeat: return TEXT("Heartbeat");
		case EShiftSfx::ModWarn: return TEXT("ModWarn");
		case EShiftSfx::ModSweep: return TEXT("ModSweep");
		case EShiftSfx::Strike: return TEXT("Strike");
		case EShiftSfx::Shadowban: return TEXT("Shadowban");
		case EShiftSfx::Blame: return TEXT("Blame");
		case EShiftSfx::Day: return TEXT("Day");
		case EShiftSfx::Win: return TEXT("Win");
		case EShiftSfx::Lose: return TEXT("Lose");
		case EShiftSfx::DQ: return TEXT("DQ");
		default: return TEXT("");
		}
	}

	USoundBase* LoadSound(UObject* Outer, const FString& AssetName)
	{
		const FString Path = FString::Printf(TEXT("/Game/Shift/Audio/%s.%s"), *AssetName, *AssetName);
		return LoadObject<USoundBase>(Outer, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
}

void UShiftAudio::Init(UWorld* InWorld)
{
	World = InWorld;
	Sfx.SetNum(static_cast<int32>(EShiftSfx::Count));
	LastPlayed.SetNumZeroed(static_cast<int32>(EShiftSfx::Count));
	LoadedSfx = 0;
	for (int32 i = 0; i < static_cast<int32>(EShiftSfx::Count); ++i)
	{
		Sfx[i] = LoadSound(this, FString(TEXT("MS_Sfx_")) + SfxName(static_cast<EShiftSfx>(i)));
		LoadedSfx += Sfx[i] ? 1 : 0;
	}

	MusicSound = LoadSound(this, TEXT("MS_Music"));
	if (MusicSound && InWorld)
	{
		// bPersistAcrossLevelTransition, bAutoDestroy = false: one component for the whole session.
		Music = UGameplayStatics::SpawnSound2D(InWorld, MusicSound, 1.0f, 1.0f, 0.0f, nullptr, true, false);
		if (Music)
		{
			Music->bIsUISound = true;
			Music->SetFloatParameter(TEXT("DayGain"), DayGain);
			Music->SetFloatParameter(TEXT("NightGain"), NightGain);
			Music->SetFloatParameter(TEXT("TensionGain"), TensionGain);
			Music->SetFloatParameter(TEXT("Volume"), MasterVolume);
			MusicStart = FPlatformTime::Seconds();
		}
	}
	UE_LOG(LogShift, Log, TEXT("Audio: %d of %d sfx loaded, music %s"), LoadedSfx, static_cast<int32>(EShiftSfx::Count), Music ? TEXT("playing") : TEXT("unavailable"));
}

void UShiftAudio::Shutdown()
{
	if (Music)
	{
		Music->Stop();
		Music = nullptr;
	}
}

void UShiftAudio::Tick(float DeltaTime, float Night, float Intensity, float Volume)
{
	if (!Music)
	{
		return;
	}
	const float TargetDay = 1.0f - FMath::Clamp(Night, 0.0f, 1.0f);
	const float TargetNight = FMath::Clamp(Night, 0.0f, 1.0f);
	DayGain = FMath::FInterpTo(DayGain, TargetDay, DeltaTime, 6.0f);
	NightGain = FMath::FInterpTo(NightGain, TargetNight, DeltaTime, 6.0f);
	TensionGain = FMath::FInterpTo(TensionGain, FMath::Clamp(Intensity, 0.0f, 1.0f) * 1.4f, DeltaTime, 2.5f);
	MasterVolume = FMath::FInterpTo(MasterVolume, FMath::Clamp(Volume, 0.0f, 1.5f), DeltaTime, 2.5f);
	Music->SetFloatParameter(TEXT("DayGain"), DayGain);
	Music->SetFloatParameter(TEXT("NightGain"), NightGain);
	Music->SetFloatParameter(TEXT("TensionGain"), TensionGain);
	Music->SetFloatParameter(TEXT("Volume"), MasterVolume);
}

void UShiftAudio::Play(EShiftSfx Which, float Volume)
{
	const int32 Index = static_cast<int32>(Which);
	UWorld* W = World.Get();
	if (!W || !Sfx.IsValidIndex(Index) || !Sfx[Index])
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - LastPlayed[Index] < 0.03)
	{
		return;   // the same effect twice within 30 ms would only double the level
	}
	LastPlayed[Index] = Now;
	UGameplayStatics::PlaySound2D(W, Sfx[Index], Volume, 1.0f, 0.0f, nullptr, nullptr, /*bIsUISound*/ true);
}

float UShiftAudio::BeatPhase() const
{
	if (!Music)
	{
		return 0.0f;
	}
	const double Elapsed = FPlatformTime::Seconds() - MusicStart;
	return static_cast<float>(FMath::Fmod(Elapsed, BeatSeconds) / BeatSeconds);
}

float UShiftAudio::BeatPulse() const
{
	return Music ? FMath::Exp(-BeatPhase() * 7.0f) : 0.0f;
}

bool UShiftAudio::IsMusicPlaying() const
{
	return Music && Music->IsPlaying();
}

void UShiftAudio::PlayRawWave(const FString& WaveAssetName)
{
	UWorld* W = World.Get();
	const FString Path = FString::Printf(TEXT("/Game/Shift/Audio/Waves/%s.%s"), *WaveAssetName, *WaveAssetName);
	if (USoundBase* Wave = LoadObject<USoundBase>(this, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UE_LOG(LogShift, Log, TEXT("QA raw wave %s"), *Path);
		UGameplayStatics::PlaySound2D(W, Wave, 1.0f, 1.0f, 0.0f, nullptr, nullptr, true);
	}
}
