// P05 NetCostLab - editor target. Its modules are also what the editor binary loads when it runs as the
// benchmark's dedicated server (UnrealEditor-Cmd.exe <project> <map> -server), the only server this engine can run.

using UnrealBuildTool;

public class NetCostLabEditorTarget : TargetRules
{
	public NetCostLabEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("NetCostLab");
	}
}
