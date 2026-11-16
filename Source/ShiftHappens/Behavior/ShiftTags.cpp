// Shift Happens - Game Off 2026

#include "Behavior/ShiftTags.h"

namespace ShiftTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(RivalMegaPending, "Shift.Rival.MegaPending", "The last days started and the rival's Mega Campaign pulse has not been applied yet")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(RivalHasWeakHub, "Shift.Rival.HasWeakHub", "A high-value rival node is weak enough that the rival defends it")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(PlayerShadowbanned, "Shift.Player.Shadowbanned", "The player is shadowbanned after a strike")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(PlayerStrike1, "Shift.Player.Strike.1", "The player has one strike")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(PlayerStrike2, "Shift.Player.Strike.2", "The player has two strikes")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(PlayerStrike3, "Shift.Player.Strike.3", "The player has three strikes (disqualified)")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(PhaseEarly, "Shift.Phase.Early", "First third of the jam")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(PhaseMid, "Shift.Phase.Mid", "Middle of the jam")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(PhaseFinal, "Shift.Phase.Final", "The final rush")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(ModsTelegraphing, "Shift.Mods.Telegraphing", "The Mod Eye's next sweep is within its 3 s warning")
}
