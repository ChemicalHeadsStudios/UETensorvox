#pragma once

#include "CoreMinimal.h"
#include "AudioCaptureComponent.h"
#include "GameplayTagContainer.h"
#include "AudioTranscriberComponent.generated.h"


USTRUCT(BlueprintType)
struct FDeepSpeechConfiguration
{
	GENERATED_BODY()
public:
	FDeepSpeechConfiguration() : BeamWidth(0), AsyncTickTranscriptionInterval(1.0)
	{
		
	}
	
	UPROPERTY(Category="DeepSpeech Audio Configuration", BlueprintReadOnly, EditAnywhere)
	int32 BeamWidth;
	
	UPROPERTY(Category="DeepSpeech Audio Configuration", BlueprintReadOnly, EditAnywhere)
	FString ModelPath;

	UPROPERTY(Category="DeepSpeech Audio Configuration", BlueprintReadOnly, EditAnywhere)
	FString ScorerPath;
	
	UPROPERTY(Category="DeepSpeech Audio Configuration", BlueprintReadOnly, EditAnywhere)
	float AsyncTickTranscriptionInterval;
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

	TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> bTranscriberRunning;

	virtual void StartRealtimeTranscription();

	virtual void EndRealtimeTranscription();

	static int16 ArrayMean(const TArray<int16>& InView);
	
public:

	UPROPERTY(Category="DeepSpeech Audio Transcriber", BlueprintReadOnly, EditAnywhere)
	FDeepSpeechConfiguration SpeechConfiguration;
	
protected:

	
	virtual bool CanLoadModel();

	virtual void CreateTranscriptionThread();


	TSharedPtr<FString, ESPMode::ThreadSafe> TranscribedWords;
	
	/**
	 * The input device's sample rate. Gathered at runtime.
	 */
	int32 RuntimeSampleRate;
	/**
	 * Model sample rate, gathered at runtime when 
	 */
	int32 ModelSampleRate;

	// Cleaned up by the interference thread.
	FEvent* QueueNotify;

	TFuture<void> ThreadHandle;
	
	static bool CheckForError(const FString& Name, int32 Error);

};
