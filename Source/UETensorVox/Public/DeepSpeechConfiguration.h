#pragma once

#include "CoreMinimal.h"
#include "DeepSpeechConfiguration.generated.h"

USTRUCT(BlueprintType)
struct FDeepSpeechConfiguration
{
	GENERATED_BODY()
public:
	FDeepSpeechConfiguration() : BeamWidth(0), AsyncTickTranscriptionInterval(1.0)
	{
		ModelAlphaBeta = {INDEX_NONE, INDEX_NONE};
	}
	
	UPROPERTY(Category="DeepSpeech Audio Configuration", BlueprintReadOnly, EditAnywhere)
	int32 BeamWidth;
	
	UPROPERTY(Category="DeepSpeech Audio Configuration", BlueprintReadOnly, EditAnywhere)
	FString ModelPath;

	UPROPERTY(Category="DeepSpeech Audio Configuration", BlueprintReadOnly, EditAnywhere)
	FString ScorerPath;
	
	UPROPERTY(Category="DeepSpeech Audio Configuration", BlueprintReadOnly, EditAnywhere)
	float AsyncTickTranscriptionInterval;
	
	UPROPERTY(Category="DeepSpeech Audio Configuration", BlueprintReadOnly, EditAnywhere)
	FVector2D ModelAlphaBeta;
};
