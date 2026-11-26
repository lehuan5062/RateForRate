// Rate for Rate - Game Off 2026

#pragma once

#include "Async/Future.h"
#include <atomic>
#include "Containers/Array.h"
#include "Game/ShiftForecast.h"
#include "HAL/Platform.h"
#include "Templates/SharedPointer.h"

/**
 * Computes the build screen's forecast off the game thread. One world at a time: a request for another world cancels the one that is running (it stops within a play or two), the
 * latest request wins, and finished worlds are kept (the last few), so going back and forth between jams costs nothing. All of it is driven from the game thread; the worker only
 * sees its own job (values and atomics, never the service), so destroying the service waits for the job and nothing dangles.
 */
class FShiftForecastService
{
public:
	~FShiftForecastService();

	/** Asks for the forecast of a world. Returns at once; a finished or running request for the same world is left alone. */
	void Request(int32 JamIndex, uint32 Seed);

	/** Once a frame: collects a finished job and starts the request that is waiting. */
	void Tick();

	/** Drops the waiting request and stops the running job (waits for it): the cost of the forecast must not touch a jam being played. */
	void Cancel();

	/** The finished forecast of a world, or null. */
	TSharedPtr<const FShiftForecastModel> Find(int32 JamIndex, uint32 Seed) const;

	bool IsComputing() const { return Job.IsValid(); }
	/** 0..1 of the running job's plays. */
	float Progress() const;

private:
	struct FJob
	{
		int32 JamIndex = 0;
		uint32 Seed = 0;
		int32 Total = 0;
		std::atomic<bool> bCancel{ false };
		std::atomic<int32> Done{ 0 };
		TSharedPtr<const FShiftForecastModel> Result;   // written by the worker before its future completes, read after
	};

	void StartJob(int32 JamIndex, uint32 Seed);

	TSharedPtr<FJob> Job;
	TFuture<void> JobDone;
	bool bWaiting = false;
	int32 WaitingJam = 0;
	uint32 WaitingSeed = 0;
	TArray<TSharedPtr<const FShiftForecastModel>> Finished;
};
