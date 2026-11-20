// Native behaviour layer: scripted rival, other entries, Mod Eye and player bots, written against the sim core's public API.
// Promoted from Tools/SimHarness/policies.h (the calibrated stand-ins). Each Update() is what a StateTree task will call;
// until the StateTree assets exist (spike S7) the game drives these directly.
#pragma once

#include <cstdint>
#include "Sim/ShiftGraph.h"
#include "Sim/ShiftParams.h"
#include "Sim/ShiftRivalBank.h"
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
		double RivalCapShare = 1.0;          // at/above this share the rival only attacks player-owned nodes (1 = never: since round 10 nobody targets the player just for being the player)
		double RivalBandBase = 1.5;          // strength = base + slope * max(0, playerShare - threshold). 1.5 (was 1.0) since round 8: the rival no longer stacks plugs on one voter (RivalNodeCooldown), so one plug has to do the job: a fresh voter of the player's (80% sure) flips in one plug from 1.44 up
		double RivalBandSlope = 4.0;
		double RivalBandThreshold = 0.15;
		double RivalPostsMax = 3.0;          // the rival's Post bank: every action (a plug on a voter, or a re-plug of a weak hub of its own) costs a Post, so the final push is not unlimited (0 = no bank, no limit)
		double RivalPostsRegen = 0.30;       // ... Posts a second it refills (the rival acts 0.30 times a second before the push, so the bank stays full; the push drains it and the rival falls back to its normal pace: about 37% fewer attacks in the push)
		double RivalDefendCost = 1.0;        // ... Posts a re-plug of a weak hub of its own costs
		double GrudgeSeconds = 30.0;         // an entry prefers the player's voters (RivalPlayerMult, OthersTargetsPlayer) only for this long after the player took a voter from it; otherwise the player's voters are like anyone's
		double RivalNodeCooldown = 30.0;     // seconds a voter the rival has just attacked is left alone, when anything else is worth a Post: it does not hammer one voter all game (0 = it does: the best target every time)
		double MegaDays = 3.0;               // last N days: Mega Campaign
		double MegaConvBoost = 0.3;          // +conviction on all rival nodes at Mega start
		double EyeAwayCount = 0.0;           // scheduled "the Mod Eye looks away" windows per run (0 = none)
		double EyeAwaySeconds = 9.0;         // length of one window
		double EyeAwayEndMargin = 25.0;      // the last window ends at least this long before the jam does (past SockReview, so a burst cannot hide in it)
		double EyeAwayWarn = 5.0;            // seconds of notice before a window starts
		double FinalPushRegen = 1.10;        // the player's Post refill multiplier in the rival's final push (the last MegaDays days), 1 = off
		double MegaAttackMult = 2.0;         // actions per rival tick during Mega
		// stand-in others
		double OthersInterval = 6.0;         // each other entry acts this often (10 until round 10: the other colours were for show)
		double OthersMaxV = 300.0;           // the biggest member or bridge another entry goes for (100 until round 10)
		double OthersTargetsPlayer = 3.0;    // how much readier another entry is to plug a voter the player holds while it has a grudge (GrudgeSeconds after the player took one of its voters); otherwise like any other voter
		double OthersLeaderMult = 2.0;       // how much likelier another entry is to pick a voter of whoever leads the board (1 = no preference: any voter alike): the pack piles onto the front-runner, the rival most of the time, you when you lead
		double OthersHubs = 1.0;             // 1: another entry may also go for a local hub (gated like the rival when Params::GatesApplyToAll is on)
		double OthersHubMaxV = 600.0;        // ... of at most this vote weight
		// stand-in Mod Eye (director)
		double ModFirstSweepMin = 0.0;       // the Eye's cycle is already running at t=0: first sweep ~ U[this, jam ModIntervalMax]
		// player bots
		double BotInterval = 0.2;            // minimum seconds between bot clicks
		double BotStart = 0.0;               // seconds before the bot's first click (the build screen's forecast replays one world with a few different rhythms)
		double BotBlame = 1.0;               // 1: a bot answers the strike that would disqualify it with Shift the Blame (after ReactDelay, like a player who saw the prompt); 0 = never (the Blame-free baseline)
		double BotNoise = 0.0;               // greedy bot: every target's score is jittered by up to +-this fraction before the best is picked, a player who does not find the optimum every time (0 = off)
		double QaGiveCelebs = 0.0;           // QA only: 1 = the player starts the run owning the celebrities (what is a celebrity worth? the playtester: "guarantee to score top 1")
		double SockMaxHeat = 45.0;           // greedy+sock socks only when Heat <= this
		double SockWindup = 0.5;             // seconds the bot holds night before the sock click
		double ReactDelay = 0.6;             // reactor / explorer: seconds a human needs to notice an announced attack before answering it
		double ReactReserve = 1.0;           // reactor / explorer: Posts held back for answering attacks (a human keeps one spare); plugs only start above 1 + this
		double ReactMinValue = 100.0;        // ... and the least vote weight (the voter plus half its player-held neighbours') worth answering at all
		double ReactRallyThreats = 2.0;      // ... a Rally is worth its Posts when this many rings close in on the cluster ...
		double ReactRallyValue = 400.0;      // ... or when the cluster is worth this much (hubs and celebrities)
		double XReact = 1.0;                 // explorer: answer announced attacks (reinforce, or Rally)
		double XRally = 1.0;                 // explorer: a Rally is an allowed answer
		double XSock = 0.0;                  // explorer: sockpuppets on gated hubs while Heat <= SockMaxHeat (the greedy+sock habit)
		double XBurst = 0.0;                 // explorer: a sockpuppet burst in the last LateSeconds (the finisher habit)
		double XIdle = 0.0;                  // explorer: do nothing until the last LateSeconds (the late habit)
		double XReinforceBelow = 0.0;        // explorer: reinforce own voters that would flip back within this many seconds (0 = never)
		double XSockMinV = 0.0;              // explorer: only sockpuppet voters worth at least this much vote weight
		double XSockShare = 1.0;             // explorer: a share of all vote weight the explorer keeps its sockpuppet votes under (on top of the Mod Eye's tolerance)
		double XPlugMinV = 0.0;
		double XWindow = 0.0;                // explorer: sockpuppet the heaviest voter while the Mod Eye looks away (and only then)
		double XWindowBank = 8.0;            // ... and keep the Posts for it: no plugs when a window starts within this many seconds
		double XParry = 1.0;                 // explorer: reinforcing is an allowed answer to an announced attack (0 = Rally only)
		double XValueFirst = 0.0;            // explorer: plug the most valuable reachable voter first instead of the best value per Plug (the tester's snowball)
		double XKeepRally = 0.0;             // explorer: keep the player's biggest voter under a Rally shield (recast when it runs under 1.5 s)              // explorer: only plug voters worth at least this much
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
		Reactor,        // a human-like player: honest greedy play that answers announced attacks (reinforce, or Rally) after a reaction delay
		Explorer,       // every habit of the other bots as switches in PolicyParams (X*), driven by the random search of Tools\explore.ps1
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
		case BotKind::Reactor: return "reactor";
		case BotKind::Explorer: return "explorer";
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
		if (L == "reactor") { *Out = BotKind::Reactor; return true; }
		if (L == "explorer") { *Out = BotKind::Explorer; return true; }
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
		// True while the player took a voter from this entry in the last GrudgeSeconds: only then does it prefer the player's voters.
		static bool Grudge(const Sim& S, const PolicyParams& PP, int Entry) { return S.Time() - S.LastTakenByPlayer(Entry) < PP.GrudgeSeconds; }

		static double IntervalFor(const Sim& S, const PolicyParams& /*PP*/)
		{
			return S.Jam().rivalInterval > 0.1 ? S.Jam().rivalInterval : 2.0;
		}

		void Reset(const Sim& S, const PolicyParams& PP)
		{
			Next_ = IntervalFor(S, PP);
			MegaStarted_ = false;
			Bank_.Reset(PP.RivalPostsMax, S.Time());
			Starved = 0;
			AttacksOn.assign(static_cast<size_t>(S.NodeCount()), 0);
			AttackedAt_.assign(static_cast<size_t>(S.NodeCount()), -1.0e9);
		}

		// The rival's Post bank as of now (0 when there is none: PP.RivalPostsMax = 0), and paying for an action out of it (false: nothing left, the action does not happen).
		double PostsNow(const Sim& S, const PolicyParams& PP) const { return Bank_.PostsAt(S.Time(), PP.RivalPostsMax, PP.RivalPostsRegen); }
		bool Pay(const Sim& S, const PolicyParams& PP, double Cost) { return Bank_.Spend(S.Time(), PP.RivalPostsMax, PP.RivalPostsRegen, Cost); }

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
			if (!Pay(S, PP, PP.RivalDefendCost))
			{
				++Starved;   // no Post left: the hub stays weak for now
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
			F.ownerMult[kPlayerId] = Grudge(S, PP, Rival) ? PP.RivalPlayerMult : 1.0;
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
			// A voter the rival attacked a moment ago is left alone for RivalNodeCooldown seconds, when anything else is worth a Post: it does not hammer one voter all game.
			const bool bCooldown = PP.RivalNodeCooldown > 0.0 && AttackedAt_.size() == static_cast<size_t>(S.NodeCount());
			if (bCooldown)
			{
				NodeMult_.assign(AttackedAt_.size(), 1.0);
				for (size_t I = 0; I < AttackedAt_.size(); ++I)
				{
					if (S.Time() - AttackedAt_[I] < PP.RivalNodeCooldown)
					{
						NodeMult_[I] = 0.0;
					}
				}
				F.nodeMult = NodeMult_.data();
			}
			Target T;
			bool bFound = S.BestTarget(Rival, F, &T);
			if (!bFound && bCooldown)
			{
				F.nodeMult = nullptr;   // nothing else is worth a plug: the voter it just attacked is fair game again
				bFound = S.BestTarget(Rival, F, &T);
			}
			if (bFound)
			{
				if (!Pay(S, PP, 1.0))
				{
					++Starved;   // no Post left: the attack does not happen (a parried attack still cost its Post)
					return;
				}
				const ActionOutcome O = S.Plug(Rival, T.node, Strength);
				++Attacks;
				if (static_cast<size_t>(T.node) < AttackedAt_.size())
				{
					AttackedAt_[static_cast<size_t>(T.node)] = S.Time();
					++AttacksOn[static_cast<size_t>(T.node)];
				}
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
		int Starved = 0;   // actions that did not happen for want of a Post
		std::vector<int> AttacksOn;   // statistics: the rival's attacks on each voter

	private:
		double Next_ = 0.0;
		bool MegaStarted_ = false;
		RivalBank Bank_;
		std::vector<double> AttackedAt_;   // per voter: sim time of the rival's last attack on it
		std::vector<double> NodeMult_;     // scratch: the target filter's per-voter multiplier
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

		// One action of one other entry: the best low-weight target (what the StateTree task calls). With OthersLeaderMult the pack goes after whoever leads the board, and with
		// OthersHubs it may also take a local hub (under the gates: an entry needs a foothold among a hub's followers, like the player and the rival).
		static void Act(Sim& S, const PolicyParams& PP, int Entry)
		{
			TargetFilter F;
			F.maxV = PP.OthersMaxV;
			F.ownerMult[kPlayerId] = StandInRival::Grudge(S, PP, Entry) ? PP.OthersTargetsPlayer : 1.0;
			F.roleMask = (1u << static_cast<uint32_t>(NodeRole::Member)) | (1u << static_cast<uint32_t>(NodeRole::Bridge));
			if (PP.OthersLeaderMult != 1.0)
			{
				const int Leader = S.Leader();
				if (Leader >= 0 && Leader != Entry && Leader != kPlayerId)
				{
					F.ownerMult[Leader] *= PP.OthersLeaderMult;
				}
			}
			Target T;
			bool bFound = S.BestTarget(Entry, F, &T);
			if (PP.OthersHubs > 0.5)
			{
				TargetFilter H = F;
				H.maxV = PP.OthersHubMaxV;
				H.roleMask = 1u << static_cast<uint32_t>(NodeRole::LocalHub);
				Target Hub;
				if (S.BestTarget(Entry, H, &Hub) && (!bFound || Hub.score > T.score))
				{
					T = Hub;
					bFound = true;
				}
			}
			if (bFound)
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
				if (S.Suspicious())
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
		// The first click is not before this sim time.
		void SetStart(double Seconds) { Next_ = Seconds; }

		// NextSweep: sim time of the next Mod Eye sweep. The Eye's countdown is on the HUD, so a human player knows it too; 1e30 when there is none.
		void Update(Sim& S, const PolicyParams& PP, double NextSweep = 1.0e30, double NextWindow = 1.0e30)
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
			if (Kind_ == BotKind::Reactor || Kind_ == BotKind::Explorer)
			{
				UpdateAdvanced(S, PP, NextSweep, NextWindow);
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
			if (PickTarget(S, PP, F, &T))
			{
				LastOutcome = S.Plug(kPlayerId, T.node, 1.0);
				++Actions;
			}
		}

		ActionOutcome LastOutcome;   // statistics (debug)
		int Actions = 0;

	private:
		// The best target by score, or with PP.BotNoise the best after every score was jittered by up to +-BotNoise (from the bot's own stream: with no noise nothing is drawn).
		bool PickTarget(const Sim& S, const PolicyParams& PP, const TargetFilter& F, Target* Out)
		{
			if (!(PP.BotNoise > 0.0))
			{
				return S.BestTarget(kPlayerId, F, Out);
			}
			std::vector<Target> All;
			S.CollectTargets(kPlayerId, F, &All);
			bool bHave = false;
			double BestScore = 0.0;
			for (const Target& C : All)
			{
				const double Jittered = C.score * (1.0 + PP.BotNoise * (2.0 * R_.NextDouble() - 1.0));
				if (!bHave || Jittered > BestScore)
				{
					*Out = C;
					BestScore = Jittered;
					bHave = true;
				}
			}
			return bHave;
		}

		struct BotStyle
		{
			bool React = false;
			bool Rally = false;
			bool Sock = false;
			bool Burst = false;
			bool IdleUntilFinale = false;
			double ReinforceBelow = 0.0;
			double SockMinV = 0.0;
			double SockShare = 1.0;
			bool Parry = true;
			bool Window = false;
			double WindowBank = 8.0;
			bool ValueFirst = false;
			bool KeepRally = false;
		};

		static BotStyle StyleFor(BotKind K, const PolicyParams& PP)
		{
			BotStyle B;
			if (K == BotKind::Reactor)
			{
				B.React = true;
				B.Rally = true;
			}
			else
			{
				B.React = PP.XReact > 0.5;
				B.Rally = PP.XRally > 0.5;
				B.Sock = PP.XSock > 0.5;
				B.Burst = PP.XBurst > 0.5;
				B.IdleUntilFinale = PP.XIdle > 0.5;
				B.ReinforceBelow = PP.XReinforceBelow;
				B.SockMinV = PP.XSockMinV;
				B.SockShare = PP.XSockShare;
				B.Parry = PP.XParry > 0.5;
				B.Window = PP.XWindow > 0.5;
				B.WindowBank = PP.XWindowBank;
				B.ValueFirst = PP.XValueFirst > 0.5;
				B.KeepRally = PP.XKeepRally > 0.5;
			}
			return B;
		}

		// The reactor and the explorer: the other bots' habits as switches, plus answers to announced attacks.
		void UpdateAdvanced(Sim& S, const PolicyParams& PP, double NextSweep, double NextWindow)
		{
			const BotStyle B = StyleFor(Kind_, PP);
			const double Now = S.Time();
			const bool bFinale = (B.Burst || B.IdleUntilFinale) && S.TotalSeconds() - Now <= PP.LateSeconds + 1.0e-9;
			if (B.IdleUntilFinale && !bFinale)
			{
				return;
			}

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
					if ((bFinale || bWindowSock_) ? FindBurstTarget(S, &Node, B.SockShare) : FindGatedHub(S, &Node, B.SockMinV, B.SockShare))
					{
						LastOutcome = S.Sock(Node);
						++Actions;
						if (LastOutcome.result == ActionResult::Flipped && S.GetParams().SockLaunderConv <= 1.0)
						{
							ReinforceNode_ = Node;
							ReinforceLeft_ = ReinforcesToLaunder(S.GetParams());
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
					ReinforceNode_ = -1;
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

			if (B.Window)
			{
				const Params& P = S.GetParams();
				// The Mod Eye looks away: sockpuppet the heaviest voter while it does, and save the Posts for it beforehand.
				if (S.EyeAwayRemaining() > 0.0)
				{
					int Target = -1;
					if (S.Posts() >= 1.0 + (P.SockLaunderConv <= 1.0 ? ReinforcesToLaunder(P) : 0) && FindBurstTarget(S, &Target, B.SockShare))
					{
						Winding_ = true;
						bWindowSock_ = true;
						WindupEnd_ = Now + PP.SockWindup;
						S.SetNightHeld(true);
						return;
					}
				}
				else if (NextWindow - Now > 0.0 && NextWindow - Now <= B.WindowBank && S.Posts() < P.PostsMax - 1.0e-9)
				{
					return;
				}
			}
			if (B.KeepRally && KeepRally(S))
			{
				return;
			}
			if (B.React && React(S, PP, B))
			{
				return;
			}
			if (B.ReinforceBelow > 0.0 && ReinforceAtRisk(S, B.ReinforceBelow))
			{
				return;
			}
			if (bFinale && B.Burst)
			{
				Finale(S, PP, NextSweep);
				return;
			}
			if (B.Sock && S.Heat() <= PP.SockMaxHeat)
			{
				int Node = -1;
				if (FindGatedHub(S, &Node, B.SockMinV, B.SockShare))
				{
					Winding_ = true;
					bWindowSock_ = false;
					WindupEnd_ = Now + PP.SockWindup;
					S.SetNightHeld(true);
					return;
				}
			}
			if (B.React && S.Posts() < 1.0 + PP.ReactReserve - 1.0e-9)
			{
				return;   // keep the reserve for the next announced attack
			}
			TargetFilter F;
			F.minV = PP.XPlugMinV;
			Target Tgt;
			bool bHave = false;
			if (B.ValueFirst)
			{
				std::vector<Target> All;
				S.CollectTargets(kPlayerId, F, &All);
				for (const Target& C : All)
				{
					if (!bHave || C.value > Tgt.value)
					{
						Tgt = C;
						bHave = true;
					}
				}
			}
			else
			{
				bHave = S.BestTarget(kPlayerId, F, &Tgt);
			}
			if (bHave)
			{
				LastOutcome = S.Plug(kPlayerId, Tgt.node, 1.0);
				++Actions;
			}
		}

		// The castle: keep the heaviest voter of the player's under a Rally shield, recasting as soon as the shield runs low.
		bool KeepRally(Sim& S)
		{
			if (S.RallyCooldownRemaining() > 0.0 || S.Posts() < S.GetParams().RallyCost)
			{
				return false;
			}
			int Best = -1;
			double BestWeight = 0.0;
			for (int I = 0; I < S.NodeCount(); ++I)
			{
				const double W = S.GetGraph().nodes[static_cast<size_t>(I)].weight;
				if (S.Opinion(I) == kPlayerId && (Best < 0 || W > BestWeight))
				{
					Best = I;
					BestWeight = W;
				}
			}
			if (Best < 0 || S.ShieldRemaining(Best) >= 1.5)
			{
				return false;
			}
			LastOutcome = S.Rally(Best);
			++Actions;
			return true;
		}

		// Answer the most valuable announced attack that has been on screen for ReactDelay and would flip its voter. A Rally when it is ready, affordable and worth it
		// (ReactRallyThreats or more rings on the cluster, or a cluster worth ReactRallyValue; always when there is no other answer), else a parry: reinforcing the ringed voter.
		// Returns true when it acted.
		bool React(Sim& S, const PolicyParams& PP, const BotStyle& B)
		{
			const Params& P = S.GetParams();
			const PendingAttack* Best = nullptr;
			double BestValue = 0.0;
			for (const PendingAttack& A : S.PendingAttacks())
			{
				if (A.total - A.timeLeft < PP.ReactDelay - 1.0e-9 || A.timeLeft < 0.12)
				{
					continue;   // not noticed yet, or too late to act
				}
				if (S.Opinion(A.node) != kPlayerId || S.LockRemaining(A.node) > 0.0)
				{
					continue;
				}
				const GraphNode& G = S.GetGraph().nodes[static_cast<size_t>(A.node)];
				double Value = G.weight;
				for (size_t K = 0; K < G.follows.size(); ++K)
				{
					if (S.Opinion(G.follows[K]) == kPlayerId) { Value += 0.5 * S.GetGraph().nodes[static_cast<size_t>(G.follows[K])].weight; }
				}
				for (size_t K = 0; K < G.followers.size(); ++K)
				{
					if (S.Opinion(G.followers[K]) == kPlayerId) { Value += 0.5 * S.GetGraph().nodes[static_cast<size_t>(G.followers[K])].weight; }
				}
				if (Value > BestValue)
				{
					BestValue = Value;
					Best = &A;
				}
			}
			if (Best == nullptr || BestValue < PP.ReactMinValue)
			{
				return false;
			}
			const double Conv = S.Conviction(Best->node);
			const double Drain = P.PlugDrain * Best->strength;
			if (Conv - Drain > P.PlugFlipThreshold + 1.0e-9)
			{
				return false;   // it only dents the voter: not worth a Post
			}
			// the rings on the same cluster: the ringed voter and the player's voters one hop away
			int Rings = 0;
			const GraphNode& H = S.GetGraph().nodes[static_cast<size_t>(Best->node)];
			for (const PendingAttack& A : S.PendingAttacks())
			{
				bool bSame = A.node == Best->node;
				for (size_t K = 0; K < H.follows.size() && !bSame; ++K) { bSame = H.follows[K] == A.node; }
				for (size_t K = 0; K < H.followers.size() && !bSame; ++K) { bSame = H.followers[K] == A.node; }
				Rings += (bSame && S.Opinion(A.node) == kPlayerId) ? 1 : 0;
			}
			const bool bRallyWorth = B.Rally && (!B.Parry || Rings >= static_cast<int>(PP.ReactRallyThreats + 0.5) || BestValue >= PP.ReactRallyValue);
			if (bRallyWorth && S.RallyCooldownRemaining() <= 0.0 && S.Posts() >= P.RallyCost)
			{
				LastOutcome = S.Rally(Best->node);
				++Actions;
				return true;
			}
			const bool bParry = P.ParryOnReinforce > 0.5;
			const double Reinforced = Conv + P.PlugReinforce > 1.0 ? 1.0 : Conv + P.PlugReinforce;
			const bool bReinforceSaves = Reinforced - Drain > P.PlugFlipThreshold + 1.0e-9;
			const double Cost = bParry && P.ParryCost > 1.0 ? P.ParryCost : 1.0;
			if (B.Parry && (bParry || bReinforceSaves) && S.Posts() >= Cost)
			{
				LastOutcome = S.Plug(kPlayerId, Best->node, 1.0);
				++Actions;
				return true;
			}
			return false;
		}

		// Reinforce the heaviest own voter that would flip back within the given number of seconds and is not strong already.
		bool ReinforceAtRisk(Sim& S, double WithinSeconds)
		{
			int Best = -1;
			double BestWeight = 0.0;
			for (int I = 0; I < S.NodeCount(); ++I)
			{
				if (S.Opinion(I) != kPlayerId || S.LockRemaining(I) > 0.0 || S.Conviction(I) > 0.7 || S.SecondsToFlip(I) >= WithinSeconds)
				{
					continue;
				}
				const double W = S.GetGraph().nodes[static_cast<size_t>(I)].weight;
				if (Best < 0 || W > BestWeight)
				{
					Best = I;
					BestWeight = W;
				}
			}
			if (Best < 0)
			{
				return false;
			}
			LastOutcome = S.Plug(kPlayerId, Best, 1.0);
			++Actions;
			return true;
		}

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
			if ((Kind_ == BotKind::Finisher || Kind_ == BotKind::Explorer) && S.Posts() >= Reserve + 1.0)
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

		// A sockpuppet on this voter would take the sockpuppet share over what the Mod Eye tolerates: a careful player (and a bot) skips it.
		static bool BreaksTolerance(const Sim& S, int Node, double ShareCap = 1.0)
		{
			const Params& P = S.GetParams();
			const double Cap = ShareCap > 0.0 ? ShareCap : 1.0;   // 0 = no cap of its own
			const double Limit = P.SockTolerance < Cap ? P.SockTolerance : Cap;
			return Limit < 1.0 && S.SockBoughtWeight() + S.GetGraph().nodes[static_cast<size_t>(Node)].weight > Limit * S.TotalWeight();
		}

		// Best (highest value) hub or celebrity the player cannot Plug yet and that is not immune.
		static bool FindGatedHub(const Sim& S, int* OutNode, double MinWeight = 0.0, double ShareCap = 1.0)
		{
			int Best = -1;
			double BestValue = 0.0;
			for (int I = 0; I < S.NodeCount(); ++I)
			{
				if (!S.GetGraph().IsGateNode(I) || S.Opinion(I) == kPlayerId || S.LockRemaining(I) > 0.0)
				{
					continue;
				}
				if (!S.IsGated(I) || S.GetGraph().nodes[static_cast<size_t>(I)].weight < MinWeight || BreaksTolerance(S, I, ShareCap))
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
		static bool FindBurstTarget(const Sim& S, int* OutNode, double ShareCap = 1.0)
		{
			int Best = -1;
			double BestValue = 0.0;
			for (int I = 0; I < S.NodeCount(); ++I)
			{
				if (S.Opinion(I) == kPlayerId || S.LockRemaining(I) > 0.0 || BreaksTolerance(S, I, ShareCap))
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
		bool bWindowSock_ = false;   // the sockpuppet being wound up is for a Mod Eye window (any heaviest voter, not only a gated hub)
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
				SHIFT_PP(RivalBandSlope), SHIFT_PP(RivalBandThreshold), SHIFT_PP(RivalPostsMax), SHIFT_PP(RivalPostsRegen), SHIFT_PP(RivalDefendCost), SHIFT_PP(GrudgeSeconds), SHIFT_PP(RivalNodeCooldown), SHIFT_PP(MegaDays), SHIFT_PP(MegaConvBoost),
				SHIFT_PP(MegaAttackMult), SHIFT_PP(FinalPushRegen), SHIFT_PP(EyeAwayCount), SHIFT_PP(EyeAwaySeconds), SHIFT_PP(EyeAwayEndMargin), SHIFT_PP(EyeAwayWarn), SHIFT_PP(OthersInterval), SHIFT_PP(OthersMaxV), SHIFT_PP(OthersTargetsPlayer), SHIFT_PP(OthersLeaderMult), SHIFT_PP(OthersHubs), SHIFT_PP(OthersHubMaxV),
				SHIFT_PP(ModFirstSweepMin), SHIFT_PP(BotInterval), SHIFT_PP(BotStart), SHIFT_PP(BotNoise), SHIFT_PP(BotBlame), SHIFT_PP(QaGiveCelebs), SHIFT_PP(SockMaxHeat), SHIFT_PP(SockWindup), SHIFT_PP(LateSeconds), SHIFT_PP(RallyMinRisk), SHIFT_PP(ReactDelay), SHIFT_PP(ReactReserve), SHIFT_PP(ReactMinValue), SHIFT_PP(ReactRallyThreats), SHIFT_PP(ReactRallyValue), SHIFT_PP(XReact), SHIFT_PP(XRally), SHIFT_PP(XSock), SHIFT_PP(XBurst),
				SHIFT_PP(XIdle), SHIFT_PP(XReinforceBelow), SHIFT_PP(XPlugMinV), SHIFT_PP(XSockMinV), SHIFT_PP(XSockShare), SHIFT_PP(XParry), SHIFT_PP(XWindow), SHIFT_PP(XWindowBank), SHIFT_PP(XValueFirst), SHIFT_PP(XKeepRally),
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
