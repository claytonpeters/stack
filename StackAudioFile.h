#ifndef _STACKAUDIOFILE_H_INCLUDED
#define _STACKAUDIOFILE_H_INCLUDED

// Includes:
#include "StackCue.h"
#include <gtk/gtk.h>

// Supported file formats
enum StackAudioFileFormat
{
    STACK_AUDIO_FILE_FORMAT_NONE = 0,
    STACK_AUDIO_FILE_FORMAT_WAVE = 1,
    STACK_AUDIO_FILE_FORMAT_MP3 = 2,
	STACK_AUDIO_FILE_FORMAT_OGG = 3, // Reserved for future implementation
	STACK_AUDIO_FILE_FORMAT_AAC = 4, // Reserved for future implementation
};

enum StackSampleFormat
{
	STACK_SAMPLE_FORMAT_UNKNOWN = 0,
	STACK_SAMPLE_FORMAT_INT8 = 1,
	STACK_SAMPLE_FORMAT_INT16 = 2,
	STACK_SAMPLE_FORMAT_INT24 = 3,
	STACK_SAMPLE_FORMAT_INT32 = 4,
	STACK_SAMPLE_FORMAT_FLOAT32 = 5,
	STACK_SAMPLE_FORMAT_FLOAT64 = 6,
};

struct StackAudioFile
{
	StackAudioFileFormat format;
	size_t channels;
	size_t sample_rate;
	stack_time_t length;
	size_t frames;
    GFile *file;
    GFileInputStream *stream;
};

// Opens the audio file at path, and parses any headers. Returns NULL if no
// supported audio-format could be found or returns something that extends
// StackAudioFile otherwise
StackAudioFile *stack_audio_file_create(const char *uri);

// Destroys a StackAudioFile (or extension)
void stack_audio_file_destroy(StackAudioFile *audio_file);

// Seeks to a the given point (in nanoseconds) in the audio
void stack_audio_file_seek(StackAudioFile *audio_file, stack_time_t pos);

// Reads frames from the audio file and converts them to float
size_t stack_audio_file_read(StackAudioFile *audio_file, float *buffer, size_t frames);

// Converts audio data from a given input format to float
bool stack_audio_file_convert(StackSampleFormat format, const void *input, const size_t samples, float *output);

// Converts a stack_time_t (nanoseconds) to a sample/frame index
uint64_t stack_time_to_samples(stack_time_t t, uint32_t sample_rate);

// Converts a stack_time_t (nanoseconds) to a byte count
uint64_t stack_time_to_bytes(stack_time_t t, uint32_t sample_rate, uint32_t frame_size);

#endif
