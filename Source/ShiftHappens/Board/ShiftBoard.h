// Rate for Rate - Game Off 2026

#pragma once

#include "Components/InstancedStaticMeshComponent.h"
#include "Containers/Array.h"
#include "GameFramework/Actor.h"
#include "HAL/Platform.h"
#include "Materials/MaterialParameterCollection.h"
#include "Math/MathFwd.h"
#include "Sim/ShiftSim.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "ShiftBoard.generated.h"

/** Presentation state that is not part of the simulation. */
struct FShiftBoardView
{
	FVector2D CursorWorld = FVector2D::ZeroVector;   // world XY of the mouse cursor
	int32 HoverNode = -1;
	float Time = 0.0f;
	float Night = 0.0f;                              // 0 day .. 1 night
	float Heat = 0.0f;                               // 0..1
	float Beat = 0.0f;                               // 0..1 pulse on the music beat
	int32 EdgeMode = 0;                              // relation lines: 0 faint, 1 off, 2 full (the old look)
	int32 RallyNode = -1;                            // a Rally just went off here: teal lines to the voters it shields (-1 none)
	int32 Mood = -1;                                 // the jam's look: its background pattern and its ink and night colours (-1 none: the dots of the title; the day paper is the same in every jam)
	float BackgroundOpacity = 1.0f;                  // the paper behind the board, 0..1; below 1 only in the see-through window (the desktop shows through the rest)
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

	/** A parried attack: the ring in the attacker's colour (Who) is flung back out from where it was (FromFrac: 1 = just announced, 0 = about to land) for a third of a second. */
	void BounceNode(int32 Node, int32 Who, float FromFrac);

	int32 NodeCount() const { return NodeWorld_.Num(); }
	int32 EdgeCount() const { return Edges_.Num(); }

private:
	struct FEdgeRec
	{
		int32 Follower = -1;     // the node that listens
		int32 Influencer = -1;   // the node being listened to (influence flows from here to the follower)
	};

	void ApplyTheme(const FShiftBoardView& View) const;

	/** In the see-through window: the level's paper plane becomes the translucent M_BgGlass, drawn behind everything else. */
	void UseGlassBackground();

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
	TArray<float> Bounce_;       // 1 -> 0 over 0.35 s after a parry
	TArray<float> BounceFrom_;
	TArray<int32> BounceWho_;
	TArray<float> Pressure_;   // 0..1: how close a voter of the player is to flipping back (smoothed), drawn as a shake and an orange ring
	TArray<int32> PrevOwner_;
	TArray<FEdgeRec> Edges_;
	TArray<float> EdgeLen_;
	float LayoutW_ = 1280.0f;
	float LayoutH_ = 720.0f;
};
