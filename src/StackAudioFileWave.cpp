// Includes:
#include "StackAudioFileWave.h"
#include "StackLog.h"

// RIFF Wave header structure
#pragma pack(push, 2)
struct WaveHeader
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
};
#pragma pack(pop)

static bool stack_audio_file_wave_read_header(GInputStream *stream, WaveHeader *header, size_t *data_start_offset, size_t *data_size, StackSampleFormat *format)
{
	*data_start_offset = 0;

    if (g_input_stream_read(stream, (void*)header, sizeof(WaveHeader), NULL, NULL) != sizeof(WaveHeader))
    {
        stack_log("stack_audio_file_wave_read_header(): Failed to read %lu byte header\n", sizeof(WaveHeader));
        return false;
    }

    // Check for RIFF header
    if (!(header->chunk_id[0] == 'R' && header->chunk_id[1] == 'I' && header->chunk_id[2] == 'F' && header->chunk_id[3] == 'F'))
    {
        stack_log("stack_audio_file_wave_read_header(): Not a Wave file: No RIFF header\n");
        return false;
    }

    // Check for WAVE format
    if (!(header->format[0] == 'W' && header->format[1] == 'A' && header->format[2] == 'V' && header->format[3] == 'E'))
    {
        stack_log("stack_audio_file_wave_read_header(): Not a Wave file: No WAVEfmt header\n");
        return false;
    }

    // Check for 'fmt ' subchunk
    if (!(header->subchunk_1_id[0] == 'f' && header->subchunk_1_id[1] == 'm' && header->subchunk_1_id[2] == 't' && header->subchunk_1_id[3] == ' '))
    {
        stack_log("stack_audio_file_wave_read_header(): Failed to find fmt chunk\n");
        return false;
    }

    // Check the audio_format. We support PCM (1) and IEEE-float (3)
	bool valid_format = true;
	if (header->audio_format == 1)
	{
		if (header->bits_per_sample == 8)
		{
			*format = STACK_SAMPLE_FORMAT_INT8;
		}
		else if (header->bits_per_sample == 16)
		{
			*format = STACK_SAMPLE_FORMAT_INT16;
		}
		else if (header->bits_per_sample == 24)
		{
			*format = STACK_SAMPLE_FORMAT_INT24;
		}
		else if (header->bits_per_sample == 32)
		{
			*format = STACK_SAMPLE_FORMAT_INT32;
		}
		else
		{
			valid_format = false;
		}
	}
	else if (header->audio_format == 3)
	{
		if (header->bits_per_sample == 32)
		{
			*format = STACK_SAMPLE_FORMAT_FLOAT32;
		}
		else if (header->bits_per_sample == 64)
		{
			*format = STACK_SAMPLE_FORMAT_FLOAT64;
		}
		else
		{
			valid_format = false;
		}
	}
	else
	{
		valid_format = false;
	}

	if (!valid_format)
    {
        stack_log("stack_audio_file_wave_read_header(): Non-PCM audio format (%d) is not supported\n", header->audio_format);
        return false;
    }

    // If the byte rate isn't given, calculate it
    if (header->byte_rate == 0)
    {
        header->byte_rate = header->sample_rate * header->num_channels * header->bits_per_sample / 8;
    }

    // Check that our second subchunk is "data"
    if (header->subchunk_2_id[0] == 'd' && header->subchunk_2_id[1] == 'a' && header->subchunk_2_id[2] == 't' && header->subchunk_2_id[3] == 'a')
    {
		*data_start_offset = sizeof(WaveHeader);
		*data_size = header->subchunk_2_size;
        return true;
    }

    // If our second subchunk is not data, search for the subchunk

    // Seek past chunk 2 (we've already read it's size as part of the normal
    // WaveHeader structure)
    g_seekable_seek(G_SEEKABLE(stream), header->subchunk_2_size, G_SEEK_CUR, NULL, NULL);
	*data_start_offset = sizeof(WaveHeader) + header->subchunk_2_size;

    // Find the data chunk
    bool found_data_chunk = false;
    while (!found_data_chunk)
    {
        char chunk_id[4];
        uint32_t subchunk_size;

        // Read chunk ID and size
        if (g_input_stream_read(stream, &chunk_id, 4, NULL, NULL) == 4 && g_input_stream_read(stream, &subchunk_size, 4, NULL, NULL) == 4)
        {
            // If the chunk ID is data
            if (chunk_id[0] == 'd' && chunk_id[1] == 'a' && chunk_id[2] == 't' && chunk_id[3] == 'a')
            {
				// Keep track of our offset in the file
				*data_start_offset += 8;
				*data_size = subchunk_size;

                // Stop searching
                found_data_chunk = true;
            }
            else
            {
                // Seek past the chunk
                g_seekable_seek(G_SEEKABLE(stream), subchunk_size, G_SEEK_CUR, NULL, NULL);

				// Keep track of our offset in the file
				*data_start_offset += subchunk_size + 8;
            }
        }
        else
        {
            stack_log("stack_audio_file_wave_read_header(): Failed to read chunk ID\n");
            return false;
        }
    }

    return true;
}

StackAudioFileWave *stack_audio_file_create_wave(GFileInputStream *stream)
{
	WaveHeader header;
	size_t data_start_offset, data_size;
	StackSampleFormat format = STACK_SAMPLE_FORMAT_UNKNOWN;
	bool is_wave_file = stack_audio_file_wave_read_header(G_INPUT_STREAM(stream), &header, &data_start_offset, &data_size, &format);
	if (!is_wave_file)
	{
		return NULL;
	}

	// We've got a wave file, so set up our result
	StackAudioFileWave *result = new StackAudioFileWave;
	result->super.format = STACK_AUDIO_FILE_FORMAT_WAVE;
	result->super.channels = header.num_channels;
	result->super.sample_rate = header.sample_rate;
	result->super.length = (stack_time_t)(double(data_size) / double(header.byte_rate) * NANOSECS_PER_SEC_F);
	result->frame_size = header.num_channels * header.bits_per_sample / 8;
	result->super.frames = data_size / result->frame_size;
	result->sample_format = format;
	result->data_start_offset = data_start_offset;
	result->data_size = data_size;
	result->data_read = 0;

	return result;
}

void stack_audio_file_destroy_wave(StackAudioFileWave *audio_file)
{
	delete audio_file;
}

void stack_audio_file_seek_wave(StackAudioFileWave *audio_file, stack_time_t pos)
{
	uint64_t target_data_byte = stack_time_to_bytes(pos, audio_file->super.sample_rate, audio_file->frame_size);
	uint64_t seek_point = audio_file->data_start_offset + target_data_byte;

	// Seek to the right point in the file
	g_seekable_seek(G_SEEKABLE(audio_file->super.stream), seek_point, G_SEEK_SET, NULL, NULL);
	audio_file->data_read = target_data_byte;
}

size_t stack_audio_file_read_wave(StackAudioFileWave *audio_file, float *buffer, size_t frames)
{
	size_t frames_read = 0, samples_read = 0;

	// If you ask for nothing, or we're already at the end of the file, you get
	// nothing and we can return immediately
	if (frames == 0 || audio_file->data_read >= audio_file->data_size)
	{
		return 0;
	}

	// Figure out how much to read in bytes, without going past the end of the
	// data chunk
	size_t bytes_to_read = audio_file->frame_size * frames;
	if (audio_file->data_read + bytes_to_read > audio_file->data_size)
	{
		bytes_to_read = audio_file->data_size - audio_file->data_read;
	}

	// If the data format in the file is float32, we can read directly in to the
	// output buffer, else we have to allocate some memory
	char *read_buffer = NULL;
	if (audio_file->sample_format == STACK_SAMPLE_FORMAT_FLOAT32)
	{
		read_buffer = (char*)buffer;
	}
	else
	{
		read_buffer = new char[bytes_to_read];
	}

	// Read into the buffer
	gssize bytes_read = g_input_stream_read(G_INPUT_STREAM(audio_file->super.stream), read_buffer, bytes_to_read, NULL, NULL);

	if (bytes_read > 0)
	{
		// Keep track of how much data we've read
		audio_file->data_read += bytes_read;

		// Work out how many frames we actually read which could be less than
		// was requested
		frames_read = bytes_read / audio_file->frame_size;

		// Perform the conversion from whatever format we had to float32
		if (audio_file->sample_format != STACK_SAMPLE_FORMAT_FLOAT32)
		{
			stack_audio_file_convert(audio_file->sample_format, read_buffer, frames_read * audio_file->super.channels, buffer);
		}
	}

	// Tidy up
	if (audio_file->sample_format != STACK_SAMPLE_FORMAT_FLOAT32)
	{
		delete [] read_buffer;
	}

	return frames_read;
}
