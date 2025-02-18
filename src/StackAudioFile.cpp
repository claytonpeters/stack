// Includes:
#include "StackAudioFile.h"
#include "StackAudioFileWave.h"
#if HAVE_LIBMAD == 1
#include "StackAudioFileMP3.h"
#endif
#if HAVE_VORBISFILE == 1
#include "StackAudioFileOgg.h"
#endif
#if HAVE_LIBFLAC == 1
#include "StackAudioFileFLAC.h"
#endif
#include "StackLog.h"

static const float INT8_SCALAR = 7.8125e-3f;
static const float INT16_SCALAR = 3.051757812e-5f;
static const float INT24_SCALAR = 1.192092896e-7f;
static const float INT32_SCALAR = 4.656612873e-10;

// Helper function that reset the stream file pointer
static bool stack_audio_file_reset_stream(GFileInputStream *stream)
{
	// Reset back to the start of the file
	g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);
	return false;
}

// Creates a new StackAudioFile object from the supplied file
StackAudioFile *stack_audio_file_create(const char *filename)
{
	StackAudioFile* result = NULL;

	// Open the file
	GFile *file = NULL;
	if (filename[0] == '/')
	{
		file = g_file_new_for_path(filename);
	}
	else
	{
		file = g_file_new_for_uri(filename);
	}

	if (file == NULL)
	{
		stack_log("stack_audio_file_create(): Failed to open file\n");
	    return NULL;
	}

	// Open a stream
	GFileInputStream *stream = g_file_read(file, NULL, NULL);
	if (stream == NULL)
	{
		stack_log("stack_audio_file_create(): Failed to get file input stream\n");
	    g_object_unref(file);
	    return NULL;
	}

	// Attempt to load as the various types
	if (!((result = (StackAudioFile*)stack_audio_file_create_wave(stream))
		  || stack_audio_file_reset_stream(stream)
#if HAVE_VORBISFILE == 1
		  || (result = (StackAudioFile*)stack_audio_file_create_ogg(stream))
#endif
#if HAVE_LIBFLAC == 1
		  || (result = (StackAudioFile*)stack_audio_file_create_flac(stream))
#endif
#if HAVE_LIBMAD == 1 /* note that we do MP3 last as it doesn't always necessarily have a standard header */
		  || (result = (StackAudioFile*)stack_audio_file_create_mp3(stream))
#endif
		))
	{
		stack_log("stack_audio_file_create(): Failed to load file as any format\n");
		g_object_unref(stream);
		g_object_unref(file);
	}
	else
	{
		// We set these, we leave the subclasses to set the others
		result->file = file;
		result->stream = stream;
	}

	return result;
}

// Closes the audio file and destroys the object
void stack_audio_file_destroy(StackAudioFile *audio_file)
{
	// Tidy up common data
	g_object_unref(audio_file->stream);
	g_object_unref(audio_file->file);

	// Destroy audio_file depending on what format was returned on creation
	switch (audio_file->format)
	{
		case STACK_AUDIO_FILE_FORMAT_WAVE:
			stack_audio_file_destroy_wave((StackAudioFileWave*)audio_file);
			break;
#if HAVE_VORBISFILE == 1
		case STACK_AUDIO_FILE_FORMAT_OGG:
			stack_audio_file_destroy_ogg((StackAudioFileOgg*)audio_file);
			break;
#endif
#if HAVE_LIBFLAC == 1
		case STACK_AUDIO_FILE_FORMAT_FLAC:
			stack_audio_file_destroy_flac((StackAudioFileFLAC*)audio_file);
			break;
#endif
#if HAVE_LIBMAD == 1
		case STACK_AUDIO_FILE_FORMAT_MP3:
			stack_audio_file_destroy_mp3((StackAudioFileMP3*)audio_file);
			break;
#endif
	}
}

// Seeks to the specific time in the file
void stack_audio_file_seek(StackAudioFile *audio_file, stack_time_t pos)
{
	switch (audio_file->format)
	{
		case STACK_AUDIO_FILE_FORMAT_WAVE:
			stack_audio_file_seek_wave((StackAudioFileWave*)audio_file, pos);
			break;
#if HAVE_VORBISFILE == 1
		case STACK_AUDIO_FILE_FORMAT_OGG:
			stack_audio_file_seek_ogg((StackAudioFileOgg*)audio_file, pos);
			break;
#endif
#if HAVE_LIBFLAC == 1
		case STACK_AUDIO_FILE_FORMAT_FLAC:
			stack_audio_file_seek_flac((StackAudioFileFLAC*)audio_file, pos);
			break;
#endif
#if HAVE_LIBMAD == 1
		case STACK_AUDIO_FILE_FORMAT_MP3:
			stack_audio_file_seek_mp3((StackAudioFileMP3*)audio_file, pos);
			break;
#endif
		default:
			stack_log("stack_audio_file_seek(): Unknown file format\n");
			break;
	}
}

// Read the requested number of frames from the file into buffer
size_t stack_audio_file_read(StackAudioFile *audio_file, float *buffer, size_t frames)
{
	switch (audio_file->format)
	{
		case STACK_AUDIO_FILE_FORMAT_WAVE:
			return stack_audio_file_read_wave((StackAudioFileWave*)audio_file, buffer, frames);
#if HAVE_VORBISFILE == 1
		case STACK_AUDIO_FILE_FORMAT_OGG:
			return stack_audio_file_read_ogg((StackAudioFileOgg*)audio_file, buffer, frames);
#endif
#if HAVE_LIBFLAC == 1
		case STACK_AUDIO_FILE_FORMAT_FLAC:
			return stack_audio_file_read_flac((StackAudioFileFLAC*)audio_file, buffer, frames);
#endif
#if HAVE_LIBMAD == 1
		case STACK_AUDIO_FILE_FORMAT_MP3:
			return stack_audio_file_read_mp3((StackAudioFileMP3*)audio_file, buffer, frames);
#endif
		default:
			stack_log("stack_audio_file_read(): Unknown file format\n");
			return -1;
	}
}

// TODO: The following functions assume the endianness of the system and the
// contents of the file are the same... and in the case of int24, assumes that
// that *both* are Big Endian
static bool stack_audio_file_convert_int24(const char *input, size_t samples, float *output)
{
	for (size_t i = 0; i < samples; i++)
	{
		int32_t sample = (*(int32_t*)&input[i * 3] & 0x00ffffff);
		if (sample & 0x00800000) { sample |= sample | 0xff000000; }
		output[i] = (float)sample * INT24_SCALAR;
	}

	return true;
}

template <typename T> bool stack_audio_file_convert_trivial(const T *input, const size_t samples, float *output, const float scalar)
{
	for (size_t i = 0; i < samples; i++)
	{
		output[i] = (float)((T*)input)[i] * scalar;
	}

	return true;
}

// Converts audio data to system-endian floating point
bool stack_audio_file_convert(StackSampleFormat format, const void *input, const size_t samples, float *output)
{
	switch (format)
	{
		case STACK_SAMPLE_FORMAT_INT8:
			return stack_audio_file_convert_trivial<int8_t>((int8_t*)input, samples, output, INT8_SCALAR);
		case STACK_SAMPLE_FORMAT_INT16:
			return stack_audio_file_convert_trivial<int16_t>((int16_t*)input, samples, output, INT16_SCALAR);
		case STACK_SAMPLE_FORMAT_INT24:
			return stack_audio_file_convert_int24((char*)input, samples, output);
		case STACK_SAMPLE_FORMAT_INT32:
			return stack_audio_file_convert_trivial<int32_t>((int32_t*)input, samples, output, INT32_SCALAR);
		case STACK_SAMPLE_FORMAT_FLOAT32:
			memcpy(output, input, sizeof(float) * samples);
			return true;
		case STACK_SAMPLE_FORMAT_FLOAT64:
			return stack_audio_file_convert_trivial<double>((double*)input, samples, output, 1.0f);
		default:
			return false;
	}

	return true;
}

uint64_t stack_time_to_samples(stack_time_t t, uint32_t sample_rate)
{
	uint64_t whole_seconds = t / NANOSECS_PER_SEC;
	uint64_t remainder_nanosec = t % NANOSECS_PER_SEC;
	return (whole_seconds * (uint64_t)sample_rate) + (remainder_nanosec * (uint64_t)sample_rate / NANOSECS_PER_SEC);

	// The above is a less integer-overflow-inducing (albeit slightly slower)
	// way of doing:
	// return t * sample_rate / NANOSECS_PER_SEC;
}

uint64_t stack_time_to_bytes(stack_time_t t, uint32_t sample_rate, uint32_t frame_size)
{
	return stack_time_to_samples(t, sample_rate) * (uint64_t)frame_size;
}
