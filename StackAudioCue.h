#ifndef _STACKAUDIOCUE_H_INCLUDED
#define _STACKAUDIOCUE_H_INCLUDED

// Includes:
#include "StackCue.h"

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
	
	// Fade in time at start of action
	//stack_time_t fade_in_time;
	
	// Fade out time at start of action
	//stack_time_t fade_out_time;
	
	// Media start time (e.g. this much time is skipped at the start of the 
	// media file)
	stack_time_t media_start_time;
	
	// Media end time (e.g. from this time forward is ignored at the end of the 
	// media file)
	stack_time_t media_end_time;

	// The (untrimmed) length of the media file
	stack_time_t file_length;
	
	// Pre-fade-in volume, in dB	
	//double start_volume;
	
	// Post-fade-in, pre-fade-out volume, in dB
	double play_volume;
	
	// Post-fade-out volume, in dB
	//double end_volume;
	
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

	// Rebuffer after pause
	long rebuffer_amount;
} StackAudioCue;

// Functions: Audio cue functions
void stack_audio_cue_register();
bool stack_audio_cue_set_file(StackAudioCue *cue, const char *uri);

// Defines:
#define STACK_AUDIO_CUE(_c) ((StackAudioCue*)(_c))

#endif

