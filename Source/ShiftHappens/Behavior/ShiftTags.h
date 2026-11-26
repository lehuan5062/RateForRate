// Rate for Rate - Game Off 2026

#pragma once

#include "NativeGameplayTags.h"

/**
 * Gameplay Tags that describe the state of a jam run to the behaviour StateTrees. The tags are derived from the numeric core every sim step by
 * FShiftEvaluator_GameTags; the stock "Has Tags" StateTree condition then tests them (the rival tree's Mega Campaign and Defend gates use it).
 *
 * Tags only: there is no Ability System here. The sim core is a fixed-step, deterministic, engine-free simulation and the Ability System needs
 * actors and a ticking world.
 */
namespace ShiftTags
{
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(RivalMegaPending)      // Shift.Rival.MegaPending   the last days started and the rival's Mega Campaign has not been applied yet
		UE_DECLARE_GAMEPLAY_TAG_EXTERN(RivalHasWeakHub)       // Shift.Rival.HasWeakHub    a high-value rival node is weak enough to defend
		UE_DECLARE_GAMEPLAY_TAG_EXTERN(PlayerShadowbanned)    // Shift.Player.Shadowbanned the player is shadowbanned after a strike
		UE_DECLARE_GAMEPLAY_TAG_EXTERN(PlayerStrike1)         // Shift.Player.Strike.1 .. .3
		UE_DECLARE_GAMEPLAY_TAG_EXTERN(PlayerStrike2)
		UE_DECLARE_GAMEPLAY_TAG_EXTERN(PlayerStrike3)
		UE_DECLARE_GAMEPLAY_TAG_EXTERN(PhaseEarly)            // Shift.Phase.Early|Mid|Final  the phase of the jam by time
		UE_DECLARE_GAMEPLAY_TAG_EXTERN(PhaseMid)
		UE_DECLARE_GAMEPLAY_TAG_EXTERN(PhaseFinal)
		UE_DECLARE_GAMEPLAY_TAG_EXTERN(ModsTelegraphing)      // Shift.Mods.Telegraphing   the Mod Eye's next sweep is within its 3 s warning
}
