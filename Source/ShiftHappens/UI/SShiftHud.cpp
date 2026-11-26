// Rate for Rate - Game Off 2026

#include "UI/SShiftHud.h"

#include "Board/ShiftPalette.h"
#include <cfloat>
#include "Containers/Array.h"
#include "Containers/SparseSet.h"
#include "Containers/UnrealString.h"
#include "GenericPlatform/GenericApplication.h"
#include "GenericPlatform/ICursor.h"
#include "HAL/Platform.h"
#include "Input/CursorReply.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Layout/Geometry.h"
#include "Layout/SlateRect.h"
#include "Math/Color.h"
#include "Math/MathFwd.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/Build.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "ShiftHudModel.h"
#include "Styling/WidgetStyle.h"
#include "Templates/TupleFwd.h"
#include "Types/PaintArgs.h"
#include "UI/ShiftPaint.h"

namespace
{
	constexpr float DesignW = 1280.0f;
	constexpr float DesignH = 720.0f;
	constexpr float RowHeight = 31.0f;
	constexpr float CompactRowH = 16.0f;
	constexpr float CompactLeaderboardW = 186.0f;
	constexpr float RippleSeconds = 0.55f;

	/** sRGB hex to the linear colour Slate expects. */
	FLinearColor Hex(uint32 Rgb, float Alpha = 1.0f)
	{
		FLinearColor C = FLinearColor::FromSRGBColor(FColor(static_cast<uint8>((Rgb >> 16) & 0xFF), static_cast<uint8>((Rgb >> 8) & 0xFF), static_cast<uint8>(Rgb & 0xFF)));
		C.A = Alpha;
		return C;
	}

	FLinearColor Mix(const FLinearColor& A, const FLinearColor& B, float T)
	{
		return A + (B - A) * FMath::Clamp(T, 0.0f, 1.0f);
	}

	FLinearColor WithAlpha(FLinearColor C, float Alpha)
	{
		C.A = Alpha;
		return C;
	}

	FString Compact(float Votes)
	{
		return Votes >= 1000.0f ? FString::Printf(TEXT("%.1fk"), Votes / 1000.0f) : FString::Printf(TEXT("%.0f"), Votes);
	}

	// the MODS countdown of the top bar: an icon at (ModsIconX, ModsIconY) and a text to its right; the trailer's spotlight is fitted to the same numbers
	constexpr float ModsIconX = 900.0f;
	constexpr float ModsIconY = 20.0f;
	constexpr float ModsIconR = 9.5f;
	constexpr float ModsTextX = 916.0f;
	constexpr float ModsTextY = 13.0f;
	constexpr float ModsTextPx = 12.0f;

	FString ModsLabel(const FShiftHudModel& M)
	{
		return M.EyeAwayLeft > 0.0f ? FString::Printf(TEXT("MODS AWAY %.0fs"), M.EyeAwayLeft) : FString::Printf(TEXT("MODS %.0fs"), M.NextSweepIn);
	}

	/** The shape of an entry, so ownership does not rest on colour alone: disc, triangle up, ring, square, triangle down, plus, hollow square, diamond, cross. */
	void DrawMarker(FShiftPainter& P, int32 Id, float Cx, float Cy, float R)
	{
		const FLinearColor Dark(0.07f, 0.06f, 0.14f, 1.0f);
		const FLinearColor Light(1.0f, 1.0f, 1.0f, 1.0f);
		const int32 Kind = Id % 9;
		auto Shape = [&](float Rr, const FLinearColor& Col)
			{
				switch (Kind)
				{
				case 0:
				case 2:
					P.Disc(Cx, Cy, Rr, Col);
					break;
				case 1:
					P.FillPolygon({ FVector2f(Cx, Cy - Rr), FVector2f(Cx + Rr, Cy + Rr * 0.8f), FVector2f(Cx - Rr, Cy + Rr * 0.8f) }, Col);
					break;
				case 3:
				case 6:
					P.Rect(Cx - Rr, Cy - Rr, Rr * 2.0f, Rr * 2.0f, Col, 0.0f);
					break;
				case 4:
					P.FillPolygon({ FVector2f(Cx - Rr, Cy - Rr * 0.8f), FVector2f(Cx + Rr, Cy - Rr * 0.8f), FVector2f(Cx, Cy + Rr) }, Col);
					break;
				case 5:
					P.Rect(Cx - Rr, Cy - Rr * 0.34f, Rr * 2.0f, Rr * 0.68f, Col, 0.0f);
					P.Rect(Cx - Rr * 0.34f, Cy - Rr, Rr * 0.68f, Rr * 2.0f, Col, 0.0f);
					break;
				case 7:
					P.FillPolygon({ FVector2f(Cx, Cy - Rr * 1.2f), FVector2f(Cx + Rr * 1.1f, Cy), FVector2f(Cx, Cy + Rr * 1.2f), FVector2f(Cx - Rr * 1.1f, Cy) }, Col);
					break;
				default:
					P.Line(FVector2f(Cx - Rr, Cy - Rr), FVector2f(Cx + Rr, Cy + Rr), Col, Rr * 0.8f);
					P.Line(FVector2f(Cx - Rr, Cy + Rr), FVector2f(Cx + Rr, Cy - Rr), Col, Rr * 0.8f);
					break;
				}
			};
		Shape(R + 1.8f, Dark);
		Shape(R, Light);
		if (Kind == 2 || Kind == 6)
		{
			Shape(R * 0.45f, Dark);
		}
	}

	FString HudOrdinal(int32 N)
	{
		const int32 Mod100 = N % 100;
		const TCHAR* Suffix = TEXT("th");
		if (Mod100 < 11 || Mod100 > 13)
		{
			switch (N % 10)
			{
			case 1: Suffix = TEXT("st"); break;
			case 2: Suffix = TEXT("nd"); break;
			case 3: Suffix = TEXT("rd"); break;
			default: break;
			}
		}
		return FString::Printf(TEXT("%d%s"), N, Suffix);
	}
}

void SShiftHud::Construct(const FArguments& InArgs)
{
	OnClick = InArgs._OnClick;
	OnUi = InArgs._OnUi;
	OnKey = InArgs._OnKey;
	OnPan = InArgs._OnPan;
}

FVector2D SShiftHud::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(DesignW, DesignH);
}

FVector2f SShiftHud::LocalToDesign(const FGeometry& Geo, const FVector2f& Local) const
{
	const FVector2f Size = FVector2f(Geo.GetLocalSize());
	const float Scale = FMath::Max(0.01f, FMath::Min(Size.X / DesignW, Size.Y / DesignH));
	const FVector2f Origin = (Size - FVector2f(DesignW, DesignH) * Scale) * 0.5f;
	return (Local - Origin) / Scale;
}

void SShiftHud::QaMouse(int32 Kind, const FVector2f& Design)
{
	const FGeometry Geo = GetCachedGeometry();
	const FVector2f Size = FVector2f(Geo.GetLocalSize());
	const float Scale = FMath::Max(0.01f, FMath::Min(Size.X / DesignW, Size.Y / DesignH));
	const FVector2f Origin = (Size - FVector2f(DesignW, DesignH) * Scale) * 0.5f;
	const FVector2f Absolute = FVector2f(Geo.LocalToAbsolute(FVector2D(Origin + Design * Scale)));

	const bool bRight = Kind >= 3;
	const FKey Button = bRight ? EKeys::RightMouseButton : EKeys::LeftMouseButton;
	TSet<FKey> Pressed;
	if (Kind != 2 && Kind != 4)
	{
		Pressed.Add(Button);
	}
	const FPointerEvent Event(0, Absolute, Absolute, Pressed, Button, 0.0f, FModifierKeysState());
	bQaInjecting = true;
	bQaDriven = true;
	if (Kind == 0 || Kind == 5)
	{
		OnMouseMove(Geo, Event);
	}
	else if (Kind == 1 || Kind == 3)
	{
		OnMouseButtonDown(Geo, Event);
	}
	else
	{
		OnMouseButtonUp(Geo, Event);
	}
	bQaInjecting = false;
}

FVector2f SShiftHud::DesignToLocal(const FVector2f& Design) const
{
	const FVector2f Size = FVector2f(GetCachedGeometry().GetLocalSize());
	const float Scale = FMath::Max(0.01f, FMath::Min(Size.X / DesignW, Size.Y / DesignH));
	const FVector2f Origin = (Size - FVector2f(DesignW, DesignH) * Scale) * 0.5f;
	return Origin + Design * Scale;
}

void SShiftHud::PushRipple(const FVector2f& Design, int32 Kind)
{
	FRipple Ripple;
	Ripple.Pos = Design;
	Ripple.Start = AnimTime;
	Ripple.Kind = Kind;
	if (Ripples.Num() >= 8)
	{
		Ripples.RemoveAt(0);
	}
	Ripples.Add(Ripple);
	PressUntil = AnimTime + 0.12f;
}

void SShiftHud::Dock(FShiftPainter& P, EShiftDock D) const
{
	const FFrame& F = Frames[static_cast<int32>(D)];
	P.Origin = F.Origin;
	P.Scale = F.Scale;
	HitOrigin = F.Origin;
	HitScale = F.Scale;
}

FVector4f SShiftHud::WindowRect(const FShiftPainter& P) const
{
	return FVector4f(-P.Origin.X / P.Scale, -P.Origin.Y / P.Scale, (WindowSize.X - P.Origin.X) / P.Scale, (WindowSize.Y - P.Origin.Y) / P.Scale);
}

void SShiftHud::FillWindow(FShiftPainter& P, const FLinearColor& Color) const
{
	const FVector4f R = WindowRect(P);
	P.Rect(R.X, R.Y, R.Z - R.X, R.W - R.Y, Color);
}

void SShiftHud::AddWindowHit(const FShiftPainter& P, int32 Id) const
{
	const FVector4f R = WindowRect(P);
	AddHit(R.X, R.Y, R.Z - R.X, R.W - R.Y, Id);
}

FVector2f SShiftHud::DockShift(EShiftDock D) const
{
	const FFrame& From = Frames[static_cast<int32>(D)];
	const FFrame& Centre = Frames[static_cast<int32>(EShiftDock::C)];
	return (From.Origin - Centre.Origin) / FMath::Max(0.01f, Centre.Scale);
}

bool SShiftHud::AddHit(float X, float Y, float W, float H, int32 Id, bool bSlider, float SliderX0, float SliderX1) const
{
	FHit Hit;
	Hit.X = HitOrigin.X + X * HitScale;
	Hit.Y = HitOrigin.Y + Y * HitScale;
	Hit.W = W * HitScale;
	Hit.H = H * HitScale;
	Hit.Id = Id;
	Hit.bSlider = bSlider;
	Hit.SliderX0 = HitOrigin.X + SliderX0 * HitScale;
	Hit.SliderX1 = HitOrigin.X + SliderX1 * HitScale;
	Hits.Add(Hit);
	return Hit.Contains(MouseLocal);
}

const SShiftHud::FHit* SShiftHud::FindHit(const FVector2f& Local) const
{
	for (int32 i = Hits.Num() - 1; i >= 0; --i)
	{
		if (Hits[i].Contains(Local))
		{
			return &Hits[i];
		}
	}
	return nullptr;
}

float SShiftHud::SliderValue(const FHit& Hit, float LocalX)
{
	return FMath::Clamp((LocalX - Hit.SliderX0) / FMath::Max(1.0f, Hit.SliderX1 - Hit.SliderX0), 0.0f, 1.0f);
}

FReply SShiftHud::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FKey Button = MouseEvent.GetEffectingButton();
	if (Button == EKeys::MiddleMouseButton)
	{
		if (bModal)
		{
			return FReply::Unhandled();
		}
		PanLast = LocalToDesign(MyGeometry, FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition())));
		bPanning = true;
		return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse).CaptureMouse(SharedThis(this));
	}
	if (Button != EKeys::LeftMouseButton && Button != EKeys::RightMouseButton)
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2f Design = LocalToDesign(MyGeometry, FVector2f(Local));
	MouseDesign = Design;
	MouseLocal = FVector2f(Local);
	// the right button on empty space (no voter under the cursor, no button): a pan if it moves, a Rally click if it does not
	const bool bEmptyRight = Button == EKeys::RightMouseButton && !bModal && !Model.Tooltip.bVisible && FindHit(MouseLocal) == nullptr;
	if (!bQaInjecting)
	{
		bMouseInside = true;
		if (!Model.bSystemCursor && !bEmptyRight)
		{
			PushRipple(Design, Button == EKeys::RightMouseButton ? 2 : ((Model.bNightActive || MouseEvent.IsShiftDown()) ? 1 : 0));
		}
	}
	if (bEmptyRight)
	{
		bRightPending = true;
		RightStart = Design;
		PanLast = Design;
		return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse).CaptureMouse(SharedThis(this));
	}

	FReply Reply = FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	if (Button == EKeys::LeftMouseButton)
	{
		if (const FHit* Hit = FindHit(MouseLocal))
		{
			if (Hit->bSlider)
			{
				DragId = Hit->Id;
				DragHit = *Hit;
				OnUi.ExecuteIfBound(Hit->Id, SliderValue(*Hit, MouseLocal.X));
				return Reply.CaptureMouse(SharedThis(this));
			}
			OnUi.ExecuteIfBound(Hit->Id, 0.0f);
			return Reply;
		}
	}
	if (!bModal)
	{
		OnClick.ExecuteIfBound(Local, MouseEvent.IsShiftDown(), Button == EKeys::RightMouseButton);
	}
	return Reply;
}

FReply SShiftHud::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bPanning && MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		bPanning = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (bRightPending && MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		const bool bWasPan = bPanning;
		bRightPending = false;
		bPanning = false;
		if (!bWasPan)
		{
			const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
			if (!bQaInjecting && !Model.bSystemCursor)
			{
				PushRipple(LocalToDesign(MyGeometry, FVector2f(Local)), 2);
			}
			OnClick.ExecuteIfBound(Local, MouseEvent.IsShiftDown(), true);
		}
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (DragId >= 0 && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		DragId = -1;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SShiftHud::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	MouseDesign = LocalToDesign(MyGeometry, FVector2f(Local));
	MouseLocal = FVector2f(Local);
	bMouseInside = bMouseInside || !bQaInjecting;
	if (bQaDriven && !bQaInjecting)
	{
		return FReply::Unhandled();   // a scripted run: the real (headless) pointer must not fight the scripted one
	}
	if (bRightPending && !bPanning && FVector2f::Distance(MouseDesign, RightStart) > 6.0f)
	{
		bPanning = true;   // far enough to be a drag, not a click
	}
	if (bPanning)
	{
		OnPan.ExecuteIfBound(MouseDesign - PanLast);
		PanLast = MouseDesign;
		return FReply::Handled();
	}
	if (DragId >= 0)
	{
		OnUi.ExecuteIfBound(DragHit.Id, SliderValue(DragHit, MouseLocal.X));
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SShiftHud::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	if (DragId < 0)
	{
		bMouseInside = false;
	}
}

void SShiftHud::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	DragId = -1;
	bPanning = false;
	bRightPending = false;
}

FCursorReply SShiftHud::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (!Model.bSystemCursor)
	{
		// The game draws its own cursor (PaintCursor); the operating system's arrow is hidden over the window. The query also tells us the mouse is here, even before it moves.
		const FVector2D LocalPos = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition());
		MouseDesign = LocalToDesign(MyGeometry, FVector2f(LocalPos));
		MouseLocal = FVector2f(LocalPos);
		bMouseInside = true;
		return FCursorReply::Cursor(EMouseCursor::None);
	}
	const FVector2D Local = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition());
	if (const FHit* Hit = FindHit(FVector2f(Local)))
	{
		if (Hit->Id != EShiftUi::SettingsClose && Hit->Id != EShiftUi::SettingsPanel)
		{
			return FCursorReply::Cursor(EMouseCursor::Hand);
		}
	}
	return FCursorReply::Unhandled();
}

FReply SShiftHud::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (OnKey.IsBound())
	{
		if (InKeyEvent.IsRepeat())
		{
			return FReply::Handled();   // holding a key is one press, not a stream of them
		}
		OnKey.Execute(InKeyEvent.GetKey());
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SShiftHud::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	AnimTime += InDeltaTime;
	PulseTime = Model.bReduceMotion ? 0.0f : AnimTime;   // the pulses stand still
	ResultsAge = Model.bOver ? ResultsAge + InDeltaTime : 0.0f;
	Ripples.RemoveAll([this](const FRipple& R) { return AnimTime - R.Start > RippleSeconds; });
	const float Rate = FMath::Min(1.0f, InDeltaTime * 9.0f);
	for (const FShiftHudRow& Row : Model.Rows)
	{
		FRowAnim& Anim = RowAnims.FindOrAdd(Row.EntryId);
		const float TargetY = static_cast<float>(Row.Rank) * RowHeight;
		if (!Anim.bInit)
		{
			Anim.Y = TargetY;
			Anim.Share = Row.Share;
			Anim.Rank = Row.Rank;
			Anim.bInit = true;
		}
		if (Model.bEase && Anim.Rank != Row.Rank)
		{
			Anim.Pulse = 1.0f;   // an entry moved up or down the board: its row flashes teal (gain) or red (loss)
			Anim.bUp = Row.Rank < Anim.Rank;
		}
		Anim.Rank = Row.Rank;
		Anim.Pulse = FMath::Max(0.0f, Anim.Pulse - InDeltaTime * 1.5f);
		Anim.Y += (TargetY - Anim.Y) * Rate;
		Anim.Share += (Row.Share - Anim.Share) * Rate;
	}

	// The build screen's lists: rows slide to their places and the bars glide to their values, so moving a slider reads as a smooth change and never as a flicker.
	if (Model.Screen == EShiftScreen::Build)
	{
		const FShiftHudBuild& B = Model.Build;
		auto Ease = [this, Rate](int32 List, const TArray<FShiftHudForecastRow>& Rows)
			{
				for (int32 r = 0; r < Rows.Num(); ++r)
				{
					FRowAnim& Anim = BuildAnims.FindOrAdd(List * 32 + Rows[r].EntryId);
					const float TargetY = static_cast<float>(r);
					if (!Anim.bInit)
					{
						Anim.Y = TargetY;
						Anim.Share = Rows[r].Value;
						Anim.bInit = true;
					}
					Anim.Y += (TargetY - Anim.Y) * Rate;
					Anim.Share += (Rows[r].Value - Anim.Share) * Rate;
				}
			};
		Ease(0, B.ByQuality);
		Ease(1, B.ByForecast);
		if (!B.bForecastReady)
		{
			for (TPair<int32, FRowAnim>& Pair : BuildAnims)
			{
				if (Pair.Key >= 32)
				{
					Pair.Value.bInit = false;   // a new world: its rows start where they belong instead of sliding in from the last one
				}
			}
		}
		ForecastFade += ((B.bForecastReady ? 1.0f : 0.0f) - ForecastFade) * FMath::Min(1.0f, InDeltaTime * 6.0f);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// shared pieces

bool SShiftHud::Button(FShiftPainter& P, const FPal& C, int32 Id, float X, float Y, float W, float H, const FString& Label, bool bPrimary, float Px) const
{
	const bool bHover = AddHit(X, Y, W, H, Id);
	const float Lift = bHover ? -2.0f : 0.0f;
	P.Rect(X, Y + 4.0f + Lift, W, H, Hex(0x000000, 0.16f), H * 0.5f);
	if (!bPrimary)
	{
		P.Rect(X - 1.0f, Y + Lift - 1.0f, W + 2.0f, H + 2.0f, bHover ? C.Teal : C.Stroke, H * 0.5f + 1.0f);
	}
	FLinearColor Fill = bPrimary ? C.Teal : C.PanelSolid;
	if (bHover)
	{
		Fill = bPrimary ? Mix(C.Teal, FLinearColor::White, 0.2f) : Mix(C.PanelSolid, C.Teal, 0.14f);
	}
	P.Rect(X, Y + Lift, W, H, Fill, H * 0.5f);
	P.Text(Label, X + W * 0.5f, Y + Lift + (H - Px * 1.25f) * 0.5f, Px, bPrimary ? FLinearColor::White : C.Ink, EShiftAlign::Center, true);
	return bHover;
}

void SShiftHud::SettingsButton(FShiftPainter& P, const FPal& C, float Cx, float Cy) const
{
	const bool bHover = AddHit(Cx - 16.0f, Cy - 16.0f, 32.0f, 32.0f, EShiftUi::Settings);
	const bool bOpen = Model.bSettingsOpen;
	P.Disc(Cx, Cy, 15.0f, (bHover || bOpen) ? WithAlpha(C.Teal, 0.30f) : C.Track);
	// a gear: eight teeth around a ring
	const FLinearColor Ink = bOpen ? C.Teal : C.Ink;
	const FVector2f Centre(Cx, Cy);
	for (int32 i = 0; i < 8; ++i)
	{
		const float Angle = static_cast<float>(i) * UE_TWO_PI / 8.0f;
		const FVector2f Dir(FMath::Sin(Angle), -FMath::Cos(Angle));
		P.Line(Centre + Dir * 8.0f, Centre + Dir * 12.0f, Ink, 3.4f);
	}
	P.Arc(Cx, Cy, 6.6f, 0.0f, 1.0f, Ink, 3.2f);
	if (!Model.bEffectsOn || !Model.bMusicOn)
	{
		P.Disc(Cx + 11.0f, Cy - 11.0f, 4.6f, C.Red);   // some sound is off: a red dot on the gear
	}
}

float SShiftHud::Solid(float Alpha) const
{
	return Model.bGlassActive ? 1.0f - FMath::Square(1.0f - Alpha) : Alpha;
}

FLinearColor SShiftHud::Scaled(FLinearColor Wash) const
{
	Wash.A *= Model.BackgroundOpacity;   // in the see-through window the washes fade with the paper, so the desktop shows through the menus too
	return Wash;
}

void SShiftHud::PaintSettings(FShiftPainter& P, const FPal& C) const
{
	// The menu dropped from the gear button. Everything behind it stops listening: a click outside it closes it.
	Hits.Reset();
	bModal = true;
	Dock(P, EShiftDock::C);
	AddWindowHit(P, EShiftUi::SettingsClose);

	const float W = 380.0f;
	const float H = 532.0f;
	const float Pad = 28.0f;
	const bool bTitle = Model.Screen == EShiftScreen::Title;   // on the title it opens beside the menu, inside the safe zone
	Dock(P, bTitle ? EShiftDock::C : EShiftDock::TR);          // elsewhere it hangs from the gear button, in the top right corner of the window
	const float X = bTitle ? 1216.0f - W : DesignW - W - 24.0f;
	const float Y = bTitle ? 656.0f - H : 56.0f;
	P.Rect(X + 2.0f, Y + 8.0f, W, H, Hex(0x000000, 0.22f), 18.0f);
	P.Rect(X - 1.0f, Y - 1.0f, W + 2.0f, H + 2.0f, C.Stroke, 19.0f);
	P.Rect(X, Y, W, H, C.PanelSolid, 18.0f);
	AddHit(X, Y, W, H, EShiftUi::SettingsPanel);   // the padding swallows clicks, so only a click outside closes it

	P.Text(TEXT("SETTINGS"), X + Pad, Y + 22.0f, 14.0f, C.Ink, EShiftAlign::Left, true);
	if (!bTitle)
	{
		P.Text(TEXT("Esc closes"), X + W - Pad, Y + 24.0f, 11.0f, C.InkDim, EShiftAlign::Right);
	}

	const float BtnH = 34.0f;
	const float Gap = 10.0f;
	const float Inner = W - 2.0f * Pad;

	// effects and music side by side
	const float ColW = (Inner - 24.0f) * 0.5f;
	const float Small = (ColW - 8.0f) * 0.5f;
	const float MusicX = X + Pad + ColW + 24.0f;
	P.Text(TEXT("EFFECTS"), X + Pad, Y + 62.0f, 11.0f, C.InkDim, EShiftAlign::Left, true);
	P.Text(TEXT("MUSIC"), MusicX, Y + 62.0f, 11.0f, C.InkDim, EShiftAlign::Left, true);
	Button(P, C, EShiftUi::EffectsOn, X + Pad, Y + 82.0f, Small, BtnH, TEXT("On"), Model.bEffectsOn, 13.0f);
	Button(P, C, EShiftUi::EffectsOff, X + Pad + Small + 8.0f, Y + 82.0f, Small, BtnH, TEXT("Off"), !Model.bEffectsOn, 13.0f);
	Button(P, C, EShiftUi::MusicOn, MusicX, Y + 82.0f, Small, BtnH, TEXT("On"), Model.bMusicOn, 13.0f);
	Button(P, C, EShiftUi::MusicOff, MusicX + Small + 8.0f, Y + 82.0f, Small, BtnH, TEXT("Off"), !Model.bMusicOn, 13.0f);

	P.Text(TEXT("WINDOW"), X + Pad, Y + 140.0f, 11.0f, C.InkDim, EShiftAlign::Left, true);
	// two choices: Fullscreen is the borderless kind (a window the size of the monitor; there is no exclusive fullscreen) and Windowed
	const float HalfW = (Inner - Gap) * 0.5f;
	Button(P, C, EShiftUi::WindowFullscreen + 1, X + Pad, Y + 160.0f, HalfW, BtnH, TEXT("Fullscreen"), Model.WindowMode != 2, 13.0f);
	Button(P, C, EShiftUi::WindowFullscreen + 2, X + Pad + HalfW + Gap, Y + 160.0f, HalfW, BtnH, TEXT("Windowed"), Model.WindowMode == 2, 13.0f);

	// the see-through window: the desktop shows through the paper behind the board (needs a restart; Windows only)
	P.Text(TEXT("BACKGROUND"), X + Pad, Y + 218.0f, 11.0f, C.InkDim, EShiftAlign::Left, true);
	Button(P, C, EShiftUi::GlassToggle, X + Pad, Y + 238.0f, Inner, BtnH, Model.bGlassSaved ? TEXT("See-through window: on") : TEXT("See-through window: off"), Model.bGlassSaved, 13.0f);
	{
		const float X0 = X + Pad + 6.0f;
		const float X1 = X + W - Pad - 6.0f;
		const float Ty = Y + 308.0f;
		const float Frac = static_cast<float>(Model.GlassOpacity) / 100.0f;
		const bool bLive = Model.bGlassActive;   // the slider is live only while the window is see-through
		const FLinearColor Fill = bLive ? C.Teal : C.InkDim;
		const bool bHover = AddHit(X0 - 14.0f, Ty - 14.0f, X1 - X0 + 28.0f, 28.0f, EShiftUi::GlassOpacity, true, X0, X1);
		P.Text(TEXT("Paper opacity"), X + Pad, Y + 284.0f, 12.0f, C.Ink, EShiftAlign::Left);
		P.Text(FString::Printf(TEXT("%d%%"), Model.GlassOpacity), X + W - Pad, Y + 284.0f, 12.0f, C.Ink, EShiftAlign::Right, true);
		P.Rect(X0, Ty - 3.0f, X1 - X0, 6.0f, C.Track, 3.0f);
		P.Rect(X0, Ty - 3.0f, (X1 - X0) * Frac, 6.0f, Fill, 3.0f);
		P.Disc(X0 + (X1 - X0) * Frac, Ty, bHover ? 11.0f : 9.0f, Fill);
		P.Disc(X0 + (X1 - X0) * Frac, Ty, 4.0f, FLinearColor::White);
		const bool bPending = Model.bGlassSaved != Model.bGlassActive;
		P.Text(bPending ? TEXT("Restart the game to apply") : (bLive ? TEXT("Windowed or borderless only") : TEXT("Takes effect after a restart")), X + Pad, Y + 328.0f, 11.0f, bPending ? C.Amber : C.InkDim, EShiftAlign::Left);
	}

	P.Text(TEXT("ACCESSIBILITY"), X + Pad, Y + 358.0f, 11.0f, C.InkDim, EShiftAlign::Left, true);
	Button(P, C, EShiftUi::Markers, X + Pad, Y + 378.0f, Inner, BtnH, Model.bMarkers ? TEXT("Shapes beside colours: on") : TEXT("Shapes beside colours: off"), Model.bMarkers, 13.0f);
	const float Half = (Inner - Gap) * 0.5f;
	Button(P, C, EShiftUi::LargeText, X + Pad, Y + 428.0f, Half, BtnH, Model.bLargeText ? TEXT("Large text: on") : TEXT("Large text: off"), Model.bLargeText, 13.0f);
	Button(P, C, EShiftUi::ReduceMotion, X + Pad + Half + Gap, Y + 428.0f, Half, BtnH, Model.bReduceMotion ? TEXT("Calm: on") : TEXT("Calm: off"), Model.bReduceMotion, 13.0f);
	Button(P, C, EShiftUi::ColourBlind, X + Pad, Y + 478.0f, Half, BtnH, Model.bColourBlind ? TEXT("Colour-blind: on") : TEXT("Colour-blind: off"), Model.bColourBlind, 13.0f);
	Button(P, C, EShiftUi::Hints, X + Pad + Half + Gap, Y + 478.0f, Half, BtnH, Model.bHintsOn ? TEXT("Coach tips: on") : TEXT("Coach tips: off"), Model.bHintsOn, 13.0f);
}

/** The arrow's outline without stroked joints: offsets a closed polygon by Dist (positive = inwards) with mitred corners. */
static TArray<FVector2f> ShiftInsetPolygon(const TArray<FVector2f>& In, float Dist)
{
	float Area = 0.0f;
	for (int32 i = 0; i < In.Num(); ++i)
	{
		const FVector2f& A = In[i];
		const FVector2f& B = In[(i + 1) % In.Num()];
		Area += A.X * B.Y - B.X * A.Y;
	}
	const float Side = Area > 0.0f ? 1.0f : -1.0f;   // which side of an edge is the inside
	TArray<FVector2f> Out;
	for (int32 i = 0; i < In.Num(); ++i)
	{
		const FVector2f& Prev = In[(i + In.Num() - 1) % In.Num()];
		const FVector2f& Cur = In[i];
		const FVector2f& Next = In[(i + 1) % In.Num()];
		FVector2f E1 = (Cur - Prev).GetSafeNormal();
		FVector2f E2 = (Next - Cur).GetSafeNormal();
		const FVector2f N1(-E1.Y * Side, E1.X * Side);   // inward normals of the two edges
		const FVector2f N2(-E2.Y * Side, E2.X * Side);
		const float Denominator = FMath::Max(0.35f, 1.0f + FVector2f::DotProduct(N1, N2));
		Out.Add(Cur + (N1 + N2) * (Dist / Denominator));   // a mitred corner
	}
	return Out;
}

/** Rounds every corner of a closed polygon with a quadratic curve through the corner. */
static TArray<FVector2f> ShiftRoundPolygon(const TArray<FVector2f>& In, float Radius)
{
	TArray<FVector2f> Out;
	for (int32 i = 0; i < In.Num(); ++i)
	{
		const FVector2f& Prev = In[(i + In.Num() - 1) % In.Num()];
		const FVector2f& Cur = In[i];
		const FVector2f& Next = In[(i + 1) % In.Num()];
		const float R1 = FMath::Min(Radius, (Prev - Cur).Size() * 0.5f);
		const float R2 = FMath::Min(Radius, (Next - Cur).Size() * 0.5f);
		const FVector2f A = Cur + (Prev - Cur).GetSafeNormal() * R1;
		const FVector2f B = Cur + (Next - Cur).GetSafeNormal() * R2;
		for (int32 s = 0; s <= 6; ++s)
		{
			const float T = static_cast<float>(s) / 6.0f;
			Out.Add(A * ((1.0f - T) * (1.0f - T)) + Cur * (2.0f * T * (1.0f - T)) + B * (T * T));
		}
	}
	return Out;
}

/** The shift arrow as one seamless outlined shape: Outline filled, then the same shape shrunk by Stroke filled in Face (the corners rounded by Radius first). */
static void ShiftPaintArrow(FShiftPainter& P, const TArray<FVector2f>& Arrow, float Radius, float Stroke, const FLinearColor& Outline, const FLinearColor& Face)
{
	const TArray<FVector2f> Smooth = ShiftRoundPolygon(Arrow, Radius);
	P.FillPolygon(ShiftInsetPolygon(Smooth, -Stroke * 0.55f), Outline);
	P.FillPolygon(ShiftInsetPolygon(Smooth, Stroke * 0.45f), Face);
}

void SShiftHud::PaintKeycap(FShiftPainter& P, const FPal& C, float X, float Y, float S, bool bPressed, const TCHAR* Label) const
{
	// the logo's key (keycap() in Tools/itch/make_images.py) with the title key's effects: the pulsing glow, the press, the night colours, the violet arrow
	const float Cx = X + S * 0.5f;
	const float Cy = Y + S * 0.5f;
	const float Pulse = 0.5f + 0.5f * FMath::Sin(PulseTime * 2.4f);
	P.Disc(Cx, Cy, S * (0.73f + 0.05f * Pulse), bPressed ? Hex(0x8B6CFF, 0.20f) : WithAlpha(C.Teal, 0.07f + 0.05f * Pulse));
	P.Disc(Cx, Cy, S * 0.52f, bPressed ? Hex(0x8B6CFF, 0.22f) : WithAlpha(C.Teal, 0.08f));

	const FLinearColor Outline = Hex(0x1B1930);
	const FLinearColor Base = Mix(Hex(0xB9B3A6), Hex(0x1A1D3A), C.N);
	const FLinearColor Cap = Mix(Hex(0xFBFAF6), Hex(0x2B2F57), C.N);
	const FLinearColor Glyph = bPressed ? Hex(0x7B55FF) : Mix(Outline, Hex(0xEDEDFF), C.N);
	const float Radius = S * 0.22f;
	const float Edge = S * 0.035f;
	const auto Box = [&](float Top, float Bottom, const FLinearColor& Fill)
		{
			const float L = X + S * 0.04f;
			const float W = S * 0.92f;
			P.Rect(L, Top, W, Bottom - Top, Outline, Radius);
			P.Rect(L + Edge, Top + Edge, W - 2.0f * Edge, Bottom - Top - 2.0f * Edge, Fill, Radius - Edge);
		};
	Box(Y + S * 0.19f, Y + S * 0.96f, Base);
	const float Sink = bPressed ? S * 0.07f : 0.0f;
	const float CapTop = Y + S * 0.05f + Sink;
	const float CapBottom = Y + S * 0.86f + Sink;
	Box(CapTop, CapBottom, Cap);

	static const FVector2f ArrowShape[7] = { FVector2f(0.50f, 0.04f), FVector2f(0.94f, 0.52f), FVector2f(0.69f, 0.52f), FVector2f(0.69f, 0.96f), FVector2f(0.31f, 0.96f), FVector2f(0.31f, 0.52f), FVector2f(0.06f, 0.52f) };
	const bool bLabelled = Label != nullptr;   // a keycap with a word on it (the trailer's SHIFT key) keeps the arrow, smaller and higher, with the word under it
	const float ArrowSize = S * (bLabelled ? 0.30f : 0.46f);
	const float ArrowY = bLabelled ? CapTop + S * 0.27f : (CapTop + CapBottom) * 0.5f;
	TArray<FVector2f> Arrow;
	for (const FVector2f& Point : ArrowShape)
	{
		Arrow.Add(FVector2f(Cx + (Point.X - 0.5f) * ArrowSize, ArrowY + (Point.Y - 0.5f) * ArrowSize));
	}
	if (bLabelled)
	{
		P.Text(Label, Cx, CapTop + S * 0.52f, S * 0.22f, Glyph, EShiftAlign::Center, true);
	}
	ShiftPaintArrow(P, Arrow, ArrowSize * 0.10f, S * (bLabelled ? 0.035f : 0.045f), Glyph, Cap);
}

void SShiftHud::PaintShiftKey(FShiftPainter& P, const FPal& C, float Cx, float Cy, bool bPressed) const
{
	// the wide "shift" key of the first title screen (commit 82e5fc3): the outlined arrow on the left of the word; the trailer's "OR HOLD" scene uses it
	const float W = 190.0f;
	const float H = 112.0f;
	const float X = Cx - W * 0.5f;
	const float Y = Cy - H * 0.5f;
	const float Off = bPressed ? 9.0f : 0.0f;
	const float Pulse = 0.5f + 0.5f * FMath::Sin(PulseTime * 2.4f);

	P.Disc(Cx, Cy, 150.0f + 10.0f * Pulse, bPressed ? Hex(0x8B6CFF, 0.20f) : WithAlpha(C.Teal, 0.07f + 0.05f * Pulse));
	P.Disc(Cx, Cy, 108.0f, bPressed ? Hex(0x8B6CFF, 0.22f) : WithAlpha(C.Teal, 0.08f));

	const FLinearColor Base = Mix(Hex(0xB9B3A6), Hex(0x1A1D3A), C.N);
	const FLinearColor Cap = Mix(Hex(0xFBFAF6), Hex(0x2B2F57), C.N);
	const FLinearColor Face = Mix(Hex(0xFFFFFF), Hex(0x363B6B), C.N);
	const FLinearColor Glyph = bPressed ? Hex(0x7B55FF) : Mix(Hex(0x1B1930), Hex(0xEDEDFF), C.N);

	P.Rect(X, Y + 12.0f, W, H, Base, 26.0f);
	P.Rect(X, Y + Off, W, H, Cap, 26.0f);
	P.Rect(X + 9.0f, Y + Off + 8.0f, W - 18.0f, H - 28.0f, Face, 20.0f);

	const float Ay = Y + Off + H * 0.42f;
	TArray<FVector2f> Arrow;
	const float Ax = X + 56.0f;
	Arrow.Add(FVector2f(Ax, Ay - 22.0f));
	Arrow.Add(FVector2f(Ax + 20.0f, Ay));
	Arrow.Add(FVector2f(Ax + 8.0f, Ay));
	Arrow.Add(FVector2f(Ax + 8.0f, Ay + 22.0f));
	Arrow.Add(FVector2f(Ax - 8.0f, Ay + 22.0f));
	Arrow.Add(FVector2f(Ax - 8.0f, Ay));
	Arrow.Add(FVector2f(Ax - 20.0f, Ay));
	ShiftPaintArrow(P, Arrow, 4.5f, 4.0f, Glyph, Face);
	P.Text(TEXT("shift"), X + 92.0f, Ay - 20.0f, 36.0f, Glyph, EShiftAlign::Left, true);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// title

void SShiftHud::MenuRow(FShiftPainter& P, const FPal& C, int32 Id, int32 Rank, float Y, const FString& Label, const FString& KeyHint, int32 EntryColor, float Bar, bool bNudge) const
{
	// one row of the title menu, drawn like a row of the jam's leaderboard: rank, the entry's dot, a vote bar
	const float X = 243.0f;
	const float W = 540.0f * Bar;
	const float H = 48.0f;
	const bool bHover = AddHit(X, Y, W, H, Id);
	const FLinearColor Color = ShiftPalette::Entry(EntryColor);
	P.Rect(X, Y + 3.0f, W, H, Hex(0x000000, 0.14f), 7.0f);
	P.Rect(X, Y, W, H, Mix(C.PanelSolid, Color, bHover ? 0.48f : 0.26f), 7.0f);
	P.Text(FString::FromInt(Rank), X + 28.0f, Y + 12.0f, 19.0f, C.InkDim, EShiftAlign::Center, true);
	P.Disc(X + 66.0f, Y + H * 0.5f, 9.0f, Color);
	P.Text(Label, X + 90.0f, Y + 10.0f, 22.0f, C.Ink, EShiftAlign::Left, bHover);
	if (bNudge)
	{
		P.Text(TEXT("New? Start here"), X + W - 14.0f, Y + 15.0f, 14.0f, C.Teal, EShiftAlign::Right, true);
	}
	else if (!KeyHint.IsEmpty())
	{
		P.Text(KeyHint, X + W - 14.0f, Y + 15.0f, 15.0f, C.InkDim, EShiftAlign::Right);
	}
}

void SShiftHud::PaintLogo(FShiftPainter& P, const FPal& C, float X, float Y, float Px, bool bPressed) const
{
	// the logo of Tools/itch/make_images.py: the key, then Roboto Black, RATE, FOR and RATE in teal, white and teal, a thick ink outline, drawn letter by letter so each outline laps the letter before it
	const float Mark = Px * 1.05f;
	PaintKeycap(P, C, X - Mark * 0.04f, Y + Px * 0.65f - Mark * 0.5f, Mark, bPressed);

	const FLinearColor Outline = Hex(0x1B1930);
	const float Stroke = FMath::RoundToFloat(Px * 0.075f);
	const float Tracking = Px * 0.018f;
	const auto Word = [&](const TCHAR* Letters, float At, const FLinearColor& Fill)
		{
			for (const TCHAR* Letter = Letters; *Letter; ++Letter)
			{
				const FString Glyph = FString::Chr(*Letter);
				P.TextFx(Glyph, At, Y, Px, Fill, EShiftAlign::Left, EShiftTrailerFont::Black, 0, Stroke, Outline);
				At += P.MeasureFx(Glyph, Px, EShiftTrailerFont::Black, 0).X + Tracking;
			}
			return At;
		};
	const float WordX = X - Mark * 0.04f + Mark + Px * 0.28f - Px * 0.085f;   // Slate draws an outlined glyph inset
	const float AfterRate = Word(TEXT("RATE"), WordX, Hex(0x1FB6A6));
	const float AfterFor = Word(TEXT("FOR"), AfterRate + Px * 0.30f, FLinearColor::White);
	Word(TEXT("RATE"), AfterFor + Px * 0.30f, Hex(0x1FB6A6));
}

void SShiftHud::PaintTitle(FShiftPainter& P, const FPal& C) const
{
	// the wash covers the whole window; the logo and the menu are centred on it
	Dock(P, EShiftDock::C);
	FillWindow(P, Scaled(Mix(Hex(0xF6F1E6, 0.58f), Hex(0x04060F, 0.66f), C.N)));

	// the name, as it always was
	const float Bob = FMath::Sin(PulseTime * 1.1f) * 3.0f;
	const float LogoX = 243.0f;   // the logo (about 772 wide at this size) nearly centred on the screen
	PaintLogo(P, C, LogoX, 76.0f + Bob, 88.0f, Model.bShiftDown);
	const FString Hint = Model.bShiftDown ? FString(TEXT("NIGHT SHIFT: sockpuppets, bots, bribes. Powerful, fragile, risky."))
		: FString(TEXT("Try it: hold SHIFT to work the night shift."));
	P.Text(Hint, LogoX, 190.0f, 20.0f, Model.bShiftDown ? Hex(0xB79BFF) : C.InkDim, EShiftAlign::Left, Model.bShiftDown);

	// the menu is a leaderboard, and Play is in first place
	const float RowX = 243.0f;   // every row starts where the logo icon does
	const float RowW = 540.0f;
	const float PlayY = 232.0f;
	const float PlayH = 64.0f;
	const bool bPlayHover = AddHit(RowX, PlayY, RowW, PlayH, EShiftUi::TitleStart);
	P.Rect(RowX, PlayY, RowW, PlayH, bPlayHover ? Mix(C.Teal, FLinearColor::White, 0.16f) : C.Teal, 9.0f);
	P.Text(TEXT("1"), RowX + 28.0f, PlayY + 16.0f, 25.0f, FLinearColor::White, EShiftAlign::Center, true);
	P.Disc(RowX + 66.0f, PlayY + PlayH * 0.5f, 12.0f, FLinearColor::White);
	P.Text(TEXT("Play"), RowX + 90.0f, PlayY + 13.0f, 29.0f, FLinearColor::White, EShiftAlign::Left, true);

	const float Pitch = 50.0f;   // eight rows below Play now
	float Y = 306.0f;
	MenuRow(P, C, EShiftUi::LearnToPlay, 2, Y, TEXT("Learn to play"), FString(), 3, 0.74f, Model.bLessonNudge);
	MenuRow(P, C, EShiftUi::HowTo, 3, Y += Pitch, TEXT("How to play"), FString(), 2, 0.64f, false);
	MenuRow(P, C, EShiftUi::Achievements, 4, Y += Pitch, TEXT("Achievements"), FString(), 4, 0.56f, false);
	MenuRow(P, C, EShiftUi::Replays, 5, Y += Pitch, TEXT("Replays"), FString(), 7, 0.52f, false);
	MenuRow(P, C, EShiftUi::Settings, 6, Y += Pitch, TEXT("Settings"), FString(), 5, 0.47f, false);
	MenuRow(P, C, EShiftUi::Credits, 7, Y += Pitch, TEXT("Credits"), FString(), 6, 0.42f, false);
	MenuRow(P, C, EShiftUi::Quit, 8, Y += Pitch, TEXT("Quit"), FString(), 1, 0.37f, false);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// jam select

void SShiftHud::PaintJamSelect(FShiftPainter& P, const FPal& C) const
{
	Dock(P, EShiftDock::C);
	FillWindow(P, Scaled(Mix(Hex(0xF6F1E6, 0.80f), Hex(0x04060F, 0.80f), C.N)));
	P.Text(TEXT("Pick a jam"), 640.0f, 34.0f, 36.0f, C.Ink, EShiftAlign::Center, true);
	P.Text(TEXT("Each jam has its own rival, its own network and its own mood."), 640.0f, 84.0f, 15.0f, C.InkDim, EShiftAlign::Center);

	// Three wide cards at a time; more jams page with the arrows (or Left and Right), so any number of jam packs fits. The card height is the same on every page.
	int32 MostFacts = 3;
	for (const FShiftHudJamCard& Card : Model.JamCards)
	{
		MostFacts = FMath::Max(MostFacts, Card.Facts.Num());
	}
	const float CardW = 336.0f;
	const float CardStride = 372.0f;   // three cards and two 36-unit gaps span 1080 of the 1280: a 100-unit gutter each side for the page buttons (16 from the card, 40 from the edge)
	const float Extra = 19.0f * static_cast<float>(MostFacts - 3);   // a fourth fact pushes the rest of the card down
	const float CardH = 480.0f + Extra;
	const int32 Total = Model.JamCards.Num();
	const int32 Pages = FMath::Max(1, FMath::DivideAndRoundUp(Total, JamsPerPage));
	const int32 Page = FMath::Clamp(Model.JamPage, 0, Pages - 1);
	const int32 First = Page * JamsPerPage;
	const int32 Shown = FMath::Min(JamsPerPage, Total - First);
	const float CardX0 = 100.0f + CardStride * 0.5f * static_cast<float>(JamsPerPage - Shown);   // a short last page is centred
	for (int32 Slot = 0; Slot < Shown; ++Slot)
	{
		const int32 i = First + Slot;
		const FShiftHudJamCard& Card = Model.JamCards[i];
		const float X = CardX0 + CardStride * static_cast<float>(Slot);
		const float Y0 = 128.0f;
		const bool bHover = AddHit(X, Y0 - 6.0f, CardW, CardH + 6.0f, EShiftUi::JamCard0 + Slot);
		const float Y = Y0 + (bHover ? -6.0f : 0.0f);

		P.Rect(X, Y + 8.0f, CardW, CardH, Hex(0x000000, 0.18f), 18.0f);
		P.Rect(X - 2.0f, Y - 2.0f, CardW + 4.0f, CardH + 4.0f, bHover ? C.Teal : C.Stroke, 20.0f);
		P.Rect(X, Y, CardW, CardH, C.PanelSolid, 18.0f);

		P.Text(FString::Printf(TEXT("JAM %d"), i + 1), X + 24.0f, Y + 22.0f, 12.0f, C.Teal, EShiftAlign::Left, true);
		P.Text(P.Fit(Card.Title, CardW - 48.0f, 26.0f, true), X + 24.0f, Y + 42.0f, 26.0f, C.Ink, EShiftAlign::Left, true);
		P.TextBlock(Card.Subtitle, X + 24.0f, Y + 84.0f, CardW - 48.0f, 13.0f, 18.0f, C.InkDim);

		P.Rect(X + 24.0f, Y + 150.0f, CardW - 48.0f, 1.0f, C.Stroke);
		P.Text(TEXT("THE RIVAL"), X + 24.0f, Y + 166.0f, 11.0f, C.InkDim, EShiftAlign::Left, true);
		P.Text(P.Fit(Card.RivalGame, CardW - 48.0f, 19.0f, true), X + 24.0f, Y + 184.0f, 19.0f, C.Ink, EShiftAlign::Left, true);
		P.Text(P.Fit(FString::Printf(TEXT("by @%s"), *Card.RivalAuthor), CardW - 48.0f, 13.0f), X + 24.0f, Y + 212.0f, 13.0f, C.InkDim);
		P.TextBlock(Card.RivalBlurb, X + 24.0f, Y + 240.0f, CardW - 48.0f, 13.0f, 18.0f, C.Ink);

		float Fy = Y + 292.0f;
		for (const FString& Fact : Card.Facts)
		{
			P.Disc(X + 30.0f, Fy + 8.0f, 2.5f, C.Teal);
			P.Text(P.Fit(Fact, CardW - 54.0f, 12.0f), X + 42.0f, Fy, 12.0f, C.InkDim);
			Fy += 19.0f;
		}

		P.Text(TEXT("DIFFICULTY"), X + 24.0f, Y + 356.0f + Extra, 11.0f, C.InkDim, EShiftAlign::Left, true);
		if (Card.Difficulty > 0)
		{
			for (int32 s = 0; s < 3; ++s)
			{
				P.Disc(X + 28.0f + 24.0f * static_cast<float>(s), Y + 392.0f + Extra, 8.0f, s < Card.Difficulty ? C.Amber : C.Track);
			}
		}
		else
		{
			P.Text(TEXT("Fair"), X + 24.0f, Y + 383.0f + Extra, 15.0f, C.Teal, EShiftAlign::Left, true);   // the control group: the rating is no head start
		}
		const FString Best = Card.BestRank > 0 ? FString::Printf(TEXT("Best: %s place"), *HudOrdinal(Card.BestRank)) : FString(TEXT("Not played yet"));
		P.Text(Best, X + CardW - 24.0f, Y + 383.0f + Extra, 15.0f, Card.BestRank == 1 ? C.Teal : C.Ink, EShiftAlign::Right, Card.BestRank == 1);

		P.Rect(X + 24.0f, Y + CardH - 62.0f, CardW - 48.0f, 40.0f, bHover ? C.Teal : WithAlpha(C.Teal, 0.16f), 20.0f);
		P.Text(TEXT("Build your entry"), X + CardW * 0.5f, Y + CardH - 52.0f, 16.0f, bHover ? FLinearColor::White : C.Teal, EShiftAlign::Center, true);
	}

	if (Pages > 1)
	{
		// the page arrows beside the cards, the dots and a "jams 4-5 of 5" line under them
		// round buttons in the style of Back, each holding the game's own arrow (the SHIFT keycap's), turned to point left or right
		static const FVector2f ArrowShape[7] = { FVector2f(0.50f, 0.04f), FVector2f(0.94f, 0.52f), FVector2f(0.69f, 0.52f), FVector2f(0.69f, 0.96f), FVector2f(0.31f, 0.96f), FVector2f(0.31f, 0.52f), FVector2f(0.06f, 0.52f) };
		const float Size = 44.0f;   // in the 100-unit gutter: 40 from the window edge, 16 from the card
		const float ArrowY = 128.0f + CardH * 0.5f - Size * 0.5f;
		for (int32 Dir = 0; Dir < 2; ++Dir)
		{
			const bool bNext = Dir == 1;
			const bool bEnabled = bNext ? Page < Pages - 1 : Page > 0;
			if (!bEnabled)
			{
				continue;   // nothing that way: no button
			}
			const float Ax = bNext ? 1280.0f - 40.0f - Size : 40.0f;
			const bool bOver = AddHit(Ax, ArrowY, Size, Size, bNext ? EShiftUi::JamNext : EShiftUi::JamPrev);
			const float Lift = bOver ? -2.0f : 0.0f;
			P.Rect(Ax, ArrowY + 4.0f + Lift, Size, Size, Hex(0x000000, 0.16f), Size * 0.5f);
			P.Rect(Ax - 1.0f, ArrowY + Lift - 1.0f, Size + 2.0f, Size + 2.0f, bOver ? C.Teal : C.Stroke, Size * 0.5f + 1.0f);
			P.Rect(Ax, ArrowY + Lift, Size, Size, bOver ? C.Teal : C.PanelSolid, Size * 0.5f);
			const float Cx = Ax + Size * 0.5f;
			const float Cy = ArrowY + Lift + Size * 0.5f;
			const float A = Size * 0.46f;
			TArray<FVector2f> Arrow;
			for (const FVector2f& Pt : ArrowShape)
			{
				// the shape points up; (x, y) -> (y, x) points it left, mirrored it points right
				const float Px = (Pt.Y - 0.5f) * A;
				const float Py = (Pt.X - 0.5f) * A;
				Arrow.Add(FVector2f(Cx + (bNext ? -Px : Px), Cy + Py));
			}
			const FLinearColor Ink = bOver ? FLinearColor::White : C.Teal;
			P.FillPolygon(Arrow, Ink);
			P.Polygon(Arrow, Ink, 1.5f);
		}
		const float DotsY = 128.0f + CardH + 30.0f;
		const float DotsX0 = 640.0f - 9.0f * static_cast<float>(Pages - 1);
		for (int32 p = 0; p < Pages; ++p)
		{
			P.Disc(DotsX0 + 18.0f * static_cast<float>(p), DotsY, p == Page ? 5.5f : 4.0f, p == Page ? C.Teal : C.Track);
		}
		const FString Range = Shown > 1 ? FString::Printf(TEXT("Jams %d-%d of %d"), First + 1, First + Shown, Total) : FString::Printf(TEXT("Jam %d of %d"), First + 1, Total);
		P.Text(Range, 640.0f, DotsY + 16.0f, 12.0f, C.InkDim, EShiftAlign::Center);
	}

	Dock(P, EShiftDock::TL);   // Back and the gear hold their corners
	Button(P, C, EShiftUi::Back, 24.0f, 22.0f, 104.0f, 36.0f, TEXT("< Back"), false, 14.0f);
	Dock(P, EShiftDock::TR);
	SettingsButton(P, C, 1246.0f, 24.0f);
	Dock(P, EShiftDock::C);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// build

void SShiftHud::PaintBuild(FShiftPainter& P, const FPal& C) const
{
	Dock(P, EShiftDock::C);
	FillWindow(P, Scaled(Mix(Hex(0xF6F1E6, 0.84f), Hex(0x04060F, 0.84f), C.N)));
	const FShiftHudBuild& B = Model.Build;
	int32 Spent = 0;
	for (int32 i = 0; i < 3; ++i)
	{
		Spent += B.Hours[i];
	}
	const int32 Left = B.TotalHours - Spent;

	P.Text(TEXT("Build your entry"), 60.0f, 62.0f, 34.0f, C.Ink, EShiftAlign::Left, true);
	P.Text(FString::Printf(TEXT("%s  -  %d hours left before the deadline. Spend them."), *B.JamTitle, B.TotalHours), 60.0f, 108.0f, 15.0f, C.InkDim);

	struct FSliderInfo
	{
		const TCHAR* Label;
		const TCHAR* Text;
	};
	static const FSliderInfo Infos[3] =
	{
		{ TEXT("Quality"), TEXT("Polish the game. A better game keeps the voters it wins, but nobody plays all the entries.") },
		{ TEXT("Thumbnail and title"), TEXT("Cover art and a catchy name: how many voters look at your page, and how far each Post goes.") },
		{ TEXT("Network"), TEXT("Discord pals and rate-for-rate rings: followers on day one, and a bigger, faster Post bank.") },
	};

	for (int32 i = 0; i < 3; ++i)
	{
		const float Y = 150.0f + 126.0f * static_cast<float>(i);
		P.Rect(60.0f, Y, 580.0f, 114.0f, C.PanelSolid, 14.0f);
		P.Text(Infos[i].Label, 84.0f, Y + 14.0f, 20.0f, C.Ink, EShiftAlign::Left, true);
		P.Text(FString::Printf(TEXT("%d h"), B.Hours[i]), 616.0f, Y + 12.0f, 26.0f, C.Teal, EShiftAlign::Right, true);
		P.TextBlock(Infos[i].Text, 84.0f, Y + 42.0f, 530.0f, 12.0f, 15.0f, C.InkDim);
		P.Text(P.Fit(B.Effects[i], 530.0f, 14.0f, true), 84.0f, Y + 68.0f, 14.0f, C.Teal, EShiftAlign::Left, true);

		const float X0 = 90.0f;
		const float X1 = 610.0f;
		const float Ty = Y + 94.0f;
		const bool bHover = AddHit(X0 - 18.0f, Ty - 16.0f, X1 - X0 + 36.0f, 32.0f, EShiftUi::SliderQuality + i, true, X0, X1);
		P.Rect(X0, Ty - 3.0f, X1 - X0, 6.0f, C.Track, 3.0f);
		const float Frac = static_cast<float>(B.Hours[i]) / 8.0f;
		P.Rect(X0, Ty - 3.0f, (X1 - X0) * Frac, 6.0f, C.Teal, 3.0f);
		for (int32 t = 0; t <= 8; ++t)
		{
			P.Rect(X0 + (X1 - X0) * static_cast<float>(t) / 8.0f - 0.75f, Ty + 8.0f, 1.5f, 5.0f, C.Stroke);
		}
		P.Disc(X0 + (X1 - X0) * Frac, Ty, bHover ? 13.0f : 11.0f, C.Teal);
		P.Disc(X0 + (X1 - X0) * Frac, Ty, 5.0f, FLinearColor::White);
	}

	const bool bAllSpent = Left == 0;
	P.Text(bAllSpent ? FString(TEXT("All 12 hours planned."))
		: FString::Printf(TEXT("%d unplanned hour%s: unspent hours are lost."), Left, Left == 1 ? TEXT("") : TEXT("s")),
		60.0f, 534.0f, 14.0f, bAllSpent ? C.InkDim : C.Amber, EShiftAlign::Left, !bAllSpent);

	// The two lists. Both are laid out the same way every time: ordered by value (ties by entry), bars on a fixed scale (so a bar only moves when its own value does), one decimal,
	// rows that slide and bars that glide. The quality list is exact and always there; the vote forecast fades in once the plays it averages are in.
	auto Fade = [](FLinearColor Col, float A) { Col.A *= A; return Col; };
	auto PaintList = [&](int32 List, float X, const TCHAR* Title, const TArray<FShiftHudForecastRow>& Rows, bool bShares, float Alpha, float BarScale)
		{
			P.Rect(X, 150.0f, 262.0f, 318.0f, C.PanelSolid, 14.0f);
			P.Text(Title, X + 16.0f, 164.0f, 11.0f, C.InkDim, EShiftAlign::Left, true);
			if (Alpha <= 0.01f)
			{
				return;
			}
			for (int32 r = 0; r < Rows.Num(); ++r)
			{
				const FShiftHudForecastRow& R = Rows[r];
				const FRowAnim* Anim = BuildAnims.Find(List * 32 + R.EntryId);
				const float Place = Anim != nullptr && Anim->bInit ? Anim->Y : static_cast<float>(r);
				const float Value = Anim != nullptr && Anim->bInit ? Anim->Share : R.Value;
				const float Y = 190.0f + 30.0f * Place;
				if (R.bPlayer)
				{
					P.Rect(X + 6.0f, Y - 3.0f, 250.0f, 28.0f, WithAlpha(C.Teal, 0.18f * Alpha), 8.0f);
				}
				P.Text(FString::FromInt(r + 1), X + 22.0f, Y + 2.0f, 13.0f, Fade(R.bPlayer ? C.Teal : C.Ink, Alpha), EShiftAlign::Center, true);
				P.Disc(X + 42.0f, Y + 11.0f, 5.5f, Fade(R.Color, Alpha));
				P.Text(P.Fit(R.Name, 132.0f, 12.0f, R.bPlayer), X + 54.0f, Y + 1.0f, 12.0f, Fade(C.Ink, Alpha), EShiftAlign::Left, R.bPlayer);
				const FString ValueText = bShares ? FString::Printf(TEXT("%.1f%%"), Value * 100.0f) : FString::Printf(TEXT("%.1f"), Value);
				P.Text(ValueText, X + 250.0f, Y + 1.0f, 12.0f, Fade(C.InkDim, Alpha), EShiftAlign::Right);
				P.Rect(X + 54.0f, Y + 19.0f, 150.0f, 3.0f, Fade(C.Track, Alpha), 1.5f);
				P.Rect(X + 54.0f, Y + 19.0f, 150.0f * FMath::Clamp(Value / BarScale, 0.0f, 1.0f), 3.0f, Fade(R.Color, Alpha), 1.5f);
			}
		};
	PaintList(0, 680.0f, TEXT("IF VOTERS JUDGED QUALITY"), B.ByQuality, false, 1.0f, 10.0f);
	PaintList(1, 958.0f, TEXT("WHAT THE VOTE WILL ACTUALLY DO"), B.ByForecast, true, B.bForecastReady ? ForecastFade : 0.0f, 0.8f);
	if (!B.bForecastReady)
	{
		const float Dots = FMath::Fmod(AnimTime * 3.0f, 4.0f);
		P.Text(FString::Printf(TEXT("Playing out this jam%s"), Dots < 1.0f ? TEXT("") : (Dots < 2.0f ? TEXT(".") : (Dots < 3.0f ? TEXT("..") : TEXT("...")))), 958.0f + 131.0f, 290.0f, 14.0f, C.Ink, EShiftAlign::Center, true);
		P.TextBlock(TEXT("with your build, against this rival and these entries."), 958.0f + 36.0f, 316.0f, 190.0f, 12.0f, 16.0f, C.InkDim);
		P.Rect(958.0f + 36.0f, 366.0f, 190.0f, 4.0f, C.Track, 2.0f);
		P.Rect(958.0f + 36.0f, 366.0f, 190.0f * FMath::Clamp(B.ForecastProgress, 0.0f, 1.0f), 4.0f, C.Teal, 2.0f);
	}

	P.Rect(680.0f, 484.0f, 540.0f, 112.0f, C.PanelSolid, 14.0f);
	FString VoteRank = TEXT("...");
	if (B.bForecastReady)
	{
		VoteRank = B.ForecastRankLo == B.ForecastRankHi ? HudOrdinal(B.ForecastRank) : FString::Printf(TEXT("%s-%s"), *HudOrdinal(B.ForecastRankLo), *HudOrdinal(B.ForecastRankHi));
	}
	P.Text(FString::Printf(TEXT("Your game: %s on quality  ->  %s on votes"), *HudOrdinal(B.QualityRank), *VoteRank), 700.0f, 498.0f, 20.0f,
		!B.bForecastReady ? C.InkDim : (B.ForecastRankLo > B.QualityRank ? C.Red : C.Teal), EShiftAlign::Left, true);
	P.TextBlock(FString::Printf(TEXT("The average of many plays of a steady player (a Plug about every 0.7 s) in this very jam. A single play swings about %.0f points around it. The rival has %s followers; you have a few."),
		B.ForecastSpread * 100.0f, *B.RivalFollowers), 700.0f, 530.0f, 500.0f, 13.0f, 17.0f, C.InkDim);

	Button(P, C, EShiftUi::Upload, 840.0f, 610.0f, 380.0f, 58.0f, TEXT("UPLOAD AND START VOTING"), true, 20.0f);
	P.Text(TEXT("ENTER"), 820.0f, 631.0f, 12.0f, C.InkDim, EShiftAlign::Right);

	Dock(P, EShiftDock::TL);   // Back and the gear hold their corners
	Button(P, C, EShiftUi::Back, 24.0f, 22.0f, 104.0f, 36.0f, TEXT("< Back"), false, 14.0f);
	Dock(P, EShiftDock::TR);
	SettingsButton(P, C, 1246.0f, 24.0f);
	Dock(P, EShiftDock::C);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// play

void SShiftHud::PaintTooltip(FShiftPainter& P, const FPal& C) const
{
	const FShiftHudTooltip& T = Model.Tooltip;
	const float CardW = 380.0f;
	const float ActionsH = T.Actions.Num() > 0 ? 12.0f + 21.0f * static_cast<float>(T.Actions.Num()) : 0.0f;
	const float CardH = 118.0f + ActionsH;
	// The right gutter (leaderboard, states, feed) is never covered: the card flips to the left of the voter when it would reach it.
	const float LeaderboardX = FMath::Min(DesignW - 10.0f - CompactLeaderboardW, Model.LeaderboardMode == 1 ? 996.0f : DesignW);
	const float LeaderboardBottom = 668.0f;
	float X = T.Anchor.X + T.AnchorRadius + 14.0f;
	float Y = T.Anchor.Y + 12.0f;
	if (X + CardW > LeaderboardX - 8.0f && Y < LeaderboardBottom)
	{
		X = T.Anchor.X - T.AnchorRadius - 14.0f - CardW;    // flip to the left so the card never covers the leaderboard
	}
	if (X + CardW > DesignW - 8.0f)
	{
		X = T.Anchor.X - T.AnchorRadius - 14.0f - CardW;
	}
	X = FMath::Max(8.0f, X);
	Y = FMath::Clamp(Y, 48.0f, 668.0f - CardH);

	P.Rect(X, Y, CardW, CardH, Mix(Hex(0xFFFFFF, 0.97f), Hex(0x0A0D1C, 0.97f), C.N), 10.0f);
	P.Rect(X, Y, 4.0f, CardH, T.bHasOwner ? WithAlpha(T.OwnerColor, 0.95f) : C.Track, 2.0f);
	P.Text(P.Fit(T.Title, CardW - 26.0f, 14.0f, true), X + 14.0f, Y + 8.0f, 14.0f, C.Ink, EShiftAlign::Left, true);
	P.Text(P.Fit(T.Line1, CardW - 26.0f, 12.0f), X + 14.0f, Y + 28.0f, 12.0f, C.InkDim);
	P.Text(P.Fit(T.Line2, CardW - 26.0f, 12.0f), X + 14.0f, Y + 45.0f, 12.0f, C.Ink);
	P.Rect(X + 14.0f, Y + 66.0f, CardW - 28.0f, 5.0f, C.Track, 2.5f);
	P.Rect(X + 14.0f, Y + 66.0f, (CardW - 28.0f) * FMath::Clamp(T.Conviction, 0.0f, 1.0f), 5.0f, T.bHasOwner ? T.OwnerColor : C.Track, 2.5f);
	P.Text(P.Fit(T.Status, CardW - 26.0f, 12.0f, true), X + 14.0f, Y + 82.0f, 12.0f, T.StatusColor, EShiftAlign::Left, true);
	if (T.Actions.Num() > 0)
	{
		// what can be done here, each with its key
		P.Rect(X + 14.0f, Y + 104.0f, CardW - 28.0f, 1.0f, C.Stroke);
		const float KeyW = 96.0f;
		for (int32 i = 0; i < T.Actions.Num(); ++i)
		{
			const FShiftHudAction& A = T.Actions[i];
			const float Ry = Y + 112.0f + 21.0f * static_cast<float>(i);
			P.Rect(X + 14.0f, Ry, KeyW, 18.0f, A.bAvailable ? WithAlpha(C.Teal, 0.22f) : C.Track, 5.0f);
			P.Text(A.Key, X + 14.0f + KeyW * 0.5f, Ry + 3.0f, 10.0f, A.bAvailable ? C.Ink : C.InkDim, EShiftAlign::Center, true);
			P.Text(P.Fit(A.Text, CardW - 28.0f - KeyW - 10.0f, 12.0f), X + 14.0f + KeyW + 10.0f, Ry + 1.0f, 12.0f, A.bAvailable ? C.Ink : C.InkDim);
		}
	}
}

void SShiftHud::PaintPlay(FShiftPainter& P, const FPal& C) const
{
	// ------------------------------------------------------------------ top bar: it spans the window, and its left, middle and right parts each hold their own edge
	Dock(P, EShiftDock::TL);
	{
		const FVector4f Win = WindowRect(P);
		P.Rect(Win.X, 0, Win.Z - Win.X, 40, C.Panel);
		P.Rect(Win.X, 40, Win.Z - Win.X, 1, C.Stroke);
	}
	P.Text(TEXT("RATE FOR RATE"), 18, 10, 15, C.Ink, EShiftAlign::Left, true);
	P.Text(P.Fit(Model.JamName, 230.0f, 13), 160, 12, 13, C.InkDim);
	if (Model.bReplay)
	{
		P.Text(TEXT("REPLAY"), 400, 11, 14, C.Amber, EShiftAlign::Left, true);   // a recording is playing: clicks and keys do nothing
	}
	Dock(P, EShiftDock::T);
	P.Text(FString::Printf(TEXT("%s  -  Day %d / %d"), *Model.DateLabel, Model.Day, Model.Days), 640, 9, 16, C.Ink, EShiftAlign::Center, true);
	P.Rect(510, 33, 260, 3, C.Track, 1.5f);
	P.Rect(510, 33, 260.0f * FMath::Clamp(Model.RunProgress, 0.0f, 1.0f), 3, C.Teal, 1.5f);
	Dock(P, EShiftDock::TR);
	P.Text(Model.DeadlineLabel, 1224, 12, 13, C.InkDim, EShiftAlign::Right);
	SettingsButton(P, C, 1250.0f, 20.0f);
	if (Model.bModsEnabled && Model.NextSweepIn >= 0.0f)
	{
		const bool bAway = Model.EyeAwayLeft > 0.0f;
		const bool bSoon = !bAway && Model.NextSweepIn < 3.0f;
		const FLinearColor EyeColor = bAway ? Hex(0x7B5CF0) : (bSoon ? C.Red : C.InkDim);
		const float Pulse = bSoon ? 0.5f + 0.5f * FMath::Sin(PulseTime * 14.0f) : 0.0f;
		P.Disc(ModsIconX, ModsIconY, ModsIconR, EyeColor);
		P.Disc(ModsIconX, ModsIconY, 7.0f, C.Panel);
		if (!bAway)
		{
			P.Disc(ModsIconX, ModsIconY, 3.0f + 1.5f * Pulse, EyeColor);   // the Eye looks away: no pupil
		}
		P.Text(ModsLabel(Model), ModsTextX, ModsTextY, ModsTextPx, EyeColor, EShiftAlign::Left, bSoon || bAway);
	}

	// ------------------------------------------------------------------ leaderboard
	Dock(P, EShiftDock::TR);   // the right strip (leaderboard, states, feed) holds the right edge
	const int32 LeaderRows = FMath::Max(1, Model.Rows.Num());
	const float LeaderBottom = Model.LeaderboardMode == 1 ? 52.0f + 38.0f + RowHeight * static_cast<float>(LeaderRows)
		: (Model.LeaderboardMode == 0 ? 48.0f + 20.0f + CompactRowH * static_cast<float>(LeaderRows) + 4.0f : 48.0f + 22.0f);
	// Compact by default (the top three and the player, on a see-through panel) so the right side of the board stays in view; L cycles compact / full / hidden.
	if (Model.LeaderboardMode == 1)
	{
		const float PanelX = 996.0f;
		const float PanelY = 52.0f;
		P.Rect(PanelX, PanelY, 274, 38.0f + RowHeight * static_cast<float>(FMath::Max(1, Model.Rows.Num())), WithAlpha(C.Panel, Solid(0.86f)), 10);
		P.Text(TEXT("LEADERBOARD  (L: smaller)"), PanelX + 14, PanelY + 10, 11, C.InkDim, EShiftAlign::Left, true);
		float MaxShare = 0.001f;
		for (const FShiftHudRow& Row : Model.Rows)
		{
			MaxShare = FMath::Max(MaxShare, Row.Share);
		}
		for (const FShiftHudRow& Row : Model.Rows)
		{
			const FRowAnim* Anim = RowAnims.Find(Row.EntryId);
			const float Y = PanelY + 34.0f + (Anim ? Anim->Y : static_cast<float>(Row.Rank) * RowHeight);
			const float Share = Anim ? Anim->Share : Row.Share;
			if (Anim && Anim->Pulse > 0.01f)
			{
				P.Rect(PanelX + 4, Y - 3, 266, RowHeight + 1, WithAlpha(Anim->bUp ? C.Teal : C.Red, 0.40f * Anim->Pulse), 8);
			}
			if (Row.bPlayer)
			{
				P.Rect(PanelX + 6, Y - 2, 262, RowHeight - 1, WithAlpha(C.Teal, 0.18f), 8);
			}
			P.Text(FString::FromInt(Row.Rank + 1), PanelX + 26, Y + 4, 14, Row.bPlayer ? C.Teal : C.Ink, EShiftAlign::Center, true);
			P.Disc(PanelX + 46, Y + 13, 6.0f, Row.Color);
			if (Model.bMarkers) { DrawMarker(P, Row.EntryId, PanelX + 46, Y + 13, 3.6f); }
			P.Text(P.Fit(Row.Name, 134.0f, 13, Row.bPlayer), PanelX + 60, Y + 2, 13, C.Ink, EShiftAlign::Left, Row.bPlayer);
			P.Text(FString::Printf(TEXT("%s  %.0f%%"), *Compact(Row.Votes), Share * 100.0f), PanelX + 264, Y + 3, 12, C.InkDim, EShiftAlign::Right);
			P.Rect(PanelX + 60, Y + 21, 170, 4, C.Track, 2);
			P.Rect(PanelX + 60, Y + 21, 170.0f * FMath::Clamp(Share / MaxShare, 0.0f, 1.0f), 4, Row.Color, 2);
		}
	}
	else if (Model.LeaderboardMode == 0)
	{
		// All nine entries, one thin row each, on a see-through panel: the board stays readable underneath and nobody is hidden.
		const float PanelW = CompactLeaderboardW;
		const float PanelX = DesignW - 10.0f - PanelW;
		const float PanelY = 48.0f;
		const int32 RowCount = FMath::Max(1, Model.Rows.Num());
		P.Rect(PanelX, PanelY, PanelW, 20.0f + CompactRowH * static_cast<float>(RowCount) + 4.0f, WithAlpha(C.Panel, Solid(0.58f)), 8);
		P.Text(TEXT("LEADERBOARD  (L)"), PanelX + 10, PanelY + 4, 10, C.InkDim, EShiftAlign::Left, true);
		for (const FShiftHudRow& Row : Model.Rows)
		{
			const float Y = PanelY + 18.0f + CompactRowH * static_cast<float>(Row.Rank);
			if (const FRowAnim* Pulse = RowAnims.Find(Row.EntryId); Pulse && Pulse->Pulse > 0.01f)
			{
				P.Rect(PanelX + 3, Y - 2, PanelW - 6, CompactRowH + 2, WithAlpha(Pulse->bUp ? C.Teal : C.Red, 0.45f * Pulse->Pulse), 5);
			}
			if (Row.bPlayer)
			{
				P.Rect(PanelX + 4, Y - 1, PanelW - 8, CompactRowH, WithAlpha(C.Teal, 0.24f), 5);
			}
			P.Text(FString::FromInt(Row.Rank + 1), PanelX + 14, Y + 1, 11, Row.bPlayer ? C.Teal : C.Ink, EShiftAlign::Center, true);
			P.Disc(PanelX + 28, Y + 7, 4.0f, Row.Color);
			if (Model.bMarkers) { DrawMarker(P, Row.EntryId, PanelX + 28, Y + 7, 2.8f); }
			P.Text(P.Fit(Row.Name, Row.bPlayer && Model.FakeShare > 0.004f ? 74.0f : 92.0f, 11, Row.bPlayer), PanelX + 38, Y + 1, 11, C.Ink, EShiftAlign::Left, Row.bPlayer);
			if (Row.bPlayer && Model.FakeShare > 0.004f)
			{
				P.Text(FString::Printf(TEXT("+%.0f%% fake"), Model.FakeShare * 100.0f), PanelX + PanelW - 40, Y + 1, 10, Hex(0xB79BFF), EShiftAlign::Right, true);
			}
			else if (FMath::Abs(Row.Trend) >= 0.5f)
			{
				// who is gaining and who is losing over the last 20 s, in points: what the other entries do to the leader shows here
				P.Text(FString::Printf(TEXT("%+.0f"), Row.Trend), PanelX + PanelW - 36, Y + 2, 10, Row.Trend > 0.0f ? C.Teal : C.Red, EShiftAlign::Right, true);
			}
			P.Text(FString::Printf(TEXT("%.0f%%"), Row.Share * 100.0f), PanelX + PanelW - 8, Y + 1, 11, C.InkDim, EShiftAlign::Right);
		}
	}
	else
	{
		for (const FShiftHudRow& Row : Model.Rows)
		{
			if (Row.bPlayer)
			{
				P.Rect(DesignW - 124.0f, 48.0f, 114.0f, 22.0f, WithAlpha(C.Panel, Solid(0.62f)), 11);
				P.Text(FString::Printf(TEXT("YOU #%d  %.0f%%  (L)"), Row.Rank + 1, Row.Share * 100.0f), DesignW - 67.0f, 53.0f, 11, C.Teal, EShiftAlign::Center, true);
			}
		}
	}

	// ------------------------------------------------------------------ bottom bar: it spans the window; POSTS, Rally, the night chip and the alerts hold the left edge, Heat and strikes the right one
	Dock(P, EShiftDock::BL);
	{
		const FVector4f Win = WindowRect(P);
		P.Rect(Win.X, 676, Win.Z - Win.X, 44, C.Panel);
		P.Rect(Win.X, 676, Win.Z - Win.X, 1, C.Stroke);
	}
	P.Text(TEXT("POSTS"), 20, 690, 11, C.InkDim, EShiftAlign::Left, true);
	const int32 PipCount = FMath::Max(1, FMath::RoundToInt(Model.PostsMax));
	const float PipStep = PipCount >= 6 ? 20.0f : 26.0f;
	for (int32 i = 0; i < PipCount; ++i)
	{
		const float Fill = FMath::Clamp(Model.Posts - static_cast<float>(i), 0.0f, 1.0f);
		P.Disc(86.0f + PipStep * static_cast<float>(i), 698, 9.5f, C.Track);
		if (Fill > 0.02f)
		{
			P.Disc(86.0f + PipStep * static_cast<float>(i), 698, 4.0f + 5.5f * Fill, Mix(WithAlpha(C.Teal, 0.55f), C.Teal, Fill));
		}
	}
	P.Text(FString::Printf(TEXT("%.1f"), Model.Posts), 86.0f + PipStep * static_cast<float>(PipCount) + 2.0f, 690, 13, C.Ink, EShiftAlign::Left, true);

	// An alert takes over the middle of this bar (below), so what it would cover is not painted meanwhile.
	const bool bAlertUp = Model.Alert.Kind != 0;

	// Rally (right-click on a voter you hold): ready, or counting down.
	if (!bAlertUp)
	{
		const bool bReady = Model.RallyCooldown <= 0.0f;
		P.Rect(268, 683, 214, 28, WithAlpha(bReady ? C.Teal : C.Track, bReady ? 0.22f : 0.60f), 14);
		P.Text(bReady ? FString::Printf(TEXT("RALLY  right-click  (%.0f Post%s)"), Model.RallyCost, Model.RallyCost > 1.5f ? TEXT("s") : TEXT("")) : FString::Printf(TEXT("RALLY  %.0fs"), FMath::CeilToFloat(Model.RallyCooldown)),
			375, 689, 12, bReady ? C.Teal : C.InkDim, EShiftAlign::Center, true);
	}

	const bool bNight = Model.bNightActive;
	if (!bAlertUp)
	{
		P.Rect(500, 683, 150, 28, bNight ? Hex(0x2A2250) : WithAlpha(C.Amber, 0.28f), 14);
		P.Text(bNight ? TEXT("NIGHT SHIFT") : TEXT("DAY SHIFT"), 575, 688, 14, bNight ? Hex(0xCBB8FF) : Hex(0x7A5400), EShiftAlign::Center, true);
		const bool bFree = Model.EyeAwayLeft > 0.0f;
		const FString Hint = !Model.NightHint.IsEmpty() ? Model.NightHint : (bNight ? FString(TEXT("click = sockpuppet")) : FString(TEXT("hold SHIFT to cheat")));
		P.Text(P.Fit(Hint, 130.0f, 11.0f, bFree), 664, 691, 11, bFree ? Hex(0x7B5CF0) : (Model.NightHint.IsEmpty() ? C.InkDim : C.Ink), EShiftAlign::Left, bFree);
	}

	Dock(P, EShiftDock::BR);
	if (Model.bShowHeat)
	{
		// Always on, so the risk is known before the first sockpuppet; dimmed until night shift is in use.
		const float Fade = Model.bHeatActive ? 1.0f : 0.55f;
		P.Text(TEXT("HEAT"), 800, 690, 11, WithAlpha(C.InkDim, Fade), EShiftAlign::Left, true);
		P.Rect(836, 694, 120, 8, WithAlpha(C.Track, Fade), 4);
		const float Heat = FMath::Clamp(Model.Heat, 0.0f, 1.0f);
		P.Rect(836, 694, 120.0f * Heat, 8, WithAlpha(Mix(C.Amber, C.Red, Heat), Fade), 4);
		if (Model.HeatProjected > Heat + 0.001f)
		{
			// While SHIFT is held: where the next sockpuppet would take Heat (red above the red tick).
			const bool bRed = Model.HeatProjected > Model.HeatRedZone;
			const float X0 = 836.0f + 120.0f * Heat;
			P.Rect(X0, 692, 120.0f * (Model.HeatProjected - Heat), 12, WithAlpha(bRed ? C.Red : C.Amber, 0.45f), 4);
			P.Text(FString::Printf(TEXT("next sock %.0f%%"), Model.HeatProjected * 100.0f), 836, 677, 10, bRed ? C.Red : C.InkDim);
		}
		P.Rect(836.0f + 120.0f * Model.HeatRedZone - 1.0f, 690, 2, 16, WithAlpha(C.Red, Fade));
		for (int32 i = 0; i < Model.MaxStrikes; ++i)
		{
			const bool bHit = i < Model.Strikes;
			P.Disc(976.0f + 20.0f * static_cast<float>(i), 698, 7.5f, WithAlpha(bHit ? C.Red : C.Track, Fade));
		}
		P.Text(TEXT("STRIKES"), 1042, 690, 11, WithAlpha(C.InkDim, Fade), EShiftAlign::Left, true);
	}
	P.Text(TEXT("SPACE pause   ESC menu"), 1262, 693, 11, C.InkDim, EShiftAlign::Right);

	// ------------------------------------------------------------------ the alert: urgent events take over the middle of the bottom bar, where the eyes already are for Posts, Heat and strikes
	// (x 280..792: the POSTS pips and counter at the left, the HEAT bar and the strike pips at the right stay in view; the edge frame's bottom strip, y 712 and below, stays clear)
	Dock(P, EShiftDock::BL);   // it covers what is hidden meanwhile: the Rally and night chips
	if (bAlertUp)
	{
		const FShiftHudAlert& A = Model.Alert;
		const float X0 = 280.0f;
		const float W = 512.0f;
		const float Y0 = 678.0f;
		const float H = 32.0f;
		const float In = FMath::Clamp(A.Age / 0.14f, 0.0f, 1.0f);
		const float Dy = (1.0f - In) * 4.0f;
		const bool bLive = A.bClickable && !Model.bPaused;
		const bool bHover = bLive && AddHit(X0, Y0, W, H, EShiftUi::Blame);
		const float Pulse = A.bClickable ? 0.5f + 0.5f * FMath::Sin(PulseTime * 9.0f) : 0.0f;
		FLinearColor Fill = A.Color;
		Fill = Mix(Fill, FLinearColor::White, (bHover ? 0.18f : 0.0f) + 0.10f * Pulse);
		if (Model.bPaused)
		{
			Fill = Mix(Fill, Hex(0x77777F), 0.7f);
		}
		P.Rect(X0, Y0 + 2.0f + Dy, W, H, Hex(0x000000, 0.22f * In), 9.0f);
		P.Rect(X0, Y0 + Dy, W, H, WithAlpha(Fill, In), 9.0f);
		const FLinearColor Ink = WithAlpha(FLinearColor::White, In);
		const float TextW = W - 28.0f - (A.SecondsLeft >= 0.0f ? 64.0f : 0.0f);
		P.Text(P.Fit(A.Title, TextW, 15.0f, true), X0 + 14.0f, Y0 + 2.0f + Dy, 15.0f, Ink, EShiftAlign::Left, true);
		P.Text(P.Fit(A.Detail, TextW, 11.0f), X0 + 14.0f, Y0 + 17.0f + Dy, 11.0f, WithAlpha(FLinearColor::White, 0.92f * In), EShiftAlign::Left);
		if (A.SecondsLeft >= 0.0f)
		{
			P.Text(FString::Printf(TEXT("%.1fs"), A.SecondsLeft), X0 + W - 14.0f, Y0 + 6.0f + Dy, 18.0f, Ink, EShiftAlign::Right, true);
		}
		if (A.Frac >= 0.0f)
		{
			P.Rect(X0 + 14.0f, Y0 + H - 3.0f + Dy, (W - 28.0f) * FMath::Clamp(A.Frac, 0.0f, 1.0f), 2.0f, WithAlpha(FLinearColor::White, 0.85f * In), 1.0f);
		}
	}

	// ------------------------------------------------------------------ the replay viewer: pause, speed, where in the jam and the way out, over the middle of the bottom bar
	if (Model.bViewer && !Model.bOver)
	{
		Dock(P, EShiftDock::BL);
		const float X0 = 280.0f;
		const float W = 512.0f;
		const float Y0 = 678.0f;
		const float H = 32.0f;
		FLinearColor BarFill = C.PanelSolid;
		BarFill.A = 1.0f;   // fully opaque: the Rally and night chips behind it must not show through the track
		P.Rect(X0, Y0 + 2.0f, W, H, Hex(0x000000, 0.22f), 9.0f);
		P.Rect(X0, Y0, W, H, BarFill, 9.0f);
		float X = X0 + 8.0f;
		{
			const bool bHover = AddHit(X, Y0 + 4.0f, 50.0f, 24.0f, EShiftUi::ViewerPause);
			P.Rect(X, Y0 + 4.0f, 50.0f, 24.0f, bHover ? Mix(C.Teal, FLinearColor::White, 0.16f) : C.Teal, 8.0f);
			P.Text(Model.bViewerPaused ? TEXT("Play") : TEXT("Pause"), X + 25.0f, Y0 + 8.0f, 12.0f, FLinearColor::White, EShiftAlign::Center, true);
			X += 56.0f;
		}
		static const TCHAR* const Speeds[4] = { TEXT("1x"), TEXT("2x"), TEXT("4x"), TEXT("8x") };
		for (int32 Si = 0; Si < 4; ++Si)
		{
			const bool bOn = Model.ViewerSpeed == Si;
			const bool bHover = AddHit(X, Y0 + 4.0f, 32.0f, 24.0f, EShiftUi::ViewerSpeed0 + Si);
			P.Rect(X, Y0 + 4.0f, 32.0f, 24.0f, bOn ? C.Amber : (bHover ? WithAlpha(C.Teal, 0.30f) : WithAlpha(C.Track, 0.7f)), 8.0f);
			P.Text(Speeds[Si], X + 16.0f, Y0 + 8.0f, 12.0f, bOn ? Hex(0x1B1930) : C.Ink, EShiftAlign::Center, bOn);
			X += 36.0f;
		}
		X += 8.0f;
		const float ExitW = 52.0f;
		const float BarW = X0 + W - 8.0f - ExitW - 10.0f - X;
		AddHit(X, Y0 + 4.0f, BarW, 24.0f, EShiftUi::ViewerSeek, true, X, X + BarW);
		const float Fill = BarW * FMath::Clamp(Model.ViewerProgress, 0.0f, 1.0f);
		P.Rect(X, Y0 + 13.0f, BarW, 6.0f, C.Track, 3.0f);
		P.Rect(X, Y0 + 13.0f, Fill, 6.0f, C.Teal, 3.0f);
		P.Disc(X + Fill, Y0 + 16.0f, 6.5f, C.Teal);
		X += BarW + 10.0f;
		{
			const bool bHover = AddHit(X, Y0 + 4.0f, ExitW, 24.0f, EShiftUi::Menu);
			P.Rect(X, Y0 + 4.0f, ExitW, 24.0f, bHover ? WithAlpha(C.Red, 0.85f) : WithAlpha(C.Track, 0.7f), 8.0f);
			P.Text(TEXT("Exit"), X + ExitW * 0.5f, Y0 + 8.0f, 12.0f, bHover ? FLinearColor::White : C.Ink, EShiftAlign::Center, true);
		}
	}

	// ------------------------------------------------------------------ the right gutter
	// Everything that is not the board lives in the strip right of the voters (the layout keeps them left of x = 1084): under the leaderboard the states you are in, then the
	// notification feed; at the bottom the coach card. Nothing here floats over the board.
	{
		const float GX = DesignW - 10.0f - CompactLeaderboardW;
		const float GW = CompactLeaderboardW;
		float BottomY = 668.0f;

		// the bottom stack: the coach card (the Blame prompt is in the bottom bar, where the eyes already are); it sits above the bottom bar, wherever that is
		Dock(P, EShiftDock::BR);
		if (Model.CoachAlpha > 0.01f && !Model.CoachText.IsEmpty())
		{
			const TArray<FString> Lines = P.Wrap(Model.CoachText, GW - 36.0f, 12.0f, false);
			const float H = 30.0f + 15.0f * static_cast<float>(Lines.Num());
			const float Y = BottomY - H;
			const bool bHover = AddHit(GX, Y, GW, H, EShiftUi::CoachDismiss);
			P.Rect(GX, Y, GW, H, WithAlpha(C.PanelSolid, 0.97f * Model.CoachAlpha), 10.0f);
			P.Rect(GX, Y, 4.0f, H, WithAlpha(C.Teal, Model.CoachAlpha), 2.0f);
			float Ty = Y + 18.0f;
			for (const FString& Line : Lines)
			{
				P.Text(Line, GX + 14.0f, Ty, 12.0f, WithAlpha(C.Ink, Model.CoachAlpha));
				Ty += 15.0f;
			}
			P.Text(bHover ? TEXT("click to dismiss") : TEXT("tip"), GX + GW - 10.0f, Y + 5.0f, 9.0f, WithAlpha(C.InkDim, Model.CoachAlpha), EShiftAlign::Right, true);
			BottomY = Y - 8.0f;
		}
		Dock(P, EShiftDock::TR);

		// the states you are in, pinned under the leaderboard: one chip each, a title and a line of detail
		float Y = LeaderBottom + 8.0f;
		const auto Pin = [&](const FString& Title, const FString& Detail, const FLinearColor& Fill) -> float
			{
				const float H = Detail.IsEmpty() ? 24.0f : 38.0f;
				const float Top = Y;
				P.Rect(GX, Top, GW, H, Fill, 9.0f);
				P.Text(P.Fit(Title, GW - 16.0f, 12.0f, true), GX + 8.0f, Top + 5.0f, 12.0f, FLinearColor::White, EShiftAlign::Left, true);
				if (!Detail.IsEmpty())
				{
					P.Text(P.Fit(Detail, GW - 16.0f, 10.0f), GX + 8.0f, Top + 21.0f, 10.0f, WithAlpha(FLinearColor::White, 0.92f));
				}
				Y += H + 5.0f;
				return Top;
			};
		const FLinearColor Purple = Hex(0x7B5CF0);
		if (Model.ShadowbanSeconds > 0.0f)
		{
			Pin(FString::Printf(TEXT("SHADOWBANNED  %.1fs"), Model.ShadowbanSeconds), TEXT("your clicks are refused until it ends"), Hex(0xFF4D5A, 0.92f));
		}
		if (Model.EyeAwayLeft > 0.0f)
		{
			Pin(FString::Printf(TEXT("MOD EYE LOOKS AWAY  %.0fs"), Model.EyeAwayLeft), TEXT("no Heat now: hold SHIFT"), WithAlpha(Purple, 0.92f));
		}
		else if (Model.EyeAwayWarnIn >= 0.0f)
		{
			Pin(FString::Printf(TEXT("MOD EYE AWAY IN %.0fs"), Model.EyeAwayWarnIn), TEXT("bank Posts, then hold SHIFT"), WithAlpha(Purple, 0.70f));
		}
		if (Model.FinalPushLeft > 0.0f)
		{
			Pin(FString::Printf(TEXT("FINAL PUSH  %.0fs"), Model.FinalPushLeft),
				Model.RivalPostsMax > 0.0f ? FString::Printf(TEXT("rival Posts %d/%.0f, yours +%.0f%%"), FMath::FloorToInt(Model.RivalPosts + 1.0e-3f), Model.RivalPostsMax, Model.FinalPushBonus * 100.0f)
				: FString::Printf(TEXT("rival x2 attacks, your Posts +%.0f%%"), Model.FinalPushBonus * 100.0f), WithAlpha(C.Red, 0.92f));
		}
		else if (Model.FinalPushIn >= 0.0f)
		{
			Pin(FString::Printf(TEXT("FINAL PUSH IN %.0fs"), Model.FinalPushIn), TEXT("the rival goes all-in: bank Posts"), WithAlpha(Mix(C.Red, C.Amber, 0.5f), 0.92f));
		}
		if (Model.SympathyPoints >= 0.5f)
		{
			Pin(TEXT("UNDERDOGS RALLY"), FString(TEXT("a big lead drifts back to the runner-up")), WithAlpha(C.Amber, 0.92f));
		}
		if (Model.FinalAuditIn >= 0.0f)
		{
			const float Pulse = 0.5f + 0.5f * FMath::Sin(PulseTime * 10.0f);
			Pin(FString::Printf(TEXT("FINAL AUDIT  %.0fs"), FMath::CeilToFloat(Model.FinalAuditIn)), TEXT("new sockpuppet votes are purged"), WithAlpha(C.Red, 0.80f + 0.15f * Pulse));
		}
		bool bFakeHover = false;
		float FakeY = 0.0f;
		if (Model.FakeShare > 0.004f)
		{
			// Sockpuppet votes you hold right now, backed or not: what a strike takes. Holding them heats you up, so above the limit Heat climbs.
			const bool bOver = Model.FakeLimit > 0.0f && Model.FakeShare > Model.FakeLimit;
			FakeY = Pin(FString::Printf(TEXT("SOCKPUPPET VOTES  %.0f%%"), Model.FakeShare * 100.0f),
				Model.FakeLimit > 0.0f ? FString::Printf(TEXT("+%.1f Heat/s, safe up to %.0f%%"), Model.FakeHeatRate, Model.FakeLimit * 100.0f) : FString::Printf(TEXT("+%.1f Heat/s"), Model.FakeHeatRate),
				WithAlpha(bOver ? C.Red : Purple, 0.90f));
			bFakeHover = AddHit(GX, FakeY, GW, 38.0f, EShiftUi::FakeInfo);
		}

		// the notification feed: toasts and news, newest first, as cards that only take the room the pinned states leave
		for (const FShiftHudFeedItem& Item : Model.Feed)
		{
			const TArray<FString> Lines = P.Wrap(Item.Text, GW - 24.0f, 11.0f, !Item.bNews && !Item.bInk);
			const int32 LineCount = FMath::Min(Lines.Num(), 5);
			const float H = 12.0f + 14.0f * static_cast<float>(LineCount) + (Item.bNews ? 12.0f : 0.0f);
			if (Y + H > BottomY)
			{
				break;
			}
			P.Rect(GX, Y, GW, H, WithAlpha(C.PanelSolid, 0.94f * Item.Alpha), 9.0f);
			P.Rect(GX, Y, 4.0f, H, WithAlpha(Item.bNews ? C.Amber : Item.Color, Item.Alpha), 2.0f);   // chatter keeps its sentiment colour here only
			float Ty = Y + 6.0f;
			if (Item.bNews)
			{
				P.Text(TEXT("NEWS"), GX + 12.0f, Ty, 9.0f, WithAlpha(C.Amber, Item.Alpha), EShiftAlign::Left, true);
				Ty += 12.0f;
			}
			for (int32 L = 0; L < LineCount; ++L)
			{
				P.Text(Lines[L], GX + 12.0f, Ty, 11.0f, WithAlpha((Item.bNews || Item.bInk) ? C.Ink : Item.Color, Item.Alpha), EShiftAlign::Left, !Item.bNews && !Item.bInk);
				Ty += 14.0f;
			}
			Y += H + 5.0f;
		}

		// hovering the sockpuppet-votes chip explains it, in words, to the left of the gutter
		if (bFakeHover && !Model.FakeInfo.IsEmpty())
		{
			const float CW = 300.0f;
			const TArray<FString> Lines = P.Wrap(Model.FakeInfo, CW - 28.0f, 12.0f, false);
			const float CH = 38.0f + 16.0f * static_cast<float>(Lines.Num());
			const float CX = GX - 10.0f - CW;
			const float CY = FMath::Clamp(FakeY, 48.0f, 668.0f - CH);
			P.Rect(CX, CY, CW, CH, WithAlpha(C.PanelSolid, 0.98f), 10.0f);
			P.Rect(CX, CY, 4.0f, CH, Purple, 2.0f);
			P.Text(TEXT("SOCKPUPPET VOTES"), CX + 14.0f, CY + 9.0f, 12.0f, Purple, EShiftAlign::Left, true);
			float Ty = CY + 30.0f;
			for (const FString& Line : Lines)
			{
				P.Text(Line, CX + 14.0f, Ty, 12.0f, C.Ink);
				Ty += 16.0f;
			}
		}
	}

	// ------------------------------------------------------------------ the jam ran out: "TIME" across the board while the results are held back a moment
	if (Model.TimeBanner > 0.01f)
	{
		Dock(P, EShiftDock::C);
		const FVector4f Win = WindowRect(P);
		const float A = Model.TimeBanner;
		P.Rect(Win.X, 288.0f, Win.Z - Win.X, 136.0f, WithAlpha(Hex(0x04060F), 0.72f * A));
		P.Text(TEXT("TIME"), 640.0f, 296.0f, 72.0f, WithAlpha(FLinearColor::White, A), EShiftAlign::Center, true);
		P.Text(TEXT("The vote is closed. Counting the votes..."), 640.0f, 392.0f, 16.0f, WithAlpha(C.Teal, A), EShiftAlign::Center, true);
	}

	// ------------------------------------------------------------------ tooltip (it belongs to the board: the centre dock)
	Dock(P, EShiftDock::C);
	if (Model.Tooltip.bVisible && !Model.bOver && !Model.bPaused)
	{
		PaintTooltip(P, C);
	}

	// ------------------------------------------------------------------ the edge frame: a thin line along the left, right and bottom edges for a strike, the Blame offer and a disqualification
	// (the voters stay inside x 56..1080, so it never covers one)
	if (Model.EdgeFlash > 0.01f)
	{
		const FLinearColor Edge = WithAlpha(Model.EdgeColor, 0.80f * FMath::Clamp(Model.EdgeFlash, 0.0f, 1.0f));
		Dock(P, EShiftDock::TL);
		const FVector4f Win = WindowRect(P);   // the strips hug the window's own edges
		P.Rect(Win.X, 41.0f, 14.0f, Win.W - 41.0f, Edge);
		P.Rect(Win.Z - 9.0f, 41.0f, 9.0f, Win.W - 41.0f, Edge);
		P.Rect(Win.X + 14.0f, Win.W - 8.0f, Win.Z - Win.X - 23.0f, 8.0f, Edge);
		Dock(P, EShiftDock::C);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// results

void SShiftHud::PaintScatter(FShiftPainter& P, const FPal& C, float X, float Y, float W, float H, const FString& Title, const TArray<FShiftHudDot>& Dots,
	const FShiftHudFit& Fit, const FString& Caption) const
{
	P.Rect(X, Y, W, H, WithAlpha(C.Track, 0.5f), 12.0f);
	P.Text(Title, X + 12.0f, Y + 8.0f, 12.0f, C.Ink, EShiftAlign::Left, true);

	const float Px0 = X + 14.0f;
	const float Py0 = Y + 34.0f;
	const float Pw = W - 28.0f;
	const float Ph = H - 82.0f;

	float XMin = FLT_MAX, XMax = -FLT_MAX, YMax = 0.001f;
	for (const FShiftHudDot& D : Dots)
	{
		XMin = FMath::Min(XMin, D.X);
		XMax = FMath::Max(XMax, D.X);
		YMax = FMath::Max(YMax, D.Y);
	}
	if (Dots.Num() == 0 || XMax - XMin < 1.0e-5f)
	{
		XMin = 0.0f;
		XMax = 1.0f;
	}
	const float XPad = (XMax - XMin) * 0.08f;
	XMin -= XPad;
	XMax += XPad;
	YMax *= 1.12f;

	auto ToX = [&](float V) { return Px0 + Pw * (V - XMin) / (XMax - XMin); };
	auto ToY = [&](float V) { return Py0 + Ph * (1.0f - V / YMax); };

	P.Line(FVector2f(Px0, Py0 + Ph), FVector2f(Px0 + Pw, Py0 + Ph), C.Stroke, 1.2f);
	P.Line(FVector2f(Px0, Py0), FVector2f(Px0, Py0 + Ph), C.Stroke, 1.2f);

	// regression line, clipped to the plot
	const float Y0 = Fit.Intercept + Fit.Slope * XMin;
	const float Y1 = Fit.Intercept + Fit.Slope * XMax;
	if (Dots.Num() >= 3)
	{
		const float C0 = FMath::Clamp(Y0, 0.0f, YMax);
		const float C1 = FMath::Clamp(Y1, 0.0f, YMax);
		float Xa = XMin, Xb = XMax;
		if (FMath::Abs(Fit.Slope) > 1.0e-9f)
		{
			Xa = FMath::Clamp((C0 - Fit.Intercept) / Fit.Slope, XMin, XMax);
			Xb = FMath::Clamp((C1 - Fit.Intercept) / Fit.Slope, XMin, XMax);
		}
		P.Line(FVector2f(ToX(Xa), ToY(C0)), FVector2f(ToX(Xb), ToY(C1)), WithAlpha(C.Ink, 0.45f), 1.6f);
	}

	for (const FShiftHudDot& D : Dots)
	{
		if (D.bPlayer)
		{
			P.Disc(ToX(D.X), ToY(D.Y), 9.0f, WithAlpha(C.Teal, 0.35f));
		}
		P.Disc(ToX(D.X), ToY(D.Y), D.bPlayer ? 5.5f : 4.5f, D.Color);
	}

	P.Text(Caption, X + W * 0.5f, Y + H - 44.0f, 11.0f, C.InkDim, EShiftAlign::Center);
	P.Text(FString::Printf(TEXT("explains %.0f%% of the vote"), Fit.R2 * 100.0f), X + W * 0.5f, Y + H - 28.0f, 14.0f,
		Fit.R2 > 0.5f ? C.Red : C.Ink, EShiftAlign::Center, true);
}

void SShiftHud::PaintTimeline(FShiftPainter& P, const FPal& C, float X, float Y, float W, float H) const
{
	P.Rect(X, Y, W, H, WithAlpha(C.Track, 0.5f), 12.0f);
	P.Text(TEXT("Share of the vote, day by day"), X + 12.0f, Y + 8.0f, 12.0f, C.Ink, EShiftAlign::Left, true);

	const int32 E = Model.TimelineEntries;
	const int32 N = Model.TimelineT.Num();
	const float Px0 = X + 44.0f;
	const float Py0 = Y + 34.0f;
	const float Pw = W - 62.0f;
	const float Ph = H - 84.0f;
	if (E <= 0 || N < 2 || Model.TimelineShares.Num() < E * N)
	{
		P.Text(TEXT("No timeline for this run."), X + W * 0.5f, Y + H * 0.5f, 13.0f, C.InkDim, EShiftAlign::Center);
		return;
	}
	float YMax = 0.10f;
	for (const float V : Model.TimelineShares)
	{
		YMax = FMath::Max(YMax, V);
	}
	const float GridStep = YMax > 0.5f ? 0.2f : (YMax > 0.25f ? 0.1f : 0.05f);   // round percent steps: 5, 10 or 20
	YMax = FMath::Min(1.0f, FMath::CeilToFloat(YMax * 1.05f / GridStep) * GridStep);
	const int32 GridLines = FMath::RoundToInt(YMax / GridStep);
	auto ToX = [&](float T) { return Px0 + Pw * T; };
	auto ToY = [&](float V) { return Py0 + Ph * (1.0f - V / YMax); };

	// the grid: a horizontal line every 5, 10 or 20 percent, day ticks along the bottom
	for (int32 G = 0; G <= GridLines; ++G)
	{
		const float V = GridStep * static_cast<float>(G);
		P.Line(FVector2f(Px0, ToY(V)), FVector2f(Px0 + Pw, ToY(V)), WithAlpha(C.Stroke, G == 0 ? 1.0f : 0.45f), G == 0 ? 1.2f : 1.0f);
		P.Text(FString::Printf(TEXT("%.0f%%"), V * 100.0f), Px0 - 6.0f, ToY(V) - 7.0f, 10.0f, C.InkDim, EShiftAlign::Right);
	}
	const int32 Days = FMath::Max(1, Model.TimelineDays);
	for (const int32 Day : { 1, 8, 15, 22, Days })
	{
		if (Day > Days)
		{
			continue;
		}
		const float Tx = ToX(static_cast<float>(Day - 1) / static_cast<float>(Days));
		P.Line(FVector2f(Tx, Py0 + Ph), FVector2f(Tx, Py0 + Ph + 4.0f), C.Stroke, 1.2f);
		P.Text(FString::Printf(TEXT("day %d"), Day), Tx, Py0 + Ph + 6.0f, 10.0f, C.InkDim, EShiftAlign::Center);
	}

	// the moments: a thin line from the axis, a dot on it (strike red, a celebrity won teal, the rival's final push amber)
	for (const FShiftHudMark& Mark : Model.TimelineMarks)
	{
		const FLinearColor Col = Mark.Kind == 0 ? C.Red : (Mark.Kind == 1 ? C.Teal : C.Amber);
		P.Line(FVector2f(ToX(Mark.T), Py0), FVector2f(ToX(Mark.T), Py0 + Ph), WithAlpha(Col, 0.30f), 1.2f);
		P.Disc(ToX(Mark.T), Py0 + Ph, 3.6f, Col);
	}

	// the lines: the other entries first, the player's on top and thick
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 Entry = 0; Entry < E; ++Entry)
		{
			const bool bPlayer = Entry == 0;
			if ((Pass == 1) != bPlayer)
			{
				continue;
			}
			const FLinearColor Col = ShiftPalette::Entry(Entry);
			for (int32 I = 1; I < N; ++I)
			{
				P.Line(FVector2f(ToX(Model.TimelineT[I - 1]), ToY(Model.TimelineShares[(I - 1) * E + Entry])), FVector2f(ToX(Model.TimelineT[I]), ToY(Model.TimelineShares[I * E + Entry])),
					bPlayer ? Col : WithAlpha(Col, 0.85f), bPlayer ? 3.6f : 1.7f);
			}
			if (Model.bMarkers)
			{
				DrawMarker(P, Entry, ToX(Model.TimelineT[N - 1]) + 7.0f, ToY(Model.TimelineShares[(N - 1) * E + Entry]), 3.6f);
			}
		}
	}

	// the key
	const float Ky = Y + H - 22.0f;
	float Kx = X + 16.0f;
	const TCHAR* const Names[3] = { TEXT("strike"), TEXT("celebrity won"), TEXT("rival's final push") };
	const FLinearColor Cols[3] = { C.Red, C.Teal, C.Amber };
	for (int32 K = 0; K < 3; ++K)
	{
		P.Disc(Kx + 4.0f, Ky + 7.0f, 3.6f, Cols[K]);
		P.Text(Names[K], Kx + 14.0f, Ky, 11.0f, C.InkDim);
		Kx += 24.0f + 6.2f * static_cast<float>(FCString::Strlen(Names[K]));
	}
}

void SShiftHud::PaintResults(FShiftPainter& P, const FPal& C) const
{
	Hits.Reset();
	Dock(P, EShiftDock::C);
	// the card eases in: the dim fades up and the card rises into place over 0.4 s (not under Calm, not in QA runs)
	const float Ease = Model.bEase ? 1.0f - FMath::Pow(1.0f - FMath::Clamp(ResultsAge / 0.4f, 0.0f, 1.0f), 3.0f) : 1.0f;
	FillWindow(P, Hex(0x000000, 0.58f * Ease));
	P.Origin.Y += (1.0f - Ease) * 30.0f * P.Scale;
	HitOrigin.Y = P.Origin.Y;
	P.Rect(90, 44, 1100, 632, Mix(Hex(0xFFFFFF, 0.98f), Hex(0x0A0D1C, 0.98f), C.N), 20);

	P.Text(Model.OutcomeTitle, 640, 58, 40, Model.OutcomeColor, EShiftAlign::Center, true);
	float Y = P.TextBlock(Model.OutcomeLine1, 640, 112, 960, 16, 21, C.Ink, EShiftAlign::Center);
	P.Text(Model.OutcomeLine2, 640, Y + 4.0f, 13, C.InkDim, EShiftAlign::Center);

	const float ListY = 214.0f;
	float Ry = ListY;
	for (const FShiftHudRow& Row : Model.Rows)
	{
		if (Row.bPlayer)
		{
			P.Rect(116, Ry - 3, 470, 26, WithAlpha(C.Teal, 0.18f), 8);
		}
		P.Text(FString::FromInt(Row.Rank + 1), 138, Ry + 1, 14, Row.bPlayer ? C.Teal : C.Ink, EShiftAlign::Center, true);
		P.Disc(164, Ry + 10, 5.5f, Row.Color);
		if (Model.bMarkers) { DrawMarker(P, Row.EntryId, 164, Ry + 10, 3.6f); }
		P.Text(P.Fit(Row.Name, 250.0f, 13, Row.bPlayer), 178, Ry, 13, C.Ink, EShiftAlign::Left, Row.bPlayer);
		P.Text(FString::Printf(TEXT("%s   %.1f%%"), *Compact(Row.Votes), Row.Share * 100.0f), 574, Ry + 1, 12, C.InkDim, EShiftAlign::Right);
		Ry += 27.0f;
	}
	if (!Model.PlaysLine.IsEmpty())
	{
		P.TextBlock(Model.PlaysLine, 116.0f, Ry + 6.0f, 470.0f, 12.0f, 16.0f, C.InkDim);
	}

	// the chart area has two tabs: the thesis plots (quality and fame against the votes), and the shares over the jam
	{
		const TCHAR* const Tabs[2] = { TEXT("Quality and fame"), TEXT("Over time") };
		const float TabW[2] = { 134.0f, 92.0f };
		float Tx = 610.0f;
		for (int32 T = 0; T < 2; ++T)
		{
			const bool bOn = Model.ResultsTab == T;
			const bool bHover = AddHit(Tx, 178.0f, TabW[T], 24.0f, EShiftUi::ResultsTab0 + T);
			P.Rect(Tx, 178.0f, TabW[T], 24.0f, bOn ? C.Teal : (bHover ? WithAlpha(C.Teal, 0.30f) : WithAlpha(C.Track, 0.7f)), 12.0f);
			P.Text(Tabs[T], Tx + TabW[T] * 0.5f, 183.0f, 12.0f, bOn ? FLinearColor::White : C.Ink, EShiftAlign::Center, bOn);
			Tx += TabW[T] + 8.0f;
		}
	}
	if (Model.ResultsTab == 1)
	{
		PaintTimeline(P, C, 610.0f, ListY - 6.0f, 572.0f, 250.0f);
	}
	else
	{
		PaintScatter(P, C, 610.0f, ListY - 6.0f, 280.0f, 250.0f, TEXT("Quality vs votes"), Model.QualityDots, Model.QualityFit, TEXT("better game  ->"));
		PaintScatter(P, C, 902.0f, ListY - 6.0f, 280.0f, 250.0f, TEXT("Starting fame vs votes"), Model.FameDots, Model.FameFit, TEXT("more followers at the start  ->"));
	}

	P.Text(Model.OutcomeLine3, 640, 500, 13, C.InkDim, EShiftAlign::Center);
	if (!Model.OutcomeNews.IsEmpty())
	{
		P.Text(TEXT("NEWS"), 640, 522, 9, C.Amber, EShiftAlign::Center, true);
		P.Text(Model.OutcomeNews, 640, 536, 12, C.Ink, EShiftAlign::Center);
	}
	if (Model.NewAchievements.Num() > 0)
	{
		FString Names;
		for (const FString& Name : Model.NewAchievements)
		{
			Names += (Names.IsEmpty() ? TEXT("") : TEXT("  |  ")) + Name;
		}
		P.Text(TEXT("ACHIEVEMENT UNLOCKED"), 640, 558, 9, C.Teal, EShiftAlign::Center, true);
		P.Text(Names, 640, 572, 13, C.Teal, EShiftAlign::Center, true);
	}

	// honest vs sockpuppet bar
	const float BarX = 116.0f;
	const float BarW = 1048.0f;
	P.Text(TEXT("YOUR VOTES"), BarX, 538.0f, 10.0f, C.InkDim, EShiftAlign::Left, true);
	P.Rect(BarX, 556.0f, BarW, 10.0f, C.Track, 5.0f);
	const float Honest = FMath::Clamp(Model.HonestShare, 0.0f, 1.0f);
	const float Sock = FMath::Clamp(Model.SockShare, 0.0f, 1.0f - Honest);
	P.Rect(BarX, 556.0f, BarW * Honest, 10.0f, C.Teal, 5.0f);
	if (Sock > 0.0005f)
	{
		P.Rect(BarX + BarW * Honest, 556.0f, BarW * Sock, 10.0f, Hex(0x8B6CFF), 5.0f);
	}
	P.Text(FString::Printf(TEXT("honest %.1f%%"), Honest * 100.0f), BarX, 572.0f, 12.0f, C.Teal, EShiftAlign::Left, true);
	P.Text(FString::Printf(TEXT("sockpuppets %.1f%%"), Sock * 100.0f), BarX + BarW, 572.0f, 12.0f, Hex(0x8B6CFF), EShiftAlign::Right, true);
	if (!Model.RecordingPath.IsEmpty())
	{
		P.Text(P.Fit(FString::Printf(TEXT("Recorded: %s. Send it to the developer to show how you played (O opens the folder)."), *FPaths::GetCleanFilename(Model.RecordingPath)), BarW, 11.0f), BarX, 588.0f, 11.0f, C.InkDim);
	}

	// the row of buttons, centred on the card: Try again, Next jam (when there is one), Menu, Copy result and, when this jam was recorded, the recordings folder
	const float By = 606.0f;
	const float Gap = 12.0f;
	const float WNext = Model.bCanNextJam ? 190.0f : 0.0f;
	const float WFiles = Model.RecordingPath.IsEmpty() ? 0.0f : 110.0f;
	const float WWatch = Model.RecordingPath.IsEmpty() ? 0.0f : 150.0f;
	const int32 Buttons = 3 + (Model.bCanNextJam ? 1 : 0) + (WFiles > 0.0f ? 1 : 0) + (WWatch > 0.0f ? 1 : 0);
	float X = 640.0f - 0.5f * (170.0f + WNext + 150.0f + 140.0f + WFiles + WWatch + Gap * static_cast<float>(Buttons - 1));
	Button(P, C, EShiftUi::Retry, X, By, 170.0f, 46.0f, TEXT("Try again  (R)"), true, 17.0f);
	X += 170.0f + Gap;
	if (Model.bCanNextJam)
	{
		Button(P, C, EShiftUi::NextJam, X, By, WNext, 46.0f, TEXT("Next jam  (ENTER)"), false, 17.0f);
		X += WNext + Gap;
	}
	Button(P, C, EShiftUi::Menu, X, By, 150.0f, 46.0f, TEXT("Menu  (ESC)"), false, 17.0f);
	X += 150.0f + Gap;
	Button(P, C, EShiftUi::Share, X, By, 140.0f, 46.0f, TEXT("Copy result"), false, 15.0f);
	X += 140.0f + Gap;
	if (WWatch > 0.0f)
	{
		Button(P, C, EShiftUi::WatchReplay, X, By, WWatch, 46.0f, TEXT("Watch replay"), false, 15.0f);
		X += WWatch + Gap;
	}
	if (WFiles > 0.0f)
	{
		Button(P, C, EShiftUi::Recordings, X, By, WFiles, 46.0f, TEXT("Files  (O)"), false, 15.0f);
	}
	Dock(P, EShiftDock::TR);
	SettingsButton(P, C, 1250.0f, 20.0f);
	Dock(P, EShiftDock::C);
}

void SShiftHud::PaintPause(FShiftPainter& P, const FPal& C) const
{
	Hits.Reset();
	Dock(P, EShiftDock::TL);
	{
		const FVector4f Win = WindowRect(P);
		P.Rect(Win.X, 41, Win.Z - Win.X, Win.W - 41 - 44, Hex(0x000000, 0.45f));   // between the two bars, edge to edge
	}
	Dock(P, EShiftDock::C);
	P.Text(TEXT("PAUSED"), 640, Model.bInLesson ? 270.0f : 210.0f, 52, FLinearColor::White, EShiftAlign::Center, true);
	float By = Model.bInLesson ? 350.0f : 290.0f;
	Button(P, C, EShiftUi::Resume, 520.0f, By, 240.0f, 50.0f, TEXT("Resume  (SPACE)"), true, 18.0f);
	By += 64.0f;
	Button(P, C, EShiftUi::Retry, 520.0f, By, 240.0f, 46.0f, Model.bInLesson ? TEXT("Restart lesson  (R)") : TEXT("Restart  (R)"), false, 17.0f);
	By += 60.0f;
	Button(P, C, EShiftUi::HowTo, 520.0f, By, 240.0f, 46.0f, TEXT("How to play  (H)"), false, 17.0f);
	By += 60.0f;
	Button(P, C, EShiftUi::Menu, 520.0f, By, 240.0f, 46.0f, Model.bInLesson ? TEXT("Leave lesson") : TEXT("Quit to menu"), false, 17.0f);
	if (Model.bInLesson)
	{
		Dock(P, EShiftDock::TR);
		SettingsButton(P, C, 1250.0f, 20.0f);
		Dock(P, EShiftDock::C);
		return;
	}

	// what the build the player uploaded comes to, in numbers
	P.Rect(400.0f, 530.0f, 480.0f, 104.0f, WithAlpha(C.PanelSolid, 0.92f), 12.0f);
	P.Text(TEXT("YOUR BUILD"), 420.0f, 540.0f, 11.0f, C.InkDim, EShiftAlign::Left, true);
	static const TCHAR* const Labels[3] = { TEXT("Quality"), TEXT("Thumbnail"), TEXT("Network") };
	for (int32 i = 0; i < 3; ++i)
	{
		const float Ly = 560.0f + 22.0f * static_cast<float>(i);
		P.Text(FString::Printf(TEXT("%s %d h"), Labels[i], Model.Build.Hours[i]), 420.0f, Ly, 13.0f, C.Ink, EShiftAlign::Left, true);
		P.Text(P.Fit(Model.Build.Effects[i], 300.0f, 12.0f), 560.0f, Ly + 1.0f, 12.0f, C.InkDim);
	}
	Dock(P, EShiftDock::TR);
	SettingsButton(P, C, 1250.0f, 20.0f);
	Dock(P, EShiftDock::C);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// guided lessons and the reference page

void SShiftHud::PaintLesson(FShiftPainter& P, const FPal& C) const
{
	const FShiftLessonView& L = Model.Lesson;
	const float Pulse = 0.5f + 0.5f * FMath::Sin(PulseTime * 6.0f);
	const FLinearColor Glow = Hex(0xFFB020);

	// the part of the HUD the step points at (the rectangles are where PaintPlay draws them)
	Dock(P, EShiftDock::C);
	float Ax = 0.0f, Ay = 0.0f, Aw = 0.0f, Ah = 0.0f;
	EShiftDock AnchorDock = EShiftDock::C;
	switch (L.Anchor)
	{
	case EShiftLessonAnchor::Leaderboard: Ax = 1080.0f; Ay = 44.0f; Aw = 194.0f; Ah = 110.0f; AnchorDock = EShiftDock::TR; break;
	case EShiftLessonAnchor::Posts: Ax = 12.0f; Ay = 680.0f; Aw = 240.0f; Ah = 38.0f; AnchorDock = EShiftDock::BL; break;
	case EShiftLessonAnchor::NightChip: Ax = 494.0f; Ay = 680.0f; Aw = 164.0f; Ah = 36.0f; AnchorDock = EShiftDock::BL; break;
	case EShiftLessonAnchor::Heat: Ax = 794.0f; Ay = 680.0f; Aw = 270.0f; Ah = 38.0f; AnchorDock = EShiftDock::BR; break;
	default: break;
	}
	const FVector2f AnchorShift = DockShift(AnchorDock);   // those rectangles are where PaintPlay draws them in their dock: moved into the centre dock the card is placed in
	Ax += AnchorShift.X;
	Ay += AnchorShift.Y;
	if (Aw > 0.0f)
	{
		P.Rect(Ax - 4.0f, Ay - 4.0f, Aw + 8.0f, Ah + 8.0f, WithAlpha(Glow, 0.16f + 0.22f * Pulse), 12.0f);
	}

	// the card: room for the text, the buttons and the step dots
	const float W = 440.0f;
	const TArray<FString> Lines = P.Wrap(L.Body, W - 36.0f, 14.0f);
	const float Footer = L.bFinish ? 64.0f : (L.bReading ? 58.0f : 34.0f);
	const float H = 46.0f + 19.0f * static_cast<float>(Lines.Num()) + Footer;
	const FVector2f Candidates[4] = { FVector2f(22.0f, 54.0f), FVector2f(22.0f, 668.0f - H), FVector2f(596.0f, 54.0f), FVector2f(596.0f, 668.0f - H) };
	int32 Chosen = 0;
	for (int32 i = 0; i < 4; ++i)
	{
		bool bFree = true;
		for (const FShiftLessonSpot& Spot : L.Spots)
		{
			const float Pad = Spot.Radius + 44.0f;
			bFree = bFree && !(Spot.Center.X > Candidates[i].X - Pad && Spot.Center.X < Candidates[i].X + W + Pad && Spot.Center.Y > Candidates[i].Y - Pad && Spot.Center.Y < Candidates[i].Y + H + Pad);
		}
		if (Aw > 0.0f)
		{
			bFree = bFree && !(Ax < Candidates[i].X + W && Ax + Aw > Candidates[i].X && Ay < Candidates[i].Y + H && Ay + Ah > Candidates[i].Y);
		}
		if (bFree)
		{
			Chosen = i;
			break;
		}
	}
	const float X = Candidates[Chosen].X;
	const float Y = Candidates[Chosen].Y;

	// the voters to use: two pulsing rings and a label, joined to the card by a line
	for (const FShiftLessonSpot& Spot : L.Spots)
	{
		P.Line(FVector2f(X + W * 0.5f, Y + H * 0.5f), Spot.Center, WithAlpha(Glow, 0.45f), 2.0f);
	}
	P.Rect(X - 2.0f, Y - 2.0f, W + 4.0f, H + 4.0f, Glow, 16.0f);
	P.Rect(X, Y, W, H, WithAlpha(C.PanelSolid, 0.98f), 14.0f);
	for (const FShiftLessonSpot& Spot : L.Spots)
	{
		P.Arc(Spot.Center.X, Spot.Center.Y, Spot.Radius + 10.0f + 4.0f * Pulse, 0.0f, 1.0f, WithAlpha(Glow, 0.95f), 3.5f);
		P.Arc(Spot.Center.X, Spot.Center.Y, Spot.Radius + 20.0f + 6.0f * Pulse, 0.0f, 1.0f, WithAlpha(Glow, 0.45f), 2.5f);
		if (!Spot.Label.IsEmpty())
		{
			const float LabelW = P.Measure(Spot.Label, 13.0f, true).X + 22.0f;
			float Ly = Spot.Center.Y - Spot.Radius - 50.0f;
			if (Ly < 48.0f)
			{
				Ly = Spot.Center.Y + Spot.Radius + 26.0f;
			}
			P.Rect(Spot.Center.X - LabelW * 0.5f, Ly, LabelW, 24.0f, WithAlpha(Glow, 0.96f), 12.0f);
			P.Text(Spot.Label, Spot.Center.X, Ly + 4.0f, 13.0f, Hex(0x3A2600), EShiftAlign::Center, true);
		}
	}

	P.Text(L.Title, X + 18.0f, Y + 12.0f, 11.0f, C.InkDim, EShiftAlign::Left, true);
	for (int32 i = 0; i < L.Steps; ++i)
	{
		const float Dx = X + W - 18.0f - 12.0f * static_cast<float>(L.Steps - 1 - i);
		P.Disc(Dx, Y + 19.0f, i == L.Step - 1 ? 4.5f : 3.5f, i < L.Step - 1 ? C.Teal : (i == L.Step - 1 ? Glow : C.Track));
	}
	P.TextBlock(L.Body, X + 18.0f, Y + 36.0f, W - 36.0f, 14.0f, 19.0f, C.Ink);

	const float Fy = Y + H - Footer + 8.0f;
	if (L.bFinish)
	{
		float Bx = X + 18.0f;
		if (L.bHasNext)
		{
			Button(P, C, EShiftUi::LessonNext, Bx, Fy, 138.0f, 38.0f, TEXT("Next lesson"), true, 15.0f);
			Bx += 148.0f;
		}
		Button(P, C, EShiftUi::LessonPlay, Bx, Fy, 138.0f, 38.0f, TEXT("Play a jam"), !L.bHasNext, 15.0f);
		Bx += 148.0f;
		Button(P, C, EShiftUi::LessonLeave, Bx, Fy, 100.0f, 38.0f, TEXT("Menu"), false, 15.0f);
	}
	else if (L.bReading)
	{
		Button(P, C, EShiftUi::LessonContinue, X + W - 18.0f - 176.0f, Fy, 176.0f, 38.0f, TEXT("Continue  (ENTER)"), true, 15.0f);
		const bool bHoverLeave = AddHit(X + 18.0f, Fy + 8.0f, 110.0f, 24.0f, EShiftUi::LessonLeave);
		P.Text(TEXT("Leave lesson"), X + 18.0f, Fy + 10.0f, 12.0f, bHoverLeave ? C.Red : C.InkDim);
	}
	else if (L.bCheer)
	{
		P.Text(TEXT("Nice."), X + 18.0f, Fy, 17.0f, C.Teal, EShiftAlign::Left, true);
	}
	else
	{
		P.Text(TEXT("Do it to carry on."), X + 18.0f, Fy, 12.0f, C.InkDim);
		const bool bHoverLeave = AddHit(X + W - 128.0f, Fy - 4.0f, 110.0f, 24.0f, EShiftUi::LessonLeave);
		P.Text(TEXT("Leave lesson"), X + W - 18.0f, Fy - 2.0f, 12.0f, bHoverLeave ? C.Red : C.InkDim, EShiftAlign::Right);
	}
}

void SShiftHud::PaintExtras(FShiftPainter& P, const FPal& C) const
{
	Hits.Reset();
	bModal = true;
	Dock(P, EShiftDock::C);
	AddWindowHit(P, EShiftUi::ExtrasClose);   // a click anywhere closes the page
	FillWindow(P, Hex(0x000000, 0.62f));
	if (Model.ExtrasPage == 3)
	{
		// the replays: the newest recordings, each with a Watch button
		const float W = 780.0f;
		const float RowH = 54.0f;
		const float Gap = 8.0f;
		const int32 N = Model.Replays.Num();
		const float ListH = N > 0 ? static_cast<float>(N) * (RowH + Gap) : 56.0f;
		const float H = 24.0f + 40.0f + 30.0f + 14.0f + ListH + 30.0f + 18.0f;
		const float X = (DesignW - W) * 0.5f;
		const float Y = (DesignH - H) * 0.5f;
		P.Rect(X, Y, W, H, C.PanelSolid, 18.0f);
		P.Text(TEXT("REPLAYS"), X + W * 0.5f, Y + 20.0f, 30.0f, C.Ink, EShiftAlign::Center, true);
		P.Text(TEXT("Your latest jams, played back exactly as you played them."), X + W * 0.5f, Y + 62.0f, 13.0f, C.InkDim, EShiftAlign::Center);
		float Ry = Y + 94.0f;
		if (N == 0)
		{
			P.Text(TEXT("Nothing recorded yet. Every jam you finish is saved here."), X + W * 0.5f, Ry + 18.0f, 15.0f, C.InkDim, EShiftAlign::Center);
		}
		for (int32 I = 0; I < N; ++I)
		{
			const FShiftHudReplayRow& Row = Model.Replays[I];
			P.Rect(X + 24.0f, Ry, W - 48.0f, RowH, WithAlpha(C.Ink, 0.06f), 10.0f);
			P.Text(P.Fit(Row.Title, W - 48.0f - 150.0f, 15.0f, true), X + 42.0f, Ry + 8.0f, 15.0f, C.Ink, EShiftAlign::Left, true);
			P.Text(P.Fit(Row.Detail, W - 48.0f - 150.0f, 12.0f), X + 42.0f, Ry + 31.0f, 12.0f, Row.bStale ? C.Amber : C.InkDim);
			Button(P, C, EShiftUi::ReplayWatch0 + I, X + W - 24.0f - 112.0f, Ry + 7.0f, 104.0f, 40.0f, TEXT("Watch"), false, 15.0f);
			Ry += RowH + Gap;
		}
		P.Text(TEXT("Click anywhere to close"), X + W * 0.5f, Y + H - 30.0f, 11.0f, C.InkDim, EShiftAlign::Center);
		return;
	}
	if (Model.ExtrasPage == 2)
	{
		// the credits are a small card, as tall as its lines
		float Body = 0.0f;
		for (const FString& Line : Model.CreditsText)
		{
			Body += Line.IsEmpty() ? 12.0f : (Line.EndsWith(TEXT(":")) ? 24.0f : 21.0f);
		}
		const float W = 380.0f;
		const float H = 56.0f + 28.0f + Body + 20.0f + 28.0f;
		const float X = (DesignW - W) * 0.5f;
		const float Y = (DesignH - H) * 0.5f;
		P.Rect(X, Y, W, H, C.PanelSolid, 18.0f);
		P.Text(TEXT("CREDITS"), X + W * 0.5f, Y + 22.0f, 28.0f, C.Ink, EShiftAlign::Center, true);
		float Cy = Y + 84.0f;
		for (const FString& Line : Model.CreditsText)
		{
			if (Line.IsEmpty())
			{
				Cy += 12.0f;
				continue;
			}
			const bool bHead = Line.EndsWith(TEXT(":"));
			P.Text(bHead ? Line.LeftChop(1) : Line, X + W * 0.5f, Cy, bHead ? 15.0f : 16.0f, bHead ? C.Teal : C.Ink, EShiftAlign::Center, bHead);
			Cy += bHead ? 24.0f : 21.0f;
		}
		P.Text(TEXT("Click anywhere to close"), X + W * 0.5f, Y + H - 28.0f, 11.0f, C.InkDim, EShiftAlign::Center);
		return;
	}
	const float X = 90.0f;
	const float Y = 28.0f;
	const float W = 1100.0f;
	const float H = 664.0f;   // room for 24 achievements in three columns (8 rows) under the career strip
	P.Rect(X, Y, W, H, C.PanelSolid, 18.0f);
	int32 Earned = 0;
	for (const FShiftHudAchItem& Item : Model.Achievements)
	{
		Earned += Item.bEarned ? 1 : 0;
	}
	P.Text(TEXT("ACHIEVEMENTS"), X + W * 0.5f, Y + 18.0f, 32.0f, C.Ink, EShiftAlign::Center, true);
	P.Text(FString::Printf(TEXT("%d of %d earned"), Earned, Model.Achievements.Num()), X + W * 0.5f, Y + 58.0f, 13.0f, C.InkDim, EShiftAlign::Center);

	// the career strip: one block per jam
	const float StripY = Y + 90.0f;
	const int32 Blocks = FMath::Max(1, Model.Career.Num());
	const float BlockW = (W - 72.0f - static_cast<float>(Blocks - 1) * 16.0f) / static_cast<float>(Blocks);
	for (int32 I = 0; I < Model.Career.Num(); ++I)
	{
		const FShiftHudCareerRow& R = Model.Career[I];
		const float Bx = X + 36.0f + static_cast<float>(I) * (BlockW + 16.0f);
		P.Rect(Bx, StripY, BlockW, 62.0f, WithAlpha(C.Ink, 0.06f), 10.0f);
		P.Text(R.Jam, Bx + 14.0f, StripY + 8.0f, 13.0f, C.Teal, EShiftAlign::Left, true);
		const FString Best = R.BestRank > 0 ? FString::Printf(TEXT("best %s place, %.1f%% of the vote"), *HudOrdinal(R.BestRank), R.BestShare * 100.0f) : FString(TEXT("not finished yet"));
		P.Text(FString::Printf(TEXT("%d run%s, %d won, %d disqualified"), R.Runs, R.Runs == 1 ? TEXT("") : TEXT("s"), R.Wins, R.Dq), Bx + 14.0f, StripY + 26.0f, 12.0f, C.Ink);
		P.Text(Best, Bx + 14.0f, StripY + 42.0f, 12.0f, C.InkDim);
	}

	// the grid: three columns
	const float GridY = StripY + 82.0f;
	const float CellW = (W - 72.0f - 32.0f) / 3.0f;
	for (int32 I = 0; I < Model.Achievements.Num(); ++I)
	{
		const FShiftHudAchItem& Item = Model.Achievements[I];
		const float Cx = X + 36.0f + static_cast<float>(I % 3) * (CellW + 16.0f);
		const float Cy = GridY + static_cast<float>(I / 3) * 58.0f;
		P.Rect(Cx, Cy, CellW, 52.0f, WithAlpha(Item.bEarned ? C.Teal : C.Ink, Item.bEarned ? 0.14f : 0.05f), 10.0f);
		P.Disc(Cx + 26.0f, Cy + 26.0f, 13.0f, Item.bEarned ? C.Teal : WithAlpha(C.Ink, 0.20f));
		P.Text(Item.bEarned ? TEXT("+") : TEXT("?"), Cx + 26.0f, Cy + 15.0f, 18.0f, Item.bEarned ? C.PanelSolid : C.InkDim, EShiftAlign::Center, true);
		const bool bShow = Item.bEarned || !Item.bHidden;
		P.Text(bShow ? Item.Name : FString(TEXT("Hidden achievement")), Cx + 52.0f, Cy + 6.0f, 14.0f, Item.bEarned ? C.Ink : C.InkDim, EShiftAlign::Left, true);
		P.TextBlock(bShow ? Item.Desc : FString(TEXT("Keep playing to find out.")), Cx + 52.0f, Cy + 25.0f, CellW - 64.0f, 11.0f, 13.0f, C.InkDim);
	}
	P.Text(TEXT("Click anywhere to close"), X + W * 0.5f, Y + H - 22.0f, 11.0f, C.InkDim, EShiftAlign::Center);
}

void SShiftHud::PaintHowTo(FShiftPainter& P, const FPal& C) const
{
	Hits.Reset();
	bModal = true;
	Dock(P, EShiftDock::C);
	AddWindowHit(P, EShiftUi::HowTo);   // a click anywhere closes it; the button sits on top
	FillWindow(P, Hex(0x000000, 0.62f));

	// the page is as tall as its text: every gap (under the title, between the rows, above the button, under the closing line) is the same
	const float W = 1020.0f;
	const float Pad = 36.0f;
	const float ColW = (W - 3.0f * Pad) * 0.5f;
	const float Gap = 28.0f;
	const float LineH = 19.0f;
	const float HeadH = 26.0f;
	const int32 Rows = (Model.HowToText.Num() + 1) / 2;
	TArray<float> RowH;
	RowH.Init(0.0f, Rows);
	TArray<FString> Heads, Bodies;
	for (int32 i = 0; i < Model.HowToText.Num(); ++i)
	{
		FString Head, Body;
		if (!Model.HowToText[i].Split(TEXT(":"), &Head, &Body))
		{
			Body = Model.HowToText[i];
		}
		Body.TrimStartInline();
		Heads.Add(Head);
		Bodies.Add(Body);
		RowH[i / 2] = FMath::Max(RowH[i / 2], HeadH + LineH * static_cast<float>(P.Wrap(Body, ColW, 14.0f, false).Num()));
	}
	float Content = 0.0f;
	for (const float H : RowH)
	{
		Content += H;
	}
	const float TitleH = 34.0f;
	const float ButtonH = 42.0f;
	const float CloseH = 12.0f;
	const float H = Pad + TitleH + Gap + Content + Gap * static_cast<float>(FMath::Max(0, Rows - 1)) + Gap + ButtonH + Gap + CloseH + Pad;
	const float X = (DesignW - W) * 0.5f;
	const float Y = (DesignH - H) * 0.5f;
	P.Rect(X, Y, W, H, C.PanelSolid, 18.0f);
	P.Text(TEXT("HOW TO PLAY"), X + W * 0.5f, Y + Pad - 4.0f, 34.0f, C.Ink, EShiftAlign::Center, true);
	float RowY = Y + Pad + TitleH + Gap;
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Col = 0; Col < 2; ++Col)
		{
			const int32 i = Row * 2 + Col;
			if (i >= Heads.Num())
			{
				break;
			}
			const float Bx = X + Pad + static_cast<float>(Col) * (ColW + Pad);
			P.Text(Heads[i], Bx, RowY, 16.0f, C.Teal, EShiftAlign::Left, true);
			P.TextBlock(Bodies[i], Bx, RowY + HeadH, ColW, 14.0f, LineH, C.Ink);
		}
		RowY += RowH[Row] + Gap;
	}
	Button(P, C, EShiftUi::LearnToPlay, X + W * 0.5f - 105.0f, RowY, 210.0f, ButtonH, TEXT("Learn to play"), true, 16.0f);
	P.Text(Model.Screen == EShiftScreen::Title ? TEXT("Click anywhere to close") : TEXT("Click anywhere or press H to close"), X + W * 0.5f, RowY + ButtonH + Gap, 11.0f, C.InkDim, EShiftAlign::Center);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the cursor set

#if !UE_BUILD_SHIPPING
void SShiftHud::PaintTrailer(FShiftPainter& P, const FPal& C) const
{
	const FShiftHudTrailer& T = Model.Trailer;
	// the whole widget in design units (the 1280 x 720 design rect is only its 16:9 middle)
	const FVector2f Local = FVector2f(P.Geo.GetLocalSize());
	const float X0 = -P.Origin.X / P.Scale;
	const float Y0 = -P.Origin.Y / P.Scale;
	const float W = Local.X / P.Scale;
	const float H = Local.Y / P.Scale;
	const FLinearColor Ink = Hex(0x0A0914);

	// spotlight: everything but the rectangle is dimmed, the rectangle gets a pulsing teal frame
	if (T.SpotAlpha > 0.001f)
	{
		// the rectangle is given for the unzoomed screen: it follows the zoom
		FVector4f Rect = T.SpotRect;
		if (T.bSpotMods)
		{
			// the MODS countdown: from the left edge of its icon to the end of its text (which changes width as it counts down), with a margin
			const float TextW = P.Measure(ModsLabel(Model), ModsTextPx, true).X;
			const float Margin = 7.0f;
			const float Left = ModsIconX - ModsIconR - Margin;
			Rect = FVector4f(Left, ModsIconY - ModsIconR - Margin, ModsTextX + TextW + Margin - Left, 2.0f * (ModsIconR + Margin));
		}
		const float Zm = FMath::Max(1.0f, T.Zoom);
		const float Sx = T.ZoomFocus.X + (Rect.X - T.ZoomFocus.X) * Zm;
		const float Sy = T.ZoomFocus.Y + (Rect.Y - T.ZoomFocus.Y) * Zm;
		const float Sw = Rect.Z * Zm;
		const float Sh = Rect.W * Zm;
		const FLinearColor Dim = WithAlpha(Ink, 0.62f * T.SpotAlpha);
		P.Rect(X0, Y0, W, Sy - Y0, Dim);
		P.Rect(X0, Sy + Sh, W, Y0 + H - (Sy + Sh), Dim);
		P.Rect(X0, Sy, Sx - X0, Sh, Dim);
		P.Rect(Sx + Sw, Sy, X0 + W - (Sx + Sw), Sh, Dim);
		const float Pulse = 0.5f + 0.5f * FMath::Sin(PulseTime * 7.0f);
		const FLinearColor Frame = WithAlpha(C.Teal, (0.65f + 0.35f * Pulse) * T.SpotAlpha);
		const float Th = 3.0f;
		P.Rect(Sx - Th, Sy - Th, Sw + 2.0f * Th, Th, Frame);
		P.Rect(Sx - Th, Sy + Sh, Sw + 2.0f * Th, Th, Frame);
		P.Rect(Sx - Th, Sy, Th, Sh, Frame);
		P.Rect(Sx + Sw, Sy, Th, Sh, Frame);
	}

	if (T.Black > 0.001f)
	{
		P.Rect(X0, Y0, W, H, WithAlpha(FLinearColor::Black, T.Black));
	}

	// "OR HOLD" and a SHIFT keycap under it, straight over the game: the keycap is the title logo's (it presses and turns violet while the night is held)
	if (T.KeyAlpha > 0.001f)
	{
		const float A = T.KeyAlpha;
		const float CardW = 380.0f;
		const float CardH = 300.0f;
		const float CardX = 640.0f - CardW * 0.5f;
		const float CardY = 200.0f;
		P.TextFx(TEXT("OR HOLD"), 640.0f, CardY + 22.0f, 62.0f, WithAlpha(FLinearColor::White, A), EShiftAlign::Center, EShiftTrailerFont::Black, 90, 5.0f, WithAlpha(Ink, 0.9f * A));   // no box: the words carry their own outline, like a slam without one
		PaintShiftKey(P, C, 640.0f, CardY + 184.0f, Model.bNightActive || Model.bShiftDown);
	}

	const bool bOnCard = T.Black > 0.9f;
	for (const FShiftTrailerText& Tx : T.Texts)
	{
		TArray<FString> Lines;
		Tx.Text.ToUpper().ParseIntoArray(Lines, TEXT("|"), false);
		if (Lines.IsEmpty())
		{
			continue;
		}
		const float Age = Tx.Age;
		const float Life = FMath::Max(0.4f, Tx.Life);
		if (Tx.bSlam)
		{
			// SHIFT split: a teal and a red copy start apart and snap onto the white text on the beat; on the way out they split again
			const float In = FMath::Clamp(Age / 0.16f, 0.0f, 1.0f);
			const float Ease = 1.0f - FMath::Pow(1.0f - In, 3.0f);
			const float Out = FMath::Clamp((Age - (Life - 0.26f)) / 0.26f, 0.0f, 1.0f);
			const float Alpha = FMath::Clamp(Age / 0.05f, 0.0f, 1.0f) * (1.0f - Out);
			if (Alpha <= 0.001f)
			{
				continue;
			}
			const float Split = 26.0f * (1.0f - Ease) + 22.0f * Out;
			const float Scale = 1.0f + 0.10f * (1.0f - Ease);
			const float Cx = Tx.X >= 0.0f ? Tx.X : 640.0f;   // x= puts the words off the middle (the title screen's menu sits on the left)
			float Px = Tx.Px > 0.0f ? Tx.Px : 84.0f;
			float Widest = 1.0f;
			for (const FString& Line : Lines)
			{
				Widest = FMath::Max(Widest, P.MeasureFx(Line, Px, EShiftTrailerFont::Black, 90).X);
			}
			Px *= FMath::Min(1.0f, 1100.0f / Widest);
			const float LineH = Px * 1.14f;
			const float Top = (Tx.Y >= 0.0f ? Tx.Y : 360.0f) - LineH * static_cast<float>(Lines.Num()) * 0.5f;
			if (!bOnCard && Tx.bBox)
			{
				// over the game: a dark box around the words, so the voters behind them never get in the way of reading
				const float BlockH = LineH * static_cast<float>(Lines.Num());
				const float BoxW = (FMath::Min(Widest, 1100.0f) + 110.0f) * Scale;
				const float BoxH = (BlockH + 56.0f) * Scale;
				const float BoxY = Top + BlockH * 0.5f - BoxH * 0.5f;
				P.Rect(Cx - BoxW * 0.5f - 1.5f, BoxY - 1.5f, BoxW + 3.0f, BoxH + 3.0f, WithAlpha(C.Teal, 0.55f * Alpha), 20.0f);
				P.Rect(Cx - BoxW * 0.5f, BoxY, BoxW, BoxH, WithAlpha(Ink, 0.88f * Alpha), 18.0f);
			}
			const float GhostAlpha = Alpha * (0.22f + 0.78f * FMath::Max(1.0f - Ease, Out));
			for (int32 i = 0; i < Lines.Num(); ++i)
			{
				const float Y = Top + LineH * static_cast<float>(i);
				P.TextFx(Lines[i], Cx - Split, Y + 4.0f * (1.0f - Ease), Px, WithAlpha(C.Teal, GhostAlpha), EShiftAlign::Center, EShiftTrailerFont::Black, 90, 0.0f, FLinearColor::Black, Scale);
				P.TextFx(Lines[i], Cx + Split, Y - 4.0f * (1.0f - Ease), Px, WithAlpha(C.Red, GhostAlpha), EShiftAlign::Center, EShiftTrailerFont::Black, 90, 0.0f, FLinearColor::Black, Scale);
				P.TextFx(Lines[i], Cx, Y, Px, WithAlpha(FLinearColor::White, Alpha), EShiftAlign::Center, EShiftTrailerFont::Black, 90, bOnCard ? 0.0f : (Tx.bBox ? 3.0f : 5.0f), WithAlpha(Ink, (Tx.bBox ? 0.6f : 0.9f) * Alpha), Scale);
			}
		}
		else
		{
			// caption: caps in the lower left above the bottom bar, in a dark box with a teal bar, sliding in on the beat
			const float In = FMath::Clamp(Age / 0.18f, 0.0f, 1.0f);
			const float Ease = 1.0f - FMath::Pow(1.0f - In, 3.0f);
			const float Out = FMath::Clamp((Age - (Life - 0.22f)) / 0.22f, 0.0f, 1.0f);
			const float Alpha = Ease * (1.0f - Out);
			if (Alpha <= 0.001f)
			{
				continue;
			}
			const float Px = 44.0f;
			const float LineH = Px * 1.12f;
			const float Slide = -36.0f * (1.0f - Ease);
			const float Bottom = 650.0f;
			const float Top = Bottom - LineH * static_cast<float>(Lines.Num());
			float Widest = 1.0f;
			for (const FString& Line : Lines)
			{
				Widest = FMath::Max(Widest, P.MeasureFx(Line, Px, EShiftTrailerFont::BoldCondensed, 70).X);
			}
			P.Rect(50.0f + Slide, Top - 16.0f, 36.0f + Widest + 28.0f, LineH * static_cast<float>(Lines.Num()) + 32.0f, WithAlpha(Ink, 0.88f * Alpha), 14.0f);
			P.Rect(64.0f + Slide, Top + 4.0f, 6.0f, LineH * static_cast<float>(Lines.Num()) - 8.0f, WithAlpha(C.Teal, Alpha), 3.0f);
			for (int32 i = 0; i < Lines.Num(); ++i)
			{
				const float Y = Top + LineH * static_cast<float>(i);
				P.TextFx(Lines[i], 86.0f + Slide + 2.0f, Y + 3.0f, Px, WithAlpha(FLinearColor::Black, 0.45f * Alpha), EShiftAlign::Left, EShiftTrailerFont::BoldCondensed, 70, 0.0f, FLinearColor::Black);
				P.TextFx(Lines[i], 86.0f + Slide, Y, Px, WithAlpha(FLinearColor::White, Alpha), EShiftAlign::Left, EShiftTrailerFont::BoldCondensed, 70, 2.0f, WithAlpha(Ink, 0.7f * Alpha));
			}
		}
	}

	if (T.White > 0.001f)
	{
		P.Rect(X0, Y0, W, H, WithAlpha(FLinearColor::White, T.White));
	}
}
#endif

void SShiftHud::PaintCursor(FShiftPainter& P, const FPal& C) const
{
#if !UE_BUILD_SHIPPING
	if (Model.Trailer.bActive && Model.Trailer.bHideCursor)
	{
		return;   // a card covers the screen
	}
	const bool bTrailerRun = Model.Trailer.bActive;
#else
	const bool bTrailerRun = false;
#endif
	// Click rings first, so the pointer is above them.
	for (const FRipple& R : Ripples)
	{
		const float T = (AnimTime - R.Start) / RippleSeconds;
		if (T < 0.0f || T >= 1.0f)
		{
			continue;
		}
		const float Ease = 1.0f - (1.0f - T) * (1.0f - T);
		const FLinearColor Tint = R.Kind == 1 ? C.Violet : (R.Kind == 2 ? C.Amber : C.Teal);
		P.Disc(R.Pos.X, R.Pos.Y, 4.0f + 8.0f * Ease, WithAlpha(Tint, 0.35f * (1.0f - T)));
		P.Arc(R.Pos.X, R.Pos.Y, 5.0f + 30.0f * Ease, 0.0f, 1.0f, WithAlpha(Tint, 0.9f * (1.0f - T)), 1.0f + 2.5f * (1.0f - T));
	}

	const bool bPseudo = Model.bPseudoCursor;
	if (!bPseudo && (bTrailerRun || Model.bSystemCursor || !bMouseInside))   // the scripted trailer run never draws the real mouse
	{
		return;
	}
	const FVector2f Pos = bPseudo ? Model.PseudoCursor : MouseDesign;

	EShiftCursor Shape = Model.Tooltip.bVisible ? Model.Tooltip.Cursor : EShiftCursor::Arrow;
	const FFrame& Centre = Frames[static_cast<int32>(EShiftDock::C)];
	if (const FHit* Hit = FindHit(Centre.Origin + Pos * Centre.Scale))
	{
		if (Hit->Id != EShiftUi::SettingsClose && Hit->Id != EShiftUi::SettingsPanel)
		{
			Shape = EShiftCursor::Button;
		}
	}

	const bool bPressed = AnimTime < PressUntil;
	float S = Shape == EShiftCursor::Button ? 1.12f : 1.0f;
	if (bPressed)
	{
		S *= 0.86f;
	}

	FLinearColor Fill = FLinearColor::White;
	switch (Shape)
	{
	case EShiftCursor::Button: Fill = C.Teal; break;
	case EShiftCursor::Sock: Fill = C.Violet; break;
	case EShiftCursor::Blocked: Fill = Hex(0xB9B7C9); break;
	default: break;
	}
	const FLinearColor Edge = Hex(0x1B1930);

	static const FVector2f Shape0[] = {
		FVector2f(0.0f, 0.0f), FVector2f(0.0f, 17.0f), FVector2f(4.3f, 13.1f), FVector2f(7.2f, 19.4f),
		FVector2f(9.9f, 18.2f), FVector2f(7.0f, 12.1f), FVector2f(12.4f, 12.1f) };
	TArray<FVector2f> Pointer;
	TArray<FVector2f> Shadow;
	for (const FVector2f& Pt : Shape0)
	{
		Pointer.Add(Pos + Pt * S);
		Shadow.Add(Pos + Pt * S + FVector2f(1.2f, 1.8f));
	}
	P.Polygon(Shadow, Hex(0x000000, 0.22f), 3.6f);
	P.FillPolygon(Pointer, Fill);
	P.Polygon(Pointer, Edge, 1.6f);

	// The badge: a small disc at the lower right of the pointer that says what a click would do here.
	const FVector2f B = Pos + FVector2f(17.0f, 19.0f) * S;
	const float R = 7.0f * S;
	switch (Shape)
	{
	case EShiftCursor::Plug:
		P.Disc(B.X, B.Y, R + 1.2f, Edge);
		P.Disc(B.X, B.Y, R, C.Teal);
		P.Arc(B.X, B.Y, 3.6f * S, 0.0f, 1.0f, FLinearColor::White, 1.5f);
		P.Disc(B.X, B.Y, 1.3f * S, FLinearColor::White);
		break;
	case EShiftCursor::Reinforce:
		P.Disc(B.X, B.Y, R + 1.2f, Edge);
		P.Disc(B.X, B.Y, R, C.Teal);
		P.Line(B + FVector2f(-3.6f, 0.0f) * S, B + FVector2f(3.6f, 0.0f) * S, FLinearColor::White, 2.0f);
		P.Line(B + FVector2f(0.0f, -3.6f) * S, B + FVector2f(0.0f, 3.6f) * S, FLinearColor::White, 2.0f);
		break;
	case EShiftCursor::Sock:
		P.Disc(B.X, B.Y, R + 1.2f, Edge);
		P.Disc(B.X, B.Y, R, Hex(0x2A2550));
		P.Rect(B.X - 4.2f * S, B.Y - 1.8f * S, 8.4f * S, 3.6f * S, C.Violet, 1.8f * S);
		P.Disc(B.X - 1.8f * S, B.Y, 0.9f * S, FLinearColor::White);
		P.Disc(B.X + 1.8f * S, B.Y, 0.9f * S, FLinearColor::White);
		break;
	case EShiftCursor::Blocked:
		P.Disc(B.X, B.Y, R + 1.2f, Edge);
		P.Disc(B.X, B.Y, R, FLinearColor::White);
		P.Arc(B.X, B.Y, 4.6f * S, 0.0f, 1.0f, C.Red, 2.0f);
		P.Line(B + FVector2f(-3.3f, 3.3f) * S, B + FVector2f(3.3f, -3.3f) * S, C.Red, 2.0f);
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------

int32 SShiftHud::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
	const float Scale = FMath::Max(0.01f, FMath::Min(Size.X / DesignW, Size.Y / DesignH));
	WindowSize = Size;
	// The docks: the design rect is placed once for each of the nine anchors of the window (corners, edge middles, centre), so that the design point at the anchor lands on the same
	// point of the window. A part painted in a dock keeps its distance from that edge at any window shape; the centre dock is the classic centred 16:9 rect (the board's frame).
	for (int32 i = 0; i < 9; ++i)
	{
		const FVector2f Anchor(0.5f * static_cast<float>(i % 3), 0.5f * static_cast<float>(i / 3));
		Frames[i].Origin = Anchor * Size - Anchor * FVector2f(DesignW, DesignH) * Scale;
		Frames[i].Scale = Scale;
	}
	const FVector2f Origin = Frames[static_cast<int32>(EShiftDock::C)].Origin;
	FShiftPainter::MinPx = Model.bLargeText ? 14.0f : 0.0f;
	FShiftPainter P(OutDrawElements, AllottedGeometry, LayerId, Origin, Scale);

	// the trailer's zoom-in: the HUD is scaled about the focus point exactly as the board camera is (the trailer's own overlay is not)
	FVector2f ZoomedOrigin = Origin;
	float ZoomedScale = Scale;
#if !UE_BUILD_SHIPPING
	if (Model.Trailer.bActive && Model.Trailer.Zoom > 1.0001f)
	{
		const FVector2f FocusLocal = Origin + Model.Trailer.ZoomFocus * Scale;
		for (FFrame& Frame : Frames)
		{
			Frame.Origin = FocusLocal + (Frame.Origin - FocusLocal) * Model.Trailer.Zoom;
			Frame.Scale = Scale * Model.Trailer.Zoom;
		}
		ZoomedOrigin = Frames[static_cast<int32>(EShiftDock::C)].Origin;
		ZoomedScale = Scale * Model.Trailer.Zoom;
	}
#endif
	Dock(P, EShiftDock::C);

	FPal C;
	C.N = Model.Night;
	C.Ink = Mix(Hex(0x1B1930), Hex(0xEDEDFF), C.N);
	C.InkDim = WithAlpha(C.Ink, 0.62f);
	C.Panel = Mix(Hex(0xFFFFFF, 0.84f), Hex(0x0A0D1C, 0.86f), C.N);
	C.Panel.A = Solid(C.Panel.A);
	C.PanelSolid = Mix(Hex(0xFFFFFF, 0.97f), Hex(0x0E1226, 0.97f), C.N);
	C.Stroke = Mix(Hex(0x1B1930, 0.16f), Hex(0xFFFFFF, 0.16f), C.N);
	C.Track = Mix(Hex(0x1B1930, 0.10f), Hex(0xFFFFFF, 0.12f), C.N);
	C.Teal = Hex(0x1FB6A6);
	C.Red = Hex(0xFF4D5A);
	C.Amber = Hex(0xF2B134);
	C.Violet = Hex(0x8B6CFF);

	// the scripted cursor of the trailer is the mouse: it hovers the buttons and cards it rests on (the real mouse never reaches a window that sits behind the others)
	if (Model.bPseudoCursor)
	{
		MouseLocal = Frames[static_cast<int32>(EShiftDock::C)].Origin + Model.PseudoCursor * Frames[static_cast<int32>(EShiftDock::C)].Scale;
	}

	Hits.Reset();
	bModal = false;

	switch (Model.Screen)
	{
	case EShiftScreen::Title:
		bModal = true;
		PaintTitle(P, C);
		break;
	case EShiftScreen::JamSelect:
		bModal = true;
		PaintJamSelect(P, C);
		break;
	case EShiftScreen::Build:
		bModal = true;
		PaintBuild(P, C);
		break;
	case EShiftScreen::Play:
	default:
		for (const FShiftHudModel::FMarker& M : Model.Markers)
		{
			DrawMarker(P, M.Owner, M.X, M.Y - 15.0f, 4.5f);
		}
		PaintPlay(P, C);
		// the vote counts that rise from the voters just won or lost: they rise and fade, or only fade with Reduce motion
		for (const FShiftHudFloat& F : Model.Floats)
		{
			const float T = FMath::Clamp(F.Age / FMath::Max(0.1f, F.Life), 0.0f, 1.0f);
			const float Rise = Model.bReduceMotion ? 0.0f : 40.0f * (1.0f - (1.0f - T) * (1.0f - T));
			const float Alpha = T < 0.12f ? T / 0.12f : 1.0f - (T - 0.12f) / 0.88f;
			const float TextSize = F.bBig ? 22.0f : 16.0f;
			P.Text(F.Text, F.X + 1.0f, F.Y - 26.0f - Rise + 1.5f, TextSize, Hex(0x000000, 0.45f * Alpha), EShiftAlign::Center, true);
			P.Text(F.Text, F.X, F.Y - 26.0f - Rise, TextSize, WithAlpha(F.Color, Alpha), EShiftAlign::Center, true);
		}
		if (Model.bOver)
		{
			bModal = true;
			PaintResults(P, C);
		}
		else if (Model.bPaused)
		{
			bModal = true;
			PaintPause(P, C);
		}
		else if (Model.Lesson.bActive)
		{
			PaintLesson(P, C);
		}
		break;
	}
	if (Model.bHowTo)
	{
		PaintHowTo(P, C);
	}
	else if (Model.ExtrasPage != 0)
	{
		PaintExtras(P, C);
	}
	else if (Model.bSettingsOpen)
	{
		PaintSettings(P, C);
	}
	// the wash between two screens: the new screen comes up under paper that thins out; it covers everything but the cursor
	if (Model.TransitionCover > 0.002f)
	{
		Dock(P, EShiftDock::C);
		FillWindow(P, WithAlpha(Mix(Hex(0xF6F1E6), Hex(0x04060F), C.N), Model.TransitionCover * Model.BackgroundOpacity));
	}
#if !UE_BUILD_SHIPPING
	if (Model.Trailer.bActive)
	{
		P.Origin = Origin;
		P.Scale = Scale;
		PaintTrailer(P, C);   // above the whole HUD, below the scripted cursor; never zoomed
	}
#endif
	P.Origin = ZoomedOrigin;
	P.Scale = ZoomedScale;
	Dock(P, EShiftDock::C);
	PaintCursor(P, C);

	return P.Layer + 1;
}
