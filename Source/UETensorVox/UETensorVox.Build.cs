// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UETensorVox : ModuleRules
{
	public UETensorVox(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;


		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"AudioCapture",
				"AudioCaptureCore",
				"OnlineSubsystem",
				"OnlineSubsystemUtils",
				"Projects",
				"UETensorVoxLibrary",
				"SignalProcessing"
			}
		);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AudioMixer",
			}
		);
	}
}