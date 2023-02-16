#if HAVE_LIBMAD == 1
// Includes:
#include "StackAudioFileMP3.h"
#include "MPEGAudioFile.h"
#include <vector>

static const uint16_t DEFAULT_DECODER_DELAY = 529;

typedef struct MP3Info
{
	uint32_t sample_rate;
	uint16_t num_channels;
	uint64_t num_samples_per_channel;
	std::vector<MP3FrameInfo> frames;
} MP3Info;

static bool stack_audio_file_mp3_process(GInputStream *stream, MP3Info *mp3_info)
{
    uint32_t frames = 0;
	mp3_info->num_samples_per_channel = 0;

    if (!mpeg_audio_file_find_frames(stream, &mp3_info->num_channels, &mp3_info->sample_rate, &frames, &mp3_info->num_samples_per_channel, &mp3_info->frames))
    {
        fprintf(stderr, "stack_audio_file_mp3_process(): Processing failed\n");
        return false;
    }

    // Return whether we succesfully parsed the header
    return true;
}

/*static inline float scale_sample(mad_fixed_t sample)
{
	static const float INT16_SCALAR = 3.051757812e-5f;

	/*sample += (1L << (MAD_F_FRACBITS - 16));
	if (sample >= MAD_F_ONE)
	{
		sample = MAD_F_ONE - 1;
	}
	else if (sample < -MAD_F_ONE)
	{
		sample = -MAD_F_ONE;
	}/

	return (float)(sample >> (MAD_F_FRACBITS + 1 - 16)) * INT16_SCALAR;
}*/

StackAudioFileMP3 *stack_audio_file_create_mp3(GFileInputStream *stream)
{
	MP3Info mp3_info;
	bool is_mp3_file = stack_audio_file_mp3_process(G_INPUT_STREAM(stream), &mp3_info);
	if (!is_mp3_file)
	{
		return NULL;
	}

	// Rewind back to the first MP3 frame as we've read the entire file
	g_seekable_seek(G_SEEKABLE(stream), mp3_info.frames[0].byte_position, G_SEEK_SET, NULL, NULL);

	StackAudioFileMP3 *result = new StackAudioFileMP3;
	result->super.format = STACK_AUDIO_FILE_FORMAT_MP3;
	result->super.channels = mp3_info.num_channels;
	result->super.sample_rate = mp3_info.sample_rate;
	result->super.length = (stack_time_t)(double(mp3_info.num_samples_per_channel) / double(mp3_info.sample_rate) * NANOSECS_PER_SEC_F);
	std::swap(result->frames, mp3_info.frames);
	result->frames_buffer = NULL;

	// Initialise MP3 decoding
	mad_stream_init(&result->mp3_stream);
	mad_frame_init(&result->mp3_frame);
	mad_synth_init(&result->mp3_synth);
	mad_stream_options(&result->mp3_stream, 0);
	result->frame_iterator = result->frames.begin();
	result->delay = 0;
	result->padding = 0;

	// Create the ring buffer capable of handling two MP3 frames of 1152 audio frames
	// of two channels
	result->decoded_buffer = stack_ring_buffer_create(1152 * 4);

	return result;
}

void stack_audio_file_destroy_mp3(StackAudioFileMP3 *audio_file)
{
	// Tidy up MP3 decoder
	mad_synth_finish(&audio_file->mp3_synth);
	mad_frame_finish(&audio_file->mp3_frame);
	mad_stream_finish(&audio_file->mp3_stream);

	// Tidy up buffering
	stack_ring_buffer_destroy(audio_file->decoded_buffer);
	if (audio_file->frames_buffer != NULL)
	{
		delete audio_file->frames_buffer;
	}

	// Tidy up ourselves
	delete audio_file;
}

static size_t stack_audio_file_mp3_decode_next_mpeg_frame(StackAudioFileMP3 *audio_file)
{
	size_t bytes_in_buffer = 0;
	bool skip_decode = false, current_is_last_frame = false;

	// If we don't have a previous frame, we need to read two
	if (audio_file->frames_buffer == NULL)
	{
		// Get some handy iterators
		MP3FrameInfoVector::iterator current_frame = audio_file->frame_iterator;
		MP3FrameInfoVector::iterator next_frame = audio_file->frame_iterator;
		next_frame++;

		// Work out how many bytes to read for two frames
		size_t bytes_to_read = current_frame->frame_size_bytes + next_frame->frame_size_bytes;

		// Allocate
		audio_file->frames_buffer = new unsigned char[bytes_to_read];

		// Read the two frames
		bytes_in_buffer = g_input_stream_read(G_INPUT_STREAM(audio_file->super.stream), audio_file->frames_buffer, bytes_to_read, NULL, NULL);

		// Check for an info frame (should be the first frame)
		if (bytes_in_buffer > 40)
		{
			unsigned char *h = &audio_file->frames_buffer[36];

			// Skip past the Xing or Info frame if we have one
			if (h[0] == 'X' && h[1] == 'i' && h[2] == 'n' && h[3] == 'g')
			{
				skip_decode = true;
			}
			else if (h[0] == 'I' && h[1] == 'n' && h[2] == 'f' && h[3] == 'o')
			{
				skip_decode = true;
			}
		}

		// If we found a Xing/Info frame, check for a LAME or LAVC header
		if (skip_decode && bytes_in_buffer > 0x9f)
		{
			bool get_delay = false;
			unsigned char *h = &audio_file->frames_buffer[0x9c];
			if (h[0] == 'L' && h[1] == 'A' && ((h[2] == 'M' && h[3] == 'E') || (h[2] == 'V' && h[3] == 'E')))
			{
				get_delay = true;
			}

			if (get_delay && bytes_in_buffer > 0xb4)
			{
				unsigned char *d = &audio_file->frames_buffer[0xb1];

				// Grab the first 12 bits at 'd' for the delay
				audio_file->delay = (((uint16_t)d[0] << 4) | ((uint16_t)d[1] & 0xf0) >> 4) + DEFAULT_DECODER_DELAY;

				// Grab the next 12 bits for the padding
				audio_file->padding = ((((uint16_t)d[1] & 0x0f) << 8) | (uint16_t)d[2]) - DEFAULT_DECODER_DELAY;
			}
		}
	}
	else
	{
		// Get some handy iterators
		MP3FrameInfoVector::iterator current_frame = audio_file->frame_iterator;
		MP3FrameInfoVector::iterator previous_frame = audio_file->frame_iterator;
		previous_frame--;
		MP3FrameInfoVector::iterator next_frame = audio_file->frame_iterator;
		next_frame++;

		current_is_last_frame = (next_frame == audio_file->frames.end());

		// Work out how many bytes to read for two frames
		size_t bytes_to_read;
		if (!current_is_last_frame)
		{
			bytes_to_read = current_frame->frame_size_bytes + next_frame->frame_size_bytes;
		}
		else
		{
			// On the last frame, we need to add MAD_BUFFER_GUARD zeroes to ensure
			// the last frame isn't truncated
			bytes_to_read = current_frame->frame_size_bytes + MAD_BUFFER_GUARD;
		}
		unsigned char *new_frames_buffer = new unsigned char[bytes_to_read];

		// Copy in the previous frame from the last buffer
		memcpy(new_frames_buffer, &audio_file->frames_buffer[previous_frame->frame_size_bytes], current_frame->frame_size_bytes);

		// No longer need the old buffer
		delete [] audio_file->frames_buffer;

		gssize bytes_read = 0;
		if (!current_is_last_frame)
		{
			// Read in the next frame
			bytes_read = g_input_stream_read(G_INPUT_STREAM(audio_file->super.stream), &new_frames_buffer[current_frame->frame_size_bytes], next_frame->frame_size_bytes, NULL, NULL);
		}
		else
		{
			// These are our guard zeros for the last frame
			memset(&new_frames_buffer[current_frame->frame_size_bytes], 0, MAD_BUFFER_GUARD);
			bytes_read = MAD_BUFFER_GUARD;
		}

		// Store the buffer
		audio_file->frames_buffer = new_frames_buffer;

		// Keep a note of how much data we can process this time
		bytes_in_buffer = current_frame->frame_size_bytes + bytes_read;
	}

	// Increment our iterator to move on to the next MPEG frame
	audio_file->frame_iterator++;

	// If we had an info frame, skip decoding
	if (skip_decode)
	{
		return 0;
	}

	// Perform the decode
	mad_stream_buffer(&audio_file->mp3_stream, audio_file->frames_buffer, bytes_in_buffer);
	mad_frame_decode(&audio_file->mp3_frame, &audio_file->mp3_stream);
	mad_synth_frame(&audio_file->mp3_synth, &audio_file->mp3_frame);

	// This is the maximum size of a mad_pcm frame, 1152 frames of two channels
	float scale_buffer[1152 * 2];

	// Perform the scaling from 24-bit int to float
	size_t frames_to_add = audio_file->mp3_synth.pcm.length;
	if (audio_file->mp3_synth.pcm.length > 0)
	{
		if (audio_file->mp3_synth.pcm.channels == 1)
		{
			for (size_t i = audio_file->delay; i < audio_file->mp3_synth.pcm.length; i++)
			{
				scale_buffer[i] = (float)mad_f_todouble(audio_file->mp3_synth.pcm.samples[0][i]);
			}
		}
		else if (audio_file->mp3_synth.pcm.channels == 2)
		{
			for (size_t i = audio_file->delay; i < audio_file->mp3_synth.pcm.length; i++)
			{
				scale_buffer[i*2] = (float)mad_f_todouble(audio_file->mp3_synth.pcm.samples[0][i]);
				scale_buffer[i*2+1] = (float)mad_f_todouble(audio_file->mp3_synth.pcm.samples[1][i]);
			}
		}

		// If we've got a delay, which could be true in the first audio frame,
		// then we need to skip it, so we need to add fewer samples
		frames_to_add -= audio_file->delay;

		// If we're in the last frame, then don't add any padding samples we've
		// discovered
		if (current_is_last_frame)
		{
			frames_to_add -= audio_file->padding;
		}

		// Write multiplexed data to the ring buffer
		stack_ring_buffer_write(audio_file->decoded_buffer, &scale_buffer[audio_file->delay * audio_file->mp3_synth.pcm.channels], frames_to_add * audio_file->mp3_synth.pcm.channels, 1);

		// Once we've used the delay, reset it to zero
		audio_file->delay = 0;
	}

	// Return how many samples we decoded
	return frames_to_add;
}

void stack_audio_file_seek_mp3(StackAudioFileMP3 *audio_file, stack_time_t pos)
{
	// Reset synth/frame/stream so we're ready to decode from the start again
	mad_synth_finish(&audio_file->mp3_synth);
	mad_frame_finish(&audio_file->mp3_frame);
	mad_stream_finish(&audio_file->mp3_stream);
	mad_stream_init(&audio_file->mp3_stream);
	mad_frame_init(&audio_file->mp3_frame);
	mad_synth_init(&audio_file->mp3_synth);
	mad_stream_options(&audio_file->mp3_stream, 0);

	// Reset ring buffer
	stack_ring_buffer_reset(audio_file->decoded_buffer);

	// Determine which sample we're looking for
	uint64_t start_sample = stack_time_to_samples(pos, audio_file->super.sample_rate);

	// Iterate through the known frames, and search for the frame that contains
	// data for our given position
	size_t previous_frame_position = audio_file->frames.begin()->byte_position;
	for (MP3FrameInfoVector::iterator frame = audio_file->frames.begin(); frame != audio_file->frames.end(); frame++)
	{
		// If the frame we're looking at contains our start_sample
		if (frame->sample_position + frame->frame_size_samples >= start_sample)
		{
			// We break here meaning so that we don't update
			// previous_frame_position and thus it points to the byte offset in
			// the file of the frame prior to the one containing our sample,
			// which is useful as we need two frames for meaningful output

			// Set our read iterator to the previous frame (where we're about
			// to seek to)
			audio_file->frame_iterator = frame;
			if (audio_file->frame_iterator != audio_file->frames.begin())
			{
				audio_file->frame_iterator--;
			}

			break;
		}

		// Keep track of the byte position - we need it for seeking
		previous_frame_position = frame->byte_position;
	}

	// Seek to the start of the frame before
	g_seekable_seek(G_SEEKABLE(audio_file->super.stream), previous_frame_position, G_SEEK_SET, NULL, NULL);

	// Skip past however many samples we additionally decoded before our target sample
	size_t skip_frames = start_sample - audio_file->frame_iterator->sample_position;

	fprintf(stderr, "stack_audio_file_seek_mp3(): seek pos %lu (sample %lu) - seek to mpegframe at byte %lu = offset %ld frames\n", pos, start_sample, previous_frame_position, skip_frames);

	// Decode the next two frames into our buffer
	stack_audio_file_mp3_decode_next_mpeg_frame(audio_file);
	stack_audio_file_mp3_decode_next_mpeg_frame(audio_file);

	// TODO: This offset probably needs to account for encoder delay
	stack_ring_buffer_skip(audio_file->decoded_buffer, skip_frames);
}

size_t stack_audio_file_read_mp3(StackAudioFileMP3 *audio_file, float *buffer, size_t frames)
{
	// TODO: Don't hardcode this!
	size_t channels = 2;

	size_t frames_out = 0;
	while (frames_out < frames && (audio_file->frame_iterator != audio_file->frames.end() || audio_file->decoded_buffer->used > 0))
	{
		// Take an appropriate amount of data from the ring buffer
		if (audio_file->decoded_buffer->used > 0)
		{
			frames_out += stack_ring_buffer_read(audio_file->decoded_buffer, &buffer[frames_out * channels], (frames - frames_out) * channels, 1) / channels;
		}

		// If there's less than a full MP3 frame in the decoded buffer, and
		// we're not at the end of the file, decode another frame
		if (audio_file->decoded_buffer->used < 1152 * channels && audio_file->frame_iterator != audio_file->frames.end())
		{
			// Decode more MP3 data
			stack_audio_file_mp3_decode_next_mpeg_frame(audio_file);
		}
	}

	return frames_out;
}

#endif
