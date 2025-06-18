#ifndef _STACKALSAMIDIDEVICE_H_INCLUDED
#define _STACKALSAMIDIDEVICE_H_INCLUDED

// Includes:
#include "StackMidiDevice.h"
#include <alsa/asoundlib.h>
#include <thread>
#include <mutex>

// Structure
struct StackAlsaMidiDevice
{
	// Super class
	StackMidiDevice super;

	// The ALSA devices
	snd_rawmidi_t *handle_in;
	snd_rawmidi_t *handle_out;

	// Read thread
	std::thread read_thread;

	// Write mutex (so we don't accidentally interleave messages)
	std::mutex write_mutex;

	// Whether we're sync'd on a status code
	bool synced;

	// Keep track of the last status byte so that repeated events of the same
	// type can be decoded properly when status bytes are not sent
	uint8_t last_status_byte;

	// Kill flag for the thread
	bool thread_running;
};

// Functions: Register the device
void stack_alsa_midi_device_register();

// Defines:
#define STACK_ALSA_MIDI_DEVICE(_d) ((StackAlsaMidiDevice*)(_d))

#endif

