#ifndef _STACKOSCCUE_H_INCLUDED
#define _STACKOSCCUE_H_INCLUDED

// Includes:
#include "StackCue.h"

// StackOSC cue is a cue that can send out arbitrary OSC packets for remote
// control of other OSC-supporting software/hardware
struct StackOSCCue
{
	// Superclass
	StackCue super;

	// The OSC tab
	GtkWidget *osc_tab;

	// The UDP socket
	int sock;
};

// Functions: OSC cue functions
void stack_osc_cue_register();

// Defines:
#define STACK_OSC_CUE(_c) ((StackOSCCue*)(_c))

#endif

