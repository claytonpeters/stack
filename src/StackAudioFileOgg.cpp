#if HAVE_VORBISFILE == 1
// Includes:
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>
#include "StackAudioFileOgg.h"
#include "StackLog.h"

#define OGG_DECODE_CHUNK_SIZE_SAMPLES 2048

// Wrapper for vorbisfile so it can use GFile for read
size_t ogg_gfile_wrapper_read(void *ptr, size_t size, size_t nmemb, void *datasource)
{
	size_t result = g_input_stream_read(G_INPUT_STREAM(datasource), ptr, size * nmemb, NULL, NULL);
	return result / size;
}

// Wrapper for vorbisfile so it can use GFile for seek
int ogg_gfile_wrapper_seek(void *datasource, ogg_int64_t offset, int whence)
{
	GSeekType seek_type;
	switch (whence)
	{
		case SEEK_SET:
			seek_type = G_SEEK_SET;
			break;
		case SEEK_CUR:
			seek_type = G_SEEK_CUR;
			break;
		case SEEK_END:
			seek_type = G_SEEK_END;
			break;
		default:
			return -1;
			break;
	}

	if (g_seekable_seek(G_SEEKABLE(datasource), offset, seek_type, NULL, NULL))
	{
		return 0;
	}
	else
	{
		return -1;
	}
}

// Wrapper for vorbisfile so it can use GFile for close
int ogg_gfile_wrapper_close(void *datasource)
{
	// We do nothing here, as StackAudioFile handles the closing of the stream
	return 0;
}

// Wrapper for vorbisfile so it can use GFile for tell
long ogg_gfile_wrapper_tell(void *datasource)
{
	return (long)g_seekable_tell(G_SEEKABLE(datasource));
}

StackAudioFileOgg *stack_audio_file_create_ogg(GFileInputStream *stream)
{
	// Define our functions for libvorbisfile to use for GFile operations as it
	// only deals with stdio FILE* normally
	static ov_callbacks callbacks = {
		.read_func = ogg_gfile_wrapper_read,
		.seek_func = ogg_gfile_wrapper_seek,
		.close_func = ogg_gfile_wrapper_close,
		.tell_func = ogg_gfile_wrapper_tell
	};

	// Rewind back to the start of the file, in case another format left us somewhere else
	g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);

	// Create a new object and attempt to open the file
	StackAudioFileOgg *result = new StackAudioFileOgg;
	int open_result = ov_open_callbacks((void*)stream, &result->file, NULL, 0, callbacks);
	if (open_result != 0)
	{
		stack_log("stack_audio_file_create_ogg(): Not opening as Vorbis: %d\n", open_result);
		delete result;
		return NULL;
	}

	// Fill in the details about the file
	vorbis_info *ogg_info = ov_info(&result->file, 0);
	result->super.format = STACK_AUDIO_FILE_FORMAT_OGG;
	result->super.channels = ogg_info->channels;
	result->super.sample_rate = ogg_info->rate;
	result->super.frames = ov_pcm_total(&result->file, 0);
	result->super.length = (stack_time_t)(double(result->super.frames) / double(result->super.sample_rate) * NANOSECS_PER_SEC_F);
	result->eof = false;

	// Create a buffer of decoded frames
	result->decoded_buffer = stack_ring_buffer_create(4096 * result->super.channels);

	return result;
}

void stack_audio_file_destroy_ogg(StackAudioFileOgg *audio_file)
{
	// Tidy up libvorbisfile
	ov_clear(&audio_file->file);

	// Tidy up out ring buffer
	stack_ring_buffer_destroy(audio_file->decoded_buffer);

	// Tidy up ourselves
	delete audio_file;
}

void stack_audio_file_seek_ogg(StackAudioFileOgg *audio_file, stack_time_t pos)
{
	if (ov_time_seek(&audio_file->file, (double)pos / NANOSECS_PER_SEC_F) < 0)
	{
		stack_log("stack_audio_file_seek_ogg(): Failed to seek\n");
	}

	audio_file->eof = false;
	stack_ring_buffer_reset(audio_file->decoded_buffer);
}

void stack_audio_file_ogg_decode_more(StackAudioFileOgg *audio_file)
{
	// These two must be the same size
	int16_t read_buffer[OGG_DECODE_CHUNK_SIZE_SAMPLES];
	float conv_buffer[OGG_DECODE_CHUNK_SIZE_SAMPLES];

	// Read 16-bit samples into our read buffer
	long bytes_read = ov_read(&audio_file->file, (char*)read_buffer, OGG_DECODE_CHUNK_SIZE_SAMPLES * sizeof(int16_t), 0, sizeof(int16_t), 1, &audio_file->bitstream);
	if (bytes_read <= 0)
	{
		// Assume any error or EOF as an end-of-file
		audio_file->eof = true;
	}
	else
	{
		// Convert to float and add to our ring buffer
		size_t samples_read = bytes_read / sizeof(int16_t);
		stack_audio_file_convert(STACK_SAMPLE_FORMAT_INT16, read_buffer, samples_read, conv_buffer);
		stack_ring_buffer_write(audio_file->decoded_buffer, conv_buffer, samples_read, 1);
	}
}

size_t stack_audio_file_read_ogg(StackAudioFileOgg *audio_file, float *buffer, size_t frames)
{
	const size_t channels = audio_file->super.channels;

	size_t frames_out = 0;
	while (frames_out < frames && (audio_file->decoded_buffer->used > 0 || !audio_file->eof))
	{
		// Take an appropriate amount of data from the ring buffer
		if (audio_file->decoded_buffer->used > 0)
		{
			frames_out += stack_ring_buffer_read(audio_file->decoded_buffer, &buffer[frames_out * channels], (frames - frames_out) * channels, 1) / channels;
		}

		// Try to keep 1024 samples in our ring buffer
		if (audio_file->decoded_buffer->used < 1024 * channels && !audio_file->eof)
		{
			// Decode more Ogg data
			stack_audio_file_ogg_decode_more(audio_file);
		}
	}

	return frames_out;
}

#endif
