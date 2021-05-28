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
#if TENSORVOX_VALID_PLATFORM
	if (CanRunTranscriber())
	{
		
		FString BinaryFullPath = FPaths::Combine(IPluginManager::Get().FindPlugin("UETensorVox")->GetBaseDir(),
			TEXT("Binaries"), TEXT("ThirdParty"));

#if PLATFORM_WINDOWS
		BinaryFullPath = FPaths::Combine(BinaryFullPath, TEXT("Win64"));
#elif PLATFORM_LINUX
		BinaryFullPath = FPaths::Combine(BinaryPath, TEXT("Linux64"));
#elif PLATFORM_APPLE
		BinaryFullPath = FPaths::Combine(BinaryPath, TEXT("Apple64"));
#endif

		DeepSpeechHandle = FPlatformProcess::GetDllHandle(*FPaths::Combine(BinaryFullPath, TEXT("libdeepspeech.so")));
		if (DeepSpeechHandle)
		{
			char* VersionBuffer = DS_Version();
			UE_LOG(LogUETensorVox, Log, TEXT("Successfully loaded Mozilla's DeepSpeech library %s."), *FString(VersionBuffer));
			DS_FreeString(VersionBuffer);
		}
		else
		{
			UE_LOG(LogUETensorVox, Error, TEXT("Failed to load Mozilla's DeepSpeech library."));
		}
	}
#endif
}

void FUETensorVoxModule::ShutdownModule()
{
#if TENSORVOX_VALID_PLATFORM
	if (DeepSpeechHandle)
	{
		FPlatformProcess::FreeDllHandle(DeepSpeechHandle);
	}
#endif
}


bool* GGlobalHasAVX = nullptr;

bool FUETensorVoxModule::HasAvx()
{
	// https://stackoverflow.com/questions/6121792/how-to-check-if-a-cpu-supports-the-sse3-instruction-set
#if PLATFORM_HAS_CPUID
	if (GGlobalHasAVX == nullptr)
	{
		int CpuInfo[4];
		__cpuid(CpuInfo, 1);

		const bool bOsUsesXSAVE_XRSTORE = CpuInfo[2] & (1 << 27);
		const bool bCpuAVXSuport = CpuInfo[2] & (1 << 28);
		bool bAVXSupported = false;
		if (bOsUsesXSAVE_XRSTORE && bCpuAVXSuport)
		{
			uint64 xcrFeatureMask = _xgetbv(_XCR_XFEATURE_ENABLED_MASK);
			bAVXSupported = (xcrFeatureMask & 0x6) == 0x6;
		}
		// Cache the result.
		GGlobalHasAVX = new bool(bAVXSupported);
		return bAVXSupported;
	}
	else
	{
		return *GGlobalHasAVX;
	}
#endif
	return false;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUETensorVoxModule, UETensorVox)
