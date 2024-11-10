#ifndef _STACKAUDIOCUE_H_INCLUDED
#define _STACKAUDIOCUE_H_INCLUDED

// Includes:
#include "StackCue.h"
#include "StackAudioFile.h"
#include "StackAudioPreview.h"
#include "StackResampler.h"
#include "StackAudioLevelsTab.h"
#include <thread>

// An audio cue
struct StackAudioCue
{
	// Superclass
	StackCue super;

	// The short (i.e. not fully qualified) file to playback
	gchar *short_filename;

	// The media tab
	GtkWidget *media_tab;

	// The levels tab
	StackAudioLevelsTab *levels_tab;

	// The levels
	GtkWidget *master_scale;
	GtkWidget *master_value;
	GtkWidget **channel_scales;
	GtkWidget **channel_values;
	bool affect_live;

	// The currently open file
	StackAudioFile *playback_file;

	// The resampler to resample from file-rate to device-rate
	StackResampler *resampler;

	// Audio Preview: The audio preview widget
	StackAudioPreview *preview_widget;

	// The current loop count
	int32_t playback_loops;

	// Buffer for playback - this is the temporary store for data either read
	// from the file or output from the resampler, which we then use to scale
	// and sum with crosspoint information
	float *playback_buffer;

	// Size of playback buffer in frames
	size_t playback_buffer_frames;
};

// Functions: Audio cue functions
void stack_audio_cue_register();
bool stack_audio_cue_set_file(StackAudioCue *cue, const char *uri);

// Defines:
#define STACK_AUDIO_CUE(_c) ((StackAudioCue*)(_c))

#endif

