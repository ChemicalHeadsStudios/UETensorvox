
#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
THIRD_PARTY_INCLUDES_START
#include "RtAudio.h"
THIRD_PARTY_INCLUDES_END


// Buffers to de-interleave recorded audio
UETENSORVOX_API  struct FDeinterleavedAudio
{
	TArray<int16> PCMData;
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
	void StartRecording(const int32 RecordingBlockSize = 1024);
	// Stops recording if the recording manager is recording. If not recording but has recorded data (due to set duration), it will just return the generated USoundWave.
	void StopRecording();

	TArray<FDeinterleavedAudio> DownsampleAndSeperateChannels(TArray<int16> InSamples);
	
	static TArray<int16> DownmixStereoToMono(const TArray<int16>& FirstChannel, const TArray<int16>& SecondChannel);

	// Called by RtAudio when a new audio buffer is ready to be supplied.
	int32 OnAudioCapture(void* InBuffer, uint32 InBufferFrames, double StreamTime, bool bOverflow);


	
	TQueue<FDeinterleavedAudio> RawRecordingBlocks;

private:
	RtAudio ADCInstance;
	// Stream parameters to initialize the ADCInstance
	RtAudio::StreamParameters StreamParams;
protected:

	// The sample rate used in the recording
	float RecordingSampleRate;

	bool bSplitChannels;
	// Number of overflows detected while recording
	int32 NumOverflowsDetected;

	// Whether or not we have an error
	uint32 bError : 1;

	// Whether or not the manager is actively recording.

	// Num input channels
	FThreadSafeCounter NumInputChannels;

	FThreadSafeBool bRecording;
};
