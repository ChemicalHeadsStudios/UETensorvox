#pragma once

#include "CoreMinimal.h"
#include "AudioCaptureComponent.h"
#include "GameplayTagContainer.h"
#include "deepspeech.h"
#include "MultithreadedPatching.h"

#include "AudioTranscriberComponent.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), meta=(DisplayName="DeepSpeech Audio Transcriber"))
class UETENSORVOX_API UAudioTranscriberComponent : public UAudioCaptureComponent
{
	GENERATED_BODY()

public:
	UAudioTranscriberComponent(const FObjectInitializer& ObjectInitializer);

	ModelState* ModelState;
	
	UPROPERTY(Category="DeepSpeech Audio Transcriber", BlueprintReadOnly, EditAnywhere)
	FString ModelPath;	
	
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

};
