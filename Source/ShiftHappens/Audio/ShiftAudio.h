// Shift Happens - Game Off 2026

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ShiftAudio.generated.h"

class UAudioComponent;
class UMusicClockComponent;
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

/** Asset-name suffix of a sound effect ("Click" for /Game/Shift/Audio/MS_Sfx_Click). */
const TCHAR* ShiftSfxName(EShiftSfx Sfx);

/**
 * Plays the game's sound. Every sound is a MetaSound Source (no wave assets): the looping music source MS_Music (day / night / tension layers
 * mixed inside the graph by the float parameters DayGain, NightGain, TensionGain and Volume, clocked by a Harmonix Metronome) and one MS_Sfx_*
 * one-shot per effect. The beat pulse that drives the board comes from a Harmonix music clock bound to the music source's "MIDI Clock" output.
 */
UCLASS()
class UShiftAudio : public UObject
{
	GENERATED_BODY()

public:
	/** Loads the assets and starts the music (silent until Tick raises the volume). Safe to call without the assets present. */
	void Init(UWorld* InWorld);
	void Shutdown();

	/** Smooths the music parameters toward the targets. Night 0..1 crossfades day and night layers; Intensity 0..1 adds the tension layer. */
	void Tick(float DeltaTime, float Night, float Intensity, float Volume);

	void Play(EShiftSfx Sfx, float Volume = 1.0f);

	/** 0..1 position inside the current beat (112.5 BPM), from the Harmonix clock (wall clock only if the clock is unavailable). */
	float BeatPhase() const;
	/** 1 on the beat, decaying quickly: drives the board's beat pulse. */
	float BeatPulse() const;

	bool HasMusic() const { return Music != nullptr; }
	bool IsMusicPlaying() const;
	bool HasMusicClock() const { return MusicClock != nullptr; }
	int32 LoadedSfxCount() const { return LoadedSfx; }
	/** Beat events received from the Harmonix clock since the music started (QA: proves the clock is bound and running). */
	int32 BeatEventCount() const { return BeatEvents; }

private:
	UFUNCTION()
	void OnMusicBeat(int32 BeatNumber, int32 BeatInBar);

	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> Sfx;

	UPROPERTY()
	TObjectPtr<USoundBase> MusicSound;

	UPROPERTY()
	TObjectPtr<UAudioComponent> Music;

	UPROPERTY()
	TObjectPtr<UMusicClockComponent> MusicClock;

	TWeakObjectPtr<UWorld> World;
	TArray<double> LastPlayed;
	int32 LoadedSfx = 0;
	int32 BeatEvents = 0;
	double MusicStart = 0.0;
	float DayGain = 1.0f;
	float NightGain = 0.0f;
	float TensionGain = 0.0f;
	float MasterVolume = 0.0f;
};
