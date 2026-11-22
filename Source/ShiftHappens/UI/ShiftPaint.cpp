// Shift Happens - Game Off 2026

#include "UI/ShiftPaint.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/Platform.h"
#include "Layout/PaintGeometry.h"
#include "Math/Color.h"
#include "Math/MathFwd.h"
#include "Math/UnrealMathUtility.h"
#include "Rendering/DrawElementTypes.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/SlateLayoutTransform.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

namespace
{
	const FSlateBrush* RectBrush()
	{
		static const FSlateColorBrush Brush(FLinearColor::White);
		return &Brush;
	}

	/** Rounded rectangle brushes by radius (in half design units); persistent so Slate can keep the pointers. */
	const FSlateBrush* RoundedBrush(float Radius)
	{
		static TMap<int32, TUniquePtr<FSlateRoundedBoxBrush>> Cache;
		const int32 Key = FMath::Max(1, FMath::RoundToInt(Radius * 2.0f));
		TUniquePtr<FSlateRoundedBoxBrush>& Slot = Cache.FindOrAdd(Key);
		if (!Slot.IsValid())
		{
			Slot = MakeUnique<FSlateRoundedBoxBrush>(FLinearColor::White, static_cast<float>(Key) * 0.5f);
		}
		return Slot.Get();
	}

	/** Full rounding: a circle for square elements, a pill for wide ones. */
	const FSlateBrush* PillBrush()
	{
		static TUniquePtr<FSlateRoundedBoxBrush> Brush;
		if (!Brush.IsValid())
		{
			Brush = MakeUnique<FSlateRoundedBoxBrush>(FLinearColor::White, 1.0f);
			Brush->OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
		}
		return Brush.Get();
	}

	FSlateFontInfo MakeFont(float Px, bool bBold)
	{
		// Slate font sizes are points: 1 pt = 4/3 px at DPI scale 1.
		return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), static_cast<float>(FMath::Max(6, FMath::RoundToInt(Px * 0.75f))));
	}
}

FPaintGeometry FShiftPainter::Geom(float X, float Y, float W, float H) const
{
	return Geo.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(Scale, Origin + FVector2f(X, Y) * Scale));
}

void FShiftPainter::Rect(float X, float Y, float W, float H, const FLinearColor& Color, float Radius)
{
	if (W <= 0.0f || H <= 0.0f || Color.A <= 0.001f)
	{
		return;
	}
	FSlateDrawElement::MakeBox(Out, ++Layer, Geom(X, Y, W, H), Radius > 0.5f ? RoundedBrush(Radius) : RectBrush(), ESlateDrawEffect::None, Color);
}

void FShiftPainter::Disc(float Cx, float Cy, float R, const FLinearColor& Color)
{
	if (R <= 0.0f || Color.A <= 0.001f)
	{
		return;
	}
	FSlateDrawElement::MakeBox(Out, ++Layer, Geom(Cx - R, Cy - R, 2.0f * R, 2.0f * R), PillBrush(), ESlateDrawEffect::None, Color);
}

void FShiftPainter::Line(const FVector2f& A, const FVector2f& B, const FLinearColor& Color, float Thickness)
{
	TArray<FVector2f> Points;
	Points.Add(Origin + A * Scale);
	Points.Add(Origin + B * Scale);
	FSlateDrawElement::MakeLines(Out, ++Layer, Geo.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Thickness * Scale);
}

void FShiftPainter::Arc(float Cx, float Cy, float R, float From01, float To01, const FLinearColor& Color, float Thickness)
{
	if (To01 <= From01 || Color.A <= 0.001f)
	{
		return;
	}
	const int32 Segments = FMath::Clamp(FMath::CeilToInt((To01 - From01) * 48.0f), 2, 64);
	TArray<FVector2f> Points;
	Points.Reserve(Segments + 1);
	for (int32 i = 0; i <= Segments; ++i)
	{
		const float Turn = FMath::Lerp(From01, To01, static_cast<float>(i) / static_cast<float>(Segments));
		const float Angle = Turn * UE_TWO_PI;
		Points.Add(Origin + FVector2f(Cx + R * FMath::Sin(Angle), Cy - R * FMath::Cos(Angle)) * Scale);
	}
	FSlateDrawElement::MakeLines(Out, ++Layer, Geo.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Thickness * Scale);
}

FVector2f FShiftPainter::Measure(const FString& Str, float Px, bool bBold) const
{
	if (Str.IsEmpty())
	{
		return FVector2f::ZeroVector;
	}
	const TSharedRef<FSlateFontMeasure> MeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	// Measure at the scale the text is drawn at: glyph advances do not scale linearly, so a width taken at scale 1 is too narrow in a larger window.
	const float DrawScale = FMath::Max(0.01f, Geo.GetAccumulatedLayoutTransform().GetScale() * Scale);
	const FVector2D Size = MeasureService->Measure(Str, MakeFont(Px, bBold), DrawScale);
	return FVector2f(static_cast<float>(Size.X) / DrawScale, static_cast<float>(Size.Y) / DrawScale);
}

FString FShiftPainter::Fit(const FString& Str, float MaxWidth, float Px, bool bBold) const
{
	if (Measure(Str, Px, bBold).X <= MaxWidth)
	{
		return Str;
	}
	FString Cut = Str;
	while (Cut.Len() > 1)
	{
		Cut.LeftChopInline(1);
		if (Measure(Cut + TEXT("..."), Px, bBold).X <= MaxWidth)
		{
			break;
		}
	}
	return Cut + TEXT("...");
}

TArray<FString> FShiftPainter::Wrap(const FString& Str, float MaxWidth, float Px, bool bBold) const
{
	TArray<FString> Lines;
	TArray<FString> Words;
	Str.ParseIntoArray(Words, TEXT(" "), true);
	FString Current;
	for (const FString& Word : Words)
	{
		const FString Candidate = Current.IsEmpty() ? Word : Current + TEXT(" ") + Word;
		if (Current.IsEmpty() || Measure(Candidate, Px, bBold).X <= MaxWidth)
		{
			Current = Candidate;
		}
		else
		{
			Lines.Add(Current);
			Current = Word;
		}
	}
	if (!Current.IsEmpty())
	{
		Lines.Add(Current);
	}
	return Lines;
}

float FShiftPainter::TextBlock(const FString& Str, float X, float Y, float MaxWidth, float Px, float LineHeight, const FLinearColor& Color,
	EShiftAlign Align, bool bBold)
{
	for (const FString& Line : Wrap(Str, MaxWidth, Px, bBold))
	{
		Text(Line, X, Y, Px, Color, Align, bBold);
		Y += LineHeight;
	}
	return Y;
}

void FShiftPainter::Polygon(const TArray<FVector2f>& Points, const FLinearColor& Color, float Thickness)
{
	if (Points.Num() < 2)
	{
		return;
	}
	TArray<FVector2f> Scaled;
	Scaled.Reserve(Points.Num() + 1);
	for (const FVector2f& Pt : Points)
	{
		Scaled.Add(Origin + Pt * Scale);
	}
	Scaled.Add(Origin + Points[0] * Scale);
	FSlateDrawElement::MakeLines(Out, ++Layer, Geo.ToPaintGeometry(), Scaled, ESlateDrawEffect::None, Color, true, Thickness * Scale);
}

void FShiftPainter::Text(const FString& Str, float X, float Y, float Px, const FLinearColor& Color, EShiftAlign Align, bool bBold)
{
	if (Str.IsEmpty() || Color.A <= 0.001f)
	{
		return;
	}
	const FVector2f Size = Measure(Str, Px, bBold);
	float Left = X;
	if (Align == EShiftAlign::Center)
	{
		Left = X - Size.X * 0.5f;
	}
	else if (Align == EShiftAlign::Right)
	{
		Left = X - Size.X;
	}
	FSlateDrawElement::MakeText(Out, ++Layer, Geom(Left, Y, Size.X + 2.0f, Size.Y), Str, MakeFont(Px, bBold), ESlateDrawEffect::None, Color);
}
