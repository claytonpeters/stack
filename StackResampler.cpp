#if HAVE_LIBSOXR == 1
// Includes:
#include "StackResampler.h"
#include "StackLog.h"
#include <cmath>
#include <cstdio>

StackResampler *stack_resampler_create(double input_sample_rate, double output_sample_rate, size_t channels)
{
	StackResampler *result = new StackResampler;
	result->input_sample_rate = input_sample_rate;
	result->output_sample_rate = output_sample_rate;
	result->channels = channels;
	result->resample_buffer = NULL;
	result->resample_buffer_size = 0;

	// Setup SOXR (these are currently the SOXR defaults)
	soxr_io_spec_t io_spec = {
		SOXR_FLOAT32_I,	// Input data type
		SOXR_FLOAT32_I, // Output data type
		1.0,			// Linear gain
		NULL,			// Internal use
		0				// Flags
	};
	soxr_quality_spec_t quality_spec = soxr_quality_spec(SOXR_HQ, 0);
	soxr_runtime_spec_t runtime_spec = soxr_runtime_spec(1);

	soxr_error_t error;
	result->soxr = soxr_create(input_sample_rate, output_sample_rate, channels, &error, &io_spec, &quality_spec, &runtime_spec);

	if (error)
	{
		stack_log("stack_resample_create(): Failed to initialise SOXR resampler: %s\n", soxr_strerror(error));
		delete result;
		return NULL;
	}

	// Create an output buffer
	result->output_buffer = stack_ring_buffer_create(8192 * channels);

	return result;
}

void stack_resampler_destroy(StackResampler *resampler)
{
	soxr_delete(resampler->soxr);
	stack_ring_buffer_destroy(resampler->output_buffer);
	if (resampler->resample_buffer != NULL)
	{
		delete [] resampler->resample_buffer;
	}

	delete resampler;
}

size_t stack_resampler_push(StackResampler *resampler, float *input, size_t input_frames)
{
	// Figure out how many output frames we'll get for our input frames
	const size_t output_frames = (size_t)ceil((double)input_frames * (resampler->output_sample_rate / resampler->input_sample_rate));

	// Re-allocate the buffer if we need to
	if (resampler->resample_buffer_size < output_frames)
	{
		if (resampler->resample_buffer != NULL)
		{
			delete [] resampler->resample_buffer;
		}

		resampler->resample_buffer = new float[output_frames * resampler->channels];
		resampler->resample_buffer_size = output_frames;
	}

	// Perform the resampling
	size_t done = 0;
	soxr_error_t error;
	error = soxr_process(resampler->soxr, input, input_frames, NULL, resampler->resample_buffer, resampler->resample_buffer_size, &done);

	if (error)
	{
		stack_log("stack_resampler_resample(): Failed to resample: %s\n", soxr_strerror(error));
	}

	// Write the data to the ring buffer
	stack_ring_buffer_write(resampler->output_buffer, resampler->resample_buffer, done * resampler->channels, 1);

	// Return the new size of the ring buffer
	return resampler->output_buffer->used / resampler->channels;
}

// Returns the number of frames in the (multiplexed) ring buffer
size_t stack_resampler_get_buffered_size(StackResampler *resampler)
{
	return resampler->output_buffer->used / resampler->channels;
}

// Get data from the ring buffer
size_t stack_resampler_get_frames(StackResampler *resampler, float *buffer, size_t frames)
{
	return stack_ring_buffer_read(resampler->output_buffer, buffer, frames * resampler->channels, 1) / resampler->channels;
}
#endif
