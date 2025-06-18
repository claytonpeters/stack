#ifndef _STACKMIDICUE_H_INCLUDED
#define _STACKMIDICUE_H_INCLUDED

// Includes:
#include "StackCue.h"

// StackMidiCue is a cue type that fires a midi event
struct StackMidiCue
{
	// Superclass
	StackCue super;

	char field_event[32];
	char field_channel[5];
	char field_param1[5];
	char field_param2[5];

	// The MIDI tab
	GtkWidget *midi_tab;
};

// Functions: Midi cue functions
void stack_midi_cue_register();

// Defines:
#define STACK_MIDI_CUE(_c) ((StackMidiCue*)(_c))

#endif

