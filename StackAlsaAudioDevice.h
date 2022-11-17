#ifndef _STACKALSAAUDIODEVICE_H_INCLUDED
#define _STACKALSAAUDIODEVICE_H_INCLUDED

// Includes:
#include "StackAudioDevice.h"
#include "semaphore.h"
#include <alsa/asoundlib.h>

// Structure
typedef struct StackAlsaAudioDevice
{
	// Super class
	StackAudioDevice super;

	// The ALSA device
	snd_pcm_t *stream;

	// Synchronisation semaphore
	semaphore sync_semaphore;
} StackAlsaAudioDevice;

// Functions: Register the device
void stack_alsa_audio_device_register();

// Defines:
#define STACK_ALSA_AUDIO_DEVICE(_d) ((StackAlsaAudioDevice*)(_d))

#endif

