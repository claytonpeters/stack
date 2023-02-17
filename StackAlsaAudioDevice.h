#ifndef _STACKALSAAUDIODEVICE_H_INCLUDED
#define _STACKALSAAUDIODEVICE_H_INCLUDED

// Includes:
#include "StackAudioDevice.h"
#include "semaphore.h"
#include <alsa/asoundlib.h>
#include <thread>

// Structure
struct StackAlsaAudioDevice
{
	// Super class
	StackAudioDevice super;

	// The ALSA device
	snd_pcm_t *stream;

	// The output format
	snd_pcm_format_t format;

	// Output loop thread
	std::thread output_thread;

	// Kill flag for the thread
	bool thread_running;
};

// Functions: Register the device
void stack_alsa_audio_device_register();

// Defines:
#define STACK_ALSA_AUDIO_DEVICE(_d) ((StackAlsaAudioDevice*)(_d))

#endif

