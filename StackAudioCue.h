#ifndef _STACKAUDIOCUE_H_INCLUDED
#define _STACKAUDIOCUE_H_INCLUDED

// Includes:
#include "StackCue.h"

// RIFF Wave header structure
#pragma pack(push, 2)
typedef struct WaveHeader
{
	char chunk_id[4];
	uint32_t chunk_size;
	char format[4];
	char subchunk_1_id[4];
	uint32_t subchunk_1_size;
	uint16_t audio_format;
	uint16_t num_channels;
	uint32_t sample_rate;
	uint32_t byte_rate;
	uint16_t block_align;
	uint16_t bits_per_sample;
	char subchunk_2_id[4];
	uint32_t subchunk_2_size;
} WaveHeader;
#pragma pack(pop)

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

	// Amount of audio data sent so far in playback
	stack_time_t playback_data_sent;
	
	// The current volume of the cue
	double playback_live_volume;
	
	// The currently open file
	GFile *playback_file;
	GFileInputStream *playback_file_stream;
	
	// The wave header of the open file
	WaveHeader playback_header;
	
	// Audio pointer
	size_t playback_audio_ptr;
} StackAudioCue;

// Functions: Audio cue functions
void stack_audio_cue_register();

// Defines:
#define STACK_AUDIO_CUE(_c) ((StackAudioCue*)(_c))

#endif

