// Shift Happens - Game Off 2026

#include "Game/ShiftLesson.h"

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Game/ShiftBuild.h"
#include "Game/ShiftContent.h"
#include "Game/ShiftRun.h"
#include "HAL/Platform.h"
#include "Logging/LogMacros.h"
#include "Math/MathFwd.h"
#include "Math/UnrealMathUtility.h"
#include "ShiftHappens.h"
#include "Sim/ShiftGraph.h"
#include "Sim/ShiftParams.h"
#include "Sim/ShiftSim.h"
#include "Templates/Function.h"
#include "UI/ShiftHudModel.h"

namespace
{
	constexpr int32 kPlayer = ShiftSim::kPlayerId;
	constexpr float kCheerSeconds = 0.9f;

	/** The ring around a voter, as the board draws it (see Board/ShiftBoard.cpp, NodeRadius). */
	float NodeRadius(const ShiftSim::GraphNode& N)
	{
		return FMath::Clamp(8.0f + 3.2f * FMath::Sqrt(static_cast<float>(N.weight) / 30.0f), 10.0f, 34.0f);
	}

	float DistanceBetween(const ShiftSim::Sim& S, int32 A, int32 B)
	{
		const ShiftSim::GraphNode& NA = S.GetGraph().nodes[static_cast<size_t>(A)];
		const ShiftSim::GraphNode& NB = S.GetGraph().nodes[static_cast<size_t>(B)];
		return FMath::Sqrt(FMath::Square(NA.x - NB.x) + FMath::Square(NA.y - NB.y));
	}

	/** Gives a voter an owner and the highest conviction at which the player still needs exactly PlugsWanted Plugs (the number depends on the build). */
	void StageConviction(ShiftSim::Sim& S, int32 Node, int32 Owner, int32 PlugsWanted)
	{
		for (int32 I = 20; I >= 4; --I)
		{
			S.SetNodeState(Node, Owner, 0.05 * I, 0.0);
			if (S.PlugsNeeded(kPlayer, Node, 1.0, true) == PlugsWanted)
			{
				return;
			}
		}
	}

	bool Contains(const TArray<int32>& List, int32 Value)
	{
		return List.Contains(Value);
	}
}

void FShiftLesson::Configure(int32 Chapter, FShiftLessonSetup& Out)
{
	Out.Jam = 0;
	Out.Seed = 7;
	Out.Build = FShiftBuild();
	Out.bMods = Chapter == 2;

	TMap<FString, double>& O = Out.Overrides;
	// A small board: the stock ones are built for the real jams.
	O.Add(TEXT("Nodes"), 18.0);
	O.Add(TEXT("Communities"), 3.0);
	O.Add(TEXT("Bridges"), 1.0);
	O.Add(TEXT("Celebs"), 1.0);
	O.Add(TEXT("Entries"), 4.0);
	// Nothing moves on its own: the rival and the other entries never act, voters do not drift to anyone else, no underdog pull, no final push.
	O.Add(TEXT("RivalInterval"), 100000.0);
	O.Add(TEXT("OthersInterval"), 100000.0);
	O.Add(TEXT("RivalRevenge"), 0.0);
	O.Add(TEXT("OthersRevenge"), 0.0);
	O.Add(TEXT("AdoptRate"), 0.0);
	O.Add(TEXT("DriftRate"), 0.0);
	O.Add(TEXT("Backlash"), 0.0);
	O.Add(TEXT("MegaDays"), 0.0);
	O.Add(TEXT("EyeAwayCount"), 0.0);
	// The run never ends by itself; Posts come back quickly; an announced attack gives a beginner time to answer.
	O.Add(TEXT("SecPerDay"), 3600.0);
	O.Add(TEXT("Regen"), 0.8);
	O.Add(TEXT("AttackTelegraph"), 3.0);
	O.Add(TEXT("SockHeldHeat"), 8.0);   // holding a fake vote still warms you, gently: a first try at backing one must not be struck out of the lesson
	O.Add(TEXT("ModIntervalMin"), 100000.0);
	O.Add(TEXT("ModIntervalMax"), 100000.0);
	O.Add(TEXT("ModFirstSweepMin"), 100000.0);
}

void FShiftLesson::Begin(int32 InChapter, int32 FirstStep)
{
	ChapterIx = FMath::Clamp(InChapter, 1, ChapterCount());
	Picks = FPicks();
	Steps.Reset();
	StepEvents.Reset();
	Targets.Reset();
	Notice.Empty();
	BuildSteps();
	FirstStepIx = FMath::Clamp(FirstStep - 1, 0, FMath::Max(0, Steps.Num() - 1));
	StepIx = FirstStepIx;
	Phase = EPhase::Reading;
	PhaseTime = 0.0f;
	HoverTime = 0.0f;
	NightTime = 0.0f;
	StepKey = -1;
	ViewState = FShiftLessonView();
}

void FShiftLesson::ChoosePicks(const ShiftSim::Sim& S)
{
	const ShiftSim::Graph& G = S.GetGraph();
	const int32 Count = S.NodeCount();
	TArray<int32> Used;

	// The padlocked hub: a local hub that is not the player's, with at least two followers; the one with the fewest so the gate is short to open.
	int32 BestFollowers = 1000;
	for (int32 I = 0; I < Count; ++I)
	{
		const ShiftSim::GraphNode& N = G.nodes[static_cast<size_t>(I)];
		if (N.role != ShiftSim::NodeRole::LocalHub || S.Opinion(I) == kPlayer)
		{
			continue;
		}
		const int32 Followers = static_cast<int32>(N.followers.size());
		if (Followers >= 2 && Followers < BestFollowers)
		{
			BestFollowers = Followers;
			Picks.Hub = I;
		}
	}
	if (Picks.Hub >= 0)
	{
		Used.Add(Picks.Hub);
		for (const int Follower : G.nodes[static_cast<size_t>(Picks.Hub)].followers)
		{
			if (G.nodes[static_cast<size_t>(Follower)].role == ShiftSim::NodeRole::Member && S.Opinion(Follower) != kPlayer)
			{
				Picks.HubFollowers.Add(Follower);
				Used.Add(Follower);
			}
		}
	}

	// The voter that shakes: one of the player's own with the most neighbours (they are what pushes it).
	int32 BestNeighbours = -1;
	for (int32 I = 0; I < Count; ++I)
	{
		if (S.Opinion(I) != kPlayer || Contains(Used, I))
		{
			continue;
		}
		const ShiftSim::GraphNode& N = G.nodes[static_cast<size_t>(I)];
		const int32 Neighbours = static_cast<int32>(N.follows.size() + N.followers.size());
		if (Neighbours > BestNeighbours)
		{
			BestNeighbours = Neighbours;
			Picks.Own = I;
		}
	}
	if (Picks.Own < 0)
	{
		for (int32 I = 0; I < Count && Picks.Own < 0; ++I)
		{
			if (S.Opinion(I) == kPlayer)
			{
				Picks.Own = I;
			}
		}
	}
	if (Picks.Own >= 0)
	{
		Used.Add(Picks.Own);
	}

	// Weak and strong: plain members of other games, nearest to the player's own voter so the lesson stays in one corner of the board.
	auto Nearest = [&](int32 Skip) -> int32
		{
			int32 Best = -1;
			float BestDistance = 1.0e9f;
			for (int32 I = 0; I < Count; ++I)
			{
				if (G.nodes[static_cast<size_t>(I)].role != ShiftSim::NodeRole::Member || S.Opinion(I) == kPlayer || Contains(Used, I) || I == Skip)
				{
					continue;
				}
				const float Distance = Picks.Own >= 0 ? DistanceBetween(S, I, Picks.Own) : 0.0f;
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					Best = I;
				}
			}
			return Best;
		};
	Picks.Weak = Nearest(-1);
	if (Picks.Weak >= 0)
	{
		Used.Add(Picks.Weak);
	}
	Picks.Strong = Nearest(Picks.Weak);
	if (G.celebrities.size() > 0)
	{
		Picks.Celeb = G.celebrities[0];
	}
	UE_LOG(LogShift, Log, TEXT("Lesson picks: own=%d weak=%d strong=%d hub=%d (%d followers) celeb=%d"), Picks.Own, Picks.Weak, Picks.Strong, Picks.Hub, Picks.HubFollowers.Num(), Picks.Celeb);
}

void FShiftLesson::Attach(FShiftRun& Run)
{
	if (!Run.IsValid())
	{
		return;
	}
	ShiftSim::Sim& S = Run.GetSim();
	S.SetPosts(S.GetParams().PostsMax);
	ChoosePicks(S);
	if (ChapterIx == 2)
	{
		Run.SetNextModSweep(S.Time() + 3600.0);   // no sweep until the step that is about it
	}
	EnterStep(Run);
}

bool FShiftLesson::Saw(EShiftEventType Type, int32 Node, int32 SockWanted) const
{
	for (const FShiftEvent& E : StepEvents)
	{
		if (E.Type == Type && (Node < 0 || E.Node == Node) && (SockWanted < 0 || E.bSock == (SockWanted != 0)))
		{
			return true;
		}
	}
	return false;
}

void FShiftLesson::BuildSteps()
{
	using ShiftSim::Sim;
	FShiftLesson* L = this;
	auto Step = [this]() -> FStep& { return Steps.AddDefaulted_GetRef(); };

	if (ChapterIx == 1)
	{
		// 1. the goal
		{
			FStep& S = Step();
			S.bRead = true;
			S.Anchor = EShiftLessonAnchor::Leaderboard;
			S.Label = TEXT("You");
			S.Targets = [](const Sim& Sm, TArray<int32>& Out)
				{
					for (int32 I = 0; I < Sm.NodeCount() && Out.Num() < 3; ++I)
					{
						if (Sm.Opinion(I) == kPlayer)
						{
							Out.Add(I);
						}
					}
				};
		}
		// 2. hover a voter
		{
			FStep& S = Step();
			S.Label = TEXT("Hover");
			S.Stage = [L](FShiftRun& R) { L->StepKey = L->Picks.Weak; StageConviction(R.GetSim(), L->Picks.Weak, R.GetSim().RivalId(), 1); };
			S.Targets = [L](const Sim&, TArray<int32>& Out) { Out.Add(L->Picks.Weak); };
			S.Done = [L](FShiftRun&) { return L->HoverTime > 0.5f; };
			S.AutoAct = [L](const Sim&) { return FShiftLessonAuto{ FShiftLessonAuto::EKind::Hover, L->Picks.Weak, false }; };
		}
		// 3. plug
		{
			FStep& S = Step();
			S.Allow = AllowPlug;
			S.Label = TEXT("Click");
			S.Anchor = EShiftLessonAnchor::Posts;
			S.Stage = [L](FShiftRun& R) { L->StepKey = L->Picks.Weak; StageConviction(R.GetSim(), L->Picks.Weak, R.GetSim().RivalId(), 1); R.GetSim().SetPosts(R.GetSim().GetParams().PostsMax); };
			S.Targets = [L](const Sim&, TArray<int32>& Out) { Out.Add(L->Picks.Weak); };
			S.Done = [L](FShiftRun&) { return L->Saw(EShiftEventType::PlayerFlipped, L->Picks.Weak); };
			S.AutoAct = [L](const Sim&) { return FShiftLessonAuto{ FShiftLessonAuto::EKind::Click, L->Picks.Weak, false }; };
		}
		// 4. conviction: a strong voter takes two Plugs
		{
			FStep& S = Step();
			S.Allow = AllowPlug;
			S.Label = TEXT("Plug");
			S.Anchor = EShiftLessonAnchor::Posts;
			S.Stage = [L](FShiftRun& R) { L->StepKey = L->Picks.Strong; StageConviction(R.GetSim(), L->Picks.Strong, R.GetSim().RivalId(), 2); R.GetSim().SetPosts(R.GetSim().GetParams().PostsMax); };
			S.Targets = [L](const Sim&, TArray<int32>& Out) { Out.Add(L->Picks.Strong); };
			S.Done = [L](FShiftRun&) { return L->Saw(EShiftEventType::PlayerFlipped, L->Picks.Strong); };
			S.AutoAct = [L](const Sim&) { return FShiftLessonAuto{ FShiftLessonAuto::EKind::Click, L->Picks.Strong, false }; };
		}
		// 5. drift: a voter of yours shakes, reinforce it
		{
			FStep& S = Step();
			S.Allow = AllowReinforce;
			S.Label = TEXT("Reinforce");
			S.Stage = [L](FShiftRun& R)
				{
					Sim& Sm = R.GetSim();
					L->StepKey = L->Picks.Own;
					const ShiftSim::GraphNode& N = Sm.GetGraph().nodes[static_cast<size_t>(L->Picks.Own)];
					TArray<int32> Around;
					for (const int F : N.follows) { Around.AddUnique(F); }
					for (const int F : N.followers) { Around.AddUnique(F); }
					for (const int Other : Around)
					{
						if (Sm.Opinion(Other) != kPlayer)
						{
							Sm.SetNodeState(Other, Sm.RivalId(), 1.0, 0.0);   // the neighbours that push it away
						}
					}
					Sm.SetNodeState(L->Picks.Own, kPlayer, 0.25, 0.0);
					Sm.SetPosts(Sm.GetParams().PostsMax);
				};
			S.Targets = [L](const Sim&, TArray<int32>& Out) { Out.Add(L->Picks.Own); };
			S.Keep = [L](FShiftRun& R)
				{
					Sim& Sm = R.GetSim();
					if (Sm.Opinion(L->Picks.Own) == kPlayer && Sm.Conviction(L->Picks.Own) < 0.15)
					{
						Sm.SetNodeState(L->Picks.Own, kPlayer, 0.25, 0.0);   // it keeps shaking, but it does not leave before the player has read the card
					}
				};
			S.Done = [L](FShiftRun&) { return L->Saw(EShiftEventType::PlayerReinforced, L->Picks.Own) || L->Saw(EShiftEventType::PlayerParried, L->Picks.Own); };
			S.AutoAct = [L](const Sim&) { return FShiftLessonAuto{ FShiftLessonAuto::EKind::Click, L->Picks.Own, false }; };
		}
		// 6. a gate: click the padlocked hub
		auto StageHub = [L](FShiftRun& R)
			{
				Sim& Sm = R.GetSim();
				L->StepKey = -1;
				Sm.SetNodeState(L->Picks.Hub, Sm.RivalId(), 0.9, 0.0);
				for (const int32 F : L->Picks.HubFollowers)
				{
					StageConviction(Sm, F, Sm.RivalId(), 1);
				}
				Sm.SetPosts(Sm.GetParams().PostsMax);
			};
		{
			FStep& S = Step();
			S.Allow = AllowPlug;
			S.Label = TEXT("Click");
			S.Stage = StageHub;
			S.Targets = [L](const Sim&, TArray<int32>& Out) { Out.Add(L->Picks.Hub); };
			S.Done = [L](FShiftRun&) { return L->Saw(EShiftEventType::PlayerGated, L->Picks.Hub); };
			S.AutoAct = [L](const Sim&) { return FShiftLessonAuto{ FShiftLessonAuto::EKind::Click, L->Picks.Hub, false }; };
		}
		// 7. win the followers, then the hub
		{
			FStep& S = Step();
			S.Allow = AllowPlug;
			S.Label = TEXT("Win");
			S.Stage = StageHub;
			S.Targets = [L](const Sim& Sm, TArray<int32>& Out)
				{
					if (Sm.IsGated(L->Picks.Hub))
					{
						for (const int32 F : L->Picks.HubFollowers)
						{
							if (Sm.Opinion(F) != kPlayer)
							{
								Out.Add(F);
							}
						}
					}
					if (Out.Num() == 0 || !Sm.IsGated(L->Picks.Hub))
					{
						Out.Reset();
						Out.Add(L->Picks.Hub);
					}
				};
			S.Done = [L](FShiftRun& R) { return R.GetSim().Opinion(L->Picks.Hub) == kPlayer; };
			S.AutoAct = [L](const Sim& Sm)
				{
					TArray<int32> T;
					T.Reset();
					if (Sm.IsGated(L->Picks.Hub))
					{
						for (const int32 F : L->Picks.HubFollowers) { if (Sm.Opinion(F) != kPlayer) { T.Add(F); } }
					}
					return FShiftLessonAuto{ FShiftLessonAuto::EKind::Click, T.Num() > 0 ? T[0] : L->Picks.Hub, false };
				};
		}
		// 8. an announced attack: parry it
		{
			FStep& S = Step();
			S.Allow = AllowReinforce;
			S.Label = TEXT("Parry");
			S.Stage = [L](FShiftRun& R)
				{
					Sim& Sm = R.GetSim();
					L->StepKey = L->Picks.Strong;
					Sm.SetNodeState(L->Picks.Strong, kPlayer, 0.6, 0.0);
					Sm.SetPosts(Sm.GetParams().PostsMax);
				};
			S.GoDelay = 1.0f;
			S.Go = [L](FShiftRun& R) { R.GetSim().Plug(R.GetSim().RivalId(), L->Picks.Strong, 40.0); };   // the ring starts to close, slowly: nobody is hurried while reading the card
			S.Targets = [L](const Sim&, TArray<int32>& Out) { Out.Add(L->Picks.Strong); };
			S.Done = [L](FShiftRun&) { return L->Saw(EShiftEventType::PlayerParried, L->Picks.Strong); };
			S.AutoAct = [L](const Sim&) { return FShiftLessonAuto{ FShiftLessonAuto::EKind::Click, L->Picks.Strong, false }; };
		}
		// 9. Rally
		{
			FStep& S = Step();
			S.Allow = AllowRally;
			S.Label = TEXT("Right-click");
			S.Stage = [L](FShiftRun& R)
				{
					Sim& Sm = R.GetSim();
					L->StepKey = -1;
					Sm.SetNodeState(L->Picks.Weak, kPlayer, 0.6, 0.0);
					Sm.SetPosts(Sm.GetParams().PostsMax);
				};
			S.Targets = [L](const Sim&, TArray<int32>& Out) { Out.Add(L->Picks.Weak); };
			S.Done = [L](FShiftRun&) { return L->Saw(EShiftEventType::PlayerRallied, -1); };
			S.AutoAct = [L](const Sim&) { return FShiftLessonAuto{ FShiftLessonAuto::EKind::RightClick, L->Picks.Weak, false }; };
		}
		// 10. the end
		{
			FStep& S = Step();
			S.bRead = true;
		}
		return;
	}

	// ---- chapter 2: night shift
	// 1. a celebrity behind its gate
	{
		FStep& S = Step();
		S.Allow = AllowPlug;
		S.Label = TEXT("Click");
		S.Stage = [L](FShiftRun& R) { Sim& Sm = R.GetSim(); L->StepKey = -1; Sm.SetNodeState(L->Picks.Celeb, Sm.RivalId(), 1.0, 0.0); Sm.SetPosts(Sm.GetParams().PostsMax); };
		S.Targets = [L](const Sim&, TArray<int32>& Out) { Out.Add(L->Picks.Celeb); };
		S.Done = [L](FShiftRun&) { return L->Saw(EShiftEventType::PlayerGated, L->Picks.Celeb); };
		S.AutoAct = [L](const Sim&) { return FShiftLessonAuto{ FShiftLessonAuto::EKind::Click, L->Picks.Celeb, false }; };
	}
	// 2. hold SHIFT
	{
		FStep& S = Step();
		S.Allow = AllowNight;
		S.Anchor = EShiftLessonAnchor::NightChip;
		S.Stage = [L](FShiftRun&) { L->StepKey = -1; };
		S.Targets = [](const Sim&, TArray<int32>&) {};
		S.Done = [L](FShiftRun&) { return L->NightTime > 0.8f; };
		S.AutoAct = [](const Sim&) { return FShiftLessonAuto{ FShiftLessonAuto::EKind::NightOn, -1, true }; };
	}
	// 3. a sockpuppet flips the celebrity
	{
		FStep& S = Step();
		S.Allow = AllowPlug | AllowNight;
		S.Label = TEXT("Click");
		S.Anchor = EShiftLessonAnchor::Heat;
		S.Stage = [L](FShiftRun& R) { Sim& Sm = R.GetSim(); L->StepKey = -1; Sm.SetNodeState(L->Picks.Celeb, Sm.RivalId(), 1.0, 0.0); Sm.SetHeat(0.0); Sm.SetPosts(Sm.GetParams().PostsMax); };
		S.Targets = [L](const Sim&, TArray<int32>& Out) { Out.Add(L->Picks.Celeb); };
		S.Done = [L](FShiftRun&) { return L->Saw(EShiftEventType::PlayerFlipped, L->Picks.Celeb, 1); };
		S.AutoAct = [L](const Sim&) { return FShiftLessonAuto{ FShiftLessonAuto::EKind::Click, L->Picks.Celeb, true }; };
	}
	// 4. back it until it is a real vote
	{
		FStep& S = Step();
		S.Allow = AllowReinforce | AllowNight;
		S.Label = TEXT("Back it");
		S.Anchor = EShiftLessonAnchor::Heat;
		S.Stage = [L](FShiftRun& R)
			{
				Sim& Sm = R.GetSim();
				L->StepKey = L->Picks.Celeb;
				if (Sm.Opinion(L->Picks.Celeb) != kPlayer || !Sm.IsSockpuppet(L->Picks.Celeb))
				{
					Sm.SetNodeState(L->Picks.Celeb, Sm.RivalId(), 1.0, 0.0);
					Sm.SetPosts(Sm.GetParams().PostsMax);
					Sm.SetNightHeld(true);
					Sm.Sock(L->Picks.Celeb);
				}
				Sm.SetPosts(Sm.GetParams().PostsMax);
			};
		S.Targets = [L](const Sim&, TArray<int32>& Out) { Out.Add(L->Picks.Celeb); };
		S.Done = [L](FShiftRun& R) { return R.GetSim().Opinion(L->Picks.Celeb) == kPlayer && !R.GetSim().IsSockpuppet(L->Picks.Celeb); };
		S.AutoAct = [L](const Sim&) { return FShiftLessonAuto{ FShiftLessonAuto::EKind::Click, L->Picks.Celeb, true }; };
	}
	// 5. Heat is high and the Mod Eye checks: read it first, the world waits
	{
		FStep& S = Step();
		S.bRead = true;
		S.bRedOk = true;
		S.Anchor = EShiftLessonAnchor::Heat;
		S.Stage = [L](FShiftRun& R)
			{
				Sim& Sm = R.GetSim();
				L->StepKey = -1;
				Sm.ForgiveStrike();
				Sm.SetHeat(Sm.GetParams().HeatRedZone + 15.0);
				R.SetNextModSweep(Sm.Time() + 90.0);
			};
		S.Targets = [](const Sim&, TArray<int32>&) {};
	}
	// 6. let go of SHIFT and let Heat cool
	{
		FStep& S = Step();
		S.Allow = AllowNight;
		S.Anchor = EShiftLessonAnchor::Heat;
		S.Stage = [L](FShiftRun& R)
			{
				Sim& Sm = R.GetSim();
				L->StepKey = -1;
				Sm.ForgiveStrike();
				Sm.SetHeat(Sm.GetParams().HeatRedZone + 15.0);   // well above the tick: about five seconds of cooling
				R.SetNextModSweep(Sm.Time() + 90.0);   // far away, and kept away (Keep): the Mod Eye is explained, never sprung on a reader
			};
		S.Targets = [](const Sim&, TArray<int32>&) {};
		S.Done = [](FShiftRun& R) { return R.GetSim().Heat() < R.GetSim().GetParams().HeatRedZone - 1.0 && !R.IsNightHeld(); };
		S.bRedOk = true;
		S.Keep = [](FShiftRun& R)
			{
				const Sim& Sm = R.GetSim();
				if (R.NextModSweep() - Sm.Time() < 20.0)
				{
					R.SetNextModSweep(Sm.Time() + 90.0);
				}
			};
		S.AutoAct = [](const Sim&) { return FShiftLessonAuto{ FShiftLessonAuto::EKind::NightOff, -1, false }; };
	}
	// 6. strikes, the audit, the Blame
	{
		FStep& S = Step();
		S.bRead = true;
	}
	// 7. the end
	{
		FStep& S = Step();
		S.bRead = true;
	}
}

void FShiftLesson::EnterStep(FShiftRun& Run)
{
	Phase = EPhase::Reading;
	PhaseTime = 0.0f;
	HoverTime = 0.0f;
	NightTime = 0.0f;
	bGoPending = false;
	bNagged = false;
	StepEvents.Reset();
	Targets.Reset();
	if (Steps.IsValidIndex(StepIx))
	{
		const FStep& St = Steps[StepIx];
		if (St.Stage)
		{
			St.Stage(Run);
		}
		if (St.Targets)
		{
			St.Targets(Run.GetSim(), Targets);
		}
		if (!St.bRead)
		{
			// nothing to read first: the card is up and the player may act right away
			Phase = EPhase::Doing;
			bGoPending = St.Go != nullptr;
		}
	}
	RefreshView(Run.GetSim());
	UE_LOG(LogShift, Log, TEXT("Lesson %d step %d/%d shown"), ChapterIx, StepIx + 1, Steps.Num());
}

void FShiftLesson::Continue(FShiftRun& Run)
{
	if (Phase == EPhase::Complete || !Steps.IsValidIndex(StepIx))
	{
		return;
	}
	if (Phase != EPhase::Reading)
	{
		return;
	}
	// only a text step is ever read first: Continue ends it
	Advance(Run);
}

void FShiftLesson::Advance(FShiftRun& Run)
{
	UE_LOG(LogShift, Log, TEXT("Lesson %d step %d done"), ChapterIx, StepIx + 1);
	++StepIx;
	if (StepIx >= Steps.Num())
	{
		Phase = EPhase::Complete;
		StepIx = Steps.Num() - 1;
		UE_LOG(LogShift, Log, TEXT("Lesson %d complete"), ChapterIx);
		RefreshView(Run.GetSim());
		return;
	}
	EnterStep(Run);
}

void FShiftLesson::Tick(FShiftRun& Run, float DeltaTime, const TArray<FShiftEvent>& Events, int32 HoverNode)
{
	if (!Run.IsValid() || !Steps.IsValidIndex(StepIx))
	{
		return;
	}
	ShiftSim::Sim& S = Run.GetSim();
	const FStep& St = Steps[StepIx];
	PhaseTime += DeltaTime;
	// Nothing in a lesson can hurt: Heat never reaches the red tick (except in the step about it, and never the cap) and a strike, should one happen, is taken back.
	{
		const ShiftSim::Params& Pm = S.GetParams();
		const double Ceiling = St.bRedOk ? Pm.HeatMax - 25.0 : Pm.HeatRedZone - 5.0;
		if (S.Heat() > Ceiling)
		{
			S.SetHeat(Ceiling);
		}
		if (S.Strikes() > 0)
		{
			S.ForgiveStrike();
		}
	}

	if (Phase == EPhase::Doing)
	{
		if (bGoPending && PhaseTime >= St.GoDelay)
		{
			bGoPending = false;
			St.Go(Run);
		}
		StepEvents.Append(Events);
		// The voter the step is about was lost to the world (a shaking voter, a ring that landed): put the scene back and say so.
		for (const FShiftEvent& E : Events)
		{
			if (E.Type == EShiftEventType::PlayerLostVoter && StepKey >= 0 && E.Node == StepKey)
			{
				if (St.Stage) { St.Stage(Run); }
				bGoPending = St.Go != nullptr;   // the delay runs again
				PhaseTime = 0.0f;
				StepEvents.Reset();
				Notice = TEXT("It got away. Same again.");
				break;
			}
		}
		const bool bHeld = Targets.Contains(HoverNode);
		HoverTime = bHeld ? HoverTime + DeltaTime : 0.0f;
		NightTime = Run.IsNightHeld() ? NightTime + DeltaTime : 0.0f;
		if (St.Keep)
		{
			St.Keep(Run);
		}
		if (ChapterIx == 2 && StepIx == 5 && !bNagged && NightTime > 6.0f)
		{
			bNagged = true;
			Notice = TEXT("Let go of SHIFT. Heat goes down by itself.");
		}
		if (St.Done && St.Done(Run))
		{
			Phase = EPhase::Cheer;
			PhaseTime = 0.0f;
		}
	}
	else if (Phase == EPhase::Cheer && PhaseTime >= kCheerSeconds)
	{
		Advance(Run);
		if (!Steps.IsValidIndex(StepIx))
		{
			return;
		}
	}
	Targets.Reset();
	if (Steps[StepIx].Targets)
	{
		Steps[StepIx].Targets(S, Targets);
	}
	RefreshView(S);
}

void FShiftLesson::RefreshView(const ShiftSim::Sim& S)
{
	FShiftLessonView& V = ViewState;
	V.bActive = true;
	V.Chapter = ChapterIx;
	V.Step = StepIx + 1;
	V.Steps = Steps.Num();
	V.Title = ChapterIx == 1 ? TEXT("LESSON 1 OF 2: THE BASICS") : TEXT("LESSON 2 OF 2: NIGHT SHIFT");
	const FString Raw = FShiftContent::LessonText(ChapterIx, StepIx + 1);
	V.Body = Expand ? Expand(Raw) : Raw;
	V.bReading = Phase == EPhase::Reading;
	V.bCheer = Phase == EPhase::Cheer;
	V.bFinish = Phase == EPhase::Complete;
	V.bHasNext = ChapterIx < ChapterCount();
	V.bModEye = ChapterIx != 2 || StepIx >= 4;
	V.Anchor = Steps.IsValidIndex(StepIx) ? Steps[StepIx].Anchor : EShiftLessonAnchor::None;
	V.Spots.Reset();
	if (Phase == EPhase::Complete || Phase == EPhase::Cheer)
	{
		return;
	}
	for (const int32 Node : Targets)
	{
		if (Node < 0 || Node >= S.NodeCount())
		{
			continue;
		}
		const ShiftSim::GraphNode& N = S.GetGraph().nodes[static_cast<size_t>(Node)];
		FShiftLessonSpot Spot;
		Spot.Center = FVector2f(N.x, N.y);
		Spot.Radius = NodeRadius(N);
		Spot.Label = Steps.IsValidIndex(StepIx) ? Steps[StepIx].Label : FString();
		V.Spots.Add(Spot);
	}
}

bool FShiftLesson::AllowClick(const ShiftSim::Sim& S, int32 Node, bool bRight, bool bNight, FString& Why) const
{
	if (Phase != EPhase::Doing || !Steps.IsValidIndex(StepIx))
	{
		Why = Phase == EPhase::Reading ? TEXT("Read the card, then press Continue.") : FString();
		return false;
	}
	const uint32 Allow = Steps[StepIx].Allow;
	if (bRight)
	{
		if ((Allow & AllowRally) == 0)
		{
			Why = TEXT("Rally comes later in this lesson.");
			return false;
		}
	}
	else if (bNight && (Allow & AllowNight) == 0)
	{
		Why = TEXT("Night shift is the second lesson. Click without SHIFT.");
		return false;
	}
	else
	{
		const bool bOwn = S.Opinion(Node) == kPlayer;
		if (bOwn ? (Allow & AllowReinforce) == 0 : (Allow & AllowPlug) == 0)
		{
			Why = bOwn ? TEXT("Not now: follow the step on the card.") : TEXT("Not now: click the voter in the spotlight.");
			return false;
		}
	}
	if (Targets.Num() > 0 && !Targets.Contains(Node))
	{
		Why = TEXT("Not that one: use the voter in the spotlight.");
		return false;
	}
	return true;
}

bool FShiftLesson::NightAllowed() const
{
	return Phase != EPhase::Complete && Steps.IsValidIndex(StepIx) && (Steps[StepIx].Allow & AllowNight) != 0;
}

FString FShiftLesson::ConsumeNotice()
{
	FString Out = Notice;
	Notice.Empty();
	return Out;
}

FShiftLessonAuto FShiftLesson::Auto(const ShiftSim::Sim& S) const
{
	if (Phase == EPhase::Reading)
	{
		return FShiftLessonAuto{ FShiftLessonAuto::EKind::Continue, -1, false };
	}
	if (Phase != EPhase::Doing || !Steps.IsValidIndex(StepIx) || !Steps[StepIx].AutoAct)
	{
		return FShiftLessonAuto();
	}
	return Steps[StepIx].AutoAct(S);
}
