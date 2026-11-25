// Shift Happens - Game Off 2026

#include "Game/ShiftRun.h"

#include "Behavior/ShiftPolicies.h"
#include "Containers/Array.h"
#include "Containers/StringConv.h"
#include "Containers/UnrealString.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/Platform.h"
#include "HAL/UnrealMemory.h"
#include "Logging/LogMacros.h"
#include "Math/UnrealMathUtility.h"
#include "ShiftBuild.h"
#include "ShiftHappens.h"
#include "ShiftRecording.h"
#include "Sim/ShiftBlameRules.h"
#include "Sim/ShiftParams.h"
#include "Sim/ShiftRng.h"
#include "Sim/ShiftSim.h"
#include <string>
#include "Templates/TupleFwd.h"
#include "Templates/UniquePtr.h"
#include <vector>

bool FShiftRun::Start(int32 InJamIndex, uint32 InSeed, const FShiftBuild& InBuild, bool bInModsEnabled)
{
	JamIndex = FMath::Clamp(InJamIndex, 0, ShiftSim::kJamCount - 1);
	Seed = InSeed;
	bModsEnabled = bInModsEnabled;

	ShiftSim::Params Params;
	ShiftSim::JamDef Jam;
	ShiftSim::SetupJam(JamIndex, Params, Jam);
	// QA overrides go in twice: before the build, so that its slopes (Params::Build*) can be tuned by name, and after it, so that an explicit value (-Set PostsMax=3) wins over what
	// the build made of it.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		if (Pass == 1)
		{
			ShiftBuildRules::Apply(InBuild, Params, Jam);
		}
		for (const TPair<FString, double>& Override : Overrides)
		{
			const std::string Name = TCHAR_TO_UTF8(*Override.Key);
			if (!ShiftSim::SetParam(Params, Name, Override.Value) && !ShiftSim::SetJamField(Jam, Name, Override.Value) && !ShiftSim::SetPolicyParam(Policy, Name, Override.Value) && Pass == 1)
			{
				UE_LOG(LogShift, Error, TEXT("FShiftRun: unknown override '%s'"), *Override.Key);
			}
		}
	}

	Sim = MakeUnique<ShiftSim::Sim>();
	if (!Sim->Init(Jam, Params, Seed, bHasDynSeed ? DynSeed : Seed))
	{
		UE_LOG(LogShift, Error, TEXT("FShiftRun: sim init failed (jam %d, seed %u)"), JamIndex, Seed);
		Sim.Reset();
		return false;
	}

	if (Policy.QaGiveCelebs > 0.5)
	{
		for (const int Celeb : Sim->GetGraph().celebrities)
		{
			Sim->SetNodeState(Celeb, ShiftSim::kPlayerId, 0.9, 0.0);
		}
	}
	RunBuild = InBuild;
	History.Reset();
	Marks.Reset();
	SampleHistory();
	Rec = FShiftRecording();
	bRecordingStarted = false;
	bRecording = false;
	bRecNight = false;
	bReplaying = false;
	bInjecting = false;
	ReplayEvents.Reset();
	ReplayNext = 0;
	FlipStats = FShiftFlipStats();
	StatOwner.SetNum(Sim->NodeCount());
	StatGainTime.Init(0.0, Sim->NodeCount());
	StatGainSock.Init(0, Sim->NodeCount());
	for (int32 i = 0; i < Sim->NodeCount(); ++i)
	{
		StatOwner[i] = Sim->Opinion(i);
	}
	RevertedNow.Reset();

	Rival.Reset(*Sim, Policy);
	Others.Reset(*Sim, Policy);
	ModRng.Seed(ShiftSim::Rng::Derive(Seed, 0x4D4F44u));
	NextSweep = ModRng.Uniform(Policy.ModFirstSweepMin, Jam.modIntervalMax);

	// The Mod Eye looks away: a few windows spread over the jam, jittered with their own stream (the sweeps' stream stays as it was), the last one ending well before the
	// final audit's review window so a burst cannot hide in it.
	EyeWindowStart.Reset();
	EyeWindowNext = 0;
	bEyeWarned = false;
	bEyeActive = false;
	EyeWindowEnd = 0.0;
	if (bModsEnabled && Policy.EyeAwayCount >= 0.5 && Policy.EyeAwaySeconds > 0.0)
	{
		ShiftSim::Rng EyeRng;
		EyeRng.Seed(ShiftSim::Rng::Derive(Seed, 0x455945u));
		const int32 Count = FMath::RoundToInt(static_cast<float>(Policy.EyeAwayCount));
		const double Total = Sim->TotalSeconds();
		for (int32 I = 0; I < Count; ++I)
		{
			const double Centre = Total * (I + 1) / (Count + 1);
			const double Jitter = EyeRng.Uniform(-0.12, 0.12) * Total / (Count + 1);
			const double Start = FMath::Clamp(Centre + Jitter - 0.5 * Policy.EyeAwaySeconds, 15.0, FMath::Max(15.0, Total - Policy.EyeAwayEndMargin - Policy.EyeAwaySeconds));
			EyeWindowStart.Add(Start);
		}
	}

	// Spotlights: a few moments spread over the middle of the jam, from their own stream
	SpotStart.Reset();
	SpotNext = 0;
	SpotNode = -1;
	SpotEnd = 0.0;
	SpotRng.Seed(ShiftSim::Rng::Derive(Seed, 0x53504Fu));
	if (Policy.SpotlightCount >= 0.5 && Policy.SpotlightSeconds > 0.0)
	{
		const int32 Count = FMath::RoundToInt(static_cast<float>(Policy.SpotlightCount));
		const double Total = Sim->TotalSeconds();
		for (int32 I = 0; I < Count; ++I)
		{
			const double Slot = (0.78 - 0.22) * Total / Count;
			SpotStart.Add(0.22 * Total + Slot * I + SpotRng.Uniform(0.1, 0.9) * Slot);
		}
	}

	Accumulator = 0.0;
	LastDay = 0;
	BlameRemaining = 0.0;
	BlameWindowLength = 0.0;
	BlameFake = 0.0;
	BlameStats = FShiftBlameStats();
	RivalFrozenUntil = 0.0;
	bBlameUsed = false;
	bFinalPushAnnounced = false;
	PushStart = FShiftPushSnapshot();
	bNightHeld = false;
	bPaused = false;
	bOver = false;
	Pending.Reset();
	Events.Reset();
	return true;
}

void FShiftRun::Tick(double DeltaSeconds)
{
	if (!Sim.IsValid() || bPaused || bOver)
	{
		return;
	}
	Accumulator += FMath::Min(DeltaSeconds * TimeScale, 0.25 * FMath::Max(1.0, TimeScale));
	const double Dt = Sim->GetParams().Dt;
	int32 Guard = 0;
	const int32 MaxSteps = 12 + FMath::CeilToInt(static_cast<float>(TimeScale)) * 6;
	while (Accumulator + 1.0e-9 >= Dt && Guard++ < MaxSteps && !bOver)
	{
		StepOnce();
		Accumulator -= Dt;
	}
}

void FShiftRun::SetBot(ShiftSim::BotKind Kind)
{
	bBotActive = Kind != ShiftSim::BotKind::Idle;
	Bot.Reset(Kind, bHasDynSeed ? DynSeed : Seed);
	Bot.SetStart(Policy.BotStart);
}

void FShiftRun::QueueAction(int32 Node, bool bSock)
{
	if (Sim.IsValid() && !bOver && !bReplaying && Pending.Num() < 16)
	{
		Pending.Add({ Node, bSock, false });
	}
}

void FShiftRun::QueueRally(int32 Node)
{
	if (Sim.IsValid() && !bOver && !bReplaying && Pending.Num() < 16)
	{
		Pending.Add({ Node, false, true });
	}
}

void FShiftRun::DrainEvents(TArray<FShiftEvent>& Out)
{
	Out.Append(Events);
	Events.Reset();
}

void FShiftRun::ApplyModStrike()
{
	const double FakeShareBefore = Sim->SockBoughtWeight() / FMath::Max(1.0, Sim->TotalWeight());
	const ShiftSim::StrikeOutcome Strike = Sim->ApplyStrike();
	for (const int Node : Strike.revertedNodes)
	{
		RevertedNow.Add(Node);
	}
	FShiftEvent Event;
	Event.Type = EShiftEventType::Strike;
	Event.Value = static_cast<float>(Strike.strikes);
	Event.Count = static_cast<int32>(Strike.revertedNodes.size());
	Event.Share = static_cast<float>(FakeShareBefore);
	Emit(Event);

	// Shift the Blame is for the strike that would disqualify, and only for a few fake votes (Sim/ShiftBlameRules.h); the strikes before it keep every consequence.
	const double EarlierFake = BlameStats.StrikeFakeSum;     // the fake share of the strikes before this one
	++BlameStats.Strikes;
	BlameStats.StrikeFakeSum += FakeShareBefore;
	const double BlameShare = ShiftSim::BlameBasisShare(Sim->GetParams(), FakeShareBefore, EarlierFake);
	if (Strike.disqualified)
	{
		++BlameStats.Fatal;
		BlameStats.FatalFake.Add(static_cast<float>(FakeShareBefore));
		BlameStats.FatalBasis.Add(static_cast<float>(BlameShare));
	}
	const ShiftSim::BlameVerdict Verdict = ShiftSim::JudgeBlame(*Sim, BlameShare, bBlameUsed);
	const double Window = ShiftSim::BlameWindowFor(*Sim);
	if (Verdict == ShiftSim::BlameVerdict::Offered && Window > 1.0e-9)
	{
		++BlameStats.Offered;
		BlameFake = BlameShare;
		BlameWindowLength = Window;
		BlameRemaining = Window;
		FShiftEvent Offer;
		Offer.Type = EShiftEventType::BlameOffered;
		Offer.Value = static_cast<float>(Window);
		Offer.Share = static_cast<float>(BlameShare);
		Offer.Count = Strike.strikes;
		Emit(Offer);
	}
	else if (Verdict == ShiftSim::BlameVerdict::Refused)
	{
		++BlameStats.Refused;
		BlameFake = BlameShare;
		FShiftEvent Refusal;
		Refusal.Type = EShiftEventType::BlameRefused;
		Refusal.Share = static_cast<float>(BlameShare);
		Refusal.Count = Strike.strikes;
		Emit(Refusal);
	}
	// Disqualification is announced once, at the end of StepOnce, when the sim reports it: at once when nothing was offered, when the window has run out otherwise.
}

bool FShiftRun::TakeBlame()
{
	if (!Sim.IsValid() || bPaused || bOver || !BlameAvailable() || (bReplaying && !bInjecting))
	{
		return false;
	}
	if (bRecording && !bBotActive)
	{
		Rec.Events.Add({ static_cast<int32>(Sim->StepCount()), TEXT('B'), -1 });
	}
	bBlameUsed = true;
	BlameRemaining = 0.0;
	++BlameStats.Taken;
	RivalFrozenUntil = Sim->Time() + ShiftSim::ApplyBlame(*Sim);   // the mods pick on the rival instead, and the rival is busy defending itself for a while
	FShiftEvent Event;
	Event.Type = EShiftEventType::BlameTaken;
	Emit(Event);
	return true;
}

void FShiftRun::HeatGuard()
{
	if (bModsEnabled && Sim->HeatMaxed() && !Sim->Disqualified())
	{
		ApplyModStrike();
	}
}

void FShiftRun::ResolveModSweep()
{
	if (!bModsEnabled)
	{
		return;
	}
	const ShiftSim::JamDef& Jam = Sim->Jam();
	NextSweep += ModRng.Uniform(Jam.modIntervalMin, Jam.modIntervalMax);
	FShiftEvent Event;
	if (Sim->Suspicious() && !Sim->Disqualified())
	{
		Event.Type = EShiftEventType::ModSweepHit;
		Emit(Event);
		ApplyModStrike();
	}
	else
	{
		Event.Type = EShiftEventType::ModSweepClear;
		Emit(Event);
	}
}

void FShiftRun::ModUpdate()
{
	HeatGuard();
	if (Sim->Time() + 1.0e-9 >= NextSweep)
	{
		ResolveModSweep();
	}
}

void FShiftRun::TrackOwnership()
{
	const ShiftSim::Sim& S = *Sim;
	for (int32 i = 0; i < S.NodeCount(); ++i)
	{
		const int32 Now = S.Opinion(i);
		const int32 Was = StatOwner[i];
		if (Now == Was)
		{
			if (Now == ShiftSim::kPlayerId && StatGainSock[i] != 0 && !S.IsSockpuppet(i))
			{
				++FlipStats.Laundered;   // still the player's, no longer flagged: it was backed until it became a real vote
				StatGainSock[i] = 0;
			}
			continue;
		}
		StatOwner[i] = Now;
		if (!RevertedNow.Contains(i) && !PurgedNow.Contains(i))
		{
			// who moved the vote: an entry's action, or opinion pressure (QA only)
			const int32 Flipper = S.LastFlipper(i);
			if (Flipper >= 0 || Flipper == ShiftSim::kFlipNatural)
			{
				const auto OwnerClass = [&S](int32 Owner) { return Owner < 0 ? 3 : (Owner == ShiftSim::kPlayerId ? 0 : (Owner == S.RivalId() ? 1 : 2)); };
				const double Weight = S.GetGraph().nodes[static_cast<size_t>(i)].weight / FMath::Max(1.0, S.TotalWeight());
				if (Flipper == ShiftSim::kFlipNatural)
				{
					++FlowStats.HerdMoves[OwnerClass(Was)][OwnerClass(Now)];
					FlowStats.HerdWeight[OwnerClass(Was)][OwnerClass(Now)] += Weight;
				}
				else
				{
					++FlowStats.PlugMoves[OwnerClass(Was)][OwnerClass(Now)];
					FlowStats.PlugWeight[OwnerClass(Was)][OwnerClass(Now)] += Weight;
					if (S.GetGraph().nodes[static_cast<size_t>(i)].weight >= S.GetParams().HighValueV)
					{
						++FlowStats.BigMoves[OwnerClass(Was)][OwnerClass(Now)];
						if (OwnerClass(Now) == 2 && OwnerClass(Was) == 1)
						{
							// another entry took a hub or a celebrity from the rival: the board should say so (a skirmish between two nobodies, and the player's own losses, have no note of this kind)
							FShiftEvent Big;
							Big.Type = EShiftEventType::EntryTookBig;
							Big.Node = i;
							Big.PrevOwner = Was;
							Big.NewOwner = Now;
							Big.Value = static_cast<float>(S.GetGraph().nodes[static_cast<size_t>(i)].weight);
							Emit(Big);
						}
					}
				}
			}
		}
		if (Now == ShiftSim::kPlayerId)
		{
			const bool bSock = S.IsSockpuppet(i);
			StatGainSock[i] = bSock ? 1 : 0;
			StatGainTime[i] = S.Time();
			++FlipStats.Gains;
			FlipStats.SockGains += bSock ? 1 : 0;
		}
		else if (Was == ShiftSim::kPlayerId)
		{
			const double Dwell = S.Time() - StatGainTime[i];
			if (StatGainSock[i] != 0)
			{
				FlipStats.SockDwellSum += Dwell;
				++FlipStats.SockDwellCount;
			}
			else
			{
				FlipStats.PlugDwellSum += Dwell;
				++FlipStats.PlugDwellCount;
			}
			if (RevertedNow.Contains(i))
			{
				++FlipStats.StrikeReverts;
			}
			else if (PurgedNow.Contains(i))
			{
				// counted as an audit purge when the audit ran
			}
			else
			{
				++FlipStats.Losses;
				const int32 Flipper = S.LastFlipper(i);
				const int32 Cause = Flipper == ShiftSim::kFlipNatural ? 0 : (Flipper == S.RivalId() ? 1 : 2);
				(Cause == 0 ? FlipStats.LossHerd : (Cause == 1 ? FlipStats.LossRival : FlipStats.LossOthers))++;
				FlipStats.LossRivalPush += (Cause == 1 && bFinalPushAnnounced) ? 1 : 0;
				FShiftEvent Event;
				Event.Value = static_cast<float>(Cause);
				Event.Type = EShiftEventType::PlayerLostVoter;
				Event.Node = i;
				Event.PrevOwner = Was;
				Event.NewOwner = Now;
				Emit(Event);
			}
		}
	}
	RevertedNow.Reset();
	PurgedNow.Reset();
}

/** Starts, announces and ends the Mod Eye's windows. A sweep that would fall inside a window (or just after it) waits until 3.5 s after it. */
void FShiftRun::UpdateSpotlight()
{
	ShiftSim::Sim& S = *Sim;
	const double Now = S.Time();
	if (SpotNode >= 0 && Now + 1.0e-9 >= SpotEnd)
	{
		FShiftEvent End;
		End.Type = EShiftEventType::SpotlightEnd;
		End.Node = SpotNode;
		Emit(End);
		SpotNode = -1;
	}
	if (SpotNode >= 0 || !SpotStart.IsValidIndex(SpotNext) || Now + 1.0e-9 < SpotStart[SpotNext])
	{
		return;
	}
	++SpotNext;
	// a voter anyone could win over: not the player's, not a hub or a celebrity, no gate in the way
	TArray<int32> Candidates;
	for (int32 I = 0; I < S.NodeCount(); ++I)
	{
		if (S.Opinion(I) != ShiftSim::kPlayerId && !S.GetGraph().IsGateNode(I) && !S.IsGated(I) && S.LockRemaining(I) <= 0.0)
		{
			Candidates.Add(I);
		}
	}
	if (Candidates.Num() == 0)
	{
		return;
	}
	SpotNode = Candidates[FMath::Clamp(static_cast<int32>(SpotRng.Uniform(0.0, static_cast<double>(Candidates.Num()))), 0, Candidates.Num() - 1)];
	SpotEnd = Now + Policy.SpotlightSeconds;
	FShiftEvent Start;
	Start.Type = EShiftEventType::SpotlightStart;
	Start.Node = SpotNode;
	Start.Value = static_cast<float>(Policy.SpotlightSeconds);
	Emit(Start);
}

void FShiftRun::UpdateEyeWindows()
{
	ShiftSim::Sim& S = *Sim;
	const double Now = S.Time();
	if (bEyeActive && Now + 1.0e-9 >= EyeWindowEnd)
	{
		bEyeActive = false;
		bEyeWarned = false;
		++EyeWindowNext;
		FShiftEvent End;
		End.Type = EShiftEventType::EyeAwayEnd;
		Emit(End);
	}
	if (bEyeActive || !EyeWindowStart.IsValidIndex(EyeWindowNext))
	{
		return;
	}
	const double Start = EyeWindowStart[EyeWindowNext];
	if (!bEyeWarned && Now + 1.0e-9 >= Start - Policy.EyeAwayWarn)
	{
		bEyeWarned = true;
		if (NextSweep > Start - 1.0e-9 && NextSweep < Start + Policy.EyeAwaySeconds + 3.5)
		{
			NextSweep = Start + Policy.EyeAwaySeconds + 3.5;
		}
		FShiftEvent Warn;
		Warn.Type = EShiftEventType::EyeAwayWarn;
		Warn.Value = static_cast<float>(FMath::Max(0.0, Start - Now));
		Emit(Warn);
	}
	if (Now + 1.0e-9 >= Start)
	{
		bEyeActive = true;
		EyeWindowEnd = Start + Policy.EyeAwaySeconds;
		S.SetEyeAway(Policy.EyeAwaySeconds);
		FShiftEvent Go;
		Go.Type = EShiftEventType::EyeAwayStart;
		Go.Value = static_cast<float>(Policy.EyeAwaySeconds);
		Emit(Go);
	}
}

void FShiftRun::StepOnce()
{
	ShiftSim::Sim& S = *Sim;

	if (bReplaying)
	{
		InjectReplay();   // the recorded clicks of this step, where the live ones would have been queued
	}
	if (bRecording && !bBotActive && bNightHeld != bRecNight)
	{
		Rec.Events.Add({ static_cast<int32>(S.StepCount()), bNightHeld ? TEXT('N') : TEXT('n'), -1 });
		bRecNight = bNightHeld;
	}

	const bool bOfferWasOpen = BlameRemaining > 0.0;
	BlameRemaining = FMath::Max(0.0, BlameRemaining - S.GetParams().Dt);
	if (bOfferWasOpen && BlameRemaining <= 0.0 && !bBlameUsed)
	{
		++BlameStats.Lapsed;   // the player let the window run out
	}
	UpdateEyeWindows();
	UpdateSpotlight();
	if (Driver.IsValid())
	{
		Driver->Tick(static_cast<float>(S.GetParams().Dt));   // StateTrees: rival, other entries, jam director
	}
	else
	{
		if (S.Time() >= RivalFrozenUntil)
		{
			Rival.Update(S, Policy);
		}
		Others.Update(S, Policy);
		if (bModsEnabled)
		{
			ModUpdate();
		}
	}

	if (bBotActive)
	{
		Bot.Update(S, Policy, bModsEnabled ? NextSweep : 1.0e30, NextEyeWindowStart());   // the bot manages the night flag itself when it sockpuppets
	}
	else
	{
		S.SetNightHeld(bNightHeld);
	}
	// A bot that would be disqualified answers with Shift the Blame once it would have seen the prompt, like a player who did (PolicyParams::BotBlame 0 = never).
	if (bBotActive && Policy.BotBlame > 0.5 && BlameAvailable() && BlameWindowLength - BlameRemaining >= Policy.ReactDelay - 1.0e-9)
	{
		TakeBlame();
	}
	for (const FPendingAction& Action : Pending)
	{
		if (bRecording && !bBotActive)
		{
			Rec.Events.Add({ static_cast<int32>(S.StepCount()), Action.bRally ? TEXT('R') : (Action.bSock ? TEXT('S') : TEXT('P')), Action.Node });
		}
		// A click on one of your own voters is a reinforcement, day or night: a sockpuppet on your own voter would only be refused.
		const bool bSockClick = Action.bSock && !(Action.Node >= 0 && Action.Node < S.NodeCount() && S.Opinion(Action.Node) == ShiftSim::kPlayerId);
		const ShiftSim::ActionOutcome O = Action.bRally ? S.Rally(Action.Node) : (bSockClick ? S.Sock(Action.Node) : S.Plug(ShiftSim::kPlayerId, Action.Node, 1.0));
		FShiftEvent Event;
		Event.Node = Action.Node;
		Event.PrevOwner = O.prevOwner;
		Event.NewOwner = O.newOwner;
		Event.bSock = bSockClick;
		switch (O.result)
		{
		case ShiftSim::ActionResult::Flipped:
			Event.Type = EShiftEventType::PlayerFlipped;
			break;
		case ShiftSim::ActionResult::Reinforced:
			Event.Type = O.parried ? EShiftEventType::PlayerParried : EShiftEventType::PlayerReinforced;
			if (O.parried)
			{
				Event.PrevOwner = O.parriedWho;
				Event.Value = O.parriedTotal > 1.0e-9 ? static_cast<float>(O.parriedLeft / O.parriedTotal) : 0.0f;
			}
			break;
		case ShiftSim::ActionResult::Weakened:
			Event.Type = EShiftEventType::PlayerWeakened;
			break;
		case ShiftSim::ActionResult::Gated:
			Event.Type = EShiftEventType::PlayerGated;
			Event.Value = O.gateNeeded > 1.0e-9 ? static_cast<float>(FMath::Clamp(O.gateProgress / O.gateNeeded, 0.0, 1.0)) : 0.0f;
			break;
		case ShiftSim::ActionResult::Locked:
			Event.Type = EShiftEventType::PlayerLocked;
			break;
		case ShiftSim::ActionResult::NoPosts:
			Event.Type = EShiftEventType::PlayerNoPosts;
			break;
		case ShiftSim::ActionResult::Overheated:
			Event.Type = EShiftEventType::PlayerOverheated;
			break;
		case ShiftSim::ActionResult::Rallied:
			Event.Type = EShiftEventType::PlayerRallied;
			Event.Count = O.touched;
			break;
		case ShiftSim::ActionResult::OnCooldown:
		case ShiftSim::ActionResult::NotYours:
			Event.Type = EShiftEventType::PlayerRallyBlocked;
			Event.Value = O.result == ShiftSim::ActionResult::OnCooldown ? 0.0f : 1.0f;
			break;
		default:
			Event.Type = EShiftEventType::PlayerRefused;
			break;
		}
		Emit(Event);
		// a person who flips or backs the spotlighted voter in time gets Posts back (a bot's actions do not come through here)
		if (SpotNode >= 0 && Action.Node == SpotNode && !bBotActive && !bSockClick
			&& (Event.Type == EShiftEventType::PlayerFlipped || Event.Type == EShiftEventType::PlayerReinforced || Event.Type == EShiftEventType::PlayerParried))
		{
			S.SetPosts(S.Posts() + Policy.SpotlightBonusPosts);
			FShiftEvent Hit;
			Hit.Type = EShiftEventType::SpotlightHit;
			Hit.Node = SpotNode;
			Hit.Value = static_cast<float>(Policy.SpotlightBonusPosts);
			Emit(Hit);
			SpotNode = -1;
		}
	}
	Pending.Reset();

	// The rival's final push (the last MegaDays days) is answered: the player's Posts refill faster for the same days, and the start is announced once.
	{
		const int32 PushDays = FMath::RoundToInt(static_cast<float>(Policy.MegaDays));
		const bool bPush = Policy.FinalPushRegen > 1.0 + 1.0e-9 && PushDays > 0 && S.DayIndex() >= S.Jam().days - PushDays;
		S.SetPostsRegen(S.Jam().regen * (bPush ? Policy.FinalPushRegen : 1.0));
		if (bPush && !bFinalPushAnnounced)
		{
			bFinalPushAnnounced = true;
			PushStart.bSet = true;
			PushStart.RivalAttacks = Rival.Attacks;
			PushStart.RivalDefends = Rival.Defends;
			PushStart.RivalStarved = Rival.Starved;
			PushStart.Threats = S.Attacks();
			FShiftEvent Push;
			Push.Type = EShiftEventType::FinalPushStart;
			Push.Value = static_cast<float>(S.TotalSeconds() - S.Time());
			Push.Share = static_cast<float>(Policy.FinalPushRegen - 1.0);
			Emit(Push);
		}
	}

	S.Step();
	if (S.StepCount() % ShiftRecordingInfo::SampleEverySteps == 0)
	{
		SampleHistory();
	}
	if (bRecording && S.StepCount() % ShiftRecordingInfo::SampleEverySteps == 0)
	{
		FShiftRecSample Sample;
		Sample.Step = static_cast<int32>(S.StepCount());
		Sample.Posts = static_cast<float>(S.Posts());
		Sample.Heat = static_cast<float>(S.Heat());
		for (int32 E = 0; E < S.NumEntries(); ++E)
		{
			Sample.Shares.Add(static_cast<float>(S.Share(E)));
		}
		Rec.Samples.Add(Sample);
	}
	if (S.Finished() && !bAuditDone && S.GetParams().FinalAudit > 0.5)
	{
		// The final audit: before the tally is read, the mods purge every sockpuppet vote that nobody backed (no strike, no shadowban).
		bAuditDone = true;
		const double Total = FMath::Max(1.0, S.TotalWeight());
		const double PurgedWeight = S.AuditWeight();
		const std::vector<int> Purged = S.AuditSockpuppets();
		for (const int Node : Purged)
		{
			PurgedNow.Add(Node);
		}
		FlipStats.AuditPurges += static_cast<int32>(Purged.size());
		FlipStats.AuditPurgedShare += PurgedWeight / Total;
		FShiftEvent Event;
		Event.Type = EShiftEventType::FinalAudit;
		Event.Count = static_cast<int32>(Purged.size());
		Event.Value = static_cast<float>(PurgedWeight / Total);
		Emit(Event);
	}
	TrackOwnership();

	const int32 Day = S.DayIndex();
	if (Day != LastDay)
	{
		LastDay = Day;
		FShiftEvent Event;
		Event.Type = EShiftEventType::DayChanged;
		Event.Value = static_cast<float>(Day);
		Emit(Event);
	}

	const bool bBlamePending = S.Disqualified() && BlameAvailable();   // a last-moment Shift the Blame can still save the run
	if ((S.Disqualified() && !bBlamePending) || S.Finished())
	{
		bOver = true;
		SampleHistory();   // the last point of the timeline is the final tally
		FShiftEvent Event;
		Event.Type = S.Disqualified() ? EShiftEventType::Disqualified : EShiftEventType::Finished;
		Emit(Event);
		if (bRecording)
		{
			FinishRecording(S.Disqualified() ? TEXT("disqualified") : (S.Leader() == ShiftSim::kPlayerId ? TEXT("won") : TEXT("lost")));
		}
	}
}

void FShiftRun::SampleHistory()
{
	if (!Sim.IsValid())
	{
		return;
	}
	const ShiftSim::Sim& S = *Sim;
	FShiftRecSample Sample;
	Sample.Step = static_cast<int32>(S.StepCount());
	Sample.Posts = static_cast<float>(S.Posts());
	Sample.Heat = static_cast<float>(S.Heat());
	for (int32 E = 0; E < S.NumEntries(); ++E)
	{
		Sample.Shares.Add(static_cast<float>(S.Share(E)));
	}
	if (History.Num() > 0 && History.Last().Step == Sample.Step)
	{
		History.Last() = MoveTemp(Sample);
	}
	else
	{
		History.Add(MoveTemp(Sample));
	}
}

void FShiftRun::NoteMark(const FShiftEvent& Event)
{
	if (!Sim.IsValid())
	{
		return;
	}
	FShiftTimelineMark Mark;
	Mark.Step = static_cast<int32>(Sim->StepCount());
	switch (Event.Type)
	{
	case EShiftEventType::Strike:
		Mark.Kind = 0;
		break;
	case EShiftEventType::FinalPushStart:
		Mark.Kind = 2;
		break;
	case EShiftEventType::PlayerFlipped:
	{
		const std::vector<int>& Celebs = Sim->GetGraph().celebrities;
		if (std::find(Celebs.begin(), Celebs.end(), Event.Node) == Celebs.end())
		{
			return;
		}
		Mark.Kind = 1;
		break;
	}
	default:
		return;
	}
	Marks.Add(Mark);
}

void FShiftRun::StartRecording()
{
	if (!Sim.IsValid() || bReplaying)
	{
		return;
	}
	Rec = FShiftRecording();
	Rec.Tag = ShiftRecordingInfo::BuildTag;
	Rec.Tuning = TuningHash();
	Rec.Jam = JamIndex;
	Rec.Seed = Seed;
	Rec.Quality = RunBuild.Quality;
	Rec.Thumbnail = RunBuild.Thumbnail;
	Rec.Network = RunBuild.Network;
	bRecordingStarted = true;
	bRecording = true;
	bRecNight = false;
	FShiftRecSample First;
	First.Step = static_cast<int32>(Sim->StepCount());
	First.Posts = static_cast<float>(Sim->Posts());
	First.Heat = static_cast<float>(Sim->Heat());
	for (int32 E = 0; E < Sim->NumEntries(); ++E)
	{
		First.Shares.Add(static_cast<float>(Sim->Share(E)));
	}
	Rec.Samples.Add(First);
}

void FShiftRun::FinishRecording(const TCHAR* Result)
{
	if (!bRecording || !Sim.IsValid())
	{
		return;
	}
	const ShiftSim::Sim& S = *Sim;
	bRecording = false;
	Rec.Result = Result;
	Rec.EndStep = static_cast<int32>(S.StepCount());
	Rec.EndTime = S.Time();
	Rec.Rank = S.RankOf(ShiftSim::kPlayerId) + 1;
	Rec.Strikes = S.Strikes();
	Rec.Hash = FString::Printf(TEXT("%016llx"), static_cast<unsigned long long>(S.StateHash()));
	Rec.FinalShares.Reset();
	for (int32 E = 0; E < S.NumEntries(); ++E)
	{
		Rec.FinalShares.Add(static_cast<float>(S.Share(E)));
	}
}

void FShiftRun::SetReplay(const FShiftRecording& Source)
{
	ReplayEvents = Source.Events;
	ReplayNext = 0;
	bReplaying = true;
	bRecording = false;
	bRecordingStarted = false;
}

void FShiftRun::InjectReplay()
{
	const int32 Step = static_cast<int32>(Sim->StepCount());
	while (ReplayEvents.IsValidIndex(ReplayNext) && ReplayEvents[ReplayNext].Step <= Step)
	{
		const FShiftRecEvent& E = ReplayEvents[ReplayNext++];
		switch (E.Type)
		{
		case TEXT('P'): Pending.Add({ E.Node, false, false }); break;
		case TEXT('S'): Pending.Add({ E.Node, true, false }); break;
		case TEXT('R'): Pending.Add({ E.Node, false, true }); break;
		case TEXT('N'): bNightHeld = true; break;
		case TEXT('n'): bNightHeld = false; break;
		case TEXT('B'):
			bInjecting = true;
			TakeBlame();
			bInjecting = false;
			break;
		default: break;
		}
	}
}

FString FShiftRun::TuningHash() const
{
	if (!Sim.IsValid())
	{
		return FString();
	}
	uint64 H = 1469598103934665603ull;
	const auto Mix = [&H](double V)
		{
			uint64 Bits = 0;
			FMemory::Memcpy(&Bits, &V, sizeof(Bits));
			for (int32 I = 0; I < 8; ++I)
			{
				H ^= (Bits >> (8 * I)) & 0xFFull;
				H *= 1099511628211ull;
			}
		};
	const ShiftSim::Params& P = Sim->GetParams();
	for (int32 I = 0; I < ShiftSim::ParamCount(); ++I)
	{
		double V = 0.0;
		if (ShiftSim::GetParam(P, ShiftSim::ParamNameAt(I), &V))
		{
			Mix(V);
		}
	}
	for (int32 I = 0; I < ShiftSim::PolicyParamCount(); ++I)
	{
		double V = 0.0;
		if (ShiftSim::GetPolicyParam(Policy, ShiftSim::PolicyParamNameAt(I), &V))
		{
			Mix(V);
		}
	}
	const ShiftSim::JamDef& J = Sim->Jam();
	Mix(J.nodes); Mix(J.communities); Mix(J.bridges); Mix(J.celebs); Mix(J.entries); Mix(J.days); Mix(J.secPerDay); Mix(J.regen); Mix(J.localGate); Mix(J.celebGate);
	Mix(J.rivalId); Mix(J.celebExtraFollowers); Mix(J.rivalFollowerFraction); Mix(J.rivalInterval); Mix(J.modIntervalMin); Mix(J.modIntervalMax);
	return FString::Printf(TEXT("%016llx"), static_cast<unsigned long long>(H));
}
