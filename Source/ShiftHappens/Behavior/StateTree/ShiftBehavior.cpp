// Shift Happens - Game Off 2026

#include "Behavior/StateTree/ShiftBehavior.h"

#include "Behavior/ShiftPolicies.h"
#include "Behavior/StateTree/ShiftBehaviorContext.h"
#include "Containers/ArrayView.h"
#include "Containers/UnrealString.h"
#include "Game/ShiftRun.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/Platform.h"
#include "Logging/LogMacros.h"
#include "ShiftHappens.h"
#include "Sim/ShiftParams.h"
#include "Sim/ShiftRng.h"
#include "Sim/ShiftSim.h"
#include "StateTree.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeTypes.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	UStateTree* LoadTree(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("/Game/Shift/StateTrees/%s.%s"), Name, Name);
		return LoadObject<UStateTree>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
}

TSharedPtr<FShiftBehavior> FShiftBehavior::Create(FShiftRun& Run)
{
	UStateTree* Rival = LoadTree(TEXT("ST_Rival"));
	UStateTree* Others = LoadTree(TEXT("ST_Others"));
	UStateTree* Director = LoadTree(TEXT("ST_Director"));
	UStateTree* Ambient = LoadTree(TEXT("ST_Ambient"));
	if (!Rival || !Others || !Director || !Ambient)
	{
		UE_LOG(LogShift, Error, TEXT("Behaviour StateTrees MISSING (rival %d, others %d, director %d, ambient %d): this run uses the native stand-in policies. Run Tools/run_py.ps1 -Script Tools/py/build_statetrees.py."),
			Rival ? 1 : 0, Others ? 1 : 0, Director ? 1 : 0, Ambient ? 1 : 0);
		return nullptr;
	}
	for (UStateTree* Tree : { Rival, Others, Director, Ambient })
	{
		if (!Tree->IsReadyToRun())
		{
			UE_LOG(LogShift, Error, TEXT("StateTree %s is not ready to run (not compiled?): this run uses the native stand-in policies."), *Tree->GetName());
			return nullptr;
		}
	}

	TSharedRef<FShiftBehavior> Behavior = MakeShared<FShiftBehavior>();
	const ShiftSim::Sim& S = Run.GetSim();
	const ShiftSim::PolicyParams& PP = Run.GetPolicy();
	const uint32 RunSeed = Run.GetSeed();

	auto Add = [&](const FString& Name, UStateTree* Tree, int32 EntryId, double FirstTime, uint32 Salt)
		{
			TUniquePtr<FInstance> Instance = MakeUnique<FInstance>();
			Instance->Name = Name;
			Instance->Tree = Tree;
			Instance->Context = NewObject<UShiftBehaviorContext>(GetTransientPackage());
			Instance->Context->Run = &Run;
			Instance->Context->EntryId = EntryId;
			Instance->Context->NextTime = FirstTime;
			const uint32 Derived = ShiftSim::Rng::Derive(RunSeed, Salt);
			Instance->Context->Rng.Seed(Derived);
			Instance->Seed = static_cast<int32>(Derived & 0x7FFFFFFFu);
			Behavior->Instances.Add(MoveTemp(Instance));
		};

	// Order = the order the native stand-ins ran in: rival, other entries, then the director.
	Add(TEXT("Rival"), Rival, S.RivalId(), ShiftSim::StandInRival::IntervalFor(S, PP), 0x52495641u);
	for (int32 E = 0; E < S.NumEntries(); ++E)
	{
		if (E != ShiftSim::kPlayerId && E != S.RivalId())
		{
			Add(FString::Printf(TEXT("Other%d"), E), Others, E, ShiftSim::StandInOthers::FirstActionTime(S, PP, E), 0x4F544800u + static_cast<uint32>(E));
		}
	}
	Add(TEXT("Director"), Director, -1, 0.0, 0x44495243u);
	Add(TEXT("Ambient"), Ambient, -1, S.TotalSeconds() / 8.0 * 1.3, 0x414D4249u);

	UE_LOG(LogShift, Log, TEXT("Behaviour: %d StateTree instances (rival, %d other entries, director, ambient)"), Behavior->Instances.Num(), Behavior->Instances.Num() - 3);
	return Behavior;
}

void FShiftBehavior::Tick(float DeltaTime)
{
	for (TUniquePtr<FInstance>& Ptr : Instances)
	{
		FInstance& I = *Ptr;
		if (I.bFailed)
		{
			continue;
		}
		UShiftBehaviorContext* ContextObject = I.Context;
		const FOnCollectStateTreeExternalData Collect = FOnCollectStateTreeExternalData::CreateLambda(
			[ContextObject](const FStateTreeExecutionContext& /*Context*/, const UStateTree* /*StateTree*/, TArrayView<const FStateTreeExternalDataDesc> Descs, TArrayView<FStateTreeDataView> OutViews)
			{
				for (int32 Index = 0; Index < Descs.Num(); ++Index)
				{
					if (Descs[Index].Struct == UShiftBehaviorContext::StaticClass())
					{
						OutViews[Index] = FStateTreeDataView(ContextObject);
					}
				}
				return true;
			});

		FStateTreeExecutionContext Exec(*ContextObject, *I.Tree, I.Data, Collect);
		if (!Exec.IsValid())
		{
			UE_LOG(LogShift, Error, TEXT("StateTree instance %s could not be linked: disabling it."), *I.Name);
			I.bFailed = true;
			continue;
		}
		if (!I.bStarted)
		{
			I.bStarted = true;
			FStateTreeExecutionContext::FStartParameters Params;
			Params.RandomSeed = I.Seed;     // seeded from the run seed: the default is the platform cycle counter, which would break reproducibility
			Exec.Start(MoveTemp(Params));
		}
		Exec.Tick(DeltaTime);
	}
}

int32 FShiftBehavior::ActionCount(EShiftBehaviorCounter Counter) const
{
	int32 Total = 0;
	for (const TUniquePtr<FInstance>& Ptr : Instances)
	{
		Total += Ptr->Context ? Ptr->Context->Counters[static_cast<int32>(Counter)] : 0;
	}
	return Total;
}

FShiftBehavior::~FShiftBehavior()
{
	// One summary line for the first few runs of a process (the balance commandlet plays hundreds): what the trees actually did.
	static int32 Logged = 0;
	if (Logged++ < 3 && Instances.Num() > 0)
	{
		UE_LOG(LogShift, Log, TEXT("Behaviour activity: rival mega %d defend %d attack %d | other entries plugged %d | mods telegraphed %d swept %d | ambient boosts %d streamers %d drama %d"),
			ActionCount(EShiftBehaviorCounter::RivalMega), ActionCount(EShiftBehaviorCounter::RivalDefend), ActionCount(EShiftBehaviorCounter::RivalAttack),
			ActionCount(EShiftBehaviorCounter::OtherPlug), ActionCount(EShiftBehaviorCounter::ModTelegraph), ActionCount(EShiftBehaviorCounter::ModSweep),
			ActionCount(EShiftBehaviorCounter::AmbientBoost), ActionCount(EShiftBehaviorCounter::AmbientStreamer), ActionCount(EShiftBehaviorCounter::AmbientDrama));
	}
}

void FShiftBehavior::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (TUniquePtr<FInstance>& Ptr : Instances)
	{
		Collector.AddReferencedObject(Ptr->Context);
		Collector.AddReferencedObject(Ptr->Tree);
	}
}
