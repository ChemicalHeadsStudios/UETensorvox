// Fill out your copyright notice in the Description page of Project Settings.

using System.Diagnostics.Eventing.Reader;
using System.IO;
using UnrealBuildTool;

public class UETensorVoxLibrary : ModuleRules
{
	public UETensorVoxLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		PublicDefinitions.Add("WITH_TENSORVOX=1");
		
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "include"));
		// libdeepspeech is always a .so, no matter what platform. Kind of nice eh?

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "Win64", "libdeepspeech.so.if.lib"));
			RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "Win64", "libdeepspeech.so"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux && Target.Architecture.StartsWith("x86_64"))
		{
			RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "Linux64", "libdeepspeech.so"));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Apple))
		{
			RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "Apple64", "libdeepspeech.so"));
		}
		


		PublicDelayLoadDLLs.Add("libdeepspeech.so");	
	}
}