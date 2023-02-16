#ifndef _STACKAUDIOCUE_H_INCLUDED
#define _STACKAUDIOCUE_H_INCLUDED

// Includes:
#include "StackCue.h"
#include <thread>

// Supported file formats
typedef enum StackAudioFileFormat
{
	STACK_AUDIO_FILE_FORMAT_NONE = 0,
	STACK_AUDIO_FILE_FORMAT_WAVE = 1,
	STACK_AUDIO_FILE_FORMAT_MP3 = 2,
} StackAudioFileFormat;

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

	// The (untrimmed) length of the media file
	stack_time_t file_length;

	// Post-fade-in, pre-fade-out volume, in dB
	double play_volume;

	// The GtkBuilder instance
	GtkBuilder *builder;

	// The media tab
	GtkWidget *media_tab;

	// The audio format
	StackAudioFileFormat format;

	// Arbitrary per-file data
	void *file_data;

	// Amount of audio data sent so far in playback
	stack_time_t playback_data_sent;

	// The current volume of the cue
	double playback_live_volume;

	// The currently open file
	GFile *playback_file;
	GFileInputStream *playback_file_stream;

	// Audio pointer
	size_t playback_audio_ptr;

	// Arbitrary per-file playback data
	void *playback_data;

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

