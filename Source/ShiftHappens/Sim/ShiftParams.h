// Shift Happens - engine-independent numeric core.
// All tunables live here as plain data. Defaults are the balanced values; Data/Tuning.csv
// (Name,Value) and Data/Jams.csv override them through SetParam / SetJamField.
// Every Params field is a double (counts are rounded where used) so one name->field table
// covers the whole struct.
#pragma once

#include <string>

namespace ShiftSim
{

	constexpr int kMaxEntries = 16;   // buffer size only; a jam has JamDef::entries (9)
	constexpr int kPlayerId = 0;      // entry index of the player

	struct Params
	{
		// ---- time -------------------------------------------------------------
		double Dt = 0.05;                       // fixed step, 20 Hz

		// ---- vote weight / influence ---------------------------------------------
		double WeightScale = 30.0;              // V = round(WeightScale * (1+followers)^WeightExp)
		double WeightExp = 1.3;
		double InflBase = 1.0;                  // infl = sqrt(InflBase + followers)

		// ---- graph generation ----------------------------------------------------
		double MemberMin = 5.0;                 // members per community, informational target (node total wins)
		double MemberMax = 9.0;
		double MemberFollowHubProb = 0.92;      // member follows the local hub
		double MemberRandomFollows = 1.0;       // extra random follows per member
		double RandomFollowSameCommunityProb = 0.75;
		double HubRandomFollowProb = 0.5;       // local hub follows one other local hub
		double ReciprocalProb = 0.15;           // target fraction of directed edges that are reciprocated
		double CelebFollowProb = 0.12;          // per member, per celebrity
		double HubFollowsCelebProb = 0.1;       // per local hub, per celebrity
		double CelebMinFollowersPerCommunity = 1.0;
		double BridgeCrossHubFollows = 2.0;     // bridge follows this many foreign hubs
		double BridgeCrossMemberFollows = 1.0;
		double BridgeFollowers = 2.0;           // foreign members that follow a bridge
		double HubExtraFollowers = 0.0;         // audience outside the sampled graph, added to hubs
		double LayoutWidth = 1280.0;
		double LayoutHeight = 720.0;
		double LayoutMargin = 56.0;
		double LayoutMarginRight = 200.0;       // voters keep this far from the right edge: the HUD's gutter (leaderboard, states, feed) lives there
		double LayoutRingX = 410.0;             // community centres sit on this ellipse
		double LayoutRingY = 250.0;
		double LayoutMinDist = 40.0;            // minimum node spacing the layout aims for
		double LayoutCelebRadius = 110.0;       // celebrities cluster this far from the centre

		// ---- entries ---------------------------------------------------------------
		double QualityMin = 3.0;                // every entry's Q ~ U[QualityMin, QualityMax]
		double QualityMax = 8.0;
		double PlayerBaseQuality = 5.0;         // player's Q = base + build bonus
		double PlayerBuildBonus = 0.0;
		double RivalClout = 3.0;
		double OtherCloutMin = 0.2;
		double OtherCloutMax = 0.5;
		double PlayerClout = 0.05;
		double ChampionFraction = 0.8;          // members of a community that back its championed entry
		double ChampionConvMin = 0.5;
		double ChampionConvMax = 0.9;
		double RivalConv = 1.0;                 // celebrity hubs
		double RivalFollowerConvMin = 0.5;
		double RivalFollowerConvMax = 0.9;
		double PlayerStartNodes = 2.0;
		double PlayerPlugCost = 1.0;            // Posts a Plug on a foreign voter costs the player (the Thumbnail lever: a better thumbnail makes every Post go further)
		// The build levers (Sim/ShiftBuildRules.h): per hour over an even 4/4/4 split; under it a lever loses BuildBelowSlope times as fast.
		double BuildBelowSlope = 2.0;
		double BuildQualityPerHour = 0.225;      // Quality: quality points (PlayerBuildBonus)
		double BuildFlipConvPerHour = 0.015;    // Quality: conviction of a voter the player wins (PlayerFlipConv)
		double BuildCloutPerHour = 0.5;         // Thumbnail: clout, relative per hour (the first look of undecided voters)
		double BuildPlugCostPerHour = 0.00375;   // Thumbnail: PlayerPlugCost drops by this per hour
		double BuildStartNodesPerHour = 0.5;    // Network: start voters
		double BuildBankPerHour = 0.5;          // Network: the Post bank (PostsMax)
		double BuildRegenPerHour = 0.003;     // Network: Post refill, relative per hour
		double PlayerStartConv = 0.6;
		double PlayerStartMaxFollowers = 1.0;   // player starts on low-degree nodes

		// ---- opinion dynamics (per step, synchronous) -------------------------------
		double Capture = 0.35;                  // weight of followers' opinions on their followee
		double ConvBase = 0.4;                  // pressure weight = infl * (ConvBase + ConvWeight * c)
		double ConvWeight = 0.6;
		double AppealSlope = 0.15;              // appeal = 1 + AppealSlope * (Q - AppealMid) / AppealScale
		double AppealMid = 5.0;
		double AppealScale = 5.0;
		double LowQualityThreshold = 3.0;       // broken build
		double LowQualityPenalty = 0.6;         // appeal multiplier below the threshold
		double Front1 = 1.25;                   // front-page pressure multiplier by tally rank
		double Front2 = 1.12;
		double Front3 = 1.06;
		double Stickiness = 3.0;                // sup = P[cur] + Stickiness * c
		double FlipMargin = 1.1;
		double DrainRate = 0.12;
		double RegainRate = 0.08;
		double FlipConv = 0.3;                  // conviction after a natural switch
		double AdoptThreshold = 0.3;            // undecided: adopt the top entry when sumP exceeds this
		double AdoptRate = 1.0;                 // per second
		double AdoptConv = 0.25;
		double DriftRate = 0.15;                // per second, when pressure is low
		double DriftConv = 0.2;

		// ---- actions ---------------------------------------------------------------
		double PlugDrain = 0.45;                // conviction removed from a foreign node per Plug
		double PlugReinforce = 0.3;             // conviction added to an owned node per Plug
		double PlugFlipThreshold = 0.15;
		double PlugFlipConv = 0.55;
		double AttackTelegraph = 1.5;           // seconds between another entry choosing a voter of the player's and the plug landing (0 = at once): the player can answer in between
		double PlugLock = 6.0;                  // immunity seconds after a Plug flip
		double PlayerFlipConv = 0.8;            // the same two numbers for a flip by the player: a small fanbase is loyal (set to PlugFlipConv / PlugLock for the symmetric game)
		double PlayerPlugLock = 6.0;
		double SockStrength = 3.0;
		double SockConv = 0.25;
		double SockLock = 3.0;
		double SockHeat = 22.0;
		double SockRequiresNight = 1.0;         // 1: Sock refused unless the night-held flag is set
		double SockLaunderConv = 0.9;           // a sockpuppet flip becomes a real vote once its conviction reaches this: three Posts of reinforcing (0.25 + 3 x 0.3) in its 3 s immunity (>1 = off, strikes and the audit always revert)
		double SockOverheatRefuse = 1.0;        // 1: a sockpuppet that would take Heat to the cap is refused (Overheated, costs nothing) instead of striking at once
		double EyeAwayHeatMult = 0.0;           // while the Mod Eye looks away (Sim::SetEyeAway): the Heat that night shift and a sockpuppet click add is multiplied by this ...
		double EyeAwayHeldMult = 0.5;           // ... and the Heat that held sockpuppet votes add by this
		double SockHeldHeat = 50.0;             // Heat per second per 100% of all vote weight held in sockpuppet votes, flagged or backed: holding fake votes keeps you warm (0 = off); cooling is 3 a second, so a 6% fake share is the most Heat can hold steady
		double SockTolerance = 1.0;             // share of all vote weight the player may hold in sockpuppet votes (flagged or backed) when the Mod Eye passes; above it the sweep strikes (>= 1 = off, the default: SockHeldHeat does the job)
		double SockReview = 15.0;               // seconds: the final audit also purges sockpuppet votes bought in the last this-many seconds of the jam, backed or not (0 = only the ones still flagged)
		double FinalAudit = 1.0;                // 1: at the end of the jam the Mod Eye purges every sockpuppet vote that is still flagged (Sim::RevertSockpuppets)
		double RallyCost = 1.0;                 // Posts a Rally costs (right-click on a voter the player holds)
		double RallyBoost = 0.3;                // conviction a Rally adds to that voter and to every voter of the player's one hop away
		double RallyLock = 6.0;                 // immunity seconds a Rally also gives every voter it boosts (immune voters cannot be plugged or drained)
		double RallyCooldown = 8.0;             // seconds before the next Rally (longer than the 6 s shield: one cluster cannot be kept shielded for ever)
		double ParryOnReinforce = 1.0;          // 1: reinforcing a voter that has an announced attack pending (a ring closing in) cancels the soonest one on it: the ring bounces
		double ParryCost = 1.0;                 // Posts a reinforcement that parries costs (1 = the same as any reinforcement)
		double PostsMax = 5.0;
		double PostsStart = 5.0;
		double PostsRegen = 0.5;                // per second; Jams.csv Regen overrides per jam

		double GatesApplyToAll = 0.0;           // 1: rival and other entries are gated by hubs too (player is always gated)

		// ---- heat / moderation (the Mod Eye schedule itself is NOT in the core) -----------
		double HeatDecay = 3.0;                 // per second while night is not held
		double HeatNightRate = 4.0;             // per second while night is held
		double HeatRedZone = 60.0;
		double HeatMax = 100.0;
		double HeatAfterStrike = 35.0;
		double ShadowbanSeconds = 6.0;
		double MaxStrikes = 3.0;
		// Shift the Blame (Sim/ShiftBlameRules.h): a last chance at the strike that would disqualify the player, not a free fourth strike. It works only when the fake votes the
		// mods just took away were few enough to pass for the rival's doing.
		double BlameMaxFake = 0.20;             // the most the fake votes the Blame's basis counts may come to, as a share of all vote weight (see BlameBasis; 0.08 on the fatal strike alone, round 5, still let a gambler win 42% on GAME OFF)
		double BlameWindow = 5.0;               // seconds the offer stays open (0 = no Shift the Blame at all); the playtester asked for 5 after 4
		double BlameBasis = 1.0;                // what BlameMaxFake is measured on: 0 = the fatal strike's own fake votes, 1 = the fake votes of all of the player's strikes added up (a pile of them at strike one makes the rival an unlikely culprit at strike three; the playtester: the limit "doesn't matter" when only the last strike counts)
		double BlameRivalConv = 0.25;           // conviction every voter of the rival's loses when the blame lands
		double BlameRivalFreeze = 10.0;         // seconds the rival is busy defending itself afterwards (behaviour layer)

		// ---- query helpers ---------------------------------------------------------
		double TargetFollowerCredit = 0.5;      // value = V + credit * V of non-owned followers
		double HighValueV = 250.0;              // "high value" node threshold for WeakestOwned defaults
	};

	// Per-jam definition (Data/Jams.csv row).
	struct JamDef
	{
		std::string name = "Jam";
		int nodes = 36;
		int communities = 5;
		int bridges = 2;
		int celebs = 2;
		int entries = 9;
		int days = 31;
		double secPerDay = 6.0;
		double regen = 0.5;                     // Posts per second
		double localGate = 0.25;                // fraction of a local hub's followers (V) that must back the player
		double celebGate = 0.15;                // same for celebrity hubs
		int rivalId = 1;                        // entry index of the rival
		double celebExtraFollowers = 25.0;      // audience outside the sampled graph, added to celebrity hubs
		double rivalFollowerFraction = 0.3;     // share of celebrity followers that start rival-owned
		// Behaviour-layer parameters carried by the jam definition (the core itself never reads them).
		double rivalInterval = 3.3;             // seconds between rival actions (StateTree parameter; the plan's first guess was 2)
		double modIntervalMin = 25.0;           // Mod Eye sweep spacing in seconds (director parameter)
		double modIntervalMax = 35.0;
	};

	// Name <-> field access for CSV loading. Names are the C++ field names.
	bool SetParam(Params& P, const std::string& Name, double Value);
	bool GetParam(const Params& P, const std::string& Name, double* OutValue);
	int ParamCount();
	const char* ParamNameAt(int Index);

	bool SetJamField(JamDef& J, const std::string& Name, double Value);
	bool SetJamName(JamDef& J, const std::string& Value);

	// Built-in defaults for jam index 0..2 (Tiny Jam, 48h Pixel Rumble, GAME OFF 2026).
	JamDef DefaultJam(int Index);

} // namespace ShiftSim
