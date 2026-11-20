// P05 NetCostLab - game target: the packaged build used for the headless benchmark clients.
// There is no Server target: the launcher engine cannot build one (01b_Research_Findings.md #1).

using UnrealBuildTool;

public class NetCostLabTarget : TargetRules
{
	public NetCostLabTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("NetCostLab");
	}
}
