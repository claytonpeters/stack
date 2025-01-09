#if HAVE_LIBFLAC == 1
// Includes:
#include "StackAudioFileFLAC.h"
#include "StackLog.h"

static const float INT8_SCALAR = 7.8125e-3f ;
static const float INT16_SCALAR = 3.051757812e-5f;
static const float INT24_SCALAR = 1.192092896e-7f;
static const float INT32_SCALAR = 4.656612873e-10;

FLAC__StreamDecoderReadStatus flac_gfile_wrapper_read(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
{
	StackAudioFileFLAC* file = (StackAudioFileFLAC*)client_data;

	size_t result = g_input_stream_read(G_INPUT_STREAM(file->super.stream), buffer, *bytes, NULL, NULL);
	if (result < *bytes)
	{
		*bytes = result;
		file->eof = true;
		return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
	}
	else
	{
		*bytes = result;
		return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
	}
}

FLAC__StreamDecoderSeekStatus flac_gfile_wrapper_seek(const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *client_data)
{
	StackAudioFileFLAC* file = (StackAudioFileFLAC*)client_data;

	if (g_seekable_seek(G_SEEKABLE(file->super.stream), absolute_byte_offset, G_SEEK_SET, NULL, NULL))
	{
		return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
	}
	else
	{
		return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
	}
}

FLAC__StreamDecoderTellStatus flac_gfile_wrapper_tell(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data)
{
	StackAudioFileFLAC* file = (StackAudioFileFLAC*)client_data;
	*absolute_byte_offset = (FLAC__uint64)g_seekable_tell(G_SEEKABLE(file->super.stream));
	return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

FLAC__StreamDecoderLengthStatus flac_gfile_wrapper_length(const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *client_data)
{
	StackAudioFileFLAC* file = (StackAudioFileFLAC*)client_data;
	size_t before = (FLAC__uint64)g_seekable_tell(G_SEEKABLE(file->super.stream));
	if (!g_seekable_seek(G_SEEKABLE(file->super.stream), 0, G_SEEK_END, NULL, NULL))
	{
		return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
	}

	*stream_length = (FLAC__uint64)g_seekable_tell(G_SEEKABLE(file->super.stream));
	g_seekable_seek(G_SEEKABLE(file->super.stream), before, G_SEEK_SET, NULL, NULL);

	return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
}

FLAC__bool flac_gfile_wrapper_eof(const FLAC__StreamDecoder *decoder, void *client_data)
{
	StackAudioFileFLAC* file = (StackAudioFileFLAC*)client_data;
	return file->eof;
}

FLAC__StreamDecoderWriteStatus flac_gfile_wrapper_frame(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data)
{
	StackAudioFileFLAC* file = (StackAudioFileFLAC*)client_data;

	// If we're just testing the file (i.e. we're still in create), don't do anything
	if (!file->ready)
	{
		return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
	}

	if (frame->header.channels != file->super.channels)
	{
		stack_log("flac_gfile_wrapper_frame(): Frame differs in channel count - this is not supported!\n");
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	}

	if (frame->header.sample_rate != file->super.sample_rate)
	{
		stack_log("flac_gfile_wrapper_frame(): Frame differs in sample rate - this is not supported!\n");
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	}

	float scalar = 1.0;
	switch (frame->header.bits_per_sample)
	{
		case 8:
			scalar = INT8_SCALAR;
			break;
		case 16:
			scalar = INT16_SCALAR;
			break;
		case 24:
			scalar = INT24_SCALAR;
			break;
		case 32:
			scalar = INT32_SCALAR;
			break;
		default:
			stack_log("flac_gfile_wrapper_frame(): Unknown bit depth for frame\n");
			return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
			break;
	}

	size_t channels = frame->header.channels;
	size_t frames = frame->header.blocksize;
	size_t samples = frames * channels;

	// FLAC returns non-multiplexed data, so we need to convert the channels one at a time. It
	// also always returns 32-bit boxed samples regardless of the bit-depth, so we can't use
	// our normal stack_audio_file_convert functions
	float *write_buffer = new float[samples];
	for (size_t channel = 0; channel < channels; channel++)
	{
		const int32_t *channel_buffer = buffer[channel];
		for (size_t sample = channel, frame = 0; sample < samples; sample += channels, frame++)
		{
			write_buffer[sample] = float(channel_buffer[frame]) * scalar;
		}
	}

	// Convert to float and add to our ring buffer
	stack_ring_buffer_write(file->decoded_buffer, write_buffer, samples, 1);

	delete [] write_buffer;

	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void flac_gfile_wrapper_error(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
	StackAudioFileFLAC* file = (StackAudioFileFLAC*)client_data;
	if (file->ready)
	{
		stack_log("flac_gfile_wrapper_frame(): error: %d\n", status);
		file->eof = true;
	}
}

StackAudioFileFLAC *stack_audio_file_create_flac(GFileInputStream *stream)
{
	// Create the decoder
	FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
	if (decoder == NULL)
	{
		return NULL;
	}

	// Rewind back to the start of the file, in case another format left us somewhere else
	g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);

	// Create our object
	StackAudioFileFLAC *result = new StackAudioFileFLAC;
	result->super.stream = stream;
	result->decoder = decoder;
	result->ready = false;
	result->eof = false;

	// Create the stream decoder
	if (FLAC__stream_decoder_init_stream(decoder, flac_gfile_wrapper_read, flac_gfile_wrapper_seek, flac_gfile_wrapper_tell, flac_gfile_wrapper_length, flac_gfile_wrapper_eof, flac_gfile_wrapper_frame, NULL, flac_gfile_wrapper_error, result) != FLAC__STREAM_DECODER_INIT_STATUS_OK)
	{
		stack_log("stack_audio_file_create_flac(): Couldn't initialise stream decoder\n");
		FLAC__stream_decoder_delete(decoder);
		delete result;
		return NULL;
	}

	// Find and decode the metadata
	if (!FLAC__stream_decoder_process_until_end_of_metadata(decoder))
	{
		FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(decoder);
		stack_log("stack_audio_file_create_flac(): Couldn't find metadata: %d\n", state);
		FLAC__stream_decoder_delete(decoder);
		delete result;
		return NULL;
	}

	// Find and decode at least one audio block
	if (!FLAC__stream_decoder_process_single(decoder))
	{
		stack_log("stack_audio_file_create_flac(): Couldn't find metadata\n");
		FLAC__stream_decoder_delete(decoder);
		delete result;
		return NULL;
	}

	// Fill in the details about the file
	result->super.format = STACK_AUDIO_FILE_FORMAT_FLAC;
	result->super.channels = FLAC__stream_decoder_get_channels(decoder);
	result->super.sample_rate = FLAC__stream_decoder_get_sample_rate(decoder);
	result->super.frames = FLAC__stream_decoder_get_total_samples(decoder);
	result->super.length = (stack_time_t)(double(result->super.frames) / double(result->super.sample_rate) * NANOSECS_PER_SEC_F);
	result->eof = false;

	// Create a buffer of decoded frames
	result->decoded_buffer = stack_ring_buffer_create(16384 * result->super.channels);

	// Mark as ready
	result->ready = true;

	return result;
}

void stack_audio_file_destroy_flac(StackAudioFileFLAC *audio_file)
{
	// Tidy up FLAC
	FLAC__stream_decoder_delete(audio_file->decoder);

	// Tidy up out ring buffer
	stack_ring_buffer_destroy(audio_file->decoded_buffer);

	// Tidy up ourselves
	delete audio_file;
}

void stack_audio_file_seek_flac(StackAudioFileFLAC *audio_file, stack_time_t pos)
{
	FLAC__StreamDecoderState pre_state = FLAC__stream_decoder_get_state(audio_file->decoder);
	audio_file->eof = false;
	stack_ring_buffer_reset(audio_file->decoded_buffer);

	if (!FLAC__stream_decoder_seek_absolute(audio_file->decoder, (pos * audio_file->super.sample_rate) / NANOSECS_PER_SEC))
	{
		FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(audio_file->decoder);
		stack_log("stack_audio_file_seek_flac(): FLAC__stream_decoder_seek_absolute failed: %d\n", state);
	}
}

void stack_audio_file_flac_decode_more(StackAudioFileFLAC *audio_file)
{
	if (!FLAC__stream_decoder_process_single(audio_file->decoder))
	{
		FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(audio_file->decoder);
		stack_log("stack_audio_file_flac_decode_more(): FLAC__stream_decoder_process_single failed: %d\n", state);
	}
}

size_t stack_audio_file_read_flac(StackAudioFileFLAC *audio_file, float *buffer, size_t frames)
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
			// Decode more FLAC data
			stack_audio_file_flac_decode_more(audio_file);
		}
	}

	return frames_out;
}

#endif
