
#include "ShiftGraph.h"

#include <cmath>
#include <cstdint>
#include "ShiftDetMath.h"
#include "ShiftParams.h"
#include "ShiftRng.h"
#include <vector>

namespace ShiftSim
{

	namespace
	{

		int GraphSz(const std::vector<int>& V)
		{
			return static_cast<int>(V.size());
		}

		GraphNode& GraphNodeAt(Graph& G, int Id)
		{
			return G.nodes[static_cast<size_t>(Id)];
		}

		bool HasFollow(const Graph& G, int A, int B)
		{
			const std::vector<int>& F = G.nodes[static_cast<size_t>(A)].follows;
			for (size_t i = 0; i < F.size(); ++i)
			{
				if (F[i] == B)
				{
					return true;
				}
			}
			return false;
		}

		// A follows B. No self edges, no duplicates.
		bool AddFollow(Graph& G, int A, int B)
		{
			if (A == B || HasFollow(G, A, B))
			{
				return false;
			}
			GraphNodeAt(G, A).follows.push_back(B);
			GraphNodeAt(G, B).followers.push_back(A);
			return true;
		}

		int NewNode(Graph& G, NodeRole Role, int Community)
		{
			GraphNode Node;
			Node.id = G.NodeCount();
			Node.role = Role;
			Node.community = Community;
			G.nodes.push_back(Node);
			return Node.id;
		}

		// Picks a hub or member of community C (never a bridge) uniformly.
		int PickCommunityNode(const Graph& G, int C, Rng& R, const std::vector<std::vector<int>>& Core)
		{
			(void)G;
			const std::vector<int>& L = Core[static_cast<size_t>(C)];
			return L[static_cast<size_t>(R.Range(GraphSz(L)))];
		}

		int PickOtherCommunity(int Self, int Count, Rng& R)
		{
			if (Count <= 1)
			{
				return Self;
			}
			int C = R.Range(Count - 1);
			if (C >= Self)
			{
				++C;
			}
			return C;
		}

		void LayoutGraph(Graph& G, const Params& P, uint32_t Seed)
		{
			Rng R(Rng::Derive(Seed, 3u));
			const double W = P.LayoutWidth;
			const double H = P.LayoutHeight;
			const double Cx = 0.5 * (P.LayoutMargin + (W - P.LayoutMarginRight));   // the middle of the part of the board the HUD leaves free
			const double Cy = H * 0.5;
			const double Pi = 3.14159265358979323846;
			const int Count = static_cast<int>(G.communityNodes.size());

			std::vector<double> Px(G.nodes.size(), Cx);
			std::vector<double> Py(G.nodes.size(), Cy);
			std::vector<uint8_t> Placed(G.nodes.size(), 0);

			for (int C = 0; C < Count; ++C)
			{
				const double Ang = -0.5 * Pi + 2.0 * Pi * (static_cast<double>(C) + 0.5) / static_cast<double>(Count);
				const double Jx = R.Uniform(0.94, 1.06);
				const double Jy = R.Uniform(0.94, 1.06);
				const double Hx = Cx + P.LayoutRingX * std::cos(Ang) * Jx;
				const double Hy = Cy + P.LayoutRingY * std::sin(Ang) * Jy;
				const int Hub = G.hubOfCommunity[static_cast<size_t>(C)];
				Px[static_cast<size_t>(Hub)] = Hx;
				Py[static_cast<size_t>(Hub)] = Hy;
				Placed[static_cast<size_t>(Hub)] = 1;

				const std::vector<int>& List = G.communityNodes[static_cast<size_t>(C)];
				const double Radius = 52.0 + 11.0 * std::sqrt(static_cast<double>(GraphSz(List)));
				for (int i = 0; i < GraphSz(List); ++i)
				{
					const int Id = List[static_cast<size_t>(i)];
					if (Id == Hub || GraphNodeAt(G, Id).role == NodeRole::Bridge)
					{
						continue;
					}
					double BestX = Hx;
					double BestY = Hy;
					double BestScore = -1.0;
					for (int Try = 0; Try < 24; ++Try)
					{
						const double A = R.Uniform(0.0, 2.0 * Pi);
						const double Rad = 34.0 + std::sqrt(R.NextDouble()) * Radius;
						const double X = Hx + Rad * std::cos(A);
						const double Y = Hy + Rad * std::sin(A) * 0.85;
						double MinD = 1.0e9;
						for (size_t k = 0; k < G.nodes.size(); ++k)
						{
							if (Placed[k] == 0)
							{
								continue;
							}
							const double Dx = X - Px[k];
							const double Dy = Y - Py[k];
							const double D = std::sqrt(Dx * Dx + Dy * Dy);
							if (D < MinD)
							{
								MinD = D;
							}
						}
						if (MinD > BestScore)
						{
							BestScore = MinD;
							BestX = X;
							BestY = Y;
						}
					}
					Px[static_cast<size_t>(Id)] = BestX;
					Py[static_cast<size_t>(Id)] = BestY;
					Placed[static_cast<size_t>(Id)] = 1;
				}
			}

			// Celebrities sit near the middle of the board.
			const int Celebs = GraphSz(G.celebrities);
			for (int i = 0; i < Celebs; ++i)
			{
				const int Id = G.celebrities[static_cast<size_t>(i)];
				if (Celebs == 1)
				{
					Px[static_cast<size_t>(Id)] = Cx;
					Py[static_cast<size_t>(Id)] = Cy;
				}
				else
				{
					const double A = 0.5 * Pi + 2.0 * Pi * static_cast<double>(i) / static_cast<double>(Celebs);
					Px[static_cast<size_t>(Id)] = Cx + P.LayoutCelebRadius * std::cos(A);
					Py[static_cast<size_t>(Id)] = Cy + P.LayoutCelebRadius * 0.7 * std::sin(A);
				}
				Placed[static_cast<size_t>(Id)] = 1;
			}

			// Bridges sit between their home hub and the first foreign hub they follow.
			for (int i = 0; i < GraphSz(G.bridges); ++i)
			{
				const int Id = G.bridges[static_cast<size_t>(i)];
				const GraphNode& B = G.nodes[static_cast<size_t>(Id)];
				int Other = -1;
				for (size_t k = 0; k < B.follows.size(); ++k)
				{
					const GraphNode& T = G.nodes[static_cast<size_t>(B.follows[k])];
					if (T.role == NodeRole::LocalHub && T.community != B.community)
					{
						Other = T.id;
						break;
					}
				}
				const int Home = G.hubOfCommunity[static_cast<size_t>(B.community)];
				const int Mate = Other >= 0 ? Other : Home;
				Px[static_cast<size_t>(Id)] = 0.5 * (Px[static_cast<size_t>(Home)] + Px[static_cast<size_t>(Mate)]) + R.Uniform(-18.0, 18.0);
				Py[static_cast<size_t>(Id)] = 0.5 * (Py[static_cast<size_t>(Home)] + Py[static_cast<size_t>(Mate)]) + R.Uniform(-18.0, 18.0);
				Placed[static_cast<size_t>(Id)] = 1;
			}

			// Relaxation: push overlapping nodes apart, then keep everything inside the margin.
			const size_t Total = G.nodes.size();
			for (int Iter = 0; Iter < 80; ++Iter)
			{
				for (size_t a = 0; a < Total; ++a)
				{
					for (size_t b = a + 1; b < Total; ++b)
					{
						double Dx = Px[b] - Px[a];
						double Dy = Py[b] - Py[a];
						double D2 = Dx * Dx + Dy * Dy;
						if (D2 >= P.LayoutMinDist * P.LayoutMinDist)
						{
							continue;
						}
						if (D2 < 1.0e-6)
						{
							Dx = 1.0 + static_cast<double>(a % 3);
							Dy = 1.0 + static_cast<double>(b % 3);
							D2 = Dx * Dx + Dy * Dy;
						}
						const double D = std::sqrt(D2);
						const double Push = 0.5 * (P.LayoutMinDist - D) / D;
						Px[a] -= Dx * Push;
						Py[a] -= Dy * Push;
						Px[b] += Dx * Push;
						Py[b] += Dy * Push;
					}
				}
				for (size_t a = 0; a < Total; ++a)
				{
					Px[a] = ClampRange(Px[a], P.LayoutMargin, W - P.LayoutMarginRight);
					Py[a] = ClampRange(Py[a], P.LayoutMargin, H - P.LayoutMargin);
				}
			}

			for (size_t a = 0; a < Total; ++a)
			{
				G.nodes[a].x = static_cast<float>(Px[a]);
				G.nodes[a].y = static_cast<float>(Py[a]);
			}
		}

	} // namespace

	double ComputeWeight(const Params& P, int FollowerCount)
	{
		const double F = static_cast<double>(FollowerCount < 0 ? 0 : FollowerCount);
		return std::floor(P.WeightScale * DetPow(1.0 + F, P.WeightExp) + 0.5);
	}

	double ComputeInfluence(const Params& P, int FollowerCount)
	{
		const double F = static_cast<double>(FollowerCount < 0 ? 0 : FollowerCount);
		return std::sqrt(P.InflBase + F);
	}

	bool BuildGraph(const JamDef& Jam, const Params& P, uint32_t Seed, Graph* Out)
	{
		if (Out == nullptr)
		{
			return false;
		}
		Graph& G = *Out;
		G = Graph();

		const int Comms = Jam.communities;
		const int Celebs = Jam.celebs;
		const int Bridges = Jam.bridges;
		const int MemberTotal = Jam.nodes - Comms - Celebs - Bridges;
		if (Comms < 1 || Celebs < 0 || Bridges < 0 || MemberTotal < Comms)
		{
			return false;
		}

		Rng R(Rng::Derive(Seed, 1u));

		// Members per community: even split, remainder to random communities.
		std::vector<int> MemberCount(static_cast<size_t>(Comms), MemberTotal / Comms);
		{
			std::vector<int> Order(static_cast<size_t>(Comms));
			for (int i = 0; i < Comms; ++i)
			{
				Order[static_cast<size_t>(i)] = i;
			}
			R.Shuffle(Order, Comms);
			const int Rem = MemberTotal % Comms;
			for (int i = 0; i < Rem; ++i)
			{
				MemberCount[static_cast<size_t>(Order[static_cast<size_t>(i)])] += 1;
			}
		}

		G.communityNodes.assign(static_cast<size_t>(Comms), std::vector<int>());
		G.hubOfCommunity.assign(static_cast<size_t>(Comms), -1);
		std::vector<std::vector<int>> Core(static_cast<size_t>(Comms)); // hub + members only

		for (int C = 0; C < Comms; ++C)
		{
			const int Hub = NewNode(G, NodeRole::LocalHub, C);
			G.hubOfCommunity[static_cast<size_t>(C)] = Hub;
			G.communityNodes[static_cast<size_t>(C)].push_back(Hub);
			Core[static_cast<size_t>(C)].push_back(Hub);
			for (int i = 0; i < MemberCount[static_cast<size_t>(C)]; ++i)
			{
				const int M = NewNode(G, NodeRole::Member, C);
				G.communityNodes[static_cast<size_t>(C)].push_back(M);
				Core[static_cast<size_t>(C)].push_back(M);
			}
		}
		for (int i = 0; i < Celebs; ++i)
		{
			const int Z = NewNode(G, NodeRole::Celebrity, -1);
			G.celebrities.push_back(Z);
		}
		for (int i = 0; i < Bridges; ++i)
		{
			const int Home = i % Comms;
			const int B = NewNode(G, NodeRole::Bridge, Home);
			G.bridges.push_back(B);
			G.communityNodes[static_cast<size_t>(Home)].push_back(B);
		}

		const int MemberFollows = RoundToInt(P.MemberRandomFollows);

		// Local structure: members follow the hub plus random follows.
		for (int C = 0; C < Comms; ++C)
		{
			const int Hub = G.hubOfCommunity[static_cast<size_t>(C)];
			const std::vector<int>& L = Core[static_cast<size_t>(C)];
			for (int i = 1; i < GraphSz(L); ++i)
			{
				const int M = L[static_cast<size_t>(i)];
				if (R.Chance(P.MemberFollowHubProb))
				{
					AddFollow(G, M, Hub);
				}
				for (int k = 0; k < MemberFollows; ++k)
				{
					for (int Try = 0; Try < 6; ++Try)
					{
						int Target;
						if (Comms > 1 && !R.Chance(P.RandomFollowSameCommunityProb))
						{
							const int C2 = PickOtherCommunity(C, Comms, R);
							Target = PickCommunityNode(G, C2, R, Core);
						}
						else
						{
							Target = PickCommunityNode(G, C, R, Core);
						}
						if (Target != M && AddFollow(G, M, Target))
						{
							break;
						}
					}
				}
			}
			if (Comms > 1 && R.Chance(P.HubRandomFollowProb))
			{
				const int C2 = PickOtherCommunity(C, Comms, R);
				AddFollow(G, Hub, G.hubOfCommunity[static_cast<size_t>(C2)]);
			}
		}

		// Celebrity hubs: followed sparsely across communities.
		const bool bExclusive = P.CelebExclusive > 0.5 && Celebs > 1;
		std::vector<uint8_t> HasCeleb(G.nodes.size(), 0);   // exclusive audiences: the voters that already follow a celebrity
		if (bExclusive)
		{
			for (int C = 0; C < Comms; ++C)
			{
				const std::vector<int>& L = Core[static_cast<size_t>(C)];
				std::vector<uint8_t> Reached(static_cast<size_t>(Celebs), 0);
				for (int i = 0; i < GraphSz(L); ++i)
				{
					const int Id = L[static_cast<size_t>(i)];
					const double Prob = std::fmin(1.0, static_cast<double>(Celebs) * ((i == 0) ? P.HubFollowsCelebProb : P.CelebFollowProb));
					if (R.Chance(Prob))
					{
						const int zi = R.Range(Celebs);
						AddFollow(G, Id, G.celebrities[static_cast<size_t>(zi)]);
						HasCeleb[static_cast<size_t>(Id)] = 1;
						Reached[static_cast<size_t>(zi)] = 1;
					}
				}
				const int MinFollowers = RoundToInt(P.CelebMinFollowersPerCommunity);
				for (int zi = 0; zi < Celebs && MinFollowers > 0 && GraphSz(L) > 1; ++zi)
				{
					for (int Try = 0; Try < 6 && Reached[static_cast<size_t>(zi)] == 0; ++Try)
					{
						const int Id = L[static_cast<size_t>(1 + R.Range(GraphSz(L) - 1))];
						if (HasCeleb[static_cast<size_t>(Id)] == 0 && AddFollow(G, Id, G.celebrities[static_cast<size_t>(zi)]))
						{
							HasCeleb[static_cast<size_t>(Id)] = 1;
							Reached[static_cast<size_t>(zi)] = 1;
						}
					}
				}
			}
		}
		for (int zi = 0; zi < Celebs && !bExclusive; ++zi)
		{
			const int Z = G.celebrities[static_cast<size_t>(zi)];
			for (int C = 0; C < Comms; ++C)
			{
				const std::vector<int>& L = Core[static_cast<size_t>(C)];
				bool Any = false;
				for (int i = 0; i < GraphSz(L); ++i)
				{
					const int Id = L[static_cast<size_t>(i)];
					const double Prob = (i == 0) ? P.HubFollowsCelebProb : P.CelebFollowProb;
					if (R.Chance(Prob))
					{
						AddFollow(G, Id, Z);
						Any = true;
					}
				}
				const int MinFollowers = RoundToInt(P.CelebMinFollowersPerCommunity);
				if (MinFollowers > 0 && !Any && GraphSz(L) > 1)
				{
					AddFollow(G, L[static_cast<size_t>(1 + R.Range(GraphSz(L) - 1))], Z);
				}
			}
			for (int zj = 0; zj < Celebs && P.CelebLinks > 0.5; ++zj)
			{
				if (zj != zi)
				{
					AddFollow(G, Z, G.celebrities[static_cast<size_t>(zj)]);
				}
			}
		}

		// Bridges: follow their home hub and reach into other communities.
		for (int i = 0; i < Bridges; ++i)
		{
			const int B = G.bridges[static_cast<size_t>(i)];
			const int Home = GraphNodeAt(G, B).community;
			AddFollow(G, B, G.hubOfCommunity[static_cast<size_t>(Home)]);
			if (Comms > 1)
			{
				const int HubFollows = RoundToInt(P.BridgeCrossHubFollows);
				for (int k = 0; k < HubFollows; ++k)
				{
					for (int Try = 0; Try < 6; ++Try)
					{
						const int C2 = PickOtherCommunity(Home, Comms, R);
						if (AddFollow(G, B, G.hubOfCommunity[static_cast<size_t>(C2)]))
						{
							break;
						}
					}
				}
				const int MemberFollowsCount = RoundToInt(P.BridgeCrossMemberFollows);
				for (int k = 0; k < MemberFollowsCount; ++k)
				{
					for (int Try = 0; Try < 6; ++Try)
					{
						const int C2 = PickOtherCommunity(Home, Comms, R);
						const std::vector<int>& L = Core[static_cast<size_t>(C2)];
						if (GraphSz(L) > 1 && AddFollow(G, B, L[static_cast<size_t>(1 + R.Range(GraphSz(L) - 1))]))
						{
							break;
						}
					}
				}
				const int Followers = RoundToInt(P.BridgeFollowers);
				for (int k = 0; k < Followers; ++k)
				{
					for (int Try = 0; Try < 6; ++Try)
					{
						const int C2 = PickOtherCommunity(Home, Comms, R);
						const std::vector<int>& L = Core[static_cast<size_t>(C2)];
						if (GraphSz(L) > 1 && AddFollow(G, L[static_cast<size_t>(1 + R.Range(GraphSz(L) - 1))], B))
						{
							break;
						}
					}
				}
			}
			if (bExclusive)
			{
				if (R.Chance(std::fmin(1.0, static_cast<double>(Celebs) * P.CelebFollowProb)))
				{
					AddFollow(G, B, G.celebrities[static_cast<size_t>(R.Range(Celebs))]);
				}
			}
			for (int zi = 0; zi < Celebs && !bExclusive; ++zi)
			{
				if (R.Chance(P.CelebFollowProb))
				{
					AddFollow(G, B, G.celebrities[static_cast<size_t>(zi)]);
				}
			}
		}

		// Reciprocal follows. ReciprocalProb is the target fraction of directed edges that
		// end up reciprocated: adding the reverse of a fraction q of edges reciprocates 2q/(1+q)
		// of all edges, so q = p/(2-p).
		{
			const double Target = ClampRange(P.ReciprocalProb, 0.0, 1.0);
			const double Q = Target / (2.0 - Target);
			std::vector<int> From;
			std::vector<int> To;
			for (int a = 0; a < G.NodeCount(); ++a)
			{
				if (GraphNodeAt(G, a).role == NodeRole::Celebrity)
				{
					continue;
				}
				const std::vector<int>& F = GraphNodeAt(G, a).follows;
				for (size_t k = 0; k < F.size(); ++k)
				{
					if (GraphNodeAt(G, F[k]).role != NodeRole::Celebrity)
					{
						From.push_back(a);
						To.push_back(F[k]);
					}
				}
			}
			for (size_t e = 0; e < From.size(); ++e)
			{
				if (!HasFollow(G, To[e], From[e]) && R.Chance(Q))
				{
					AddFollow(G, To[e], From[e]);
				}
			}
		}

		// Followers, weights, influence.
		for (int a = 0; a < G.NodeCount(); ++a)
		{
			GraphNode& Node = GraphNodeAt(G, a);
			int Extra = 0;
			if (Node.role == NodeRole::Celebrity)
			{
				Extra = RoundToInt(Jam.celebExtraFollowers);
			}
			else if (Node.role == NodeRole::LocalHub)
			{
				Extra = RoundToInt(P.HubExtraFollowers);
			}
			Node.extraFollowers = Extra;
			Node.followerCount = GraphSz(Node.followers) + Extra;
			Node.weight = ComputeWeight(P, Node.followerCount);
			Node.infl = ComputeInfluence(P, Node.followerCount);
			G.totalWeight += Node.weight;
		}

		LayoutGraph(G, P, Seed);
		return true;
	}

} // namespace ShiftSim
