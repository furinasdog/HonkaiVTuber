using UnrealBuildTool;

public class HonkaiLoadingScreen : ModuleRules
{
    public HonkaiLoadingScreen(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Slate", "SlateCore", "PreLoadScreen" });
    }
}
