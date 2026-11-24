// Shift Happens - Game Off 2026

#include "Board/ShiftBoard.h"

#include "Board/ShiftPalette.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/ArrayView.h"
#include "Engine/EngineTypes.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/Platform.h"
#include "Logging/LogMacros.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Math/Color.h"
#include "Math/MathFwd.h"
#include "Math/NumericLimits.h"
#include "Math/UnrealMathUtility.h"
#include "ShiftGlass.h"
#include "ShiftHappens.h"
#include "Sim/ShiftGraph.h"
#include "Sim/ShiftParams.h"
#include "Sim/ShiftSim.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	// Custom-data layouts: keep in sync with Tools/py/build_materials.py.
	constexpr int32 NodeFloats = 20;
	constexpr int32 GlowFloats = 9;
	constexpr int32 EdgeFloats = 13;

	constexpr float EdgeZ = 0.0f;
	constexpr float GlowZ = 1.0f;
	constexpr float NodeZ = 2.0f;

	constexpr float PlaneSize = 100.0f;     // /Engine/BasicShapes/Plane is 100 x 100 units
	constexpr float NodeQuadHalf = 2.9f;    // node quad half size, in body radii (node.hlsl lays its rings out in pixels, up to about 22 px beyond a small body)
	constexpr float GlowQuadHalf = 3.4f;    // glow quad half size, in body radii (glow.hlsl fades out by 3.4)
	constexpr float EdgeHalfWidth = 1.4f;   // world units
	constexpr float EdgeMargin = 6.0f;
	constexpr float EdgeBulgeFraction = 0.14f;
	constexpr float PressureEdgeMin = 0.25f;         // a voter shows its pushers once its pressure is above this (0..1)
	constexpr float QuietEdgeAlpha = 0.085f;          // alpha of a relation line that is not hovered and carries no push
	constexpr double PressureHorizonSeconds = 8.0;   // a voter of the player that would flip back within this many seconds starts to shake

	float Hash01(int32 A, int32 B)
	{
		uint32 H = static_cast<uint32>(A) * 73856093u ^ static_cast<uint32>(B) * 19349663u;
		H ^= H >> 13;
		H *= 0x5bd1e995u;
		H ^= H >> 15;
		return static_cast<float>(H & 0xFFFFFFu) / static_cast<float>(0x1000000);
	}

	FLinearColor ColorOfOwner(int32 Owner)
	{
		return Owner >= 0 ? ShiftPalette::Entry(Owner) : ShiftPalette::Undecided();
	}
}

AShiftBoard::AShiftBoard()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	auto MakeIsm = [this, Root](const TCHAR* Name, int32 SortPriority)
		{
			UInstancedStaticMeshComponent* Comp = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
			Comp->SetupAttachment(Root);
			Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Comp->SetCastShadow(false);
			Comp->bAffectDistanceFieldLighting = false;
			Comp->SetCanEverAffectNavigation(false);
			Comp->SetTranslucentSortPriority(SortPriority);
			return Comp;
		};
	EdgeISM = MakeIsm(TEXT("Edges"), 0);
	GlowISM = MakeIsm(TEXT("Glows"), 1);
	NodeISM = MakeIsm(TEXT("Nodes"), 2);
}

FVector2D AShiftBoard::SimToWorld(double SimX, double SimY) const
{
	return FVector2D(LayoutH_ * 0.5f - static_cast<float>(SimY), static_cast<float>(SimX) - LayoutW_ * 0.5f);
}

void AShiftBoard::Build(const ShiftSim::Sim& Sim)
{
	const ShiftSim::Graph& Graph = Sim.GetGraph();
	const int32 N = Graph.NodeCount();
	LayoutW_ = static_cast<float>(Sim.GetParams().LayoutWidth);
	LayoutH_ = static_cast<float>(Sim.GetParams().LayoutHeight);

	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UMaterialInterface* NodeMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shift/Materials/M_Node.M_Node"));
	UMaterialInterface* GlowMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shift/Materials/M_Glow.M_Glow"));
	UMaterialInterface* EdgeMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shift/Materials/M_Edge.M_Edge"));
	ThemeMPC = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Shift/Materials/MPC_Theme.MPC_Theme"));
	UE_LOG(LogShift, Log, TEXT("Board assets: plane=%d node=%d glow=%d edge=%d mpc=%d"),
		Plane != nullptr, NodeMat != nullptr, GlowMat != nullptr, EdgeMat != nullptr, ThemeMPC != nullptr);
	UseGlassBackground();

	struct FSetup
	{
		UInstancedStaticMeshComponent* Comp;
		UMaterialInterface* Mat;
		int32 Floats;
	};
	const FSetup Setups[3] = { { EdgeISM, EdgeMat, EdgeFloats }, { GlowISM, GlowMat, GlowFloats }, { NodeISM, NodeMat, NodeFloats } };
	for (const FSetup& S : Setups)
	{
		S.Comp->SetStaticMesh(Plane);
		if (S.Mat)
		{
			S.Comp->SetMaterial(0, S.Mat);
		}
		S.Comp->ClearInstances();
		S.Comp->SetNumCustomDataFloats(S.Floats);
	}

	NodeWorld_.SetNum(N);
	NodeRadii_.SetNum(N);
	Flash_.Init(0.0f, N);
	Hover_.Init(0.0f, N);
	Pressure_.Init(0.0f, N);
	Bounce_.Init(0.0f, N);
	BounceFrom_.Init(0.0f, N);
	BounceWho_.Init(-1, N);
	PrevOwner_.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		const ShiftSim::GraphNode& Node = Graph.nodes[i];
		NodeWorld_[i] = SimToWorld(Node.x, Node.y);
		NodeRadii_[i] = FMath::Clamp(8.0f + 3.2f * FMath::Sqrt(static_cast<float>(Node.weight) / 30.0f), 10.0f, 34.0f);
		PrevOwner_[i] = Sim.Opinion(i);

		const float NodeSize = NodeRadii_[i] * NodeQuadHalf * 2.0f / PlaneSize;
		NodeISM->AddInstance(FTransform(FRotator::ZeroRotator, FVector(NodeWorld_[i].X, NodeWorld_[i].Y, NodeZ), FVector(NodeSize, NodeSize, 1.0f)));
		const float GlowSize = NodeRadii_[i] * GlowQuadHalf * 2.0f / PlaneSize;
		GlowISM->AddInstance(FTransform(FRotator::ZeroRotator, FVector(NodeWorld_[i].X, NodeWorld_[i].Y, GlowZ), FVector(GlowSize, GlowSize, 1.0f)));
	}

	Edges_.Reset();
	EdgeLen_.Reset();
	for (int32 i = 0; i < N; ++i)
	{
		for (const int32 j : Graph.nodes[i].follows)
		{
			Edges_.Add({ i, j });

			const FVector2D P0 = NodeWorld_[j];   // influencer
			const FVector2D P1 = NodeWorld_[i];   // follower
			const FVector2D Chord = P1 - P0;
			const float Len = FMath::Max(static_cast<float>(Chord.Size()), 1.0f);
			EdgeLen_.Add(Len);
			const FVector2D Dir = Chord / Len;
			const FVector2D Nrm(-Dir.Y, Dir.X);
			const float Apex = 0.5f * EdgeBulgeFraction * Len;   // distance of the curve's apex from the chord
			const FVector2D Center = (P0 + P1) * 0.5f + Nrm * (Apex * 0.5f);
			const float SizeX = (Len + 2.0f * EdgeMargin) / PlaneSize;
			const float SizeY = (Apex + 2.0f * (EdgeHalfWidth + 3.0f + EdgeMargin)) / PlaneSize;
			const float YawDeg = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Dir.Y), static_cast<float>(Dir.X)));
			EdgeISM->AddInstance(FTransform(FRotator(0.0f, YawDeg, 0.0f), FVector(Center.X, Center.Y, EdgeZ), FVector(SizeX, SizeY, 1.0f)));
		}
	}
	UE_LOG(LogShift, Log, TEXT("Board built: %d nodes, %d edges."), N, Edges_.Num());
}

void AShiftBoard::BounceNode(int32 Node, int32 Who, float FromFrac)
{
	if (Bounce_.IsValidIndex(Node))
	{
		Bounce_[Node] = 1.0f;
		BounceFrom_[Node] = FMath::Clamp(FromFrac, 0.05f, 1.0f);
		BounceWho_[Node] = Who;
	}
}

void AShiftBoard::FlashNode(int32 Node)
{
	if (Flash_.IsValidIndex(Node))
	{
		Flash_[Node] = 1.0f;
	}
}

int32 AShiftBoard::NodeAtWorld(const FVector2D& World, float Slack) const
{
	int32 Best = -1;
	float BestScore = TNumericLimits<float>::Max();
	for (int32 i = 0; i < NodeWorld_.Num(); ++i)
	{
		const float D = static_cast<float>(FVector2D::Distance(World, NodeWorld_[i]));
		if (D <= NodeRadii_[i] * Slack)
		{
			const float Score = D / NodeRadii_[i];
			if (Score < BestScore)
			{
				BestScore = Score;
				Best = i;
			}
		}
	}
	return Best;
}

void AShiftBoard::UseGlassBackground()
{
	if (!ShiftGlass::IsActive() || !GetWorld())
	{
		return;
	}
	UMaterialInterface* GlassMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shift/Materials/M_BgGlass.M_BgGlass"));
	if (!GlassMat)
	{
		UE_LOG(LogShift, Warning, TEXT("Glass: M_BgGlass is missing (run Tools/py/build_materials.py), the background stays opaque"));
		return;
	}
	int32 Swapped = 0;
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		UStaticMeshComponent* Comp = It->GetStaticMeshComponent();
		UMaterialInterface* Current = Comp ? Comp->GetMaterial(0) : nullptr;
		if (Current && Current->GetName() == TEXT("M_Bg"))
		{
			Comp->SetMaterial(0, GlassMat);
			Comp->SetTranslucentSortPriority(-10);   // behind the edges (0), halos (1) and voters (2)
			++Swapped;
		}
	}
	UE_LOG(LogShift, Display, TEXT("Glass: the background is translucent (%d plane)"), Swapped);
}

void AShiftBoard::ApplyTheme(const FShiftBoardView& View) const
{
	if (!ThemeMPC || !GetWorld())
	{
		return;
	}
	UMaterialParameterCollectionInstance* Inst = GetWorld()->GetParameterCollectionInstance(ThemeMPC);
	if (!Inst)
	{
		return;
	}
	Inst->SetScalarParameterValue(TEXT("Night"), View.Night);
	Inst->SetScalarParameterValue(TEXT("Heat"), View.Heat);
	Inst->SetScalarParameterValue(TEXT("Beat"), View.Beat);
	Inst->SetScalarParameterValue(TEXT("Time"), View.Time);
	Inst->SetScalarParameterValue(TEXT("BgOpacity"), ShiftGlass::IsActive() ? FMath::Clamp(View.BackgroundOpacity, 0.0f, 1.0f) : 1.0f);
	Inst->SetVectorParameterValue(TEXT("Cursor"), FLinearColor(static_cast<float>(View.CursorWorld.X), static_cast<float>(View.CursorWorld.Y), 0.0f, 0.0f));
}

void AShiftBoard::Update(const ShiftSim::Sim& Sim, const FShiftBoardView& View, float DeltaTime)
{
	const ShiftSim::Graph& Graph = Sim.GetGraph();
	const int32 N = Graph.NodeCount();
	if (N != NodeWorld_.Num())
	{
		return;
	}

	for (int32 i = 0; i < N; ++i)
	{
		const int32 OwnerId = Sim.Opinion(i);
		if (OwnerId != PrevOwner_[i])
		{
			Flash_[i] = 1.0f;
			PrevOwner_[i] = OwnerId;
		}
		Flash_[i] = FMath::Max(0.0f, Flash_[i] - DeltaTime * 1.8f);
		Hover_[i] = FMath::FInterpTo(Hover_[i], View.HoverNode == i ? 1.0f : 0.0f, DeltaTime, 16.0f);

		const FLinearColor Fill = ColorOfOwner(OwnerId);
		const float Conv = FMath::Clamp(static_cast<float>(Sim.Conviction(i)), 0.0f, 1.0f);
		const float Lock01 = FMath::Clamp(static_cast<float>(Sim.LockRemaining(i) / 6.0), 0.0f, 1.0f);

		float Gate = -1.0f;
		if (Graph.IsGateNode(i))
		{
			const ShiftSim::GateInfo Info = Sim.Gate(i);
			if (Info.applies && !Info.open && Info.needed > 1.0e-6)
			{
				Gate = FMath::Clamp(static_cast<float>(Info.progress / Info.needed), 0.0f, 1.0f);
			}
		}
		const float Expr = OwnerId >= 0 ? FMath::Clamp((Conv - 0.5f) * 2.0f, -1.0f, 1.0f) : 0.0f;
		const float Sock = Sim.IsSockpuppet(i) ? 1.0f : 0.0f;

		// A voter of the player that opinion pressure is about to flip away: 0 at PressureHorizonSeconds or more from flipping, 1 when it is about to go.
		float PressureTarget = 0.0f;
		if (OwnerId == ShiftSim::kPlayerId && Sim.LockRemaining(i) <= 0.0)
		{
			PressureTarget = static_cast<float>(FMath::Clamp(1.0 - Sim.SecondsToFlip(i) / PressureHorizonSeconds, 0.0, 1.0));
		}
		Pressure_[i] = FMath::FInterpTo(Pressure_[i], PressureTarget, DeltaTime, 5.0f);

		// Who is after this voter: an announced attack (a ring in the attacker's colour closing in) comes first, then herd pressure (the colour of the entry that
		// out-pushes it), so the colour always names the entry that will take the voter.
		float ThreatFrac = 0.0f;
		FLinearColor PushCol(1.0f, 0.38f, 0.05f);
		int32 ThreatWho = -1;
		double ThreatLeft = 0.0;
		double ThreatTotal = 0.0;
		if (Sim.ThreatOn(i, &ThreatWho, &ThreatLeft, &ThreatTotal) && ThreatTotal > 1.0e-6)
		{
			ThreatFrac = static_cast<float>(FMath::Clamp(ThreatLeft / ThreatTotal, 0.02, 1.0));
			PushCol = ColorOfOwner(ThreatWho);
		}
		else if (Pressure_[i] > 0.05f && Sim.Challenger(i) >= 0)
		{
			PushCol = ColorOfOwner(Sim.Challenger(i));
		}
		if (Bounce_[i] > 0.0f)
		{
			// a parried ring: flung back out to full radius over a third of a second, in the attacker's colour
			Bounce_[i] = FMath::Max(0.0f, Bounce_[i] - DeltaTime / 0.35f);
			if (ThreatFrac <= 0.0f)
			{
				ThreatFrac = FMath::Lerp(BounceFrom_[i], 1.0f, 1.0f - Bounce_[i]);
				PushCol = ColorOfOwner(BounceWho_[i]);
			}
		}
		const float Shield01 = static_cast<float>(FMath::Clamp(Sim.ShieldRemaining(i) / FMath::Max(0.1, Sim.GetParams().RallyLock), 0.0, 1.0));

		const float NodeData[NodeFloats] = {
			Fill.R, Fill.G, Fill.B, Conv, Lock01, Gate, Hover_[i], Flash_[i],
			static_cast<float>(Graph.nodes[i].role), Expr, Sock, Pressure_[i],
			static_cast<float>(NodeWorld_[i].X), static_cast<float>(NodeWorld_[i].Y), NodeRadii_[i],
			ThreatFrac, PushCol.R, PushCol.G, PushCol.B, Shield01 };
		NodeISM->SetCustomData(i, MakeArrayView(NodeData, NodeFloats), false);

		const float GlowData[GlowFloats] = {
			Fill.R, Fill.G, Fill.B, Flash_[i], Hover_[i],
			static_cast<float>(NodeWorld_[i].X), static_cast<float>(NodeWorld_[i].Y), NodeRadii_[i], 1.0f };
		GlowISM->SetCustomData(i, MakeArrayView(GlowData, GlowFloats), false);
	}

	for (int32 e = 0; e < Edges_.Num(); ++e)
	{
		const FEdgeRec& E = Edges_[e];
		const int32 OwnerId = Sim.Opinion(E.Influencer);
		const FLinearColor Col = ColorOfOwner(OwnerId);
		const float ConvJ = FMath::Clamp(static_cast<float>(Sim.Conviction(E.Influencer)), 0.0f, 1.0f);
		const bool bHot = View.HoverNode == E.Influencer || View.HoverNode == E.Follower;
		// An edge that carries the push on a voter of the player that is about to flip back: the neighbour on the other end backs the entry that out-pushes it.
		const int32 PushOnFollower = Sim.Challenger(E.Follower);
		const int32 PushOnInfluencer = Sim.Challenger(E.Influencer);
		const bool bPush = (PushOnFollower >= 0 && Pressure_[E.Follower] > PressureEdgeMin && Sim.Opinion(E.Influencer) == PushOnFollower)
			|| (PushOnInfluencer >= 0 && Pressure_[E.Influencer] > PressureEdgeMin && Sim.Opinion(E.Follower) == PushOnInfluencer);
		FLinearColor EdgeCol = Col;
		float Base = bHot ? 0.85f : (OwnerId >= 0 ? 0.14f + 0.14f * ConvJ : 0.07f);
		float Pulse = (OwnerId >= 0 ? 0.30f + 0.70f * ConvJ : 0.0f) + (bHot ? 0.35f : 0.0f);
		float Wid = bHot ? EdgeHalfWidth + 0.9f : EdgeHalfWidth;
		if (View.EdgeMode != 2)
		{
			// Faint (default) and Off: every relation is a thin neutral line (or nothing), so the voters, their rings and the few lines that matter stand out. Colour,
			// strength and travelling pulses are kept for the hovered voter's relations and for the pushes on a voter about to flip back.
			if (!bHot)
			{
				EdgeCol = FLinearColor(0.32f, 0.32f, 0.42f);
				Base = View.EdgeMode == 1 ? 0.0f : QuietEdgeAlpha;
				Pulse = 0.0f;
				Wid = EdgeHalfWidth * 0.6f;
			}
			if (bPush)
			{
				EdgeCol = ColorOfOwner((PushOnFollower >= 0 && Pressure_[E.Follower] > PressureEdgeMin && Sim.Opinion(E.Influencer) == PushOnFollower) ? PushOnFollower : PushOnInfluencer);
				Base = 0.95f;
				Pulse = 0.9f;
				Wid = EdgeHalfWidth + 1.0f;
			}
		}
		// A Rally that just went off: teal lines from the rallied voter to the voters it shields.
		if (View.RallyNode >= 0 && (E.Influencer == View.RallyNode || E.Follower == View.RallyNode) && Sim.ShieldRemaining(E.Influencer) > 0.0 && Sim.ShieldRemaining(E.Follower) > 0.0)
		{
			EdgeCol = FLinearColor(0.15f, 0.85f, 0.75f);
			Base = 0.95f;
			Pulse = 0.9f;
			Wid = EdgeHalfWidth + 1.0f;
		}
		const FVector2D P0 = NodeWorld_[E.Influencer];
		const FVector2D P1 = NodeWorld_[E.Follower];
		const float EdgeData[EdgeFloats] = {
			EdgeCol.R, EdgeCol.G, EdgeCol.B, Base, FMath::Min(Pulse, 1.0f), Hash01(E.Follower, E.Influencer), 0.35f,
			Wid,
			static_cast<float>(P0.X), static_cast<float>(P0.Y), static_cast<float>(P1.X), static_cast<float>(P1.Y),
			EdgeBulgeFraction * EdgeLen_[e] };
		EdgeISM->SetCustomData(e, MakeArrayView(EdgeData, EdgeFloats), false);
	}

	EdgeISM->MarkRenderStateDirty();
	GlowISM->MarkRenderStateDirty();
	NodeISM->MarkRenderStateDirty();
	ApplyTheme(View);
}
