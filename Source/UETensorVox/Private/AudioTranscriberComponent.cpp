#include "AudioTranscriberComponent.h"
#include "UETensorVox.h"
#include "deepspeech.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "VoiceInterface.h"

UAudioTranscriberComponent::UAudioTranscriberComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	bAutoActivate = true;
}

void UAudioTranscriberComponent::BeginPlay()
{
	Super::BeginPlay();

#if !UE_SERVER
	{
		if (!ModelState)
		{
			const FString& Model = FPaths::ProjectContentDir() + ModelPath;
			int32 Status = DS_CreateModel(TCHAR_TO_UTF8(*Model), &ModelState);
			if (Status != 0)
			{
				char* error = DS_ErrorCodeToErrorMessage(Status);
				UE_LOG(LogUETensorVox, Warning, TEXT("Could not create model: %s."), *FString(error));
				free(error);
				return;
			}
			else
			{
				UE_LOG(LogUETensorVox, Warning, TEXT("DeepSpeech SST Model loaded!"));
			}
		}
	}


#endif
}

void UAudioTranscriberComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if !UE_SERVER
	if (ModelState)
	{
		UE_LOG(LogUETensorVox, Warning, TEXT("Deleted DeepSpeech model!"));
		DS_FreeModel(ModelState);
		ModelState = nullptr;
	}
#endif

	Super::EndPlay(EndPlayReason);
}

int32 UAudioTranscriberComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	int32 ReturnValue = Super::OnGenerateAudio(OutAudio, NumSamples);
	UE_LOG(LogUETensorVox, Warning, TEXT("Generated audio %i"), NumSamples);
	return ReturnValue;
}
