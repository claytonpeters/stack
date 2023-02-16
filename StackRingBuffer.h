#ifndef _STACKRINGBUFFER_H_INCLUDED
#define _STACKRINGBUFFER_H_INCLUDED

#include <unistd.h>

typedef struct StackRingBuffer
{
	// The actual buffer of data
	float *start_pointer;

	// Pre-calculated as start_pointer + capacity
	float *end_pointer;

	// The amount of samples the buffer can hold
	size_t capacity;

	// The number of samples in the buffer
	size_t used;

	// Pointer to read data from
	float *read_pointer;

	// Pointer to write data to
	float *write_pointer;
} StackRingBuffer;

// Functions:

// Creates a new ring buffer with the given maximum 'capacity'
StackRingBuffer *stack_ring_buffer_create(size_t capacity);

// Destroys a ring buffer
void stack_ring_buffer_destroy(StackRingBuffer *buffer);

// Reads up to 'count' samples from the ring 'buffer' and stores them in 'data'.
// The 'stride' parameter specifies how much to increment the output pointer
// when copying out, which can be used to multiplex data in a destination. For
// non-multiplexed data, this should be set to 1. Returns the number of samples
// that were copied back to 'data'
size_t stack_ring_buffer_read(StackRingBuffer *buffer, float *data, size_t count, size_t stride);

// Writes up to 'count' samples in to the ring 'buffer' from the given 'data'.
// The 'stride' parameter specifies how much to increment the input pointer
// when copying from 'data', which is useful if the source data is multiplexed.
// For non-multiplexed data, this should be set to 1. Returns the number of
// samples that were coied in to the ring buffer
size_t stack_ring_buffer_write(StackRingBuffer *buffer, const float *data, size_t count, size_t stride);

// Resets the ring buffer to empty
void stack_ring_buffer_reset(StackRingBuffer *buffer);

// Moves the read pointer forward by count samples, effectively skipping past
// some written data. Returns the amount of samples still available for reading
size_t stack_ring_buffer_skip(StackRingBuffer *buffer, size_t count);

#endif
