// NetCostLabCore - the reusable runtime module: a gameplay-aware Iris prioritiser (added in step S6).
// It must not depend on any game module.

using UnrealBuildTool;

public class NetCostLabCore : ModuleRules
{
	public NetCostLabCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		// NetCore: FNetHandle (Net/Core/NetHandle/NetHandle.h), which the public header uses for focus targets.
		PublicDependencyModuleNames.Add("NetCore");

		// IrisCore (private), plus the UE_WITH_IRIS / fast-array defines the Iris headers expect (ModuleRules.cs:1747-1762).
		// Set up from Phase 4 on purpose: the plugin exists only to extend Iris, so its first build proves the Iris
		// dependency resolves against the installed engine before any prioritiser code is written.
		SetupIrisSupport(Target);
	}
}
