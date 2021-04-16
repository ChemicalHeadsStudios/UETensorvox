#pragma once

#include "CoreMinimal.h"
#include "AudioCaptureComponent.h"
#include "GameplayTagContainer.h"
#include "deepspeech.h"
#include "MultithreadedPatching.h"

#include "AudioTranscriberComponent.generated.h"


struct FModelStateWithError
{
	ModelState* State;
	int32 Error;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), meta=(DisplayName="DeepSpeech Audio Transcriber"))
class UETENSORVOX_API UAudioTranscriberComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UAudioTranscriberComponent(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	FThreadSafeBool bWorkerThreadRunning;

	UFUNCTION(BlueprintCallable)
	virtual void StartRealtimeTranscription();

	
	virtual void EndRealtimeTranscription();

	
public:

	UPROPERTY(Category="DeepSpeech Audio Transcriber", BlueprintReadOnly, EditAnywhere)
	int32 BeamWidth;
	
	UPROPERTY(Category="DeepSpeech Audio Transcriber", BlueprintReadOnly, EditAnywhere)
	FString ModelPath;

	UPROPERTY(Category="DeepSpeech Audio Transcriber", BlueprintReadOnly, EditAnywhere)
	FString ScorerPath;
	
	UPROPERTY(Category="DeepSpeech Audio Transcriber", BlueprintReadOnly, EditAnywhere)
	float AsyncTickTranscriptionInterval;

protected:

	
	virtual bool CanLoadModel();

	virtual void CreateTranscriptionThread();
	virtual void CreateModelAsync(TFunction<void(bool)> Callback);

	/**
	 * The input device's sample rate. Gathered at runtime.
	 */
	int32 RuntimeSampleRate;
	/**
	 * Model sample rate, gathered at runtime when 
	 */
	int32 ModelSampleRate;

	TFuture<FModelStateWithError> ModelFuture;

	FEvent* QueueNotify;

	ModelState* LoadedModel;

	FCriticalSection TranscriptionLock; 
	FString TranscribedWords;

	TFuture<void> ThreadHandle;
	
	static bool CheckForError(int32 Error);

};
