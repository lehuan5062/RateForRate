// Shift Happens - Game Off 2026

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "HAL/Platform.h"
#include "Math/Color.h"
#include "Math/MathFwd.h"

enum class EShiftScreen : uint8
{
	Title,
	JamSelect,
	Build,
	Play,        // the run itself; the results card is an overlay while the run is over
};

/** Clickable things. Ids are shared between the widget (hit regions) and the controller (actions). */
namespace EShiftUi
{
	enum Id : int32
	{
		None = -1,
		TitleStart = 1,
		Back = 2,
		JamCard0 = 10,            // 10, 11, 12
		SliderQuality = 20,       // 20, 21, 22 (value = 0..1 along the track)
		SliderThumbnail = 21,
		SliderNetwork = 22,
		Upload = 30,
		Retry = 40,
		NextJam = 41,
		Menu = 42,
		Resume = 43,
		Blame = 44,
		CoachDismiss = 45,
		Mute = 50,
		Share = 51,
		FakeInfo = 52,            // hover only: the explanation of the sockpuppet-votes chip
	};
}

/** One jam card on the select screen. */
struct FShiftHudJamCard
{
	FString Title;
	FString Subtitle;
	FString RivalAuthor;
	FString RivalGame;
	FString RivalBlurb;
	TArray<FString> Facts;     // short bullet lines about the jam
	int32 Difficulty = 1;      // 1..3
	int32 BestRank = 0;        // 1-based, 0 = never played
};

struct FShiftHudForecastRow
{
	int32 EntryId = 0;
	FString Name;
	float Value = 0.0f;        // quality (stars) or vote share, depending on the list
	FLinearColor Color = FLinearColor::White;
	bool bPlayer = false;
};

/** The build screen: 12 hours split across quality, thumbnail and network, plus a live forecast. */
struct FShiftHudBuild
{
	int32 Hours[3] = { 4, 4, 4 };
	int32 TotalHours = 12;
	FString JamTitle;
	TArray<FShiftHudForecastRow> ByQuality;    // best quality first
	TArray<FShiftHudForecastRow> ByForecast;   // best forecast share first
	bool bForecastReady = false;               // the vote forecast is in (the quality list is always there)
	float ForecastProgress = 0.0f;             // 0..1 of the plays while it is not
	int32 ForecastRank = 0;                    // 1-based
	int32 ForecastRankLo = 0;                  // the range of ranks the player could end in when entries are within a hair of its share (equal when nobody is)
	int32 ForecastRankHi = 0;
	float ForecastShare = 0.0f;
	float ForecastSpread = 0.0f;               // how far one play of a steady player strays from the forecast (0..1)
	int32 QualityRank = 0;                     // 1-based
	FString RivalFollowers;                    // "1.2M"
	FString Effects[3];                        // what each lever comes to in play (quality, thumbnail, network), with numbers, from the same rules the sim uses
};

/** One card of the notification feed in the right gutter: a toast, or a news headline (bNews). */
struct FShiftHudFeedItem
{
	FString Text;
	FLinearColor Color = FLinearColor::White;
	float Alpha = 1.0f;       // fades in and out
	bool bNews = false;
};

/** An urgent event that takes over the middle of the bottom bar for a few seconds: a strike, the Shift the Blame offer, a disqualification. The right gutter is peripheral, this is not. */
struct FShiftHudAlert
{
	int32 Kind = 0;                    // 0 = none (EAlertKind in the controller)
	FString Title;
	FString Detail;
	FLinearColor Color = FLinearColor::White;
	float Frac = -1.0f;                // the draining bar, 1 full .. 0 empty (< 0: none)
	float Age = 0.0f;                  // seconds on screen, for the slide-in
	float SecondsLeft = -1.0f;         // a countdown at the right end of the band (< 0: none)
	bool bClickable = false;           // Shift the Blame: the whole band is the button (key B too)
};

/** One leaderboard row. */
struct FShiftHudRow
{
	int32 EntryId = 0;
	int32 Rank = 0;                    // 0-based
	FString Name;
	float Share = 0.0f;                // 0..1 of all vote weight
	float Votes = 0.0f;
	FLinearColor Color = FLinearColor::White;
	bool bPlayer = false;
	bool bRival = false;
};

/** Hover card for a voter. Positions are in design units (1280 x 720). */
struct FShiftHudTooltip
{
	bool bVisible = false;
	FVector2f Anchor = FVector2f::ZeroVector;
	float AnchorRadius = 14.0f;
	FString Title;
	FString Line1;
	FString Line2;
	FString Status;
	FLinearColor StatusColor = FLinearColor::White;
	FLinearColor OwnerColor = FLinearColor::White;
	float Conviction = 0.0f;
	bool bHasOwner = false;
};

/** A point on the results scatter plots. */
struct FShiftHudDot
{
	float X = 0.0f;
	float Y = 0.0f;
	FLinearColor Color = FLinearColor::White;
	bool bPlayer = false;
};

/** Least-squares line through a point set, with R squared (how much of the spread of Y the line explains). */
struct FShiftHudFit
{
	float Slope = 0.0f;
	float Intercept = 0.0f;
	float R2 = 0.0f;

	static FShiftHudFit Compute(const TArray<FShiftHudDot>& Dots)
	{
		FShiftHudFit Fit;
		const int32 N = Dots.Num();
		if (N < 3)
		{
			return Fit;
		}
		double Sx = 0.0, Sy = 0.0;
		for (const FShiftHudDot& D : Dots)
		{
			Sx += D.X;
			Sy += D.Y;
		}
		const double Mx = Sx / N;
		const double My = Sy / N;
		double Sxx = 0.0, Sxy = 0.0, Syy = 0.0;
		for (const FShiftHudDot& D : Dots)
		{
			Sxx += (D.X - Mx) * (D.X - Mx);
			Sxy += (D.X - Mx) * (D.Y - My);
			Syy += (D.Y - My) * (D.Y - My);
		}
		if (Sxx < 1.0e-12 || Syy < 1.0e-12)
		{
			return Fit;
		}
		Fit.Slope = static_cast<float>(Sxy / Sxx);
		Fit.Intercept = static_cast<float>(My - Fit.Slope * Mx);
		Fit.R2 = static_cast<float>((Sxy * Sxy) / (Sxx * Syy));
		return Fit;
	}
};

/** Everything the HUD draws, rebuilt by the controller each frame. */
struct FShiftHudModel
{
	EShiftScreen Screen = EShiftScreen::Play;
	bool bShiftDown = false;
	bool bMuted = false;
	TArray<FShiftHudJamCard> JamCards;
	int32 SelectedJam = 0;
	FShiftHudBuild Build;
	FString CoachText;                 // tutorial card, empty when none
	float CoachAlpha = 0.0f;
	bool bBlameOffer = false;          // Shift the Blame prompt
	float BlameSeconds = 0.0f;         // time left to take it
	float BlameWindow = 2.0f;
	FShiftHudAlert Alert;              // the urgent event on screen (Kind 0 = none): its offer is the Blame prompt
	float EdgeFlash = 0.0f;            // 0..1: a thin frame at the left, right and bottom edge of the screen (never over the board's voters), for strikes, the offer and the disqualification
	FLinearColor EdgeColor = FLinearColor::White;

	// header
	FString JamName;
	FString DateLabel;
	FString DeadlineLabel;
	int32 Day = 1;
	int32 Days = 31;
	float RunProgress = 0.0f;          // 0..1 through the whole run

	// leaderboard, best first
	TArray<FShiftHudRow> Rows;
	float FakeShare = 0.0f;            // share of all vote weight held by sockpuppet votes right now (0..1)
	float FakeLimit = 0.0f;            // the fake share Heat can hold steady (cooling = held-vote heating), above it you heat up; 0 = no limit
	int32 LeaderboardMode = 0;         // 0 compact (top three and you), 1 full, 2 hidden (a one-line chip)

	// player resources
	float Posts = 0.0f;
	float PostsMax = 5.0f;
	float Heat = 0.0f;                 // 0..1
	float HeatRedZone = 0.6f;
	int32 Strikes = 0;
	int32 MaxStrikes = 3;
	float ShadowbanSeconds = 0.0f;
	bool bShowHeat = false;            // the Heat bar and strike pips are always there during play, dimmed until night shift has been used (bHeatActive)
	bool bHeatActive = false;          // night held, or Heat above zero, or a strike: draw the Heat bar at full strength
	float HeatProjected = 0.0f;        // 0..1: Heat after the next sockpuppet, shown while SHIFT is held (0 otherwise)
	bool bModsEnabled = false;
	float NextSweepIn = -1.0f;         // seconds, < 0 when unknown
	float RallyCooldown = 0.0f;        // seconds until the next Rally is possible (0 = ready)
	float RallyCost = 1.0f;            // Posts a Rally costs
	float EyeAwayLeft = -1.0f;         // seconds left of a window in which the Mod Eye looks away, < 0 when none
	float EyeAwayWarnIn = -1.0f;       // seconds until an announced window starts, < 0 when none is announced
	float FinalPushIn = -1.0f;         // seconds until the rival's final push, shown for the last 12 s before it, < 0 otherwise
	float FinalPushLeft = -1.0f;       // seconds of the final push left while it is on, < 0 otherwise
	float FinalPushBonus = 0.0f;       // the player's extra Post refill in the final push (0.25 = 25%)
	float RivalPosts = 0.0f;           // the rival's Post bank now, and its size (0 = the rival has no bank): shown on the final push card
	float RivalPostsMax = 0.0f;
	float FakeHeatRate = 0.0f;         // Heat a second the fake votes held right now add, before cooling
	FString FakeInfo;                  // the plain-language explanation shown when the sockpuppet-votes chip is hovered
	FString NightHint;                 // what night shift could do for you right now ("" = the plain hint)
	float FinalAuditIn = -1.0f;        // seconds until the final audit while it is close (the last 10 s), < 0 otherwise

	// state
	float Night = 0.0f;                // 0 day .. 1 night (smoothed)
	bool bNightActive = false;
	bool bPaused = false;
	bool bOver = false;

	// feedback
	TArray<FShiftHudFeedItem> Feed;    // newest first: toasts and news, drawn as cards in the right gutter (nothing floats over the board)
	FShiftHudTooltip Tooltip;

	// results (shown when bOver)
	FString OutcomeTitle;
	FString OutcomeLine1;
	FString OutcomeLine2;
	FString OutcomeLine3;
	FString PlaysLine;
	FLinearColor OutcomeColor = FLinearColor::White;
	float HonestShare = 0.0f;
	float SockShare = 0.0f;
	bool bCanNextJam = false;
	TArray<FShiftHudDot> QualityDots;      // x = quality, y = final vote share
	TArray<FShiftHudDot> FameDots;         // x = vote share at the start (fame), y = final vote share
	FShiftHudFit QualityFit;
	FShiftHudFit FameFit;
};
