#ifndef _STACKGROUPCUE_H_INCLUDED
#define _STACKGROUPCUE_H_INCLUDED

// Includes:
#include "StackCue.h"

// Supported actions
enum StackGroupCueAction
{
	STACK_GROUP_CUE_ENTER = 0,
	STACK_GROUP_CUE_TRIGGER_ALL = 1,
	STACK_GROUP_CUE_TRIGGER_RANDOM = 2,
	STACK_GROUP_CUE_TRIGGER_PLAYLIST = 3,
	STACK_GROUP_CUE_TRIGGER_SHUFFLED_PLAYLIST = 4
};

// A group cue
struct StackGroupCue
{
	// Superclass
	StackCue super;

	// The array of cues
	StackCueStdList *cues;

	// The group tab
	GtkWidget *group_tab;

	// Tracking of played cue UIDs
	std::list<cue_uid_t> *played_cues;

	// Stop should only stop child cues if not called during a pulse (i.e.
	// only when a user asks). This allows zero-length cues to work properly
	bool in_pulse;
};

// Functions: Group cue functions
void stack_group_cue_register();

// Defines:
#define STACK_GROUP_CUE(_c) ((StackGroupCue*)(_c))

#endif
