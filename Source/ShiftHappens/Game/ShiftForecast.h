// Shift Happens - Game Off 2026

#pragma once

#include <atomic>
#include "Containers/Array.h"
#include "Game/ShiftBuild.h"
#include "HAL/Platform.h"
#include "Templates/SharedPointer.h"

/** What the build screen shows for one build: the average outcome of a steady player in the real world of the run. */
struct FShiftForecast
{
	int32 PlayerRank = 0;                 // 1-based by forecast share (ties: the lower entry id first)
	int32 RankLo = 0;                     // the rank the player has if the entries within a hair of its share all finish behind it ...
	int32 RankHi = 0;                     // ... and ahead of it (equal to RankLo when nobody is that close)
	float PlayerShare = 0.0f;
	int32 QualityRank = 0;                // 1-based by quality
	TArray<float> Quality;                // per entry id
	TArray<int32> QualityOrder;           // entry ids, best quality first
	TArray<float> Share;                  // per entry id: the average final share
	TArray<int32> ShareOrder;             // entry ids, best forecast first
};

/**
 * The build screen's forecast plays the real world: the graph, the entries, the rival and the other entries of the run the player is about to start, with a steady player (a Plug
 * about every 0.7 s on a good target) and the build being tried. One play is one future, and a steady player's share swings by 15 or more points from play to play, so the forecast
 * is the average of many plays, and the plays are not made for every build the slider can reach: a grid of builds is played (eight variations each) and one smooth surface is
 * fitted to it per entry, forced to rise with every lever's hours (more hours never make a lever worse). The chart then cannot jitter, and it cannot contradict the descriptions.
 */
namespace ShiftForecastRun
{
	/** Which variations the samples apply (bit mask). */
	enum EVariation : int32
	{
		VaryDynamics = 1,    // the luck of the undecided voters
		VaryStart = 2,       // the first click comes up to 0.7 s later
		VaryTargets = 4,     // the player sometimes picks the second best target (also reseeds the dynamics)
	};

	/** The variations of the forecast's samples and the size of the player's slips. */
	constexpr int32 ForecastVariation = VaryStart | VaryTargets;
	constexpr double ForecastNoise = 0.2;
	/** Plays per grid build. */
	constexpr int32 SamplesPerBuild = 8;
	/** Grid builds within this share of each other are a tie for the rank display. */
	constexpr float RankTie = 0.015f;

	/** How one sample differs from the plain run (sample 0 is always the plain run: the real world, the dynamics seeded from the run seed, clicking from the first moment). */
	struct FVariant
	{
		uint32 DynSeed = 0;     // seeds the dynamics stream and the bot's own stream
		double Start = 0.0;     // seconds before the first click
		double Noise = 0.0;     // the bot's score jitter (fraction)
	};

	FVariant MakeVariant(uint32 Seed, int32 SampleIndex, int32 Variation, double Noise);

	/** Plays one sample to the end of the jam with the real run's rules (no Mod Eye: a steady honest player never meets it) and returns every entry's final share (per entry id). */
	bool PlaySample(int32 JamIndex, uint32 Seed, const FShiftBuild& Build, const FVariant& Variant, TArray<float>& OutShare);

	/** The builds that are played: every split in steps of two hours that spends at most the twelve (72 of them). */
	TArray<FShiftBuild> DesignBuilds();

	/** The quality of every entry in a world (per entry id; the player's slot is the neutral build's). Cheap: one sim init. */
	TArray<float> WorldQualities(int32 JamIndex, uint32 Seed);

	/** Fills the quality part of a forecast for a build: the quality of every entry (the other entries' from WorldQualities), best first, and the player's place. Needs no plays. */
	void FillQuality(int32 JamIndex, const TArray<float>& OtherQuality, const FShiftBuild& Build, FShiftForecast& Out);
}

/** The forecast of one world, ready to look up: the smoothed average share of every entry for every build the build screen allows. */
struct FShiftForecastModel
{
	static constexpr int32 MaxHours = 8;

	int32 JamIndex = 0;
	uint32 Seed = 0;
	int32 NumEntries = 0;
	float PlayerSpread = 0.0f;           // how far one play of a steady player strays from the average (the standard deviation of its share, 0..1)
	TArray<float> OtherQuality;          // per entry id; the player's slot is unused (it depends on the build)
	TArray<float> Table;                 // [cell * NumEntries + entry]; a cell is (Quality * 9 + Thumbnail) * 9 + Network, only the builds of at most 12 hours are filled

	static int32 CellOf(int32 Quality, int32 Thumbnail, int32 Network) { return (Quality * (MaxHours + 1) + Thumbnail) * (MaxHours + 1) + Network; }
	static bool IsLegal(int32 Quality, int32 Thumbnail, int32 Network)
	{
		return Quality >= 0 && Thumbnail >= 0 && Network >= 0 && Quality <= MaxHours && Thumbnail <= MaxHours && Network <= MaxHours && Quality + Thumbnail + Network <= FShiftBuild::TotalHours;
	}

	/** The forecast for a build (a build over 12 hours or past 8 in a lever is clamped to the nearest one the screen allows). */
	FShiftForecast Evaluate(const FShiftBuild& Build) const;
};

namespace ShiftForecastRun
{
	/** Plays the grid and fits the model. Safe on any thread; runs the plays on the worker pool. Returns null when Cancel went true. Done counts the finished plays (SamplesPerBuild times the
	 *  grid size in all). Deterministic: the same world always gives the same model, whatever the scheduling. */
	TSharedPtr<const FShiftForecastModel> BuildModel(int32 JamIndex, uint32 Seed, const std::atomic<bool>& Cancel, std::atomic<int32>& Done);

	/** The fit alone, from the plays: Shares[(DesignIndex * SamplesPerBuild + Sample) * NumEntries + Entry]. Exposed for the commandlet and the tests. */
	void FitModel(FShiftForecastModel& Model, const TArray<float>& Shares);
}
