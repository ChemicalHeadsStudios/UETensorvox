// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Modules/ModuleManager.h"


DECLARE_LOG_CATEGORY_EXTERN(LogUETensorVox, Log, All);

class FUETensorVoxModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void* DeepSpeechHandle;

};