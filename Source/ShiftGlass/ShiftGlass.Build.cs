// Rate for Rate - Game Off 2026

using UnrealBuildTool;

// The see-through window: a swap chain provider for the D3D12 RHI. A module of its own because it must load at PostConfigInit: the engine makes the game window
// (for the loading screen) before the game module loads, and the window's swap chain is made then.
public class ShiftGlass : ModuleRules
{
	public ShiftGlass(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
		PrivateDependencyModuleNames.AddRange(new string[] { "RHI", "Engine", "ImageCore" });

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.AddRange(new string[] { "dcomp.lib", "dwmapi.lib" });
		}
	}
}
