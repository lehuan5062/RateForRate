// Rate for Rate - Game Off 2026

#pragma once

#include "HAL/Platform.h"
#include "Math/Color.h"

/** Design colours for the entries. Written as sRGB hex and converted to linear for the unlit HDR materials. */
namespace ShiftPalette
{
	/** The colour-blind-safe set for the nine entries (Okabe and Ito, plus a brown) is used instead while this is on. Off by default, so footage and images do not change. */
	inline bool& ColourBlind()
	{
		static bool bOn = false;
		return bOn;
	}

	/** 0 = you (teal), 1 = rival (coral red), 2.. = the other entries. */
	inline FLinearColor Entry(int32 EntryIndex)
	{
		static const FLinearColor Safe[9] = {
			FLinearColor::FromSRGBColor(FColor(0x00, 0x9E, 0x73)), // you: bluish green
			FLinearColor::FromSRGBColor(FColor(0xD5, 0x5E, 0x00)), // rival: vermillion
			FLinearColor::FromSRGBColor(FColor(0xF0, 0xE4, 0x42)), // yellow
			FLinearColor::FromSRGBColor(FColor(0x00, 0x72, 0xB2)), // blue
			FLinearColor::FromSRGBColor(FColor(0xCC, 0x79, 0xA7)), // reddish purple
			FLinearColor::FromSRGBColor(FColor(0x56, 0xB4, 0xE9)), // sky blue
			FLinearColor::FromSRGBColor(FColor(0xE6, 0x9F, 0x00)), // orange
			FLinearColor::FromSRGBColor(FColor(0x99, 0x99, 0x99)), // grey
			FLinearColor::FromSRGBColor(FColor(0x8C, 0x51, 0x0A)), // brown
		};
		if (ColourBlind())
		{
			return Safe[((EntryIndex % 9) + 9) % 9];
		}
		static const FLinearColor Colors[9] = {
			FLinearColor::FromSRGBColor(FColor(0x1F, 0xB6, 0xA6)), // you
			FLinearColor::FromSRGBColor(FColor(0xFF, 0x4D, 0x5A)), // rival
			FLinearColor::FromSRGBColor(FColor(0xF2, 0xB1, 0x34)), // amber
			FLinearColor::FromSRGBColor(FColor(0x7B, 0x8C, 0xDE)), // periwinkle
			FLinearColor::FromSRGBColor(FColor(0x9B, 0x6D, 0xD7)), // violet
			FLinearColor::FromSRGBColor(FColor(0x4F, 0xB0, 0xE8)), // sky
			FLinearColor::FromSRGBColor(FColor(0xE8, 0x7A, 0xA8)), // pink
			FLinearColor::FromSRGBColor(FColor(0x7B, 0xC9, 0x6F)), // green
			FLinearColor::FromSRGBColor(FColor(0xC9, 0xA2, 0x7A)), // tan
		};
		return Colors[((EntryIndex % 9) + 9) % 9];
	}

	/** Voters who have not decided yet. */
	inline FLinearColor Undecided()
	{
		return FLinearColor::FromSRGBColor(FColor(0xC8, 0xC8, 0xD4));
	}
}
