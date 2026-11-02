// SPDX-License-Identifier: MIT
using UnrealBuildTool;

public class HonkaiWindowStartup : ModuleRules
{
    public HonkaiWindowStartup(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.Add("Core");
    }
}
