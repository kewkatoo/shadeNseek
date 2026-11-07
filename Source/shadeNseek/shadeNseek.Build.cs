using UnrealBuildTool;

public class shadeNseek : ModuleRules
{
	public shadeNseek(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"UMG", "Slate", "SlateCore", "ImageWrapper", "AssetRegistry"
		});
	}
}
