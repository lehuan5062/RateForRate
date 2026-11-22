// Shift Happens - Game Off 2026

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "HAL/Platform.h"
#include "Layout/Geometry.h"
#include "Layout/PaintGeometry.h"
#include "Math/Color.h"
#include "Math/MathFwd.h"
#include "Rendering/DrawElements.h"

enum class EShiftAlign : uint8
{
	Left,
	Center,
	Right,
};

/**
 * Immediate-mode painter for the HUD. Coordinates are in 1280 x 720 design units; Origin/Scale place the design rect inside the
 * widget, and the layout scale also scales brush radii and font rasterisation, so everything stays crisp at any window size.
 * Brushes are persistent because Slate keeps brush pointers until the elements are batched after OnPaint returns.
 */
struct FShiftPainter
{
	FShiftPainter(FSlateWindowElementList& InOut, const FGeometry& InGeo, int32 InLayer, const FVector2f& InOrigin, float InScale)
		: Out(InOut), Geo(InGeo), Layer(InLayer), Origin(InOrigin), Scale(InScale)
	{
	}

	void Rect(float X, float Y, float W, float H, const FLinearColor& Color, float Radius = 0.0f);
	void Disc(float Cx, float Cy, float R, const FLinearColor& Color);
	void Line(const FVector2f& A, const FVector2f& B, const FLinearColor& Color, float Thickness = 1.5f);
	/** Arc around (Cx, Cy), from..to in 0..1 turns, clockwise starting at the top. */
	void Arc(float Cx, float Cy, float R, float From01, float To01, const FLinearColor& Color, float Thickness = 2.0f);
	void Text(const FString& Str, float X, float Y, float Px, const FLinearColor& Color, EShiftAlign Align = EShiftAlign::Left, bool bBold = false);

	FVector2f Measure(const FString& Str, float Px, bool bBold = false) const;
	/** Truncates with an ellipsis so the text fits MaxWidth design units. */
	FString Fit(const FString& Str, float MaxWidth, float Px, bool bBold = false) const;
	/** Greedy word wrap to MaxWidth design units. */
	TArray<FString> Wrap(const FString& Str, float MaxWidth, float Px, bool bBold = false) const;
	/** Draws wrapped text; returns the y just below the last line. */
	float TextBlock(const FString& Str, float X, float Y, float MaxWidth, float Px, float LineHeight, const FLinearColor& Color,
		EShiftAlign Align = EShiftAlign::Left, bool bBold = false);
	/** Closed polyline from design-unit points. */
	void Polygon(const TArray<FVector2f>& Points, const FLinearColor& Color, float Thickness = 2.0f);
	/** Filled simple polygon (convex or concave) in design units, drawn as thin horizontal rows: use opaque colors, outline it with Polygon. */
	void FillPolygon(const TArray<FVector2f>& Points, const FLinearColor& Color);

	FSlateWindowElementList& Out;
	const FGeometry& Geo;
	int32 Layer;
	FVector2f Origin;
	float Scale;

private:
	FPaintGeometry Geom(float X, float Y, float W, float H) const;
};
