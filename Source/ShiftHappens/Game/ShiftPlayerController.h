// Rate for Rate - Game Off 2026

#pragma once

#include "Audio/ShiftAudio.h"
#include "Audio/ShiftAudioMeter.h"
#include "Board/ShiftBoard.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Engine/EngineTypes.h"
#include "Engine/TimerHandle.h"
#include "Game/ShiftAchievements.h"
#include "Game/ShiftBuild.h"
#include "Game/ShiftForecast.h"
#include "Game/ShiftForecastService.h"
#include "Game/ShiftLesson.h"
#include "Game/ShiftRecording.h"
#include "Game/ShiftRun.h"
#include "GameFramework/PlayerController.h"
#include "HAL/Platform.h"
#include "InputCoreTypes.h"
#include "Math/Color.h"
#include "Math/MathFwd.h"
#include "Math/RandomStream.h"
#include "Misc/Build.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "UI/ShiftHudModel.h"
#include "UI/SShiftHud.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "ShiftPlayerController.generated.h"

#if !UE_BUILD_SHIPPING
#include "Trailer/ShiftTrailer.h"   // the controller owns the trailer run (a unique pointer: its type must be complete here)
#endif

/**
 * Owns the screen flow (title, jam select, build, play), the jam run, the board and the HUD; turns clicks into Plug / Sockpuppet
 * actions; and carries the command-line hooks used for unattended QA:
 *   -ShiftScreen=title|jams|build|play   start on a screen (default: play if any run flag below is given, else the title)
 *   -ShiftJam=0..2 -ShiftSeed=N          which jam and seed (default jam 2, seed 7 for QA; random otherwise)
 *   -ShiftBuild=Q,T,N                    build hours (quality, thumbnail, network; default 4,4,4)
 *   -ShiftSet=Name=Value,...             override sim Params, jam fields or policy fields by name, as the balance commandlet's -Set does (BotBlame=0 keeps a bot from taking Shift the Blame)
 *   -ShiftBot=idle|random|greedy|sock|finisher|late  scripted player; -ShiftSpeed=N fast-forwards the sim; -ShiftWarp=S runs S sim seconds first
 *   -ShiftNight=0..1                     force the night blend      -ShiftCursor=X,Y  fake cursor in design pixels
 *   -ShiftClicks="T,X,Y[,n];..."         scripted board clicks      -ShiftUi="T,Id[,V];..."  scripted UI actions
 *   -ShiftMouse="T,X,Y,m|d|u;..."        scripted mouse move / left down / left up through the real widget (design pixels)
 *   -ShiftTutorial / -ShiftNoTutorial    force or skip the coach cards
 *   -ShiftMasterVolume=0..1              scales all sound (QA runs use a small value); -ShiftNoAudio skips audio entirely
 *   -ShiftNoSfx / -ShiftNoMusic          play the music only / the effects only (never saved); -ShiftSfxOff=Name,Name silences those effects; -ShiftTension=0..1 pins the music's tension layer
 *   -ShiftAudioMeter=all|sfx|music|spike|<names>  audio QA: plays MetaSound sources inaudibly (needs -Sound) and logs their in-graph meters; no recording
 *   -ShiftAudioCpu                       with -ShiftAudioMeter: logs the render cost of each sound as a share of one core instead of its levels
 *   -ShiftAudioMeterDrop=<starts>        audio QA hook: ignore the meter data of those Play() starts (forces the retry paths); -ShiftAudioMeterStall=<ms>@<starts> stalls the game thread
 *   -ShiftShot=<absolute png>            screenshot (with UI) after -ShiftShotDelay seconds (default 4); -ShiftQuit exits afterwards
 *   -ShiftShotTimes=t1,t2,...            with -ShiftShot: one screenshot per time (seconds) in a single run, named <path>_<time in hundredths>.png
 *   -ShiftTrailer[=<csv>]                editor builds only: plays the scripted trailer run (Data/Trailer.csv, Trailer/ShiftTrailer.h); -ShiftTrailerLatency=<s> shifts the picture against the sound
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

#if !UE_BUILD_SHIPPING
	/** Plays the scripted trailer run (Trailer/ShiftTrailer.h): from -ShiftTrailer, or the console command Shift.Trailer. */
	void StartTrailer();
#endif

private:
#if !UE_BUILD_SHIPPING
	friend class FShiftTrailer;   // the director drives the run, the night, the cursor and the clicks directly
#endif

	// ---- flow
	void StartRun(int32 InJamIndex, uint32 InSeed, bool bInAttract);
	void StartAttract();
	void StartPlay();
	void RestartRun();
	void StartLesson(int32 Chapter, int32 FirstStep);
	void LeaveLesson(EShiftScreen NewScreen);
	void TickLesson(float DeltaTime);
	void EnterScreen(EShiftScreen NewScreen);
	void RefreshJamCards();
	void RefreshForecast();
	void SetBuildHours(int32 Slider, float Value01);
	uint32 NewSeed() const;
	void SetPaused(bool bInPaused);
	/** M and the Mute button: both switches off, or back on when both are already off. */
	void ToggleMute();
	void SetEffectsOn(bool bOn);
	void SetMusicOn(bool bOn);
	/** Pushes the two switches (and the QA overrides -ShiftNoSfx / -ShiftNoMusic) into the audio. */
	void ApplySoundSettings() const;
	/** The window mode buttons: 0 fullscreen, 1 borderless (windowed fullscreen), 2 windowed (the EWindowMode values). Applied through the game user settings and saved with them. */
	void SetWindowMode(int32 Mode);
	int32 CurrentWindowMode() const;
	void CopyResultToClipboard();
	/** Saves the recording of the jam being played (Game/ShiftRecording.h): finished when the jam ended, "abandoned" when it is left unfinished. */
	void SaveRecording(const TCHAR* AbandonedResult);
	FString RecordingsDir() const;
	void OpenRecordingsFolder() const;
	/** The replays page: the newest recordings in Saved/Recordings, their header read. */
	void RefreshReplays();
	/** The paper wash between two screens: starts now (nothing in QA runs and with Reduce motion). */
	void BeginTransition();
	/** A vote count that rises from a voter: "+82" for a voter the player won, "-31" for one it lost. */
	void SpawnFloat(int32 Node, const FString& Text, const FLinearColor& Color);
	/** The replay viewer: plays a recording back in the game, exactly as it was played (the sim is deterministic), with pause, speed and seeking. */
	void StartReplayFile(const FString& Path);
	void SeekReplay(float Value01);

	// ---- input
	void HandleUi(int32 Id, float Value01);
	void HandleHudClick(const FVector2D& LocalPos, bool bShift, bool bRightButton);
	void HandleHudKey(const FKey& Key);
	void HandleHudPan(const FVector2f& DeltaDesign);
	bool IsNightHeldNow() const;
	bool IsNightHeldRaw() const;

	// ---- model
	void BuildHudModel(FShiftHudModel& Out);
	void BuildMenuModel(FShiftHudModel& Out);
	void BuildHoverTooltip(FShiftHudTooltip& Out) const;
	void FillBuildEffects(FShiftHudBuild& B) const;
	void BuildResults(FShiftHudModel& Out) const;
	FString BuildShareText() const;
	void HandleEvents(const TArray<FShiftEvent>& Events);
	void ShowToast(const FString& Text, const FLinearColor& Color, float Seconds = 2.6f);
	void PlaySfx(EShiftSfx Which, float Volume = 1.0f) const { if (Audio) { Audio->Play(Which, Volume); } }
	void TickAudio(float DeltaTime);
	void ShowHeadline(const FString& Event, const FString& EntryName);
	/** The text of a headline with its placeholders filled; empty if the Event has none. Draws from Rng (the end-of-jam news uses a seeded stream). */
	FString BuildHeadline(const FString& Event, const FString& Entry, FRandomStream& Rng) const;
	/** Chatter: a comment line from the Comments table in a card of the feed, spoken by Speaker (empty = a random voter). At most 2 wait; none in lessons or attract. */
	void Chatter(const TCHAR* Context, const FString& Speaker, float Delay, float Seconds = 3.4f);
	void TickChatter(float DeltaTime);
	FString RandomHandle();
	void TickLostVoters(float DeltaTime);
	void TickShareTrend();

	// ---- alerts: the urgent events (strike, Shift the Blame offer, disqualification, final push and audit) take over the middle of the bottom bar for a few seconds (FShiftHudAlert)
	enum EAlertKind : int32 { AlertNone = 0, AlertStrike, AlertBlameOffer, AlertBlameTaken, AlertDisqualified, AlertFinalPush, AlertFinalAudit };
	struct FAlert
	{
		int32 Kind = AlertNone;
		FString Title;
		FString Detail;
		FLinearColor Color = FLinearColor::White;
		float Duration = 3.0f;
		int32 Priority = 0;
	};
	void PushAlert(int32 Kind, const FString& Title, const FString& Detail, const FLinearColor& Color, float Duration, int32 Priority);
	void TickAlerts(float DeltaTime);
	void FillAlertModel(FShiftHudModel& M) const;
	void ResetAlerts();
	bool ResultsShown() const { return Run.IsValid() && Run->IsOver() && ResultsHold <= 0.0f; }
	FString ExpandTutorialText(const FString& Text) const;
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

	UPROPERTY()
	TObjectPtr<UShiftAudioMeter> AudioMeter;

	TArray<FString> NodeHandles;
	TArray<FString> NodeBios;
	TArray<FShiftEvent> EventScratch;
	FRandomStream CosmeticRng;

	// screen flow
	EShiftScreen Screen = EShiftScreen::Title;
	bool bAttract = true;
	FShiftBuild Build;
	FShiftForecast Forecast;                       // what the build screen shows for the current build: the quality list at once, the vote forecast once the plays are in
	FShiftForecastService ForecastService;         // plays the world off the game thread
	TArray<float> WorldQuality;                    // the other entries' quality in this world, per entry id
	bool bForecastReady = false;
	float ForecastSpread = 0.0f;                   // how far one play of a steady player strays from the average (0..1)
	TArray<FShiftHudJamCard> JamCards;
	TArray<int32> CardJam;          // jam index behind each card
	uint32 LastRegistryVersion = 0;

	int32 JamIndex = 2;
	int32 QaResultsTab = 0;         // -ShiftResultsTab: the tab every run starts on
	int32 ResultsTab = 0;           // the results card's chart tab: 0 quality and fame, 1 the shares over time
	uint32 Seed = 7;
	bool bFixedSeed = false;
	bool bEffectsOn = true;         // the settings menu's Effects switch (remembered)
	bool bMusicOn = true;           // ... and its Music switch (remembered)
	bool bForceNoSfx = false;       // QA / capture: -ShiftNoSfx, never saved
	bool bForceNoMusic = false;     // QA / capture: -ShiftNoMusic, never saved
	int32 EdgeMode = 0;            // relation lines: 0 faint, 1 off, 2 full (key E, remembered)
	int32 LeaderboardMode = 0;      // 0 compact, 1 full, 2 hidden (key L, remembered)

	float NightBlend = 0.0f;
	float NightOverride = -1.0f;
	bool bNightLatch = false;
	bool bPaused = false;
	bool bResultsBuilt = false;
	bool bRecordingSaved = false;
	bool bForceRecord = false;          // -ShiftRecord: a QA run is recorded too
	bool bReplayMode = false;           // -ShiftReplay=<file>: the recording is replayed instead of played
	FShiftRecording ReplaySource;
	bool bTransitions = false;          // the paper wash between screens (off in QA runs, so scripted shots and flows are unaffected)
	float TransitionAge = 1.0f;         // seconds since a screen change; at or past TransitionLength there is no wash
	static constexpr float TransitionLength = 0.4f;
	TArray<FShiftHudFloat> FloatTexts;  // the rising vote counts
	bool bViewer = false;               // the replay was started from the interface (not -ShiftReplay): the viewer bar is up and leaving restores the build sliders
	bool bViewerPaused = false;
	int32 ViewerSpeed = 0;              // index into the speeds 1, 2, 4, 8
	double LastSeekReal = 0.0;
	FShiftBuild ViewerSavedBuild;
	TArray<FString> ReplayFiles;        // the file behind each row of the replays page
	TArray<FShiftHudReplayRow> ReplayRows;
	FString RecordingPath;              // the file the last jam was recorded to
	FAlert AlertNow;                // the alert on screen (Kind AlertNone = none); the Blame offer is not queued, it is read from the run
	TArray<FAlert> AlertQueue;      // lower priority ones waiting for it
	float AlertAge = 0.0f;
	float OfferAge = 0.0f;          // seconds the Shift the Blame offer has been open
	float BlameCue = 0.0f;          // seconds to the next warning sound while it is
	float EdgeFlash = 0.0f;         // 0..1, decays
	FLinearColor EdgeColor = FLinearColor::White;
	float ResultsHold = 0.0f;       // after a disqualification the results screen waits this long (real seconds) so the alert is seen first
	bool bBlameRefused = false;     // the fatal strike took too many fake votes to blame on the rival
	float BlameRefusedShare = 0.0f;
	FShiftHudModel ResultsModel;
	TArray<float> InitialShares;

	/** A card in the right gutter: a toast or a news headline. The newest is first. */
	struct FFeedItem
	{
		FString Text;
		FLinearColor Color = FLinearColor::White;
		float Remaining = 0.0f;
		float Duration = 1.0f;
		bool bNews = false;
		bool bInk = false;       // chatter: ink text, the colour on the accent bar only
		bool bChatter = false;   // chatter never counts as an active toast (it must not starve the lost-voter summary)
	};
	TArray<FFeedItem> Feed;
	void PushFeed(const FString& Text, const FLinearColor& Color, float Seconds, bool bNews, bool bChatter = false);

	struct FChatter
	{
		FString Context;
		FString Speaker;
		float Delay = 0.0f;
		float Seconds = 3.4f;
	};
	TArray<FChatter> ChatterQueue;
	float ChatterGap = 0.0f;      // quiet time after a chatter card
	float IdleClock = 0.0f;       // real seconds since the last click
	TArray<int32> GateChatted;    // gates whose opening was already commented

	// achievements and career
	FShiftAchTracker Ach;
	uint32 AchSaved = 0;                      // the achievements already in the save
	TArray<FString> AchNewThisRun;            // their names, for the results card
	int32 ExtrasPage = 0;                     // title: 0 none, 1 achievements, 2 credits
	float SpotFlashClock = 0.0f;              // heartbeat of the spotlighted voter

	// The view: the board is centred on the screen (the sim keeps a strip free on the right for the HUD) and can be panned. All of it is the camera's position;
	// the sim, the recordings and the balance never see it. A voter at sim (x, y) is drawn at design pixel (x, y) + ViewShift.
	TWeakObjectPtr<AActor> ViewCamera;
	FVector ViewCameraBase = FVector::ZeroVector;
	FVector2D ViewPan = FVector2D::ZeroVector;          // design pixels, what the player did
	FVector2D ViewPanTarget = FVector2D::ZeroVector;
	FVector2D ViewShift = FVector2D::ZeroVector;        // centring plus pan
	FVector2D ViewPanLo = FVector2D::ZeroVector;        // the range the pan may take, so the voters stay on the screen
	FVector2D ViewPanHi = FVector2D::ZeroVector;
	bool bViewBounds = false;
	void UpdateView(float DeltaTime);
	void ResetView();
	FVector2D SimToDesign(double X, double Y) const { return FVector2D(X + ViewShift.X, Y + ViewShift.Y); }
	bool bMarkers = false;                    // accessibility switches (saved; -ShiftMarkers / -ShiftLargeText / -ShiftCalm force them on for a QA session)
	bool bLargeText = false;
	bool bReduceMotion = false;
	bool bGlassSaved = false;                 // the saved see-through window switch (it changes at the next launch)
	int32 GlassOpacityPct = 85;               // the Paper opacity slider of the see-through window, percent (saved); the paper's real opacity is ShiftSave::GlassPaperAlpha of it (linear)
	float GlassAlphaForced = -1.0f;           // -ShiftGlass=<0..1> (QA): the paper's real opacity for this session, until the slider is moved; negative = the slider rules
	float PaperAlpha() const { return GlassAlphaForced >= 0.0f ? GlassAlphaForced : ShiftSave::GlassPaperAlpha(GlassOpacityPct); }
	void FillAccessModel(FShiftHudModel& M) const;
	void QuitGame();
	bool AchEligible() const;                 // a real human run: not QA, a bot, the attract loop, a lesson, a replay, a forced night or a -ShiftSet world
	void PollAchievements();
	void FinishAchievements();
	void FillExtrasModel(FShiftHudModel& M) const;
	bool HasActiveToast(float MinRemaining) const;

	// voters the player lost to opinion pressure or a rival attack, gathered into one toast every few seconds (see TickLostVoters)
	struct FLostVoter
	{
		int32 Node = -1;
		int32 NewOwner = -1;
		int32 Cause = 0;        // 0 opinion pressure, 1 the rival, 2 another entry
	};
	TArray<FLostVoter> LostRecently;
	float LostCooldown = 0.0f;

	int32 LastLeader = -1;
	int32 LastHeadlineDay = -1;

	// the leaderboard's trend: every entry's share sampled once a second of run time, the last 21 samples (see TickShareTrend)
	TArray<TArray<float>> ShareHistory;
	double ShareSampleTime = -1.0;

	int32 HoverNode = -1;
	FVector2D CursorWorld = FVector2D::ZeroVector;
	bool bFakeCursor = false;
	FVector2D FakeCursorDesign = FVector2D::ZeroVector;

	// -ShiftPseudoCursor: a scripted cursor for trailer footage. It glides between waypoints (scripted clicks, lesson autoplay, bot actions),
	// drives the fake cursor above so the hover cards show, and rings where a click lands. Presentation only: nothing here touches the sim.
	bool bPseudoCursor = false;
	bool bPseudoShown = false;            // false until the first waypoint: the cursor glides in from the side instead of sitting at a corner
	bool bSystemCursor = false;           // -ShiftSystemCursor: the operating system's own arrow instead of the game's cursor
	FVector2D PseudoPos = FVector2D(640.0, 360.0);
	FVector2D PseudoFrom = FVector2D(640.0, 360.0);
	FVector2D PseudoTo = FVector2D(640.0, 360.0);
	float PseudoClock = 1.0f;             // seconds into the current glide
	float PseudoSeconds = 0.3f;           // how long the glide takes
	float PseudoPlannedClick = -1.0f;     // the time of the scripted click the cursor is already heading for
	int32 PseudoBotActions = 0;           // bot actions already shown
	void PseudoMoveTo(const FVector2D& Design, float Seconds);
	void PseudoClick(const FVector2D& Design, int32 Kind);
	void TickPseudoCursor(float DeltaTime);

	// pause when the window loses focus (a held SHIFT would otherwise get stuck); off for unattended QA runs
	bool bAutoPauseOnFocusLoss = true;
	bool bQaRunFlag = false;           // unattended QA run: never touch the player's saved progress

	// audio state
	float QaMasterVolume = 1.0f;
	FString QaAudioMeterSpec;
	bool bPrevNight = false;
	float HeartbeatTimer = 0.0f;
	double LastWarnedSweep = -1.0;
	bool bAuditWarned = false;

	// guided lessons (see ShiftLesson.h): a lesson run records nothing, saves no rank and shows no pop-up cards
	TUniquePtr<FShiftLesson> Lesson;
	bool bLessonRun = false;
	int32 LessonChapter = 1;
	bool bLessonDone = false;
	bool bLessonNudge = false;         // the basics were never finished: the title screen points at Learn to play
	bool bHowTo = false;               // the reference page is open
	bool bSettingsOpen = false;        // the settings menu (sound, window mode) is dropped from the gear button
	bool bLessonAuto = false;          // -ShiftLessonAuto: QA performs every step by itself
	float LessonAutoTimer = 0.0f;
	double LessonAutoSim = 0.0;
	float LessonAutoWall = 0.0f;
	float LessonAutoDone = 0.0f;
	int32 RallyFxNode = -1;         // the voter a Rally just went off on, for the teal lines on the board
	float RallyFxAge = 99.0f;
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

#if !UE_BUILD_SHIPPING
	// the scripted trailer run (not in Shipping builds)
	TUniquePtr<FShiftTrailer> Trailer;
	bool bRunOverride = false;       // StartRun takes the bot, speed and warp below instead of the command line
	FString RunOverrideBot;
	float RunOverrideSpeed = 1.0f;
	float RunOverrideWarp = 0.0f;
	void TrailerBeginRun(int32 Jam, uint32 InSeed, const FString& Bot, float Speed, float Warp);
	void TrailerWarp(float SimSeconds);
	void TrailerQuit();
#endif

	FTimerHandle ShotTimer;
	TArray<FTimerHandle> MultiShotTimers;   // -ShiftShotTimes
	FTimerHandle QuitTimer;
	FString ShotPath;
	bool bQuitAfterShot = false;

	double FrameSum = 0.0;
	double FrameMax = 0.0;
	int32 FrameCount = 0;
};
