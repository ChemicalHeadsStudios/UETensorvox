#include "AudioTranscriberComponent.h"
#include "UETensorVox.h"
#include "deepspeech.h"
#include "DeepSpeechMicrophoneRecorder.h"

UAudioTranscriberComponent::UAudioTranscriberComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	bAutoActivate = false;
	ModelSampleRate = INDEX_NONE;
	RuntimeSampleRate = INDEX_NONE;
	BeamWidth = 500;
	PrimaryComponentTick.TickInterval = 1.0f / 20.0f;
	AsyncTickTranscriptionInterval = 1.0f / 3.0f;
}

void UAudioTranscriberComponent::CreateModelAsync(TFunction<void(bool)> Callback)
{
	if (CanLoadModel())
	{
		UE_LOG(LogUETensorVox, Warning, TEXT("Create DeepSpeech model async."));

		const FString& Model = FPaths::ProjectContentDir() + ModelPath;
		const FString& Scorer = FPaths::ProjectContentDir() + ScorerPath;

		if (LoadedModel)
		{
			DS_FreeModel(LoadedModel);
			UE_LOG(LogUETensorVox, Warning, TEXT("Disposed of existing DeepSpeech model in preparation for new model."));
		}

		ModelFuture = AsyncThread([=]()
		                          {
			                          ModelState* State;
			                          const int32 Error = DS_CreateModel(TCHAR_TO_UTF8(*Model), &State);
			                          if (State && !ScorerPath.IsEmpty())
			                          {
				                          DS_EnableExternalScorer(State, TCHAR_TO_UTF8(*Scorer));
				                          if (BeamWidth != 0)
				                          {
					                          DS_SetModelBeamWidth(State, BeamWidth);
				                          }
			                          }
			                          return FModelStateWithError{State, Error};
		                          }, 0, TPri_Normal, [=]()
		                          {
			                          AsyncTask(ENamedThreads::GameThread, [=]()
			                          {
				                          if (IsValid(this))
				                          {
					                          const FModelStateWithError& StateWithError = ModelFuture.Get();
					                          if (CheckForError(StateWithError.Error))
					                          {
						                          return;
					                          }
					                          UE_LOG(LogUETensorVox, Warning, TEXT("Loaded DeepSpeech model."));
					                          LoadedModel = StateWithError.State;
					                          if (Callback)
					                          {
						                          Callback(LoadedModel != nullptr);
					                          }
				                          }
			                          });
		                          });
	}
}


void UAudioTranscriberComponent::CreateTranscriptionThread()
{
	if (CanLoadModel() && LoadedModel)
	{
		UE_LOG(LogUETensorVox, Warning, TEXT("Started realtime transcription."));

		bWorkerThreadRunning = true;

		if (QueueNotify)
		{
			FPlatformProcess::ReturnSynchEventToPool(QueueNotify);
			QueueNotify = nullptr;
		}

		QueueNotify = FPlatformProcess::GetSynchEventFromPool();

		ThreadHandle = AsyncThread([this, AsyncTickTranscriptionInterval = this->AsyncTickTranscriptionInterval]()
		{
			StreamingState* Stream;
			DS_CreateStream(LoadedModel, &Stream);
			if (Stream)
			{
				UE_LOG(LogUETensorVox, Warning, TEXT("Created stream"));
		
				FDeepSpeechMicrophoneRecorder Recorder;
				Recorder.StartRecording();


				TArray<int16> AllSamples;
				
				while (bWorkerThreadRunning)
				{
					if (this)
					{
						while (Recorder.RawRecordingBlocks.Peek())
						{

							const FDeinterleavedAudio& Audio = *Recorder.RawRecordingBlocks.Peek();
							TArray<FDeinterleavedAudio> MultiChannel = Recorder.DownsampleAndSeperateChannels(Audio.PCMData);
						

							if (MultiChannel.Num() > 0)
							{
								const bool bAtleastStereo = MultiChannel.Num() >= 2;
								const TArray<int16>& Sample = bAtleastStereo
									                              ? FDeepSpeechMicrophoneRecorder::DownmixStereoToMono(
										                              MultiChannel[0].PCMData, MultiChannel[1].PCMData)
									                              : MultiChannel[0].PCMData;
								if (Sample.Num() > 0)
								{
									// DS_FeedAudioContent(Stream, Sample.GetData(), Sample.GetTypeSize() * Sample.Num());
									AllSamples.Append(Sample);
								}
							}
							
							Recorder.RawRecordingBlocks.Pop();
							
							// char* TranscriptionChar = DS_IntermediateDecode(Stream);
							// if (TranscriptionChar)
							// {
							// 	
							// 	const FString& Word = FString(TranscriptionChar);
							// 	{
							// 		FScopeLock Lock(&TranscriptionLock);
							// 		TranscribedWords = Word;
							// 	}
							// 	UE_LOG(LogUETensorVox, Warning, TEXT("Transcribed %s"), *Word);
							// 	DS_FreeString(TranscriptionChar);
							// }

						}
						QueueNotify->Wait(FMath::TruncToInt(AsyncTickTranscriptionInterval * 1000.0f));
					} else
					{
						break;
					}
				}
				Recorder.StopRecording();


				char* TranscriptionChar = DS_SpeechToText(LoadedModel, AllSamples.GetData(), AllSamples.GetTypeSize() * AllSamples.Num());
				if (TranscriptionChar)
				{
					const FString& Word = FString(TranscriptionChar);
					{
						FScopeLock Lock(&TranscriptionLock);
						TranscribedWords = Word;
					}
					UE_LOG(LogUETensorVox, Warning, TEXT("Transcribed %s"), *Word);
					DS_FreeString(TranscriptionChar);
				}
				
				DS_FreeStream(Stream);

				FPlatformProcess::ReturnSynchEventToPool(QueueNotify);

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
	EndRealtimeTranscription();

	if (CanLoadModel() && LoadedModel)
	{
		DS_FreeModel(LoadedModel);
	}

	Super::EndPlay(EndPlayReason);
}


void UAudioTranscriberComponent::StartRealtimeTranscription()
{
	TranscribedWords.Empty();
	
	if (LoadedModel)
	{
		CreateTranscriptionThread();
	}
	else
	{
		CreateModelAsync([this](bool bLoaded)
		{
			if (bLoaded)
			{
				if (IsValid(this))
				{
					CreateTranscriptionThread();
				}
			}
		});
	}
}

void UAudioTranscriberComponent::EndRealtimeTranscription()
{
	if (CanLoadModel() && bWorkerThreadRunning)
	{
		bWorkerThreadRunning = false;
		
		if (QueueNotify)
		{
			QueueNotify->Trigger();
		}

		if (ThreadHandle.IsValid())
		{
			ThreadHandle.Wait();
		}

		UE_LOG(LogUETensorVox, Warning, TEXT("Ended realtime transcription."));
	}
}

bool UAudioTranscriberComponent::CanLoadModel()
{
// #if UE_SERVER
// 	return false;
// #endif
	return true;
}


bool UAudioTranscriberComponent::CheckForError(int32 Error)
{
	if (Error != 0)
	{
		char* Buffer = DS_ErrorCodeToErrorMessage(Error);
		const FString& ErrorString = FString(Buffer);
		UE_LOG(LogUETensorVox, Error, TEXT("DeepSpeech Error: %s"), *ErrorString);
		DS_FreeString(Buffer);
		return true;
	}
	return false;
}
