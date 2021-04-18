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


void UAudioTranscriberComponent::CreateTranscriptionThread()
{
	if (CanLoadModel())
	{
		UE_LOG(LogUETensorVox, Warning, TEXT("Started realtime transcription."));
		bWorkerThreadRunning = true;

		ThreadHandle = AsyncThread([this, InModelPath = ModelPath, InScorerPath = ScorerPath, InBeamWidth = BeamWidth, InAsyncTickTranscriptionInterval = AsyncTickTranscriptionInterval]()
		{
			if (!this)
			{
				return;
			}


			FEvent* ThreadQueueNotify = FPlatformProcess::GetSynchEventFromPool();
			QueueNotify = ThreadQueueNotify;
			
			const FString& Model = FPaths::ProjectContentDir() + InModelPath;
			const FString& Scorer = FPaths::ProjectContentDir() + InScorerPath;

			ModelState* LoadedModel;
			if (CheckForError(TEXT("Model"), DS_CreateModel(TCHAR_TO_UTF8(*Model), &LoadedModel)))
			{
				FPlatformProcess::ReturnSynchEventToPool(ThreadQueueNotify);
				return;
			}

			if (LoadedModel && !InScorerPath.IsEmpty())
			{
				if (CheckForError(TEXT("LM scorer"), DS_EnableExternalScorer(LoadedModel, TCHAR_TO_UTF8(*Scorer))))
				{
					FPlatformProcess::ReturnSynchEventToPool(ThreadQueueNotify);
					return;
				}

				if (InBeamWidth != 0)
				{
					if (CheckForError(TEXT("Model beam width"), DS_SetModelBeamWidth(LoadedModel, InBeamWidth)))
					{
						FPlatformProcess::ReturnSynchEventToPool(ThreadQueueNotify);
						return;
					}
				}
			}
			else
			{
				FPlatformProcess::ReturnSynchEventToPool(ThreadQueueNotify);
				return;
			}
			


			StreamingState* Stream;
			DS_CreateStream(LoadedModel, &Stream);
			// DS_SetScorerAlphaBeta(LoadedModel, 2.0f, 1.0f);

			if (Stream)
			{
				UE_LOG(LogUETensorVox, Warning, TEXT("Created stream"));

				TArray<int16> AllSamples;

				FDeepSpeechMicrophoneRecorder Recorder;
				Recorder.StartRecording();				
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
						ThreadQueueNotify->Wait(FMath::TruncToInt(InAsyncTickTranscriptionInterval * 1000.0f));
					} else
					{
						break;
					}
				}
				DS_FreeStream(Stream);				
				Recorder.StopRecording();


				// When the record stops it's completely possible for this to be out of deleted.

				char* TranscriptionChar = DS_SpeechToText(LoadedModel, AllSamples.GetData(),
				                                          AllSamples.GetTypeSize() * AllSamples.Num());
				if (TranscriptionChar)
				{
					const FString& Word = FString(TranscriptionChar);
					UE_LOG(LogUETensorVox, Warning, TEXT("Transcribed %s"), *Word);
                    DS_FreeString(TranscriptionChar);
					
					if (this)
					{
						// AsyncTask(ENamedThreads::GameThread, [this, InWord = *Word]()
						// {
						// 	if(IsValid(this))
						// 	{
						// 		TranscribedWords = FString(InWord);
						// 	}
						// 	
						// });
					}
				}
			}

			DS_FreeModel(LoadedModel);

			if(ThreadQueueNotify)
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
	if(CanLoadModel())
	{
		EndRealtimeTranscription();
	}
	
	Super::EndPlay(EndPlayReason);
}


void UAudioTranscriberComponent::StartRealtimeTranscription()
{	
	if (CanLoadModel())
	{
		TranscribedWords.Empty();
		CreateTranscriptionThread();
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
