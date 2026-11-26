// Rate for Rate - Game Off 2026

using UnrealBuildTool;

public class ShiftHappens : ModuleRules
{
	public ShiftHappens(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The simulation core (Sim/) must give the same bits in the editor, the Development game and the Shipping game. UBT compiles the editor with /fp:precise
		// but game targets with /fp:fast by default; fast-math changes the last bit of conviction and heat, so a state hash recorded by the shipped game did not match
		// when Tools\replay.ps1 replayed it in the editor ("DIVERGED"), although the votes came out the same. Precise everywhere.
		FPSemantics = FPSemanticsMode.Precise;

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
			// The see-through window: a swap chain provider for the D3D12 RHI, in a module of its own that loads early (Source/ShiftGlass).
			"ShiftGlass",
			// All sound is MetaSound; the music clock is Harmonix (experimental plugins, enabled in RateForRate.uproject).
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
