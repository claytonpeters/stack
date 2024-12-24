#if HAVE_VORBISFILE == 1
#ifndef _STACKAUDIOFILEOGG_H_INCLUDED
#define  _STACKAUDIOFILEOGG_H_INCLUDED

// Includes:
#include "StackAudioFile.h"
#include <vorbis/vorbisfile.h>
#include "StackRingBuffer.h"

struct StackAudioFileOgg
{
	StackAudioFile super;
	OggVorbis_File file;
	bool eof;
	int bitstream;

	// We read in blocks, so we need to buffer
	StackRingBuffer *decoded_buffer;
};

StackAudioFileOgg *stack_audio_file_create_ogg(GFileInputStream *file);
void stack_audio_file_destroy_ogg(StackAudioFileOgg *audio_file);
void stack_audio_file_seek_ogg(StackAudioFileOgg* audio_file, stack_time_t pos);
size_t stack_audio_file_read_ogg(StackAudioFileOgg *audio_file, float *buffer, size_t frames);

#endif
#endif
