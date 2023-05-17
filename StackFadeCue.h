#ifndef _STACKFADECUE_H_INCLUDED
#define _STACKFADECUE_H_INCLUDED

// Includes:
#include "StackCue.h"

enum StackFadeProfile
{
	STACK_FADE_PROFILE_LINEAR,
	STACK_FADE_PROFILE_QUAD,
	STACK_FADE_PROFILE_EXP,
	STACK_FADE_PROFILE_INVEXP,
};

// An audio cue
struct StackFadeCue
{
	// Superclass
	StackCue super;

	// The GtkBuilder instance
	GtkBuilder *builder;

	// The fade tab
	GtkWidget *fade_tab;

	// The volume of the target when the cue started
	double playback_start_target_volume;

	// A string of the target cue ID
	char target_cue_id_string[32];
};

// Functions: Audio cue functions
void stack_fade_cue_register();
void stack_fade_cue_set_target(StackFadeCue *cue, StackCue *target);

// Defines:
#define STACK_FADE_CUE(_c) ((StackFadeCue*)(_c))

#endif

