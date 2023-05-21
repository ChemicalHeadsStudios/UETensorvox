// Copyright SIA Chemical Heads 2022

using System.IO;
using UnrealBuildTool;

public class UETensorVox : ModuleRules
{
	public UETensorVox(ReadOnlyTargetRules Target) : base(Target)
	{
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_2;
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
			}
		);

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) || Target.Platform == UnrealTargetPlatform.Mac || 
		    Target.Platform == UnrealTargetPlatform.IOS || Target.Platform == UnrealTargetPlatform.Android)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"WebRTC",
				}
			);
			AddEngineThirdPartyPrivateStaticDependencies(Target, "WebRTC");

			PrivateIncludePaths.AddRange(
				new string[] {
					Path.Combine(EngineDirectory, "Source", "Runtime", "AudioCaptureImplementations", "AudioCaptureRtAudio", "Private") // This is required to include RtAudio.h in AudioRecordingManager.h.
				}
			);
		}
		

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) ||
		    Target.Platform == UnrealTargetPlatform.Mac)
		{
			PrivateDependencyModuleNames.Add("AudioCaptureRtAudio");
		}
		// else if (Target.Platform == UnrealTargetPlatform.PS4)
		// {
		// 	PrivateDependencyModuleNames.Add("AudioCaptureSony");
		// }
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			PrivateDependencyModuleNames.Add("AudioCaptureAudioUnit");
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PrivateDependencyModuleNames.Add("AudioCaptureAndroid");
		}
		// else if (Target.Platform == UnrealTargetPlatform.Switch)
		// {
		// 	PrivateDependencyModuleNames.Add("AudioCaptureSwitch");
		// }
		else
		{
			// throw new BuildException("RtAudio not found on platform, consider disabling this plugin on this module!");
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Allow us to use direct sound
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DirectSound");
		}
	}
}