// Shift Happens - Game Off 2026

using UnrealBuildTool;

public class ShiftHappens : ModuleRules
{
	public ShiftHappens(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Flat module with subfolders (Game/, UI/, Sim/, ...): include as "UI/Foo.h" from anywhere in the module.
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "StateTreeModule", "GameplayTags", "PropertyBindingUtils", "GameFeatures"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"AudioMixer", "SlateCore", "ApplicationCore", "Projects",
			// All sound is MetaSound; the music clock is Harmonix (experimental plugins, enabled in ShiftHappens.uproject).
			"MetasoundEngine", "MetasoundFrontend", "MetasoundGraphCore", "MetasoundGenerator", "AudioExtensions",
			"HarmonixMetasound", "HarmonixMidi", "Harmonix", "MusicEnvironment"
		});

		// Editor-only: authoring of the StateTree assets (Behavior/StateTree/ShiftStateTreeBuilder.cpp).
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "StateTreeEditorModule", "AssetRegistry" });
		}
	}
}
