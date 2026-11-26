// Rate for Rate - Game Off 2026

#include "Audio/Nodes/ShiftSynth.h"

#include "Audio/ShiftAudioStats.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/Platform.h"
#include "Logging/LogMacros.h"
#include "Math/UnrealMathUtility.h"
#include "MetasoundLiteral.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundVertex.h"
#include "ShiftComposite.h"
#include "ShiftHappens.h"
#include "Templates/TupleFwd.h"
#include "UObject/NameTypes.h"

namespace ShiftAudio
{
	using namespace Metasound;

	namespace
	{
		struct FKeyDef
		{
			const TCHAR* Namespace;
			const TCHAR* Name;        // "{n}" is replaced by the input count
			const TCHAR* Variant;
			int32 Major;
		};

		/** The node table of Tools/py/ms_graph.py (KEYS). */
		const TMap<FName, FKeyDef>& Keys()
		{
			static const TMap<FName, FKeyDef> Table = {
				{ "sine", { TEXT("UE"), TEXT("Sine"), TEXT("Audio"), 1 } },
				{ "saw", { TEXT("UE"), TEXT("Saw"), TEXT("Audio"), 1 } },
				{ "square", { TEXT("UE"), TEXT("Square"), TEXT("Audio"), 1 } },
				{ "tri", { TEXT("UE"), TEXT("Triangle"), TEXT("Audio"), 1 } },
				{ "noise", { TEXT("UE"), TEXT("Noise"), TEXT("Audio"), 1 } },
				{ "adsr", { TEXT("ADSR Envelope"), TEXT("ADSR Envelope"), TEXT("Audio"), 1 } },
				{ "adsr_f", { TEXT("ADSR Envelope"), TEXT("ADSR Envelope"), TEXT("Float"), 1 } },
				{ "ad", { TEXT("AD Envelope"), TEXT("AD Envelope"), TEXT("Audio"), 1 } },
				{ "ad_f", { TEXT("AD Envelope"), TEXT("AD Envelope"), TEXT("Float"), 1 } },
				{ "mixer_m", { TEXT("AudioMixer"), TEXT("Audio Mixer (Mono, {n})"), TEXT(""), 1 } },
				{ "mixer_s", { TEXT("AudioMixer"), TEXT("Audio Mixer (Stereo, {n})"), TEXT(""), 1 } },
				{ "panner", { TEXT("UE"), TEXT("Stereo Panner"), TEXT(""), 1 } },
				{ "mul_a", { TEXT("UE"), TEXT("Multiply"), TEXT("Audio"), 1 } },
				{ "mul_af", { TEXT("UE"), TEXT("Multiply"), TEXT("Audio by Float"), 1 } },
				{ "mul_f", { TEXT("UE"), TEXT("Multiply"), TEXT("Float"), 1 } },
				{ "add_a", { TEXT("UE"), TEXT("Add"), TEXT("Audio"), 1 } },
				{ "add_f", { TEXT("UE"), TEXT("Add"), TEXT("Float"), 1 } },
				{ "sub_f", { TEXT("UE"), TEXT("Subtract"), TEXT("Float"), 1 } },
				{ "ladder", { TEXT("UE"), TEXT("Ladder Filter"), TEXT("Audio"), 1 } },
				{ "svf", { TEXT("UE"), TEXT("State Variable Filter"), TEXT("Audio"), 1 } },
				{ "lp1", { TEXT("UE"), TEXT("One-Pole Low Pass Filter"), TEXT("Audio"), 1 } },
				{ "delay_t", { TEXT("UE"), TEXT("Trigger Delay"), TEXT(""), 1 } },
				{ "midi_f", { TEXT("UE"), TEXT("MIDI To Frequency"), TEXT("Float"), 1 } },
				{ "lin2log", { TEXT("UE"), TEXT("Linear To Log Frequency"), TEXT(""), 1 } },
				{ "reverb", { TEXT("UE"), TEXT("Plate Reverb"), TEXT("Stereo"), 1 } },
				{ "counter", { TEXT("UE"), TEXT("Trigger Counter"), TEXT(""), 1 } },
				{ "seq", { TEXT("UE"), TEXT("Trigger Sequence ({n})"), TEXT(""), 1 } },
				{ "cmp_f", { TEXT("TriggerCompare"), TEXT("Clamp"), TEXT("Float"), 1 } },
				{ "cmp_i", { TEXT("TriggerCompare"), TEXT("Clamp"), TEXT("Int32"), 1 } },
				{ "get_f", { TEXT("Array"), TEXT("Get"), TEXT("Float:Array"), 1 } },
				{ "modulo", { TEXT("UE"), TEXT("Modulo"), TEXT("Int32"), 1 } },
				{ "metronome", { TEXT("HarmonixNodes"), TEXT("Metronome"), TEXT(""), 0 } },
				{ "to_transport", { TEXT("HarmonixNodes"), TEXT("TriggerToTransport"), TEXT(""), 0 } },
				{ "subdiv", { TEXT("HarmonixNodes"), TEXT("MidiClockSubdivisionTrigger"), TEXT(""), 0 } },
				{ "tap_meter", { TEXT("Shift"), TEXT("Meter"), TEXT(""), 1 } },
				{ "tap_hit", { TEXT("Shift"), TEXT("Hit Tap"), TEXT(""), 1 } },
				{ "tap_level", { TEXT("Shift"), TEXT("Level Tap"), TEXT(""), 1 } },
				{ "tap_step", { TEXT("Shift"), TEXT("Step Tap"), TEXT(""), 1 } },
			};
			return Table;
		}

		FSig MakeSig(const INode* Node, const TCHAR* Pin, ESig Type)
		{
			FSig Sig;
			Sig.Node = Node;
			Sig.Pin = FVertexName(Pin);
			Sig.Type = Type;
			return Sig;
		}
	}

	const INode* FSynth::Node(const TCHAR* Key, int32 N)
	{
		const FKeyDef* Def = Keys().Find(FName(Key));
		if (!Def)
		{
			UE_LOG(LogShift, Error, TEXT("Composite graph: unknown node key '%s'"), Key);
			return nullptr;
		}
		const FString Name = FString(Def->Name).Replace(TEXT("{n}"), *FString::FromInt(N));
		const FString Variant = FString(Def->Variant).Replace(TEXT("{n}"), *FString::FromInt(N));
		return G.Add(Def->Namespace, *Name, *Variant, Def->Major);
	}

	void FSynth::Feed(const INode* Node, const TCHAR* Pin, const FArg& Value)
	{
		if (!Node || Value.bNone)
		{
			return;
		}
		if (Value.bSig)
		{
			G.Connect(Value.Sig.Node ? FPin{ Value.Sig.Node, Value.Sig.Pin } : FPin(), Node, Pin);
			return;
		}
		const FVertexName PinName(Pin);
		if (!Node->GetVertexInterface().ContainsInputVertex(PinName))
		{
			UE_LOG(LogShift, Error, TEXT("Composite graph: a node has no input pin '%s'"), Pin);
			return;
		}
		const FString Type = Node->GetVertexInterface().GetInputVertex(PinName).DataTypeName.ToString();
		if (Type == TEXT("Float") || Type == TEXT("Time"))
		{
			G.Set(Node, Pin, FLiteral(Value.Num));
		}
		else if (Type == TEXT("Int32") || Type.StartsWith(TEXT("Enum")))
		{
			G.Set(Node, Pin, FLiteral(FMath::RoundToInt(Value.Num)));
		}
		else if (Type == TEXT("Bool") || Type == TEXT("Trigger"))
		{
			G.Set(Node, Pin, FLiteral(Value.Num != 0.0f));
		}
		else
		{
			UE_LOG(LogShift, Error, TEXT("Composite graph: cannot set pin '%s' of type %s from a number"), Pin, *Type);
		}
	}

	void FSynth::FeedArray(const INode* Node, const TCHAR* Pin, const TArray<float>& Values)
	{
		G.Set(Node, Pin, FLiteral(Values));
	}

	FSig FSynth::Out(const INode* Node, const TCHAR* Pin, ESig Type) const
	{
		return MakeSig(Node, Pin, Type);
	}

	const INode* FSynth::Make(const TCHAR* Key, FPins Pins, int32 N)
	{
		const INode* Created = Node(Key, N);
		for (const FPinValue& Pin : Pins)
		{
			Feed(Created, Pin.Name, Pin.Value);
		}
		return Created;
	}

	FSig FSynth::Noise()
	{
		Seed += 7919;
		return Out(Make(TEXT("noise"), { { TEXT("Seed"), Seed }, { TEXT("Type"), 1 } }), TEXT("Audio"), ESig::Audio);   // white (the node defaults to pink)
	}

	FSig FSynth::Osc(const TCHAR* Kind, const FArg& Freq, const FArg& Fm, const FArg& Sync)
	{
		return Out(Make(Kind, { { TEXT("Frequency"), Freq }, { TEXT("Modulation"), Fm }, { TEXT("Sync"), Sync } }), TEXT("Audio"), ESig::Audio);
	}

	FSig FSynth::Ad(const FSig& Trig, float Attack, float Decay, float CurveA, float CurveD, bool bAudio, FSig* OutOnDone)
	{
		const INode* Env = Make(bAudio ? TEXT("ad") : TEXT("ad_f"), { { TEXT("Trigger"), Trig }, { TEXT("Attack Time"), Attack }, { TEXT("Decay Time"), Decay },
			{ TEXT("Attack Curve"), CurveA }, { TEXT("Decay Curve"), CurveD } });
		if (OutOnDone)
		{
			*OutOnDone = Out(Env, TEXT("On Done"), ESig::Trigger);
		}
		return Out(Env, TEXT("Out Envelope"), bAudio ? ESig::Audio : ESig::Float);
	}

	FSig FSynth::ExpEnv(const FSig& Trig, float Tau, float Attack, bool bAudio, float MaxLen)
	{
		float Length = 5.0f * Tau;
		if (MaxLen > 0.0f)
		{
			Length = FMath::Min(Length, FMath::Max(0.01f, MaxLen - Attack));
		}
		Length = FMath::Max(0.01f, Length);
		const float K = Length / Tau;
		const float Curve = FMath::Loge(FMath::Max(1.0e-6f, 1.0f - FMath::Exp(-0.3f * K))) / FMath::Loge(0.3f);
		return Ad(Trig, Attack, Length, 1.0f, FMath::Min(3.0f, FMath::Max(0.15f, Curve)), bAudio);
	}

	FSig FSynth::Glide(const FSig& Trig, float F0, float F1, float Dur, bool bAudio)
	{
		const float Ratio = FMath::Max(1.0e-3f, F1 / F0);
		const float Curve = FMath::Loge(FMath::Sqrt(Ratio) + 1.0f) / FMath::Loge(2.0f);
		return Ad(Trig, Dur, 0.001f, FMath::Min(3.0f, FMath::Max(0.2f, Curve)), 1.0f, bAudio);
	}

	FSig FSynth::Adsr(const FSig& TrigOn, const FSig& TrigOff, float Attack, float Decay, float Sustain, float Release)
	{
		const INode* Env = Make(TEXT("adsr"), { { TEXT("Trigger Attack"), TrigOn }, { TEXT("Trigger Release"), TrigOff }, { TEXT("Attack Time"), Attack },
			{ TEXT("Decay Time"), Decay }, { TEXT("Sustain Level"), Sustain }, { TEXT("Release Time"), Release }, { TEXT("Hard Reset"), true } });
		return Out(Env, TEXT("Out Envelope"), ESig::Audio);
	}

	FSig FSynth::Mul(const FSig& A, const FArg& B)
	{
		if (A.Type == ESig::Audio)
		{
			const bool bAudioB = B.bSig && B.Sig.Type == ESig::Audio;
			return Out(Make(bAudioB ? TEXT("mul_a") : TEXT("mul_af"), { { TEXT("PrimaryOperand"), A }, { TEXT("AdditionalOperands"), B } }), TEXT("Out"), ESig::Audio);
		}
		return Out(Make(TEXT("mul_f"), { { TEXT("PrimaryOperand"), A }, { TEXT("AdditionalOperands"), B } }), TEXT("Out"), ESig::Float);
	}

	FSig FSynth::Add(const FSig& A, const FArg& B)
	{
		const bool bAudio = A.Type == ESig::Audio;
		return Out(Make(bAudio ? TEXT("add_a") : TEXT("add_f"), { { TEXT("PrimaryOperand"), A }, { TEXT("AdditionalOperands"), B } }), TEXT("Out"), bAudio ? ESig::Audio : ESig::Float);
	}

	FSig FSynth::SubF(const FArg& A, const FArg& B)
	{
		return Out(Make(TEXT("sub_f"), { { TEXT("PrimaryOperand"), A }, { TEXT("AdditionalOperands"), B } }), TEXT("Out"), ESig::Float);
	}

	FSig FSynth::FScale(const FSig& A, float K)
	{
		return Out(Make(TEXT("mul_f"), { { TEXT("PrimaryOperand"), A }, { TEXT("AdditionalOperands"), K } }), TEXT("Out"), ESig::Float);
	}

	FSig FSynth::Mix(const TArray<FMono>& Items)
	{
		if (Items.Num() == 1)
		{
			return Items[0].Gain == 1.0f ? Items[0].Audio : Mul(Items[0].Audio, Items[0].Gain);
		}
		if (Items.Num() > 8)
		{
			TArray<FMono> Groups;
			for (int32 i = 0; i < Items.Num(); i += 8)
			{
				TArray<FMono> Part;
				for (int32 k = i; k < FMath::Min(i + 8, Items.Num()); ++k)
				{
					Part.Add(Items[k]);
				}
				FMono Group;
				Group.Audio = Mix(Part);
				Groups.Add(Group);
			}
			return Mix(Groups);
		}
		const INode* Mixer = Node(TEXT("mixer_m"), FMath::Max(2, Items.Num()));
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			Feed(Mixer, *FString::Printf(TEXT("In %d"), i), Items[i].Audio);
			Feed(Mixer, *FString::Printf(TEXT("Gain %d"), i), Items[i].Gain);
		}
		return Out(Mixer, TEXT("Out"), ESig::Audio);
	}

	FStereo FSynth::MixStereo(const TArray<TPair<FStereo, float>>& Items)
	{
		TArray<FStereoItem> Converted;
		for (const TPair<FStereo, float>& Pair : Items)
		{
			FStereoItem Item;
			Item.Audio = Pair.Key;
			Item.Gain = Pair.Value;
			Converted.Add(Item);
		}
		return MixStereo(Converted);
	}

	FStereo FSynth::MixStereo(const TArray<FStereoItem>& Items)
	{
		if (Items.Num() == 1)
		{
			return Items[0].Audio;
		}
		if (Items.Num() > 8)
		{
			TArray<FStereoItem> Groups;
			for (int32 i = 0; i < Items.Num(); i += 8)
			{
				TArray<FStereoItem> Part;
				for (int32 k = i; k < FMath::Min(i + 8, Items.Num()); ++k)
				{
					Part.Add(Items[k]);
				}
				FStereoItem Group;
				Group.Audio = MixStereo(Part);
				Groups.Add(Group);
			}
			return MixStereo(Groups);
		}
		const INode* Mixer = Node(TEXT("mixer_s"), FMath::Max(2, Items.Num()));
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			Feed(Mixer, *FString::Printf(TEXT("In %d L"), i), Items[i].Audio.Left);
			Feed(Mixer, *FString::Printf(TEXT("In %d R"), i), Items[i].Audio.Right);
			Feed(Mixer, *FString::Printf(TEXT("Gain %d"), i), Items[i].GainSig.IsValid() ? FArg(Items[i].GainSig) : FArg(Items[i].Gain));
		}
		FStereo Result;
		Result.Left = Out(Mixer, TEXT("Out L"), ESig::Audio);
		Result.Right = Out(Mixer, TEXT("Out R"), ESig::Audio);
		return Result;
	}

	FSynth::FSvf FSynth::Svf(const FSig& In, const FArg& Cutoff, float Q)
	{
		const float Resonance = FMath::Min(10.0f, FMath::Max(1.0f, 1.0f + 9.0f * (Q - 0.5f) / 24.5f));
		const INode* Filter = Make(TEXT("svf"), { { TEXT("In"), In }, { TEXT("Cutoff Frequency"), Cutoff }, { TEXT("Resonance"), Resonance } });
		FSvf Result;
		Result.Lp = Out(Filter, TEXT("Low Pass Filter"), ESig::Audio);
		Result.Hp = Out(Filter, TEXT("High Pass Filter"), ESig::Audio);
		Result.Bp = Out(Filter, TEXT("Band Pass"), ESig::Audio);
		Result.Bs = Out(Filter, TEXT("Band Stop"), ESig::Audio);
		return Result;
	}

	FSig FSynth::Ladder(const FSig& In, const FArg& Cutoff, float Resonance)
	{
		return Out(Make(TEXT("ladder"), { { TEXT("In"), In }, { TEXT("Cutoff Frequency"), Cutoff }, { TEXT("Resonance"), Resonance } }), TEXT("Out"), ESig::Audio);
	}

	FSig FSynth::LogSweep(const FSig& U, float F0, float F1)
	{
		if (F1 >= F0)
		{
			return Out(Make(TEXT("lin2log"), { { TEXT("Value"), U }, { TEXT("Min Domain"), 0.0f }, { TEXT("Max Domain"), 1.0f }, { TEXT("Min Range"), F0 }, { TEXT("Max Range"), F1 } }), TEXT("Frequency"), ESig::Float);
		}
		const FSig Inverted = SubF(1.0f, U);
		return Out(Make(TEXT("lin2log"), { { TEXT("Value"), Inverted }, { TEXT("Min Domain"), 0.0f }, { TEXT("Max Domain"), 1.0f }, { TEXT("Min Range"), F1 }, { TEXT("Max Range"), F0 } }), TEXT("Frequency"), ESig::Float);
	}

	FSig FSynth::Lp1(const FSig& In, float Cutoff)
	{
		return Out(Make(TEXT("lp1"), { { TEXT("In"), In }, { TEXT("Cutoff Frequency"), Cutoff } }), TEXT("Out"), ESig::Audio);
	}

	FSig FSynth::MidiHz(const FArg& Note)
	{
		return Out(Make(TEXT("midi_f"), { { TEXT("MIDI In"), Note } }), TEXT("Out Frequency"), ESig::Float);
	}

	FSig FSynth::DelayTrigger(const FSig& Trig, float Seconds)
	{
		return Out(Make(TEXT("delay_t"), { { TEXT("In"), Trig }, { TEXT("Delay Time"), Seconds } }), TEXT("Out"), ESig::Trigger);
	}

	FStereo FSynth::Reverb(const FSig& Left, const FSig& Right, float Wet, float Dry, float Decay, float Damp)
	{
		const INode* Plate = Make(TEXT("reverb"), { { TEXT("In Left"), Left }, { TEXT("In Right"), Right }, { TEXT("Dry Level"), Dry }, { TEXT("Wet Level"), Wet },
			{ TEXT("Decay"), Decay }, { TEXT("Dampening"), Damp } });
		FStereo Result;
		Result.Left = Out(Plate, TEXT("Out Left"), ESig::Audio);
		Result.Right = Out(Plate, TEXT("Out Right"), ESig::Audio);
		return Result;
	}

	FStereo FSynth::Pan(const FSig& In, float Amount)
	{
		const INode* Panner = Make(TEXT("panner"), { { TEXT("In"), In }, { TEXT("Pan Amount"), Amount } });
		FStereo Result;
		Result.Left = Out(Panner, TEXT("Out Left"), ESig::Audio);
		Result.Right = Out(Panner, TEXT("Out Right"), ESig::Audio);
		return Result;
	}

	void FSynth::MeterTap(const FStereo& Mix)
	{
		if (!FShiftAudioStats::TapsWanted())
		{
			return;
		}
		Make(TEXT("tap_meter"), { { TEXT("Left"), Mix.Left }, { TEXT("Right"), Mix.Right } });
	}

	void FSynth::HitTap(const FSig& Trig, const FString& Name, int32 Expected)
	{
		if (!FShiftAudioStats::TapsWanted())
		{
			return;
		}
		FShiftAudioStats& Stats = FShiftAudioStats::Get();
		const int32 Index = Stats.IndexOf(Stats.FindOrAddTap(FName(*Name), true, Expected));
		Make(TEXT("tap_hit"), { { TEXT("In"), Trig }, { TEXT("Tap"), Index } });
	}

	void FSynth::LevelTap(const FSig& Audio, const FString& Name)
	{
		if (!FShiftAudioStats::TapsWanted())
		{
			return;
		}
		FShiftAudioStats& Stats = FShiftAudioStats::Get();
		const int32 Index = Stats.IndexOf(Stats.FindOrAddTap(FName(*(FString(TEXT("Lvl_")) + Name)), false, 0));
		Make(TEXT("tap_level"), { { TEXT("Audio"), Audio }, { TEXT("Tap"), Index } });
	}

	void FSynth::StepTap(const FSig& Count)
	{
		if (!FShiftAudioStats::TapsWanted())
		{
			return;
		}
		Make(TEXT("tap_step"), { { TEXT("Count"), Count } });
	}
}
