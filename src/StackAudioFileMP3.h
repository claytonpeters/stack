#if HAVE_LIBMAD == 1
#ifndef _STACKAUDIOFILEMP3_H_INCLUDED
#define  _STACKAUDIOFILEMP3_H_INCLUDED

// Includes:
#include "StackAudioFile.h"
#include "StackRingBuffer.h"
#include "MPEGAudioFile.h"
#include <mad.h>
#include <vector>

typedef std::vector<MP3FrameInfo> MP3FrameInfoVector;

struct StackAudioFileMP3
{
	StackAudioFile super;

	MP3FrameInfoVector frames;

	// Our decoder
	mad_stream mp3_stream;
	mad_frame mp3_frame;
	mad_synth mp3_synth;

	// The frame index we're currently decoding
	MP3FrameInfoVector::iterator frame_iterator;

	// We read in blocks, so we need to buffer
	StackRingBuffer *decoded_buffer;

	// We always need a minimum of two frames to decode, but we need the two
	// frames to overlap, so we keep a copy of the old frames
	unsigned char *frames_buffer;

	// Things we might discover from an info header: encoder delay at start of file
	uint16_t delay;

	// Padding added in last frame
	uint16_t padding;
};

StackAudioFileMP3 *stack_audio_file_create_mp3(GFileInputStream *file);
void stack_audio_file_destroy_mp3(StackAudioFileMP3 *audio_file);
void stack_audio_file_seek_mp3(StackAudioFileMP3* audio_file, stack_time_t pos);
size_t stack_audio_file_read_mp3(StackAudioFileMP3 *audio_file, float *buffer, size_t frames)
	__attribute__((access (write_only, 2, 3)));

#endif
#endif
