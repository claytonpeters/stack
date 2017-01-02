#ifndef _STACKACTIONCUE_H_INCLUDED
#define _STACKACTIONCUE_H_INCLUDED

// Includes:
#include "StackCue.h"

// Supported file formats
typedef enum StackActionCueAction
{
	STACK_ACTION_CUE_PLAY = 0,
	STACK_ACTION_CUE_PAUSE = 1,
	STACK_ACTION_CUE_STOP = 2,
} StackActionCueAction;

// An audio cue
typedef struct StackActionCue
{
	// Superclass
	StackCue super;
		
	// The target cue
	cue_uid_t target;

	// What to do to the target
	StackActionCueAction action;

	// The GtkBuilder instance
	GtkBuilder *builder;
	
	// The action tab
	GtkWidget *action_tab;

} StackActionCue;

// Functions: Action cue functions
void stack_action_cue_register();

// Defines:
#define STACK_ACTION_CUE(_c) ((StackActionCue*)(_c))

#endif

