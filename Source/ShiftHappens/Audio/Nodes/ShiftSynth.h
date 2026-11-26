// Rate for Rate - Game Off 2026

#pragma once

#include "Audio/Nodes/ShiftComposite.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "HAL/Platform.h"
#include <initializer_list>
#include "Math/UnrealMathUtility.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundVertex.h"
#include "Templates/TupleFwd.h"

/**
 * The C++ counterpart of Tools/py/ms_graph.py: typed helpers that add the engine's own MetaSound nodes to a graph being built (oscillators,
 * envelopes, filters, mixers, ...) and wire them, with the same node keys, pins and values the Python builder used. Nothing here makes sound
 * itself; every call adds one or a few ordinary engine nodes, so a recipe written with it is the same graph as before.
 */
namespace ShiftAudio
{
	enum class ESig : uint8 { Audio, Float, Trigger, Int32 };

	/** A signal: one output of a node in the graph. */
	struct FSig
	{
		const Metasound::INode* Node = nullptr;
		Metasound::FVertexName Pin;
		ESig Type = ESig::Float;

		bool IsValid() const { return Node != nullptr; }
	};

	/** A pin value: a signal to connect, or a number to set as the pin's default. */
	struct FArg
	{
		FSig Sig;
		float Num = 0.0f;
		bool bSig = false;
		bool bNone = false;

		FArg() : bNone(true) {}
		FArg(float InNum) : Num(InNum) {}
		FArg(double InNum) : Num(static_cast<float>(InNum)) {}
		FArg(int32 InNum) : Num(static_cast<float>(InNum)) {}
		FArg(bool bInValue) : Num(bInValue ? 1.0f : 0.0f) {}
		FArg(const FSig& InSig) : Sig(InSig), bSig(true) {}
	};

	struct FPinValue
	{
		const TCHAR* Name;
		FArg Value;
	};

	using FPins = std::initializer_list<FPinValue>;

	/** A mono or stereo layer: a signal (mono) or a left/right pair, with its gain (for mixers). */
	struct FMono
	{
		FSig Audio;
		float Gain = 1.0f;
	};

	struct FStereo
	{
		FSig Left;
		FSig Right;
	};

	/** One input of a stereo mixer: a stereo pair and its gain, a number or (when GainSig is valid) a float signal. */
	struct FStereoItem
	{
		FStereo Audio;
		float Gain = 1.0f;
		FSig GainSig;
	};

	/** The graph that is being built, with the helpers. */
	class FSynth
	{
	public:
		explicit FSynth(const TCHAR* InName) : G(InName)
		{
			// the noise seed of a graph follows its name, as the Python builder's did
			int32 Sum = 0;
			for (const TCHAR* C = InName; *C; ++C) { Sum += static_cast<int32>(*C); }
			Seed = 1000 + (Sum % 5000);
		}

		FGraphBuilder G;

		/** The noise seed counter: a recipe split over several graphs hands it on, so the noise is the one a single graph would have had. */
		int32 GetSeed() const { return Seed; }
		void SetSeed(int32 InSeed) { Seed = InSeed; }

		// ---- generic -------------------------------------------------------------------------------------------------------
		/** A node by key (see the table in ShiftSynth.cpp), N is the input count of the sized mixers and sequences. */
		const Metasound::INode* Node(const TCHAR* Key, int32 N = 0);
		const Metasound::INode* Make(const TCHAR* Key, FPins Pins, int32 N = 0);
		void Feed(const Metasound::INode* Node, const TCHAR* Pin, const FArg& Value);
		void FeedArray(const Metasound::INode* Node, const TCHAR* Pin, const TArray<float>& Values);
		FSig Out(const Metasound::INode* Node, const TCHAR* Pin, ESig Type) const;

		// ---- sources -------------------------------------------------------------------------------------------------------
		FSig Noise();                                   // white noise with a fixed seed
		FSig Osc(const TCHAR* Kind, const FArg& Freq, const FArg& Fm = FArg(), const FArg& Sync = FArg());

		// ---- envelopes (the same shapes as Graph.ad / exp_env / glide / adsr) ---------------------------------------------------
		FSig Ad(const FSig& Trig, float Attack, float Decay, float CurveA = 1.0f, float CurveD = 1.0f, bool bAudio = true, FSig* OutOnDone = nullptr);
		FSig ExpEnv(const FSig& Trig, float Tau, float Attack = 0.002f, bool bAudio = true, float MaxLen = -1.0f);
		FSig Glide(const FSig& Trig, float F0, float F1, float Dur, bool bAudio = true);
		FSig Adsr(const FSig& TrigOn, const FSig& TrigOff, float Attack, float Decay, float Sustain, float Release);

		// ---- maths ---------------------------------------------------------------------------------------------------------
		FSig Mul(const FSig& A, const FArg& B);         // Audio x Audio, Audio x Float, Float x Float
		FSig Add(const FSig& A, const FArg& B);
		FSig SubF(const FArg& A, const FArg& B);
		FSig FScale(const FSig& A, float K);
		FSig Mix(const TArray<FMono>& Items);           // Audio Mixer (Mono, N), groups of 8
		FStereo MixStereo(const TArray<TPair<FStereo, float>>& Items);
		FStereo MixStereo(const TArray<FStereoItem>& Items);

		// ---- filters, effects ----------------------------------------------------------------------------------------------
		struct FSvf { FSig Lp; FSig Hp; FSig Bp; FSig Bs; };
		FSvf Svf(const FSig& In, const FArg& Cutoff, float Q = 0.7f);
		FSig Ladder(const FSig& In, const FArg& Cutoff, float Resonance = 1.0f);
		FSig LogSweep(const FSig& U, float F0, float F1);
		FSig Lp1(const FSig& In, float Cutoff);
		FSig MidiHz(const FArg& Note);
		FSig DelayTrigger(const FSig& Trig, float Seconds);
		FStereo Reverb(const FSig& Left, const FSig& Right, float Wet, float Dry, float Decay, float Damp);
		FStereo Pan(const FSig& In, float Amount);

		// ---- QA taps (invisible inside the composite; they publish to FShiftAudioStats while the meter run asks for it) ------------------
		void MeterTap(const FStereo& Mix);
		void HitTap(const FSig& Trig, const FString& Name, int32 Expected);
		void LevelTap(const FSig& Audio, const FString& Name);
		void StepTap(const FSig& Count);

	private:
		int32 Seed = 1000;
	};

	/** MIDI note numbers and dB: the same conversions the Python builder used. */
	inline float DbToLinear(float Db) { return FMath::Pow(10.0f, Db / 20.0f); }
}
