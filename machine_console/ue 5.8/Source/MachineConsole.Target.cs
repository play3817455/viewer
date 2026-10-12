using UnrealBuildTool;
using System.Collections.Generic;

public class MachineConsoleTarget : TargetRules
{
	public MachineConsoleTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("MachineConsole");
	}
}
