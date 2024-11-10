#ifndef _STACKAUDIOLEVELSTAB_H_INCLUDED
#define _STACKAUDIOLEVELSTAB_H_INCLUDED

// Includes:
#include "StackCue.h"
#include <gtk/gtk.h>

// Typedefs:
typedef StackProperty*(*salt_get_volume_property_t)(StackCue*, size_t, bool);
typedef StackProperty*(*salt_get_crosspoint_property_t)(StackCue*, size_t, size_t, bool);

typedef struct StackAudioLevelsTab
{
	GtkWidget *root;
	GtkWidget *master_scale;
	GtkWidget *master_entry;
	GtkWidget **channel_scales;
	GtkWidget **channel_entries;
	StackCue *cue;
	salt_get_volume_property_t get_volume_property;
	salt_get_crosspoint_property_t get_crosspoint_property;
	bool allow_nulls;
} StackAudioLevelsTab;

// Definition:
#define STACK_AUDIO_LEVELS_TAB(_t) ((StackAudioLevelsTab*)(_t))

// Functions:
StackAudioLevelsTab *stack_audio_levels_tab_new(StackCue *cue, salt_get_volume_property_t get_volume_property, salt_get_crosspoint_property_t get_crosspoint_property);
void stack_audio_levels_tab_destroy(StackAudioLevelsTab *tab);
void stack_audio_levels_tab_populate(StackAudioLevelsTab *tab, size_t input_channels, size_t output_channels, bool show_crosspoints, bool affect_live_checked, GCallback affect_live_cb);

#endif
