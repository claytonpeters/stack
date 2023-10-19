#ifndef _STACKGROUPCUE_H_INCLUDED
#define _STACKGROUPCUE_H_INCLUDED

// Includes:
#include "StackCue.h"

// An audio cue
struct StackGroupCue
{
	// Superclass
	StackCue super;

	// The array of cues (this is a std::list internally)
	void *cues;

	// The GtkBuilder instance
	GtkBuilder *builder;

	// The group tab
	GtkWidget *group_tab;
};

// Functions: Group cue functions
void stack_group_cue_register();

// Defines:
#define STACK_GROUP_CUE(_c) ((StackGroupCue*)(_c))

#endif
