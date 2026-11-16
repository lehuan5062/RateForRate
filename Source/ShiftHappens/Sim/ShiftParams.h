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
    double LayoutRingX = 480.0;             // community centres sit on this ellipse
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
    double PlugLock = 6.0;                  // immunity seconds after a Plug flip
    double SockStrength = 3.0;
    double SockConv = 0.25;
    double SockLock = 3.0;
    double SockHeat = 28.0;
    double SockRequiresNight = 1.0;         // 1: Sock refused unless the night-held flag is set
    double SockLaunderConv = 2.0;           // optional: a sockpuppet flip becomes organic once conviction reaches this (>1 = disabled, strikes always revert)
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
