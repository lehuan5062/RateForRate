// Shift Happens - Game Off 2026

// "Shift Music": the looping game music as ONE composite node (the C++ counterpart of Tools/py/ms_music.py). Inside: the Harmonix clock (Trigger To
// Transport -> Metronome 112.5 BPM, 4/4, 8-bar loop -> 16th-note subdivision), the pattern lanes (Array Get + Trigger Compare, two voices through a
// Trigger Sequence), the voices (ShiftVoices.cpp), the three layers (day, night, tension), the layer mixer, the plate reverb, the master volume and a
// limiter: all the engine's own nodes with the same wiring and values as before. The node shows the Harmonix clock as its "MIDI Clock" output.

#include <atomic>
#include "Audio/Nodes/ShiftVoices.h"
#include "Audio/ShiftAudioStats.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "HAL/Platform.h"
#include "HarmonixMetasound/DataTypes/MidiClock.h"
#include "Internationalization/Text.h"
#include "Math/UnrealMathUtility.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataReference.h"
#include "MetasoundFacade.h"
#include "MetasoundLiteral.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundParamHelper.h"
#include "MetasoundPrimitives.h"
#include "MetasoundTrigger.h"
#include "MetasoundVertex.h"
#include "MetasoundVertexData.h"
#include "Misc/Build.h"
#include "Misc/CommandLine.h"
#include "Misc/CString.h"
#include "Misc/Parse.h"
#include "ShiftComposite.h"
#include "ShiftSynth.h"
#include "Templates/Function.h"
#include "Templates/TupleFwd.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/NameTypes.h"

#define LOCTEXT_NAMESPACE "ShiftHappens_MusicNode"

namespace ShiftAudio
{
	using namespace Voices;
	using Metasound::INode;
	using Metasound::FLiteral;
	using Metasound::FNodeClassMetadata;

	namespace
	{
		constexpr int32 Bars = 8;
		constexpr int32 Steps = Bars * 16;
		constexpr float PanComp = 1.4142f;   // the equal-power panner is -3 dB at the centre; the original mix compensated, so do we
		constexpr float Tempo = 112.5f;
		const float StepSeconds = 60.0f / 112.5f / 4.0f;
		const float LoopSeconds = Steps * StepSeconds;

		// ---- pattern data (the original arrangement) -------------------------------------------------------------------------------
		struct FChord { int32 Bass; int32 Tones[3]; };
		const FChord DayProg[8] = { { 48, { 60, 64, 67 } }, { 43, { 59, 62, 67 } }, { 45, { 60, 64, 69 } }, { 41, { 60, 65, 69 } },
			{ 48, { 60, 64, 67 } }, { 43, { 59, 62, 67 } }, { 41, { 60, 65, 69 } }, { 43, { 59, 62, 67 } } };
		const FChord NightProg[8] = { { 45, { 57, 60, 64 } }, { 40, { 55, 59, 64 } }, { 41, { 57, 60, 65 } }, { 38, { 57, 62, 65 } },
			{ 45, { 57, 60, 64 } }, { 40, { 55, 59, 64 } }, { 38, { 57, 62, 65 } }, { 40, { 56, 59, 64 } } };
		struct FNote { int32 Step; int32 Note; };
		const TArray<FNote>& DayMelody(int32 Bar)
		{
			static const TArray<FNote> Table[8] = {
				{ { 0, 76 }, { 3, 79 }, { 4, 76 }, { 8, 74 }, { 10, 72 }, { 12, 74 } },
				{ { 0, 74 }, { 3, 79 }, { 4, 74 }, { 8, 71 }, { 10, 74 }, { 12, 79 } },
				{ { 0, 72 }, { 3, 76 }, { 4, 81 }, { 8, 79 }, { 10, 76 }, { 12, 72 } },
				{ { 0, 81 }, { 3, 79 }, { 4, 76 }, { 8, 72 }, { 10, 76 }, { 12, 79 } },
				{ { 0, 76 }, { 3, 79 }, { 4, 76 }, { 8, 74 }, { 10, 72 }, { 12, 79 } },
				{ { 0, 79 }, { 3, 74 }, { 4, 79 }, { 8, 81 }, { 10, 79 }, { 12, 74 } },
				{ { 0, 76 }, { 3, 72 }, { 4, 76 }, { 8, 79 }, { 10, 81 }, { 12, 79 } },
				{ { 0, 74 }, { 2, 76 }, { 4, 79 }, { 6, 81 }, { 8, 79 }, { 12, 74 }, { 14, 71 } } };
			return Table[Bar];
		}
		const TArray<FNote>& NightMelody(int32 Bar)
		{
			static const TArray<FNote> Table[8] = {
				{ { 0, 69 }, { 8, 76 }, { 12, 72 } }, { { 0, 71 } }, { { 0, 72 }, { 6, 69 }, { 12, 65 } }, { { 0, 69 } },
				{ { 0, 76 }, { 4, 74 }, { 8, 72 }, { 12, 69 } }, { { 0, 67 } }, { { 0, 74 }, { 6, 77 }, { 12, 81 } }, { { 0, 68 }, { 8, 71 } } };
			return Table[Bar];
		}

		TArray<float> Blank() { TArray<float> P; P.Init(0.0f, Steps); return P; }
		void Put(TArray<float>& P, int32 Bar, int32 Step, float Value) { P[Bar * 16 + Step] = Value; }

		struct FClock
		{
			FSig Step;        // the 16th-note trigger
			FSig OnTrigger;
			FSig Count;
			FSig Index;       // Count mod 128 (1 .. 127, 0)
			FSig LoopStart;
		};

		struct FLaneVoice
		{
			FSig Trig;
			FSig Val;
			FSig Extra;       // the second array of the lane (the bass accent)
		};

		/** One pattern lane: the lane's array is read with the step counter and every non-zero entry becomes a note trigger. */
		TArray<FLaneVoice> Lane(FSynth& G, const FClock& Clk, const FString& Name, const TArray<float>& Pattern, int32 Voices, const TArray<float>* Extra = nullptr)
		{
			// the counter runs 1..128 and the index is Count mod 128, so element i of the array holds step i - 1
			auto Rotated = [](const TArray<float>& P)
				{
					TArray<float> R;
					R.SetNum(Steps);
					for (int32 i = 0; i < Steps; ++i)
					{
						R[i] = P[((i - 1) % Steps + Steps) % Steps];
					}
					return R;
				};
			const TArray<float> Main = Rotated(Pattern);
			const TArray<float> Second = Extra ? Rotated(*Extra) : TArray<float>();

			auto Read = [&](const FSig& Trigger, const TArray<float>& Array) -> FSig
				{
					const INode* Get = G.Make(TEXT("get_f"), { { TEXT("Trigger"), Trigger }, { TEXT("Index"), Clk.Index } });
					G.FeedArray(Get, TEXT("Array"), Array);
					return G.Out(Get, TEXT("Element"), ESig::Float);
				};

			TArray<FLaneVoice> Result;
			FLaneVoice First;
			First.Val = Read(Clk.OnTrigger, Main);
			if (Extra)
			{
				First.Extra = Read(Clk.OnTrigger, Second);
			}
			int32 Expected = 0;
			for (float Value : Pattern) { Expected += Value > 0.0f ? 1 : 0; }
			const FSig Hit = G.Out(G.Make(TEXT("cmp_f"), { { TEXT("Compare"), Clk.OnTrigger }, { TEXT("A"), First.Val }, { TEXT("B"), 0.0f }, { TEXT("Type"), 3 } }), TEXT("True"), ESig::Trigger);
			G.HitTap(Hit, Name, Expected);   // QA: how many times the lane fires
			if (Voices == 1)
			{
				First.Trig = Hit;
				Result.Add(First);
				return Result;
			}
			// several voices: the note-on triggers are dealt round-robin, and each voice reads its own value when its trigger fires, so an earlier
			// note keeps ringing at its own pitch while the next one starts on another voice
			const INode* Seq = G.Make(TEXT("seq"), { { TEXT("In"), Hit }, { TEXT("Loop"), true } }, Voices);
			for (int32 k = 0; k < Voices; ++k)
			{
				FLaneVoice V;
				V.Trig = G.Out(Seq, *FString::Printf(TEXT("Out %d"), k), ESig::Trigger);
				V.Val = Read(V.Trig, Main);
				Result.Add(V);
			}
			return Result;
		}

		/** A graph input of a part, as a signal. */
		FSig InputSig(FSynth& G, const TCHAR* Name, const TCHAR* DataType, ESig Type, const FLiteral& Default)
		{
			FSig Sig;
			if (const INode* Node = G.G.Input(Name, FName(DataType), Default))
			{
				Sig.Node = Node;
				Sig.Pin = FGraphBuilder::Source(Node).Name;
				Sig.Type = Type;
			}
			return Sig;
		}

		/** A graph output of a part, fed by a signal. */
		bool OutputSig(FSynth& G, const TCHAR* Name, const TCHAR* DataType, const FSig& Sig)
		{
			const INode* Out = G.G.Output(Name, FName(DataType));
			return Out && Sig.IsValid() && G.G.ConnectToOutput(FPin{ Sig.Node, Sig.Pin }, Out);
		}

		FSig Hz(FSynth& G, const FSig& Note) { return G.MidiHz(Note); }

		/**
		 * One voice of a layer. An always-on voice is built in the layer's own graph and runs while the layer runs. A gated voice has a graph of its own (built by the
		 * recipe, with a "Trig" and a "Val" input and an "Out" output): the music runs it only from a note-on until its sound has ended, so a voice that is silent
		 * between its notes (a pad between its chords, a hat between its hits) costs nothing then.
		 */
		struct FVoiceDef
		{
			FString Name;                 // taps and logs
			float Pan = 0.0f;
			float Gain = 1.0f;            // times the equal-power panner's compensation, applied in the layer's mix
			FSig Audio;                   // always-on voice: its sound in the layer's graph
			FSig Trig;                    // gated voice: its note-on trigger and its value (note, velocity) in the layer's graph
			FSig Val;
			TUniquePtr<FSynth> Own;       // gated voice: its own graph
			TUniquePtr<FGraphPart> Part;  // gated voice: the engine's operator of that graph, once built
			float ActiveSeconds = 0.0f;   // gated voice: how long after a note-on its output can be anything but silence (its envelope's length plus a margin)
			bool IsGated() const { return Own.IsValid(); }
		};

		void AddVoice(TArray<FVoiceDef>& Voices, const FString& Name, const FSig& Audio, float Pan, float Gain)
		{
			FVoiceDef V;
			V.Name = Name;
			V.Pan = Pan;
			V.Gain = Gain * PanComp;   // the equal-power panner is -3 dB at the centre; the original mix compensated, so does the layer mix
			V.Audio = Audio;
			Voices.Add(MoveTemp(V));
		}

		using FVoiceBuild = TFunction<FSig(FSynth&, const FSig&, const FSig&)>;

		/** A gated voice: Build makes its sound from its note trigger and value in a graph of its own. The noise seed is handed on, so the noise is what one graph would have had. */
		void AddGated(FSynth& G, TArray<FVoiceDef>& Voices, const FString& Name, const FLaneVoice& Lane, float Pan, float Gain, float ActiveSeconds, const FVoiceBuild& Build)
		{
			FVoiceDef V;
			V.Name = Name;
			V.Pan = Pan;
			V.Gain = Gain * PanComp;
			V.Trig = Lane.Trig;
			V.Val = Lane.Val;
			V.ActiveSeconds = ActiveSeconds;
			V.Own = MakeUnique<FSynth>(TEXT("ShiftMusicVoice"));
			V.Own->SetSeed(G.GetSeed());
			const FSig T = InputSig(*V.Own, TEXT("Trig"), TEXT("Trigger"), ESig::Trigger, FLiteral(false));
			const FSig X = InputSig(*V.Own, TEXT("Val"), TEXT("Float"), ESig::Float, FLiteral(0.0f));
			OutputSig(*V.Own, TEXT("Out"), TEXT("Audio"), Build(*V.Own, T, X));
			G.SetSeed(V.Own->GetSeed());
			Voices.Add(MoveTemp(V));
		}

		// How long each kind of voice can sound after its note-on: the longest envelope in it (the exponential envelopes end at 5 time constants), the pad's hold
		// and release, and a margin of 50 to 150 ms. Every voice's output is an envelope times something, so it is silent once that has ended. The meter run checks
		// these numbers against the real sound (see FShiftMusicRunner, "shadow gating").
		constexpr float PadHoldSeconds = 15.0f * (60.0f / 112.5f / 4.0f);

		float PadActive(float Release) { return PadHoldSeconds + Release + 0.15f; }
		float HatActive(float Decay) { return 5.0f * Decay + 0.0005f + 0.1f; }
		constexpr float SnareActive = 5.0f * 0.07f + 0.15f;     // the noise's envelope, 0.35 s; the tone's is shorter
		constexpr float TomActive = 5.0f * 0.18f + 0.15f;       // the body's envelope, 0.9 s
		constexpr float RimActive = 5.0f * 0.012f + 0.15f;      // 0.06 s

		void DayLayer(FSynth& G, const FClock& Clk, TArray<FVoiceDef>& Voices)
		{
			// pad: one chord per bar, 3 tones, 2 voices each so a bar's tail rings into the next bar
			for (int32 N = 0; N < 3; ++N)
			{
				TArray<float> Pat = Blank();
				for (int32 B = 0; B < Bars; ++B) { Put(Pat, B, 0, static_cast<float>(DayProg[B].Tones[N])); }
				TArray<FLaneVoice> Lanes = Lane(G, Clk, FString::Printf(TEXT("DayPad%d"), N), Pat, 2);
				for (int32 K = 0; K < 2; ++K)
				{
					AddGated(G, Voices, FString::Printf(TEXT("DayPad%d_%d"), N, K), Lanes[K], (N - 1) * 0.5f, 0.075f, PadActive(0.6f),
						[](FSynth& VG, const FSig& T, const FSig& V)
						{
							const FSig Rel = VG.DelayTrigger(T, PadHoldSeconds);
							return Pad(VG, T, Rel, FArg(Hz(VG, V)), 1300.0f, 0.35f, 0.6f);
						});
				}
			}
			// offbeat stabs (chord tones an octave up)
			for (int32 N = 0; N < 3; ++N)
			{
				TArray<float> Pat = Blank();
				for (int32 B = 0; B < Bars; ++B)
				{
					const int32 StepsOf[3] = { 6, 14, 10 };
					const int32 Count = (B % 2 == 1) ? 3 : 2;
					for (int32 I = 0; I < Count; ++I) { Put(Pat, B, StepsOf[I], static_cast<float>(DayProg[B].Tones[N] + 12)); }
				}
				const FLaneVoice V = Lane(G, Clk, FString::Printf(TEXT("DayStab%d"), N), Pat, 1)[0];
				AddVoice(Voices, FString::Printf(TEXT("DayStab%d"), N), Stab(G, V.Trig, FArg(Hz(G, V.Val))), (N - 1) * 0.45f, 0.085f);
			}
			// bass: four hits per bar
			{
				TArray<float> Pat = Blank();
				for (int32 B = 0; B < Bars; ++B)
				{
					const int32 Bass = DayProg[B].Bass;
					Put(Pat, B, 0, Bass); Put(Pat, B, 6, Bass); Put(Pat, B, 10, Bass + 7); Put(Pat, B, 14, Bass + 12);
				}
				const FLaneVoice V = Lane(G, Clk, TEXT("DayBass"), Pat, 1)[0];
				AddVoice(Voices, TEXT("DayBass"), BassDay(G, V.Trig, FArg(Hz(G, V.Val))), 0.0f, 0.27f);
			}
			// marimba melody, two voices
			{
				TArray<float> Pat = Blank();
				for (int32 B = 0; B < Bars; ++B) { for (const FNote& Nt : DayMelody(B)) { Put(Pat, B, Nt.Step, static_cast<float>(Nt.Note)); } }
				TArray<FLaneVoice> Lanes = Lane(G, Clk, TEXT("DayMelody"), Pat, 2);
				for (int32 K = 0; K < 2; ++K)
				{
					AddVoice(Voices, FString::Printf(TEXT("DayMelody_%d"), K), Marimba(G, Lanes[K].Trig, FArg(Hz(G, Lanes[K].Val)), 0.92f), 0.25f, 0.26f);
				}
			}
			// drums: the pattern values are velocity x the original lane gain
			{
				TArray<float> Pat = Blank();
				for (int32 B = 0; B < Bars; ++B)
				{
					Put(Pat, B, 0, 0.9f * 0.5f); Put(Pat, B, 8, 0.8f * 0.45f);
					if (B % 2 == 1) { Put(Pat, B, 10, 0.6f * 0.36f); }
				}
				const FLaneVoice V = Lane(G, Clk, TEXT("DayKick"), Pat, 1)[0];
				AddVoice(Voices, TEXT("DayKick"), Kick(G, V.Trig, FArg(V.Val), 0.25f, 46.0f), 0.0f, 1.0f);
			}
			{
				TArray<float> Pat = Blank();
				for (int32 B = 0; B < Bars; ++B) { Put(Pat, B, 4, 0.9f * 0.20f); Put(Pat, B, 12, 0.95f * 0.22f); }
				const FLaneVoice V = Lane(G, Clk, TEXT("DaySnare"), Pat, 1)[0];
				AddGated(G, Voices, TEXT("DaySnare"), V, 0.1f, 1.0f, SnareActive, [](FSynth& VG, const FSig& T, const FSig& X) { return Snare(VG, T, FArg(X)); });
			}
			{
				TArray<float> Pat = Blank();
				for (int32 B = 0; B < Bars; ++B) { for (int32 S = 0; S < 16; S += 2) { Put(Pat, B, S, (S % 4 == 0 ? 0.9f : 0.55f) * 0.085f); } }
				const FLaneVoice V = Lane(G, Clk, TEXT("DayHat"), Pat, 1)[0];
				AddGated(G, Voices, TEXT("DayHat"), V, -0.35f, 1.0f, HatActive(0.025f), [](FSynth& VG, const FSig& T, const FSig& X) { return Hat(VG, T, FArg(X)); });
			}
		}

		void NightLayer(FSynth& G, const FClock& Clk, TArray<FVoiceDef>& Voices)
		{
			for (int32 N = 0; N < 3; ++N)
			{
				for (int32 Variant = 0; Variant < 2; ++Variant)
				{
					const int32 Octave = Variant == 0 ? 0 : 12;
					const float Gain = Variant == 0 ? 0.15f : 0.05f;
					const float PanAmount = Variant == 0 ? (N - 1) * 0.6f : (1 - N) * 0.6f;
					const float Cutoff = Variant == 0 ? 650.0f : 900.0f;
					const float Attack = Variant == 0 ? 0.9f : 1.2f;
					TArray<float> Pat = Blank();
					for (int32 B = 0; B < Bars; ++B) { Put(Pat, B, 0, static_cast<float>(NightProg[B].Tones[N] + Octave)); }
					const FString PadName = FString::Printf(TEXT("Night%s%d"), Variant == 0 ? TEXT("Low") : TEXT("High"), N);
					TArray<FLaneVoice> Lanes = Lane(G, Clk, PadName, Pat, 2);
					for (int32 K = 0; K < 2; ++K)
					{
						AddGated(G, Voices, FString::Printf(TEXT("%s_%d"), *PadName, K), Lanes[K], PanAmount, Gain, PadActive(1.0f),
							[Cutoff, Attack](FSynth& VG, const FSig& T, const FSig& V)
							{
								const FSig Rel = VG.DelayTrigger(T, PadHoldSeconds);
								return Pad(VG, T, Rel, FArg(Hz(VG, V)), Cutoff, Attack, 1.0f);
							});
					}
				}
			}
			// pulsing bass: eighth notes with an accent pattern driving level and filter
			{
				TArray<float> Pat = Blank();
				TArray<float> Accent = Blank();
				for (int32 B = 0; B < Bars; ++B)
				{
					for (int32 S = 0; S < 16; S += 2)
					{
						const float A = S % 8 == 0 ? 1.0f : (S % 4 == 0 ? 0.7f : 0.45f);
						Put(Pat, B, S, static_cast<float>(NightProg[B].Bass));
						Put(Accent, B, S, A);
					}
				}
				const FLaneVoice V = Lane(G, Clk, TEXT("NightBass"), Pat, 1, &Accent)[0];
				const FSig Cutoff = G.Add(G.FScale(V.Extra, 90.0f), 260.0f);
				const FSig Vel = G.Add(G.FScale(V.Extra, 0.26f), 0.05f);
				AddVoice(Voices, TEXT("NightBass"), BassNight(G, V.Trig, FArg(Hz(G, V.Val)), FArg(Cutoff), FArg(Vel)), 0.0f, 1.0f);
			}
			// bell melody
			{
				TArray<float> Pat = Blank();
				for (int32 B = 0; B < Bars; ++B) { for (const FNote& Nt : NightMelody(B)) { Put(Pat, B, Nt.Step, static_cast<float>(Nt.Note)); } }
				TArray<FLaneVoice> Lanes = Lane(G, Clk, TEXT("NightBell"), Pat, 2);
				for (int32 K = 0; K < 2; ++K)
				{
					AddVoice(Voices, FString::Printf(TEXT("NightBell_%d"), K), Bell(G, Lanes[K].Trig, FArg(Hz(G, Lanes[K].Val)), 0.8f), -0.1f, 0.17f);
				}
			}
			// heartbeat kick on every beat, shaker on the offbeats, toms every fourth bar
			{
				TArray<float> Pat = Blank();
				for (int32 B = 0; B < Bars; ++B) { for (int32 S = 0; S < 16; S += 4) { Put(Pat, B, S, S % 8 == 0 ? 0.9f * 0.42f : 0.6f * 0.28f); } }
				const FLaneVoice V = Lane(G, Clk, TEXT("NightKick"), Pat, 1)[0];
				AddVoice(Voices, TEXT("NightKick"), Kick(G, V.Trig, FArg(V.Val), 0.05f, 42.0f), 0.0f, 1.0f);
			}
			{
				TArray<float> Pat = Blank();
				for (int32 B = 0; B < Bars; ++B) { for (int32 S = 2; S < 16; S += 4) { Put(Pat, B, S, 0.4f * 0.045f); } }
				const FLaneVoice V = Lane(G, Clk, TEXT("NightShaker"), Pat, 1)[0];
				AddGated(G, Voices, TEXT("NightShaker"), V, 0.3f, 1.0f, HatActive(0.025f), [](FSynth& VG, const FSig& T, const FSig& X) { return Hat(VG, T, FArg(X)); });
			}
			{
				TArray<float> Pat = Blank();
				for (int32 B = 0; B < Bars; ++B) { if (B % 4 == 3) { Put(Pat, B, 10, 90.0f); Put(Pat, B, 13, 75.0f); } }
				const FLaneVoice V = Lane(G, Clk, TEXT("NightTom"), Pat, 1)[0];
				AddGated(G, Voices, TEXT("NightTom"), V, 0.0f, 1.0f, TomActive, [](FSynth& VG, const FSig& T, const FSig& X) { return Tom(VG, T, FArg(X), 0.8f * 0.30f); });
			}
		}

		void TensionLayer(FSynth& G, const FClock& Clk, TArray<FVoiceDef>& Voices)
		{
			const float Pattern4[4] = { 1.0f, 0.35f, 0.7f, 0.35f };
			for (int32 Parity = 0; Parity < 2; ++Parity)
			{
				TArray<float> Pat = Blank();
				for (int32 B = 0; B < Bars; ++B) { for (int32 S = Parity; S < 16; S += 2) { Put(Pat, B, S, Pattern4[S % 4] * (0.55f + 0.045f * B) * 0.16f); } }
				const FString Name = FString::Printf(TEXT("TensionHat%d"), Parity);
				const FLaneVoice V = Lane(G, Clk, Name, Pat, 1)[0];
				AddGated(G, Voices, Name, V, Parity == 0 ? -0.25f : 0.25f, 1.0f, HatActive(0.025f), [](FSynth& VG, const FSig& T, const FSig& X) { return Hat(VG, T, FArg(X)); });
			}
			{
				TArray<float> Pat = Blank();
				for (int32 B = 0; B < Bars; ++B) { Put(Pat, B, 14, 0.8f * 0.10f); }
				const FLaneVoice V = Lane(G, Clk, TEXT("TensionOpenHat"), Pat, 1)[0];
				AddGated(G, Voices, TEXT("TensionOpenHat"), V, 0.0f, 1.0f, HatActive(0.12f), [](FSynth& VG, const FSig& T, const FSig& X) { return Hat(VG, T, FArg(X), 0.12f); });
			}
			const int32 RimSteps[2] = { 3, 11 };
			const float RimVel[2] = { 0.7f * 0.20f, 0.6f * 0.18f };
			const float RimPan[2] = { 0.35f, -0.35f };
			for (int32 I = 0; I < 2; ++I)
			{
				TArray<float> Pat = Blank();
				for (int32 B = 0; B < Bars; ++B) { Put(Pat, B, RimSteps[I], RimVel[I]); }
				const FString RimName = FString::Printf(TEXT("TensionRim%d"), RimSteps[I]);
				const FLaneVoice V = Lane(G, Clk, RimName, Pat, 1)[0];
				AddGated(G, Voices, RimName, V, RimPan[I], 1.0f, RimActive, [](FSynth& VG, const FSig& T, const FSig& X) { return Rim(VG, T, FArg(X)); });
			}
			{
				TArray<float> Pat = Blank();
				for (int32 S = 0; S < 16; ++S) { Put(Pat, Bars - 1, S, (0.25f + 0.65f * S / 15.0f) * 0.10f); }
				const FLaneVoice V = Lane(G, Clk, TEXT("TensionRoll"), Pat, 1)[0];
				AddGated(G, Voices, TEXT("TensionRoll"), V, 0.0f, 1.0f, SnareActive, [](FSynth& VG, const FSig& T, const FSig& X) { return Snare(VG, T, FArg(X)); });
			}
			// riser across the loop: noise through a band-pass sweeping 300 Hz -> 6 kHz, amplitude 0.04 + 0.55 * u^2, restarted every loop
			{
				const FSig U = G.Ad(Clk.LoopStart, LoopSeconds, 0.001f, 1.0f, 1.0f, false);
				const FSig Cutoff = G.LogSweep(U, 300.0f, 6000.0f);
				const FSig Band = G.Svf(G.Noise(), FArg(Cutoff), 2.0f).Bp;
				const FSig Swell = G.Ad(Clk.LoopStart, LoopSeconds, 0.001f, 2.0f, 1.0f, true);
				const FSig Riser = G.Mix({ Part(Band, 0.04f), Part(G.Mul(Band, Swell), 0.55f) });
				AddVoice(Voices, TEXT("TensionRiser"), Riser, 0.0f, 0.30f);
			}
		}
	}

#if !UE_BUILD_SHIPPING
	/** The micro-benchmark spec for this generator (QA: -ShiftMusicBench with the meter's -ShiftAudioCpu), or empty. */
	inline FString BenchSpecForThisGenerator()
	{
		FString List;
		const int32 Index = FShiftAudioStats::Get().BenchIndex.load();
		if (Index < 0 || !FParse::Value(FCommandLine::Get(), TEXT("ShiftMusicBench="), List, false))
		{
			return FString();
		}
		TArray<FString> Specs;
		List.ParseIntoArray(Specs, TEXT(","));
		return Specs.IsValidIndex(Index) ? Specs[Index] : FString();
	}

	/**
	 * QA micro-benchmark: <count> copies of one kind of engine node in place of the day layer, so the CPU cost of that kind can be read off the generator's
	 * render timing (the meter's -ShiftAudioCpu). Spec = <kind>:<count>[:loop]; the triggers are the 16th notes, or the loop start (rarely) with ":loop".
	 * "mix" mixes the shared sine <count> times (the common part to subtract), "mul" multiplies two shared sines, the rest add their own nodes on top.
	 */
	inline void BenchLayer(FSynth& G, const FClock& Clk, TArray<FVoiceDef>& Voices, const FString& Spec)
	{
		TArray<FString> Parts;
		Spec.ParseIntoArray(Parts, TEXT(":"));
		const FString Kind = Parts.IsValidIndex(0) ? Parts[0] : FString(TEXT("mix"));
		const int32 Count = Parts.IsValidIndex(1) ? FMath::Max(1, FCString::Atoi(*Parts[1])) : 1;
		const FSig Trig = (Parts.IsValidIndex(2) && Parts[2] == TEXT("loop")) ? Clk.LoopStart : Clk.OnTrigger;
		const FSig Shared = G.Osc(TEXT("sine"), 220.0f);
		const FSig Shared2 = G.Osc(TEXT("sine"), 331.0f);
		const FSig NoiseSource = G.Noise();
		TArray<FMono> Items;
		for (int32 k = 0; k < Count; ++k)
		{
			const float F = 110.0f + 13.0f * k;
			FSig S = Shared;
			if (Kind == TEXT("sine") || Kind == TEXT("saw") || Kind == TEXT("tri") || Kind == TEXT("square")) { S = G.Osc(*Kind, F); }
			else if (Kind == TEXT("sync")) { S = G.Osc(TEXT("sine"), F, FArg(), Trig); }
			else if (Kind == TEXT("noise")) { S = G.Noise(); }
			else if (Kind == TEXT("mul")) { S = G.Mul(Shared, FArg(Shared2)); }
			else if (Kind == TEXT("mulf")) { S = G.Mul(Shared, FArg(0.5f)); }
			else if (Kind == TEXT("env")) { S = G.Mul(Shared, FArg(G.ExpEnv(Trig, 0.2f))); }
			else if (Kind == TEXT("envf")) { S = G.Mul(Shared, FArg(G.ExpEnv(Trig, 0.2f, 0.002f, false))); }
			else if (Kind == TEXT("adsr")) { S = G.Mul(Shared, FArg(G.Adsr(Trig, Trig, 0.01f, 0.05f, 1.0f, 0.5f))); }
			else if (Kind == TEXT("ladder")) { S = G.Ladder(NoiseSource, F * 4.0f, 1.0f); }
			else if (Kind == TEXT("svf")) { S = G.Svf(NoiseSource, F * 4.0f, 0.7f).Hp; }
			else if (Kind == TEXT("pan")) { S = G.Pan(Shared, 0.3f).Left; }
			else if (Kind == TEXT("hz")) { S = G.Mul(Shared, FArg(G.MidiHz(FArg(60.0f + k)))); }
			else if (Kind == TEXT("reverb")) { S = G.Reverb(Shared, Shared2, 0.16f, 1.0f, 0.8f, 0.45f).Left; }
			Items.Add(Part(S, 0.01f));
		}
		AddVoice(Voices, TEXT("Bench"), G.Mix(Items), 0.0f, 1.0f);
	}
#endif

	/** How long a layer keeps rendering after its gain reached 0: the longest tail of a voice (pad release, reverb is downstream and shared) is under 3 s. */
	constexpr float LayerTailSeconds = 4.0f;

	/** A gated voice while the music runs. */
	struct FGatedRun
	{
		FGatedRun(TUniquePtr<FGraphPart> InPart, const Metasound::FTriggerReadRef& InTrig, const Metasound::TDataWriteReference<float>& InGain, const Metasound::FAudioBufferReadRef& InAudio,
			float InBaseGain, int32 InActiveBlocks, FShiftAudioStats::FTap* InTail)
			: Part(MoveTemp(InPart)), Trig(InTrig), Gain(InGain), Audio(InAudio), BaseGain(InBaseGain), ActiveBlocks(InActiveBlocks), Tail(InTail)
		{
		}

		TUniquePtr<FGraphPart> Part;
		Metasound::FTriggerReadRef Trig;                    // the voice's note-on trigger, produced by its layer's front part
		Metasound::TDataWriteReference<float> Gain;         // the layer mix's gain input for this voice: BaseGain while it runs, 0 while it does not
		Metasound::FAudioBufferReadRef Audio;               // its output, read by the shadow check
		float BaseGain = 1.0f;
		int32 ActiveBlocks = 0;                             // blocks it runs from a note-on
		int32 Left = 0;                                     // blocks left in the current window
		bool bRan = false;
		FShiftAudioStats::FTap* Tail = nullptr;             // QA: the loudest sound it made outside its window (shadow gating only)
	};

	/** One layer while the music runs: its front part (the lanes and the always-on voices), its gated voices, and its mix (pans and the layer's mixer). */
	struct FLayerRun
	{
		FLayerRun(TUniquePtr<FGraphPart> InFront, TUniquePtr<FGraphPart> InMix, const Metasound::FFloatReadRef& InGain, const Metasound::TDataWriteReference<float>& InGated, int32 InQuiet,
			TArray<FGatedRun>&& InVoices)
			: Front(MoveTemp(InFront)), Mix(MoveTemp(InMix)), Gain(InGain), Gated(InGated), Voices(MoveTemp(InVoices)), Quiet(InQuiet)
		{
		}

		TUniquePtr<FGraphPart> Front;
		TUniquePtr<FGraphPart> Mix;
		Metasound::FFloatReadRef Gain;
		Metasound::TDataWriteReference<float> Gated;
		TArray<FGatedRun> Voices;
		int32 Quiet = 0;          // blocks the layer has been silent; it starts out as silent as it can be, so a layer nobody asks for never renders
		bool bRan = false;
	};

	/**
	 * The operator of the music. Every block it runs the clock part, then each layer that is audible (its gain is above silence, or has been for less than
	 * LayerTailSeconds so its voices ring out): the layer's front part, each gated voice that is inside its window (from its note-on until its sound has ended),
	 * and the layer's mix; then the final mix. A layer or a voice that is skipped gives its mixer a gain of 0, so whatever its output buffer still holds is not
	 * heard. All parts are the engine's own graph operators; only the decision which of them executes is made here.
	 *
	 * Shadow gating (QA, the meter's long music case): every gated voice runs all the time and keeps its gain, while the window is still worked out; the sound a
	 * voice makes outside its window is measured (a tap per voice), which proves the window of each voice is long enough.
	 */
	class FShiftMusicRunner : public Metasound::IOperator
	{
	public:
		FShiftMusicRunner(TUniquePtr<FGraphPart> InClock, TArray<FLayerRun>&& InLayers, TUniquePtr<FGraphPart> InFinal, const Metasound::FTriggerReadRef& InPlay,
			const Metasound::FFloatReadRef& InVolume, int32 InQuietBlocks, bool bInShadow)
			: Clock(MoveTemp(InClock)), Final(MoveTemp(InFinal)), Layers(MoveTemp(InLayers)), Play(InPlay), Volume(InVolume), QuietBlocks(InQuietBlocks), bShadow(bInShadow)
		{
		}

		virtual void BindInputs(Metasound::FInputVertexInterfaceData& InVertexData) override;
		virtual void BindOutputs(Metasound::FOutputVertexInterfaceData& InVertexData) override;

		virtual FResetFunction GetResetFunction() override
		{
			return [](IOperator* Op, const FResetParams& Params) { static_cast<FShiftMusicRunner*>(Op)->Reset(Params); };
		}
		virtual FExecuteFunction GetExecuteFunction() override
		{
			return [](IOperator* Op) { static_cast<FShiftMusicRunner*>(Op)->Run(); };
		}
		virtual FPostExecuteFunction GetPostExecuteFunction() override
		{
			return [](IOperator* Op) { static_cast<FShiftMusicRunner*>(Op)->PostRun(); };
		}

	private:
		void Run();
		void PostRun();
		void Reset(const FResetParams& Params);

		TUniquePtr<FGraphPart> Clock;
		TUniquePtr<FGraphPart> Final;
		TArray<FLayerRun> Layers;
		Metasound::FTriggerReadRef Play;
		Metasound::FFloatReadRef Volume;
		int32 QuietBlocks = 0;
		bool bShadow = false;
	};
}

namespace ShiftAudio
{
	void FShiftMusicRunner::Run()
	{
		Clock->Execute();
		for (FLayerRun& L : Layers)
		{
			const float Gain = *L.Gain;
			L.bRan = Gain > 1.0e-4f;
			if (L.bRan)
			{
				L.Quiet = 0;
			}
			else if (L.Quiet < QuietBlocks)
			{
				++L.Quiet;
				L.bRan = true;   // silent only for a moment: its last notes ring out
			}
			*L.Gated = L.bRan ? Gain : 0.0f;
			if (!L.bRan)
			{
				for (FGatedRun& V : L.Voices)
				{
					V.Left = 0;
					V.bRan = false;
				}
				continue;
			}
			L.Front->Execute();
			for (FGatedRun& V : L.Voices)
			{
				if (V.Trig->IsTriggeredInBlock())
				{
					V.Left = V.ActiveBlocks;
				}
				const bool bInWindow = V.Left > 0;
				if (bInWindow)
				{
					--V.Left;
				}
				V.bRan = bInWindow || bShadow;
				*V.Gain = V.bRan ? V.BaseGain : 0.0f;
				if (V.bRan)
				{
					V.Part->Execute();
				}
				if (bShadow && !bInWindow && V.Tail)
				{
					float Peak = 0.0f;
					const float* Samples = V.Audio->GetData();
					for (int32 i = 0; i < V.Audio->Num(); ++i)
					{
						Peak = FMath::Max(Peak, FMath::Abs(Samples[i]));
					}
					if (Peak > V.Tail->Level.load(std::memory_order_relaxed))
					{
						V.Tail->Level.store(Peak, std::memory_order_relaxed);
					}
				}
			}
			L.Mix->Execute();
		}
		Final->Execute();
	}

	void FShiftMusicRunner::PostRun()
	{
		Clock->PostExecute();
		for (FLayerRun& L : Layers)
		{
			if (!L.bRan)
			{
				continue;
			}
			L.Front->PostExecute();
			for (FGatedRun& V : L.Voices)
			{
				if (V.bRan)
				{
					V.Part->PostExecute();
				}
			}
			L.Mix->PostExecute();
		}
		Final->PostExecute();
	}

	void FShiftMusicRunner::Reset(const FResetParams& Params)
	{
		Clock->Reset(Params);
		for (FLayerRun& L : Layers)
		{
			L.Front->Reset(Params);
			for (FGatedRun& V : L.Voices)
			{
				V.Part->Reset(Params);
				V.Left = 0;
				V.bRan = false;
				*V.Gain = 0.0f;
			}
			L.Mix->Reset(Params);
			L.Quiet = QuietBlocks;
			L.bRan = false;
			*L.Gated = 0.0f;
		}
		Final->Reset(Params);
	}
}

namespace Metasound
{
	namespace ShiftMusicVertexNames
	{
		METASOUND_PARAM(InputPlay, "Play", "Starts the music.")
			METASOUND_PARAM(InputDayGain, "DayGain", "Level of the day layer.")
			METASOUND_PARAM(InputNightGain, "NightGain", "Level of the night layer.")
			METASOUND_PARAM(InputTensionGain, "TensionGain", "Level of the tension layer.")
			METASOUND_PARAM(InputVolume, "Volume", "Master volume.")
			METASOUND_PARAM(InputDayTrim, "Day Trim dB", "Level trim of the day layer (Tools/py/ms_trims.json).")
			METASOUND_PARAM(InputNightTrim, "Night Trim dB", "Level trim of the night layer.")
			METASOUND_PARAM(InputTensionTrim, "Tension Trim dB", "Level trim of the tension layer.")
			METASOUND_PARAM(OutputLeft, "Left", "Left channel.")
			METASOUND_PARAM(OutputRight, "Right", "Right channel.")
			METASOUND_PARAM(OutputClock, "MIDI Clock", "The Harmonix MIDI clock of the music.")
	}

	struct FShiftMusicOperator : public ShiftAudio::FCompositeOperatorBase
	{
		static const FVertexInterface& GetVertexInterface()
		{
			using namespace ShiftMusicVertexNames;
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertex<FTrigger>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputPlay)),
					TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputDayGain), 1.0f),
					TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputNightGain), 0.0f),
					TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputTensionGain), 0.0f),
					TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputVolume), 1.0f),
					TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputDayTrim), 0.0f),
					TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputNightTrim), 0.0f),
					TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputTensionTrim), 0.0f)
				),
				FOutputVertexInterface(
					TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputLeft)),
					TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputRight)),
					TOutputDataVertex<HarmonixMetasound::FMidiClock>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputClock))
				)
			);
			return Interface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto InitNodeInfo = []() -> FNodeClassMetadata
				{
					FNodeClassMetadata Info;
					Info.ClassName = { TEXT("Shift"), TEXT("Music"), TEXT("") };
					Info.MajorVersion = 1;
					Info.MinorVersion = 0;
					Info.DisplayName = METASOUND_LOCTEXT("Shift_MusicDisplayName", "Shift Music");
					Info.Description = METASOUND_LOCTEXT("Shift_MusicDescription", "The looping game music: Harmonix clock, pattern lanes, voices, day / night / tension layers, reverb and limiter, as one node.");
					Info.Author = PluginAuthor;
					Info.PromptIfMissing = PluginNodeMissingPrompt;
					Info.DefaultInterface = GetVertexInterface();
					Info.CategoryHierarchy.Emplace(METASOUND_LOCTEXT("Shift_NodeCategory", "Shift Happens"));
					return Info;
				};
			static const FNodeClassMetadata Info = InitNodeInfo();
			return Info;
		}

		static TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults)
		{
			using namespace ShiftAudio;
			using namespace ShiftMusicVertexNames;
			const FOperatorSettings& Settings = InParams.OperatorSettings;
			const float DayTrim = *InParams.InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InputDayTrim), Settings);
			const float NightTrim = *InParams.InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InputNightTrim), Settings);
			const float TensionTrim = *InParams.InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InputTensionTrim), Settings);
			FTriggerReadRef Play = InParams.InputData.GetOrCreateDefaultDataReadReference<FTrigger>(METASOUND_GET_PARAM_NAME(InputPlay), Settings);
			FFloatReadRef DayGain = InParams.InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InputDayGain), Settings);
			FFloatReadRef NightGain = InParams.InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InputNightGain), Settings);
			FFloatReadRef TensionGain = InParams.InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InputTensionGain), Settings);
			FFloatReadRef Volume = InParams.InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InputVolume), Settings);
			const float BlockRate = Settings.GetActualBlockRate();
			const int32 QuietBlocks = FMath::CeilToInt(ShiftAudio::LayerTailSeconds * BlockRate);
			const bool bShadow = FShiftAudioStats::TapsWanted() && FShiftAudioStats::Get().bShadowGating.load();

			// ---- the clock part (always runs)
			FSynth GClock(TEXT("ShiftMusic"));
			const FSig PlaySig = InputSig(GClock, TEXT("Play"), TEXT("Trigger"), ESig::Trigger, FLiteral(false));
			const INode* ClockOut = GClock.G.Output(TEXT("MIDI Clock"), FName("MIDIClock"));
			if (!PlaySig.IsValid() || !ClockOut)
			{
				return nullptr;
			}
			const FSig Transport = GClock.Out(GClock.Make(TEXT("to_transport"), { { TEXT("Play"), PlaySig } }), TEXT("Transport"), ESig::Float);
			const INode* Metronome = GClock.Make(TEXT("metronome"), { { TEXT("Transport"), Transport }, { TEXT("Simple Loop"), true }, { TEXT("Loop Length (Bars)"), Bars },
				{ TEXT("Time Sig. Numerator"), 4 }, { TEXT("Time Sig. Denominator"), 4 }, { TEXT("Tempo"), Tempo } });
			const FSig MidiClock = GClock.Out(Metronome, TEXT("MIDI Clock"), ESig::Float);
			GClock.G.ConnectToOutput(FPin{ MidiClock.Node, MidiClock.Pin }, ClockOut);
			const FSig Step = GClock.Out(GClock.Make(TEXT("subdiv"), { { TEXT("MIDI Clock"), MidiClock }, { TEXT("Base Grid Size"), 3 } }), TEXT("Trigger Out"), ESig::Trigger);
			const INode* Counter = GClock.Make(TEXT("counter"), { { TEXT("In"), Step }, { TEXT("Reset Count"), Steps } });
			const FSig Count = GClock.Out(Counter, TEXT("Count"), ESig::Int32);
			const FSig OnTrigger = GClock.Out(Counter, TEXT("On Trigger"), ESig::Trigger);
			const FSig Index = GClock.Out(GClock.Make(TEXT("modulo"), { { TEXT("PrimaryOperand"), Count }, { TEXT("AdditionalOperands"), Steps } }), TEXT("Out"), ESig::Int32);
			const FSig LoopStart = GClock.Out(GClock.Make(TEXT("cmp_i"), { { TEXT("Compare"), OnTrigger }, { TEXT("A"), Count }, { TEXT("B"), 1 }, { TEXT("Type"), 0 } }), TEXT("True"), ESig::Trigger);
			GClock.StepTap(Count);   // QA: the step counter
			if (!OutputSig(GClock, TEXT("OnTrigger"), TEXT("Trigger"), OnTrigger) || !OutputSig(GClock, TEXT("Index"), TEXT("Int32"), Index) || !OutputSig(GClock, TEXT("LoopStart"), TEXT("Trigger"), LoopStart))
			{
				return nullptr;
			}
			TUniquePtr<FGraphPart> ClockPart = MakeUnique<FGraphPart>();
			if (!ClockPart->Build(GClock.G.Release(), { { FVertexName(TEXT("Play")), FAnyDataReference(Play) } }, InParams, OutResults))
			{
				return nullptr;
			}

			// ---- the layers: each has a front part (the lanes and the voices that always run, the note triggers of the gated voices), a graph part for every gated voice, and
			// a mix part (the pans, the voice gains and the layer's mixer). They hand the noise seed on, so the noise is what one graph would have had.
			struct FLayerBuild
			{
				explicit FLayerBuild(const TCHAR* Name) : Synth(Name) {}
				FSynth Synth;
				TArray<FVoiceDef> Voices;
			};
			using FRecipe = void (*)(FSynth&, const FClock&, TArray<FVoiceDef>&);
			const FRecipe Recipes[3] = { DayLayer, NightLayer, TensionLayer };
			const FFloatReadRef* const LayerGains[3] = { &DayGain, &NightGain, &TensionGain };
			TArray<FLayerRun> Runs;
			int32 Seed = 0;
			for (int32 L = 0; L < 3; ++L)
			{
				FLayerBuild Build(TEXT("ShiftMusic"));
				FSynth& G = Build.Synth;
				if (L > 0)
				{
					G.SetSeed(Seed);
				}
				FClock Clk;
				Clk.OnTrigger = InputSig(G, TEXT("OnTrigger"), TEXT("Trigger"), ESig::Trigger, FLiteral(false));
				Clk.Index = InputSig(G, TEXT("Index"), TEXT("Int32"), ESig::Int32, FLiteral(0));
				Clk.LoopStart = InputSig(G, TEXT("LoopStart"), TEXT("Trigger"), ESig::Trigger, FLiteral(false));
				if (!Clk.OnTrigger.IsValid() || !Clk.Index.IsValid() || !Clk.LoopStart.IsValid())
				{
					return nullptr;
				}
#if !UE_BUILD_SHIPPING
				const FString BenchSpec = L == 0 ? BenchSpecForThisGenerator() : FString();
				if (!BenchSpec.IsEmpty())
				{
					BenchLayer(G, Clk, Build.Voices, BenchSpec);
				}
				else
#endif
				{
					Recipes[L](G, Clk, Build.Voices);
				}
				Seed = G.GetSeed();

				// what the front part hands on: a gated voice's note trigger and value, an always-on voice's sound
				for (int32 i = 0; i < Build.Voices.Num(); ++i)
				{
					const FVoiceDef& V = Build.Voices[i];
					const bool bOk = V.IsGated()
						? OutputSig(G, *FString::Printf(TEXT("T%d"), i), TEXT("Trigger"), V.Trig) && OutputSig(G, *FString::Printf(TEXT("V%d"), i), TEXT("Float"), V.Val)
						: OutputSig(G, *FString::Printf(TEXT("A%d"), i), TEXT("Audio"), V.Audio);
					if (!bOk)
					{
						return nullptr;
					}
				}
				TUniquePtr<FGraphPart> Front = MakeUnique<FGraphPart>();
				if (!Front->Build(G.G.Release(), {
					{ FVertexName(TEXT("OnTrigger")), *ClockPart->Output(TEXT("OnTrigger")) },
					{ FVertexName(TEXT("Index")), *ClockPart->Output(TEXT("Index")) },
					{ FVertexName(TEXT("LoopStart")), *ClockPart->Output(TEXT("LoopStart")) } }, InParams, OutResults))
				{
					return nullptr;
				}

				// the voices' graph parts and the layer's mix part
				FSynth GMix(TEXT("ShiftMusicMix"));
				TArray<FStereoItem> Items;
				TArray<FLink> Links;
				TArray<FGatedRun> Gated;
				for (int32 i = 0; i < Build.Voices.Num(); ++i)
				{
					FVoiceDef& V = Build.Voices[i];
					const FString InName = FString::Printf(TEXT("V%d"), i);
					const FSig Audio = InputSig(GMix, *InName, TEXT("Audio"), ESig::Audio, FLiteral(0.0f));
					if (!Audio.IsValid())
					{
						return nullptr;
					}
					GMix.LevelTap(Audio, V.Name);   // QA: the voice's peak before panning and gain
					FStereoItem Item;
					Item.Audio = GMix.Pan(Audio, V.Pan);
					Item.Gain = V.Gain;
					if (!V.IsGated())
					{
						Links.Add(FLink{ FVertexName(*InName), *Front->Output(*FString::Printf(TEXT("A%d"), i)) });
						Items.Add(Item);
						continue;
					}
					V.Part = MakeUnique<FGraphPart>();
					if (!V.Part->Build(V.Own->G.Release(), {
						{ FVertexName(TEXT("Trig")), *Front->Output(*FString::Printf(TEXT("T%d"), i)) },
						{ FVertexName(TEXT("Val")), *Front->Output(*FString::Printf(TEXT("V%d"), i)) } }, InParams, OutResults))
					{
						return nullptr;
					}
					const FString GainName = FString::Printf(TEXT("G%d"), i);
					Item.GainSig = InputSig(GMix, *GainName, TEXT("Float"), ESig::Float, FLiteral(0.0f));
					Items.Add(Item);
					TDataWriteReference<float> GainRef = TDataWriteReference<float>::CreateNew(0.0f);
					const FAnyDataReference* VoiceOut = V.Part->Output(TEXT("Out"));
					Links.Add(FLink{ FVertexName(*InName), *VoiceOut });
					Links.Add(FLink{ FVertexName(*GainName), FAnyDataReference(TDataReadReference<float>(GainRef)) });
					FShiftAudioStats::FTap* Tail = bShadow ? FShiftAudioStats::Get().FindOrAddTailTap(FName(*(FString(TEXT("Tail_")) + V.Name))) : nullptr;
					// the references first: the part is moved into the run below
					const FTriggerReadRef VoiceTrig = Front->Output(*FString::Printf(TEXT("T%d"), i))->GetDataReadReference<FTrigger>();
					const FAudioBufferReadRef VoiceAudio = VoiceOut->GetDataReadReference<FAudioBuffer>();
					Gated.Add(FGatedRun(MoveTemp(V.Part), VoiceTrig, GainRef, VoiceAudio, V.Gain, FMath::CeilToInt(V.ActiveSeconds * BlockRate) + 1, Tail));
				}
				const FStereo LayerOut = GMix.MixStereo(Items);
				if (!OutputSig(GMix, TEXT("Left"), TEXT("Audio"), LayerOut.Left) || !OutputSig(GMix, TEXT("Right"), TEXT("Audio"), LayerOut.Right))
				{
					return nullptr;
				}
				TUniquePtr<FGraphPart> MixPart = MakeUnique<FGraphPart>();
				if (!MixPart->Build(GMix.G.Release(), Links, InParams, OutResults))
				{
					return nullptr;
				}
				Runs.Add(FLayerRun(MoveTemp(Front), MoveTemp(MixPart), *LayerGains[L], TDataWriteReference<float>::CreateNew(0.0f), QuietBlocks, MoveTemp(Gated)));
			}

			// ---- the final mix (always runs): the layer gains (a silent layer's is written as 0 by the runner), reverb, master volume
			FSynth GFinal(TEXT("ShiftMusic"));
			auto StereoIn = [&](const TCHAR* Left, const TCHAR* Right) -> FStereo
				{
					FStereo S;
					S.Left = InputSig(GFinal, Left, TEXT("Audio"), ESig::Audio, FLiteral(0.0f));
					S.Right = InputSig(GFinal, Right, TEXT("Audio"), ESig::Audio, FLiteral(0.0f));
					return S;
				};
			const FStereo DayIn = StereoIn(TEXT("DayLeft"), TEXT("DayRight"));
			const FStereo NightIn = StereoIn(TEXT("NightLeft"), TEXT("NightRight"));
			const FStereo TensionIn = StereoIn(TEXT("TensionLeft"), TEXT("TensionRight"));
			const FSig DayGainSig = InputSig(GFinal, TEXT("DayGain"), TEXT("Float"), ESig::Float, FLiteral(1.0f));
			const FSig NightGainSig = InputSig(GFinal, TEXT("NightGain"), TEXT("Float"), ESig::Float, FLiteral(0.0f));
			const FSig TensionGainSig = InputSig(GFinal, TEXT("TensionGain"), TEXT("Float"), ESig::Float, FLiteral(0.0f));
			const FSig VolumeSig = InputSig(GFinal, TEXT("Volume"), TEXT("Float"), ESig::Float, FLiteral(1.0f));
			if (!DayIn.Left.IsValid() || !DayIn.Right.IsValid() || !NightIn.Left.IsValid() || !NightIn.Right.IsValid() || !TensionIn.Left.IsValid() || !TensionIn.Right.IsValid()
				|| !DayGainSig.IsValid() || !NightGainSig.IsValid() || !TensionGainSig.IsValid() || !VolumeSig.IsValid())
			{
				return nullptr;
			}
			auto Trimmed = [&](const FSig& GainIn, float Db) -> FSig
				{
					return FMath::Abs(Db) < 1.0e-3f ? GainIn : GFinal.FScale(GainIn, DbToLinear(Db));
				};
			const FStereo Mixed = GFinal.MixStereo({ TPair<FStereo, float>(DayIn, 0.0f), TPair<FStereo, float>(NightIn, 0.0f), TPair<FStereo, float>(TensionIn, 0.0f) });
			// the layer gains are signals: set them on the mixer's gain pins
			{
				const INode* Mixer = Mixed.Left.Node;
				GFinal.Feed(Mixer, TEXT("Gain 0"), Trimmed(DayGainSig, DayTrim));
				GFinal.Feed(Mixer, TEXT("Gain 1"), Trimmed(NightGainSig, NightTrim));
				GFinal.Feed(Mixer, TEXT("Gain 2"), Trimmed(TensionGainSig, TensionTrim));
			}
			const FStereo Wet = GFinal.Reverb(Mixed.Left, Mixed.Right, 0.16f, 1.0f, 0.8f, 0.45f);
			GFinal.MeterTap(Wet);   // QA: the mix before the master volume
			// No limiter after the volume: the loudest the music gets (all layers up, the meter run checks it) is below -2 dBFS, so a limiter never acted, and it cost
			// 0.3% of a core for the two channels and delayed the music by its 10 ms look-ahead.
			const FSig Left = GFinal.Mul(Wet.Left, VolumeSig);
			const FSig Right = GFinal.Mul(Wet.Right, VolumeSig);
			if (!OutputSig(GFinal, TEXT("Left"), TEXT("Audio"), Left) || !OutputSig(GFinal, TEXT("Right"), TEXT("Audio"), Right))
			{
				return nullptr;
			}
			TUniquePtr<FGraphPart> FinalPart = MakeUnique<FGraphPart>();
			if (!FinalPart->Build(GFinal.G.Release(), {
				{ FVertexName(TEXT("DayLeft")), *Runs[0].Mix->Output(TEXT("Left")) }, { FVertexName(TEXT("DayRight")), *Runs[0].Mix->Output(TEXT("Right")) },
				{ FVertexName(TEXT("NightLeft")), *Runs[1].Mix->Output(TEXT("Left")) }, { FVertexName(TEXT("NightRight")), *Runs[1].Mix->Output(TEXT("Right")) },
				{ FVertexName(TEXT("TensionLeft")), *Runs[2].Mix->Output(TEXT("Left")) }, { FVertexName(TEXT("TensionRight")), *Runs[2].Mix->Output(TEXT("Right")) },
				{ FVertexName(TEXT("DayGain")), FAnyDataReference(TDataReadReference<float>(Runs[0].Gated)) },
				{ FVertexName(TEXT("NightGain")), FAnyDataReference(TDataReadReference<float>(Runs[1].Gated)) },
				{ FVertexName(TEXT("TensionGain")), FAnyDataReference(TDataReadReference<float>(Runs[2].Gated)) },
				{ FVertexName(TEXT("Volume")), FAnyDataReference(Volume) } }, InParams, OutResults))
			{
				return nullptr;
			}

			return MakeUnique<ShiftAudio::FShiftMusicRunner>(MoveTemp(ClockPart), MoveTemp(Runs), MoveTemp(FinalPart), Play, Volume, QuietBlocks, bShadow);
		}
	};

	using FShiftMusicNode = TNodeFacade<FShiftMusicOperator>;
	METASOUND_REGISTER_NODE(FShiftMusicNode)
}

namespace ShiftAudio
{
	void FShiftMusicRunner::BindInputs(Metasound::FInputVertexInterfaceData& InVertexData)
	{
		using namespace Metasound;
		InVertexData.BindReadVertex(FVertexName(TEXT("Play")), Play);
		InVertexData.BindReadVertex(FVertexName(TEXT("DayGain")), Layers[0].Gain);
		InVertexData.BindReadVertex(FVertexName(TEXT("NightGain")), Layers[1].Gain);
		InVertexData.BindReadVertex(FVertexName(TEXT("TensionGain")), Layers[2].Gain);
		InVertexData.BindReadVertex(FVertexName(TEXT("Volume")), Volume);
	}

	void FShiftMusicRunner::BindOutputs(Metasound::FOutputVertexInterfaceData& InVertexData)
	{
		using namespace Metasound;
		if (const FAnyDataReference* Ref = Clock->Output(TEXT("MIDI Clock")))
		{
			InVertexData.SetVertex(FVertexName(TEXT("MIDI Clock")), *Ref);
		}
		if (const FAnyDataReference* Ref = Final->Output(TEXT("Left")))
		{
			InVertexData.SetVertex(FVertexName(TEXT("Left")), *Ref);
		}
		if (const FAnyDataReference* Ref = Final->Output(TEXT("Right")))
		{
			InVertexData.SetVertex(FVertexName(TEXT("Right")), *Ref);
		}
	}
}

#undef LOCTEXT_NAMESPACE
