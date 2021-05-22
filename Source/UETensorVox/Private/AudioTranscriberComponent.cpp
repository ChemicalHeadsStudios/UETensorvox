#include "AudioTranscriberComponent.h"
#include "ThreadManager.h"

#include "UETensorVox.h"
#if TENSORVOX_VALID_PLATFORM
#include "DeepSpeechMicrophoneRecorder.h"
#include "WebRtcCommonAudioIncludes.h"
#include "deepspeech.h"
#endif

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
#if TENSORVOX_VALID_PLATFORM

	if (TranscriberComponent && TranscriberComponent->CanLoadModel() && !GTranscriberQueueRunning)
	{
		GTranscriberQueueRunning = true;
		AsyncThread([TranscriberComponent, Config = TranscriberComponent->SpeechConfiguration]()
		{
			const FString& ModelFullPath = FPaths::ProjectContentDir() + Config.ModelPath;
			const FString& ScorerFullPath = FPaths::ProjectContentDir() + Config.ScorerPath;

			TArray<TFuture<void>> DispatchedFuturesVoid;

			ModelState* Model;
			if (CheckForError(TEXT("Model"), DS_CreateModel(TCHAR_TO_UTF8(*ModelFullPath), &Model)))
			{
				return;
			}

			if (!Config.ScorerPath.IsEmpty())
			{
				if (CheckForError(TEXT("EnableExternalScorer"), DS_EnableExternalScorer(Model, TCHAR_TO_UTF8(*ScorerFullPath))))
				{
					DS_FreeModel(Model);
					return;
				}

				if (Config.ModelAlphaBeta.X != INDEX_NONE || Config.ModelAlphaBeta.Y != INDEX_NONE)
				{
					if (CheckForError(TEXT("SetAlphaBeta"), DS_SetScorerAlphaBeta(Model, Config.ModelAlphaBeta.X, Config.ModelAlphaBeta.Y)))
					{
						DS_FreeModel(Model);
						return;
					}
				}
			}

			if (Config.BeamWidth != 0)
			{
				if (CheckForError(TEXT("SetModelBeamWidth"), DS_SetModelBeamWidth(Model, Config.BeamWidth)))
				{
					DS_FreeModel(Model);
					return;
				}
			}

	
			const float GarbageFeedStart = 0.3f, GarbageFeedEnd = 0.1f;
			
			// We use VAD to determine what silence is and we just fill a buffer with the largest amount of garbage we need.
			const int32 SampleRate = 16000;
			const int32 SilenceTargetSamples = FMath::Max(GarbageFeedEnd, GarbageFeedStart) * (float)SampleRate;
			TAlignedSignedInt16Array Silence;
			Silence.Reserve(SilenceTargetSamples);
			
			UE_LOG(LogUETensorVox, Warning, TEXT("Started transcription worker. Model (alpha, beta): %s"), *Config.ModelAlphaBeta.ToString());
			{
				FDeepSpeechMicrophoneRecorder Recorder;
				TAlignedSignedInt16Array RecordedSamples;
				TFuture<FString> IntermediateTranscriptionResult;
				bool bLastRequestTranscribe = false;

				// VAD Aggressiveness mode (0, 1, 2, or 3).
				const int32 VadAggressivenes = 0;

#if WITH_WEBRTC
				// Create a WebRTC vad to determine voice level. 
				VadInst* VadInstance = WebRtcVad_Create();
				WebRtcVad_Init(VadInstance);				
				WebRtcVad_set_mode(VadInstance, VadAggressivenes);
#endif

				StreamingState* StreamState = nullptr;				
				while (GTranscriberQueueRunning)
				{
					bool bFeedVoiceData = false;
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
								if(StreamState)
								{
									DS_FeedAudioContent(StreamState, Audio.PCMData.GetData(), Audio.PCMData.Num());
									bFeedVoiceData = true;
								}
							} else if(Silence.Num() != SilenceTargetSamples)
							{
								// Fill silence buffer
								const int32 SamplesToAdd = FMath::Min(Audio.PCMData.Num(), SilenceTargetSamples - Silence.Num());
								if (SamplesToAdd > 0)
								{
									Silence.Append(Audio.PCMData.GetData(), SamplesToAdd);
								}
							}
						}
						Recorder.RawRecordingBlocks.Pop();
					}

					if (StreamState && bFeedVoiceData)
					{
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
					

					// Handle transcriptions state.
					if (bLastRequestTranscribe != GTranscribeRequested)
					{
						if (GTranscribeRequested)
						{
							// Start recording
							RecordedSamples.Empty();
							
							// WebRTC vad supports frame lengths of 320 and 480 at a 16000 sample rate.
							GTranscribeRequested = Recorder.StartRecording(SampleRate, 480);
							if(GTranscribeRequested)
							{
								if(!CheckForError(TEXT("StreamingState Init"), DS_CreateStream(Model, &StreamState)))
								{
									if (GarbageFeedStart > 0.0f)
									{
                                        TAlignedSignedInt16Array Garbage;
										if(Silence.Num() == SilenceTargetSamples)
										{
											Garbage = TAlignedSignedInt16Array(Silence, (float)Silence.Num() * GarbageFeedStart);
										} else
										{
											Garbage.SetNumZeroed(FMath::TruncToInt((float)SampleRate * GarbageFeedStart));												
										}
										
                                        DS_FeedAudioContent(StreamState, Garbage.GetData(), Garbage.Num());
                                    }
								}
							}
						}
						else
						{
							if (StreamState)
							{
								DispatchedFuturesVoid.Emplace(AsyncThread(
									[=]()
								{
									if (Model)
									{
										if (GarbageFeedEnd > 0.0f)
										{
											TAlignedSignedInt16Array Garbage;
											if (Silence.Num() == SilenceTargetSamples)
											{
												Garbage = TAlignedSignedInt16Array(Silence, (float)Silence.Num() * GarbageFeedEnd);
												UE_LOG(LogUETensorVox, Warning, TEXT("Used filled silence buffer"));
											}
											else
											{
												Garbage.SetNumZeroed(FMath::TruncToInt((float)SampleRate * GarbageFeedEnd));
											}
										}

										
										UE_LOG(LogUETensorVox, Warning, TEXT("Fed garbage"));
										char* TranscriptionChar = DS_FinishStream(StreamState);
										if (TranscriptionChar)
										{
											const FString& Word = FString(TranscriptionChar);
											if (!Word.IsEmpty())
											{
												// Check if game thread is up
												if (!IsEngineExitRequested())
												{
													AsyncTask(ENamedThreads::GameThread, [TranscriberComponent, Word]()
													{
														if (IsValid(TranscriberComponent))
														{
															TranscriberComponent->PushTranscribeResult(Word, true);
														}
													});
												} 
											}

											DS_FreeString(TranscriptionChar);
										}
									}
								}, 0, EThreadPriority::TPri_Normal));
								
								StreamState = nullptr;
							}
							
							// Finish recording
							Recorder.StopRecording();
#if 0
							AsyncTask(ENamedThreads::GameThread, [=, SampleRate = Recorder.RecordingSampleRate]()
							{
								FDeepSpeechMicrophoneRecorder::SaveAsWavMono(RecordedSamples, TEXT("/Game/TranscriberAudio/"), FString::Printf(TEXT("TranscriberAudioVAD%i"), VadAggressivenes), SampleRate);
							});
#endif
						}
						bLastRequestTranscribe = GTranscribeRequested;
					}
					GTranscribeQueueNotify->Wait(FMath::TruncToInt(Config.AsyncTickTranscriptionInterval * 1000.0f));
				}
#if WITH_WEBRTC
				// Clean up WebRTC.
				WebRtcVad_Free(VadInstance);
#endif WITH_WEBRTC

				// The futures use the model, so we can't clean it up until they are done. 
				for (const TFuture<void>& Dispatch : DispatchedFuturesVoid)
				{
					Dispatch.Wait();
				}

				DS_FreeModel(Model);
				Model = nullptr;
			}
			UE_LOG(LogUETensorVox, Warning, TEXT("Stopped transcription worker."));
		}, 0, EThreadPriority::TPri_Normal);
	}
#endif
}

void UAudioTranscriberComponent::DestroyTranscriptionThread(UAudioTranscriberComponent* TranscriberComponent)
{
#if TENSORVOX_VALID_PLATFORM
	if (TranscriberComponent && TranscriberComponent->CanLoadModel())
	{
		GTranscriberQueueRunning = false;
		NotifyTranscriptionThread();
	}
#endif
}

void UAudioTranscriberComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UAudioTranscriberComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
#if TENSORVOX_VALID_PLATFORM
	CreateTranscriptionThread(this);
#endif
}

void UAudioTranscriberComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if TENSORVOX_VALID_PLATFORM
	if (CanLoadModel())
	{
		EndRealtimeTranscription();
		DestroyTranscriptionThread(this);
	}
#endif

	Super::EndPlay(EndPlayReason);
}

void UAudioTranscriberComponent::PushTranscribeResult(const FString& InTrancribedResult, bool bFinal)
{
	TranscribedResult = InTrancribedResult;
	OnAudioTranscribed.Broadcast(InTrancribedResult, bFinal, 0);
}

void UAudioTranscriberComponent::StartRealtimeTranscription()
{
#if TENSORVOX_VALID_PLATFORM

	if (CanLoadModel())
	{
		GTranscribeRequested = true;
		NotifyTranscriptionThread();
	}
#endif
}

void UAudioTranscriberComponent::EndRealtimeTranscription()
{
#if TENSORVOX_VALID_PLATFORM
	if (CanLoadModel())
	{
		GTranscribeRequested = false;
		NotifyTranscriptionThread();
	}
#endif
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
#if TENSORVOX_VALID_PLATFORM
	return false;
#endif
	return true;
}

bool UAudioTranscriberComponent::CheckForError(const FString& Name, int32 Error)
{
#if TENSORVOX_VALID_PLATFORM
	if (Error != 0)
	{
		char* Buffer = DS_ErrorCodeToErrorMessage(Error);
		const FString& ErrorString = FString(Buffer);
		UE_LOG(LogUETensorVox, Error, TEXT("%s DeepSpeech Error: %s"), *Name, *ErrorString);
		DS_FreeString(Buffer);
		return true;
	}
#endif
	return false;
}
