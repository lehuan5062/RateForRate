// Shift Happens - Game Off 2026

#pragma once

#include "CoreMinimal.h"

/**
 * The see-through window (Windows, D3D12): the desktop shows behind the board.
 *
 * UE 5.8 compiles per-pixel window transparency out of game builds, and the D3D12 swap chain it makes for a window (flip model, alpha ignored) cannot show alpha either.
 * What the engine does offer is IDXGISwapchainProvider: the D3D12 RHI asks every registered provider to create the swap chain of a window. The provider in
 * ShiftGlassWindow.cpp makes a DirectComposition swap chain with premultiplied alpha and attaches it to the window (with the window's frame extended into its client
 * area, so the window itself is glass). The renderer side is set by the config the settings menu writes (ShiftSave) and by the rest of the game (the background
 * material, the washes of the menus); nothing here touches the sim.
 *
 * It lives in its own module that loads at PostConfigInit: the engine makes the game window (for the loading screen) before the game module is loaded, and the window's swap
 * chain is made then, so the provider must already be registered.
 *
 * Off by default. It is requested by the saved setting (GameUserSettings [ShiftHappens] GlassWindow) or by -ShiftGlass[=opacity]; it never applies to the editor,
 * commandlets or headless (-RenderOffScreen) runs. If anything fails the normal swap chain is made instead and IsActive() stays false.
 */
namespace ShiftGlass
{
	/** The window should be made see-through: the saved setting or -ShiftGlass, outside the editor, commandlets and off-screen runs. */
	SHIFTGLASS_API bool IsRequested();

	/** A composition swap chain was made for the game window: the desktop really shows through (alpha is meaningful). */
	SHIFTGLASS_API bool IsActive();

	/** The opacity from -ShiftGlass=<0..1> (QA), or a negative number when the command line does not give one. */
	SHIFTGLASS_API float CommandLineOpacity();
}
