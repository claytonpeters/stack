#if HAVE_LIBFLAC == 1
#ifndef _STACKAUDIOFILEFLAC_H_INCLUDED
#define  _STACKAUDIOFILEFLAC_H_INCLUDED

// Includes:
#include "StackAudioFile.h"
#include "StackRingBuffer.h"
#include <FLAC/stream_decoder.h>

struct StackAudioFileFLAC
{
	StackAudioFile super;
	FLAC__StreamDecoder *decoder;
	bool eof;
	bool ready;

	// We read in blocks, so we need to buffer
	StackRingBuffer *decoded_buffer;
};

StackAudioFileFLAC *stack_audio_file_create_flac(GFileInputStream *file);
void stack_audio_file_destroy_flac(StackAudioFileFLAC *audio_file);
void stack_audio_file_seek_flac(StackAudioFileFLAC* audio_file, stack_time_t pos);
size_t stack_audio_file_read_flac(StackAudioFileFLAC *audio_file, float *buffer, size_t frames)
	__attribute__((access (write_only, 2, 3)));

#endif
#endif
