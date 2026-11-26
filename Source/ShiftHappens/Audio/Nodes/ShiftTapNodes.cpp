// Rate for Rate - Game Off 2026

// The QA taps inside the composite audio nodes. They have no outputs and are never shown in an asset (the music and the effects are one node each);
// they publish numbers to FShiftAudioStats while the audio meter run has switched that on, and do nothing otherwise.
//   Shift Meter       the stereo mix: peak, RMS and the low / mid / high band levels (what the graphs' Peak / Rms / Low / Mid / High outputs were)
//   Shift Hit Tap     a pattern lane's trigger: counts the hits
//   Shift Level Tap   a voice: its peak level before panning and gain
//   Shift Step Tap    the step counter of the music clock

#include <atomic>
#include "Audio/ShiftAudioStats.h"
#include "Containers/Array.h"
#include "DSP/AlignedBuffer.h"
#include "DSP/EnvelopeFollower.h"
#include "DSP/Filter.h"
#include "HAL/Platform.h"
#include "Internationalization/Text.h"
#include "Math/UnrealMathUtility.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundEnvironment.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundParamHelper.h"
#include "MetasoundPrimitives.h"
#include "MetasoundTrigger.h"
#include "MetasoundVertex.h"
#include "MetasoundVertexData.h"
#include "Templates/UniquePtr.h"

#define LOCTEXT_NAMESPACE "ShiftHappens_TapNodes"

namespace Metasound
{
	namespace ShiftTapNames
	{
		METASOUND_PARAM(InputLeft, "Left", "Left channel.")
			METASOUND_PARAM(InputRight, "Right", "Right channel.")
			METASOUND_PARAM(InputTrigger, "In", "The trigger to count.")
			METASOUND_PARAM(InputAudio, "Audio", "The voice.")
			METASOUND_PARAM(InputCount, "Count", "The step counter.")
			METASOUND_PARAM(InputTap, "Tap", "Which named tap of FShiftAudioStats.")
	}

	namespace ShiftTapPrivate
	{
		/** The InstanceID of the generator this operator belongs to (0 when the environment has none). */
		uint64 InstanceOf(const FBuildOperatorParams& InParams)
		{
			return InParams.Environment.Contains<uint64>(CoreInterface::Environment::InstanceID) ? InParams.Environment.GetValue<uint64>(CoreInterface::Environment::InstanceID) : 0;
		}

		FNodeClassMetadata MakeInfo(const TCHAR* Name, const FText& DisplayName, const FVertexInterface& Interface)
		{
			FNodeClassMetadata Info;
			Info.ClassName = { TEXT("Shift"), Name, TEXT("") };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = DisplayName;
			Info.Description = METASOUND_LOCTEXT("Shift_TapDescription", "A QA tap inside the composite audio nodes: publishes numbers for the audio meter run, never shown in an asset.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = Interface;
			Info.CategoryHierarchy.Emplace(METASOUND_LOCTEXT("Shift_TapCategory", "Shift Happens QA"));
			return Info;
		}
	}

	// ---- Shift Meter ---------------------------------------------------------------------------------------------------------------
	class FShiftMeterOperator : public TExecutableOperator<FShiftMeterOperator>
	{
	public:
		static const FVertexInterface& GetVertexInterface()
		{
			using namespace ShiftTapNames;
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputLeft)),
					TInputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputRight))),
				FOutputVertexInterface());
			return Interface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			static const FNodeClassMetadata Info = ShiftTapPrivate::MakeInfo(TEXT("Meter"), METASOUND_LOCTEXT("Shift_MeterName", "Shift Meter"), GetVertexInterface());
			return Info;
		}

		static TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults)
		{
			using namespace ShiftTapNames;
			return MakeUnique<FShiftMeterOperator>(InParams, ShiftTapPrivate::InstanceOf(InParams),
				InParams.InputData.GetOrCreateDefaultDataReadReference<FAudioBuffer>(METASOUND_GET_PARAM_NAME(InputLeft), InParams.OperatorSettings),
				InParams.InputData.GetOrCreateDefaultDataReadReference<FAudioBuffer>(METASOUND_GET_PARAM_NAME(InputRight), InParams.OperatorSettings));
		}

		FShiftMeterOperator(const FBuildOperatorParams& InParams, uint64 InInstance, const FAudioBufferReadRef& InLeft, const FAudioBufferReadRef& InRight)
			: Instance(InInstance), Left(InLeft), Right(InRight)
		{
			Reset(InParams);
		}

		virtual void BindInputs(FInputVertexInterfaceData& InOutVertexData) override
		{
			using namespace ShiftTapNames;
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputLeft), Left);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputRight), Right);
		}

		virtual void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override {}

		void Reset(const IOperator::FResetParams& InParams)
		{
			const float SampleRate = InParams.OperatorSettings.GetSampleRate();
			auto InitFollower = [SampleRate](Audio::FEnvelopeFollower& F, float AttackMs, float ReleaseMs, Audio::EPeakMode::Type Mode)
				{
					Audio::FEnvelopeFollowerInitParams P;
					P.SampleRate = SampleRate;
					P.NumChannels = 1;
					P.AttackTimeMsec = AttackMs;
					P.ReleaseTimeMsec = ReleaseMs;
					P.Mode = Mode;
					F.Init(P);
				};
			// the settings of the old graph meters: Peak 1 ms / 250 ms; Rms 20 ms / 100 ms; the bands are Rms followers behind a low-pass at 250 Hz,
			// a band-pass at 900 Hz and a high-pass at 2.5 kHz
			InitFollower(PeakFollower, 1.0f, 250.0f, Audio::EPeakMode::Peak);
			InitFollower(RmsFollower, 20.0f, 100.0f, Audio::EPeakMode::RootMeanSquared);
			InitFollower(LowFollower, 20.0f, 100.0f, Audio::EPeakMode::RootMeanSquared);
			InitFollower(MidFollower, 20.0f, 100.0f, Audio::EPeakMode::RootMeanSquared);
			InitFollower(HighFollower, 20.0f, 100.0f, Audio::EPeakMode::RootMeanSquared);
			auto InitFilter = [SampleRate](Audio::FStateVariableFilter& F, Audio::EFilter::Type Type, float Freq)
				{
					F.Init(SampleRate, 1, 0);
					F.SetFilterType(Type);
					F.SetFrequency(Freq);
					F.SetQ(0.5f);
					F.Update();
				};
			InitFilter(LowFilter, Audio::EFilter::LowPass, 250.0f);
			InitFilter(MidFilter, Audio::EFilter::BandPass, 900.0f);
			InitFilter(HighFilter, Audio::EFilter::HighPass, 2500.0f);
			const int32 Frames = InParams.OperatorSettings.GetNumFramesPerBlock();
			Mono.SetNumZeroed(Frames);
			Scratch.SetNumZeroed(Frames);
			Band.SetNumZeroed(Frames);
		}

		void Execute()
		{
			FShiftAudioStats& Stats = FShiftAudioStats::Get();
			if (!Stats.Accepts(Instance))
			{
				return;
			}
			const int32 Frames = Left->Num();
			const float* L = Left->GetData();
			const float* R = Right->GetData();
			for (int32 i = 0; i < Frames; ++i)
			{
				Mono[i] = 0.5f * (L[i] + R[i]);
			}
			auto Last = [](const Audio::FEnvelopeFollower& F) { const TArray<float>& V = F.GetEnvelopeValues(); return V.Num() > 0 ? V[0] : 0.0f; };
			PeakFollower.ProcessAudio(Mono.GetData(), Frames, Scratch.GetData());
			Stats.Master.Peak = Last(PeakFollower);
			RmsFollower.ProcessAudio(Mono.GetData(), Frames, Scratch.GetData());
			Stats.Master.Rms = Last(RmsFollower);
			LowFilter.ProcessAudio(Mono.GetData(), Frames, Band.GetData());
			LowFollower.ProcessAudio(Band.GetData(), Frames, Scratch.GetData());
			Stats.Master.Low = Last(LowFollower);
			MidFilter.ProcessAudio(Mono.GetData(), Frames, Band.GetData());
			MidFollower.ProcessAudio(Band.GetData(), Frames, Scratch.GetData());
			Stats.Master.Mid = Last(MidFollower);
			HighFilter.ProcessAudio(Mono.GetData(), Frames, Band.GetData());
			HighFollower.ProcessAudio(Band.GetData(), Frames, Scratch.GetData());
			Stats.Master.High = Last(HighFollower);
			// the meter wants the largest value and the mean over the case, not only the latest value
			auto Max = [](std::atomic<float>& Target, float Value) { if (Value > Target.load(std::memory_order_relaxed)) { Target.store(Value, std::memory_order_relaxed); } };
			auto Add = [](std::atomic<double>& Target, float Value) { Target.store(Target.load(std::memory_order_relaxed) + Value, std::memory_order_relaxed); };
			Max(Stats.Master.PeakMax, Stats.Master.Peak);
			Max(Stats.Master.RmsMax, Stats.Master.Rms);
			Max(Stats.Master.LowMax, Stats.Master.Low);
			Max(Stats.Master.MidMax, Stats.Master.Mid);
			Max(Stats.Master.HighMax, Stats.Master.High);
			Add(Stats.Master.RmsSum, Stats.Master.Rms);
			Add(Stats.Master.LowSum, Stats.Master.Low);
			Add(Stats.Master.MidSum, Stats.Master.Mid);
			Add(Stats.Master.HighSum, Stats.Master.High);
			++Stats.Master.Blocks;
			++Stats.Master.BlocksTotal;
		}

	private:
		uint64 Instance;
		FAudioBufferReadRef Left;
		FAudioBufferReadRef Right;
		Audio::FEnvelopeFollower PeakFollower, RmsFollower, LowFollower, MidFollower, HighFollower;
		Audio::FStateVariableFilter LowFilter, MidFilter, HighFilter;
		Audio::FAlignedFloatBuffer Mono, Scratch, Band;
	};
	using FShiftMeterNode = TNodeFacade<FShiftMeterOperator>;
	METASOUND_REGISTER_NODE(FShiftMeterNode)

		// ---- Shift Hit Tap -------------------------------------------------------------------------------------------------------------
		class FShiftHitTapOperator : public TExecutableOperator<FShiftHitTapOperator>
	{
	public:
		static const FVertexInterface& GetVertexInterface()
		{
			using namespace ShiftTapNames;
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertex<FTrigger>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputTrigger)),
					TInputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputTap), 0)),
				FOutputVertexInterface());
			return Interface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			static const FNodeClassMetadata Info = ShiftTapPrivate::MakeInfo(TEXT("Hit Tap"), METASOUND_LOCTEXT("Shift_HitTapName", "Shift Hit Tap"), GetVertexInterface());
			return Info;
		}

		static TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults)
		{
			using namespace ShiftTapNames;
			const int32 Index = *InParams.InputData.GetOrCreateDefaultDataReadReference<int32>(METASOUND_GET_PARAM_NAME(InputTap), InParams.OperatorSettings);
			return MakeUnique<FShiftHitTapOperator>(ShiftTapPrivate::InstanceOf(InParams), InParams.InputData.GetOrCreateDefaultDataReadReference<FTrigger>(METASOUND_GET_PARAM_NAME(InputTrigger), InParams.OperatorSettings),
				FShiftAudioStats::Get().TapAt(Index));
		}

		FShiftHitTapOperator(uint64 InInstance, const FTriggerReadRef& InTrigger, FShiftAudioStats::FTap* InTap) : Instance(InInstance), Trigger(InTrigger), Tap(InTap) {}

		virtual void BindInputs(FInputVertexInterfaceData& InOutVertexData) override
		{
			using namespace ShiftTapNames;
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputTrigger), Trigger);
		}
		virtual void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override {}
		void Reset(const IOperator::FResetParams& InParams) {}

		void Execute()
		{
			Trigger->ExecuteBlock([](int32, int32) {}, [this](int32, int32)
				{
					if (Tap && FShiftAudioStats::Get().Accepts(Instance))
					{
						++Tap->Count;
					}
				});
		}

	private:
		uint64 Instance;
		FTriggerReadRef Trigger;
		FShiftAudioStats::FTap* Tap;
	};
	using FShiftHitTapNode = TNodeFacade<FShiftHitTapOperator>;
	METASOUND_REGISTER_NODE(FShiftHitTapNode)

		// ---- Shift Level Tap -----------------------------------------------------------------------------------------------------------
		class FShiftLevelTapOperator : public TExecutableOperator<FShiftLevelTapOperator>
	{
	public:
		static const FVertexInterface& GetVertexInterface()
		{
			using namespace ShiftTapNames;
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputAudio)),
					TInputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputTap), 0)),
				FOutputVertexInterface());
			return Interface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			static const FNodeClassMetadata Info = ShiftTapPrivate::MakeInfo(TEXT("Level Tap"), METASOUND_LOCTEXT("Shift_LevelTapName", "Shift Level Tap"), GetVertexInterface());
			return Info;
		}

		static TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults)
		{
			using namespace ShiftTapNames;
			const int32 Index = *InParams.InputData.GetOrCreateDefaultDataReadReference<int32>(METASOUND_GET_PARAM_NAME(InputTap), InParams.OperatorSettings);
			return MakeUnique<FShiftLevelTapOperator>(ShiftTapPrivate::InstanceOf(InParams), InParams.InputData.GetOrCreateDefaultDataReadReference<FAudioBuffer>(METASOUND_GET_PARAM_NAME(InputAudio), InParams.OperatorSettings),
				FShiftAudioStats::Get().TapAt(Index));
		}

		FShiftLevelTapOperator(uint64 InInstance, const FAudioBufferReadRef& InAudio, FShiftAudioStats::FTap* InTap) : Instance(InInstance), AudioIn(InAudio), Tap(InTap) {}

		virtual void BindInputs(FInputVertexInterfaceData& InOutVertexData) override
		{
			using namespace ShiftTapNames;
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputAudio), AudioIn);
		}
		virtual void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override {}
		void Reset(const IOperator::FResetParams& InParams) {}

		void Execute()
		{
			if (!Tap || !FShiftAudioStats::Get().Accepts(Instance))
			{
				return;
			}
			float Peak = 0.0f;
			const float* Data = AudioIn->GetData();
			for (int32 i = 0; i < AudioIn->Num(); ++i)
			{
				Peak = FMath::Max(Peak, FMath::Abs(Data[i]));
			}
			if (Peak > Tap->Level.load(std::memory_order_relaxed))
			{
				Tap->Level = Peak;
			}
		}

	private:
		uint64 Instance;
		FAudioBufferReadRef AudioIn;
		FShiftAudioStats::FTap* Tap;
	};
	using FShiftLevelTapNode = TNodeFacade<FShiftLevelTapOperator>;
	METASOUND_REGISTER_NODE(FShiftLevelTapNode)

		// ---- Shift Step Tap ------------------------------------------------------------------------------------------------------------
		class FShiftStepTapOperator : public TExecutableOperator<FShiftStepTapOperator>
	{
	public:
		static const FVertexInterface& GetVertexInterface()
		{
			using namespace ShiftTapNames;
			static const FVertexInterface Interface(
				FInputVertexInterface(TInputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputCount), 0)),
				FOutputVertexInterface());
			return Interface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			static const FNodeClassMetadata Info = ShiftTapPrivate::MakeInfo(TEXT("Step Tap"), METASOUND_LOCTEXT("Shift_StepTapName", "Shift Step Tap"), GetVertexInterface());
			return Info;
		}

		static TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults)
		{
			using namespace ShiftTapNames;
			return MakeUnique<FShiftStepTapOperator>(ShiftTapPrivate::InstanceOf(InParams), InParams.InputData.GetOrCreateDefaultDataReadReference<int32>(METASOUND_GET_PARAM_NAME(InputCount), InParams.OperatorSettings));
		}

		FShiftStepTapOperator(uint64 InInstance, const FInt32ReadRef& InCount) : Instance(InInstance), Count(InCount) {}

		virtual void BindInputs(FInputVertexInterfaceData& InOutVertexData) override
		{
			using namespace ShiftTapNames;
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputCount), Count);
		}
		virtual void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override {}
		void Reset(const IOperator::FResetParams& InParams) {}

		void Execute()
		{
			FShiftAudioStats& Stats = FShiftAudioStats::Get();
			if (Stats.Accepts(Instance) && *Count != Stats.Step.load(std::memory_order_relaxed))
			{
				Stats.Step = *Count;
				++Stats.StepChanges;
			}
		}

	private:
		uint64 Instance;
		FInt32ReadRef Count;
	};
	using FShiftStepTapNode = TNodeFacade<FShiftStepTapOperator>;
	METASOUND_REGISTER_NODE(FShiftStepTapNode)
}

#undef LOCTEXT_NAMESPACE
