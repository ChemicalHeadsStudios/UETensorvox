#include "DeepSpeechMicrophoneRecorder.h"

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "AudioDeviceManager.h"
#include "UETensorVox.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/AudioComponent.h"


/**
* Callback Function For the Microphone Capture for RtAudio
*/
#if TENSORVOX_VALID_PLATFORM
static int32 OnAudioCaptureCallback(void* OutBuffer, void* InBuffer, uint32 InBufferFrames, double StreamTime,
                                    RtAudioStreamStatus AudioStreamStatus, void* InUserData)
{
	// Check for stream overflow (i.e. we're feeding audio faster than we're consuming)


	// Cast the user data to the singleton mic recorder
	FDeepSpeechMicrophoneRecorder* AudioRecordingManager = (FDeepSpeechMicrophoneRecorder*)InUserData;

	// Call the mic capture callback function
	return AudioRecordingManager->OnAudioCapture(InBuffer, InBufferFrames, StreamTime, AudioStreamStatus == RTAUDIO_INPUT_OVERFLOW);
}
#endif 

/**
* FDeepSpeechMicrophoneRecorder Implementation
*/

FDeepSpeechMicrophoneRecorder::FDeepSpeechMicrophoneRecorder()
{
	TargetSampleRate = 16000;
	bRecording = false;
	bSplitChannels = false;
	bError = false;
	NumInputChannels.Set(1);
	NumOverflowsDetected = 0;
}

FDeepSpeechMicrophoneRecorder::~FDeepSpeechMicrophoneRecorder()
{
#if TENSORVOX_VALID_PLATFORM

	if (ADCInstance.isStreamOpen())
	{
		ADCInstance.abortStream();
	}
#endif
}

bool FDeepSpeechMicrophoneRecorder::StartRecording(int32 InTargetSampleRate, int32 RecordingBlockSize)
{
#if TENSORVOX_VALID_PLATFORM
	if (bError)
	{
		return false;
	}

	if (bRecording)
	{
		StopRecording();
	}

	TargetSampleRate = InTargetSampleRate;


	// If we have a stream open close it (reusing streams can cause a blip of previous recordings audio)
	if (ADCInstance.isStreamOpen())
	{
		try
		{
			ADCInstance.stopStream();
			ADCInstance.closeStream();
		}
		catch (RtAudioError& e)
		{
			FString ErrorMessage = FString(e.what());
			bError = true;
			UE_LOG(LogUETensorVox, Error, TEXT("Failed to close the mic capture device stream: %s"), *ErrorMessage);
			return false;
		}
	}

	// Get the default mic input device info
	RtAudio::DeviceInfo Info = ADCInstance.getDeviceInfo(StreamParams.deviceId);
	NumInputChannels.Set(Info.inputChannels);
	
	bool bSampleRateFound = false;


	for (auto SampleRate : Info.sampleRates)
	{
		if (static_cast<int32>(SampleRate) == TargetSampleRate)
		{
			RecordingSampleRate = SampleRate;
			bSampleRateFound = true;
			break;
		}
	}

	if (!bSampleRateFound)
	{
		UE_LOG(LogUETensorVox, Warning, TEXT("Sample %i rate not found. Selecting nearest."), TargetSampleRate);

		auto GetClosests = [](std::vector<unsigned> const& Vec, unsigned Value) -> int32
		{
			auto const It = std::lower_bound(Vec.begin(), Vec.end(), Value);
			if (It == Vec.end())
			{
				return INDEX_NONE;
			}

			return *It;
		};


		const int32 ClosestSampleRate = GetClosests(Info.sampleRates, TargetSampleRate);
		if (ClosestSampleRate != INDEX_NONE)
		{
			RecordingSampleRate = ClosestSampleRate;
		}
		else
		{
			UE_LOG(LogUETensorVox, Error, TEXT("Nearest sample rate not found"));
			return false;
		}
	}


	if (RecordingSampleRate < 0)
	{
		UE_LOG(LogUETensorVox, Warning, TEXT("Invalid sample rate provided %i."), TargetSampleRate);
		return false;
	}


	RawRecordingBlocks.Empty();
	NumOverflowsDetected = 0;
	// Publish to the mic input thread that we're ready to record...
	bRecording = true;

	StreamParams.deviceId = ADCInstance.getDefaultInputDevice(); // Only use the default input device for now
	StreamParams.nChannels = 1;
	StreamParams.firstChannel = 0;
	uint32 BufferFrames = FMath::Max(RecordingBlockSize, 256);


	UE_LOG(LogUETensorVox, Log,
	       TEXT("Started microphone recording at %d hz sample rate, %d channels, and %d frame size."),
	       RecordingSampleRate, StreamParams.nChannels, BufferFrames);

	// RtAudio uses exceptions for error handling... 
	try
	{
		// Open up new audio stream
		ADCInstance.openStream(nullptr, &StreamParams, RTAUDIO_SINT16, RecordingSampleRate, &BufferFrames, &OnAudioCaptureCallback,
		                       this);
	}
	catch (RtAudioError& e)
	{
		FString ErrorMessage = FString(e.what());
		bError = true;
		UE_LOG(LogUETensorVox, Error, TEXT("Failed to open the mic capture device: %s"), *ErrorMessage);
		return false;
	}
	catch (const std::exception& e)
	{
		FString ErrorMessage = FString(e.what());
		bError = true;
		UE_LOG(LogUETensorVox, Error, TEXT("Failed to open the mic capture device: %s"), *ErrorMessage);
		return false;
	}
	catch (...)
	{
		UE_LOG(LogUETensorVox, Error, TEXT("Failed to open the mic capture device: unknown error"));
		bError = true;
		return false;
	}

	ADCInstance.startStream();
	return true;
#endif
	return false;
}

// TArray<int16> FDeepSpeechMicrophoneRecorder::DownmixStereoToMono(const TArray<int16>& FirstChannel, const TArray<int16>& SecondChannel)
// {
// 	TArray<int16> OutMixed;
// 	check(FirstChannel.Num() == SecondChannel.Num());
// 	OutMixed.SetNumZeroed(FirstChannel.Num());
// 	for (int32 FrameIndex = 0; FrameIndex < FirstChannel.Num(); FrameIndex++)
// 	{
// 		OutMixed[FrameIndex] = ( (FirstChannel[FrameIndex] + SecondChannel[FrameIndex]) / 2.0f);
// 	}
//
// 	return OutMixed;
// }


static void SampleRateConvert(float CurrentSR, float TargetSR, int32 NumChannels, const TArray<int16>& InSamples,
                              int32 NumSamplesToConvert, TArray<int16>& OutConverted)
{
	int32 NumInputSamples = InSamples.Num();
	int32 NumOutputSamples = NumInputSamples * TargetSR / CurrentSR;

	OutConverted.Reset(NumOutputSamples);

	float SrFactor = (double)CurrentSR / TargetSR;
	float CurrentFrameIndexInterpolated = 0.0f;
	checkf(NumSamplesToConvert <= InSamples.Num(), TEXT("Desample frame length mismatch! NumSamplesToConvert %i, InSamples Num %i"),
	       NumSamplesToConvert, InSamples.Num());
	int32 NumFramesToConvert = NumSamplesToConvert / NumChannels;
	int32 CurrentFrameIndex = 0;

	for (;;)
	{
		int32 NextFrameIndex = CurrentFrameIndex + 1;
		if (CurrentFrameIndex >= NumFramesToConvert || NextFrameIndex >= NumFramesToConvert)
		{
			break;
		}

		for (int32 Channel = 0; Channel < NumChannels; ++Channel)
		{
			int32 CurrentSampleIndex = CurrentFrameIndex * NumChannels + Channel;
			int32 NextSampleIndex = CurrentSampleIndex + NumChannels;

			int16 CurrentSampleValue = InSamples[CurrentSampleIndex];
			int16 NextSampleValue = InSamples[NextSampleIndex];

			int16 NewSampleValue = FMath::Lerp(CurrentSampleValue, NextSampleValue, CurrentFrameIndexInterpolated);

			OutConverted.Add(NewSampleValue);
		}

		CurrentFrameIndexInterpolated += SrFactor;

		// Wrap the interpolated frame between 0.0 and 1.0 to maintain float precision
		while (CurrentFrameIndexInterpolated >= 1.0f)
		{
			CurrentFrameIndexInterpolated -= 1.0f;

			// Every time it wraps, we increment the frame index
			++CurrentFrameIndex;
		}
	}
}

TArray<FDeinterleavedAudio> FDeepSpeechMicrophoneRecorder::ProcessSamples(
	TArray<int16> InSamples)
{
	const int32 InputChannelCount = NumInputChannels.GetValue();
	TArray<FDeinterleavedAudio> OutResampled;

	if (InSamples.Num() > 0)
	{
		if (RecordingSampleRate != TargetSampleRate)
		{
			TArray<int16> Resampled;
			SampleRateConvert((float)RecordingSampleRate, (float)TargetSampleRate, InputChannelCount, InSamples, InSamples.Num(),
			                  Resampled);
			InSamples = Resampled;
		}


		FDeinterleavedAudio SingleChannel;
		SingleChannel.PCMData = InSamples;

		OutResampled.Add(SingleChannel);
	}
	return OutResampled;
}

USoundWave* FDeepSpeechMicrophoneRecorder::SaveAsWavMono(const TAlignedSignedInt16Array& Samples, const FString& Path,
                                                         const FString& AssetName, const int16& RecordedSampleRate)
{
	if (Samples.Num() > 0)
	{
		const FString& PackageName = Path / AssetName;
		UPackage* Package = CreatePackage(*PackageName);

		const int32 NumBytes = Samples.Num() * Samples.GetTypeSize();
		TArray<uint8> RawWaveData;
		SerializeWaveFile(RawWaveData, (uint8*)Samples.GetData(), NumBytes, 1, RecordedSampleRate);

		// Check to see if a sound wave already exists at this location
		USoundWave* ExistingSoundWave = FindObject<USoundWave>(Package, *AssetName);

		// See if it's currently being played right now
		TArray<UAudioComponent*> ComponentsToRestart;
		FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager();
		if (AudioDeviceManager && ExistingSoundWave)
		{
			AudioDeviceManager->StopSoundsUsingResource(ExistingSoundWave, &ComponentsToRestart);
		}

		// Create a new sound wave object
		USoundWave* NewSoundWave;
		if (ExistingSoundWave)
		{
			NewSoundWave = ExistingSoundWave;
			NewSoundWave->FreeResources();
		}
		else
		{
			NewSoundWave = NewObject<USoundWave>(Package, *AssetName, RF_Public | RF_Standalone);
		}

		// Compressed data is now out of date.
		NewSoundWave->InvalidateCompressedData(true, false);

		// Copy the raw wave data file to the sound wave for storage. Will allow the recording to be exported.
		FSharedBuffer UpdatedBuffed = FSharedBuffer::Clone(RawWaveData.GetData(), RawWaveData.Num());
		NewSoundWave->RawData.UpdatePayload(UpdatedBuffed);


		if (NewSoundWave)
		{
			// Copy the recorded data to the sound wave so we can quickly preview it
			NewSoundWave->RawPCMDataSize = NumBytes;
			NewSoundWave->RawPCMData = (uint8*)FMemory::Malloc(NewSoundWave->RawPCMDataSize);
			FMemory::Memcpy(NewSoundWave->RawPCMData, Samples.GetData(), NumBytes);

			// Calculate the duration of the sound wave
			// Note: We use the NumInputChannels for duration calculation since NumChannelsToSerialize may be 1 channel while NumInputChannels is 2 for the "split stereo" feature.
			NewSoundWave->Duration = (float)Samples.Num() / (float)RecordedSampleRate;
			NewSoundWave->SetSampleRate((float)RecordedSampleRate);
			NewSoundWave->NumChannels = 1;


			//GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, NewSoundWave);

			FAssetRegistryModule::AssetCreated(NewSoundWave);
			NewSoundWave->MarkPackageDirty();

			// Restart any audio components if they need to be restarted
			for (int32 ComponentIndex = 0; ComponentIndex < ComponentsToRestart.Num(); ++ComponentIndex)
			{
				ComponentsToRestart[ComponentIndex]->Play();
			}


			return NewSoundWave;
		}
	}
	return nullptr;
}

void FDeepSpeechMicrophoneRecorder::StopRecording()
{
#if TENSORVOX_VALID_PLATFORM
	UE_LOG(LogUETensorVox, Log, TEXT("Stopped recording microphone."));

	if (bRecording)
	{
		bRecording = false;
		ADCInstance.stopStream();
		ADCInstance.closeStream();
	}
#endif
}

int32 FDeepSpeechMicrophoneRecorder::OnAudioCapture(void* InBuffer, uint32 InBufferFrames, double StreamTime, bool bOverflow)
{
	if (bRecording)
	{
		if (bOverflow)
		{
			++NumOverflowsDetected;
		}

		TAlignedSignedInt16Array Block;
		Block.Append((int16*)InBuffer, InBufferFrames);
		RawRecordingBlocks.Enqueue(FDeinterleavedAudio{Block});
		return 0;
	}

	return 1;
}
