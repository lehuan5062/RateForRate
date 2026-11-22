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

/**
 * All of the game's 2D interface in one custom-painted widget: title, jam select, build screen, and the play HUD with its pause and
 * results overlays. It fills the game viewport, draws inside the largest 16:9 rectangle (matching the letterboxed camera), records
 * the buttons it paints as hit regions, and forwards clicks on them as UI actions. Clicks that miss every region on the play screen
 * go to the player controller as board clicks.
 */
class SShiftHud : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SShiftHud) {}
		SLATE_EVENT(FOnShiftHudClick, OnClick)
		SLATE_EVENT(FOnShiftHudUi, OnUi)
		SLATE_EVENT(FOnShiftHudKey, OnKey)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetModel(FShiftHudModel&& InModel) { Model = MoveTemp(InModel); }

	/** Converts a local widget position to design units (1280 x 720). */
	FVector2f LocalToDesign(const FGeometry& Geo, const FVector2f& Local) const;

	/** QA: injects a mouse event at a design-unit position. Kind: 0 = move, 1 = left button down, 2 = left button up. */
	void QaMouse(int32 Kind, const FVector2f& Design);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
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

	struct FHit
	{
		float X = 0.0f;
		float Y = 0.0f;
		float W = 0.0f;
		float H = 0.0f;
		int32 Id = -1;
		bool bSlider = false;
		float SliderX0 = 0.0f;
		float SliderX1 = 1.0f;

		bool Contains(const FVector2f& P) const { return P.X >= X && P.X <= X + W && P.Y >= Y && P.Y <= Y + H; }
	};

	struct FRowAnim
	{
		float Y = 0.0f;
		float Share = 0.0f;
		bool bInit = false;
	};

	/** Records a hit region (later regions win); returns whether the mouse is over it. */
	bool AddHit(float X, float Y, float W, float H, int32 Id, bool bSlider = false, float SliderX0 = 0.0f, float SliderX1 = 1.0f) const;
	const FHit* FindHit(const FVector2f& Design) const;
	static float SliderValue(const FHit& Hit, float DesignX);

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
	void PaintScatter(FShiftPainter& P, const FPal& C, float X, float Y, float W, float H, const FString& Title, const TArray<FShiftHudDot>& Dots,
		const FShiftHudFit& Fit, const FString& Caption) const;
	void PaintKeycap(FShiftPainter& P, const FPal& C, float Cx, float Cy, bool bPressed) const;

	FShiftHudModel Model;
	FOnShiftHudClick OnClick;
	FOnShiftHudUi OnUi;
	FOnShiftHudKey OnKey;

	mutable TArray<FHit> Hits;
	mutable bool bModal = false;
	mutable TMap<int32, FRowAnim> RowAnims;
	TMap<int32, FRowAnim> BuildAnims;      // the build screen's two lists: key = list * 32 + entry id, Y in rows (list 0 = quality, 1 = the vote forecast)
	float ForecastFade = 0.0f;             // 0..1: the vote forecast fading in once its plays are in

	FVector2f MouseDesign = FVector2f(-1000.0f, -1000.0f);
	int32 DragId = -1;
	FHit DragHit;
	float AnimTime = 0.0f;
};
