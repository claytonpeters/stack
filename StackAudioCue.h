#ifndef _STACKAUDIOCUE_H_INCLUDED
#define _STACKAUDIOCUE_H_INCLUDED

// Includes:
#include "StackCue.h"
#include "StackAudioFile.h"
#include "StackResampler.h"
#include <thread>

// An audio cue
typedef struct StackAudioCue
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

	// Post-fade-in, pre-fade-out volume, in dB
	double play_volume;

	// The GtkBuilder instance
	GtkBuilder *builder;

	// The media tab
	GtkWidget *media_tab;

	// The current volume of the cue
	double playback_live_volume;

	// The currently open file
	StackAudioFile *playback_file;

	// The resampler to resample from file-rate to device-rate
	StackResampler *resampler;

	// Audio Preview: is thread running
	bool preview_thread_run;

	// Audio Preview: Off-screen cairo surface
	cairo_surface_t *preview_surface;

	// Audio Preview: Cairo handle for off-screen surface
	cairo_t *preview_cr;

	// Audio Preview: The start and end times of the audio visible in the preview
	stack_time_t preview_start;
	stack_time_t preview_end;

	// Audio Preview: The size of the preview graph
	int preview_width;
	int preview_height;

	// Audio Preview: The thread generating the preview image
	std::thread preview_thread;

	// Audio Preview: The audio preview widget
	GtkWidget *preview_widget;

	// Time of last redraw of audio preview during playback
	stack_time_t last_preview_redraw_time;
} StackAudioCue;

// Functions: Audio cue functions
void stack_audio_cue_register();
bool stack_audio_cue_set_file(StackAudioCue *cue, const char *uri);

// Defines:
#define STACK_AUDIO_CUE(_c) ((StackAudioCue*)(_c))

#endif

