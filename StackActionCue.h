#ifndef _STACKACTIONCUE_H_INCLUDED
#define _STACKACTIONCUE_H_INCLUDED

// Includes:
#include "StackCue.h"

// Supported file formats
enum StackActionCueAction
{
	STACK_ACTION_CUE_PLAY = 0,
	STACK_ACTION_CUE_PAUSE = 1,
	STACK_ACTION_CUE_STOP = 2,
};

// An audio cue
struct StackActionCue
{
	// Superclass
	StackCue super;

	// The action tab
	GtkWidget *action_tab;

	// A string of the target cue ID
	char target_cue_id_string[32];
};

// Functions: Action cue functions
void stack_action_cue_register();

// Defines:
#define STACK_ACTION_CUE(_c) ((StackActionCue*)(_c))

#endif

