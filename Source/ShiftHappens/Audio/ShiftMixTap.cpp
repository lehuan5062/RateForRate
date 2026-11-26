// Rate for Rate - Game Off 2026

#include "Audio/ShiftMixTap.h"

#include "Containers/UnrealString.h"
#include "HAL/Platform.h"
#include "IAudioExtensionPlugin.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/ScopeLock.h"

void FShiftMixTap::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock)
{
	float BufferPeak = 0.0f;
	double Squares = 0.0;
	for (int32 i = 0; i < NumSamples; ++i)
	{
		const float S = AudioData[i];
		BufferPeak = FMath::Max(BufferPeak, FMath::Abs(S));
		Squares += static_cast<double>(S) * S;
	}
	FScopeLock Guard(&Lock);
	Peak = FMath::Max(Peak, BufferPeak);
	SumSquares += Squares;
	Samples += NumSamples;
	++Buffers;
}

const FString& FShiftMixTap::GetListenerName() const
{
	static const FString Name = TEXT("ShiftMixTap");
	return Name;
}

void FShiftMixTap::Take(float& OutPeak, float& OutRms, int32& OutBuffers)
{
	FScopeLock Guard(&Lock);
	OutPeak = Peak;
	OutRms = Samples > 0 ? static_cast<float>(FMath::Sqrt(SumSquares / static_cast<double>(Samples))) : 0.0f;
	OutBuffers = Buffers;
	Peak = 0.0f;
	SumSquares = 0.0;
	Samples = 0;
	Buffers = 0;
}
