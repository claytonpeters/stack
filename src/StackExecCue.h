#ifndef _STACKEXECCUE_H_INCLUDED
#define _STACKEXECCUE_H_INCLUDED

// Includes:
#include "StackCue.h"

// StackExecCue is a cue type that executes an arbitrary command when triggered
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

