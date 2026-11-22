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
			"HarmonixMetasound", "HarmonixMidi", "Harmonix", "MusicEnvironment",
			// The module's own MetaSound nodes (Audio/Nodes/) use the DSP library.
			"SignalProcessing"
		});

		// The module's own registration list for those nodes (see ShiftHappens.cpp).
		PrivateDefinitions.Add("METASOUND_PLUGIN=Shift");
		PrivateDefinitions.Add("METASOUND_MODULE=ShiftHappens");

		// Editor-only: authoring of the StateTree assets (Behavior/StateTree/ShiftStateTreeBuilder.cpp).
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "StateTreeEditorModule", "AssetRegistry" });
		}
	}
}
