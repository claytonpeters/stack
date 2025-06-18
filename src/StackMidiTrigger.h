#ifndef STACKMIDITRIGGER_H_INCLUDED
#define STACKMIDITRIGGER_H_INCLUDED

// Includes:
#include "StackTrigger.h"

// Whether to trigger the event on any value of parameter one
#define STACK_MIDI_TRIGGER_ANY_PARAM1  1

// Whether to trigger the event on any value of parameter two
#define STACK_MIDI_TRIGGER_ANY_PARAM2  2

// Whether to trigger the event on any channel
#define STACK_MIDI_TRIGGER_ANY_CHANNEL 4

// Configured flag (i.e. are the patch/event/params valid)
#define STACK_MIDI_TRIGGER_CONFIGURED  8

struct StackMidiTrigger
{
	// Superclass
	StackTrigger super;

	// The description for the trigger
	char *description;

	// The patch name of the MIDI device in the cue list to read events from
	char *midi_patch;

	// The event to trigger on
	StackMidiShortEvent event;

	// The receiver interface
	StackMidiEventReceiver *receiver;

	// The thread we listen for events on
	std::thread thread;
	bool thread_running;

	// Flags
	uint8_t flags;

	// Buffer for our event text
	char event_text[128];
};

// Defines:
#define STACK_MIDI_TRIGGER(_t) ((StackMidiTrigger*)(_t))

#endif
