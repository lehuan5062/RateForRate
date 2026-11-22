// Shift Happens - Game Off 2026

#pragma once

#include <atomic>
#include "Containers/Array.h"
#include "HAL/CriticalSection.h"
#include "HAL/Platform.h"
#include "Templates/UniquePtr.h"
#include "UObject/NameTypes.h"

/**
 * QA numbers of the composite audio nodes (the music and the effects are one node each, so they have no graph outputs to read): small tap nodes inside
 * the composites (Audio/Nodes/ShiftTapNodes.cpp, invisible in the assets) publish here, and UShiftAudioMeter reads them. They only work while the meter
 * has switched them on (bEnabled), so the game itself pays nothing.
 */
class FShiftAudioStats
{
public:
	static FShiftAudioStats& Get();

	/**
	 * Whether the composite nodes build their QA taps at all: only for a meter run that measures sound (-ShiftAudioMeter without -ShiftAudioCpu). The game and the CPU
	 * measurement run the graphs without them, so what they cost is what the player pays.
	 */
	static bool TapsWanted();

	/** Set by the meter run: the taps compute and publish only then. */
	std::atomic<bool> bEnabled{ false };

	/** The generator instance (MetaSound InstanceID) the taps publish for: the game plays its own music at the same time as the meter run's. ~0 = none. */
	std::atomic<uint64> SelectedInstance{ ~uint64(0) };

	/** QA (-ShiftMusicBench=<spec>,<spec>): which of the benchmark specs the next music generator builds in place of the day layer; -1 = the real music. */
	std::atomic<int32> BenchIndex{ -1 };

	/** QA (the meter's long music case): the music runs every gated voice all the time and measures what each makes outside its window (see Nodes/ShiftMusicNode.cpp). */
	std::atomic<bool> bShadowGating{ false };
	bool Accepts(uint64 Instance) const { return bEnabled.load(std::memory_order_relaxed) && SelectedInstance.load(std::memory_order_relaxed) == Instance; }

	/** The level of the stereo mix inside a composite (before its volume and limiter): follower values, like the meters the graphs used to have. */
	struct FLevels
	{
		std::atomic<float> Peak{ 0.0f };
		std::atomic<float> Rms{ 0.0f };
		std::atomic<float> Low{ 0.0f };
		std::atomic<float> Mid{ 0.0f };
		std::atomic<float> High{ 0.0f };
		std::atomic<int32> BlocksTotal{ 0 };   // blocks analysed since the case started: the meter's "an update arrived"

		// since the meter last called ResetAccumulators (it does so when the warm-up second is over): the largest values and the sums of the follower values
		std::atomic<float> PeakMax{ 0.0f };
		std::atomic<float> RmsMax{ 0.0f };
		std::atomic<float> LowMax{ 0.0f };
		std::atomic<float> MidMax{ 0.0f };
		std::atomic<float> HighMax{ 0.0f };
		std::atomic<double> RmsSum{ 0.0 };
		std::atomic<double> LowSum{ 0.0 };
		std::atomic<double> MidSum{ 0.0 };
		std::atomic<double> HighSum{ 0.0 };
		std::atomic<int32> Blocks{ 0 };

		void ResetAccumulators();
	};
	FLevels Master;

	/** The step counter of the music clock (1 .. 128), and how many times it has changed. */
	std::atomic<int32> Step{ 0 };
	std::atomic<int32> StepChanges{ 0 };

	/** A named tap: a pattern lane's hit count (Expected = hits per loop) or a voice's peak level. */
	struct FTap
	{
		FName Name;
		int32 Expected = 0;
		bool bCounter = false;
		bool bTail = false;                  // a gated voice's tail check: Level is the loudest sound the voice made outside its window
		std::atomic<int32> Count{ 0 };
		std::atomic<float> Level{ 0.0f };
	};

	/** The tap with this name (made on first use); the pointer stays valid for the life of the process. */
	FTap* FindOrAddTap(const FName& Name, bool bCounter, int32 Expected);
	FTap* FindOrAddTailTap(const FName& Name);
	FTap* TapAt(int32 Index) const;
	int32 IndexOf(const FTap* Tap) const;
	int32 NumTaps() const;

	/** Zero every number (the taps keep their names). */
	void ResetValues();

private:
	mutable FCriticalSection Lock;
	TArray<TUniquePtr<FTap>> Taps;
};
