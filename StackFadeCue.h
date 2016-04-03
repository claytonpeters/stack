#ifndef _STACKFADECUE_H_INCLUDED
#define _STACKFADECUE_H_INCLUDED

// Includes:
#include "StackCue.h"
#include "StackAudioCue.h"

typedef enum StackFadeProfile
{
	STACK_FADE_PROFILE_LINEAR,
	STACK_FADE_PROFILE_QUAD,
	STACK_FADE_PROFILE_EXP,
	STACK_FADE_PROFILE_INVEXP,
} StackFadeProfile;

// An audio cue
typedef struct StackFadeCue
{
	// Superclass
	StackCue super;
	
	// The target cue
	cue_uid_t target;	
	
	// Post-fade-out volume, in dB (relative to target cue)
	double target_volume;
	
	// Fade profile
	StackFadeProfile fade_profile;
	
	// Whether to stop the target cue after the fade
	bool stop_target;
	
	// The GtkBuilder instance
	GtkBuilder *builder;
	
	// The fade tab
	GtkWidget *fade_tab;
	
	// The volume of the target when the cue started
	float playback_start_target_volume;
} StackFadeCue;

// Functions: Audio cue functions
void stack_fade_cue_register();

// Defines:
#define STACK_FADE_CUE(_c) ((StackFadeCue*)(_c))

#endif

