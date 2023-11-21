#ifndef _STACKEXECCUE_H_INCLUDED
#define _STACKEXECCUE_H_INCLUDED

// Includes:
#include "StackCue.h"

// Supported file formats
enum StackExecCueAction
{
	STACK_EXEC_CUE_PLAY = 0,
	STACK_EXEC_CUE_PAUSE = 1,
	STACK_EXEC_CUE_STOP = 2,
};

// An audio cue
struct StackExecCue
{
	// Superclass
	StackCue super;

	// The exec tab
	GtkWidget *exec_tab;
};

// Functions: Exec cue functions
void stack_exec_cue_register();

// Defines:
#define STACK_EXEC_CUE(_c) ((StackExecCue*)(_c))

#endif

