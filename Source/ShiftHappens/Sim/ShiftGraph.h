// Shift Happens - engine-independent numeric core.
// Follower graph: communities with a local hub, celebrity hubs, bridges, positions.
#pragma once

#include <cstdint>
#include "ShiftParams.h"
#include <vector>

namespace ShiftSim
{

	enum class NodeRole : uint8_t
	{
		LocalHub = 0,
		Member = 1,
		Celebrity = 2,
		Bridge = 3,
	};

	struct GraphNode
	{
		int id = 0;
		NodeRole role = NodeRole::Member;
		int community = -1;               // home community; celebrities are -1
		float x = 0.0f;                   // position inside the layout box (Params::LayoutWidth x LayoutHeight)
		float y = 0.0f;
		std::vector<int> follows;         // nodes this node listens to
		std::vector<int> followers;       // nodes that listen to this node
		int extraFollowers = 0;           // audience outside the sampled graph (celebrities / hubs)
		int followerCount = 0;            // followers.size() + extraFollowers
		double weight = 0.0;              // V = round(WeightScale * (1+followers)^WeightExp)
		double infl = 0.0;                // sqrt(InflBase + followers)
	};

	struct Graph
	{
		std::vector<GraphNode> nodes;
		std::vector<int> hubOfCommunity;                 // node id of each community's local hub
		std::vector<std::vector<int>> communityNodes;    // hub, members and bridges homed there
		std::vector<int> celebrities;                    // celebrity hub node ids
		std::vector<int> bridges;                        // bridge node ids
		double totalWeight = 0.0;

		int NodeCount() const { return static_cast<int>(nodes.size()); }
		bool IsGateNode(int Id) const
		{
			const NodeRole R = nodes[static_cast<size_t>(Id)].role;
			return R == NodeRole::LocalHub || R == NodeRole::Celebrity;
		}
	};

	// Builds nodes, edges, weights and positions deterministically from (jam, params, seed).
	// Returns false if the jam definition is unusable (no communities, too few nodes).
	bool BuildGraph(const JamDef& Jam, const Params& P, uint32_t Seed, Graph* Out);

	// Vote weight and influence from a follower count (shared with the Sim).
	double ComputeWeight(const Params& P, int FollowerCount);
	double ComputeInfluence(const Params& P, int FollowerCount);

} // namespace ShiftSim
