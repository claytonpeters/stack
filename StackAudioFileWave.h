#ifndef _STACKAUDIOFILEWAVE_H_INCLUDED
#define _STACKAUDIOFILEWAVE_H_INCLUDED

// Includes:
#include "StackAudioFile.h"

typedef struct StackAudioFileWave
{
	StackAudioFile super;

	// Format of the audio data
	StackSampleFormat sample_format;

	// The size of a single frame of data, in bytes (e.g. 2 channels * 32 bits-per-sample / 8 bits-per-byte = 8 bytes)
	size_t frame_size;

	// The offset of the start of the actual wave data in the file, in bytes
	size_t data_start_offset;

	// The size of the data
	size_t data_size;

	// The amount of the data chunk we've read, in bytes
	size_t data_read;
} StackAudioFileWave;

StackAudioFileWave *stack_audio_file_create_wave(GFileInputStream *file);
void stack_audio_file_destroy_wave(StackAudioFileWave *audio_file);
void stack_audio_file_seek_wave(StackAudioFileWave *audio_file, stack_time_t pos);
size_t stack_audio_file_read_wave(StackAudioFileWave *audio_file, float *buffer, size_t frames);

#endif
