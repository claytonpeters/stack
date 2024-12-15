#ifndef _STACKPIPEWIREAUDIODEVICE_H_INCLUDED
#define _STACKPIPEWIREAUDIODEVICE_H_INCLUDED

// Includes:
#include "StackAudioDevice.h"
#include <thread>
#include <pipewire/pipewire.h>

// Structure
struct StackPipeWireAudioDevice
{
	// Super class
	StackAudioDevice super;

	// The PipeWire stream
	pw_stream *stream;
};

// Functions: Register the device
void stack_pipewire_audio_device_register();

// Defines:
#define STACK_PIPEWIRE_AUDIO_DEVICE(_d) ((StackPipeWireAudioDevice*)(_d))

#endif

