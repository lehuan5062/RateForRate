// Rate for Rate - Game Off 2026

#include "Audio/ShiftAudio.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Containers/UnrealString.h"
#include "Delegates/Delegate.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/Platform.h"
#include "HAL/PlatformTime.h"
#include "HarmonixMetasound/Components/MusicClockComponent.h"
#include "HarmonixMetasound/MusicClock/MusicClockState.h"
#include "Kismet/GameplayStatics.h"
#include "Logging/LogMacros.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/App.h"
#include "ShiftHappens.h"
#include "ShiftMixTap.h"
#include "Sound/SoundBase.h"
#include "Templates/SharedPointer.h"
#include "Templates/SharedPointerFwd.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	constexpr double BeatSeconds = 60.0 / 112.5;   // MS_Music runs at 112.5 BPM

	USoundBase* LoadSound(UObject* Outer, const FString& AssetName)
	{
		const FString Path = FString::Printf(TEXT("/Game/Shift/Audio/%s.%s"), *AssetName, *AssetName);
		return LoadObject<USoundBase>(Outer, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
}

const TCHAR* ShiftSfxName(EShiftSfx Sfx)
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

void UShiftAudio::Init(UWorld* InWorld)
{
	World = InWorld;
	Sfx.SetNum(static_cast<int32>(EShiftSfx::Count));
	LastPlayed.SetNumZeroed(static_cast<int32>(EShiftSfx::Count));
	LoadedSfx = 0;
	for (int32 i = 0; i < static_cast<int32>(EShiftSfx::Count); ++i)
	{
		Sfx[i] = LoadSound(this, FString(TEXT("MS_Sfx_")) + ShiftSfxName(static_cast<EShiftSfx>(i)));
		LoadedSfx += Sfx[i] ? 1 : 0;
	}

	MusicSound = LoadSound(this, TEXT("MS_Music"));
	StartMusic();
	UE_LOG(LogShift, Log, TEXT("Audio: %d of %d sfx loaded, music %s, Harmonix clock %s"), LoadedSfx, static_cast<int32>(EShiftSfx::Count),
		Music ? TEXT("playing") : TEXT("unavailable"), MusicClock ? TEXT("bound") : TEXT("not bound"));
}

void UShiftAudio::StartMusic()
{
	UWorld* InWorld = World.Get();
	if (!MusicSound || !InWorld)
	{
		return;
	}
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
		// The Harmonix Metronome inside MS_Music exposes its MIDI clock as a graph output; the clock component tracks it for the beat pulse.
		MusicClock = UMusicClockComponent::CreateMetasoundDrivenMusicClock(InWorld, Music, TEXT("MIDI Clock"), true, true);
		if (MusicClock)
		{
			MusicClock->BeatEvent.AddDynamic(this, &UShiftAudio::OnMusicBeat);
		}
	}
}

void UShiftAudio::HoldMusic()
{
	if (MusicClock)
	{
		MusicClock->BeatEvent.RemoveAll(this);
		MusicClock->Stop();
		MusicClock->DestroyComponent();
		MusicClock = nullptr;
	}
	if (Music)
	{
		Music->Stop();
		Music->DestroyComponent();
		Music = nullptr;
	}
	UE_LOG(LogShift, Log, TEXT("Audio: music held"));
}

void UShiftAudio::RestartMusic()
{
	HoldMusic();
	// bar 0 again: day layer only, no tension, full volume at once (a fade-in would swallow the first hit)
	BeatEvents = 0;
	DayGain = 1.0f;
	NightGain = 0.0f;
	TensionGain = 0.0f;
	MasterVolume = bMusicMuted ? 0.0f : 1.0f;
	StartMusic();
	UE_LOG(LogShift, Log, TEXT("Audio: music restarted (%s, Harmonix clock %s)"), Music ? TEXT("playing") : TEXT("unavailable"), MusicClock ? TEXT("bound") : TEXT("not bound"));
}

bool UShiftAudio::MusicClockRunning() const
{
	return MusicClock && MusicClock->GetState() == EMusicClockState::Running;
}

void UShiftAudio::OnMusicBeat(int32 BeatNumber, int32 BeatInBar)
{
	++BeatEvents;
}

void UShiftAudio::BeginMixTap()
{
	FAudioDevice* Device = GEngine ? GEngine->GetMainAudioDeviceRaw() : nullptr;
	if (!Device)
	{
		UE_LOG(LogShift, Warning, TEXT("MixTap: no audio device, nothing to measure"));
		return;
	}
	MixTap = MakeShared<FShiftMixTap, ESPMode::ThreadSafe>();
	Device->RegisterSubmixBufferListener(MixTap.ToSharedRef(), Device->GetMainSubmixObject());
	MixTapStart = MixTapLastLog = FPlatformTime::Seconds();
	UE_LOG(LogShift, Log, TEXT("MixTap: listening to the engine's main submix (this game's own output only)"));
}

void UShiftAudio::Shutdown()
{
	if (MixTap)
	{
		if (FAudioDevice* Device = GEngine ? GEngine->GetMainAudioDeviceRaw() : nullptr)
		{
			Device->UnregisterSubmixBufferListener(MixTap.ToSharedRef(), Device->GetMainSubmixObject());
		}
		MixTap.Reset();
	}
	UE_LOG(LogShift, Log, TEXT("Audio: Harmonix clock delivered %d beat events"), BeatEvents);
	if (MusicClock)
	{
		MusicClock->BeatEvent.RemoveAll(this);
		MusicClock->Stop();
		MusicClock->DestroyComponent();
		MusicClock = nullptr;
	}
	if (Music)
	{
		Music->Stop();
		Music = nullptr;
	}
}

void UShiftAudio::Tick(float DeltaTime, float Night, float Intensity, float Volume)
{
	if (MixTap)
	{
		// A headless run never has window focus, and the engine silences an unfocused app (FApp unfocused multiplier, 0 by default): measure the real mix instead.
		FApp::SetVolumeMultiplier(1.0f);
		const double Now = FPlatformTime::Seconds();
		if (Now - MixTapLastLog >= 0.25)
		{
			float TapPeak = 0.0f;
			float TapRms = 0.0f;
			int32 TapBuffers = 0;
			MixTap->Take(TapPeak, TapRms, TapBuffers);
			FAudioDevice* TapDevice = GEngine ? GEngine->GetMainAudioDeviceRaw() : nullptr;
			UE_LOG(LogShift, Log, TEXT("MixTap t=%.2f muted=%d musicVolume=%.3f playing=%d devicePrimary=%.3f appVolume=%.3f peak=%.6f rms=%.6f buffers=%d"), Now - MixTapStart,
				(bEffectsMuted && bMusicMuted) ? 1 : 0, MasterVolume, IsMusicPlaying() ? 1 : 0, TapDevice ? TapDevice->GetPrimaryVolume() : -1.0f, FApp::GetVolumeMultiplier(), TapPeak, TapRms, TapBuffers);
			MixTapLastLog = Now;
		}
	}
	if (!Music)
	{
		return;
	}
	const float TargetDay = 1.0f - FMath::Clamp(Night, 0.0f, 1.0f);
	const float TargetNight = FMath::Clamp(Night, 0.0f, 1.0f);
	DayGain = FMath::FInterpTo(DayGain, TargetDay, DeltaTime, 6.0f);
	NightGain = FMath::FInterpTo(NightGain, TargetNight, DeltaTime, 6.0f);
	const float UsedIntensity = TensionPin >= 0.0f ? TensionPin : Intensity;
	TensionGain = FMath::FInterpTo(TensionGain, FMath::Clamp(UsedIntensity, 0.0f, 1.0f) * 1.4f, DeltaTime, 2.5f);
	// Muted keeps the generator running at zero: the Harmonix clock behind the beat pulse must not stop.
	MasterVolume = bMusicMuted ? 0.0f : FMath::FInterpTo(MasterVolume, FMath::Clamp(Volume, 0.0f, 1.5f), DeltaTime, 2.5f);
	Music->SetFloatParameter(TEXT("DayGain"), DayGain);
	Music->SetFloatParameter(TEXT("NightGain"), NightGain);
	Music->SetFloatParameter(TEXT("TensionGain"), TensionGain);
	Music->SetFloatParameter(TEXT("Volume"), MasterVolume);
}

void UShiftAudio::SetMusicMuted(bool bMute)
{
	bMusicMuted = bMute;
	if (bMusicMuted)
	{
		MasterVolume = 0.0f;
		if (Music)
		{
			Music->SetFloatParameter(TEXT("Volume"), 0.0f);
		}
	}
	UE_LOG(LogShift, Log, TEXT("Audio: music muted=%d, music Volume=%.3f"), bMusicMuted ? 1 : 0, MasterVolume);
}

void UShiftAudio::SetEffectsMuted(bool bMute)
{
	bEffectsMuted = bMute;
	UE_LOG(LogShift, Log, TEXT("Audio: effects muted=%d"), bEffectsMuted ? 1 : 0);
}

void UShiftAudio::SetSfxOffMask(uint32 Mask)
{
	SfxOffMask = Mask;
	if (Mask != 0)
	{
		UE_LOG(LogShift, Log, TEXT("Audio: effects switched off by name, mask 0x%08x"), Mask);
	}
}

void UShiftAudio::Play(EShiftSfx Which, float Volume)
{
	if (bEffectsMuted)
	{
		return;
	}
	const int32 Index = static_cast<int32>(Which);
	if (Index >= 0 && Index < 32 && (SfxOffMask & (1u << Index)) != 0)
	{
		return;
	}
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
	if (MusicClock && MusicClock->GetState() == EMusicClockState::Running)
	{
		return FMath::Clamp(MusicClock->GetDistanceFromCurrentBeat(), 0.0f, 1.0f);
	}
	// Clock not running (yet, or the Harmonix plugin is unavailable): estimate from the time the music started.
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
