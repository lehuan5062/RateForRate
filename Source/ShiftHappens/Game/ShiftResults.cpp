// Rate for Rate - Game Off 2026

#include "Game/ShiftResults.h"

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Game/ShiftContent.h"
#include "Game/ShiftRun.h"
#include "HAL/Platform.h"
#include "Math/RandomStream.h"
#include "Math/UnrealMathUtility.h"
#include "Sim/ShiftParams.h"
#include "Sim/ShiftSim.h"

namespace
{
	constexpr double LandslideMargin = 0.15;   // a win or a loss by this much of the whole vote
	constexpr double NarrowMargin = 0.03;      // a win by less than this
	constexpr double NearMissMargin = 0.04;    // a loss by less than this
}

int32 ShiftResults::QualityRankOf(const ShiftSim::Sim& S)
{
	int32 Better = 0;
	for (int32 E = 1; E < S.NumEntries(); ++E)
	{
		if (S.GetEntry(E).quality > S.GetEntry(ShiftSim::kPlayerId).quality)
		{
			++Better;
		}
	}
	return Better + 1;
}

FShiftResultFacts ShiftResults::MakeFacts(const ShiftSim::Sim& S, const FShiftFlipStats& Flips, bool bBlameTaken, const TArray<float>& InitialShares)
{
	FShiftResultFacts F;
	F.bDisqualified = S.Disqualified();
	F.Rank = S.RankOf(ShiftSim::kPlayerId) + 1;
	F.QualityRank = QualityRankOf(S);
	F.NumEntries = S.NumEntries();
	F.RunFraction = S.Time() / FMath::Max(1.0, S.TotalSeconds());
	const int32 Leader = S.Leader();
	F.bWon = !F.bDisqualified && Leader == ShiftSim::kPlayerId;
	const double Total = FMath::Max(1.0, S.TotalWeight());
	const double Votes = S.Tally(ShiftSim::kPlayerId);
	double Runner = 0.0;
	for (int32 E = 0; E < S.NumEntries(); ++E)
	{
		if (E != Leader)
		{
			Runner = FMath::Max(Runner, S.Tally(E));
		}
	}
	F.Margin = F.bWon ? (Votes - Runner) / Total : (S.Tally(Leader) - Votes) / Total;
	F.bCheated = Flips.SockGains > 0 || S.SockBoughtWeight() > 0.0;

	// where the rival started: its rank by the share it held at the beginning (ties go to the lower entry id, as the sim ranks)
	int32 RivalStartRank = 1;
	if (InitialShares.IsValidIndex(S.RivalId()))
	{
		for (int32 E = 0; E < S.NumEntries(); ++E)
		{
			if (E != S.RivalId() && InitialShares.IsValidIndex(E) && (InitialShares[E] > InitialShares[S.RivalId()] || (InitialShares[E] == InitialShares[S.RivalId()] && E < S.RivalId())))
			{
				++RivalStartRank;
			}
		}
	}
	F.bRivalCollapsed = bBlameTaken || (RivalStartRank <= 2 && S.RankOf(S.RivalId()) + 1 >= 4);
	return F;
}

FShiftResultConditions ShiftResults::Classify(const FShiftResultFacts& F)
{
	FShiftResultConditions C;
	if (F.bDisqualified)
	{
		C.Outcome = TEXT("DQ");
		C.General = F.RunFraction < 0.5 ? TEXT("DQEarly") : TEXT("DQLate");
		return C;
	}
	const bool bBottomQuality = F.QualityRank >= F.NumEntries - 2;
	if (F.bWon)
	{
		C.Outcome = TEXT("Win");
		C.General = F.bCheated ? TEXT("SockpuppetWin") : TEXT("CleanWin");
		if (F.Margin < NarrowMargin) { C.Specific.Add(TEXT("NarrowWin")); }
		if (F.Margin >= LandslideMargin) { C.Specific.Add(TEXT("Landslide")); }
		if (F.QualityRank >= (F.NumEntries + 1) / 2) { C.Specific.Add(TEXT("QualityIgnored")); }   // won while the quality rank was in the lower half
		if (bBottomQuality) { C.Specific.Add(TEXT("NobodyPlayed")); }
		if (F.bRivalCollapsed) { C.Specific.Add(TEXT("RivalCollapsed")); }
		return C;
	}
	C.Outcome = TEXT("Lose");
	C.General = F.Margin < NearMissMargin ? TEXT("NearMiss") : (F.QualityRank <= 3 ? TEXT("QualityIgnored") : TEXT("NobodyPlayed"));
	if (F.Margin < NearMissMargin) { C.Specific.Add(TEXT("NearMiss")); }
	if (F.QualityRank <= 3) { C.Specific.Add(TEXT("QualityIgnored")); }   // lost with a good game
	if (bBottomQuality) { C.Specific.Add(TEXT("NobodyPlayed")); }
	if (F.Margin >= LandslideMargin) { C.Specific.Add(TEXT("Landslide")); }
	if (F.bRivalCollapsed) { C.Specific.Add(TEXT("RivalCollapsed")); }
	C.Specific.Remove(C.General);   // the general condition is always in the pool anyway
	return C;
}

FString ShiftResults::PickBlurb(const FShiftResultConditions& C, FRandomStream& Rng, FString* OutCondition)
{
	TArray<FString> Pool = C.Specific;
	Pool.Add(C.General);
	FString Condition = Pool[Rng.RandRange(0, Pool.Num() - 1)];
	FString Line = FShiftContent::PickBlurb(C.Outcome, Condition, Rng);
	if (Line.IsEmpty() && Condition != C.General)
	{
		Condition = C.General;
		Line = FShiftContent::PickBlurb(C.Outcome, Condition, Rng);
	}
	if (Line.IsEmpty())
	{
		Condition = TEXT("(any)");
		Line = FShiftContent::PickBlurbAny(C.Outcome, Rng);
	}
	if (OutCondition)
	{
		*OutCondition = Condition;
	}
	return Line;
}
