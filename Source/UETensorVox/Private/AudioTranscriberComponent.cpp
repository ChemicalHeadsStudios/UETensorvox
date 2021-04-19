#include "AudioTranscriberComponent.h"
#include "UETensorVox.h"
#include "deepspeech.h"
#include "DeepSpeechMicrophoneRecorder.h"
#include "WebRtcCommonAudioIncludes.h"

UAudioTranscriberComponent::UAudioTranscriberComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 1.0f / 5.0f;
	bAutoActivate = false;
	ModelSampleRate = INDEX_NONE;
	RuntimeSampleRate = INDEX_NONE;
}

FThreadSafeBool GTranscriberQueueRunning;
FThreadSafeBool GTranscribeRequested;
FEvent* GTranscribeQueueNotify = FPlatformProcess::GetSynchEventFromPool();

void UAudioTranscriberComponent::CreateTranscriptionThread(UAudioTranscriberComponent* TranscriberComponent)
{
	if (TranscriberComponent && TranscriberComponent->CanLoadModel() && !GTranscriberQueueRunning)
	{
		GTranscriberQueueRunning = true;
		AsyncThread([TranscriberComponent, Config = TranscriberComponent->SpeechConfiguration]()
		{
			const FString& Model = FPaths::ProjectContentDir() + Config.ModelPath;
			const FString& Scorer = FPaths::ProjectContentDir() + Config.ScorerPath;

			TArray<TFuture<void>> DispatchedFuturesVoid;

			ModelState* LoadedModel;
			if (CheckForError(TEXT("Model"), DS_CreateModel(TCHAR_TO_UTF8(*Model), &LoadedModel)))
			{
				return;
			}

			if (!Config.ScorerPath.IsEmpty())
			{
				if (CheckForError(TEXT("LM scorer"), DS_EnableExternalScorer(LoadedModel, TCHAR_TO_UTF8(*Scorer))))
				{
					return;
				}
			}

			if (Config.BeamWidth != 0)
			{
				if (CheckForError(TEXT("Model beam width"), DS_SetModelBeamWidth(LoadedModel, Config.BeamWidth)))
				{
					return;
				}
			}

			UE_LOG(LogUETensorVox, Warning, TEXT("Started transcription worker."));
			{
				FDeepSpeechMicrophoneRecorder Recorder;
				TAlignedSignedInt16Array RecordedSamples;
				TFuture<FString> IntermediateTranscriptionResult;
				bool bLastRequestTranscribe = false;

#if WITH_WEBRTC
				// Create a WebRTC vad to determine voice level. 
				VadInst* VadInstance = WebRtcVad_Create();
				WebRtcVad_Init(VadInstance);
#endif

				StreamingState* StreamState;
				CheckForError(TEXT("StreamingState"), DS_CreateStream(LoadedModel, &StreamState));
				
				while (GTranscriberQueueRunning)
				{
					// Collect the remaining recorded blocks.
					TAlignedSignedInt16Array LocalRecordingBuffer;
					while (Recorder.RawRecordingBlocks.Peek())
					{
						const FDeinterleavedAudio& Audio = *Recorder.RawRecordingBlocks.Peek();
						if (Audio.PCMData.Num() > 0)
						{
							bool bVoiceDetected = true;

#if WITH_WEBRTC
							const int32 VoiceStatus = WebRtcVad_Process(VadInstance, Recorder.RecordingSampleRate, Audio.PCMData.GetData(),
							                                            Audio.PCMData.Num());
							bVoiceDetected = VoiceStatus == 1 || VoiceStatus == -1;
#endif
							// Let audio data in if the vad has detected a voice level, or if it errors out due to a special mic or something.

							if (bVoiceDetected)
							{
								RecordedSamples.Append(Audio.PCMData);
								LocalRecordingBuffer.Append(Audio.PCMData);
							}
						}
						Recorder.RawRecordingBlocks.Pop();
					}

					// Handle transcriptions state.
					if (bLastRequestTranscribe != GTranscribeRequested)
					{
						
						if (GTranscribeRequested)
						{
							// Start recording
							RecordedSamples.Empty();
							// WebRTC supports frame lengths of 320 and 480 at a 16000 sample rate.
							GTranscribeRequested = Recorder.StartRecording(16000, 480);
						}
						else
						{
							// Finish recordingg
							Recorder.StopRecording();
#if 1
							AsyncTask(ENamedThreads::GameThread, [=, SampleRate = Recorder.RecordingSampleRate]()
							{
								FDeepSpeechMicrophoneRecorder::SaveAsWavMono(RecordedSamples, TEXT("/Game/TranscriberAudio/"), TEXT("TranscriberAudio"), SampleRate);
							});
#endif
							DispatchedFuturesVoid.Emplace(AsyncThread([=]()
							{
								if (LoadedModel)
								{
									char* TranscriptionChar = DS_SpeechToText(LoadedModel, RecordedSamples.GetData(), RecordedSamples.Num());

									if (TranscriptionChar)
									{
										const FString& Word = FString(TranscriptionChar);
										if (!Word.IsEmpty())
										{
											AsyncTask(ENamedThreads::GameThread, [TranscriberComponent, Word]()
											{
												if(IsValid(TranscriberComponent))
												{
													TranscriberComponent->PushTranscribeResult(Word, true);
												}
											});
										}
										
										DS_FreeString(TranscriptionChar);
									}
								}
							}, 0, EThreadPriority::TPri_Normal));
						}
						bLastRequestTranscribe = GTranscribeRequested;
					}

					if (GTranscribeRequested && StreamState && LocalRecordingBuffer.Num() > 0)
					{
						DS_FeedAudioContent(StreamState, LocalRecordingBuffer.GetData(), LocalRecordingBuffer.Num());
						char* IntermediateResult = DS_IntermediateDecode(StreamState);
						if (IntermediateResult)
						{
							FString IntermediateTranscribe = FString(IntermediateResult);
							DS_FreeString(IntermediateResult);
							if (!IntermediateTranscribe.IsEmpty())
							{
								AsyncTask(ENamedThreads::GameThread, [TranscriberComponent, IntermediateTranscribe]()
								{
									if (IsValid(TranscriberComponent))
									{
										TranscriberComponent->PushTranscribeResult(IntermediateTranscribe);
									}
								});
							}
						}
					}
					GTranscribeQueueNotify->Wait(FMath::TruncToInt(Config.AsyncTickTranscriptionInterval * 1000.0f));
				}
#if WITH_WEBRTC
				// Clean up WebRTC.
				WebRtcVad_Free(VadInstance);
#endif WITH_WEBRTC

				if(StreamState)
				{
					DS_FreeStream(StreamState);
				}
				
				// The futures use the model, so we can't clean it up until they are done. 
				for (const TFuture<void>& Dispatch : DispatchedFuturesVoid)
				{
					Dispatch.Wait();
				}

				DS_FreeModel(LoadedModel);
			}
			UE_LOG(LogUETensorVox, Warning, TEXT("Stopped transcription worker."));
		}, 0, EThreadPriority::TPri_Normal);
	}
}

void UAudioTranscriberComponent::DestroyTranscriptionThread(UAudioTranscriberComponent* TranscriberComponent)
{
	if (TranscriberComponent && TranscriberComponent->CanLoadModel())
	{
		GTranscriberQueueRunning = false;
		NotifyTranscriptionThread();
	}
}

void UAudioTranscriberComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UAudioTranscriberComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);


	CreateTranscriptionThread(this);
}

void UAudioTranscriberComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CanLoadModel())
	{
		EndRealtimeTranscription();
		DestroyTranscriptionThread(this);
	}

	Super::EndPlay(EndPlayReason);
}

void UAudioTranscriberComponent::PushTranscribeResult(const FString& InTrancribedResult, bool bFinal)
{
	UE_LOG(LogUETensorVox, Log, TEXT("Out Transcribe: %s"), *InTrancribedResult);
	TranscribedResult = InTrancribedResult;
}

void UAudioTranscriberComponent::StartRealtimeTranscription()
{
	if (CanLoadModel())
	{
		GTranscribeRequested = true;
		NotifyTranscriptionThread();
	}
}

void UAudioTranscriberComponent::EndRealtimeTranscription()
{
	if (CanLoadModel())
	{
		GTranscribeRequested = false;
		NotifyTranscriptionThread();
	}
}

void UAudioTranscriberComponent::NotifyTranscriptionThread()
{
	if (GTranscribeQueueNotify)
	{
		GTranscribeQueueNotify->Trigger();
	}
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
