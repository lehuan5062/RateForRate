// Rate for Rate - Game Off 2026

#include "Game/ShiftAchievements.h"

#include <algorithm>
#include "Game/ShiftResults.h"
#include "Game/ShiftRun.h"
#include "HAL/Platform.h"
#include "Sim/ShiftGraph.h"
#include "Sim/ShiftSim.h"

namespace
{
	constexpr float NightOwlSeconds = 60.0f;      // seconds of night shift in one run
	constexpr int32 SnowballFollowers = 10;       // voters that come around on their own ...
	constexpr double SnowballWindow = 30.0;       // ... within this many seconds of the player flipping a hub
	constexpr double SpeedrunFraction = 1.0 / 3.0; // disqualified before this fraction of the jam
	constexpr int32 WorstRankFrom = 7;            // "worst rank": this place or lower
	constexpr int32 BestQualityTo = 3;            // "best game": this quality rank or better
}

const TCHAR* ShiftAch::IdOf(int32 Index)
{
	static const TCHAR* const Ids[Num] = { TEXT("HonestRun"), TEXT("NightOwl"), TEXT("Snowball"), TEXT("BannedSpeedrun"), TEXT("BestGameWorstRank"), TEXT("ShiftedTheBlame"), TEXT("GateCrasher"),
		TEXT("AudienceCapture"), TEXT("GiantSlayer"), TEXT("StreamersPet"), TEXT("EyeContact"), TEXT("StickySituation"), TEXT("Schadenfreude"), TEXT("NobodyPlayedIt"), TEXT("FairAndSquare") };
	return Index >= 0 && Index < Num ? Ids[Index] : TEXT("");
}

void FShiftAchTracker::Reset()
{
	*this = FShiftAchTracker();
}

void FShiftAchTracker::OnEvent(const FShiftEvent& E, const ShiftSim::Sim& S)
{
	switch (E.Type)
	{
	case EShiftEventType::PlayerFlipped:
	{
		const ShiftSim::Graph& G = S.GetGraph();
		if (E.bSock && G.IsGateNode(E.Node) && S.IsGated(E.Node))
		{
			Earn(EShiftAch::GateCrasher);   // a sockpuppet got past a gate that the real player could not
		}
		if (!E.bSock && std::find(G.celebrities.begin(), G.celebrities.end(), E.Node) != G.celebrities.end())
		{
			Earn(EShiftAch::AudienceCapture);   // a celebrity listens only once their own followers back you
		}
		if (bSnowActive)
		{
			++SnowClicks;
		}
		else if (!E.bSock && G.IsGateNode(E.Node))
		{
			bSnowActive = true;
			SnowStart = S.Time();
			SnowGains0 = -1;   // set on the next Tick, once the run layer has counted this flip
			SnowClicks = 0;
		}
		break;
	}
	case EShiftEventType::SpotlightHit:
		Earn(EShiftAch::StreamersPet);
		break;
	case EShiftEventType::BlameTaken:
		Earn(EShiftAch::ShiftedTheBlame);
		break;
	case EShiftEventType::ModSweepHit:
		Earn(EShiftAch::EyeContact);
		break;
	default:
		break;
	}
}

void FShiftAchTracker::Tick(const ShiftSim::Sim& S, const FShiftFlipStats& Flips, float Seconds, bool bNightHeld)
{
	if (bNightHeld)
	{
		NightHeld += Seconds;
		if (NightHeld >= NightOwlSeconds)
		{
			Earn(EShiftAch::NightOwl);
		}
	}
	if (bSnowActive)
	{
		if (SnowGains0 < 0)
		{
			SnowGains0 = Flips.Gains;
		}
		else if (Flips.Gains - SnowGains0 - SnowClicks >= SnowballFollowers)
		{
			Earn(EShiftAch::Snowball);
			bSnowActive = false;
		}
		else if (S.Time() - SnowStart >= SnowballWindow)
		{
			bSnowActive = false;
		}
	}
}

void FShiftAchTracker::OnNightLatch()
{
	Earn(EShiftAch::StickySituation);
}

void FShiftAchTracker::OnFinished(const FShiftResultFacts& Facts, const FShiftFlipStats& Flips, int32 JamIndex)
{
	if (Facts.bDisqualified)
	{
		if (Facts.RunFraction < SpeedrunFraction)
		{
			Earn(EShiftAch::BannedSpeedrun);
		}
		return;
	}
	if (Facts.QualityRank <= BestQualityTo && Facts.Rank >= WorstRankFrom)
	{
		Earn(EShiftAch::BestGameWorstRank);
	}
	if (!Facts.bWon)
	{
		return;
	}
	if (!Facts.bCheated && Flips.SockGains == 0)
	{
		Earn(EShiftAch::HonestRun);
	}
	if (JamIndex == 2)
	{
		Earn(EShiftAch::GiantSlayer);
	}
	if (JamIndex == 3)
	{
		Earn(EShiftAch::FairAndSquare);   // the control group: nobody famous to blame it on
	}
	if (Facts.bRivalCollapsed)
	{
		Earn(EShiftAch::Schadenfreude);
	}
	if (Facts.QualityRank >= Facts.NumEntries - 2)
	{
		Earn(EShiftAch::NobodyPlayedIt);
	}
}
