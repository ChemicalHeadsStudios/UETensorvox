﻿
#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

#include "UETensorVox.h"

#if TENSORVOX_VALID_PLATFORM 
THIRD_PARTY_INCLUDES_START
#include "RtAudio.h"
THIRD_PARTY_INCLUDES_END
#endif


// Buffers to de-interleave recorded audio
UETENSORVOX_API struct FDeinterleavedAudio
{
	TAlignedSignedInt16Array PCMData;
};
/**
 * FDeepSpeechMicrophoneRecorder
 * Singleton Mic Recording Manager -- generates recordings, stores the recorded data and plays them back
 */
UETENSORVOX_API class FDeepSpeechMicrophoneRecorder
{
public:
	// Private Constructor
	FDeepSpeechMicrophoneRecorder();
	// Private Destructor
	~FDeepSpeechMicrophoneRecorder();
	// Starts a new recording with the given name and optional duration. 
	// If set to -1.0f, a duration won't be used and the recording length will be determined by StopRecording().
	bool StartRecording(int32 InTargetSampleRate = 16000, int32 RecordingBlockSize = 1024);
	// Stops recording if the recording manager is recording. If not recording but has recorded data (due to set duration), it will just return the generated USoundWave.
	void StopRecording();

	// Called by RtAudio when a new audio buffer is ready to be supplied.
	int32 OnAudioCapture(void* InBuffer, uint32 InBufferFrames, double StreamTime, bool bOverflow);
	TArray<FDeinterleavedAudio> ProcessSamples(TArray<int16> InSamples);

	/**
	 * Save samples with the recorder's settings.
	 */
	static USoundWave* SaveAsWavMono(const TAlignedSignedInt16Array& Samples, const FString& Path, const FString& AssetName, const int16& RecordedSampleRate);

	// static TArray<int16> DownmixStereoToMono(const TArray<int16>& FirstChannel, const TArray<int16>& SecondChannel);
public:

	TQueue<FDeinterleavedAudio> RawRecordingBlocks;
	int32 RecordingSampleRate;

private:
#if TENSORVOX_VALID_PLATFORM
	RtAudio ADCInstance;
	// Stream parameters to initialize the ADCInstance
	RtAudio::StreamParameters StreamParams;
#endif
protected:

	int32 TargetSampleRate;
	
	int32 NumOverflowsDetected;
	uint32 bError : 1;

	bool bSplitChannels;

	FThreadSafeCounter NumInputChannels;
	FThreadSafeBool bRecording;
};
