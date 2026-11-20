// P05 NetCostLab - primary game module: the replication workload and the benchmark harness only.
// The reusable system (the gameplay-aware Iris prioritiser) lives in the NetCostLabCore plugin; this module will depend
// on it, never the reverse. Replication modules are added in step S1, when code first uses them.

using UnrealBuildTool;

public class NetCostLab : ModuleRules
{
	public NetCostLab(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
