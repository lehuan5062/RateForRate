// Rate for Rate - Game Off 2026

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
		JamCard0 = 10,            // 10, 11, 12, 13
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
		Recordings = 53,          // results: open the folder the recordings of your jams are saved in
		LearnToPlay = 3,          // title and reference page: start the guided lessons
		Pause = 5,                // QA only (-ShiftUi): toggles the pause menu
		HowTo = 4,                // title and pause menu: the one-page reference (also the key H)
		LessonContinue = 46,      // the lesson card: read, go on
		LessonLeave = 47,         // leave the lessons for the title screen
		LessonNext = 48,          // the end of a lesson: the next lesson
		LessonPlay = 49,          // the end of a lesson: pick a jam
		Settings = 54,            // the gear button (top right): opens and closes the settings menu
		SettingsClose = 55,       // behind the open menu: a click anywhere outside it closes it
		SettingsPanel = 56,       // the open menu itself: swallows clicks on its padding
		WindowFullscreen = 60,    // settings menu: window mode buttons; the id minus 60 is the EWindowMode value (0 fullscreen, 1 borderless, 2 windowed)
		WindowBorderless = 61,
		WindowWindowed = 62,
		EffectsOn = 63,           // settings menu: sound effects on / off
		EffectsOff = 64,
		MusicOn = 65,             // settings menu: music on / off (the M key and id 50 mute or restore both)
		MusicOff = 66,
		Achievements = 70,        // title: the achievements and career page (also the key A)
		Credits = 71,             // title: the credits page (also the key C)
		Markers = 67,             // settings menu: shapes on every voter and leaderboard row, so colour is not the only signal
		LargeText = 68,           // settings menu: a floor for the text size
		ReduceMotion = 69,        // settings menu: steady instead of pulsing
		Quit = 73,                // title: leave the game (also Esc)
		GlassToggle = 74,         // settings menu: the see-through window on / off (next launch)
		GlassOpacity = 75,        // settings menu: slider, how opaque the paper behind the board is (value = 0..1 along the track)
		ResultsTab0 = 80,         // results: the two tabs of the chart area (80 quality and fame, 81 over time)
		Replays = 76,             // title: the replays page (also the key R)
		ViewerPause = 77,         // the replay viewer: pause / play
		ViewerSeek = 78,          // ... where in the jam (slider, value = 0..1 along the bar)
		WatchReplay = 79,         // results: watch the jam that just ended again
		ViewerSpeed0 = 100,       // 100, 101, 102, 103: 1x, 2x, 4x, 8x
		ReplayWatch0 = 110,       // 110..116: the Watch button of each row of the replays page
		ExtrasClose = 72,         // behind either page: a click anywhere closes it
	};
}

/** A part of the HUD a lesson step points at (the HUD knows where each one is drawn). */
enum class EShiftLessonAnchor : uint8
{
	None,
	Leaderboard,
	Posts,
	NightChip,
	Heat,
};

/** A voter to point at, in design pixels (1280 x 720). */
struct FShiftLessonSpot
{
	FVector2f Center = FVector2f::ZeroVector;
	float Radius = 14.0f;
	FString Label;
};

/** What the HUD draws for the step the player is on. */
struct FShiftLessonView
{
	bool bActive = false;
	int32 Chapter = 0;          // 1-based
	int32 Step = 0;             // 1-based
	int32 Steps = 0;
	FString Title;
	FString Body;
	bool bReading = false;      // the world is frozen and the card waits for Continue
	bool bCheer = false;        // the step was just done
	bool bFinish = false;       // the lesson is over: the card offers what to do next
	bool bHasNext = false;      // ... and there is another lesson after this one
	bool bModEye = false;       // the Mod Eye countdown belongs on screen yet (it is the subject of the last steps of the night shift lesson)
	EShiftLessonAnchor Anchor = EShiftLessonAnchor::None;
	TArray<FShiftLessonSpot> Spots;
};

/** A vote count that rises from a voter for a moment ("+82" when the player wins one, "-31" when one is lost): design pixels, seconds. */
struct FShiftHudFloat
{
	float X = 0.0f;
	float Y = 0.0f;
	float Age = 0.0f;
	float Life = 1.3f;
	FString Text;
	FLinearColor Color = FLinearColor::White;
	bool bBig = false;      // a hub or a celebrity: larger
};

/** A moment on the results timeline: T = 0..1 of the jam, Kind 0 strike, 1 celebrity won, 2 the rival's final push. */
struct FShiftHudMark
{
	float T = 0.0f;
	uint8 Kind = 0;
};

/** One row of the replays page. */
struct FShiftHudReplayRow
{
	FString Title;
	FString Detail;
	bool bStale = false;      // recorded under other rules (the build tag differs): it may play out differently
};

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

/** One achievement on its page. */
struct FShiftHudAchItem
{
	FString Name;
	FString Desc;
	bool bEarned = false;
	bool bHidden = false;     // a hidden achievement shows no description until it is earned
};

/** One jam of the career strip. */
struct FShiftHudCareerRow
{
	FString Jam;
	int32 Runs = 0;
	int32 Wins = 0;
	int32 Dq = 0;
	int32 BestRank = 0;       // 0 = never finished
	float BestShare = 0.0f;   // 0..1
};

/** One card of the notification feed in the right gutter: a toast, or a news headline (bNews). */
struct FShiftHudFeedItem
{
	FString Text;
	FLinearColor Color = FLinearColor::White;
	float Alpha = 1.0f;       // fades in and out
	bool bNews = false;
	bool bInk = false;        // chatter: ink text on the card, the colour only on the accent bar
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
	float Trend = 0.0f;                // the change of share over the last 20 s, in points of all vote weight (+ gaining, - losing; 0 until there are 5 s of history)
	FLinearColor Color = FLinearColor::White;
	bool bPlayer = false;
	bool bRival = false;
};

/** One thing the player can do to the voter under the cursor, and the key for it. */
struct FShiftHudAction
{
	FString Key;                 // CLICK, RIGHT-CLICK, SHIFT + CLICK
	FString Text;                // what it does and what it costs
	bool bAvailable = true;      // false: drawn dim (no Posts, immune, gated)
};

/** The game's own cursor shapes, chosen from what is under the pointer. */
enum class EShiftCursor : uint8
{
	Arrow,      // nothing special
	Button,     // over a button
	Plug,       // a voter you can flip
	Reinforce,  // one of your own voters
	Sock,       // night shift: a sockpuppet can go here
	Blocked,    // a voter you cannot act on (immune, overheated, gated)
};

/** Hover card for a voter. Positions are in design units (1280 x 720). */
struct FShiftHudTooltip
{
	bool bVisible = false;
	EShiftCursor Cursor = EShiftCursor::Arrow;
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
	TArray<FShiftHudAction> Actions;
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

#if !UE_BUILD_SHIPPING
/** One piece of trailer text on screen (the scripted trailer run (not in Shipping builds), Trailer/ShiftTrailer.h). */
struct FShiftTrailerText
{
	FString Text;                 // lines separated by '|'
	bool bSlam = true;            // true: a full-screen statement (huge tracked caps, teal and red ghost copies that snap together on the beat); false: a lower-left caption
	float Age = 0.0f;             // seconds since it appeared
	float Life = 2.0f;            // seconds it stays on screen, fade-out included
	float Y = -1.0f;              // slam: design y of the middle of the text (negative: the middle of the screen)
	float X = -1.0f;              // slam: design x of the middle of the text (negative: the middle of the screen, 640)
	float Px = 0.0f;              // slam: text size in design pixels (0: the big default)
	bool bBox = true;             // slam over the game: a dark box behind the words (off for a few words that must not hide what is behind them)
};

/** What the scripted trailer run asks the HUD to draw on top of the game: cover, flash, spotlight, text. */
struct FShiftHudTrailer
{
	bool bActive = false;
	float Black = 0.0f;           // full-screen black, 0..1
	float White = 0.0f;           // full-screen white flash, 0..1
	float SpotAlpha = 0.0f;       // 0..1: the rest of the screen is dimmed around SpotRect
	FVector4f SpotRect = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);   // x, y, width, height in design pixels
	bool bSpotMods = false;       // the spotlight is on the top bar's MODS countdown: the HUD fits SpotRect to it (its text changes width)
	bool bHideCursor = false;     // the scripted cursor is hidden (a card covers the screen)
	float KeyAlpha = 0.0f;        // the "OR HOLD" card with the SHIFT keycap (0..1): the keycap presses by itself when the night is held
	float Zoom = 1.0f;            // the zoom-in of the run (the board camera zooms the same way): everything but the trailer's own overlay is scaled by this about ZoomFocus
	FVector2f ZoomFocus = FVector2f(640.0f, 360.0f);   // design pixels; this point stays where it is
	TArray<FShiftTrailerText> Texts;
};
#endif

/** Everything the HUD draws, rebuilt by the controller each frame. */
struct FShiftHudModel
{
	EShiftScreen Screen = EShiftScreen::Play;
	bool bShiftDown = false;
	bool bSystemCursor = false;        // -ShiftSystemCursor: keep the operating system's arrow instead of the game's own cursor
	bool bPseudoCursor = false;        // -ShiftPseudoCursor: a scripted cursor (trailer footage) is drawn at PseudoCursor even with no mouse over the window
	FVector2f PseudoCursor = FVector2f::ZeroVector;   // design units
	bool bEffectsOn = true;            // the sound effects switch of the settings menu
	bool bMusicOn = true;              // the music switch
	int32 WindowMode = 2;             // EWindowMode as an int: 0 fullscreen, 1 borderless (windowed fullscreen), 2 windowed
	bool bSettingsOpen = false;        // the settings menu (effects, music, window mode) dropped from the gear button
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
	float SympathyPoints = -1.0f;      // underdog sympathy: how many points of lead past the threshold the entry in front has (< 0 = none): shown as a pinned chip while it is on
	FString SympathyLeader;            // ... the entry in front, whose voters drift back
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
	FString OutcomeNews;      // the end-of-jam headline, painted under the blurb (the feed is covered by the results card)
	FString PlaysLine;
	FString RecordingPath;                 // where the recording of this jam was saved ("" = none)
	bool bReplay = false;                  // a recording is being replayed: your clicks do nothing
	float TransitionCover = 0.0f;          // 0..1: how much of the window the paper wash between two screens still covers (0 = none)
	TArray<FShiftHudFloat> Floats;         // the vote counts rising from voters that were just won or lost
	bool bViewer = false;                  // ... from the replays page or the results card: the viewer bar is up
	bool bViewerPaused = false;
	int32 ViewerSpeed = 0;                 // 0..3: 1x, 2x, 4x, 8x
	float ViewerProgress = 0.0f;           // 0..1 of the jam
	TArray<FShiftHudReplayRow> Replays;    // the replays page (ExtrasPage 3)

	// guided lessons and the reference page
	FShiftLessonView Lesson;
	bool bInLesson = false;                // the run on screen is a lesson (the pause menu says so)
	bool bHowTo = false;                   // the reference page is open
	bool bMarkers = false;                 // accessibility: a shape per entry next to its colour
	bool bLargeText = false;
	bool bReduceMotion = false;
	bool bGlassActive = false;             // the window is see-through right now
	bool bGlassSaved = false;              // the saved switch (differs from bGlassActive until the next launch)
	int32 GlassOpacity = 70;               // percent
	float BackgroundOpacity = 1.0f;        // what the full-screen washes are scaled by: 1 unless the window is see-through
	struct FMarker { float X = 0.0f; float Y = 0.0f; int32 Owner = 0; };
	TArray<FMarker> Markers;               // one per voter, in design pixels (only while bMarkers)
	int32 ExtrasPage = 0;                  // title: 0 none, 1 the achievements and career page, 2 the credits
	TArray<FShiftHudAchItem> Achievements;
	TArray<FShiftHudCareerRow> Career;
	TArray<FString> CreditsText;           // one line each; a line ending in ":" is a heading
	TArray<FString> NewAchievements;       // results: the names earned in this run
	TArray<FString> HowToText;             // its blocks, "Title: text" each
	bool bLessonNudge = false;             // never finished the basics: the title screen points at Learn to play
	FLinearColor OutcomeColor = FLinearColor::White;
	float HonestShare = 0.0f;
	float SockShare = 0.0f;
	bool bCanNextJam = false;
	TArray<FShiftHudDot> QualityDots;      // x = quality, y = final vote share
	TArray<FShiftHudDot> FameDots;         // x = vote share at the start (fame), y = final vote share
	FShiftHudFit QualityFit;
	FShiftHudFit FameFit;
	int32 ResultsTab = 0;                  // results: 0 the quality and fame plots, 1 the shares over time
	int32 TimelineEntries = 0;             // results timeline: entries per sample
	int32 TimelineDays = 31;
	TArray<float> TimelineT;               // each sample's moment, 0..1 of the jam
	TArray<float> TimelineShares;          // TimelineEntries shares per sample (0..1)
	TArray<FShiftHudMark> TimelineMarks;

#if !UE_BUILD_SHIPPING
	FShiftHudTrailer Trailer;              // the scripted trailer run (not in Shipping builds)
#endif
};
