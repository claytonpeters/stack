#if HAVE_LIBSOXR == 1
#ifndef _STACKRESAMPLER_H_INCLUDED
#define _STACKRESAMPLER_H_INCLUDED

// Includes:
#include <soxr.h>
#include "StackRingBuffer.h"

struct StackResampler
{
	// The sample rate of the input data
	double input_sample_rate;

	// The sample rate of the resample data
	double output_sample_rate;

	// The number of channels in the source data
	size_t channels;

	// The SOXR resampler
	soxr_t soxr;

	// A ring buffer for our output data
	StackRingBuffer *output_buffer;

	// A temporary buffer for the output of a single input block
	float *resample_buffer;

	// The size of our resample buffer
	size_t resample_buffer_size;
};

// Functions:

// Create a new StackResampler object
StackResampler *stack_resampler_create(double input_sample_rate, double output_sample_rate, size_t channels);

// Destroy a StackResampler object
void stack_resampler_destroy(StackResampler *resampler);

// Push new data in to the resampler to be resampled
size_t stack_resampler_push(StackResampler *resampler, float *input, size_t input_frames);

// Get the number of resampled frames currently buffered
size_t stack_resampler_get_buffered_size(StackResampler *resampler);

// Take frames out of the resampled buffer
size_t stack_resampler_get_frames(StackResampler *resampler, float *buffer, size_t frames);

#endif
#endif
