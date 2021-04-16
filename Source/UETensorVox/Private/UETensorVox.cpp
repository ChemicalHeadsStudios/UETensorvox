// Copyright Epic Games, Inc. All Rights Reserved.

#include "UETensorVox.h"
#include "Core.h"
#include "Modules/ModuleManager.h"
#include "deepspeech.h"
#include "IPluginManager.h"

#define LOCTEXT_NAMESPACE "FUETensorVoxModule"

DEFINE_LOG_CATEGORY(LogUETensorVox);

void FUETensorVoxModule::StartupModule()
{
	FString BinaryPath = FPaths::Combine(IPluginManager::Get().FindPlugin("UETensorVox")->GetBaseDir(),
                                                            TEXT("Binaries"), TEXT("ThirdParty"));

#if PLATFORM_WINDOWS
	BinaryPath = FPaths::Combine(BinaryPath, TEXT("Win64"));
#elif PLATFORM_LINUX
	BinaryPath = FPaths::Combine(BinaryPath, TEXT("Linux64"));
#elif PLATFORM_APPLE
	BinaryPath = FPaths::Combine(BinaryPath, TEXT("Apple64"));
#endif
	
	DeepSpeechHandle = FPlatformProcess::GetDllHandle(*FPaths::Combine(BinaryPath, TEXT("libdeepspeech.so")));
	if(DeepSpeechHandle)
	{
		char* VersionBuffer = DS_Version();
		UE_LOG(LogUETensorVox, Log, TEXT("Successfully loaded Mozilla's DeepSpeech library %s."), *FString(VersionBuffer));
		DS_FreeString(VersionBuffer);
	} else
	{
		UE_LOG(LogUETensorVox, Error, TEXT("Failed to load Mozilla's DeepSpeech library."));

	}
}

void FUETensorVoxModule::ShutdownModule()
{
	if(DeepSpeechHandle)
	{
		FPlatformProcess::FreeDllHandle(DeepSpeechHandle);
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FUETensorVoxModule, UETensorVox)
