// Shift Happens - Game Off 2026

#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Delegates/DelegateCombinations.h"
#include "HAL/Platform.h"
#include "Input/CursorReply.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Layout/Geometry.h"
#include "Layout/SlateRect.h"
#include "Math/Color.h"
#include "Math/MathFwd.h"
#include "Rendering/DrawElements.h"
#include "Styling/WidgetStyle.h"
#include "Templates/UnrealTemplate.h"
#include "Types/PaintArgs.h"
#include "UI/ShiftHudModel.h"
#include "UI/ShiftPaint.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

DECLARE_DELEGATE_ThreeParams(FOnShiftHudClick, const FVector2D& /*LocalPos*/, bool /*bShift*/, bool /*bRightButton*/);
DECLARE_DELEGATE_TwoParams(FOnShiftHudUi, int32 /*EShiftUi::Id*/, float /*Value01 for sliders, else 0*/);
DECLARE_DELEGATE_OneParam(FOnShiftHudKey, const FKey& /*Key*/);
DECLARE_DELEGATE_OneParam(FOnShiftHudPan, const FVector2f& /*Delta in design pixels: the board follows the mouse*/);

/** Where a part of the interface is docked: the design rect is placed so that its point at this anchor lands on the same point of the window. */
enum class EShiftDock : uint8
{
	TL, T, TR,
	L, C, R,
	BL, B, BR,
};

/**
 * All of the game's 2D interface in one custom-painted widget: title, jam select, build screen, and the play HUD with its pause and
 * results overlays. It fills the game viewport and records the buttons it paints as hit regions, and forwards clicks on them as UI actions.
 * Clicks that miss every region on the play screen go to the player controller as board clicks.
 *
 * Layout: everything is drawn in 1280 x 720 design units at one scale, the largest that fits the window, so a part never changes its shape.
 * The window is not letterboxed: the camera shows as much paper as the window is wide, and each part of the interface is DOCKED to one of the
 * nine anchors of the window (EShiftDock: corners, edge middles, centre) and keeps its design distance from it. At 16:9 all docks are the same
 * place and the picture is the classic one; in a wider window the left parts stay at the left edge and the right parts at the right one, in a
 * taller window the top and bottom bars stay at the top and bottom, and the bars themselves stretch to the window's edges (WindowRect).
 */
class SShiftHud : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SShiftHud) {}
		SLATE_EVENT(FOnShiftHudClick, OnClick)
		SLATE_EVENT(FOnShiftHudUi, OnUi)
		SLATE_EVENT(FOnShiftHudKey, OnKey)
		SLATE_EVENT(FOnShiftHudPan, OnPan)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetModel(FShiftHudModel&& InModel) { Model = MoveTemp(InModel); }

	/** Converts a local widget position to design units (1280 x 720) of the centre dock: the frame of the board, the voters and the cursor. */
	FVector2f LocalToDesign(const FGeometry& Geo, const FVector2f& Local) const;

	/** Converts design units of the centre dock to a local widget position (the inverse of LocalToDesign), using the geometry of the last frame. */
	FVector2f DesignToLocal(const FVector2f& Design) const;

	/** A click ring at a design-unit position with a short press on the cursor. Kind: 0 = plug (teal), 1 = sockpuppet (violet), 2 = Rally (amber). Used for the real mouse and for the scripted cursor. */
	void PushRipple(const FVector2f& Design, int32 Kind);

	/** QA: injects a mouse event at a design-unit position. Kind: 0 = move, 1 = left button down, 2 = left button up. */
	void QaMouse(int32 Kind, const FVector2f& Design);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	struct FPal
	{
		float N = 0.0f;
		FLinearColor Ink;
		FLinearColor InkDim;
		FLinearColor Panel;
		FLinearColor PanelSolid;
		FLinearColor Stroke;
		FLinearColor Track;
		FLinearColor Teal;
		FLinearColor Red;
		FLinearColor Amber;
		FLinearColor Violet;
	};

	/** A clickable region, in local widget pixels (it was painted in some dock, and the docks differ). */
	struct FHit
	{
		float X = 0.0f;
		float Y = 0.0f;
		float W = 0.0f;
		float H = 0.0f;
		int32 Id = -1;
		bool bSlider = false;
		float SliderX0 = 0.0f;   // the track of a slider, local pixels
		float SliderX1 = 1.0f;

		bool Contains(const FVector2f& P) const { return P.X >= X && P.X <= X + W && P.Y >= Y && P.Y <= Y + H; }
	};

	struct FRowAnim
	{
		float Y = 0.0f;
		float Share = 0.0f;
		bool bInit = false;
	};

	/** Where a dock puts the design rect, in local pixels: the origin of design (0, 0) and the scale. Set at the start of every paint. */
	struct FFrame
	{
		FVector2f Origin = FVector2f::ZeroVector;
		float Scale = 1.0f;
	};

	/** Switches the painter and the hit regions that follow to a dock: what is painted and added from here on uses that dock's design units. */
	void Dock(FShiftPainter& P, EShiftDock D) const;
	/** The window's edges in the painter's current dock, in its design units: left, top, right, bottom (0, 0, 1280, 720 at 16:9). The bars and washes stretch to them. */
	FVector4f WindowRect(const FShiftPainter& P) const;
	/** A rectangle over the whole window in the current dock (washes). */
	void FillWindow(FShiftPainter& P, const FLinearColor& Color) const;
	/** A hit region over the whole window (a click anywhere). */
	void AddWindowHit(const FShiftPainter& P, int32 Id) const;
	/** Where the design point (0, 0) of a dock lands in the design units of the centre dock: add it to a rectangle painted in that dock to place it in the centre one. */
	FVector2f DockShift(EShiftDock D) const;

	/** Records a hit region given in the current dock's design units (later regions win); returns whether the mouse is over it. */
	bool AddHit(float X, float Y, float W, float H, int32 Id, bool bSlider = false, float SliderX0 = 0.0f, float SliderX1 = 1.0f) const;
	FLinearColor Scaled(FLinearColor Wash) const;   // a full-screen wash, faded by the opacity of the see-through window
	float Solid(float Alpha) const;                  // a panel's alpha in the see-through window: more solid, so the desktop behind does not blend into the text on it
	const FHit* FindHit(const FVector2f& Local) const;
	static float SliderValue(const FHit& Hit, float LocalX);

	/** Pill button with hover; returns whether the mouse is over it. */
	bool Button(FShiftPainter& P, const FPal& C, int32 Id, float X, float Y, float W, float H, const FString& Label, bool bPrimary, float Px = 16.0f) const;
	/** The gear button in the top right corner; it opens the settings menu (PaintSettings). A red dot on it means the sound is off. */
	void SettingsButton(FShiftPainter& P, const FPal& C, float Cx, float Cy) const;
	/** The settings menu dropped from the gear: sound on / off and the window mode. Painted last; it makes everything behind it inert. */
	void PaintSettings(FShiftPainter& P, const FPal& C) const;

	void PaintTitle(FShiftPainter& P, const FPal& C) const;
	void PaintJamSelect(FShiftPainter& P, const FPal& C) const;
	void PaintBuild(FShiftPainter& P, const FPal& C) const;
	void PaintPlay(FShiftPainter& P, const FPal& C) const;
	void PaintTooltip(FShiftPainter& P, const FPal& C) const;
	void PaintResults(FShiftPainter& P, const FPal& C) const;
	void PaintPause(FShiftPainter& P, const FPal& C) const;
	void PaintLesson(FShiftPainter& P, const FPal& C) const;
	void PaintHowTo(FShiftPainter& P, const FPal& C) const;
	void PaintExtras(FShiftPainter& P, const FPal& C) const;
	void PaintScatter(FShiftPainter& P, const FPal& C, float X, float Y, float W, float H, const FString& Title, const TArray<FShiftHudDot>& Dots,
		const FShiftHudFit& Fit, const FString& Caption) const;
	void PaintKeycap(FShiftPainter& P, const FPal& C, float X, float Y, float S, bool bPressed) const;
	void PaintLogo(FShiftPainter& P, const FPal& C, float X, float Y, float Px, bool bPressed) const;
	void MenuRow(FShiftPainter& P, const FPal& C, int32 Id, int32 Rank, float Y, const FString& Label, const FString& KeyHint, int32 EntryColor, float Bar, bool bNudge) const;
	/** The game's own cursor and the click rings, painted last so they sit above everything, the hover card included. */
	void PaintCursor(FShiftPainter& P, const FPal& C) const;
#if !UE_BUILD_SHIPPING
	/** The scripted trailer run (not in Shipping builds): spotlight, cover and flash, "SHIFT split" slams and lower-left captions (Trailer/ShiftTrailer.h). */
	void PaintTrailer(FShiftPainter& P, const FPal& C) const;
#endif

	struct FRipple
	{
		FVector2f Pos = FVector2f::ZeroVector;
		float Start = 0.0f;
		int32 Kind = 0;
	};

	FShiftHudModel Model;
	FOnShiftHudClick OnClick;
	FOnShiftHudUi OnUi;
	FOnShiftHudKey OnKey;
	FOnShiftHudPan OnPan;
	bool bQaDriven = false;                // -ShiftMouse is in use: real pointer moves are ignored for the pan
	mutable bool bPanning = false;         // dragging the board (the middle button, or the right button once it moved)
	mutable bool bRightPending = false;    // the right button went down on empty space: a Rally if it comes up where it went down, a pan once it moves
	mutable FVector2f RightStart = FVector2f::ZeroVector;
	mutable FVector2f PanLast = FVector2f::ZeroVector;

	mutable TArray<FHit> Hits;
	mutable bool bModal = false;
	mutable TMap<int32, FRowAnim> RowAnims;
	TMap<int32, FRowAnim> BuildAnims;      // the build screen's two lists: key = list * 32 + entry id, Y in rows (list 0 = quality, 1 = the vote forecast)
	float ForecastFade = 0.0f;             // 0..1: the vote forecast fading in once its plays are in

	mutable FFrame Frames[9];              // the nine docks (EShiftDock), set by OnPaint
	mutable FVector2f WindowSize = FVector2f(1280.0f, 720.0f);
	mutable FVector2f HitOrigin = FVector2f::ZeroVector;   // the dock AddHit reads its rectangles in
	mutable float HitScale = 1.0f;
	mutable FVector2f MouseLocal = FVector2f(-10000.0f, -10000.0f);   // the real mouse, local pixels: what the hit regions are tested against
	mutable FVector2f MouseDesign = FVector2f(-1000.0f, -1000.0f);    // the same in the centre dock's design units: what the drawn cursor uses
	mutable bool bMouseInside = false;     // the real mouse is over the window: the game's cursor is drawn at MouseDesign
	bool bQaInjecting = false;             // QaMouse is calling the handlers: those events draw no cursor and no ring
	int32 DragId = -1;
	FHit DragHit;
	float AnimTime = 0.0f;
	float PulseTime = 0.0f;                // AnimTime, or 0 while Reduce motion is on: every pulse reads it
	float PressUntil = -1.0f;              // AnimTime until which the cursor is drawn pressed
	TArray<FRipple> Ripples;
};
