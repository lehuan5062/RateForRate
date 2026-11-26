// Rate for Rate - Game Off 2026

#include "Game/ShiftForecastService.h"

#include "Async/Async.h"
#include "Async/Future.h"
#include "Game/ShiftForecast.h"
#include "HAL/Platform.h"
#include "Math/UnrealMathUtility.h"
#include "Templates/SharedPointer.h"

namespace
{
	/** Worlds kept: the jam the player is looking at, and the ones just left. */
	constexpr int32 KeepWorlds = 4;
}

FShiftForecastService::~FShiftForecastService()
{
	Cancel();
}

TSharedPtr<const FShiftForecastModel> FShiftForecastService::Find(int32 JamIndex, uint32 Seed) const
{
	for (const TSharedPtr<const FShiftForecastModel>& Model : Finished)
	{
		if (Model.IsValid() && Model->JamIndex == JamIndex && Model->Seed == Seed)
		{
			return Model;
		}
	}
	return nullptr;
}

float FShiftForecastService::Progress() const
{
	return Job.IsValid() && Job->Total > 0 ? FMath::Clamp(static_cast<float>(Job->Done.load(std::memory_order_relaxed)) / static_cast<float>(Job->Total), 0.0f, 1.0f) : 0.0f;
}

void FShiftForecastService::Request(int32 JamIndex, uint32 Seed)
{
	if (Find(JamIndex, Seed).IsValid())
	{
		bWaiting = false;
		if (Job.IsValid())
		{
			Job->bCancel.store(true, std::memory_order_relaxed);   // the player is back on a world that is ready: the other job is not wanted
		}
		return;
	}
	if (Job.IsValid() && Job->JamIndex == JamIndex && Job->Seed == Seed && !Job->bCancel.load(std::memory_order_relaxed))
	{
		bWaiting = false;
		return;
	}
	if (Job.IsValid())
	{
		Job->bCancel.store(true, std::memory_order_relaxed);
		bWaiting = true;
		WaitingJam = JamIndex;
		WaitingSeed = Seed;
		return;
	}
	StartJob(JamIndex, Seed);
}

void FShiftForecastService::StartJob(int32 JamIndex, uint32 Seed)
{
	TSharedPtr<FJob> NewJob = MakeShared<FJob>();
	NewJob->JamIndex = JamIndex;
	NewJob->Seed = Seed;
	NewJob->Total = ShiftForecastRun::DesignBuilds().Num() * ShiftForecastRun::SamplesPerBuild;
	Job = NewJob;
	bWaiting = false;
	JobDone = Async(EAsyncExecution::ThreadPool, [NewJob]()
		{
			NewJob->Result = ShiftForecastRun::BuildModel(NewJob->JamIndex, NewJob->Seed, NewJob->bCancel, NewJob->Done);
		});
}

void FShiftForecastService::Tick()
{
	if (!Job.IsValid() || !JobDone.IsReady())
	{
		return;
	}
	const TSharedPtr<FJob> Finishing = Job;
	Job.Reset();
	JobDone = TFuture<void>();
	if (Finishing->Result.IsValid() && !Finishing->bCancel.load(std::memory_order_relaxed))
	{
		Finished.Insert(Finishing->Result, 0);
		if (Finished.Num() > KeepWorlds)
		{
			Finished.SetNum(KeepWorlds);
		}
	}
	if (bWaiting)
	{
		StartJob(WaitingJam, WaitingSeed);
	}
}

void FShiftForecastService::Cancel()
{
	bWaiting = false;
	if (Job.IsValid())
	{
		Job->bCancel.store(true, std::memory_order_relaxed);
		JobDone.Wait();
		Job.Reset();
		JobDone = TFuture<void>();
	}
}
