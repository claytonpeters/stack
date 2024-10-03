#ifndef _STACKFADECUE_H_INCLUDED
#define _STACKFADECUE_H_INCLUDED

// Includes:
#include "StackCue.h"
#include "StackAudioLevelsTab.h"

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

	// The fade tab
	GtkWidget *fade_tab;

	// The levels tab
	StackAudioLevelsTab *levels_tab;

	// The volume of the target when the cue started
	double playback_start_master_volume;

	// The volume of each channel on the target when the cue stated
	double *playback_start_channel_volumes;

	// The volume of each crosspoint on the target when the cue started
	double *playback_start_cross_points;

	// A string of the target cue ID
	char target_cue_id_string[32];
};

// Functions: Audio cue functions
void stack_fade_cue_register();
void stack_fade_cue_set_target(StackFadeCue *cue, StackCue *target);

// Defines:
#define STACK_FADE_CUE(_c) ((StackFadeCue*)(_c))

#endif

