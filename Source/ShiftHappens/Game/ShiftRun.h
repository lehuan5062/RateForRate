// Rate for Rate - Game Off 2026

#pragma once

#include "Behavior/ShiftPolicies.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Game/ShiftBuild.h"
#include "Game/ShiftRecording.h"
#include "HAL/Platform.h"
#include "Math/UnrealMathUtility.h"
#include "Sim/ShiftRng.h"
#include "Sim/ShiftSim.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

/** Things the presentation layers (HUD, board, audio, feed) react to. */
enum class EShiftEventType : uint8
{
	PlayerFlipped,       // a Plug or Sock flipped a node to the player
	PlayerReinforced,
	PlayerWeakened,
	PlayerGated,         // Value = gate progress / needed (0..1)
	PlayerLocked,
	PlayerNoPosts,
	PlayerRefused,       // shadowban, disqualified, night required, already owned
	PlayerOverheated,    // a sockpuppet that would take Heat to the cap was refused (free)
	PlayerRallied,       // a Rally went off: Node, Count = voters boosted
	PlayerRallyBlocked,  // a Rally was refused (free): Value = 0 on cooldown, 1 not the player's voter
	Strike,              // Value = strikes so far
	Disqualified,
	ModSweepClear,
	ModSweepHit,
	BlameTaken,          // Shift the Blame: the strike went to the rival
	ModTelegraph,        // the Mod Eye is about to sweep
	DirectorEvent,       // Node = kind (0 algorithm boost, 1 streamer discovers, 2 drama thread), NewOwner = entry, Value = size
	DayChanged,          // Value = new day index (0-based)
	PlayerLostVoter,     // a voter the player held went to someone else (not a strike revert): Node, NewOwner, Value = cause (0 opinion pressure, 1 the rival, 2 another entry)
	FinalAudit,          // the end-of-jam audit purged sockpuppet votes: Count = how many, Value = their share of all vote weight
	Finished,
	// appended in round 4 so the numbering of the earlier events stays
	FinalPushStart,      // the rival's final push begins: Value = seconds left, Share = the player's extra Post refill (0..1)
	PlayerParried,       // the player answered an announced attack: Node, PrevOwner = the attacker, Value = the share of the ring that was left (0..1)
	EyeAwayWarn,         // a Mod Eye window is coming: Value = seconds until it starts
	EyeAwayStart,        // the Mod Eye looks away: Value = how long
	EyeAwayEnd,          // the Mod Eye is back
	// appended in round 5 so the numbering of the earlier events stays
	BlameOffered,        // the strike that would disqualify the player can still be handed to the rival: Value = seconds the offer stays open, Share = the fake votes the strike took, Count = strikes
	BlameRefused,        // ... but too many fake votes to blame on anyone, the run ends now: Share = the fake votes the strike took
	// appended in round 8
	EntryTookBig,        // another entry took a hub or a celebrity from the rival by a plug (the player's own losses have their own event): Node, PrevOwner, NewOwner, Value = its vote weight

	// appended in round 14
	SpotlightStart,      // a streamer spotlights a voter: Node, Value = seconds it lasts
	SpotlightHit,        // the player flipped or backed it in time: Node, Value = Posts given back
	SpotlightEnd,        // it ran out unanswered: Node
};

/** A moment on the timeline of the results screen (the chart of every entry's share over the jam). */
struct FShiftTimelineMark
{
	int32 Step = 0;
	uint8 Kind = 0;       // 0 a Mod Eye strike, 1 a celebrity went to the player, 2 the rival's final push began
};

struct FShiftEvent
{
	EShiftEventType Type = EShiftEventType::DayChanged;
	int32 Node = -1;
	int32 PrevOwner = -1;
	int32 NewOwner = -1;
	bool bSock = false;
	float Value = 0.0f;
	int32 Count = 0;     // Strike: how many sockpuppet votes it reverted
	float Share = 0.0f;  // Strike: the share of all vote weight those votes were (0..1)
};

/** How the player's voters came and went during a run. QA and the results screen only; it never feeds back into the sim. */
struct FShiftFlipStats
{
	int32 Gains = 0;            // nodes that became the player's, any way (plug, sockpuppet, or followers coming around)
	int32 SockGains = 0;        // ... of which sockpuppet flips
	int32 Losses = 0;           // nodes the player lost to the dynamics or to an attack (the three counts below add up to it)
	int32 LossHerd = 0;         // ... voters that changed their mind under opinion pressure
	int32 LossRival = 0;        // ... voters the rival plugged away
	int32 LossRivalPush = 0;    // ... of which during the rival's final push
	int32 LossOthers = 0;       // ... voters another entry (or an ambient event) plugged away
	int32 StrikeReverts = 0;    // sockpuppet flips undone by a strike
	int32 Laundered = 0;        // sockpuppet votes that were backed until they became real
	int32 AuditPurges = 0;      // sockpuppet votes purged by the final audit
	double AuditPurgedShare = 0.0;   // ... and their share of all vote weight
	int32 PlugDwellCount = 0;   // flips that were lost again, split by how they were won, with the seconds they were held
	double PlugDwellSum = 0.0;
	int32 SockDwellCount = 0;
	double SockDwellSum = 0.0;
};

/** Who moved the votes during a run (QA only; it never feeds back into the sim): every change of owner by the owner class it left and the one it went to, for the votes an entry moved
 *  with a Plug, a Sock or an ambient event (Plug...) and for the votes that changed their mind under opinion pressure (Herd...). Classes: 0 the player, 1 the rival, 2 another entry,
 *  3 nobody (undecided). Strike reverts and the audit are left out. The weights are shares of all vote weight, summed over the changes. */
struct FShiftFlowStats
{
	int32 PlugMoves[4][4] = {};      // [class the voter left][class it went to]
	double PlugWeight[4][4] = {};
	int32 HerdMoves[4][4] = {};
	double HerdWeight[4][4] = {};
	int32 BigMoves[4][4] = {};       // the plug moves of voters worth Params::HighValueV or more (hubs, celebrities)
};

/** How Shift the Blame went during a run (QA and the balance table only). */
struct FShiftBlameStats
{
	int32 Strikes = 0;           // strikes of any number
	double StrikeFakeSum = 0.0;  // ... and the fake share each took, summed
	int32 Fatal = 0;             // strikes that would disqualify
	int32 Offered = 0;           // ... of which could be handed to the rival
	int32 Taken = 0;             // ... of which were
	int32 Refused = 0;           // ... of which had too many fake votes
	int32 Lapsed = 0;            // ... of which the player let the window run out on
	TArray<float> FatalFake;     // the fake share of every fatal strike
	TArray<float> FatalBasis;    // ... and the share the Blame's cap was measured on (the same, or all strikes added up: Params::BlameBasis)
};

/** The rival's and the sim's counters when the final push began, so the balance table can tell the push from the rest of the run (QA only). */
struct FShiftPushSnapshot
{
	bool bSet = false;
	int32 RivalAttacks = 0;
	int32 RivalDefends = 0;
	int32 RivalStarved = 0;
	ShiftSim::AttackCounters Threats;
};

/** Drives the behaviour layer (rival, other entries, jam director) for one fixed step; the StateTree runner implements this. */
class IShiftBehaviorDriver
{
public:
	virtual ~IShiftBehaviorDriver() = default;
	virtual void Tick(float DeltaTime) = 0;
};

/**
 * One jam run: the numeric sim plus the behaviour layer that drives the rival, the other entries and the Mod Eye, advanced in
 * fixed 20 Hz steps in the same order the balance harness used (behaviours, player actions, Step). Pure game logic, no UObjects.
 */
class FShiftRun
{
public:
	/** QA / balance only: overrides by field name (sim Params, JamDef or PolicyParams), applied by Start() after the build. Unknown names are logged as errors. */
	void SetOverrides(const TMap<FString, double>& InOverrides) { Overrides = InOverrides; }

	/** The build screen's forecast: seeds the sim's dynamics stream (the coin flips of undecided voters, and the bot's own jitter) apart from the world, so one world can be replayed
	 *  with other luck. Call before Start(); without it the dynamics are seeded from the run seed, as always. */
	void SetDynamicsSeed(uint32 InDynSeed) { DynSeed = InDynSeed; bHasDynSeed = true; }

	/** Builds the sim and the behaviours. The build (hours split on the build screen) tunes the player's side. Returns false if the sim could not be built. */
	bool Start(int32 InJamIndex, uint32 InSeed, const FShiftBuild& InBuild = FShiftBuild(), bool bInModsEnabled = true);

	/** Advances in fixed steps (bounded catch-up). Does nothing while paused or over. */
	void Tick(double DeltaSeconds);

	/** A player click on a node. Applied at the start of the next step; bSock = night-shift click. */
	void QueueAction(int32 Node, bool bSock);
	/** A Rally on a voter the player holds (right-click). Applied at the start of the next step. */
	void QueueRally(int32 Node);

	/** QA autoplay: a scripted bot plays the player's side (idle, random, greedy, greedy+sock). */
	void SetBot(ShiftSim::BotKind Kind);
	bool HasBot() const { return bBotActive; }
	/** What the bot did last, for the scripted cursor of the trailer footage (presentation only): how many actions so far, on which voter, and of which kind (0 plug, 1 sockpuppet, 2 Rally). */
	int32 BotActionCount() const { return bBotActive ? static_cast<int32>(Bot.Actions) : 0; }
	int32 BotLastNode() const { return static_cast<int32>(Bot.LastOutcome.node); }
	int32 BotLastKind() const { return Bot.LastOutcome.result == ShiftSim::ActionResult::Rallied ? 2 : (Bot.LastOutcome.sockpuppet ? 1 : 0); }
	/** Multiplies the simulated time per real second (QA fast-forward). */
	void SetTimeScale(double Scale) { TimeScale = FMath::Clamp(Scale, 0.1, 60.0); }

	/** Replaces the native stand-in policies with a driver (StateTrees). Without one the run uses the stand-ins directly. */
	void SetDriver(TSharedPtr<IShiftBehaviorDriver> InDriver) { Driver = InDriver; }
	bool HasDriver() const { return Driver.IsValid(); }

	// ---- hooks for the behaviour driver (the StateTree tasks) ----
	const ShiftSim::PolicyParams& GetPolicy() const { return Policy; }
	ShiftSim::StandInRival& RivalPolicy() { return Rival; }
	ShiftSim::Rng& DirectorRng() { return ModRng; }
	bool RivalFrozen() const { return Sim.IsValid() && Sim->Time() < RivalFrozenUntil; }
	/** The rival's Post bank now and its size (0 / 0 when the rule is off): the final push's pinned card shows it. */
	double RivalPosts() const { return Sim.IsValid() ? Rival.PostsNow(*Sim, Policy) : 0.0; }
	double RivalPostsMax() const { return Policy.RivalPostsMax; }
	void SetNextModSweep(double Time) { NextSweep = Time; }
	/** Heat at the cap: the mods strike at once. */
	void HeatGuard();
	/** The Mod Eye passes: strike if the player is in the red zone, then schedule the next sweep. */
	void ResolveModSweep();
	void EmitEvent(const FShiftEvent& Event) { Events.Add(Event); }

	/** Shift the Blame (Sim/ShiftBlameRules.h): the strike that would disqualify the player can be handed to the rival for a few seconds, once per run, when the fake votes it took
	 *  were few enough to pass for the rival's doing. Refused while paused. */
	bool TakeBlame();
	/** Guided lesson 3 (Shift the Blame): the strike the mods would give now, with the Blame offer it brings; and the offer held open while the lesson is read. */
	void StageFatalStrike() { ApplyModStrike(); }
	void HoldBlameOffer() { if (BlameRemaining > 0.0 && !bBlameUsed) { BlameRemaining = BlameWindowLength; } }
	bool BlameAvailable() const { return BlameRemaining > 0.0 && !bBlameUsed; }
	double BlameSecondsLeft() const { return BlameRemaining; }
	double BlameWindowSeconds() const { return BlameWindowLength; }
	/** The share the Blame's cap was measured on for the offered (or refused) strike, for the prompt: "6% fake votes (limit 8%)". */
	double BlameFakeShare() const { return BlameFake; }
	bool BlameUsed() const { return bBlameUsed; }
	const FShiftBlameStats& GetBlameStats() const { return BlameStats; }

	void SetNightHeld(bool bHeld) { if (!bReplaying) { bNightHeld = bHeld; } }
	bool IsNightHeld() const { return bNightHeld; }
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }
	bool IsPaused() const { return bPaused; }
	bool IsOver() const { return bOver; }

	void DrainEvents(TArray<FShiftEvent>& Out);

	bool IsValid() const { return Sim.IsValid(); }
	ShiftSim::Sim& GetSim() { return *Sim; }
	const ShiftSim::Sim& GetSim() const { return *Sim; }

	int32 GetJamIndex() const { return JamIndex; }
	uint32 GetSeed() const { return Seed; }
	bool ModsEnabled() const { return bModsEnabled; }
	/** Sim time of the next Mod Eye sweep (seconds). */
	double NextModSweep() const { return NextSweep; }
	/** Sim time the next Mod Eye window starts (1e30 while one is on or none is left). */
	/** The voter a streamer is spotlighting right now (-1 = none) and the seconds it has left. Only a person can take the bonus. */
	int32 SpotlightNode() const { return SpotNode; }
	double SpotlightRemaining() const { return SpotNode >= 0 && Sim.IsValid() ? FMath::Max(0.0, SpotEnd - Sim->Time()) : 0.0; }
	double NextEyeWindowStart() const { return (bEyeActive || !EyeWindowStart.IsValidIndex(EyeWindowNext)) ? 1.0e30 : EyeWindowStart[EyeWindowNext]; }
	/** True from the notice (EyeAwayWarn seconds ahead) until the window is over. */
	bool EyeWindowAnnounced() const { return bEyeWarned || bEyeActive; }

	const FShiftFlipStats& GetFlipStats() const { return FlipStats; }
	const FShiftFlowStats& GetFlowStats() const { return FlowStats; }

	// ---- recording and replay (Game/ShiftRecording.h) ----
	/** Starts recording this run (call right after Start(), for a human player: bots and scripted QA runs are not recorded unless asked). */
	void StartRecording();
	bool IsRecording() const { return bRecording; }
	/** Ends the recording with the result as it stands ("abandoned" for a run left unfinished); the run goes on. A finished recording stays readable through GetRecording(). */
	void FinishRecording(const TCHAR* Result);
	bool HasRecording() const { return bRecordingStarted; }
	const FShiftRecording& GetRecording() const { return Rec; }
	/** Replays a recording: after Start() with the recording's jam, seed and build, the recorded clicks are applied in the steps they were applied in, and the live controls do nothing. */
	void SetReplay(const FShiftRecording& Source);
	bool IsReplaying() const { return bReplaying; }
	/** A hash of every tuning value this run plays under (sim parameters, the jam, the behaviour policies): a recording made under other tuning replays differently, and says so. */
	FString TuningHash() const;
	const FShiftBuild& GetBuild() const { return RunBuild; }
	const FShiftPushSnapshot& GetPushStart() const { return PushStart; }
	/** Every entry's share every SampleEverySteps steps (5 s) from the first step to the last, for every run (a recording keeps its own copy), and the moments worth marking on it. */
	const TArray<FShiftRecSample>& GetHistory() const { return History; }
	const TArray<FShiftTimelineMark>& GetMarks() const { return Marks; }

private:
	void StepOnce();
	void InjectReplay();
	void UpdateEyeWindows();
	void TrackOwnership();
	void ModUpdate();
	void ApplyModStrike();
	void Emit(const FShiftEvent& Event) { Events.Add(Event); NoteMark(Event); }
	void NoteMark(const FShiftEvent& Event);
	void SampleHistory();

	TUniquePtr<ShiftSim::Sim> Sim;
	TSharedPtr<IShiftBehaviorDriver> Driver;
	ShiftSim::PolicyParams Policy;
	ShiftSim::StandInRival Rival;
	ShiftSim::StandInOthers Others;
	ShiftSim::PlayerBot Bot;
	ShiftSim::Rng ModRng;
	double TimeScale = 1.0;
	bool bBotActive = false;

	int32 JamIndex = 0;
	uint32 Seed = 0;
	uint32 DynSeed = 0;
	bool bHasDynSeed = false;
	double NextSweep = 0.0;
	double BlameRemaining = 0.0;
	double BlameWindowLength = 0.0;
	double BlameFake = 0.0;
	FShiftBlameStats BlameStats;
	double RivalFrozenUntil = 0.0;
	bool bBlameUsed = false;
	double Accumulator = 0.0;
	int32 LastDay = 0;
	bool bModsEnabled = true;
	bool bNightHeld = false;
	bool bPaused = false;
	bool bOver = false;

	struct FPendingAction
	{
		int32 Node = -1;
		bool bSock = false;
		bool bRally = false;
	};
	TArray<FPendingAction> Pending;
	TArray<FShiftEvent> Events;
	TArray<FShiftRecSample> History;
	TArray<FShiftTimelineMark> Marks;

	TMap<FString, double> Overrides;

	// ownership tracking for FShiftFlipStats
	FShiftFlipStats FlipStats;
	FShiftFlowStats FlowStats;

	// recording and replay
	FShiftBuild RunBuild;
	FShiftRecording Rec;
	bool bRecordingStarted = false;
	bool bRecording = false;
	bool bRecNight = false;
	bool bReplaying = false;
	bool bInjecting = false;
	TArray<FShiftRecEvent> ReplayEvents;
	int32 ReplayNext = 0;

	TArray<int32> StatOwner;        // owner of every node after the previous step
	TArray<double> StatGainTime;    // when the player last took the node
	TArray<uint8> StatGainSock;     // ... and whether it was a sockpuppet flip
	TArray<int32> RevertedNow;      // nodes a strike reverted during the current step
	TArray<int32> PurgedNow;        // nodes the final audit purged during the current step
	bool bAuditDone = false;
	bool bFinalPushAnnounced = false;
	FShiftPushSnapshot PushStart;

	// the Mod Eye looks away (round 4): windows scheduled at Start with their own RNG, announced ahead
	TArray<double> SpotStart;          // when each spotlight begins (its own stream, so nothing else is re-rolled)
	int32 SpotNext = 0;
	int32 SpotNode = -1;
	double SpotEnd = 0.0;
	ShiftSim::Rng SpotRng;
	void UpdateSpotlight();
	TArray<double> EyeWindowStart;
	int32 EyeWindowNext = 0;
	bool bEyeWarned = false;
	bool bEyeActive = false;
	double EyeWindowEnd = 0.0;
};
