// Shift Happens - Game Off 2026

#pragma once

#include "Containers/UnrealString.h"
#include "HAL/CriticalSection.h"
#include "HAL/Platform.h"
#include "IAudioExtensionPlugin.h"
#include "ISubmixBufferListener.h"

/**
 * QA only (-ShiftMixTap): listens to the engine's main output submix, the final mix of everything this game process plays (music, effects, limiter,
 * primary volume) and nothing else. Other applications (a music player) and Windows system sounds never enter the engine's mix, so they cannot
 * show up here. It keeps only the peak and RMS level per window; no samples are stored or written anywhere.
 */
class FShiftMixTap : public ISubmixBufferListener
{
public:
	virtual void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock) override;
	virtual bool IsRenderingAudio() const override { return true; }   // keep the main submix running even when it is silent, so silence is measured, not skipped
	virtual const FString& GetListenerName() const override;

	/** Peak and RMS of everything rendered since the last call (reset by the call); OutBuffers is how many audio buffers that covered. */
	void Take(float& OutPeak, float& OutRms, int32& OutBuffers);

private:
	FCriticalSection Lock;
	float Peak = 0.0f;
	double SumSquares = 0.0;
	int64 Samples = 0;
	int32 Buffers = 0;
};
