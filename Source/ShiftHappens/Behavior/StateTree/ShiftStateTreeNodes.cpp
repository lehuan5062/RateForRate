// Shift Happens - Game Off 2026

#include "Behavior/StateTree/ShiftStateTreeNodes.h"
#include "Behavior/ShiftPolicies.h"
#include "Game/ShiftRun.h"

namespace
{
	UShiftBehaviorContext& Ctx(FStateTreeExecutionContext& Context, const TStateTreeExternalDataHandle<UShiftBehaviorContext>& Handle)
	{
		return Context.GetExternalData(Handle);
	}

	/** Seconds between the rival's actions; the Mega Campaign doubles the actions per round, so each action takes a share of the interval. */
	double RivalStep(const ShiftSim::Sim& S, const ShiftSim::PolicyParams& PP)
	{
		return ShiftSim::StandInRival::IntervalFor(S, PP) / static_cast<double>(FMath::Max(1, ShiftSim::StandInRival::ActionsPerRound(S, PP)));
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Rival
// ---------------------------------------------------------------------------------------------------------------------------------

EStateTreeRunStatus FShiftTask_RivalWait::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	UShiftBehaviorContext& C = Ctx(Context, ContextHandle);
	const ShiftSim::Sim& S = C.Run->GetSim();
	if (C.Run->RivalFrozen())
	{
		// Shift the Blame: the rival is busy defending itself; its schedule slides instead of bursting afterwards.
		C.NextTime = S.Time() + ShiftSim::StandInRival::IntervalFor(S, C.Run->GetPolicy());
		return EStateTreeRunStatus::Running;
	}
	return S.Time() + 1.0e-9 >= C.NextTime ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FShiftTask_RivalMega::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	UShiftBehaviorContext& C = Ctx(Context, ContextHandle);
	C.Run->RivalPolicy().MegaPulse(C.Run->GetSim(), C.Run->GetPolicy());
	return EStateTreeRunStatus::Succeeded;
}

EStateTreeRunStatus FShiftTask_RivalDefend::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	UShiftBehaviorContext& C = Ctx(Context, ContextHandle);
	ShiftSim::Sim& S = C.Run->GetSim();
	const ShiftSim::PolicyParams& PP = C.Run->GetPolicy();
	C.Run->RivalPolicy().Defend(S, PP);
	C.NextTime += RivalStep(S, PP);
	return EStateTreeRunStatus::Succeeded;
}

EStateTreeRunStatus FShiftTask_RivalAttack::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	UShiftBehaviorContext& C = Ctx(Context, ContextHandle);
	ShiftSim::Sim& S = C.Run->GetSim();
	const ShiftSim::PolicyParams& PP = C.Run->GetPolicy();
	C.Run->RivalPolicy().Attack(S, PP);
	C.NextTime += RivalStep(S, PP);
	return EStateTreeRunStatus::Succeeded;
}

bool FShiftCondition_RivalMegaPending::TestCondition(FStateTreeExecutionContext& Context) const
{
	UShiftBehaviorContext& C = Ctx(Context, ContextHandle);
	return C.Run->RivalPolicy().MegaPending(C.Run->GetSim(), C.Run->GetPolicy());
}

bool FShiftCondition_RivalHasWeakHub::TestCondition(FStateTreeExecutionContext& Context) const
{
	UShiftBehaviorContext& C = Ctx(Context, ContextHandle);
	return ShiftSim::StandInRival::HasWeakHub(C.Run->GetSim(), C.Run->GetPolicy());
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Other entries
// ---------------------------------------------------------------------------------------------------------------------------------

EStateTreeRunStatus FShiftTask_OtherWait::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	UShiftBehaviorContext& C = Ctx(Context, ContextHandle);
	return C.Run->GetSim().Time() + 1.0e-9 >= C.NextTime ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FShiftTask_OtherAct::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	UShiftBehaviorContext& C = Ctx(Context, ContextHandle);
	const ShiftSim::PolicyParams& PP = C.Run->GetPolicy();
	ShiftSim::StandInOthers::Act(C.Run->GetSim(), PP, C.EntryId);
	C.NextTime += PP.OthersInterval;
	return EStateTreeRunStatus::Succeeded;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Mod Eye
// ---------------------------------------------------------------------------------------------------------------------------------

EStateTreeRunStatus FShiftTask_ModHeatGuard::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	Ctx(Context, ContextHandle).Run->HeatGuard();
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FShiftTask_ModIdle::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	UShiftBehaviorContext& C = Ctx(Context, ContextHandle);
	if (!C.Run->ModsEnabled())
	{
		return EStateTreeRunStatus::Running;   // this jam run has no mods
	}
	return C.Run->GetSim().Time() + 1.0e-9 >= C.Run->NextModSweep() - 3.0 ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FShiftTask_ModTelegraph::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FShiftEvent Event;
	Event.Type = EShiftEventType::ModTelegraph;
	Ctx(Context, ContextHandle).Run->EmitEvent(Event);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FShiftTask_ModTelegraph::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	UShiftBehaviorContext& C = Ctx(Context, ContextHandle);
	return C.Run->GetSim().Time() + 1.0e-9 >= C.Run->NextModSweep() ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FShiftTask_ModSweep::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	Ctx(Context, ContextHandle).Run->ResolveModSweep();
	return EStateTreeRunStatus::Succeeded;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Ambient events
// ---------------------------------------------------------------------------------------------------------------------------------

EStateTreeRunStatus FShiftTask_AmbientWait::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	UShiftBehaviorContext& C = Ctx(Context, ContextHandle);
	return C.Run->GetSim().Time() + 1.0e-9 >= C.NextTime ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FShiftTask_AmbientEvent::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	UShiftBehaviorContext& C = Ctx(Context, ContextHandle);
	ShiftSim::Sim& S = C.Run->GetSim();
	const ShiftSim::PolicyParams& PP = C.Run->GetPolicy();
	const int32 Kind = Context.GetInstanceData(*this).Kind;

	// A random entry other than the player's: events shuffle the field, they never target the player.
	TArray<int32, TInlineAllocator<16>> Candidates;
	for (int32 E = 0; E < S.NumEntries(); ++E)
	{
		const bool bOther = E != ShiftSim::kPlayerId && E != S.RivalId();
		if (E != ShiftSim::kPlayerId && (Kind != 1 || bOther))
		{
			Candidates.Add(E);
		}
	}
	if (Candidates.Num() > 0)
	{
		const int32 Entry = Candidates[C.Rng.Range(Candidates.Num())];
		float Size = 0.0f;
		if (Kind == 0)
		{
			Size = 0.12f;
			S.AddConvictionToEntry(Entry, Size);                 // the algorithm pushes an entry: its voters dig in
		}
		else if (Kind == 1)
		{
			ShiftSim::TargetFilter F;
			F.maxV = PP.OthersMaxV * 3.0;
			F.roleMask = (1u << static_cast<uint32>(ShiftSim::NodeRole::Member)) | (1u << static_cast<uint32>(ShiftSim::NodeRole::Bridge));
			ShiftSim::Target T;
			if (S.BestTarget(Entry, F, &T))
			{
				S.Plug(Entry, T.node, 3.0);                      // a streamer plays it live: one strong push
				Size = 3.0f;
			}
		}
		else
		{
			Size = -0.10f;
			S.AddConvictionToEntry(Entry, Size);                 // a drama thread dents an entry's voters
		}

		FShiftEvent Event;
		Event.Type = EShiftEventType::DirectorEvent;
		Event.Node = Kind;
		Event.NewOwner = Entry;
		Event.Value = Size;
		C.Run->EmitEvent(Event);
	}

	// Next event: spacing follows the phase of the jam (early, mid, final rush), with some jitter.
	const double Progress = S.Time() / FMath::Max(1.0, S.TotalSeconds());
	const double Phase = Progress < 0.33 ? 1.3 : (Progress < 0.8 ? 1.0 : 0.6);
	const double Base = S.TotalSeconds() / 8.0;
	C.NextTime = S.Time() + Base * Phase * C.Rng.Uniform(0.75, 1.25);
	return EStateTreeRunStatus::Succeeded;
}
