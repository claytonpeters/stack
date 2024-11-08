#ifndef _STACKPULSEAUDIODEVICE_H_INCLUDED
#define _STACKPULSEAUDIODEVICE_H_INCLUDED

// Includes:
#include "StackAudioDevice.h"
#include <pulse/pulseaudio.h>
#include "semaphore.h"
#include <thread>

// Structure
struct StackPulseAudioDevice
{
	// Super class
	StackAudioDevice super;

	// The PulseAudio stream
	pa_stream *stream;

	// The format of the stream
	pa_sample_format_t format;

	// Synchronisation semaphore
	semaphore sync_semaphore;

	// Output loop thread
	std::thread output_thread;

	// Kill flag for the thread
	bool thread_running;
};

// Functions: PulseAudio device functions
void stack_pulse_audio_device_register();

// Defines:
#define STACK_PULSE_AUDIO_DEVICE(_d) ((StackPulseAudioDevice*)(_d))

#endif

