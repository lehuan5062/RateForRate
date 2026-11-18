
#include "ShiftParams.h"

#include "ShiftDetMath.h"
#include <string>

namespace ShiftSim
{

	namespace
	{
		struct ParamFieldEntry
		{
			const char* Name;
			double Params::* Member;
		};

#define SHIFT_P(field) { #field, &Params::field }

		const ParamFieldEntry kParamFields[] = {
			SHIFT_P(Dt),
			SHIFT_P(WeightScale), SHIFT_P(WeightExp), SHIFT_P(InflBase),
			SHIFT_P(MemberMin), SHIFT_P(MemberMax), SHIFT_P(MemberFollowHubProb), SHIFT_P(MemberRandomFollows),
			SHIFT_P(RandomFollowSameCommunityProb), SHIFT_P(HubRandomFollowProb), SHIFT_P(ReciprocalProb),
			SHIFT_P(CelebFollowProb), SHIFT_P(HubFollowsCelebProb), SHIFT_P(CelebMinFollowersPerCommunity),
			SHIFT_P(BridgeCrossHubFollows), SHIFT_P(BridgeCrossMemberFollows), SHIFT_P(BridgeFollowers),
			SHIFT_P(HubExtraFollowers), SHIFT_P(LayoutWidth), SHIFT_P(LayoutHeight), SHIFT_P(LayoutMargin), SHIFT_P(LayoutMarginRight),
			SHIFT_P(LayoutRingX), SHIFT_P(LayoutRingY), SHIFT_P(LayoutMinDist), SHIFT_P(LayoutCelebRadius),
			SHIFT_P(QualityMin), SHIFT_P(QualityMax), SHIFT_P(PlayerBaseQuality), SHIFT_P(PlayerBuildBonus),
			SHIFT_P(RivalClout), SHIFT_P(OtherCloutMin), SHIFT_P(OtherCloutMax), SHIFT_P(PlayerClout),
			SHIFT_P(ChampionFraction), SHIFT_P(ChampionConvMin), SHIFT_P(ChampionConvMax), SHIFT_P(RivalConv),
			SHIFT_P(RivalFollowerConvMin), SHIFT_P(RivalFollowerConvMax), SHIFT_P(PlayerStartNodes), SHIFT_P(PlayerPlugCost), SHIFT_P(BuildBelowSlope), SHIFT_P(BuildQualityPerHour), SHIFT_P(BuildFlipConvPerHour), SHIFT_P(BuildCloutPerHour),
			SHIFT_P(BuildPlugCostPerHour), SHIFT_P(BuildStartNodesPerHour), SHIFT_P(BuildBankPerHour), SHIFT_P(BuildRegenPerHour),
			SHIFT_P(PlayerStartConv), SHIFT_P(PlayerStartMaxFollowers),
			SHIFT_P(Capture), SHIFT_P(ConvBase), SHIFT_P(ConvWeight), SHIFT_P(AppealSlope), SHIFT_P(AppealMid),
			SHIFT_P(AppealScale), SHIFT_P(LowQualityThreshold), SHIFT_P(LowQualityPenalty),
			SHIFT_P(Front1), SHIFT_P(Front2), SHIFT_P(Front3), SHIFT_P(Stickiness), SHIFT_P(FlipMargin),
			SHIFT_P(DrainRate), SHIFT_P(RegainRate), SHIFT_P(FlipConv), SHIFT_P(AdoptThreshold),
			SHIFT_P(AdoptRate), SHIFT_P(AdoptConv), SHIFT_P(DriftRate), SHIFT_P(DriftConv),
			SHIFT_P(PlugDrain), SHIFT_P(PlugReinforce), SHIFT_P(PlugFlipThreshold), SHIFT_P(PlugFlipConv),
			SHIFT_P(AttackTelegraph), SHIFT_P(PlugLock), SHIFT_P(PlayerFlipConv), SHIFT_P(PlayerPlugLock), SHIFT_P(SockStrength), SHIFT_P(SockConv), SHIFT_P(SockLock), SHIFT_P(SockHeat),
			SHIFT_P(SockRequiresNight), SHIFT_P(SockLaunderConv), SHIFT_P(SockOverheatRefuse), SHIFT_P(FinalAudit), SHIFT_P(SockReview), SHIFT_P(SockTolerance), SHIFT_P(SockHeldHeat), SHIFT_P(EyeAwayHeatMult), SHIFT_P(EyeAwayHeldMult), SHIFT_P(RallyCost), SHIFT_P(RallyBoost), SHIFT_P(RallyLock), SHIFT_P(RallyCooldown), SHIFT_P(ParryOnReinforce), SHIFT_P(ParryCost), SHIFT_P(PostsMax), SHIFT_P(PostsStart),
			SHIFT_P(PostsRegen), SHIFT_P(GatesApplyToAll),
			SHIFT_P(HeatDecay), SHIFT_P(HeatNightRate), SHIFT_P(HeatRedZone), SHIFT_P(HeatMax),
			SHIFT_P(HeatAfterStrike), SHIFT_P(ShadowbanSeconds), SHIFT_P(MaxStrikes),
			SHIFT_P(BlameMaxFake), SHIFT_P(BlameWindow), SHIFT_P(BlameRivalConv), SHIFT_P(BlameRivalFreeze),
			SHIFT_P(TargetFollowerCredit), SHIFT_P(HighValueV),
		};

#undef SHIFT_P

		constexpr int kParamFieldCount = static_cast<int>(sizeof(kParamFields) / sizeof(kParamFields[0]));

	} // namespace

	bool SetParam(Params& P, const std::string& Name, double Value)
	{
		for (int i = 0; i < kParamFieldCount; ++i)
		{
			if (Name == kParamFields[i].Name)
			{
				P.*(kParamFields[i].Member) = Value;
				return true;
			}
		}
		return false;
	}

	bool GetParam(const Params& P, const std::string& Name, double* OutValue)
	{
		for (int i = 0; i < kParamFieldCount; ++i)
		{
			if (Name == kParamFields[i].Name)
			{
				if (OutValue != nullptr)
				{
					*OutValue = P.*(kParamFields[i].Member);
				}
				return true;
			}
		}
		return false;
	}

	int ParamCount() { return kParamFieldCount; }

	const char* ParamNameAt(int Index)
	{
		return (Index >= 0 && Index < kParamFieldCount) ? kParamFields[Index].Name : "";
	}

	bool SetJamField(JamDef& J, const std::string& Name, double Value)
	{
		if (Name == "Nodes") { J.nodes = RoundToInt(Value); return true; }
		if (Name == "Communities") { J.communities = RoundToInt(Value); return true; }
		if (Name == "Bridges") { J.bridges = RoundToInt(Value); return true; }
		if (Name == "Celebs") { J.celebs = RoundToInt(Value); return true; }
		if (Name == "Entries") { J.entries = RoundToInt(Value); return true; }
		if (Name == "Days") { J.days = RoundToInt(Value); return true; }
		if (Name == "SecPerDay") { J.secPerDay = Value; return true; }
		if (Name == "Regen") { J.regen = Value; return true; }
		if (Name == "LocalGate") { J.localGate = Value; return true; }
		if (Name == "CelebGate") { J.celebGate = Value; return true; }
		if (Name == "RivalId") { J.rivalId = RoundToInt(Value); return true; }
		if (Name == "CelebExtraFollowers") { J.celebExtraFollowers = Value; return true; }
		if (Name == "RivalFollowerFraction") { J.rivalFollowerFraction = Value; return true; }
		if (Name == "RivalInterval") { J.rivalInterval = Value; return true; }
		if (Name == "ModIntervalMin") { J.modIntervalMin = Value; return true; }
		if (Name == "ModIntervalMax") { J.modIntervalMax = Value; return true; }
		return false;
	}

	bool SetJamName(JamDef& J, const std::string& Value)
	{
		J.name = Value;
		return true;
	}

	JamDef DefaultJam(int Index)
	{
		JamDef J;
		if (Index <= 0)
		{
			J.name = "Tiny Jam #7";
			J.nodes = 36; J.communities = 5; J.bridges = 2; J.celebs = 2;
			J.days = 31; J.secPerDay = 6.0; J.regen = 0.472;
			J.localGate = 0.25; J.celebGate = 0.15;
			J.celebExtraFollowers = 8.0;
			J.modIntervalMin = 25.0; J.modIntervalMax = 35.0;
		}
		else if (Index == 1)
		{
			J.name = "48h Pixel Rumble";
			J.nodes = 52; J.communities = 7; J.bridges = 3; J.celebs = 2;
			J.days = 31; J.secPerDay = 6.0; J.regen = 0.457;
			J.localGate = 0.25; J.celebGate = 0.20;
			J.celebExtraFollowers = 8.0;
			J.modIntervalMin = 15.0; J.modIntervalMax = 25.0;
		}
		else
		{
			J.name = "GAME OFF 2026";
			J.nodes = 72; J.communities = 9; J.bridges = 4; J.celebs = 2;
			J.days = 31; J.secPerDay = 8.0; J.regen = 0.486;
			J.localGate = 0.25; J.celebGate = 0.25;
			J.celebExtraFollowers = 14.0;
			J.modIntervalMin = 12.0; J.modIntervalMax = 20.0;
		}
		return J;
	}

} // namespace ShiftSim
