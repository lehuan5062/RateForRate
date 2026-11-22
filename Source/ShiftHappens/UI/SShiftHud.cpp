// Shift Happens - Game Off 2026

#include "UI/SShiftHud.h"

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

	TSet<FKey> Pressed;
	if (Kind != 2)
	{
		Pressed.Add(EKeys::LeftMouseButton);
	}
	const FPointerEvent Event(0, Absolute, Absolute, Pressed, EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
	bQaInjecting = true;
	if (Kind == 0)
	{
		OnMouseMove(Geo, Event);
	}
	else if (Kind == 1)
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

bool SShiftHud::AddHit(float X, float Y, float W, float H, int32 Id, bool bSlider, float SliderX0, float SliderX1) const
{
	FHit Hit;
	Hit.X = X;
	Hit.Y = Y;
	Hit.W = W;
	Hit.H = H;
	Hit.Id = Id;
	Hit.bSlider = bSlider;
	Hit.SliderX0 = SliderX0;
	Hit.SliderX1 = SliderX1;
	Hits.Add(Hit);
	return Hit.Contains(MouseDesign);
}

const SShiftHud::FHit* SShiftHud::FindHit(const FVector2f& Design) const
{
	for (int32 i = Hits.Num() - 1; i >= 0; --i)
	{
		if (Hits[i].Contains(Design))
		{
			return &Hits[i];
		}
	}
	return nullptr;
}

float SShiftHud::SliderValue(const FHit& Hit, float DesignX)
{
	return FMath::Clamp((DesignX - Hit.SliderX0) / FMath::Max(1.0f, Hit.SliderX1 - Hit.SliderX0), 0.0f, 1.0f);
}

FReply SShiftHud::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FKey Button = MouseEvent.GetEffectingButton();
	if (Button != EKeys::LeftMouseButton && Button != EKeys::RightMouseButton)
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2f Design = LocalToDesign(MyGeometry, FVector2f(Local));
	MouseDesign = Design;
	if (!bQaInjecting)
	{
		bMouseInside = true;
		if (!Model.bSystemCursor)
		{
			PushRipple(Design, Button == EKeys::RightMouseButton ? 2 : ((Model.bNightActive || MouseEvent.IsShiftDown()) ? 1 : 0));
		}
	}

	FReply Reply = FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	if (Button == EKeys::LeftMouseButton)
	{
		if (const FHit* Hit = FindHit(Design))
		{
			if (Hit->bSlider)
			{
				DragId = Hit->Id;
				DragHit = *Hit;
				OnUi.ExecuteIfBound(Hit->Id, SliderValue(*Hit, Design.X));
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
	bMouseInside = bMouseInside || !bQaInjecting;
	if (DragId >= 0)
	{
		OnUi.ExecuteIfBound(DragHit.Id, SliderValue(DragHit, MouseDesign.X));
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
}

FCursorReply SShiftHud::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (!Model.bSystemCursor)
	{
		// The game draws its own cursor (PaintCursor); the operating system's arrow is hidden over the window. The query also tells us the mouse is here, even before it moves.
		const FVector2D LocalPos = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition());
		MouseDesign = LocalToDesign(MyGeometry, FVector2f(LocalPos));
		bMouseInside = true;
		return FCursorReply::Cursor(EMouseCursor::None);
	}
	const FVector2D Local = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition());
	const FVector2f Design = LocalToDesign(MyGeometry, FVector2f(Local));
	if (const FHit* Hit = FindHit(Design))
	{
		if (Hit->Id != EShiftUi::TitleStart && Hit->Id != EShiftUi::SettingsClose && Hit->Id != EShiftUi::SettingsPanel)
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
			Anim.bInit = true;
		}
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
	if (Model.bMuted)
	{
		P.Disc(Cx + 11.0f, Cy - 11.0f, 4.6f, C.Red);   // sound is off: a red dot on the gear
	}
}

void SShiftHud::PaintSettings(FShiftPainter& P, const FPal& C) const
{
	// The menu dropped from the gear button. Everything behind it stops listening: a click outside it closes it.
	Hits.Reset();
	bModal = true;
	AddHit(0.0f, 0.0f, DesignW, DesignH, EShiftUi::SettingsClose);

	const float W = 340.0f;
	const float H = 176.0f;
	const float Pad = 18.0f;
	const float X = DesignW - W - 14.0f;
	const float Y = 48.0f;
	P.Rect(X + 2.0f, Y + 8.0f, W, H, Hex(0x000000, 0.22f), 18.0f);
	P.Rect(X - 1.0f, Y - 1.0f, W + 2.0f, H + 2.0f, C.Stroke, 19.0f);
	P.Rect(X, Y, W, H, C.PanelSolid, 18.0f);
	AddHit(X, Y, W, H, EShiftUi::SettingsPanel);   // the padding swallows clicks, so only a click outside closes it

	P.Text(TEXT("SETTINGS"), X + Pad, Y + 14.0f, 13.0f, C.Ink, EShiftAlign::Left, true);
	P.Text(TEXT("Esc closes"), X + W - Pad, Y + 16.0f, 11.0f, C.InkDim, EShiftAlign::Right);

	P.Text(TEXT("SOUND  (M)"), X + Pad, Y + 46.0f, 11.0f, C.InkDim, EShiftAlign::Left, true);
	const float Half = (W - 2.0f * Pad - 8.0f) * 0.5f;
	Button(P, C, EShiftUi::SoundOn, X + Pad, Y + 64.0f, Half, 30.0f, TEXT("On"), !Model.bMuted, 13.0f);
	Button(P, C, EShiftUi::SoundOff, X + Pad + Half + 8.0f, Y + 64.0f, Half, 30.0f, TEXT("Off"), Model.bMuted, 13.0f);

	P.Text(TEXT("WINDOW"), X + Pad, Y + 110.0f, 11.0f, C.InkDim, EShiftAlign::Left, true);
	static const TCHAR* const Labels[3] = { TEXT("Fullscreen"), TEXT("Borderless"), TEXT("Windowed") };
	const float Third = (W - 2.0f * Pad - 16.0f) / 3.0f;
	for (int32 i = 0; i < 3; ++i)
	{
		Button(P, C, EShiftUi::WindowFullscreen + i, X + Pad + static_cast<float>(i) * (Third + 8.0f), Y + 128.0f, Third, 30.0f, Labels[i], Model.WindowMode == i, 13.0f);
	}
}

void SShiftHud::PaintKeycap(FShiftPainter& P, const FPal& C, float Cx, float Cy, bool bPressed) const
{
	const float W = 190.0f;
	const float H = 112.0f;
	const float X = Cx - W * 0.5f;
	const float Y = Cy - H * 0.5f;
	const float Off = bPressed ? 9.0f : 0.0f;
	const float Pulse = 0.5f + 0.5f * FMath::Sin(AnimTime * 2.4f);

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
	P.Polygon(Arrow, Glyph, 4.0f);
	P.Text(TEXT("shift"), X + 92.0f, Ay - 20.0f, 36.0f, Glyph, EShiftAlign::Left, true);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// title

void SShiftHud::PaintTitle(FShiftPainter& P, const FPal& C) const
{
	AddHit(0, 0, DesignW, DesignH, EShiftUi::TitleStart);   // the whole screen is the start button; later regions sit on top

	P.Rect(0, 0, DesignW, DesignH, Mix(Hex(0xF6F1E6, 0.58f), Hex(0x04060F, 0.66f), C.N));

	const float Bob = FMath::Sin(AnimTime * 1.1f) * 3.0f;
	P.Text(TEXT("SHIFT HAPPENS"), 643.0f, 112.0f + Bob + 4.0f, 92.0f, Hex(0x000000, 0.18f), EShiftAlign::Center, true);
	P.Text(TEXT("SHIFT HAPPENS"), 640.0f, 108.0f + Bob, 92.0f, C.Ink, EShiftAlign::Center, true);
	P.Text(TEXT("A game about winning a game jam by shifting the votes."), 640.0f, 224.0f, 21.0f, C.InkDim, EShiftAlign::Center);

	PaintKeycap(P, C, 640.0f, 384.0f, Model.bShiftDown);

	const FString Hint = Model.bShiftDown ? FString(TEXT("NIGHT SHIFT: sockpuppets, bots, bribes. Powerful, fragile, risky."))
		: FString(TEXT("Try it: hold SHIFT to work the night shift."));
	P.Text(Hint, 640.0f, 484.0f, 17.0f, Model.bShiftDown ? Hex(0xB79BFF) : C.InkDim, EShiftAlign::Center, Model.bShiftDown);

	const float Blink = 0.55f + 0.45f * FMath::Sin(AnimTime * 3.0f);
	P.Text(TEXT("Click anywhere to start"), 640.0f, 560.0f, 26.0f, WithAlpha(C.Teal, Blink), EShiftAlign::Center, true);
	P.Text(TEXT("or press ENTER      ESC quits"), 640.0f, 596.0f, 13.0f, C.InkDim, EShiftAlign::Center);

	// New here? The guided lessons teach the game hands-on; the reference page is one screen. The lessons button glows until the basics have been finished once.
	const float LessonsPulse = 0.5f + 0.5f * FMath::Sin(AnimTime * 4.0f);
	if (Model.bLessonNudge)
	{
		P.Rect(380.0f, 612.0f, 270.0f, 56.0f, WithAlpha(C.Teal, 0.20f + 0.25f * LessonsPulse), 28.0f);
	}
	Button(P, C, EShiftUi::LearnToPlay, 386.0f, 618.0f, 258.0f, 44.0f, TEXT("LEARN TO PLAY"), Model.bLessonNudge, 17.0f);
	Button(P, C, EShiftUi::HowTo, 664.0f, 618.0f, 230.0f, 44.0f, TEXT("How to play  (H)"), false, 16.0f);

	SettingsButton(P, C, 1246.0f, 24.0f);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// jam select

void SShiftHud::PaintJamSelect(FShiftPainter& P, const FPal& C) const
{
	P.Rect(0, 0, DesignW, DesignH, Mix(Hex(0xF6F1E6, 0.80f), Hex(0x04060F, 0.80f), C.N));
	P.Text(TEXT("Pick a jam"), 640.0f, 34.0f, 36.0f, C.Ink, EShiftAlign::Center, true);
	P.Text(TEXT("Each jam has its own rival, its own network and its own mood."), 640.0f, 84.0f, 15.0f, C.InkDim, EShiftAlign::Center);

	const float CardW = 360.0f;
	const float CardH = 480.0f;
	for (int32 i = 0; i < Model.JamCards.Num(); ++i)
	{
		const FShiftHudJamCard& Card = Model.JamCards[i];
		const float X = 60.0f + 400.0f * static_cast<float>(i);
		const float Y0 = 128.0f;
		const bool bHover = AddHit(X, Y0 - 6.0f, CardW, CardH + 6.0f, EShiftUi::JamCard0 + i);
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
		P.Text(FString::Printf(TEXT("by @%s"), *Card.RivalAuthor), X + 24.0f, Y + 212.0f, 13.0f, C.InkDim);
		P.TextBlock(Card.RivalBlurb, X + 24.0f, Y + 240.0f, CardW - 48.0f, 13.0f, 18.0f, C.Ink);

		float Fy = Y + 292.0f;
		for (const FString& Fact : Card.Facts)
		{
			P.Disc(X + 30.0f, Fy + 8.0f, 2.5f, C.Teal);
			P.Text(Fact, X + 42.0f, Fy, 12.0f, C.InkDim);
			Fy += 19.0f;
		}

		P.Text(TEXT("DIFFICULTY"), X + 24.0f, Y + 356.0f, 11.0f, C.InkDim, EShiftAlign::Left, true);
		for (int32 s = 0; s < 3; ++s)
		{
			P.Disc(X + 28.0f + 24.0f * static_cast<float>(s), Y + 392.0f, 8.0f, s < Card.Difficulty ? C.Amber : C.Track);
		}
		const FString Best = Card.BestRank > 0 ? FString::Printf(TEXT("Best: %s place"), *HudOrdinal(Card.BestRank)) : FString(TEXT("Not played yet"));
		P.Text(Best, X + CardW - 24.0f, Y + 383.0f, 15.0f, Card.BestRank == 1 ? C.Teal : C.Ink, EShiftAlign::Right, Card.BestRank == 1);

		P.Rect(X + 24.0f, Y + CardH - 62.0f, CardW - 48.0f, 40.0f, bHover ? C.Teal : WithAlpha(C.Teal, 0.16f), 20.0f);
		P.Text(TEXT("Build your entry"), X + CardW * 0.5f, Y + CardH - 52.0f, 16.0f, bHover ? FLinearColor::White : C.Teal, EShiftAlign::Center, true);
	}

	Button(P, C, EShiftUi::Back, 24.0f, 22.0f, 104.0f, 36.0f, TEXT("< Back"), false, 14.0f);
	SettingsButton(P, C, 1246.0f, 24.0f);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// build

void SShiftHud::PaintBuild(FShiftPainter& P, const FPal& C) const
{
	P.Rect(0, 0, DesignW, DesignH, Mix(Hex(0xF6F1E6, 0.84f), Hex(0x04060F, 0.84f), C.N));
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

	Button(P, C, EShiftUi::Back, 24.0f, 22.0f, 104.0f, 36.0f, TEXT("< Back"), false, 14.0f);
	SettingsButton(P, C, 1246.0f, 24.0f);
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
	// ------------------------------------------------------------------ top bar
	P.Rect(0, 0, DesignW, 40, C.Panel);
	P.Rect(0, 40, DesignW, 1, C.Stroke);
	P.Text(TEXT("SHIFT HAPPENS"), 18, 10, 15, C.Ink, EShiftAlign::Left, true);
	P.Text(P.Fit(Model.JamName, 230.0f, 13), 160, 12, 13, C.InkDim);
	if (Model.bReplay)
	{
		P.Text(TEXT("REPLAY"), 400, 11, 14, C.Amber, EShiftAlign::Left, true);   // a recording is playing: clicks and keys do nothing
	}
	P.Text(FString::Printf(TEXT("%s  -  Day %d / %d"), *Model.DateLabel, Model.Day, Model.Days), 640, 9, 16, C.Ink, EShiftAlign::Center, true);
	P.Rect(510, 33, 260, 3, C.Track, 1.5f);
	P.Rect(510, 33, 260.0f * FMath::Clamp(Model.RunProgress, 0.0f, 1.0f), 3, C.Teal, 1.5f);
	P.Text(Model.DeadlineLabel, 1224, 12, 13, C.InkDim, EShiftAlign::Right);
	SettingsButton(P, C, 1250.0f, 20.0f);
	if (Model.bModsEnabled && Model.NextSweepIn >= 0.0f)
	{
		const bool bAway = Model.EyeAwayLeft > 0.0f;
		const bool bSoon = !bAway && Model.NextSweepIn < 3.0f;
		const FLinearColor EyeColor = bAway ? Hex(0x7B5CF0) : (bSoon ? C.Red : C.InkDim);
		const float Pulse = bSoon ? 0.5f + 0.5f * FMath::Sin(AnimTime * 14.0f) : 0.0f;
		P.Disc(900, 20, 9.5f, EyeColor);
		P.Disc(900, 20, 7.0f, C.Panel);
		if (!bAway)
		{
			P.Disc(900, 20, 3.0f + 1.5f * Pulse, EyeColor);   // the Eye looks away: no pupil
		}
		P.Text(bAway ? FString::Printf(TEXT("MODS AWAY %.0fs"), Model.EyeAwayLeft) : FString::Printf(TEXT("MODS %.0fs"), Model.NextSweepIn), 916, 13, 12, EyeColor, EShiftAlign::Left, bSoon || bAway);
	}

	// ------------------------------------------------------------------ leaderboard
	const int32 LeaderRows = FMath::Max(1, Model.Rows.Num());
	const float LeaderBottom = Model.LeaderboardMode == 1 ? 52.0f + 38.0f + RowHeight * static_cast<float>(LeaderRows)
		: (Model.LeaderboardMode == 0 ? 48.0f + 20.0f + CompactRowH * static_cast<float>(LeaderRows) + 4.0f : 48.0f + 22.0f);
	// Compact by default (the top three and the player, on a see-through panel) so the right side of the board stays in view; L cycles compact / full / hidden.
	if (Model.LeaderboardMode == 1)
	{
		const float PanelX = 996.0f;
		const float PanelY = 52.0f;
		P.Rect(PanelX, PanelY, 274, 38.0f + RowHeight * static_cast<float>(FMath::Max(1, Model.Rows.Num())), WithAlpha(C.Panel, 0.86f), 10);
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
			if (Row.bPlayer)
			{
				P.Rect(PanelX + 6, Y - 2, 262, RowHeight - 1, WithAlpha(C.Teal, 0.18f), 8);
			}
			P.Text(FString::FromInt(Row.Rank + 1), PanelX + 26, Y + 4, 14, Row.bPlayer ? C.Teal : C.Ink, EShiftAlign::Center, true);
			P.Disc(PanelX + 46, Y + 13, 6.0f, Row.Color);
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
		P.Rect(PanelX, PanelY, PanelW, 20.0f + CompactRowH * static_cast<float>(RowCount) + 4.0f, WithAlpha(C.Panel, 0.58f), 8);
		P.Text(TEXT("LEADERBOARD  (L)"), PanelX + 10, PanelY + 4, 10, C.InkDim, EShiftAlign::Left, true);
		for (const FShiftHudRow& Row : Model.Rows)
		{
			const float Y = PanelY + 18.0f + CompactRowH * static_cast<float>(Row.Rank);
			if (Row.bPlayer)
			{
				P.Rect(PanelX + 4, Y - 1, PanelW - 8, CompactRowH, WithAlpha(C.Teal, 0.24f), 5);
			}
			P.Text(FString::FromInt(Row.Rank + 1), PanelX + 14, Y + 1, 11, Row.bPlayer ? C.Teal : C.Ink, EShiftAlign::Center, true);
			P.Disc(PanelX + 28, Y + 7, 4.0f, Row.Color);
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
				P.Rect(DesignW - 124.0f, 48.0f, 114.0f, 22.0f, WithAlpha(C.Panel, 0.62f), 11);
				P.Text(FString::Printf(TEXT("YOU #%d  %.0f%%  (L)"), Row.Rank + 1, Row.Share * 100.0f), DesignW - 67.0f, 53.0f, 11, C.Teal, EShiftAlign::Center, true);
			}
		}
	}

	// ------------------------------------------------------------------ bottom bar
	P.Rect(0, 676, DesignW, 44, C.Panel);
	P.Rect(0, 676, DesignW, 1, C.Stroke);
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
		const float Pulse = A.bClickable ? 0.5f + 0.5f * FMath::Sin(AnimTime * 9.0f) : 0.0f;
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

	// ------------------------------------------------------------------ the right gutter
	// Everything that is not the board lives in the strip right of the voters (the layout keeps them left of x = 1084): under the leaderboard the states you are in, then the
	// notification feed; at the bottom the coach card. Nothing here floats over the board.
	{
		const float GX = DesignW - 10.0f - CompactLeaderboardW;
		const float GW = CompactLeaderboardW;
		float BottomY = 668.0f;

		// the bottom stack: the coach card (the Blame prompt is in the bottom bar, where the eyes already are)
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
			Pin(FString::Printf(TEXT("MOD EYE LOOKS AWAY  %.0fs"), Model.EyeAwayLeft), TEXT("night shift is free: hold SHIFT"), WithAlpha(Purple, 0.92f));
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
			const float Pulse = 0.5f + 0.5f * FMath::Sin(AnimTime * 10.0f);
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
			const TArray<FString> Lines = P.Wrap(Item.Text, GW - 24.0f, 11.0f, !Item.bNews);
			const int32 LineCount = FMath::Min(Lines.Num(), 5);
			const float H = 12.0f + 14.0f * static_cast<float>(LineCount) + (Item.bNews ? 12.0f : 0.0f);
			if (Y + H > BottomY)
			{
				break;
			}
			P.Rect(GX, Y, GW, H, WithAlpha(C.PanelSolid, 0.94f * Item.Alpha), 9.0f);
			P.Rect(GX, Y, 4.0f, H, WithAlpha(Item.bNews ? C.Amber : Item.Color, Item.Alpha), 2.0f);
			float Ty = Y + 6.0f;
			if (Item.bNews)
			{
				P.Text(TEXT("NEWS"), GX + 12.0f, Ty, 9.0f, WithAlpha(C.Amber, Item.Alpha), EShiftAlign::Left, true);
				Ty += 12.0f;
			}
			for (int32 L = 0; L < LineCount; ++L)
			{
				P.Text(Lines[L], GX + 12.0f, Ty, 11.0f, WithAlpha(Item.bNews ? C.Ink : Item.Color, Item.Alpha), EShiftAlign::Left, !Item.bNews);
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

	// ------------------------------------------------------------------ tooltip
	if (Model.Tooltip.bVisible && !Model.bOver && !Model.bPaused)
	{
		PaintTooltip(P, C);
	}

	// ------------------------------------------------------------------ the edge frame: a thin line along the left, right and bottom edges for a strike, the Blame offer and a disqualification
	// (the voters stay inside x 56..1080, so it never covers one)
	if (Model.EdgeFlash > 0.01f)
	{
		const FLinearColor Edge = WithAlpha(Model.EdgeColor, 0.80f * FMath::Clamp(Model.EdgeFlash, 0.0f, 1.0f));
		P.Rect(0.0f, 41.0f, 14.0f, DesignH - 41.0f, Edge);
		P.Rect(DesignW - 9.0f, 41.0f, 9.0f, DesignH - 41.0f, Edge);
		P.Rect(14.0f, DesignH - 8.0f, DesignW - 23.0f, 8.0f, Edge);
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

void SShiftHud::PaintResults(FShiftPainter& P, const FPal& C) const
{
	Hits.Reset();
	P.Rect(0, 0, DesignW, DesignH, Hex(0x000000, 0.58f));
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
		P.Text(P.Fit(Row.Name, 250.0f, 13, Row.bPlayer), 178, Ry, 13, C.Ink, EShiftAlign::Left, Row.bPlayer);
		P.Text(FString::Printf(TEXT("%s   %.1f%%"), *Compact(Row.Votes), Row.Share * 100.0f), 574, Ry + 1, 12, C.InkDim, EShiftAlign::Right);
		Ry += 27.0f;
	}
	if (!Model.PlaysLine.IsEmpty())
	{
		P.TextBlock(Model.PlaysLine, 116.0f, Ry + 6.0f, 470.0f, 12.0f, 16.0f, C.InkDim);
	}

	PaintScatter(P, C, 610.0f, ListY - 6.0f, 280.0f, 250.0f, TEXT("Quality vs votes"), Model.QualityDots, Model.QualityFit, TEXT("better game  ->"));
	PaintScatter(P, C, 902.0f, ListY - 6.0f, 280.0f, 250.0f, TEXT("Starting fame vs votes"), Model.FameDots, Model.FameFit, TEXT("more followers at the start  ->"));

	P.Text(Model.OutcomeLine3, 640, 500, 13, C.InkDim, EShiftAlign::Center);

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
	const int32 Buttons = 3 + (Model.bCanNextJam ? 1 : 0) + (WFiles > 0.0f ? 1 : 0);
	float X = 640.0f - 0.5f * (170.0f + WNext + 150.0f + 140.0f + WFiles + Gap * static_cast<float>(Buttons - 1));
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
	if (WFiles > 0.0f)
	{
		Button(P, C, EShiftUi::Recordings, X, By, WFiles, 46.0f, TEXT("Files  (O)"), false, 15.0f);
	}
	SettingsButton(P, C, 1250.0f, 20.0f);
}

void SShiftHud::PaintPause(FShiftPainter& P, const FPal& C) const
{
	Hits.Reset();
	P.Rect(0, 41, DesignW, 635, Hex(0x000000, 0.45f));
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
		SettingsButton(P, C, 1250.0f, 20.0f);
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
	SettingsButton(P, C, 1250.0f, 20.0f);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// guided lessons and the reference page

void SShiftHud::PaintLesson(FShiftPainter& P, const FPal& C) const
{
	const FShiftLessonView& L = Model.Lesson;
	const float Pulse = 0.5f + 0.5f * FMath::Sin(AnimTime * 6.0f);
	const FLinearColor Glow = Hex(0xFFB020);

	// the part of the HUD the step points at (the rectangles are where PaintPlay draws them)
	float Ax = 0.0f, Ay = 0.0f, Aw = 0.0f, Ah = 0.0f;
	switch (L.Anchor)
	{
	case EShiftLessonAnchor::Leaderboard: Ax = 1080.0f; Ay = 44.0f; Aw = 194.0f; Ah = 110.0f; break;
	case EShiftLessonAnchor::Posts: Ax = 12.0f; Ay = 680.0f; Aw = 240.0f; Ah = 38.0f; break;
	case EShiftLessonAnchor::NightChip: Ax = 494.0f; Ay = 680.0f; Aw = 164.0f; Ah = 36.0f; break;
	case EShiftLessonAnchor::Heat: Ax = 794.0f; Ay = 680.0f; Aw = 270.0f; Ah = 38.0f; break;
	default: break;
	}
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

void SShiftHud::PaintHowTo(FShiftPainter& P, const FPal& C) const
{
	Hits.Reset();
	bModal = true;
	AddHit(0.0f, 0.0f, DesignW, DesignH, EShiftUi::HowTo);   // a click anywhere closes it; the button sits on top
	P.Rect(0.0f, 0.0f, DesignW, DesignH, Hex(0x000000, 0.62f));
	const float X = 130.0f;
	const float Y = 60.0f;
	const float W = 1020.0f;
	const float H = 600.0f;
	P.Rect(X, Y, W, H, C.PanelSolid, 18.0f);
	P.Text(TEXT("HOW TO PLAY"), X + W * 0.5f, Y + 20.0f, 34.0f, C.Ink, EShiftAlign::Center, true);
	const float ColW = (W - 108.0f) * 0.5f;
	for (int32 i = 0; i < Model.HowToText.Num(); ++i)
	{
		const float Bx = X + 36.0f + static_cast<float>(i % 2) * (ColW + 36.0f);
		const float By = Y + 86.0f + static_cast<float>(i / 2) * 150.0f;
		FString Head, Body;
		if (!Model.HowToText[i].Split(TEXT(":"), &Head, &Body))
		{
			Body = Model.HowToText[i];
		}
		Body.TrimStartInline();
		P.Text(Head, Bx, By, 16.0f, C.Teal, EShiftAlign::Left, true);
		P.TextBlock(Body, Bx, By + 26.0f, ColW, 14.0f, 19.0f, C.Ink);
	}
	Button(P, C, EShiftUi::LearnToPlay, X + W * 0.5f - 105.0f, Y + H - 64.0f, 210.0f, 42.0f, TEXT("Learn to play"), true, 16.0f);
	P.Text(TEXT("Click anywhere or press H to close"), X + W * 0.5f, Y + H - 18.0f, 11.0f, C.InkDim, EShiftAlign::Center);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the cursor set

void SShiftHud::PaintCursor(FShiftPainter& P, const FPal& C) const
{
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
	if (!bPseudo && (Model.bSystemCursor || !bMouseInside))
	{
		return;
	}
	const FVector2f Pos = bPseudo ? Model.PseudoCursor : MouseDesign;

	EShiftCursor Shape = Model.Tooltip.bVisible ? Model.Tooltip.Cursor : EShiftCursor::Arrow;
	if (const FHit* Hit = FindHit(Pos))
	{
		if (Hit->Id != EShiftUi::TitleStart && Hit->Id != EShiftUi::SettingsClose && Hit->Id != EShiftUi::SettingsPanel)
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
	const FVector2f Origin = (Size - FVector2f(DesignW, DesignH) * Scale) * 0.5f;
	FShiftPainter P(OutDrawElements, AllottedGeometry, LayerId, Origin, Scale);

	FPal C;
	C.N = Model.Night;
	C.Ink = Mix(Hex(0x1B1930), Hex(0xEDEDFF), C.N);
	C.InkDim = WithAlpha(C.Ink, 0.62f);
	C.Panel = Mix(Hex(0xFFFFFF, 0.84f), Hex(0x0A0D1C, 0.86f), C.N);
	C.PanelSolid = Mix(Hex(0xFFFFFF, 0.97f), Hex(0x0E1226, 0.97f), C.N);
	C.Stroke = Mix(Hex(0x1B1930, 0.16f), Hex(0xFFFFFF, 0.16f), C.N);
	C.Track = Mix(Hex(0x1B1930, 0.10f), Hex(0xFFFFFF, 0.12f), C.N);
	C.Teal = Hex(0x1FB6A6);
	C.Red = Hex(0xFF4D5A);
	C.Amber = Hex(0xF2B134);
	C.Violet = Hex(0x8B6CFF);

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
		PaintPlay(P, C);
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
	else if (Model.bSettingsOpen)
	{
		PaintSettings(P, C);
	}
	PaintCursor(P, C);

	return P.Layer + 1;
}
