#ifndef _STACKAUDIOCUE_H_INCLUDED
#define _STACKAUDIOCUE_H_INCLUDED

// Includes:
#include "StackCue.h"
#include "StackAudioFile.h"
#include "StackAudioPreview.h"
#include "StackResampler.h"
#include <thread>

// An audio cue
struct StackAudioCue
{
	// Superclass
	StackCue super;

	// The file to playback
	char *file;
	gchar *short_filename;

	// Media start time (e.g. this much time is skipped at the start of the
	// media file)
	stack_time_t media_start_time;

	// Media end time (e.g. from this time forward is ignored at the end of the
	// media file)
	stack_time_t media_end_time;

	// The number of times to play the cue on a single activation. Zero or less
	// is infinite
	int32_t loops;

	// Post-fade-in, pre-fade-out volume, in dB
	double play_volume;

	// The GtkBuilder instance
	GtkBuilder *builder;

	// The media tab
	GtkWidget *media_tab;

	// The current volume of the cue
	double playback_live_volume;

	// The current number of playback loops
	int32_t playback_loops;

	// The currently open file
	StackAudioFile *playback_file;

	// The resampler to resample from file-rate to device-rate
	StackResampler *resampler;

	// Audio Preview: The audio preview widget
	StackAudioPreview *preview_widget;
};

// Functions: Audio cue functions
void stack_audio_cue_register();
bool stack_audio_cue_set_file(StackAudioCue *cue, const char *uri);

// Defines:
#define STACK_AUDIO_CUE(_c) ((StackAudioCue*)(_c))

#endif

