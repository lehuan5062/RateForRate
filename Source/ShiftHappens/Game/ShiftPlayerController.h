// Shift Happens - Game Off 2026

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Audio/ShiftAudio.h"
#include "Game/ShiftBuild.h"
#include "Game/ShiftRun.h"
#include "UI/ShiftHudModel.h"
#include "ShiftPlayerController.generated.h"

class AShiftBoard;
class SShiftHud;

/**
 * Owns the screen flow (title, jam select, build, play), the jam run, the board and the HUD; turns clicks into Plug / Sockpuppet
 * actions; and carries the command-line hooks used for unattended QA:
 *   -ShiftScreen=title|jams|build|play   start on a screen (default: play if any run flag below is given, else the title)
 *   -ShiftJam=0..2 -ShiftSeed=N          which jam and seed (default jam 2, seed 7 for QA; random otherwise)
 *   -ShiftBuild=Q,T,N                    build hours (quality, thumbnail, network; default 4,4,4)
 *   -ShiftBot=idle|random|greedy|sock    scripted player; -ShiftSpeed=N fast-forwards the sim; -ShiftWarp=S runs S sim seconds first
 *   -ShiftNight=0..1                     force the night blend      -ShiftCursor=X,Y  fake cursor in design pixels
 *   -ShiftClicks="T,X,Y[,n];..."         scripted board clicks      -ShiftUi="T,Id[,V];..."  scripted UI actions
 *   -ShiftMouse="T,X,Y,m|d|u;..."        scripted mouse move / left down / left up through the real widget (design pixels)
 *   -ShiftTutorial / -ShiftNoTutorial    force or skip the coach cards
 *   -ShiftSfxCycle                       plays every sound effect in turn (1.5 s apart) for the audio recording check
 *   -ShiftMasterVolume=0..1              scales all sound (QA runs use a small value); -ShiftNoAudio skips audio entirely
 *   -ShiftRecordAudio=<abs wav> [-ShiftRecordStart=S -ShiftRecordSeconds=S]  records the master submix to a WAV for analysis
 *   -ShiftShot=<absolute png>            screenshot (with UI) after -ShiftShotDelay seconds (default 4); -ShiftQuit exits afterwards
 */
UCLASS()
class SHIFTHAPPENS_API AShiftPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AShiftPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PlayerTick(float DeltaTime) override;

private:
	// ---- flow
	void StartRun(int32 InJamIndex, uint32 InSeed, bool bInAttract);
	void StartAttract();
	void StartPlay();
	void RestartRun();
	void EnterScreen(EShiftScreen NewScreen);
	void RefreshJamCards();
	void RefreshForecast();
	void SetBuildHours(int32 Slider, float Value01);
	uint32 NewSeed() const;
	void SetPaused(bool bInPaused);
	void ToggleMute();
	void ApplyMute() const;
	void CopyResultToClipboard();

	// ---- input
	void HandleUi(int32 Id, float Value01);
	void HandleHudClick(const FVector2D& LocalPos, bool bShift, bool bRightButton);
	void HandleHudKey(const FKey& Key);
	bool IsNightHeldNow() const;

	// ---- model
	void BuildHudModel(FShiftHudModel& Out);
	void BuildMenuModel(FShiftHudModel& Out);
	void BuildHoverTooltip(FShiftHudTooltip& Out) const;
	void BuildResults(FShiftHudModel& Out) const;
	FString BuildShareText() const;
	void HandleEvents(const TArray<FShiftEvent>& Events);
	void ShowToast(const FString& Text, const FLinearColor& Color, float Seconds = 2.6f);
	void PlaySfx(EShiftSfx Which, float Volume = 1.0f) const { if (Audio) { Audio->Play(Which, Volume); } }
	void TickAudio(float DeltaTime);
	void ShowHeadline(const FString& Event, const FString& EntryName);
	FString NodeHandle(int32 Node) const;
	FString EntryName(int32 EntryId) const;

	// ---- tutorial coach cards
	void TickTutorial(float DeltaTime);
	void QueueCoach(int32 Index);
	void DismissCoach();

	// ---- QA hooks
	void ScheduleScreenshotFromCommandLine();
	void TakeScreenshot();
	void LogWorldDiagnostics() const;
	void QuitNow();

	TSharedPtr<FShiftRun> Run;
	TSharedPtr<SShiftHud> Hud;

	UPROPERTY()
	TObjectPtr<AShiftBoard> Board;

	UPROPERTY()
	TObjectPtr<UShiftAudio> Audio;

	TArray<FString> NodeHandles;
	TArray<FString> NodeBios;
	TArray<FShiftEvent> EventScratch;
	FRandomStream CosmeticRng;

	// screen flow
	EShiftScreen Screen = EShiftScreen::Title;
	bool bAttract = true;
	FShiftBuild Build;
	FShiftForecast Forecast;
	bool bForecastDirty = true;
	float ForecastCooldown = 0.0f;
	TArray<FShiftHudJamCard> JamCards;
	TArray<int32> CardJam;          // jam index behind each card
	uint32 LastRegistryVersion = 0;

	int32 JamIndex = 2;
	uint32 Seed = 7;
	bool bFixedSeed = false;
	bool bMuted = false;

	float NightBlend = 0.0f;
	float NightOverride = -1.0f;
	bool bNightLatch = false;
	bool bPaused = false;
	bool bResultsBuilt = false;
	FShiftHudModel ResultsModel;
	TArray<float> InitialShares;

	FString ToastText;
	FLinearColor ToastColor = FLinearColor::White;
	float ToastRemaining = 0.0f;
	float ToastDuration = 1.0f;

	FString HeadlineText;
	float HeadlineRemaining = 0.0f;
	int32 LastLeader = -1;
	int32 LastHeadlineDay = -1;

	int32 HoverNode = -1;
	FVector2D CursorWorld = FVector2D::ZeroVector;
	bool bFakeCursor = false;
	FVector2D FakeCursorDesign = FVector2D::ZeroVector;

	// pause when the window loses focus (a held SHIFT would otherwise get stuck); off for unattended QA runs
	bool bAutoPauseOnFocusLoss = true;
	bool bQaRunFlag = false;           // unattended QA run: never touch the player's saved progress

	// audio state
	float QaMasterVolume = 1.0f;
	FString QaRecordPath;
	float QaRecordStart = 1.0f;
	float QaRecordSeconds = 6.0f;
	bool bQaRecording = false;
	bool bQaSfxCycle = false;
	float QaSfxTimer = 2.0f;
	int32 QaSfxIndex = 0;
	bool bPrevNight = false;
	float HeartbeatTimer = 0.0f;
	double LastWarnedSweep = -1.0;
	int32 LastHoverNode = -1;
	float SliderSfxCooldown = 0.0f;

	// tutorial
	FString CoachText;
	int32 CoachCurrent = -1;
	float CoachTimer = 0.0f;
	float CoachAlpha = 0.0f;
	float CoachCooldown = 0.0f;
	TArray<int32> CoachQueue;
	int32 CoachSeenMask = 0;
	int32 CoachQueuedMask = 0;
	bool bCoachEnabled = true;
	bool bCoachPersist = true;
	uint32 TutEventMask = 0;
	float RunClock = 0.0f;
	bool bPlayerActed = false;
	TArray<int32> GatedSeenNodes;

	struct FScriptedClick
	{
		float Time = 0.0f;
		FVector2D Design = FVector2D::ZeroVector;
		bool bNight = false;
	};
	TArray<FScriptedClick> ScriptedClicks;

	struct FScriptedUi
	{
		float Time = 0.0f;
		int32 Id = 0;
		float Value = 0.0f;
	};
	TArray<FScriptedUi> ScriptedUi;

	struct FScriptedMouse
	{
		float Time = 0.0f;
		int32 Kind = 0;                 // 0 move, 1 down, 2 up
		FVector2f Design = FVector2f::ZeroVector;
	};
	TArray<FScriptedMouse> ScriptedMouse;
	float ScriptClock = 0.0f;

	FTimerHandle ShotTimer;
	FTimerHandle QuitTimer;
	FString ShotPath;
	bool bQuitAfterShot = false;

	double FrameSum = 0.0;
	double FrameMax = 0.0;
	int32 FrameCount = 0;
};
