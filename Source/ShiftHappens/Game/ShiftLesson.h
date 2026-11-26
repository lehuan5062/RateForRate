// Rate for Rate - Game Off 2026

#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Game/ShiftBuild.h"
#include "Game/ShiftRun.h"
#include "HAL/Platform.h"
#include "Sim/ShiftSim.h"
#include "Templates/Function.h"
#include "UI/ShiftHudModel.h"

/** How a lesson's run is built: the practice board and what is switched off on it. */
struct FShiftLessonSetup
{
	int32 Jam = 0;
	uint32 Seed = 7;
	FShiftBuild Build;
	bool bMods = false;
	TMap<FString, double> Overrides;
};

/** What a player would do now: the QA autoplay (-ShiftLessonAuto) performs it through the same paths a click or key takes. */
struct FShiftLessonAuto
{
	enum class EKind : uint8 { None, Continue, Hover, Click, RightClick, NightOn, NightOff, Blame };
	EKind Kind = EKind::None;
	int32 Node = -1;
	bool bNight = false;
};

/**
 * The guided tutorial: a short scripted lesson on a small practice board. The world freezes while a step's text is read, the voter to use is pointed at, only the
 * action the step teaches is allowed, and the step ends when the sim or an event shows the player did it. Pure game logic (no UObject); the controller owns it.
 *
 *   chapter 1: the basics (hover, plug, conviction, reinforce, gates, parry, Rally), no Mod Eye
 *   chapter 2: night shift (sockpuppets, Heat, backing them, the Mod Eye)
 *   chapter 3: Shift the Blame (the strike that would end the jam, handed to the rival)
 *
 * Every step stages its own scene, so a step can be started on its own (-ShiftLesson=<chapter>:<step>) and a scene that was lost is put back.
 */
class FShiftLesson
{
public:
	enum EAllow : uint32
	{
		AllowPlug = 1u << 0,        // a click on a voter that is not the player's
		AllowReinforce = 1u << 1,   // a click on one of the player's own voters (reinforce, parry)
		AllowRally = 1u << 2,       // right-click
		AllowNight = 1u << 3,       // SHIFT / N
	};

	static int32 ChapterCount() { return 3; }

	/** The practice board of a chapter: jam, seed, build and the overrides that silence the world. */
	static void Configure(int32 Chapter, FShiftLessonSetup& Out);

	void Begin(int32 InChapter, int32 FirstStep = 1);
	/** Called once the run exists: picks the voters the steps use (by structure, never by id) and stages the first step. */
	void Attach(FShiftRun& Run);
	void Tick(FShiftRun& Run, float DeltaTime, const TArray<FShiftEvent>& Events, int32 HoverNode);
	void Continue(FShiftRun& Run);

	int32 Chapter() const { return ChapterIx; }
	bool IsFrozen() const { return Phase == EPhase::Reading || Phase == EPhase::Complete; }
	bool IsComplete() const { return Phase == EPhase::Complete; }
	const FShiftLessonView& View() const { return ViewState; }

	/** The gate for input: whether a click (or night shift) is allowed now, and a line for the player when it is not. */
	bool AllowClick(const ShiftSim::Sim& S, int32 Node, bool bRight, bool bNight, FString& Why) const;
	bool NightAllowed() const;

	/** A line to show once (a scene was put back, ...); empty when there is none. */
	FString ConsumeNotice();

	FShiftLessonAuto Auto(const ShiftSim::Sim& S) const;

	/** Fills in the numbers of a text ({sockheat} and the like); set by the controller. */
	TFunction<FString(const FString&)> Expand;

private:
	enum class EPhase : uint8 { Reading, Doing, Cheer, Complete };

	struct FStep
	{
		uint32 Allow = 0;
		bool bRead = false;                                                  // no action to wait for: Continue ends the step (a step with an action starts at once: no Continue first)
		float GoDelay = 0.0f;                                                // seconds after the step starts before Go runs (time to read the card)
		EShiftLessonAnchor Anchor = EShiftLessonAnchor::None;
		FString Label;                                                       // written beside the spotlit voters
		TFunction<void(FShiftRun&)> Stage;                                   // sets the scene (the world is frozen while it is read)
		TFunction<void(FShiftRun&)> Go;                                      // a thing the world does to the player in this step (an announced attack)
		TFunction<bool(FShiftRun&)> Done;
		TFunction<void(const ShiftSim::Sim&, TArray<int32>&)> Targets;       // the voters to point at, and the only ones a click may use
		TFunction<FShiftLessonAuto(const ShiftSim::Sim&)> AutoAct;
		TFunction<void(FShiftRun&)> Keep;                                    // runs every tick while the step is on: keeps the scene from sliding away by itself (nothing in a lesson may get lost while it is read)
		bool bRedOk = false;                                                 // the step is about Heat above the red tick: its Heat ceiling is higher
	};

	struct FPicks
	{
		int32 Own = -1;       // a voter the player starts with: shakes in the drift step
		int32 Weak = -1;      // one Plug flips it
		int32 Strong = -1;    // two Plugs
		int32 Hub = -1;       // the padlocked hub and its followers
		int32 Celeb = -1;     // the celebrity of the night shift lesson
		TArray<int32> HubFollowers;
	};

	void BuildSteps();
	void ChoosePicks(const ShiftSim::Sim& S);
	void EnterStep(FShiftRun& Run);
	void Advance(FShiftRun& Run);
	void RefreshView(const ShiftSim::Sim& S);
	bool Saw(EShiftEventType Type, int32 Node, int32 SockWanted = -1) const;

	int32 ChapterIx = 1;
	int32 StepIx = 0;
	int32 FirstStepIx = 0;
	EPhase Phase = EPhase::Reading;
	float PhaseTime = 0.0f;
	float HoverTime = 0.0f;
	float NightTime = 0.0f;
	bool bGoPending = false;            // Go has not run yet in this step
	bool bNagged = false;               // the "let go of SHIFT" hint was given in this step
	int32 StepKey = -1;                 // the voter a step is about: when it is lost the scene is put back
	TArray<FStep> Steps;
	TArray<FShiftEvent> StepEvents;
	TArray<int32> Targets;
	FPicks Picks;
	FShiftLessonView ViewState;
	FString Notice;
};
