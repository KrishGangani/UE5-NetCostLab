// P05 NetCostLab - primary game module: the replication workload and the benchmark harness only.
// The reusable system (the gameplay-aware Iris prioritiser) lives in the NetCostLabCore plugin; this module will depend
// on it (step S7), never the reverse.

using UnrealBuildTool;

public class NetCostLab : ModuleRules
{
	public NetCostLab(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		// NetCore: the push-model macros (Net/Core/PushModel/PushModel.h); Json: the director's run reports.
		// Both are used only by our .cpp files.
		PrivateDependencyModuleNames.AddRange(new string[] { "NetCore", "Json" });

		// C5 (step S7): the gameplay prioritiser lives in the NetCostLabCore plugin; the binding (UNCLFocusSubsystem)
		// uses Iris types, so the module needs IrisCore and the Iris defines the engine headers expect.
		PrivateDependencyModuleNames.Add("NetCostLabCore");
		SetupIrisSupport(Target);
	}
}
