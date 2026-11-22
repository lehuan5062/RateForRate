// Shift Happens - Game Off 2026

// "Shift Sfx": the 26 one-shot sound effects as composite nodes (the C++ counterpart of the recipes in Tools/py/ms_sfx.py). Each MS_Sfx_<Name> asset
// holds ONE node of this class, with the effect's id and its level trim as the node's default values. The inner graph is the same engine nodes
// the Python recipe made: voices mixed, trimmed, reverb, limiter, and an OnFinished trigger.

#include "Audio/Nodes/ShiftVoices.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "HAL/Platform.h"
#include "Internationalization/Text.h"
#include "Math/UnrealMathUtility.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundFacade.h"
#include "MetasoundLiteral.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundParamHelper.h"
#include "MetasoundPrimitives.h"
#include "MetasoundTrigger.h"
#include "MetasoundVertex.h"
#include "ShiftComposite.h"
#include "ShiftSynth.h"
#include "Templates/UniquePtr.h"
#include "UObject/NameTypes.h"

#define LOCTEXT_NAMESPACE "ShiftHappens_SfxNode"

namespace ShiftAudio
{
	using namespace Voices;

	namespace
	{
		FMono Part(const FSig& Audio, float Gain)
		{
			FMono Item;
			Item.Audio = Audio;
			Item.Gain = Gain;
			return Item;
		}

		/** `t` itself when the delay is 0, else the trigger delayed (the recipes' `at`). */
		FSig At(FSynth& G, const FSig& T, float Seconds)
		{
			return Seconds <= 0.0f ? T : G.DelayTrigger(T, Seconds);
		}

		struct FRecipe
		{
			TArray<FMono> Parts;
			float Seconds = 0.1f;
			float Wet = 0.0f;
		};

		const TCHAR* const SfxNames[26] = { TEXT("Click"), TEXT("Hover"), TEXT("Slider"), TEXT("Upload"), TEXT("Transition"), TEXT("Tip"), TEXT("Plug"), TEXT("Flip"),
			TEXT("Reinforce"), TEXT("Weaken"), TEXT("Gated"), TEXT("Locked"), TEXT("NoPosts"), TEXT("NightOn"), TEXT("NightOff"), TEXT("Sock"), TEXT("Heartbeat"),
			TEXT("ModWarn"), TEXT("ModSweep"), TEXT("Strike"), TEXT("Shadowban"), TEXT("Blame"), TEXT("Day"), TEXT("Win"), TEXT("Lose"), TEXT("DQ") };

		FRecipe MakeRecipe(FSynth& G, const FSig& T, int32 Id)
		{
			FRecipe R;
			const FString Name = SfxNames[Id];
			if (Name == TEXT("Click"))
			{
				R.Parts = { Part(Tone(G, T, TEXT("tri"), 1300, 1250, 0.04f, 0.001f, 0.012f), 0.8f), Part(Burst(G, T, 0.02f, 0.004f, TEXT("hp"), 3000, 0.7f), 0.3f) };
				R.Seconds = 0.05f;
			}
			else if (Name == TEXT("Hover"))
			{
				R.Parts = { Part(Tone(G, T, TEXT("sine"), 880, 1100, 0.06f, 0.008f, 0.025f), 1.0f) };
				R.Seconds = 0.07f;
			}
			else if (Name == TEXT("Slider"))
			{
				R.Parts = { Part(Tone(G, T, TEXT("square"), 640, 640, 0.03f, 0.001f, 0.01f), 1.0f) };
				R.Seconds = 0.04f;
			}
			else if (Name == TEXT("Upload"))
			{
				const float Notes[4] = { 523.25f, 659.26f, 783.99f, 1046.5f };
				for (int32 i = 0; i < 4; ++i)
				{
					R.Parts.Add(Part(Marimba(G, At(G, T, 0.075f * i), Notes[i]), 0.7f));
				}
				R.Parts.Add(Part(NoiseSweep(G, T, 0.5f, 800, 6000, 1.5f, 0.6f), 0.15f));
				R.Seconds = 1.5f;
				R.Wet = 0.12f;
			}
			else if (Name == TEXT("Transition"))
			{
				R.Parts = { Part(Tone(G, T, TEXT("sine"), 300, 700, 0.18f, 0.004f, 0.08f), 0.5f), Part(NoiseSweep(G, T, 0.25f, 400, 2500, 1.2f, 0.5f), 0.2f) };
				R.Seconds = 0.3f;
				R.Wet = 0.1f;
			}
			else if (Name == TEXT("Tip"))
			{
				R.Parts = { Part(Bell(G, T, 1318.5f, 0.9f, 0.20f), 1.0f) };
				R.Seconds = 1.0f;
				R.Wet = 0.1f;
			}
			else if (Name == TEXT("Plug"))
			{
				R.Parts = { Part(Tone(G, T, TEXT("sine"), 620, 190, 0.09f, 0.002f, 0.04f), 0.9f), Part(Burst(G, T, 0.015f, 0.003f, TEXT("hp"), 2500, 0.7f), 0.3f) };
				R.Seconds = 0.1f;
			}
			else if (Name == TEXT("Flip"))
			{
				const FSig T2 = At(G, T, 0.07f);
				R.Parts = { Part(Bell(G, T, 1318.5f, 0.9f, 0.16f), 0.8f), Part(Bell(G, T2, 1975.5f, 0.8f, 0.14f), 0.7f), Part(Tone(G, T, TEXT("sine"), 500, 1800, 0.12f, 0.002f, 0.05f), 0.2f) };
				R.Seconds = 0.9f;
				R.Wet = 0.12f;
			}
			else if (Name == TEXT("Reinforce"))
			{
				R.Parts = { Part(Tone(G, T, TEXT("sine"), 520, 520, 0.07f, 0.003f, 0.03f), 0.7f), Part(Tone(G, At(G, T, 0.07f), TEXT("sine"), 780, 780, 0.08f, 0.003f, 0.035f), 0.7f) };
				R.Seconds = 0.16f;
			}
			else if (Name == TEXT("Weaken"))
			{
				R.Parts = { Part(Tone(G, T, TEXT("tri"), 420, 240, 0.2f, 0.004f, 0.1f, 14.0f, 0.5f), 1.0f) };
				R.Seconds = 0.22f;
			}
			else if (Name == TEXT("Gated"))
			{
				R.Parts = { Part(Tone(G, T, TEXT("square"), 120, 90, 0.18f, 0.002f, 0.06f), 0.7f), Part(Burst(G, T, 0.12f, 0.03f, TEXT("lp"), 900, 0.7f), 0.6f) };
				R.Seconds = 0.2f;
			}
			else if (Name == TEXT("Locked"))
			{
				R.Parts = { Part(Burst(G, T, 0.03f, 0.006f, TEXT("bp"), 2200, 4.0f), 0.9f), Part(Burst(G, At(G, T, 0.05f), 0.03f, 0.006f, TEXT("bp"), 1700, 4.0f), 0.8f) };
				R.Seconds = 0.1f;
			}
			else if (Name == TEXT("NoPosts"))
			{
				const FSig A = Tone(G, T, TEXT("saw"), 196, 196, 0.1f, 0.003f, 0.08f, 0.0f, 0.0f, 0.6f);
				const FSig B = Tone(G, At(G, T, 0.1f), TEXT("saw"), 147, 147, 0.16f, 0.003f, 0.1f, 0.0f, 0.0f, 0.6f);
				R.Parts = { Part(G.Lp1(G.Mix({ Part(A, 1.0f), Part(B, 1.0f) }), 900.0f), 1.0f) };
				R.Seconds = 0.3f;
			}
			else if (Name == TEXT("NightOn"))
			{
				R.Parts = { Part(NoiseSweep(G, T, 0.9f, 5000, 200, 1.2f, 0.25f), 0.5f), Part(Tone(G, T, TEXT("sine"), 130, 38, 0.7f, 0.01f, 0.3f), 0.9f) };
				R.Seconds = 0.95f;
				R.Wet = 0.2f;
			}
			else if (Name == TEXT("NightOff"))
			{
				R.Parts = { Part(NoiseSweep(G, T, 0.6f, 200, 6000, 1.2f, 0.75f), 0.5f), Part(Tone(G, T, TEXT("sine"), 180, 720, 0.45f, 0.02f, 0.2f), 0.5f) };
				R.Seconds = 0.65f;
				R.Wet = 0.12f;
			}
			else if (Name == TEXT("Sock"))
			{
				const FSig Voice = G.Mix({ Part(Tone(G, T, TEXT("saw"), 1200, 90, 0.22f, 0.001f, 0.09f), 0.7f), Part(Tone(G, At(G, T, 0.04f), TEXT("square"), 240, 1900, 0.1f, 0.001f, 0.05f), 0.3f) });
				// stutter: a 66.7 Hz gate that is open for two thirds of every period
				const FSig Gate = G.Out(G.Make(TEXT("square"), { { TEXT("Frequency"), 66.667f }, { TEXT("Bi Polar"), false }, { TEXT("Pulse Width"), 0.667f } }), TEXT("Audio"), ESig::Audio);
				R.Parts = { Part(G.Mul(Voice, Gate), 1.0f), Part(Burst(G, T, 0.06f, 0.015f, TEXT("hp"), 4000, 0.7f), 0.3f) };
				R.Seconds = 0.25f;
			}
			else if (Name == TEXT("Heartbeat"))
			{
				R.Parts = { Part(Tom(G, T, 62.0f, 1.0f), 1.0f), Part(Tom(G, At(G, T, 0.19f), 56.0f, 0.8f), 0.8f) };
				R.Seconds = 0.8f;
			}
			else if (Name == TEXT("ModWarn"))
			{
				R.Parts = { Part(Tone(G, T, TEXT("sine"), 1400, 1400, 1.2f, 0.003f, 0.35f), 0.7f), Part(Tone(G, At(G, T, 0.34f), TEXT("sine"), 1400, 1400, 1.2f, 0.003f, 0.35f), 0.3f) };
				R.Seconds = 1.6f;
				R.Wet = 0.15f;
			}
			else if (Name == TEXT("ModSweep"))
			{
				R.Parts = { Part(NoiseSweep(G, T, 1.0f, 300, 5000, 2.5f, 0.5f), 1.0f) };
				R.Seconds = 1.0f;
				R.Wet = 0.15f;
			}
			else if (Name == TEXT("Strike"))
			{
				R.Parts = { Part(Tom(G, T, 55.0f, 1.0f), 1.0f) };
				for (int32 i = 0; i < 2; ++i)
				{
					const FSig Ti = At(G, T, 0.16f * i);
					R.Parts.Add(Part(Tone(G, Ti, TEXT("square"), 820, 800, 0.12f, 0.002f, 0.1f), 0.6f));
					R.Parts.Add(Part(Tone(G, Ti, TEXT("square"), 1230, 1200, 0.12f, 0.002f, 0.1f), 0.4f));
				}
				R.Seconds = 0.6f;
				R.Wet = 0.08f;
			}
			else if (Name == TEXT("Shadowban"))
			{
				const FSig Voice = G.Mix({ Part(Pad(G, T, At(G, T, 0.3f), 220.0f, 4000.0f, 0.005f, 0.2f), 0.5f), Part(NoiseSweep(G, T, 0.6f, 6000, 150, 1.0f, 0.1f), 0.3f) });
				const FSig U = G.Ad(T, 0.6f, 0.001f, 1.0f, 1.0f, false);
				const FSig Cutoff = G.LogSweep(U, 2500.0f, 150.0f);   // a low-pass that closes from 2.5 kHz to 150 Hz
				R.Parts = { Part(G.Svf(Voice, FArg(Cutoff), 0.8f).Lp, 1.0f) };
				R.Seconds = 0.7f;
				R.Wet = 0.1f;
			}
			else if (Name == TEXT("Blame"))
			{
				R.Parts = { Part(Tom(G, T, 70.0f, 1.0f), 0.9f), Part(Burst(G, At(G, T, 0.02f), 0.05f, 0.012f, TEXT("hp"), 1800, 0.7f), 0.6f),
					Part(NoiseSweep(G, At(G, T, 0.05f), 0.25f, 500, 3500, 1.0f, 0.3f), 0.25f) };
				R.Seconds = 0.5f;
				R.Wet = 0.06f;
			}
			else if (Name == TEXT("Day"))
			{
				R.Parts = { Part(Tone(G, T, TEXT("sine"), 1000, 960, 0.03f, 0.001f, 0.01f), 0.6f), Part(Burst(G, T, 0.012f, 0.002f, TEXT("hp"), 3500, 0.7f), 0.3f) };
				R.Seconds = 0.04f;
			}
			else if (Name == TEXT("Win"))
			{
				const float Notes[4] = { 523.25f, 659.26f, 783.99f, 1046.5f };
				for (int32 i = 0; i < 4; ++i)
				{
					R.Parts.Add(Part(Marimba(G, At(G, T, 0.11f * i), Notes[i]), 0.6f));
				}
				const FSig TChord = At(G, T, 0.40f);
				const FSig TOff = At(G, T, 0.40f + 0.9f);
				for (int32 i = 0; i < 4; ++i)
				{
					R.Parts.Add(Part(Pad(G, TChord, TOff, Notes[i], 3000.0f, 0.05f, 0.9f), 0.18f));
				}
				const float Bells[3] = { 1046.5f, 1318.5f, 1568.0f };
				for (int32 i = 0; i < 3; ++i)
				{
					R.Parts.Add(Part(Bell(G, TChord, Bells[i], 0.7f, 0.5f), 0.18f));
				}
				R.Seconds = 2.4f;
				R.Wet = 0.2f;
			}
			else if (Name == TEXT("Lose"))
			{
				const FSig Voice = G.Mix({ Part(Tone(G, T, TEXT("saw"), 311, 294, 0.5f, 0.01f, 0.35f, 5.0f, 0.4f), 0.6f),
					Part(Tone(G, At(G, T, 0.45f), TEXT("saw"), 262, 247, 0.7f, 0.01f, 0.45f, 5.0f, 0.4f), 0.6f) });
				R.Parts = { Part(G.Ladder(Voice, 1200.0f, 1.0f), 1.0f) };
				R.Seconds = 1.3f;
				R.Wet = 0.12f;
			}
			else if (Name == TEXT("DQ"))
			{
				R.Parts = { Part(Tone(G, T, TEXT("square"), 130, 128, 0.6f, 0.004f, 0.35f), 0.6f), Part(Tone(G, T, TEXT("square"), 137, 135, 0.6f, 0.004f, 0.35f), 0.6f),
					Part(Burst(G, T, 0.4f, 0.12f, TEXT("lp"), 1500, 0.7f), 0.4f) };
				R.Seconds = 0.65f;
			}
			return R;
		}
	}
}

namespace Metasound
{
	namespace ShiftSfxVertexNames
	{
		METASOUND_PARAM(InputPlay, "Play", "Plays the effect.")
			METASOUND_PARAM(InputSound, "Sound", "Which of the 26 effects (the order of EShiftSfx).")
			METASOUND_PARAM(InputTrim, "Trim dB", "The level trim of the effect, from Tools/py/ms_trims.json.")
			METASOUND_PARAM(OutputLeft, "Left", "Left channel.")
			METASOUND_PARAM(OutputRight, "Right", "Right channel.")
			METASOUND_PARAM(OutputFinished, "Finished", "Fires when the effect is over.")
	}

	struct FShiftSfxOperator : public ShiftAudio::FCompositeOperatorBase
	{
		static const FVertexInterface& GetVertexInterface()
		{
			using namespace ShiftSfxVertexNames;
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertex<FTrigger>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputPlay)),
					TInputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputSound), 0),
					TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputTrim), 0.0f)
				),
				FOutputVertexInterface(
					TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputLeft)),
					TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputRight)),
					TOutputDataVertex<FTrigger>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputFinished))
				)
			);
			return Interface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto InitNodeInfo = []() -> FNodeClassMetadata
				{
					FNodeClassMetadata Info;
					Info.ClassName = { TEXT("Shift"), TEXT("Sfx"), TEXT("") };
					Info.MajorVersion = 1;
					Info.MinorVersion = 0;
					Info.DisplayName = METASOUND_LOCTEXT("Shift_SfxDisplayName", "Shift Sfx");
					Info.Description = METASOUND_LOCTEXT("Shift_SfxDescription", "One of the 26 sound effects of the game: a composite of engine nodes (voices, mixer, reverb, limiter) shown as one node.");
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
			using namespace ShiftSfxVertexNames;
			const int32 Id = FMath::Clamp(*InParams.InputData.GetOrCreateDefaultDataReadReference<int32>(METASOUND_GET_PARAM_NAME(InputSound), InParams.OperatorSettings), 0, 25);
			const float TrimDb = *InParams.InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InputTrim), InParams.OperatorSettings);

			FSynth G(*FString::Printf(TEXT("MS_Sfx_%s"), SfxNames[Id]));
			const INode* PlayNode = G.G.Input(TEXT("Play"), FName("Trigger"), FLiteral(false));
			const INode* LeftOut = G.G.Output(TEXT("Left"), FName("Audio"));
			const INode* RightOut = G.G.Output(TEXT("Right"), FName("Audio"));
			const INode* FinishedOut = G.G.Output(TEXT("Finished"), FName("Trigger"));
			if (!PlayNode || !LeftOut || !RightOut || !FinishedOut)
			{
				return nullptr;
			}
			const FSig T = FGraphBuilder::Source(PlayNode).Node ? FSig{ PlayNode, FGraphBuilder::Source(PlayNode).Name, ESig::Trigger } : FSig();

			FRecipe R = MakeRecipe(G, T, Id);
			FSig Mono = G.Mix(R.Parts);
			if (FMath::Abs(TrimDb) > 1.0e-3f)
			{
				Mono = G.Mul(Mono, DbToLinear(TrimDb));
			}
			FStereo Stereo{ Mono, Mono };
			if (R.Wet > 0.0f)
			{
				Stereo = G.Reverb(Mono, Mono, R.Wet, 1.0f, 0.7f, 0.45f);
			}
			// No limiter: every effect is trimmed to peak at its target level (all well below -1 dBFS, the meter run checks it), so a limiter never acted, and it cost
			// 0.15% of a core per channel and delayed the sound by its 10 ms look-ahead.
			G.MeterTap(Stereo);   // QA: the final mix
			const FSig Left = Stereo.Left;
			const FSig Right = Stereo.Right;
			const FSig Finished = G.DelayTrigger(T, R.Seconds + (R.Wet > 0.0f ? 0.7f : 0.05f));
			G.G.ConnectToOutput(FPin{ Left.Node, Left.Pin }, LeftOut);
			G.G.ConnectToOutput(FPin{ Right.Node, Right.Pin }, RightOut);
			G.G.ConnectToOutput(FPin{ Finished.Node, Finished.Pin }, FinishedOut);

			return BuildOwningGraphOperator(G.G.Release(), InParams, OutResults);
		}
	};

	using FShiftSfxNode = TNodeFacade<FShiftSfxOperator>;
	METASOUND_REGISTER_NODE(FShiftSfxNode)
}

#undef LOCTEXT_NAMESPACE
