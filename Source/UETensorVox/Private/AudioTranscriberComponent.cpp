#include "AudioTranscriberComponent.h"
#include "UETensorVox.h"
#include "deepspeech.h"
#include "DSP/FloatArrayMath.h"
#include "DeepSpeechMicrophoneRecorder.h"

UAudioTranscriberComponent::UAudioTranscriberComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = false;
	ModelSampleRate = INDEX_NONE;
	RuntimeSampleRate = INDEX_NONE;
}


void UAudioTranscriberComponent::CreateTranscriptionThread()
{
	if (CanLoadModel())
	{
		UE_LOG(LogUETensorVox, Warning, TEXT("Started realtime transcription."));
		ThreadHandle = AsyncThread([this, Config = SpeechConfiguration]()
		{
			if (!this)
			{
				return;
			}

			TSharedPtr<FString, ESPMode::ThreadSafe> ThreadTranscribedWords = MakeShared<FString, ESPMode::ThreadSafe>(TEXT(""));
			TranscribedWords = ThreadTranscribedWords;

			TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> bTranscribingRealtime = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(
				FThreadSafeBool(true));
			bTranscriberRunning = bTranscribingRealtime;

			FEvent* ThreadQueueNotify = FPlatformProcess::GetSynchEventFromPool();
			QueueNotify = ThreadQueueNotify;

			const FString& Model = FPaths::ProjectContentDir() + Config.ModelPath;
			const FString& Scorer = FPaths::ProjectContentDir() + Config.ScorerPath;

			ModelState* LoadedModel;
			if (CheckForError(TEXT("Model"), DS_CreateModel(TCHAR_TO_UTF8(*Model), &LoadedModel)))
			{
				FPlatformProcess::ReturnSynchEventToPool(ThreadQueueNotify);
				return;
			}

			if (!Config.ScorerPath.IsEmpty())
			{
				if (CheckForError(TEXT("LM scorer"), DS_EnableExternalScorer(LoadedModel, TCHAR_TO_UTF8(*Scorer))))
				{
					FPlatformProcess::ReturnSynchEventToPool(ThreadQueueNotify);
					return;
				}
			}

			if (Config.BeamWidth != 0)
			{
				if (CheckForError(TEXT("Model beam width"), DS_SetModelBeamWidth(LoadedModel, Config.BeamWidth)))
				{
					FPlatformProcess::ReturnSynchEventToPool(ThreadQueueNotify);
					return;
				}
			}


			StreamingState* StreamState;
			DS_CreateStream(LoadedModel, &StreamState);

			if (StreamState)
			{
				UE_LOG(LogUETensorVox, Warning, TEXT("Created stream"));

				FDeepSpeechMicrophoneRecorder Recorder;
				const bool bRecording = Recorder.StartRecording(DS_GetModelSampleRate(LoadedModel));

				FAlignedSignedInt16Array AllRecorderSamples;
				while ((*bTranscribingRealtime) && bRecording)
				{
					while (Recorder.RawRecordingBlocks.Peek())
					{
						const FDeinterleavedAudio& Audio = *Recorder.RawRecordingBlocks.Peek();
						if (Audio.PCMData.Num() > 0)
						{
							AllRecorderSamples.Append(Audio.PCMData);
							DS_FeedAudioContent(StreamState, Audio.PCMData.GetData(), Audio.PCMData.GetTypeSize() * Audio.PCMData.Num());
						}
						Recorder.RawRecordingBlocks.Pop();
					}

					char* TranscriptionChar = DS_IntermediateDecode(StreamState);
					if (TranscriptionChar)
					{
						const FString& Word = FString(TranscriptionChar);
						UE_LOG(LogUETensorVox, Warning, TEXT("Intermediate decode result: %s"), *Word);
						DS_FreeString(TranscriptionChar);
					}

					ThreadQueueNotify->Wait(FMath::TruncToInt(Config.AsyncTickTranscriptionInterval * 1000.0f));
				}
				Recorder.StopRecording();

				AsyncThread([LoadedModelPtr = LoadedModel, AllRecorderSamples]()
				{
					char* TranscriptionChar = DS_SpeechToText(LoadedModelPtr, AllRecorderSamples.GetData(),
					                                          AllRecorderSamples.GetTypeSize() * AllRecorderSamples.Num());
					if (TranscriptionChar)
					{
						const FString& Word = FString(TranscriptionChar);
						UE_LOG(LogUETensorVox, Warning, TEXT("Finished transcription with: %s"), *Word);
						DS_FreeString(TranscriptionChar);
					}

					DS_FreeModel(LoadedModelPtr);
				}, 0, EThreadPriority::TPri_Normal);
			};

			UE_LOG(LogUETensorVox, Warning, TEXT("Transcription thread stopped."));

			if (ThreadQueueNotify)
			{
				AsyncTask(ENamedThreads::GameThread, [Event = ThreadQueueNotify]()
				{
					if (Event)
					{
						FPlatformProcess::ReturnSynchEventToPool(Event);
					}
				});
			}
		}, 0, EThreadPriority::TPri_Normal);
	}
}

void UAudioTranscriberComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UAudioTranscriberComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UAudioTranscriberComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CanLoadModel())
	{
		EndRealtimeTranscription();
	}

	Super::EndPlay(EndPlayReason);
}


void UAudioTranscriberComponent::StartRealtimeTranscription()
{
	if (CanLoadModel())
	{
		if (!(bTranscriberRunning.IsValid() && *bTranscriberRunning))
		{
			CreateTranscriptionThread();
		}
		else
		{
			UE_LOG(LogUETensorVox, Error, TEXT("Attempted to start transcription while transcriber is already running"));
		}
	}
}

void UAudioTranscriberComponent::EndRealtimeTranscription()
{
	if (CanLoadModel() && bTranscriberRunning.IsValid() && (*bTranscriberRunning))
	{
		*bTranscriberRunning = false;

		if (QueueNotify)
		{
			QueueNotify->Trigger();
		}

		UE_LOG(LogUETensorVox, Warning, TEXT("Ended realtime transcription."));
	}
}

int16 UAudioTranscriberComponent::ArrayMean(const TArray<int16>& InView)
{
	int32 OutMean = 0;

	const int32 Num = InView.Num();
	for (int32 i = 0; i < Num; i++)
	{
		OutMean += InView[i];
	}

	OutMean /= static_cast<float>(Num);
	return OutMean;
}

bool UAudioTranscriberComponent::CanLoadModel()
{
#if UE_SERVER
	return false;
#endif
	return true;
}


bool UAudioTranscriberComponent::CheckForError(const FString& Name, int32 Error)
{
	if (Error != 0)
	{
		char* Buffer = DS_ErrorCodeToErrorMessage(Error);
		const FString& ErrorString = FString(Buffer);
		UE_LOG(LogUETensorVox, Error, TEXT("%s DeepSpeech Error: %s"), *Name, *ErrorString);
		DS_FreeString(Buffer);
		return true;
	}
	return false;
}
