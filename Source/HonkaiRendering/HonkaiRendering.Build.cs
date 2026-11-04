// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class HonkaiRendering : ModuleRules
{
	public HonkaiRendering(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Viewer and mapping files have independent file-local Slate helpers.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

        PrivateDependencyModuleNames.AddRange(new string[] { "SlateCore", "VRM4U", "VRM4ULoader", "AnimGraphRuntime", "RenderCore", "RHI", "Spout2_DX12", "Json", "ImageWrapper", "HonkaiMotionCapture", "HonkaiToonRenderer", "Projects", "ApplicationCore" });
		PrivateIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "Intermediate", "HonkaiBuildInfo")));
		foreach (string Script in new string[] { "Expand-MmdArchive.ps1", "Prepare-MmdEnhancement.ps1", "enhancement-tools.json", "generate_enhancement.py", "bake_face_coordinates.py" })
            RuntimeDependencies.Add("$(ProjectDir)/Scripts/" + Script, StagedFileType.NonUFS);
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.AddRange(new string[] { "ole32.lib", "uuid.lib" });
		}

		PublicIncludePaths.AddRange(new string[] {
			"HonkaiRendering",
			"HonkaiRendering/Variant_Platforming",
			"HonkaiRendering/Variant_Platforming/Animation",
			"HonkaiRendering/Variant_Combat",
			"HonkaiRendering/Variant_Combat/AI",
			"HonkaiRendering/Variant_Combat/Animation",
			"HonkaiRendering/Variant_Combat/Gameplay",
			"HonkaiRendering/Variant_Combat/Interfaces",
			"HonkaiRendering/Variant_Combat/UI",
			"HonkaiRendering/Variant_SideScrolling",
			"HonkaiRendering/Variant_SideScrolling/AI",
			"HonkaiRendering/Variant_SideScrolling/Gameplay",
			"HonkaiRendering/Variant_SideScrolling/Interfaces",
			"HonkaiRendering/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
