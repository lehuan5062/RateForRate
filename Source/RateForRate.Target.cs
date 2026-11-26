// Rate for Rate - Game Off 2026

using UnrealBuildTool;
using System.Collections.Generic;

public class RateForRateTarget : TargetRules
{
	public RateForRateTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("ShiftHappens");
		ExtraModuleNames.Add("ShiftGlass");
	}
}
