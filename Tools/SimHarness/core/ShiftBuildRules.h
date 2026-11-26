// Rate for Rate - Game Off 2026
//
// What a split of the last 12 hours ("Build your entry") does in play. Engine independent and header only, so the standalone harness tests the very same code the game runs
// (Tools\sync_sim_core.ps1 copies it). Every slope is a Params field (Build*), so -Set can tune it without a rebuild.

#pragma once

#include <cmath>
#include "ShiftParams.h"

namespace ShiftSim
{
	/** Hours put into each lever; the build screen allows 0..8 each and at most 12 in total, 4 / 4 / 4 is the neutral split. */
	struct BuildHours
	{
		int quality = 4;
		int thumbnail = 4;
		int network = 4;
	};

	/** What the split comes to, in the terms the player feels. Filled from the Params and Jam the build produced, so the build screen shows exactly what the sim uses. */
	struct BuildSummary
	{
		double postsMax = 0.0;       // the Post bank (pips)
		double postsRegen = 0.0;     // Posts a second
		int startNodes = 0;          // voters the player starts with
		double plugCost = 1.0;       // Posts a Plug on a foreign voter costs
		double flipConv = 0.0;       // conviction of a voter the player wins
		double quality = 0.0;        // the game's quality (0..10 scale, the other entries are drawn from QualityMin..QualityMax)
		double appeal = 1.0;         // how hard the player's voters pull their neighbours (1 = an average game)
		double clout = 0.0;          // how many undecided voters even look at the entry
	};

	namespace BuildDetail
	{
		/** Hours over or under an even split; under it a lever loses BuildBelowSlope times as fast as it gains over it. */
		inline double Lever(int Hours, const Params& P)
		{
			const double H = static_cast<double>(Hours - 4);
			return H >= 0.0 ? H : H * P.BuildBelowSlope;
		}

		inline double Clamp(double V, double Lo, double Hi)
		{
			return V < Lo ? Lo : (V > Hi ? Hi : V);
		}
	}

	/** Applies the build to fresh parameters and the jam (neutral at 4 / 4 / 4: nothing changes). */
	inline void ApplyBuild(const BuildHours& B, Params& P, JamDef& Jam)
	{
		const double Qh = BuildDetail::Lever(B.quality, P);
		const double Th = BuildDetail::Lever(B.thumbnail, P);
		const double Nh = BuildDetail::Lever(B.network, P);
		const double NhRaw = static_cast<double>(B.network - 4);

		// Quality: a better game keeps its voters. Voters the player wins start surer of it, and the game's appeal (its pull on neighbours) moves with its quality.
		P.PlayerBuildBonus = P.BuildQualityPerHour * Qh;
		P.PlayerFlipConv = BuildDetail::Clamp(P.PlayerFlipConv + P.BuildFlipConvPerHour * Qh, 0.5, 0.98);

		// Thumbnail: how many people consider it at all (clout) and how far each Post goes (a Plug costs a little less).
		P.PlayerClout = (Th >= 0.0 ? 1.0 + P.BuildCloutPerHour * Th : 1.0 / (1.0 - P.BuildCloutPerHour * Th)) * P.PlayerClout;   // under 4 hours 1 / (1 + 0.5 x hours lost): 3 h x0.5, 2 h x0.33, 0 h x0.2
		P.PlayerPlugCost = BuildDetail::Clamp(1.0 - P.BuildPlugCostPerHour * Th, 0.5, 1.5);

		// Network: followers on day one, and a bigger, faster Post economy.
		P.PlayerStartNodes = BuildDetail::Clamp(std::floor(P.PlayerStartNodes + P.BuildStartNodesPerHour * NhRaw + 0.5), 1.0, 4.0);
		P.PostsMax = BuildDetail::Clamp(std::floor(P.PostsMax + P.BuildBankPerHour * NhRaw + 0.5), 3.0, 8.0);
		P.PostsStart = P.PostsMax;
		Jam.regen *= 1.0 + P.BuildRegenPerHour * Nh;
	}

	/** The numbers the player feels for a build (call it with the Params and Jam ApplyBuild produced). */
	inline BuildSummary SummarizeBuild(const Params& P, const JamDef& Jam)
	{
		BuildSummary S;
		S.postsMax = P.PostsMax;
		S.postsRegen = Jam.regen;
		S.startNodes = static_cast<int>(P.PlayerStartNodes + 0.5);
		S.plugCost = P.PlayerPlugCost;
		S.flipConv = P.PlayerFlipConv;
		S.quality = P.PlayerBaseQuality + P.PlayerBuildBonus;
		S.appeal = 1.0 + P.AppealSlope * (S.quality - P.AppealMid) / P.AppealScale;
		if (S.quality < P.LowQualityThreshold)
		{
			S.appeal *= P.LowQualityPenalty;
		}
		S.clout = P.PlayerClout;
		return S;
	}
}
