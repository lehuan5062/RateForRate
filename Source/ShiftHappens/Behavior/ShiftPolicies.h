// Native behaviour layer: scripted rival, other entries, Mod Eye and player bots, written against the sim core's public API.
// Promoted from Tools/SimHarness/policies.h (the calibrated stand-ins). Each Update() is what a StateTree task will call;
// until the StateTree assets exist (spike S7) the game drives these directly.
#pragma once

#include <cstdint>
#include "Sim/ShiftGraph.h"
#include "Sim/ShiftParams.h"
#include "Sim/ShiftRng.h"
#include "Sim/ShiftSim.h"
#include <string>
#include <vector>

namespace ShiftSim
{

	struct PolicyParams
	{
		// stand-in rival
		double RivalDefendBelow = 0.75;      // defend the weakest high-value rival node when its conviction is below this
		double RivalPlayerMult = 2.0;        // attack score multiplier for player-owned nodes
		double RivalCapShare = 0.65;         // at/above this share the rival only attacks player-owned nodes (1 = never complacent)
		double RivalBandBase = 1.0;          // strength = base + slope * max(0, playerShare - threshold)
		double RivalBandSlope = 4.0;
		double RivalBandThreshold = 0.15;
		double MegaDays = 3.0;               // last N days: Mega Campaign
		double MegaConvBoost = 0.3;          // +conviction on all rival nodes at Mega start
		double MegaAttackMult = 2.0;         // actions per rival tick during Mega
		// stand-in others
		double OthersInterval = 10.0;        // each other entry acts this often
		double OthersMaxV = 100.0;           // weak: only low-weight nodes
		double OthersTargetsPlayer = 1.0;    // how readily another entry plugs a voter the player holds (1 = like any other voter, 0 = never)
		// stand-in Mod Eye (director)
		double ModFirstSweepMin = 0.0;       // the Eye's cycle is already running at t=0: first sweep ~ U[this, jam ModIntervalMax]
		// player bots
		double BotInterval = 0.2;            // minimum seconds between bot clicks
		double SockMaxHeat = 45.0;           // greedy+sock socks only when Heat <= this
		double SockWindup = 0.5;             // seconds the bot holds night before the sock click
		double RallyMinRisk = 400.0;         // rally bot: a Rally is worth it when this much vote weight in the cluster is about to flip back
		double LateSeconds = 12.0;           // finisher / late bots: length of the final sockpuppet burst, in seconds before the end
	};

	enum class BotKind
	{
		Idle = 0,
		Random,
		Greedy,
		GreedySock,
		Finisher,       // honest greedy play all run, then a sockpuppet burst in the last LateSeconds
		Late,           // does nothing until the last LateSeconds, then only the burst (the playtester's "do nothing, flip at the last moment")
		Rally,          // honest greedy play, plus a Rally on the cluster with the most vote weight about to flip back
	};

	inline const char* BotName(BotKind K)
	{
		switch (K)
		{
		case BotKind::Idle: return "idle";
		case BotKind::Random: return "random";
		case BotKind::Greedy: return "greedy";
		case BotKind::GreedySock: return "greedy+sock";
		case BotKind::Finisher: return "finisher";
		case BotKind::Late: return "late";
		case BotKind::Rally: return "rally";
		}
		return "?";
	}

	// The command-line spellings (case-insensitive): idle, random, greedy, sock, finisher, late.
	inline bool BotKindFromName(const std::string& Name, BotKind* Out)
	{
		std::string L = Name;
		for (char& Ch : L)
		{
			Ch = static_cast<char>((Ch >= 'A' && Ch <= 'Z') ? Ch - 'A' + 'a' : Ch);
		}
		if (L == "idle") { *Out = BotKind::Idle; return true; }
		if (L == "random") { *Out = BotKind::Random; return true; }
		if (L == "greedy") { *Out = BotKind::Greedy; return true; }
		if (L == "sock") { *Out = BotKind::GreedySock; return true; }
		if (L == "finisher") { *Out = BotKind::Finisher; return true; }
		if (L == "late") { *Out = BotKind::Late; return true; }
		if (L == "rally") { *Out = BotKind::Rally; return true; }
		return false;
	}

	inline int PolicyRound(double V)
	{
		return static_cast<int>(V < 0.0 ? V - 0.5 : V + 0.5);
	}

	// ---------------------------------------------------------------------------------------
	// Stand-in rival: every Jam::rivalInterval seconds defend the weakest high-value rival node,
	// else attack the best value-per-effort target (player nodes weighted x2), strength rubber-banded
	// on the player's share. Last MegaDays days: +conviction pulse and doubled actions.
	// ---------------------------------------------------------------------------------------
	class StandInRival
	{
	public:
		// Interval: the jam definition's rivalInterval (Jams.csv), 2 s if unset.
		static double IntervalFor(const Sim& S, const PolicyParams& /*PP*/)
		{
			return S.Jam().rivalInterval > 0.1 ? S.Jam().rivalInterval : 2.0;
		}

		void Reset(const Sim& S, const PolicyParams& PP)
		{
			Next_ = IntervalFor(S, PP);
			MegaStarted_ = false;
		}

		// ---- building blocks (the StateTree tasks and conditions call these; Update() below composes them) ----
		static bool InMegaPhase(const Sim& S, const PolicyParams& PP)
		{
			return S.DayIndex() >= S.Jam().days - PolicyRound(PP.MegaDays);
		}
		// The last days started and the Mega Campaign pulse has not been applied yet.
		bool MegaPending(const Sim& S, const PolicyParams& PP) const { return InMegaPhase(S, PP) && !MegaStarted_; }
		void MegaPulse(Sim& S, const PolicyParams& PP)
		{
			if (!MegaStarted_)
			{
				MegaStarted_ = true;
				S.AddConvictionToEntry(S.RivalId(), PP.MegaConvBoost);
			}
		}
		static int ActionsPerRound(const Sim& S, const PolicyParams& PP) { return InMegaPhase(S, PP) ? PolicyRound(PP.MegaAttackMult) : 1; }
		static bool HasWeakHub(const Sim& S, const PolicyParams& PP)
		{
			return S.WeakestOwned(S.RivalId(), S.GetParams().HighValueV, PP.RivalDefendBelow) >= 0;
		}
		// Defend: re-Plug the weakest high-value rival node. Returns false when none needs it.
		bool Defend(Sim& S, const PolicyParams& PP)
		{
			const int Weak = S.WeakestOwned(S.RivalId(), S.GetParams().HighValueV, PP.RivalDefendBelow);
			if (Weak < 0)
			{
				return false;
			}
			S.Plug(S.RivalId(), Weak, 1.0);
			++Defends;
			return true;
		}
		// Attack: the best value-per-effort target, strength rubber-banded on the player's share.
		void Attack(Sim& S, const PolicyParams& PP)
		{
			const int Rival = S.RivalId();
			const double Over = S.Share(kPlayerId) - PP.RivalBandThreshold;
			const double Strength = PP.RivalBandBase + PP.RivalBandSlope * (Over > 0.0 ? Over : 0.0);
			TargetFilter F;
			F.strength = Strength;
			F.ownerMult[kPlayerId] = PP.RivalPlayerMult;
			if (S.Share(Rival) >= PP.RivalCapShare)
			{
				// Comfortably ahead: leave the other entries alone, only squash the player.
				for (int E = 0; E < S.NumEntries(); ++E)
				{
					if (E != kPlayerId)
					{
						F.ownerMult[E] = 0.0;
					}
				}
				F.undecidedMult = 0.0;
			}
			Target T;
			if (S.BestTarget(Rival, F, &T))
			{
				const ActionOutcome O = S.Plug(Rival, T.node, Strength);
				++Attacks;
				if (O.flipped)
				{
					++Flips;
				}
			}
		}

		void Update(Sim& S, const PolicyParams& PP)
		{
			if (S.Time() + 1.0e-9 < Next_)
			{
				return;
			}
			Next_ += IntervalFor(S, PP);
			if (MegaPending(S, PP))
			{
				MegaPulse(S, PP);
			}
			const int Actions = ActionsPerRound(S, PP);
			for (int A = 0; A < Actions; ++A)
			{
				if (!Defend(S, PP))
				{
					Attack(S, PP);
				}
			}
		}

		int Defends = 0;   // statistics (debug)
		int Attacks = 0;
		int Flips = 0;

	private:
		double Next_ = 0.0;
		bool MegaStarted_ = false;
	};

	// ---------------------------------------------------------------------------------------
	// Stand-in other entries: weak, low-weight nodes only, every OthersInterval seconds each.
	// ---------------------------------------------------------------------------------------
	class StandInOthers
	{
	public:
		void Reset(const Sim& S, const PolicyParams& PP)
		{
			Entries_.clear();
			Next_.clear();
			for (int E = 0; E < S.NumEntries(); ++E)
			{
				if (E != kPlayerId && E != S.RivalId())
				{
					Entries_.push_back(E);
				}
			}
			const int N = static_cast<int>(Entries_.size());
			for (int I = 0; I < N; ++I)
			{
				Next_.push_back(PP.OthersInterval * (static_cast<double>(I) + 1.0) / static_cast<double>(N));
			}
		}

		// One action of one other entry: the best low-weight target (what the StateTree task calls).
		static void Act(Sim& S, const PolicyParams& PP, int Entry)
		{
			TargetFilter F;
			F.maxV = PP.OthersMaxV;
			F.ownerMult[kPlayerId] = PP.OthersTargetsPlayer;
			F.roleMask = (1u << static_cast<uint32_t>(NodeRole::Member)) | (1u << static_cast<uint32_t>(NodeRole::Bridge));
			Target T;
			if (S.BestTarget(Entry, F, &T))
			{
				S.Plug(Entry, T.node, 1.0);
			}
		}

		// Time of an entry's first action: staggered so the other entries do not all act at once.
		static double FirstActionTime(const Sim& S, const PolicyParams& PP, int Entry)
		{
			int Index = 0, Count = 0;
			for (int E = 0; E < S.NumEntries(); ++E)
			{
				if (E != kPlayerId && E != S.RivalId())
				{
					if (E == Entry)
					{
						Index = Count;
					}
					++Count;
				}
			}
			return Count > 0 ? PP.OthersInterval * (static_cast<double>(Index) + 1.0) / static_cast<double>(Count) : PP.OthersInterval;
		}

		void Update(Sim& S, const PolicyParams& PP)
		{
			for (size_t I = 0; I < Entries_.size(); ++I)
			{
				if (S.Time() + 1.0e-9 < Next_[I])
				{
					continue;
				}
				Next_[I] += PP.OthersInterval;
				Act(S, PP, Entries_[I]);
			}
		}

	private:
		std::vector<int> Entries_;
		std::vector<double> Next_;
	};

	// ---------------------------------------------------------------------------------------
	// Stand-in Mod Eye: sweeps every U[min,max] s; in the red zone when it passes -> strike.
	// Heat at the cap -> immediate strike. (The real schedule lives in the StateTree director.)
	// ---------------------------------------------------------------------------------------
	class StandInModEye
	{
	public:
		void Reset(const Sim& S, uint32_t Seed, const PolicyParams& PP)
		{
			R_.Seed(Rng::Derive(Seed, 0x4D4F44u));
			Next_ = R_.Uniform(PP.ModFirstSweepMin, S.Jam().modIntervalMax);
			Strikes_ = 0;
		}

		void Update(Sim& S, const PolicyParams& /*PP*/)
		{
			if (S.HeatMaxed())
			{
				S.ApplyStrike();
				++Strikes_;
			}
			if (S.Time() + 1.0e-9 >= Next_)
			{
				Next_ += R_.Uniform(S.Jam().modIntervalMin, S.Jam().modIntervalMax);
				if (S.InRedZone())
				{
					S.ApplyStrike();
					++Strikes_;
				}
			}
		}

		int Strikes() const { return Strikes_; }

	private:
		Rng R_;
		double Next_ = 0.0;
		int Strikes_ = 0;
	};

	// ---------------------------------------------------------------------------------------
	// Player bots.
	// ---------------------------------------------------------------------------------------
	class PlayerBot
	{
	public:
		void Reset(BotKind K, uint32_t Seed)
		{
			Kind_ = K;
			R_.Seed(Rng::Derive(Seed, 0x424F54u));
			Next_ = 0.0;
			Winding_ = false;
			WindupEnd_ = 0.0;
			ReinforceNode_ = -1;
			ReinforceLeft_ = 0;
		}

		BotKind Kind() const { return Kind_; }

		// NextSweep: sim time of the next Mod Eye sweep. The Eye's countdown is on the HUD, so a human player knows it too; 1e30 when there is none.
		void Update(Sim& S, const PolicyParams& PP, double NextSweep = 1.0e30)
		{
			if (Kind_ == BotKind::Idle)
			{
				return;
			}
			if (S.Disqualified())
			{
				S.SetNightHeld(false);
				return;
			}
			const double Now = S.Time();

			// The finale bots: the finisher plays honestly (greedy) and the late bot does nothing until the last LateSeconds of the jam, then both burst.
			const bool bFinale = (Kind_ == BotKind::Finisher || Kind_ == BotKind::Late) && S.TotalSeconds() - Now <= PP.LateSeconds + 1.0e-9;
			if (Kind_ == BotKind::Late && !bFinale)
			{
				return;
			}
			const BotKind Mode = (Kind_ == BotKind::Finisher && !bFinale) ? BotKind::Greedy : Kind_;

			if (Winding_)
			{
				if (S.ShadowbanRemaining() > 0.0)
				{
					S.SetNightHeld(false);
					Winding_ = false;
					return;
				}
				if (Now + 1.0e-9 >= WindupEnd_)
				{
					int Node = -1;
					if (bFinale ? FindBurstTarget(S, &Node) : FindGatedHub(S, &Node))
					{
						LastOutcome = S.Sock(Node);
						++Actions;
						if (LastOutcome.result == ActionResult::Flipped && S.GetParams().SockLaunderConv <= 1.0)
						{
							// follow-through: reinforce until the vote is real, before a strike or the final audit takes it back
							const Params& SP = S.GetParams();
							ReinforceNode_ = Node;
							ReinforceLeft_ = ReinforcesToLaunder(SP);
						}
					}
					S.SetNightHeld(false);
					Winding_ = false;
					Next_ = Now + PP.BotInterval;
				}
				return;
			}

			if (ReinforceNode_ >= 0)
			{
				if (ReinforceLeft_ <= 0 || S.Opinion(ReinforceNode_) != kPlayerId || !S.IsSockpuppet(ReinforceNode_) || S.LockRemaining(ReinforceNode_) <= 0.0)
				{
					ReinforceNode_ = -1;    // done, laundered, lost, or too late (the immunity is over)
				}
				else
				{
					if (Now + 1.0e-9 >= Next_ && S.Posts() >= 1.0 && S.ShadowbanRemaining() <= 0.0)
					{
						LastOutcome = S.Plug(kPlayerId, ReinforceNode_, 1.0);
						++Actions;
						--ReinforceLeft_;
						Next_ = Now + PP.BotInterval;
					}
					return;
				}
			}

			if (Now + 1.0e-9 < Next_ || S.Posts() < 1.0 || S.ShadowbanRemaining() > 0.0)
			{
				return;
			}
			Next_ = Now + PP.BotInterval;

			if (Mode == BotKind::Random)
			{
				LastOutcome = S.Plug(kPlayerId, R_.Range(S.NodeCount()), 1.0);
				++Actions;
				return;
			}

			if (bFinale)
			{
				Finale(S, PP, NextSweep);
				return;
			}

			if (Mode == BotKind::Rally && S.RallyCooldownRemaining() <= 0.0)
			{
				int Node = -1;
				if (FindRallyTarget(S, PP, &Node))
				{
					if (S.Posts() >= S.GetParams().RallyCost)
					{
						LastOutcome = S.Rally(Node);
						++Actions;
					}
					return;   // worth a Rally: save the Posts up for it instead of plugging
				}
			}

			if (Mode == BotKind::GreedySock && S.Heat() <= PP.SockMaxHeat)
			{
				int Node = -1;
				if (FindGatedHub(S, &Node))
				{
					Winding_ = true;
					WindupEnd_ = Now + PP.SockWindup;
					S.SetNightHeld(true);
					return;
				}
			}

			TargetFilter F;
			Target T;
			if (S.BestTarget(kPlayerId, F, &T))
			{
				LastOutcome = S.Plug(kPlayerId, T.node, 1.0);
				++Actions;
			}
		}

		ActionOutcome LastOutcome;   // statistics (debug)
		int Actions = 0;

	private:
		// The finale: sock the heaviest voter that is not the player's and not immune, as often as the Heat cap and the next Mod Eye sweep allow. The finisher spends
		// the Posts it can spare on honest Plugs. Both keep a Post reserve for the next sock (plus its two reinforcements when the sim launders sockpuppet votes).
		void Finale(Sim& S, const PolicyParams& PP, double NextSweep)
		{
			const Params& P = S.GetParams();
			const double Now = S.Time();
			const double Reserve = P.SockLaunderConv <= 1.0 ? 1.0 + ReinforcesToLaunder(P) : 1.0;
			int Node = -1;
			if (S.Posts() >= Reserve && FindBurstTarget(S, &Node))
			{
				const double HeatAfter = S.Heat() + P.SockHeat + P.HeatNightRate * PP.SockWindup;
				const bool bCapSafe = HeatAfter < P.HeatMax - 1.0;
				const bool bSweepSafe = NextSweep >= S.TotalSeconds() || HeatAfter - P.HeatDecay * (NextSweep - Now) <= P.HeatRedZone;
				if (bCapSafe && bSweepSafe)
				{
					Winding_ = true;
					WindupEnd_ = Now + PP.SockWindup;
					S.SetNightHeld(true);
					return;
				}
			}
			if (Kind_ == BotKind::Finisher && S.Posts() >= Reserve + 1.0)
			{
				TargetFilter F;
				Target T;
				if (S.BestTarget(kPlayerId, F, &T))
				{
					LastOutcome = S.Plug(kPlayerId, T.node, 1.0);
					++Actions;
				}
			}
		}

		// Reinforcing Plugs a fresh sockpuppet flip (conviction SockConv) needs to reach SockLaunderConv.
		static int ReinforcesToLaunder(const Params& P)
		{
			int N = 0;
			for (double C = P.SockConv; C < P.SockLaunderConv - 1.0e-9 && N < 6; C = C + P.PlugReinforce > 1.0 ? 1.0 : C + P.PlugReinforce) { ++N; }
			return N;
		}

		// Best (highest value) hub or celebrity the player cannot Plug yet and that is not immune.
		static bool FindGatedHub(const Sim& S, int* OutNode)
		{
			int Best = -1;
			double BestValue = 0.0;
			for (int I = 0; I < S.NodeCount(); ++I)
			{
				if (!S.GetGraph().IsGateNode(I) || S.Opinion(I) == kPlayerId || S.LockRemaining(I) > 0.0)
				{
					continue;
				}
				if (!S.IsGated(I))
				{
					continue;
				}
				const double V = S.NodeValue(kPlayerId, I);
				if (Best < 0 || V > BestValue)
				{
					Best = I;
					BestValue = V;
				}
			}
			if (Best < 0)
			{
				return false;
			}
			*OutNode = Best;
			return true;
		}

		// The voter of the player whose cluster (itself plus the player's voters one hop away) has the most vote weight that would flip back within 10 s, if that weight
		// reaches PP.RallyMinRisk.
		static bool FindRallyTarget(const Sim& S, const PolicyParams& PP, int* OutNode)
		{
			int Best = -1;
			double BestRisk = PP.RallyMinRisk;
			for (int I = 0; I < S.NodeCount(); ++I)
			{
				if (S.Opinion(I) != kPlayerId)
				{
					continue;
				}
				const GraphNode& G = S.GetGraph().nodes[static_cast<size_t>(I)];
				double Risk = 0.0;
				auto Add = [&S, &Risk](int J)
				{
					if (S.Opinion(J) == kPlayerId && S.LockRemaining(J) <= 0.0 && S.SecondsToFlip(J) < 10.0)
					{
						Risk += S.GetGraph().nodes[static_cast<size_t>(J)].weight;
					}
				};
				Add(I);
				for (size_t K = 0; K < G.follows.size(); ++K) { if (G.follows[K] != I) { Add(G.follows[K]); } }
				for (size_t K = 0; K < G.followers.size(); ++K) { if (G.followers[K] != I) { Add(G.followers[K]); } }
				if (Risk > BestRisk)
				{
					Best = I;
					BestRisk = Risk;
				}
			}
			if (Best < 0)
			{
				return false;
			}
			*OutNode = Best;
			return true;
		}

		// Highest value voter the player does not own and that is not immune; a sockpuppet ignores gates, so gated or not does not matter.
		static bool FindBurstTarget(const Sim& S, int* OutNode)
		{
			int Best = -1;
			double BestValue = 0.0;
			for (int I = 0; I < S.NodeCount(); ++I)
			{
				if (S.Opinion(I) == kPlayerId || S.LockRemaining(I) > 0.0)
				{
					continue;
				}
				const double V = S.NodeValue(kPlayerId, I);
				if (Best < 0 || V > BestValue)
				{
					Best = I;
					BestValue = V;
				}
			}
			if (Best < 0)
			{
				return false;
			}
			*OutNode = Best;
			return true;
		}

		BotKind Kind_ = BotKind::Idle;
		Rng R_;
		double Next_ = 0.0;
		bool Winding_ = false;
		double WindupEnd_ = 0.0;
		int ReinforceNode_ = -1;    // a fresh sockpuppet flip that is being reinforced until it launders (only when SockLaunderConv <= 1)
		int ReinforceLeft_ = 0;
	};

	// ---------------------------------------------------------------------------------------
	// PolicyParams name table
	// ---------------------------------------------------------------------------------------
	namespace PolicyDetail
	{
		struct Field
		{
			const char* Name;
			double PolicyParams::* Member;
		};
#define SHIFT_PP(field) { #field, &PolicyParams::field }
		inline const Field* Fields(int* Count)
		{
			static const Field kFields[] = {
				SHIFT_PP(RivalDefendBelow), SHIFT_PP(RivalPlayerMult), SHIFT_PP(RivalCapShare), SHIFT_PP(RivalBandBase),
				SHIFT_PP(RivalBandSlope), SHIFT_PP(RivalBandThreshold), SHIFT_PP(MegaDays), SHIFT_PP(MegaConvBoost),
				SHIFT_PP(MegaAttackMult), SHIFT_PP(OthersInterval), SHIFT_PP(OthersMaxV), SHIFT_PP(OthersTargetsPlayer),
				SHIFT_PP(ModFirstSweepMin), SHIFT_PP(BotInterval), SHIFT_PP(SockMaxHeat), SHIFT_PP(SockWindup), SHIFT_PP(LateSeconds), SHIFT_PP(RallyMinRisk),
			};
			*Count = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));
			return kFields;
		}
#undef SHIFT_PP
	} // namespace PolicyDetail

	inline bool SetPolicyParam(PolicyParams& P, const std::string& Name, double Value)
	{
		int N = 0;
		const PolicyDetail::Field* F = PolicyDetail::Fields(&N);
		for (int I = 0; I < N; ++I)
		{
			if (Name == F[I].Name)
			{
				P.*(F[I].Member) = Value;
				return true;
			}
		}
		return false;
	}

	inline bool GetPolicyParam(const PolicyParams& P, const std::string& Name, double* Out)
	{
		int N = 0;
		const PolicyDetail::Field* F = PolicyDetail::Fields(&N);
		for (int I = 0; I < N; ++I)
		{
			if (Name == F[I].Name)
			{
				*Out = P.*(F[I].Member);
				return true;
			}
		}
		return false;
	}

	inline int PolicyParamCount()
	{
		int N = 0;
		PolicyDetail::Fields(&N);
		return N;
	}

	inline const char* PolicyParamNameAt(int Index)
	{
		int N = 0;
		const PolicyDetail::Field* F = PolicyDetail::Fields(&N);
		return (Index >= 0 && Index < N) ? F[Index].Name : "";
	}

} // namespace ShiftSim
