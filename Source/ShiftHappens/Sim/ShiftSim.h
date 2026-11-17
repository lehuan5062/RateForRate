// Shift Happens - engine-independent numeric core.
//
// Sim owns one jam run: graph, entries, per-node opinion state, the 20 Hz opinion dynamics,
// the player's Posts/Heat bookkeeping and the action API (Plug, Sock, ApplyStrike).
// It does NOT contain the rival, other entries, the Mod Eye schedule, events or phases:
// the behaviour layer (StateTree tasks, or the harness stand-ins) calls this API.
//
// Plain C++17. No Unreal includes, no exceptions, no global state, own PRNG.
// Avoid macro collisions: no identifiers named min/max/TRUE/FALSE/check/ensure.
#pragma once

#include <cstdint>
#include "ShiftGraph.h"
#include "ShiftParams.h"
#include "ShiftRng.h"
#include <vector>

namespace ShiftSim
{

	constexpr int kNoOwner = -1;            // opinion value of an undecided node
	constexpr int kUnreachable = 9999;      // PlugsNeeded() when a node cannot be flipped right now

	enum class ActionResult : uint8_t
	{
		Flipped = 0,       // node changed owner
		Reinforced,        // own node, conviction raised
		Weakened,          // foreign node, conviction lowered but not flipped
		Gated,             // hub ignores the player until enough of its followers back the player
		Locked,            // node is immune (recent flip)
		NoPosts,           // player has less than 1 Post
		Shadowbanned,      // player is shadowbanned after a strike
		Disqualified,      // player reached MaxStrikes
		AlreadyOwned,      // Sock on a node the player already owns
		NotNight,          // Sock while the night-held flag is not set (Params::SockRequiresNight)
		InvalidTarget,     // bad entry / node index
		Overheated,        // Sock that would take Heat to the cap (Params::SockOverheatRefuse): refused, costs nothing
	};

	const char* ActionResultName(ActionResult R);

	struct ActionOutcome
	{
		ActionResult result = ActionResult::InvalidTarget;
		int who = -1;
		int node = -1;
		int prevOwner = kNoOwner;
		int newOwner = kNoOwner;
		double convBefore = 0.0;
		double convAfter = 0.0;
		double lockAfter = 0.0;
		bool flipped = false;
		bool sockpuppet = false;
		double postsSpent = 0.0;
		double heatDelta = 0.0;
		double gateProgress = 0.0;      // filled when result == Gated
		double gateNeeded = 0.0;

		// True when the action was accepted (cost charged, node touched).
		bool Accepted() const
		{
			return result == ActionResult::Flipped || result == ActionResult::Reinforced ||
				result == ActionResult::Weakened;
		}
	};

	// One node whose owner changed during the last Step().
	struct NodeChange
	{
		int node = -1;
		int prevOwner = kNoOwner;
		int newOwner = kNoOwner;
	};

	struct StrikeOutcome
	{
		int strikes = 0;
		bool disqualified = false;
		double shadowbanSeconds = 0.0;
		std::vector<int> revertedNodes;     // sockpuppet flips that were undone
	};

	struct GateInfo
	{
		bool applies = false;               // node is a local hub or celebrity
		bool open = true;                   // Plug works (or the player already owns it)
		double progress = 1.0;              // V-weighted share of followers backing the player
		double needed = 0.0;
		double backingWeight = 0.0;
		double totalWeight = 0.0;
	};

	struct Entry
	{
		int id = 0;
		double quality = 5.0;               // Q, ~3..8
		double clout = 0.0;
		double appeal = 1.0;                // 1 + AppealSlope*(Q-AppealMid)/AppealScale (x LowQualityPenalty)
		double seedFollowers = 0.0;         // sum of follower counts of the nodes owned at t = 0
		double seedWeight = 0.0;            // sum of V of the nodes owned at t = 0
		bool isPlayer = false;
		bool isRival = false;
	};

	// Candidate filter for target queries.
	struct TargetFilter
	{
		double strength = 1.0;              // Plug strength used to compute plugs needed
		double minV = 0.0;                  // only nodes with minV <= V <= maxV
		double maxV = 1.0e18;
		double undecidedMult = 1.0;         // score multiplier for undecided nodes
		double ownerMult[kMaxEntries];      // score multiplier by current owner (0 excludes)
		uint32_t roleMask = 0xFu;           // bit (1 << NodeRole) set = role allowed
		bool respectGates = true;           // gates only ever block the player

		TargetFilter()
		{
			for (int i = 0; i < kMaxEntries; ++i)
			{
				ownerMult[i] = 1.0;
			}
		}
	};

	struct Target
	{
		int node = -1;
		double value = 0.0;                 // V + TargetFollowerCredit * V of non-owned followers
		int plugs = 0;                      // Plugs needed at the filter's strength
		double score = 0.0;                 // value * ownerMult / plugs
	};

	class Sim
	{
	public:
		// Builds graph, entries and initial opinions from (jam, params, seed). Deterministic.
		bool Init(const JamDef& Jam, const Params& P, uint32_t Seed);

		// Advances Params::Dt seconds: Posts regen, Heat, locks, opinion dynamics.
		void Step();

		// ---- player-facing actions (who = entry index; the player is kPlayerId) -----------
		ActionOutcome Plug(int Who, int Node, double Strength = 1.0);
		ActionOutcome Sock(int Node);
		// Behaviour layer calls this when the Mod Eye catches the player or Heat hits the cap.
		StrikeOutcome ApplyStrike();
		// Undoes every sockpuppet flip that is still flagged (back to the previous owner and conviction, immunity cleared) and returns the nodes. A strike does this,
		// and so does the final audit at the end of the jam (Params::FinalAudit); neither is a strike for the audit.
		std::vector<int> RevertSockpuppets();
		// Shift the Blame: takes back the last strike and its shadowban (the reverted sockpuppets stay reverted).
		void ForgiveStrike() { if (Strikes_ > 0) { --Strikes_; } Shadowban_ = 0.0; }
		void SetNightHeld(bool Held) { NightHeld_ = Held; }

		// ---- behaviour-layer hooks (no costs, no checks) ----------------------------------
		// Adds Delta conviction to every node owned by Who (clamped to 0..1). Returns node count.
		int AddConvictionToEntry(int Who, double Delta);
		// Forces a node's state (events, scripted scenarios, tests). Owner = kNoOwner for undecided.
		void SetNodeState(int Node, int Owner, double Conviction, double LockSeconds);
		void SetPostsRegen(double PerSecond) { PostsRegen_ = PerSecond; }
		void SetHeat(double Value);
		void SetPosts(double Value);

		// ---- read access ---------------------------------------------------------------
		const Graph& GetGraph() const { return Graph_; }
		const JamDef& Jam() const { return Jam_; }
		const Params& GetParams() const { return P_; }
		uint32_t Seed() const { return Seed_; }
		int NodeCount() const { return Graph_.NodeCount(); }
		int NumEntries() const { return NumEntries_; }
		int RivalId() const { return RivalId_; }
		const Entry& GetEntry(int E) const { return Entries_[static_cast<size_t>(E)]; }
		int Opinion(int Node) const { return Opinion_[static_cast<size_t>(Node)]; }
		double Conviction(int Node) const { return Conv_[static_cast<size_t>(Node)]; }
		double LockRemaining(int Node) const { return Lock_[static_cast<size_t>(Node)]; }
		bool IsSockpuppet(int Node) const { return Sock_[static_cast<size_t>(Node)] != 0; }
		// Conviction the voter loses per second to opinion pressure as of the last Step() (0 when nobody out-pushes it, or while it is immune). A derived value for
		// presentation: it is not part of StateHash and nothing in the dynamics reads it.
		double Drain(int Node) const { return Drain_[static_cast<size_t>(Node)]; }
		// Seconds until the voter flips away if the pressure stays as it is (a huge number when it is not under pressure).
		double SecondsToFlip(int Node) const
		{
			const double D = Drain_[static_cast<size_t>(Node)];
			return D > 1.0e-9 ? Conv_[static_cast<size_t>(Node)] / D : 1.0e9;
		}
		const std::vector<NodeChange>& LastStepChanges() const { return Changes_; }

		double Time() const { return Time_; }
		int64_t StepCount() const { return StepCount_; }
		double TotalSeconds() const { return static_cast<double>(Jam_.days) * Jam_.secPerDay; }
		int DayIndex() const;               // 0-based, clamped to Days-1
		bool Finished() const { return Time_ + 1.0e-9 >= TotalSeconds(); }

		double Posts() const { return Posts_; }
		double Heat() const { return Heat_; }
		bool NightHeld() const { return NightHeld_; }
		bool InRedZone() const { return Heat_ > P_.HeatRedZone; }
		bool HeatMaxed() const { return Heat_ >= P_.HeatMax - 1.0e-9; }
		int Strikes() const { return Strikes_; }
		bool Disqualified() const;
		double ShadowbanRemaining() const { return Shadowban_; }
		double PostsRegen() const { return PostsRegen_; }

		// ---- tally / ranking -------------------------------------------------------------
		double Tally(int Entry) const { return Tally_[Entry]; }                        // sum of V by opinion
		double TotalWeight() const { return Graph_.totalWeight; }
		double Share(int Entry) const;                                                  // Tally / total V
		double UndecidedShare() const;
		int RankOf(int Entry) const { return Rank_[Entry]; }                            // 0-based
		std::vector<int> Ranking() const;                                               // entry ids, best first
		// Winner by tally, skipping the player if disqualified.
		int Leader() const;
		double SockpuppetWeight() const;                                                // V of sock-owned nodes

		// ---- gates ---------------------------------------------------------------------
		// Gate for entry Who (default: the player). Gates always apply to the player; they apply to
		// other entries only when Params::GatesApplyToAll is set.
		GateInfo Gate(int Node, int Who = kPlayerId) const;
		bool IsGated(int Node, int Who = kPlayerId) const;

		// ---- queries for behaviour layers --------------------------------------------------
		int PlugsNeeded(int Who, int Node, double Strength = 1.0, bool RespectGates = true) const;
		double NodeValue(int Who, int Node) const;
		// All candidates for Who passing the filter, best score first (ties: lower node id).
		void CollectTargets(int Who, const TargetFilter& F, std::vector<Target>* Out) const;
		bool BestTarget(int Who, const TargetFilter& F, Target* Out) const;
		// Lowest-conviction node owned by Who with V >= MinV (and conviction <= MaxConv). -1 if none.
		int WeakestOwned(int Who, double MinV, double MaxConv = 1.0e9) const;
		const std::vector<int>& NodesByCommunity(int Community) const;
		int OwnedCount(int Who) const;

		// Hash of the complete dynamic state (determinism checks).
		uint64_t StateHash() const;

	private:
		void InitEntries(Rng& R);
		void InitOpinions(Rng& R);
		void RecomputeTally();
		void RecomputeRanks();
		double FrontMult(int Rank) const;
		void SetOwner(int Node, int NewOwner);
		int NodeOwnerSafe(int Node) const;

		JamDef Jam_;
		Params P_;
		Graph Graph_;
		uint32_t Seed_ = 0;
		Rng Rng_;

		int NumEntries_ = 9;
		int RivalId_ = 1;
		std::vector<Entry> Entries_;

		std::vector<int> Opinion_;
		std::vector<double> Conv_;
		std::vector<double> Lock_;
		std::vector<uint8_t> Sock_;
		std::vector<int> SockPrevOpinion_;
		std::vector<double> SockPrevConv_;

		std::vector<int> NewOpinion_;
		std::vector<double> NewConv_;
		std::vector<double> Contrib_;
		std::vector<double> Drain_;         // derived, see Drain()

		double Tally_[kMaxEntries];
		int Rank_[kMaxEntries];
		int Order_[kMaxEntries];

		double Time_ = 0.0;
		int64_t StepCount_ = 0;
		double Posts_ = 0.0;
		double PostsRegen_ = 0.5;
		double Heat_ = 0.0;
		double Shadowban_ = 0.0;
		int Strikes_ = 0;
		bool NightHeld_ = false;

		std::vector<NodeChange> Changes_;
		std::vector<int> EmptyNodes_;       // returned for out-of-range community queries
	};

} // namespace ShiftSim
