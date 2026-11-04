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

		PublicIncludePaths.Add(ModuleDirectory);
	}
}
