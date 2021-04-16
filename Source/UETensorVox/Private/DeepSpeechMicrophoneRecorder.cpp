#include "DeepSpeechMicrophoneRecorder.h"

#include "Misc/ScopeLock.h"
#include "DSP/Dsp.h"
#include "UETensorVox.h"


/**
* Callback Function For the Microphone Capture for RtAudio
*/
static int32 OnAudioCaptureCallback(void* OutBuffer, void* InBuffer, uint32 InBufferFrames, double StreamTime,
                                    RtAudioStreamStatus AudioStreamStatus, void* InUserData)
{
	// Check for stream overflow (i.e. we're feeding audio faster than we're consuming)


	// Cast the user data to the singleton mic recorder
	FDeepSpeechMicrophoneRecorder* AudioRecordingManager = (FDeepSpeechMicrophoneRecorder*)InUserData;

	// Call the mic capture callback function
	return AudioRecordingManager->OnAudioCapture(InBuffer, InBufferFrames, StreamTime, AudioStreamStatus == RTAUDIO_INPUT_OVERFLOW);
}

/**
* FDeepSpeechMicrophoneRecorder Implementation
*/

FDeepSpeechMicrophoneRecorder::FDeepSpeechMicrophoneRecorder()
{
	RecordingSampleRate = 16000.0f;
	bRecording = false;
	bSplitChannels = false;
	bError = false;
	NumInputChannels.Set(1);
	NumOverflowsDetected = 0;
}

FDeepSpeechMicrophoneRecorder::~FDeepSpeechMicrophoneRecorder()
{
	if (ADCInstance.isStreamOpen())
	{
		ADCInstance.abortStream();
	}
}

void FDeepSpeechMicrophoneRecorder::StartRecording(int32 RecordingBlockSize)
{
	if (bError)
	{
		return;
	}

	if(bRecording)
	{
		TArray<FDeinterleavedAudio> Temp;
		// Stop any recordings currently going on (if there is one)
		StopRecording();
	}

	
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
			return;
		}
	}

	UE_LOG(LogUETensorVox, Log, TEXT("Starting mic recording."));


	// Get the default mic input device info
	RtAudio::DeviceInfo Info = ADCInstance.getDeviceInfo(StreamParams.deviceId);
	RecordingSampleRate = Info.preferredSampleRate;
	NumInputChannels.Set(Info.inputChannels);

	const int32 InputChannelCount = NumInputChannels.GetValue();
	
	// Only support mono and stereo mic inputs for now...
	if (InputChannelCount != 1 && InputChannelCount!= 2)
	{
		UE_LOG(LogUETensorVox, Warning, TEXT("Audio recording only supports mono and stereo mic input."));
		return;
	}

	RawRecordingBlocks.Empty();

	bSplitChannels = InputChannelCount > 1;

	// If we have more than 2 channels, we're going to force splitting up the assets since we don't propertly support multi-channel USoundWave assets
	if (InputChannelCount > 2)
	{
		UE_LOG(LogUETensorVox, Error, TEXT("Cannot record microphone, not single channel"));
		return;
	}

	NumOverflowsDetected = 0;

	// Publish to the mic input thread that we're ready to record...
	bRecording = true;

	StreamParams.deviceId = ADCInstance.getDefaultInputDevice(); // Only use the default input device for now

	StreamParams.nChannels = InputChannelCount;
	StreamParams.firstChannel = 0;

	uint32 BufferFrames = FMath::Max(RecordingBlockSize, 256);

	UE_LOG(LogUETensorVox, Log,
	       TEXT("Initialized mic recording manager at %d hz sample rate, %d channels, and %d Recording Block Size"),
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
	}
	catch (const std::exception& e)
	{
		bError = true;
		FString ErrorMessage = FString(e.what());
		UE_LOG(LogUETensorVox, Error, TEXT("Failed to open the mic capture device: %s"), *ErrorMessage);
	}
	catch (...)
	{
		UE_LOG(LogUETensorVox, Error, TEXT("Failed to open the mic capture device: unknown error"));
		bError = true;
	}

	ADCInstance.startStream();
}

TArray<int16> FDeepSpeechMicrophoneRecorder::DownmixStereoToMono(const TArray<int16>& FirstChannel, const TArray<int16>& SecondChannel)
{
	TArray<int16> OutMixed;
	check(FirstChannel.Num() == SecondChannel.Num());
	OutMixed.SetNumZeroed(FirstChannel.Num());
	for (int32 FrameIndex = 0; FrameIndex < FirstChannel.Num(); FrameIndex++)
	{
		OutMixed[FrameIndex] = (int16)(((float)FirstChannel[FrameIndex] + (float)SecondChannel[FrameIndex]) / 2.0f);
	}

	return OutMixed;
}


static void SampleRateConvert(float CurrentSR, float TargetSR, int32 NumChannels, const TArray<int16>& InSamples,
                              int32 NumSamplesToConvert, TArray<int16>& OutConverted)
{
	int32 NumInputSamples = InSamples.Num();
	int32 NumOutputSamples = NumInputSamples * TargetSR / CurrentSR;

	OutConverted.Reset(NumOutputSamples);

	float SrFactor = (double)CurrentSR / TargetSR;
	float CurrentFrameIndexInterpolated = 0.0f;
	checkf(NumSamplesToConvert <= InSamples.Num(), TEXT("Desample frame length mismatch! NumSamplesToConvert %i, InSamples Num %i"), NumSamplesToConvert, InSamples.Num());
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

TArray<FDeinterleavedAudio> FDeepSpeechMicrophoneRecorder::DownsampleAndSeperateChannels(
	TArray<int16> InSamples)
{
	const int32 InputChannelCount = NumInputChannels.GetValue();
	TArray<FDeinterleavedAudio> OutResampled;
	if (InSamples.Num() > 0)
	{
		// If our sample rate isn't 16000, then we need to do a SRC
		if (RecordingSampleRate != 16000.0f)
		{
			// UE_LOG(LogUETensorVox, Log, TEXT("Converting sample rate from %d hz to 16000 hz."), (int32)RecordingSampleRate);

			TArray<int16> Resampled;
			SampleRateConvert(RecordingSampleRate, (float)16000, InputChannelCount, InSamples, InSamples.Num(), Resampled);
			InSamples = Resampled;
		}

		if (bSplitChannels)
		{
			// De-interleaved buffer size will be the number of frames of audio
			int32 NumFrames = InSamples.Num() / InputChannelCount;

			// Reset our deinterleaved audio buffer
			InSamples.Reset(InputChannelCount);

			// Get ptr to the interleaved buffer for speed in non-release builds
			int16* InterleavedBufferPtr = InSamples.GetData();

			// For every input channel, create a new buffer
			for (int32 Channel = 0; Channel < InputChannelCount; ++Channel)
			{
				// Prepare a new deinterleaved buffer
				OutResampled.Add(FDeinterleavedAudio());
				FDeinterleavedAudio& DeinterleavedChannelAudio = OutResampled[Channel];

				DeinterleavedChannelAudio.PCMData.Reset();
				DeinterleavedChannelAudio.PCMData.AddUninitialized(NumFrames);

				// Get a ptr to the buffer for speed
				int16* DeinterleavedBufferPtr = DeinterleavedChannelAudio.PCMData.GetData();

				// Copy every N channel to the deinterleaved buffer, starting with the current channel
				int32 CurrentSample = Channel;
				for (int32 Frame = 0; Frame < NumFrames; ++Frame)
				{
					// Simply copy the interleaved value to the deinterleaved value
					DeinterleavedBufferPtr[Frame] = InterleavedBufferPtr[CurrentSample];

					// Increment the stride according the num channels
					CurrentSample += InputChannelCount;
				}
			}
		}
		else
		{
			FDeinterleavedAudio SingleChannel;
			SingleChannel.PCMData = InSamples;

			OutResampled.Add(SingleChannel);
		}
	}
	return OutResampled;
}

void FDeepSpeechMicrophoneRecorder::StopRecording()
{
	UE_LOG(LogUETensorVox, Log, TEXT("Stopping mic recording"));

	if (bRecording)
	{
		bRecording = false;
		ADCInstance.stopStream();
		ADCInstance.closeStream();
	}
}

int32 FDeepSpeechMicrophoneRecorder::OnAudioCapture(void* InBuffer, uint32 InBufferFrames, double StreamTime, bool bOverflow)
{
	if (bRecording)
	{
		if (bOverflow)
		{
			++NumOverflowsDetected;
		}
		
		TArray<int16> Block;
		Block.Append((int16*)InBuffer, InBufferFrames * NumInputChannels.GetValue());
		RawRecordingBlocks.Enqueue(FDeinterleavedAudio{Block});
		return 0;
	}

	return 1;
}
