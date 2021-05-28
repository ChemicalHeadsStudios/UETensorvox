// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Modules/ModuleManager.h"

UETENSORVOX_API typedef TArray<int16> TAlignedSignedInt16Array;
DECLARE_LOG_CATEGORY_EXTERN(LogUETensorVox, Log, All);

#define TENSORVOX_VALID_PLATFORM (PLATFORM_WINDOWS || PLATFORM_APPLE || PLATFORM_ANDROID || PLATFORM_PS4 || PLATFORM_XBOXONE) && !UE_SERVER

class FUETensorVoxModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	static bool HasAvx();

	FORCEINLINE static bool CanRunTranscriber()
	{
		return HasAvx();
	}

	void* DeepSpeechHandle;

};


