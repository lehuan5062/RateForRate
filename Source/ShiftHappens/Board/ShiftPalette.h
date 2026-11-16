// Shift Happens - Game Off 2026

#pragma once

#include "HAL/Platform.h"
#include "Math/Color.h"

/** Design colours for the entries. Written as sRGB hex and converted to linear for the unlit HDR materials. */
namespace ShiftPalette
{
	/** 0 = you (teal), 1 = rival (coral red), 2.. = the other entries. */
	inline FLinearColor Entry(int32 EntryIndex)
	{
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
