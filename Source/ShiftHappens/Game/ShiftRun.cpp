// Shift Happens - Game Off 2026

#include "Game/ShiftRun.h"
#include "ShiftHappens.h"

bool FShiftRun::Start(int32 InJamIndex, uint32 InSeed, const FShiftBuild& InBuild, bool bInModsEnabled)
{
	JamIndex = FMath::Clamp(InJamIndex, 0, 2);
	Seed = InSeed;
	bModsEnabled = bInModsEnabled;

	ShiftSim::Params Params;
	ShiftSim::JamDef Jam = ShiftSim::DefaultJam(JamIndex);
	ShiftBuildRules::Apply(InBuild, Params, Jam);

	Sim = MakeUnique<ShiftSim::Sim>();
	if (!Sim->Init(Jam, Params, Seed))
	{
		UE_LOG(LogShift, Error, TEXT("FShiftRun: sim init failed (jam %d, seed %u)"), JamIndex, Seed);
		Sim.Reset();
		return false;
	}

	Rival.Reset(*Sim, Policy);
	Others.Reset(*Sim, Policy);
	ModRng.Seed(ShiftSim::Rng::Derive(Seed, 0x4D4F44u));
	NextSweep = ModRng.Uniform(Policy.ModFirstSweepMin, Jam.modIntervalMax);

	Accumulator = 0.0;
	LastDay = 0;
	BlameRemaining = 0.0;
	RivalFrozenUntil = 0.0;
	bBlameUsed = false;
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
	Bot.Reset(Kind, Seed);
}

void FShiftRun::QueueAction(int32 Node, bool bSock)
{
	if (Sim.IsValid() && !bOver && Pending.Num() < 16)
	{
		Pending.Add({ Node, bSock });
	}
}

void FShiftRun::DrainEvents(TArray<FShiftEvent>& Out)
{
	Out.Append(Events);
	Events.Reset();
}

void FShiftRun::ApplyModStrike()
{
	const ShiftSim::StrikeOutcome Strike = Sim->ApplyStrike();
	FShiftEvent Event;
	Event.Type = EShiftEventType::Strike;
	Event.Value = static_cast<float>(Strike.strikes);
	Emit(Event);
	if (!bBlameUsed)
	{
		BlameRemaining = BlameWindow;   // a third strike waits this long for the player to hand it to the rival
	}
	// Disqualification is announced once, at the end of StepOnce, when the sim reports it.
}

bool FShiftRun::TakeBlame()
{
	if (!Sim.IsValid() || !BlameAvailable())
	{
		return false;
	}
	bBlameUsed = true;
	BlameRemaining = 0.0;
	Sim->ForgiveStrike();
	Sim->AddConvictionToEntry(Sim->RivalId(), -0.25);       // the mods pick on the rival instead
	RivalFrozenUntil = Sim->Time() + 10.0;                  // and the rival is busy defending itself for a while
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
	if (Sim->InRedZone() && !Sim->Disqualified())
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

void FShiftRun::StepOnce()
{
	ShiftSim::Sim& S = *Sim;

	BlameRemaining = FMath::Max(0.0, BlameRemaining - S.GetParams().Dt);
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
		Bot.Update(S, Policy);   // the bot manages the night flag itself when it sockpuppets
	}
	else
	{
		S.SetNightHeld(bNightHeld);
	}
	for (const FPendingAction& Action : Pending)
	{
		const ShiftSim::ActionOutcome O = Action.bSock ? S.Sock(Action.Node) : S.Plug(ShiftSim::kPlayerId, Action.Node, 1.0);
		FShiftEvent Event;
		Event.Node = Action.Node;
		Event.PrevOwner = O.prevOwner;
		Event.NewOwner = O.newOwner;
		Event.bSock = Action.bSock;
		switch (O.result)
		{
		case ShiftSim::ActionResult::Flipped:
			Event.Type = EShiftEventType::PlayerFlipped;
			break;
		case ShiftSim::ActionResult::Reinforced:
			Event.Type = EShiftEventType::PlayerReinforced;
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
		default:
			Event.Type = EShiftEventType::PlayerRefused;
			break;
		}
		Emit(Event);
	}
	Pending.Reset();

	S.Step();

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
		FShiftEvent Event;
		Event.Type = S.Disqualified() ? EShiftEventType::Disqualified : EShiftEventType::Finished;
		Emit(Event);
	}
}
