// Shift Happens - Game Off 2026

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShiftBoard.generated.h"

namespace ShiftSim { class Sim; }

class UInstancedStaticMeshComponent;
class UMaterialParameterCollection;

/** Presentation state that is not part of the simulation. */
struct FShiftBoardView
{
	FVector2D CursorWorld = FVector2D::ZeroVector;   // world XY of the mouse cursor
	int32 HoverNode = -1;
	float Time = 0.0f;
	float Night = 0.0f;                              // 0 day .. 1 night
	float Heat = 0.0f;                               // 0..1
	float Beat = 0.0f;                               // 0..1 pulse on the music beat
};

/**
 * Draws the follower network in the world: one instanced quad per voter (SDF material M_Node), one per halo (M_Glow) and one
 * per directed follow edge (M_Edge). World units are design pixels (1280 x 720); the camera looks straight down with screen
 * right = +Y and screen up = +X, so sim (x right, y down) maps to world (X = H/2 - y, Y = x - W/2).
 */
UCLASS()
class SHIFTHAPPENS_API AShiftBoard : public AActor
{
	GENERATED_BODY()

public:
	AShiftBoard();

	/** Builds every instance from the sim's graph. Call once after the sim is initialised. */
	void Build(const ShiftSim::Sim& Sim);

	/** Pushes the current sim state and view state into the instance custom data and the theme collection. */
	void Update(const ShiftSim::Sim& Sim, const FShiftBoardView& View, float DeltaTime);

	FVector2D SimToWorld(double SimX, double SimY) const;
	FVector2D NodeWorld(int32 Node) const { return NodeWorld_.IsValidIndex(Node) ? NodeWorld_[Node] : FVector2D::ZeroVector; }
	float NodeRadius(int32 Node) const { return NodeRadii_.IsValidIndex(Node) ? NodeRadii_[Node] : 0.0f; }

	/** Nearest node whose body (scaled by Slack) contains the world point, or -1. */
	int32 NodeAtWorld(const FVector2D& World, float Slack = 1.15f) const;

	/** Starts the flip pulse on a node. Called automatically when a node's owner changes between updates. */
	void FlashNode(int32 Node);

	int32 NodeCount() const { return NodeWorld_.Num(); }
	int32 EdgeCount() const { return Edges_.Num(); }

private:
	struct FEdgeRec
	{
		int32 Follower = -1;     // the node that listens
		int32 Influencer = -1;   // the node being listened to (influence flows from here to the follower)
	};

	void ApplyTheme(const FShiftBoardView& View) const;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> EdgeISM;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> GlowISM;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> NodeISM;

	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> ThemeMPC;

	TArray<FVector2D> NodeWorld_;
	TArray<float> NodeRadii_;
	TArray<float> Flash_;
	TArray<float> Hover_;
	TArray<int32> PrevOwner_;
	TArray<FEdgeRec> Edges_;
	TArray<float> EdgeLen_;
	float LayoutW_ = 1280.0f;
	float LayoutH_ = 720.0f;
};
