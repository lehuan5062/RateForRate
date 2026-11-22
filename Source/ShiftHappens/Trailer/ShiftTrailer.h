// Shift Happens - Game Off 2026

#pragma once

#if !UE_BUILD_SHIPPING

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "GameFramework/Actor.h"
#include "HAL/Platform.h"
#include "Math/MathFwd.h"
#include "Misc/Build.h"
#include "UI/ShiftHudModel.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AShiftPlayerController;   // a real circular dependency: the controller owns the director and the director drives the controller

/**
 * The scripted trailer run: compiled into Editor, Development and Test builds, never into Shipping (the itch.io build has none of it).
 *
 * The whole trailer is one performance of the game itself: the director reads Data/Trailer.csv (rows "Id,Bar,Cmd,Args", read at start, so the
 * cues and the wording change without a rebuild), restarts the music, and runs a clock in bars of the music's tempo (112.5 BPM, 4 beats,
 * 2.1333 s). Cues start runs and skip sim time under a cover (black or white flash), flip the night, pin the music's tension layer, move the
 * scripted cursor, spotlight part of the screen and put up the trailer text, which the HUD draws with Slate (UI/SShiftHud.cpp PaintTrailer).
 * The music and the sound effects are the game's own and play live, so the picture and the sound are one take.
 *
 * Commands (Args, space separated):
 *   black <alpha> <seconds>          fade the cover to alpha 0..1
 *   flash <seconds>                  a white flash that fades out
 *   title                            the title screen with its attract run
 *   run <jam> <seed> <bot|-> <warpSimSeconds> [speed]   a fresh run (bot as -ShiftBot, warp as -ShiftWarp)
 *   warp <simSeconds>                skip sim time in the current run
 *   bot <name> | speed <x>           change the bot / the sim speed
 *   night <0|1> [now]                the real SHIFT flip (and the music crossfade); 1 holds night, 0 releases it; "now" skips the fade (under a cover)
 *   tension <0..1|-1>                pin the music's tension layer (-1 gives it back to the game)
 *   click <x> <y> [n]                a click of the scripted cursor at the bar (design pixels; n = sockpuppet); the cursor glides there first
 *   hover <x> <y> [seconds]          the scripted cursor glides there and rests
 *   spot <x> <y> <w> <h> <seconds>   dim everything but the rectangle for that long; "spot off" ends it
 *   spot mods <seconds>              the same on the MODS countdown of the top bar (the HUD fits the rectangle to its icon and text)
 *   zoom <in> <hold> <out> <scale> <x> <y>   a zoom-in toward the design point x y: eased in over <in> seconds up to <scale>, held, eased out;
 *                                    the board camera and the HUD zoom together (the trailer's own text does not)
 *   slam <seconds> <TEXT|NEXT LINE>  a full-screen statement (SHIFT split)
 *   caption <seconds> <TEXT>         a caption, lower left
 *   end                              fade to black and quit (the end of Play In Editor, or the process)
 */
class FShiftTrailer
{
public:
	explicit FShiftTrailer(AShiftPlayerController& InOwner);

	/** Reads the cue file. Returns false, with the reason in the log, when there is nothing to play. */
	bool Load(const FString& Path);

	/** Restarts the music and begins the script: the clock starts at bar 0 as soon as the music clock runs. */
	void Start();

	bool IsActive() const { return bActive; }
	void Tick(float DeltaTime);

	/** What the HUD draws this frame. */
	void FillModel(FShiftHudTrailer& Out) const;

private:
	struct FCue
	{
		FString Id;
		float Bar = 0.0f;
		double Time = 0.0;   // seconds on the clock: the bar plus the latency
		FString Cmd;
		FString Args;
	};

	void Begin();
	void Execute(const FCue& Cue);
	void TickZoom();
	void ApplyCamera();

	AShiftPlayerController& Owner;
	TArray<FCue> Cues;
	int32 NextCue = 0;

	bool bActive = false;
	bool bPreroll = false;        // the game warms up under black with the music held; the film (and the music) start when it ends
	double PrerollLeft = 0.0;
	float PrerollSeconds = 5.0f;  // -ShiftTrailerPreroll=<s>; only with sound (a headless run has no music to line up and film time is game time)
	bool bWaitingForMusic = false;
	double WaitSeconds = 0.0;
	double Clock = 0.0;           // seconds since bar 0 (the game's own frame time, which follows the real time unless a frame is longer than the engine's 0.4 s limit)
	double WallStart = 0.0;       // the real time at bar 0: the log shows both, so a drift against the music would show
	float Latency = 0.04f;        // -ShiftTrailerLatency: the sound is this late against the picture, so the picture waits for it
	double QuitAt = -1.0;

	// what is on screen
	float Black = 1.0f;
	float BlackFrom = 1.0f;
	float BlackTarget = 1.0f;
	float BlackSeconds = 0.0f;
	float BlackAge = 0.0f;
	float White = 0.0f;
	float WhiteSeconds = 0.2f;
	float SpotAlpha = 0.0f;
	double SpotUntil = -1.0;
	FVector4f SpotRect = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	bool bSpotMods = false;
	TArray<FShiftTrailerText> Texts;

	// the zoom-in
	double ZoomT0 = -1.0;         // the clock when it started, negative when none runs
	float ZoomIn = 0.6f;
	float ZoomHold = 1.0f;
	float ZoomOut = 0.6f;
	float ZoomScale = 1.5f;
	FVector2f ZoomFocus = FVector2f(640.0f, 360.0f);
	float Zoom = 1.0f;
	TWeakObjectPtr<AActor> CameraActor;   // the board camera, found when the first zoom needs it
	FVector CameraBase = FVector::ZeroVector;
	float OrthoBase = 1280.0f;
	bool bCameraMoved = false;
};

#endif   // !UE_BUILD_SHIPPING
