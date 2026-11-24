// Shift Happens - Game Off 2026

using UnrealBuildTool;
using System.Collections.Generic;

public class ShiftHappensEditorTarget : TargetRules
{
	public ShiftHappensEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("ShiftHappens");
		ExtraModuleNames.Add("ShiftGlass");
	}
}
