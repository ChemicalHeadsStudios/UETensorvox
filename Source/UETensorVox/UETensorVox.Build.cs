// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class UETensorVox : ModuleRules
{
	public UETensorVox(ReadOnlyTargetRules Target) : base(Target)
	{
		bEnableExceptions = true;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"AudioCapture",
				"AudioCaptureCore",
				"Projects",	
				"UETensorVoxLibrary"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AudioMixer",
				"AudioPlatformConfiguration",
				"WebRTC"
			}
		);

		
		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) ||
		    Target.Platform == UnrealTargetPlatform.Mac)
		{

			PrivateDependencyModuleNames.Add("AudioCaptureRtAudio");
		}
		
		PrivateIncludePaths.AddRange(
			new string[] {
				Path.Combine(EngineDirectory, "Source", "Runtime", "AudioCaptureImplementations", "AudioCaptureRtAudio", "Private") // This is required to include RtAudio.h in AudioRecordingManager.h.
			}
		);

		if (Target.Platform == UnrealTargetPlatform.Win32 ||
		    Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Allow us to use direct sound
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DirectSound");
		}
		
		AddEngineThirdPartyPrivateStaticDependencies(Target, "WebRTC");
	}
}