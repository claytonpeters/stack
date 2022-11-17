#ifndef _STACKPULSEAUDIODEVICE_H_INCLUDED
#define _STACKPULSEAUDIODEVICE_H_INCLUDED

// Includes:
#include "StackAudioDevice.h"
#include <pulse/pulseaudio.h>
#include "semaphore.h"

// Structure
typedef struct StackPulseAudioDevice
{
	// Super class
	StackAudioDevice super;

	// The PulseAudio stream
	pa_stream *stream;

	// Synchronisation semaphore
	semaphore sync_semaphore;
} StackPulseAudioDevice;

// Functions: PulseAudio device functions
void stack_pulse_audio_device_register();

// Defines:
#define STACK_PULSE_AUDIO_DEVICE(_d) ((StackPulseAudioDevice*)(_d))

#endif

