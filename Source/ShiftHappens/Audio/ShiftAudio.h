// Shift Happens - Game Off 2026

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ShiftAudio.generated.h"

class UAudioComponent;
class USoundBase;
class UWorld;

/** One-shot sound effects. Each maps to /Game/Shift/Audio/MS_Sfx_<Name> (a MetaSound Source built by Tools/py/build_audio.py). */
UENUM()
enum class EShiftSfx : uint8
{
	Click,
	Hover,
	Slider,
	Upload,
	Transition,
	Tip,
	Plug,
	Flip,
	Reinforce,
	Weaken,
	Gated,
	Locked,
	NoPosts,
	NightOn,
	NightOff,
	Sock,
	Heartbeat,
	ModWarn,
	ModSweep,
	Strike,
	Shadowban,
	Blame,
	Day,
	Win,
	Lose,
	DQ,
	Count UMETA(Hidden)
};

/**
 * Plays the game's sound: the looping music mixer (MS_Music, three stems blended by Night and Intensity) and the sound effects.
 * Everything is a MetaSound Source, driven through float parameters on the music audio component.
 */
UCLASS()
class UShiftAudio : public UObject
{
	GENERATED_BODY()

public:
	/** Loads the assets and starts the music (silent until Tick raises the volume). Safe to call without the assets present. */
	void Init(UWorld* InWorld);
	void Shutdown();

	/** Smooths the music parameters toward the targets. Night 0..1 crossfades day and night stems; Intensity 0..1 adds the tension layer. */
	void Tick(float DeltaTime, float Night, float Intensity, float Volume);

	void Play(EShiftSfx Sfx, float Volume = 1.0f);

	/** 0..1 position inside the current beat (112.5 BPM), from the time the music started. */
	float BeatPhase() const;
	/** 1 on the beat, decaying quickly: drives the board's beat pulse. */
	float BeatPulse() const;

	bool HasMusic() const { return Music != nullptr; }
	bool IsMusicPlaying() const;
	/** QA: plays a plain SoundWave (no MetaSound) to tell device or recorder problems from graph problems. */
	void PlayRawWave(const FString& WaveAssetName);
	int32 LoadedSfxCount() const { return LoadedSfx; }

private:
	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> Sfx;

	UPROPERTY()
	TObjectPtr<USoundBase> MusicSound;

	UPROPERTY()
	TObjectPtr<UAudioComponent> Music;

	TWeakObjectPtr<UWorld> World;
	TArray<double> LastPlayed;
	int32 LoadedSfx = 0;
	double MusicStart = 0.0;
	float DayGain = 1.0f;
	float NightGain = 0.0f;
	float TensionGain = 0.0f;
	float MasterVolume = 0.0f;
};
