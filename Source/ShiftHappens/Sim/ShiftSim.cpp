
#include "ShiftSim.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "ShiftDetMath.h"
#include "ShiftGraph.h"
#include "ShiftParams.h"
#include "ShiftRng.h"
#include <vector>

namespace ShiftSim
{

	namespace
	{
		constexpr double kSimEps = 1.0e-9;

		size_t SimIx(int I)
		{
			return static_cast<size_t>(I);
		}

		void SimHashMix(uint64_t& H, uint64_t V)
		{
			H ^= V;
			H *= 1099511628211ull;
		}

		uint64_t SimDoubleBits(double D)
		{
			uint64_t B = 0;
			std::memcpy(&B, &D, sizeof(B));
			return B;
		}
	} // namespace

	const char* ActionResultName(ActionResult R)
	{
		switch (R)
		{
		case ActionResult::Flipped: return "Flipped";
		case ActionResult::Reinforced: return "Reinforced";
		case ActionResult::Weakened: return "Weakened";
		case ActionResult::Gated: return "Gated";
		case ActionResult::Locked: return "Locked";
		case ActionResult::NoPosts: return "NoPosts";
		case ActionResult::Shadowbanned: return "Shadowbanned";
		case ActionResult::Disqualified: return "Disqualified";
		case ActionResult::AlreadyOwned: return "AlreadyOwned";
		case ActionResult::NotNight: return "NotNight";
		case ActionResult::InvalidTarget: return "InvalidTarget";
		case ActionResult::Overheated: return "Overheated";
		case ActionResult::Rallied: return "Rallied";
		case ActionResult::OnCooldown: return "OnCooldown";
		case ActionResult::NotYours: return "NotYours";
		case ActionResult::Telegraphed: return "Telegraphed";
		}
		return "?";
	}

	// ---------------------------------------------------------------------------------------
	// Init
	// ---------------------------------------------------------------------------------------

	bool Sim::Init(const JamDef& Jam, const Params& P, uint32_t Seed)
	{
		return Init(Jam, P, Seed, Seed);
	}

	bool Sim::Init(const JamDef& Jam, const Params& P, uint32_t Seed, uint32_t DynSeed)
	{
		Jam_ = Jam;
		P_ = P;
		Seed_ = Seed;
		DynSeed_ = DynSeed;
		NumEntries_ = Jam.entries < 3 ? 3 : (Jam.entries > kMaxEntries ? kMaxEntries : Jam.entries);
		RivalId_ = (Jam.rivalId >= 1 && Jam.rivalId < NumEntries_) ? Jam.rivalId : 1;

		if (!BuildGraph(Jam_, P_, Seed, &Graph_))
		{
			return false;
		}

		const size_t Count = static_cast<size_t>(Graph_.NodeCount());
		Opinion_.assign(Count, kNoOwner);
		Conv_.assign(Count, 0.0);
		Lock_.assign(Count, 0.0);
		Sock_.assign(Count, 0);
		SockPrevOpinion_.assign(Count, kNoOwner);
		SockPrevConv_.assign(Count, 0.0);
		SockAt_.assign(Count, -1.0);
		NewOpinion_.assign(Count, kNoOwner);
		NewConv_.assign(Count, 0.0);
		Contrib_.assign(Count, 0.0);
		Drain_.assign(Count, 0.0);
		Challenger_.assign(Count, kNoOwner);
		Shield_.assign(Count, 0.0);
		Pending_.clear();
		AttackStats_ = AttackCounters();
		Flipper_.assign(Count, kNoOwner);
		for (int e = 0; e < kMaxEntries; ++e)
		{
			TakenByPlayerAt_[e] = -1.0e9;
			TakenByPlayerNode_[e] = -1;
		}
		for (int e = 0; e < kMaxEntries; ++e)
		{
			Tally_[e] = 0.0;
			Rank_[e] = e;
			Order_[e] = e;
		}

		Rng R(Rng::Derive(Seed, 2u));
		InitEntries(R);
		InitOpinions(R);
		Rng_.Seed(Rng::Derive(DynSeed, 4u));

		Time_ = 0.0;
		StepCount_ = 0;
		PostsRegen_ = Jam_.regen;
		Posts_ = MinOf(P_.PostsStart, P_.PostsMax);
		Heat_ = 0.0;
		Shadowban_ = 0.0;
		RallyCooldown_ = 0.0;
		EyeAway_ = 0.0;
		Strikes_ = 0;
		NightHeld_ = false;
		Changes_.clear();

		RecomputeTally();
		for (int i = 0; i < Graph_.NodeCount(); ++i)
		{
			const int O = Opinion_[SimIx(i)];
			if (O >= 0)
			{
				Entries_[SimIx(O)].seedFollowers += static_cast<double>(Graph_.nodes[SimIx(i)].followerCount);
				Entries_[SimIx(O)].seedWeight += Graph_.nodes[SimIx(i)].weight;
			}
		}
		return true;
	}

	void Sim::InitEntries(Rng& R)
	{
		Entries_.assign(SimIx(NumEntries_), Entry());
		for (int e = 0; e < NumEntries_; ++e)
		{
			Entry& En = Entries_[SimIx(e)];
			En.id = e;
			En.isPlayer = (e == kPlayerId);
			En.isRival = (e == RivalId_);
			const double Q = R.Uniform(P_.QualityMin, P_.QualityMax);
			const double Clout = R.Uniform(P_.OtherCloutMin, P_.OtherCloutMax);
			En.quality = En.isPlayer ? (P_.PlayerBaseQuality + P_.PlayerBuildBonus) : Q;
			En.clout = En.isPlayer ? P_.PlayerClout : (En.isRival ? P_.RivalClout : Clout);
			double Appeal = 1.0 + P_.AppealSlope * (En.quality - P_.AppealMid) / P_.AppealScale;
			if (En.quality < P_.LowQualityThreshold)
			{
				Appeal *= P_.LowQualityPenalty;
			}
			En.appeal = Appeal;
		}
	}

	void Sim::InitOpinions(Rng& R)
	{
		const Graph& G = Graph_;

		// Each community champions one "other" entry.
		std::vector<int> Others;
		for (int e = 0; e < NumEntries_; ++e)
		{
			if (e != kPlayerId && e != RivalId_)
			{
				Others.push_back(e);
			}
		}
		const int OtherCount = static_cast<int>(Others.size());
		R.Shuffle(Others, OtherCount);

		for (size_t C = 0; C < G.communityNodes.size(); ++C)
		{
			const int Champ = OtherCount > 0 ? Others[C % static_cast<size_t>(OtherCount)] : kNoOwner;
			const std::vector<int>& L = G.communityNodes[C];
			for (size_t k = 0; k < L.size(); ++k)
			{
				const int Id = L[k];
				const bool IsHub = G.nodes[SimIx(Id)].role == NodeRole::LocalHub;
				if (Champ != kNoOwner && (IsHub || R.Chance(P_.ChampionFraction)))
				{
					Opinion_[SimIx(Id)] = Champ;
					Conv_[SimIx(Id)] = R.Uniform(P_.ChampionConvMin, P_.ChampionConvMax);
				}
			}
		}

		// The celebrity hubs and some of their followers. Until round 11 the rival owned every celebrity; with Params::CelebOwners the first is the rival's and the others belong to two
		// other entries (drawn from a stream of their own so that no other draw moves).
		std::vector<int> CelebOwner(G.celebrities.size(), RivalId_);
		if (P_.CelebOwners > 0.5 && G.celebrities.size() > 1 && OtherCount > 0)
		{
			Rng OwnerRng;
			OwnerRng.Seed(Rng::Derive(Seed_, 0x43454Cu));
			std::vector<int> Pool = Others;
			OwnerRng.Shuffle(Pool, static_cast<int>(Pool.size()));
			for (size_t i = 1; i < G.celebrities.size() && i - 1 < Pool.size(); ++i)
			{
				CelebOwner[i] = Pool[i - 1];
			}
		}
		for (size_t i = 0; i < G.celebrities.size(); ++i)
		{
			Opinion_[SimIx(G.celebrities[i])] = CelebOwner[i];
			Conv_[SimIx(G.celebrities[i])] = P_.RivalConv;
		}
		for (int Id = 0; Id < G.NodeCount(); ++Id)
		{
			const GraphNode& Node = G.nodes[SimIx(Id)];
			if (Node.role == NodeRole::Celebrity)
			{
				continue;
			}
			int FollowedCeleb = -1;
			for (size_t k = 0; k < Node.follows.size() && FollowedCeleb < 0; ++k)
			{
				for (size_t z = 0; z < G.celebrities.size(); ++z)
				{
					if (G.celebrities[z] == Node.follows[k])
					{
						FollowedCeleb = static_cast<int>(z);
						break;
					}
				}
			}
			if (FollowedCeleb >= 0 && R.Chance(CelebOwner[static_cast<size_t>(FollowedCeleb)] == RivalId_ ? Jam_.rivalFollowerFraction : P_.OtherCelebFollowerFraction))
			{
				Opinion_[SimIx(Id)] = CelebOwner[static_cast<size_t>(FollowedCeleb)];
				Conv_[SimIx(Id)] = R.Uniform(P_.RivalFollowerConvMin, P_.RivalFollowerConvMax);
			}
		}

		// The player starts with a few low-degree members.
		const int Need = RoundToInt(P_.PlayerStartNodes);
		if (Need > 0)
		{
			std::vector<int> Cands;
			int MaxF = RoundToInt(P_.PlayerStartMaxFollowers);
			for (;;)
			{
				Cands.clear();
				for (int Id = 0; Id < G.NodeCount(); ++Id)
				{
					const GraphNode& Node = G.nodes[SimIx(Id)];
					if (Node.role == NodeRole::Member && static_cast<int>(Node.followers.size()) <= MaxF)
					{
						Cands.push_back(Id);
					}
				}
				if (static_cast<int>(Cands.size()) >= Need || MaxF > 1000)
				{
					break;
				}
				++MaxF;
			}
			R.Shuffle(Cands, static_cast<int>(Cands.size()));
			std::vector<uint8_t> UsedCommunity(G.communityNodes.size(), 0);
			std::vector<uint8_t> Taken(Cands.size(), 0);
			int Got = 0;
			for (size_t k = 0; k < Cands.size() && Got < Need; ++k)
			{
				const int C = G.nodes[SimIx(Cands[k])].community;
				if (UsedCommunity[SimIx(C)] == 0)
				{
					UsedCommunity[SimIx(C)] = 1;
					Taken[k] = 1;
					++Got;
				}
			}
			for (size_t k = 0; k < Cands.size() && Got < Need; ++k)
			{
				if (Taken[k] == 0)
				{
					Taken[k] = 1;
					++Got;
				}
			}
			for (size_t k = 0; k < Cands.size(); ++k)
			{
				if (Taken[k] != 0)
				{
					Opinion_[SimIx(Cands[k])] = kPlayerId;
					Conv_[SimIx(Cands[k])] = P_.PlayerStartConv;
				}
			}
		}
	}

	// ---------------------------------------------------------------------------------------
	// Tally / ranks
	// ---------------------------------------------------------------------------------------

	void Sim::RecomputeTally()
	{
		for (int e = 0; e < kMaxEntries; ++e)
		{
			Tally_[e] = 0.0;
		}
		for (int i = 0; i < Graph_.NodeCount(); ++i)
		{
			const int O = Opinion_[SimIx(i)];
			if (O >= 0)
			{
				Tally_[O] += Graph_.nodes[SimIx(i)].weight;
			}
		}
		RecomputeRanks();
	}

	void Sim::RecomputeRanks()
	{
		const int N = NumEntries_;
		for (int e = 0; e < N; ++e)
		{
			Order_[e] = e;
		}
		for (int i = 1; i < N; ++i)
		{
			const int V = Order_[i];
			int j = i - 1;
			while (j >= 0)
			{
				const int Other = Order_[j];
				const bool VBetter = (Tally_[V] > Tally_[Other]) || (Tally_[V] == Tally_[Other] && V < Other);
				if (!VBetter)
				{
					break;
				}
				Order_[j + 1] = Other;
				--j;
			}
			Order_[j + 1] = V;
		}
		for (int r = 0; r < N; ++r)
		{
			Rank_[Order_[r]] = r;
		}
	}

	double Sim::FrontMult(int Rank) const
	{
		if (Rank == 0) { return P_.Front1; }
		if (Rank == 1) { return P_.Front2; }
		if (Rank == 2) { return P_.Front3; }
		return 1.0;
	}

	void Sim::SetOwner(int Node, int NewOwner)
	{
		const int Old = Opinion_[SimIx(Node)];
		if (Old == NewOwner)
		{
			return;
		}
		const double W = Graph_.nodes[SimIx(Node)].weight;
		if (Old >= 0)
		{
			Tally_[Old] -= W;
		}
		if (NewOwner >= 0)
		{
			Tally_[NewOwner] += W;
		}
		Opinion_[SimIx(Node)] = NewOwner;
		Shield_[SimIx(Node)] = 0.0;
	}

	double Sim::Share(int Entry) const
	{
		return Graph_.totalWeight > 0.0 ? Tally_[Entry] / Graph_.totalWeight : 0.0;
	}

	double Sim::SympathyPressure(int* From, int* To) const
	{
		if (!(P_.Backlash > 0.0) || NumEntries_ < 2)
		{
			return 0.0;
		}
		const int Front = Order_[0];
		const int Behind = Order_[1];
		const double Lead = Share(Front) - Share(Behind);
		if (!(Lead > P_.BacklashLead))
		{
			return 0.0;
		}
		if (From != nullptr) { *From = Front; }
		if (To != nullptr) { *To = Behind; }
		return P_.Backlash * (Lead - P_.BacklashLead);
	}

	double Sim::UndecidedShare() const
	{
		double Sum = 0.0;
		for (int e = 0; e < NumEntries_; ++e)
		{
			Sum += Tally_[e];
		}
		return Graph_.totalWeight > 0.0 ? (Graph_.totalWeight - Sum) / Graph_.totalWeight : 0.0;
	}

	std::vector<int> Sim::Ranking() const
	{
		std::vector<int> Out(Order_, Order_ + NumEntries_);
		return Out;
	}

	int Sim::Leader() const
	{
		for (int r = 0; r < NumEntries_; ++r)
		{
			const int E = Order_[r];
			if (E == kPlayerId && Disqualified())
			{
				continue;
			}
			return E;
		}
		return Order_[0];
	}

	double Sim::SockpuppetWeight() const
	{
		double Sum = 0.0;
		for (int i = 0; i < Graph_.NodeCount(); ++i)
		{
			if (Sock_[SimIx(i)] != 0)
			{
				Sum += Graph_.nodes[SimIx(i)].weight;
			}
		}
		return Sum;
	}

	double Sim::SockBoughtWeight() const
	{
		double Sum = 0.0;
		for (int i = 0; i < Graph_.NodeCount(); ++i)
		{
			if (Sock_[SimIx(i)] != 0 || (SockAt_[SimIx(i)] >= 0.0 && Opinion_[SimIx(i)] == kPlayerId))
			{
				Sum += Graph_.nodes[SimIx(i)].weight;
			}
		}
		return Sum;
	}

	bool Sim::OverTolerance() const
	{
		return P_.SockTolerance < 1.0 && SockBoughtWeight() > P_.SockTolerance * TotalWeight() + kSimEps;
	}

	double Sim::AuditWeight() const
	{
		const double Since = TotalSeconds() - P_.SockReview - kSimEps;
		double Sum = 0.0;
		for (int i = 0; i < Graph_.NodeCount(); ++i)
		{
			const size_t Ix = SimIx(i);
			if (Sock_[Ix] != 0 || (P_.SockReview > 0.0 && SockAt_[Ix] >= 0.0 && SockAt_[Ix] >= Since && Opinion_[Ix] == kPlayerId))
			{
				Sum += Graph_.nodes[Ix].weight;
			}
		}
		return Sum;
	}

	int Sim::DayIndex() const
	{
		int D = static_cast<int>(Time_ / Jam_.secPerDay + 1.0e-9);
		if (D >= Jam_.days)
		{
			D = Jam_.days - 1;
		}
		return D < 0 ? 0 : D;
	}

	bool Sim::Disqualified() const
	{
		return Strikes_ >= RoundToInt(P_.MaxStrikes);
	}

	// ---------------------------------------------------------------------------------------
	// Step: the 20 Hz opinion dynamics
	// ---------------------------------------------------------------------------------------

	void Sim::Step()
	{
		const double Dt = P_.Dt;
		Time_ += Dt;
		++StepCount_;
		Changes_.clear();

		// Player economy.
		Posts_ = MinOf(P_.PostsMax, Posts_ + PostsRegen_ * Dt);
		if (Shadowban_ > 0.0)
		{
			Shadowban_ = MaxOf(0.0, Shadowban_ - Dt);
		}
		RallyCooldown_ = MaxOf(0.0, RallyCooldown_ - Dt);

		// Announced attacks that are due now. The target is checked again: if the player lost it meanwhile the attack fizzles, if it is immune (a Rally, a fresh
		// flip) the plug is refused, and a reinforcement in between changes the arithmetic as usual.
		for (size_t k = 0; k < Pending_.size();)
		{
			Pending_[k].timeLeft -= Dt;
			if (Pending_[k].timeLeft > 1.0e-9)
			{
				++k;
				continue;
			}
			const PendingAttack A = Pending_[k];
			Pending_.erase(Pending_.begin() + static_cast<std::ptrdiff_t>(k));
			if (Opinion_[SimIx(A.node)] != kPlayerId)
			{
				++AttackStats_.fizzled;
				continue;
			}
			const ActionOutcome Landed = PlugNow(A.who, A.node, A.strength);
			if (Landed.result == ActionResult::Locked)
			{
				++AttackStats_.blocked;
				AttackStats_.shieldBlocked += Shield_[SimIx(A.node)] > 0.0 ? 1 : 0;
			}
			else
			{
				++AttackStats_.landed;
				AttackStats_.flipped += Landed.flipped ? 1 : 0;
			}
		}
		const bool bEyeAway = EyeAway_ > 0.0;
		if (NightHeld_)
		{
			Heat_ += P_.HeatNightRate * (bEyeAway ? P_.EyeAwayHeatMult : 1.0) * Dt;
		}
		else
		{
			Heat_ -= P_.HeatDecay * Dt;
		}
		if (P_.SockHeldHeat > 0.0)
		{
			Heat_ += P_.SockHeldHeat * (bEyeAway ? P_.EyeAwayHeldMult : 1.0) * (SockBoughtWeight() / MaxOf(1.0, TotalWeight())) * Dt;
		}
		EyeAway_ = MaxOf(0.0, EyeAway_ - Dt);
		Heat_ = ClampRange(Heat_, 0.0, P_.HeatMax);

		const int Count = Graph_.NodeCount();
		for (int i = 0; i < Count; ++i)
		{
			Lock_[SimIx(i)] = MaxOf(0.0, Lock_[SimIx(i)] - Dt);
			Shield_[SimIx(i)] = MaxOf(0.0, Shield_[SimIx(i)] - Dt);
		}

		RecomputeRanks();

		// Per-entry pressure multiplier (appeal x front page) and drift weights (clout x front page).
		double Mult[kMaxEntries];
		double DriftW[kMaxEntries];
		for (int e = 0; e < NumEntries_; ++e)
		{
			const double Front = FrontMult(Rank_[e]);
			Mult[e] = Entries_[SimIx(e)].appeal * Front;
			DriftW[e] = Entries_[SimIx(e)].clout * Front;
		}

		// Underdog sympathy: past a lead of BacklashLead the leader's voters feel a growing pull towards the runner-up.
		int BacklashFrom = -1;
		int BacklashTo = -1;
		const double Backlash = SympathyPressure(&BacklashFrom, &BacklashTo);

		// What each node broadcasts to its followers and (via capture) to its followees.
		for (int i = 0; i < Count; ++i)
		{
			const int O = Opinion_[SimIx(i)];
			Contrib_[SimIx(i)] = (O >= 0) ? Graph_.nodes[SimIx(i)].infl * (P_.ConvBase + P_.ConvWeight * Conv_[SimIx(i)]) : 0.0;
		}

		for (int i = 0; i < Count; ++i)
		{
			NewOpinion_[SimIx(i)] = Opinion_[SimIx(i)];
			NewConv_[SimIx(i)] = Conv_[SimIx(i)];
			Drain_[SimIx(i)] = 0.0;
			Challenger_[SimIx(i)] = kNoOwner;
			if (Lock_[SimIx(i)] > 0.0)
			{
				continue; // immune this step
			}
			const GraphNode& Node = Graph_.nodes[SimIx(i)];

			double Pv[kMaxEntries];
			for (int e = 0; e < NumEntries_; ++e)
			{
				Pv[e] = 0.0;
			}
			for (size_t k = 0; k < Node.follows.size(); ++k)
			{
				const int J = Node.follows[k];
				const int O = Opinion_[SimIx(J)];
				if (O >= 0)
				{
					Pv[O] += Contrib_[SimIx(J)];
				}
			}
			for (size_t k = 0; k < Node.followers.size(); ++k)
			{
				const int F = Node.followers[k];
				const int O = Opinion_[SimIx(F)];
				if (O >= 0)
				{
					Pv[O] += P_.Capture * Contrib_[SimIx(F)];
				}
			}

			if (Backlash > 0.0 && Opinion_[SimIx(i)] == BacklashFrom)
			{
				Pv[BacklashTo] += Backlash;
			}

			double SumP = 0.0;
			int Top = -1;
			double TopP = 0.0;
			for (int e = 0; e < NumEntries_; ++e)
			{
				Pv[e] *= Mult[e];
				SumP += Pv[e];
				if (Pv[e] > TopP)
				{
					TopP = Pv[e];
					Top = e;
				}
			}

			const int Cur = Opinion_[SimIx(i)];
			double C = Conv_[SimIx(i)];
			if (Cur >= 0)
			{
				const double Sup = Pv[Cur] + P_.Stickiness * C;
				if (Top >= 0 && Top != Cur && Pv[Top] > Sup * P_.FlipMargin)
				{
					Drain_[SimIx(i)] = P_.DrainRate * (Pv[Top] - Sup) / (SumP + 1.0e-6);
					Challenger_[SimIx(i)] = Top;
					C -= P_.DrainRate * Dt * (Pv[Top] - Sup) / (SumP + 1.0e-6);
					if (C <= 0.0)
					{
						NewOpinion_[SimIx(i)] = Top;
						C = P_.FlipConv;
					}
				}
				else
				{
					C = MinOf(1.0, C + P_.RegainRate * Dt);
				}
				NewConv_[SimIx(i)] = C;
			}
			else
			{
				const double Roll = Rng_.NextDouble();
				if (SumP > P_.AdoptThreshold)
				{
					if (Roll < P_.AdoptRate * Dt && Top >= 0)
					{
						NewOpinion_[SimIx(i)] = Top;
						NewConv_[SimIx(i)] = P_.AdoptConv;
					}
				}
				else if (Roll < P_.DriftRate * Dt)
				{
					const int Pick = Rng_.Choice(DriftW, NumEntries_);
					if (Pick >= 0)
					{
						NewOpinion_[SimIx(i)] = Pick;
						NewConv_[SimIx(i)] = P_.DriftConv;
					}
				}
			}
		}

		// Commit.
		for (int i = 0; i < Count; ++i)
		{
			const int Old = Opinion_[SimIx(i)];
			const int New = NewOpinion_[SimIx(i)];
			if (New != Old)
			{
				NodeChange Ch;
				Ch.node = i;
				Ch.prevOwner = Old;
				Ch.newOwner = New;
				Changes_.push_back(Ch);
				SetOwner(i, New);
				Flipper_[SimIx(i)] = kFlipNatural;
				Sock_[SimIx(i)] = 0;
				SockAt_[SimIx(i)] = -1.0;
			}
			Conv_[SimIx(i)] = NewConv_[SimIx(i)];
			if (Sock_[SimIx(i)] != 0 && Conv_[SimIx(i)] >= P_.SockLaunderConv)
			{
				Sock_[SimIx(i)] = 0; // real support caught the flip: the vote is organic now
			}
		}
		RecomputeRanks();
	}

	// ---------------------------------------------------------------------------------------
	// Gates
	// ---------------------------------------------------------------------------------------

	GateInfo Sim::Gate(int Node, int Who) const
	{
		GateInfo G;
		if (Node < 0 || Node >= Graph_.NodeCount())
		{
			return G;
		}
		const GraphNode& N = Graph_.nodes[SimIx(Node)];
		if (N.role != NodeRole::LocalHub && N.role != NodeRole::Celebrity)
		{
			return G;
		}
		G.applies = true;
		G.needed = (N.role == NodeRole::Celebrity) ? Jam_.celebGate : Jam_.localGate;
		for (size_t k = 0; k < N.followers.size(); ++k)
		{
			const int F = N.followers[k];
			const double W = Graph_.nodes[SimIx(F)].weight;
			G.totalWeight += W;
			if (Opinion_[SimIx(F)] == Who)
			{
				G.backingWeight += W;
			}
		}
		G.progress = G.totalWeight > 0.0 ? G.backingWeight / G.totalWeight : 1.0;
		G.open = (Opinion_[SimIx(Node)] == Who) || (G.progress + 1.0e-12 >= G.needed);
		if (Who != kPlayerId && !(P_.GatesApplyToAll > 0.5))
		{
			G.applies = false;
			G.open = true;
		}
		return G;
	}

	bool Sim::IsGated(int Node, int Who) const
	{
		const GateInfo G = Gate(Node, Who);
		return G.applies && !G.open;
	}

	// ---------------------------------------------------------------------------------------
	// Actions
	// ---------------------------------------------------------------------------------------

	ActionOutcome Sim::Plug(int Who, int Node, double Strength)
	{
		if (Who != kPlayerId && Who >= 0 && Who < NumEntries_ && Node >= 0 && Node < Graph_.NodeCount() && P_.AttackTelegraph > 0.0 && Opinion_[SimIx(Node)] == kPlayerId && Sock_[SimIx(Node)] == 0)
		{
			// Another entry has picked one of the player's (honest) voters: announce it, so the player can answer before it lands. A sockpuppet vote is flagged and gets no warning:
			// nobody waits to tell a cheat, and the late burst of a sockpuppet run is not shielded by the delay.
			PendingAttack A;
			A.who = Who;
			A.node = Node;
			A.strength = Strength;
			A.timeLeft = P_.AttackTelegraph;
			A.total = P_.AttackTelegraph;
			Pending_.push_back(A);
			++AttackStats_.queued;
			ActionOutcome O;
			O.result = ActionResult::Telegraphed;
			O.who = Who;
			O.node = Node;
			O.prevOwner = kPlayerId;
			O.newOwner = kPlayerId;
			O.convBefore = Conv_[SimIx(Node)];
			O.convAfter = Conv_[SimIx(Node)];
			return O;
		}
		return PlugNow(Who, Node, Strength);
	}

	bool Sim::ThreatOn(int Node, int* Who, double* TimeLeft, double* Total) const
	{
		bool Found = false;
		double Best = 1.0e30;
		for (size_t k = 0; k < Pending_.size(); ++k)
		{
			if (Pending_[k].node == Node && Pending_[k].timeLeft < Best)
			{
				Best = Pending_[k].timeLeft;
				if (Who != nullptr) { *Who = Pending_[k].who; }
				if (TimeLeft != nullptr) { *TimeLeft = Pending_[k].timeLeft; }
				if (Total != nullptr) { *Total = Pending_[k].total; }
				Found = true;
			}
		}
		return Found;
	}

	ActionOutcome Sim::PlugNow(int Who, int Node, double Strength)
	{
		ActionOutcome O;
		O.who = Who;
		O.node = Node;
		if (Who < 0 || Who >= NumEntries_ || Node < 0 || Node >= Graph_.NodeCount())
		{
			O.result = ActionResult::InvalidTarget;
			return O;
		}
		const size_t Ix = SimIx(Node);
		const bool IsPlayer = (Who == kPlayerId);
		O.prevOwner = Opinion_[Ix];
		O.newOwner = Opinion_[Ix];
		O.convBefore = Conv_[Ix];
		O.convAfter = Conv_[Ix];
		O.lockAfter = Lock_[Ix];

		// a Plug on a foreign voter costs the player PlayerPlugCost Posts (the Thumbnail lever), a reinforcement one
		const double BaseCost = (IsPlayer && Opinion_[Ix] != Who) ? P_.PlayerPlugCost : 1.0;
		if (IsPlayer)
		{
			if (Disqualified()) { O.result = ActionResult::Disqualified; return O; }
			if (Shadowban_ > 0.0) { O.result = ActionResult::Shadowbanned; return O; }
			if (Posts_ + kSimEps < BaseCost) { O.result = ActionResult::NoPosts; return O; }
		}

		double Spend = BaseCost;
		if (Opinion_[Ix] == Who)
		{
			// Reinforce (allowed even while the node is immune). Answering an announced attack parries it: the soonest ring on this voter is cancelled.
			if (IsPlayer && P_.ParryOnReinforce > 0.5)
			{
				int Soonest = -1;
				for (size_t k = 0; k < Pending_.size(); ++k)
				{
					if (Pending_[k].node == Node && (Soonest < 0 || Pending_[k].timeLeft < Pending_[static_cast<size_t>(Soonest)].timeLeft))
					{
						Soonest = static_cast<int>(k);
					}
				}
				if (Soonest >= 0)
				{
					Spend = MaxOf(1.0, P_.ParryCost);
					if (Posts_ + kSimEps < Spend)
					{
						O.result = ActionResult::NoPosts;
						return O;
					}
					const PendingAttack& A = Pending_[static_cast<size_t>(Soonest)];
					O.parried = true;
					O.parriedWho = A.who;
					O.parriedLeft = A.timeLeft;
					O.parriedTotal = A.total;
					Pending_.erase(Pending_.begin() + Soonest);
					++AttackStats_.parried;
				}
			}
			Conv_[Ix] = MinOf(1.0, Conv_[Ix] + P_.PlugReinforce);
			O.result = ActionResult::Reinforced;
		}
		else
		{
			if (Lock_[Ix] > 0.0)
			{
				O.result = ActionResult::Locked;
				return O;
			}
			{
				const GateInfo G = Gate(Node, Who);
				if (G.applies && !G.open)
				{
					O.result = ActionResult::Gated;
					O.gateProgress = G.progress;
					O.gateNeeded = G.needed;
					return O;
				}
			}
			const double Base = (Opinion_[Ix] >= 0) ? Conv_[Ix] : 0.0;
			const double C = Base - P_.PlugDrain * Strength;
			if (C <= P_.PlugFlipThreshold + kSimEps)
			{
				if (IsPlayer && Opinion_[Ix] >= 0)
				{
					TakenByPlayerAt_[Opinion_[Ix]] = Time_;
					TakenByPlayerNode_[Opinion_[Ix]] = Node;
				}
				SetOwner(Node, Who);
				Flipper_[Ix] = Who;
				Conv_[Ix] = IsPlayer ? P_.PlayerFlipConv : P_.PlugFlipConv;
				Lock_[Ix] = IsPlayer ? P_.PlayerPlugLock : P_.PlugLock;
				Sock_[Ix] = 0;
				SockAt_[Ix] = -1.0;
				O.result = ActionResult::Flipped;
				O.flipped = true;
			}
			else
			{
				Conv_[Ix] = C;
				O.result = ActionResult::Weakened;
			}
			if (O.flipped)
			{
				RecomputeRanks();
			}
		}

		if (IsPlayer)
		{
			Posts_ = MaxOf(0.0, Posts_ - Spend);
			O.postsSpent = Spend;
			AttackStats_.postsParry += O.parried ? Spend : 0.0;
		}
		O.newOwner = Opinion_[Ix];
		O.convAfter = Conv_[Ix];
		O.lockAfter = Lock_[Ix];
		return O;
	}

	ActionOutcome Sim::Sock(int Node)
	{
		ActionOutcome O;
		O.who = kPlayerId;
		O.node = Node;
		O.sockpuppet = true;
		if (Node < 0 || Node >= Graph_.NodeCount())
		{
			O.result = ActionResult::InvalidTarget;
			return O;
		}
		const size_t Ix = SimIx(Node);
		O.prevOwner = Opinion_[Ix];
		O.newOwner = Opinion_[Ix];
		O.convBefore = Conv_[Ix];
		O.convAfter = Conv_[Ix];
		O.lockAfter = Lock_[Ix];

		if (Disqualified()) { O.result = ActionResult::Disqualified; return O; }
		if (Shadowban_ > 0.0) { O.result = ActionResult::Shadowbanned; return O; }
		if (P_.SockRequiresNight > 0.5 && !NightHeld_) { O.result = ActionResult::NotNight; return O; }
		if (Posts_ + kSimEps < 1.0) { O.result = ActionResult::NoPosts; return O; }
		if (Opinion_[Ix] == kPlayerId) { O.result = ActionResult::AlreadyOwned; return O; }
		if (Lock_[Ix] > 0.0) { O.result = ActionResult::Locked; return O; }
		// A sockpuppet that would take Heat to the cap is refused and costs nothing: the player is told to let Heat cool instead of being struck at once.
		const double SockHeat = P_.SockHeat * (EyeAway_ > 0.0 ? P_.EyeAwayHeatMult : 1.0);
		if (P_.SockOverheatRefuse > 0.5 && Heat_ + SockHeat >= P_.HeatMax - 1.0e-9) { O.result = ActionResult::Overheated; return O; }

		Posts_ = MaxOf(0.0, Posts_ - 1.0);
		O.postsSpent = 1.0;
		const double HeatBefore = Heat_;
		Heat_ = MinOf(P_.HeatMax, Heat_ + SockHeat);
		O.heatDelta = Heat_ - HeatBefore;

		const double Base = (Opinion_[Ix] >= 0) ? Conv_[Ix] : 0.0;
		const double C = Base - P_.PlugDrain * P_.SockStrength;
		if (C <= P_.PlugFlipThreshold + kSimEps)
		{
			SockPrevOpinion_[Ix] = Opinion_[Ix];
			SockPrevConv_[Ix] = Conv_[Ix];
			if (Opinion_[Ix] >= 0)
			{
				TakenByPlayerAt_[Opinion_[Ix]] = Time_;
				TakenByPlayerNode_[Opinion_[Ix]] = Node;
			}
			SetOwner(Node, kPlayerId);
			Flipper_[Ix] = kPlayerId;
			Conv_[Ix] = P_.SockConv;
			Lock_[Ix] = P_.SockLock;
			Sock_[Ix] = 1;
			SockAt_[Ix] = Time_;
			O.result = ActionResult::Flipped;
			O.flipped = true;
			RecomputeRanks();
		}
		else
		{
			Conv_[Ix] = C;
			O.result = ActionResult::Weakened;
		}
		O.newOwner = Opinion_[Ix];
		O.convAfter = Conv_[Ix];
		O.lockAfter = Lock_[Ix];
		return O;
	}

	ActionOutcome Sim::Rally(int Node)
	{
		ActionOutcome O;
		O.who = kPlayerId;
		O.node = Node;
		if (Node < 0 || Node >= Graph_.NodeCount())
		{
			O.result = ActionResult::InvalidTarget;
			return O;
		}
		const size_t Ix = SimIx(Node);
		O.prevOwner = Opinion_[Ix];
		O.newOwner = Opinion_[Ix];
		O.convBefore = Conv_[Ix];
		O.convAfter = Conv_[Ix];
		if (Disqualified()) { O.result = ActionResult::Disqualified; return O; }
		if (Shadowban_ > 0.0) { O.result = ActionResult::Shadowbanned; return O; }
		if (Opinion_[Ix] != kPlayerId) { O.result = ActionResult::NotYours; return O; }
		if (RallyCooldown_ > 0.0) { O.result = ActionResult::OnCooldown; return O; }
		if (Posts_ + kSimEps < P_.RallyCost) { O.result = ActionResult::NoPosts; return O; }

		Posts_ = MaxOf(0.0, Posts_ - P_.RallyCost);
		RallyCooldown_ = P_.RallyCooldown;
		AttackStats_.postsRally += P_.RallyCost;
		O.postsSpent = P_.RallyCost;
		std::vector<int> Group;
		Group.push_back(Node);
		const GraphNode& G = Graph_.nodes[Ix];
		for (size_t k = 0; k < G.follows.size(); ++k) { Group.push_back(G.follows[k]); }
		for (size_t k = 0; k < G.followers.size(); ++k) { Group.push_back(G.followers[k]); }
		std::vector<uint8_t> Done(static_cast<size_t>(Graph_.NodeCount()), 0);
		for (size_t k = 0; k < Group.size(); ++k)
		{
			const size_t J = SimIx(Group[k]);
			if (Done[J] != 0 || Opinion_[J] != kPlayerId)
			{
				continue;
			}
			Done[J] = 1;
			Conv_[J] = MinOf(1.0, Conv_[J] + P_.RallyBoost);
			Lock_[J] = MaxOf(Lock_[J], P_.RallyLock);
			Shield_[J] = MaxOf(Shield_[J], P_.RallyLock);
			++O.touched;
		}
		O.result = ActionResult::Rallied;
		O.convAfter = Conv_[Ix];
		return O;
	}

	std::vector<int> Sim::RevertSockpuppets()
	{
		std::vector<int> Reverted;
		for (int i = 0; i < Graph_.NodeCount(); ++i)
		{
			if (Sock_[SimIx(i)] == 0 && !(SockAt_[SimIx(i)] >= 0.0 && Opinion_[SimIx(i)] == kPlayerId))
			{
				continue;
			}
			SetOwner(i, SockPrevOpinion_[SimIx(i)]);
			Conv_[SimIx(i)] = SockPrevConv_[SimIx(i)];
			Lock_[SimIx(i)] = 0.0;
			Sock_[SimIx(i)] = 0;
			SockAt_[SimIx(i)] = -1.0;
			Reverted.push_back(i);
		}
		if (!Reverted.empty())
		{
			RecomputeRanks();
		}
		return Reverted;
	}

	std::vector<int> Sim::AuditSockpuppets()
	{
		std::vector<int> Reverted;
		const double Since = TotalSeconds() - P_.SockReview - kSimEps;
		for (int i = 0; i < Graph_.NodeCount(); ++i)
		{
			const size_t Ix = SimIx(i);
			const bool bRecent = P_.SockReview > 0.0 && SockAt_[Ix] >= 0.0 && SockAt_[Ix] >= Since && Opinion_[Ix] == kPlayerId;
			if (Sock_[Ix] == 0 && !bRecent)
			{
				continue;
			}
			SetOwner(i, SockPrevOpinion_[Ix]);
			Conv_[Ix] = SockPrevConv_[Ix];
			Lock_[Ix] = 0.0;
			Sock_[Ix] = 0;
			SockAt_[Ix] = -1.0;
			Reverted.push_back(i);
		}
		if (!Reverted.empty())
		{
			RecomputeRanks();
		}
		return Reverted;
	}

	StrikeOutcome Sim::ApplyStrike()
	{
		StrikeOutcome Out;
		Out.revertedNodes = RevertSockpuppets();
		Shadowban_ = P_.ShadowbanSeconds;
		++Strikes_;
		Heat_ = MinOf(Heat_, P_.HeatAfterStrike);
		RecomputeRanks();
		Out.strikes = Strikes_;
		Out.disqualified = Disqualified();
		Out.shadowbanSeconds = Shadowban_;
		return Out;
	}

	int Sim::AddConvictionToEntry(int Who, double Delta)
	{
		int Touched = 0;
		for (int i = 0; i < Graph_.NodeCount(); ++i)
		{
			if (Opinion_[SimIx(i)] == Who)
			{
				Conv_[SimIx(i)] = Clamp01(Conv_[SimIx(i)] + Delta);
				++Touched;
			}
		}
		return Touched;
	}

	void Sim::SetNodeState(int Node, int Owner, double Conviction, double LockSeconds)
	{
		if (Node < 0 || Node >= Graph_.NodeCount())
		{
			return;
		}
		const int Ow = (Owner >= 0 && Owner < NumEntries_) ? Owner : kNoOwner;
		SetOwner(Node, Ow);
		Conv_[SimIx(Node)] = Clamp01(Conviction);
		Lock_[SimIx(Node)] = MaxOf(0.0, LockSeconds);
		Sock_[SimIx(Node)] = 0;
		SockAt_[SimIx(Node)] = -1.0;
		RecomputeRanks();
	}

	void Sim::SetHeat(double Value)
	{
		Heat_ = ClampRange(Value, 0.0, P_.HeatMax);
	}

	void Sim::SetPosts(double Value)
	{
		Posts_ = ClampRange(Value, 0.0, P_.PostsMax);
	}

	// ---------------------------------------------------------------------------------------
	// Queries
	// ---------------------------------------------------------------------------------------

	int Sim::PlugsNeeded(int Who, int Node, double Strength, bool RespectGates) const
	{
		if (Who < 0 || Who >= NumEntries_ || Node < 0 || Node >= Graph_.NodeCount())
		{
			return kUnreachable;
		}
		const size_t Ix = SimIx(Node);
		if (Opinion_[Ix] == Who)
		{
			return 0;
		}
		if (Lock_[Ix] > 0.0)
		{
			return kUnreachable;
		}
		if (RespectGates && IsGated(Node, Who))
		{
			return kUnreachable;
		}
		const double C = (Opinion_[Ix] >= 0) ? Conv_[Ix] : 0.0;
		const double Drain = P_.PlugDrain * MaxOf(Strength, 1.0e-9);
		const double Need = (C - P_.PlugFlipThreshold - kSimEps) / Drain;
		int N = static_cast<int>(std::ceil(Need));
		if (N < 1)
		{
			N = 1;
		}
		return N;
	}

	double Sim::NodeValue(int Who, int Node) const
	{
		const GraphNode& N = Graph_.nodes[SimIx(Node)];
		double Value = N.weight;
		double Extra = 0.0;
		for (size_t k = 0; k < N.followers.size(); ++k)
		{
			const int F = N.followers[k];
			if (Opinion_[SimIx(F)] != Who)
			{
				Extra += Graph_.nodes[SimIx(F)].weight;
			}
		}
		return Value + P_.TargetFollowerCredit * Extra;
	}

	void Sim::CollectTargets(int Who, const TargetFilter& F, std::vector<Target>* Out) const
	{
		if (Out == nullptr)
		{
			return;
		}
		Out->clear();
		for (int i = 0; i < Graph_.NodeCount(); ++i)
		{
			const GraphNode& N = Graph_.nodes[SimIx(i)];
			if (((F.roleMask >> static_cast<uint32_t>(N.role)) & 1u) == 0u)
			{
				continue;
			}
			if (N.weight < F.minV || N.weight > F.maxV)
			{
				continue;
			}
			const int O = Opinion_[SimIx(i)];
			if (O == Who)
			{
				continue;
			}
			const double Mult = (O >= 0) ? F.ownerMult[O] : F.undecidedMult;
			if (!(Mult > 0.0))
			{
				continue;
			}
			const double NodeFactor = F.nodeMult != nullptr ? F.nodeMult[i] : 1.0;
			if (!(NodeFactor > 0.0))
			{
				continue;
			}
			const int Plugs = PlugsNeeded(Who, i, F.strength, F.respectGates);
			if (Plugs >= kUnreachable)
			{
				continue;
			}
			Target T;
			T.node = i;
			T.value = NodeValue(Who, i);
			T.plugs = Plugs;
			T.score = T.value * Mult * NodeFactor / static_cast<double>(Plugs);
			Out->push_back(T);
		}
		std::sort(Out->begin(), Out->end(), [](const Target& A, const Target& B) {
			if (A.score != B.score)
			{
				return A.score > B.score;
			}
			return A.node < B.node;
			});
	}

	bool Sim::BestTarget(int Who, const TargetFilter& F, Target* Out) const
	{
		bool Found = false;
		Target Best;
		for (int i = 0; i < Graph_.NodeCount(); ++i)
		{
			const GraphNode& N = Graph_.nodes[SimIx(i)];
			if (((F.roleMask >> static_cast<uint32_t>(N.role)) & 1u) == 0u)
			{
				continue;
			}
			if (N.weight < F.minV || N.weight > F.maxV)
			{
				continue;
			}
			const int O = Opinion_[SimIx(i)];
			if (O == Who)
			{
				continue;
			}
			const double Mult = (O >= 0) ? F.ownerMult[O] : F.undecidedMult;
			if (!(Mult > 0.0))
			{
				continue;
			}
			const double NodeFactor = F.nodeMult != nullptr ? F.nodeMult[i] : 1.0;
			if (!(NodeFactor > 0.0))
			{
				continue;
			}
			const int Plugs = PlugsNeeded(Who, i, F.strength, F.respectGates);
			if (Plugs >= kUnreachable)
			{
				continue;
			}
			const double Value = NodeValue(Who, i);
			const double Score = Value * Mult * NodeFactor / static_cast<double>(Plugs);
			if (!Found || Score > Best.score)
			{
				Found = true;
				Best.node = i;
				Best.value = Value;
				Best.plugs = Plugs;
				Best.score = Score;
			}
		}
		if (Found && Out != nullptr)
		{
			*Out = Best;
		}
		return Found;
	}

	int Sim::WeakestOwned(int Who, double MinV, double MaxConv) const
	{
		int Best = -1;
		for (int i = 0; i < Graph_.NodeCount(); ++i)
		{
			if (Opinion_[SimIx(i)] != Who)
			{
				continue;
			}
			const double W = Graph_.nodes[SimIx(i)].weight;
			if (W < MinV || Conv_[SimIx(i)] > MaxConv)
			{
				continue;
			}
			if (Best < 0)
			{
				Best = i;
				continue;
			}
			const double Cb = Conv_[SimIx(Best)];
			const double Ci = Conv_[SimIx(i)];
			if (Ci < Cb || (Ci == Cb && W > Graph_.nodes[SimIx(Best)].weight))
			{
				Best = i;
			}
		}
		return Best;
	}

	const std::vector<int>& Sim::NodesByCommunity(int Community) const
	{
		if (Community < 0 || Community >= static_cast<int>(Graph_.communityNodes.size()))
		{
			return EmptyNodes_;
		}
		return Graph_.communityNodes[SimIx(Community)];
	}

	int Sim::OwnedCount(int Who) const
	{
		int N = 0;
		for (int i = 0; i < Graph_.NodeCount(); ++i)
		{
			if (Opinion_[SimIx(i)] == Who)
			{
				++N;
			}
		}
		return N;
	}

	uint64_t Sim::StateHash() const
	{
		uint64_t H = 14695981039346656037ull;
		for (int i = 0; i < Graph_.NodeCount(); ++i)
		{
			SimHashMix(H, static_cast<uint64_t>(static_cast<int64_t>(Opinion_[SimIx(i)]) + 2));
			SimHashMix(H, SimDoubleBits(Conv_[SimIx(i)]));
			SimHashMix(H, SimDoubleBits(Lock_[SimIx(i)]));
			SimHashMix(H, static_cast<uint64_t>(Sock_[SimIx(i)]));
			SimHashMix(H, SimDoubleBits(SockAt_[SimIx(i)]));
		}
		SimHashMix(H, SimDoubleBits(Posts_));
		SimHashMix(H, SimDoubleBits(Heat_));
		SimHashMix(H, SimDoubleBits(Shadowban_));
		SimHashMix(H, SimDoubleBits(EyeAway_));
		SimHashMix(H, static_cast<uint64_t>(Strikes_));
		SimHashMix(H, static_cast<uint64_t>(StepCount_));
		SimHashMix(H, static_cast<uint64_t>(Pending_.size()));
		for (size_t k = 0; k < Pending_.size(); ++k)
		{
			SimHashMix(H, static_cast<uint64_t>(Pending_[k].who) * 1000003ull + static_cast<uint64_t>(Pending_[k].node));
			SimHashMix(H, SimDoubleBits(Pending_[k].timeLeft));
		}
		return H;
	}

} // namespace ShiftSim
