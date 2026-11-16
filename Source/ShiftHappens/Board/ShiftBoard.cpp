// Shift Happens - Game Off 2026

#include "Board/ShiftBoard.h"
#include "Board/ShiftPalette.h"
#include "ShiftHappens.h"
#include "Sim/ShiftSim.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

namespace
{
	// Custom-data layouts: keep in sync with Tools/py/build_materials.py.
	constexpr int32 NodeFloats = 15;
	constexpr int32 GlowFloats = 9;
	constexpr int32 EdgeFloats = 13;

	constexpr float EdgeZ = 0.0f;
	constexpr float GlowZ = 1.0f;
	constexpr float NodeZ = 2.0f;

	constexpr float PlaneSize = 100.0f;     // /Engine/BasicShapes/Plane is 100 x 100 units
	constexpr float NodeQuadHalf = 2.1f;    // node quad half size, in body radii (node.hlsl draws up to ~1.9)
	constexpr float GlowQuadHalf = 3.4f;    // glow quad half size, in body radii (glow.hlsl fades out by 3.4)
	constexpr float EdgeHalfWidth = 1.4f;   // world units
	constexpr float EdgeMargin = 6.0f;
	constexpr float EdgeBulgeFraction = 0.14f;

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

		const float NodeData[NodeFloats] = {
			Fill.R, Fill.G, Fill.B, Conv, Lock01, Gate, Hover_[i], Flash_[i],
			static_cast<float>(Graph.nodes[i].role), Expr, Sock, 0.0f,
			static_cast<float>(NodeWorld_[i].X), static_cast<float>(NodeWorld_[i].Y), NodeRadii_[i] };
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
		const float Base = bHot ? 0.85f : (OwnerId >= 0 ? 0.14f + 0.14f * ConvJ : 0.07f);
		const float Pulse = (OwnerId >= 0 ? 0.30f + 0.70f * ConvJ : 0.0f) + (bHot ? 0.35f : 0.0f);
		const FVector2D P0 = NodeWorld_[E.Influencer];
		const FVector2D P1 = NodeWorld_[E.Follower];
		const float EdgeData[EdgeFloats] = {
			Col.R, Col.G, Col.B, Base, FMath::Min(Pulse, 1.0f), Hash01(E.Follower, E.Influencer), 0.35f,
			bHot ? EdgeHalfWidth + 0.9f : EdgeHalfWidth,
			static_cast<float>(P0.X), static_cast<float>(P0.Y), static_cast<float>(P1.X), static_cast<float>(P1.Y),
			EdgeBulgeFraction * EdgeLen_[e] };
		EdgeISM->SetCustomData(e, MakeArrayView(EdgeData, EdgeFloats), false);
	}

	EdgeISM->MarkRenderStateDirty();
	GlowISM->MarkRenderStateDirty();
	NodeISM->MarkRenderStateDirty();
	ApplyTheme(View);
}
