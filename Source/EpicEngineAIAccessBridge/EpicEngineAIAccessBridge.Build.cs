// Copyright ReapAndRuin Dev. All Rights Reserved.

using UnrealBuildTool;

public class EpicEngineAIAccessBridge : ModuleRules
{
	public EpicEngineAIAccessBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"BlueprintGraph",
			"KismetCompiler",
			"Kismet",
			"AssetRegistry",
			"HTTP",
			"HTTPServer",
			"Json",
			"JsonUtilities"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"ToolMenus"
		});
	}
}
