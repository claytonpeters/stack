#ifndef _MPEGAUDIOFILE_H_INCLUDED
#define _MPEGAUDIOFILE_H_INCLUDED

// Includes:
#include <cstdint>
#include <gtk/gtk.h>
#include <vector>

typedef struct MP3FrameInfo
{
	size_t byte_position;       // Location of frame in the file
	size_t frame_size_bytes;    // The size of the frame in bytes
	size_t sample_position;	    // The first sample of this frame
	size_t frame_size_samples;  // The size of the frame in samples
} MP3FrameInfo;

// Functions:
size_t mpeg_audio_file_skip_id3v2(GInputStream *stream);
bool mpeg_audio_file_find_frames(GInputStream *stream, uint16_t *channels, uint32_t *sample_rate, uint32_t *frames, uint64_t *samples, std::vector<MP3FrameInfo> *frame_info);

#endif

