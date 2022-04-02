#pragma once

#include "CoreMinimal.h"
#include "DeepSpeechConfiguration.h"
#include "Components/ActorComponent.h"
#include "AudioTranscriberComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FAudioTranscriptionEvent, FString, Transcribed, bool, bFinalTranscription, int32, TranscriptionId);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), meta=(DisplayName="DeepSpeech Audio Transcriber"))
class UETENSORVOX_API UAudioTranscriberComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UAudioTranscriberComponent(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	
	virtual void PushTranscribeResult(const FString& TrancribedResult, bool bFinal = false);
	
	virtual void StartRealtimeTranscription();

	virtual void EndRealtimeTranscription();
public:

	UPROPERTY(Category="DeepSpeech Audio Transcriber", BlueprintReadOnly, EditAnywhere)
	FDeepSpeechConfiguration SpeechConfiguration;

	UPROPERTY(Category="DeepSpeech Audio Transcriber",BlueprintAssignable)
	FAudioTranscriptionEvent OnAudioTranscribed;
	
protected:
	virtual bool CanLoadModel();

	static void CreateTranscriptionThread(UAudioTranscriberComponent* TranscriberComponent);
	static void DestroyTranscriptionThread(UAudioTranscriberComponent* TranscriberComponent);
	static void NotifyTranscriptionThread();


	FString TranscribedResult;
	
	/**
	 * The input device's sample rate. Gathered at runtime.
	 */
	int32 RuntimeSampleRate;
	/**
	 * Model sample rate, gathered at runtime when 
	 */
	int32 ModelSampleRate;
	
	static bool CheckForError(const FString& Name, int32 Error);

};
