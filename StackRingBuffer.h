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
StackRingBuffer *stack_ring_buffer_create(size_t capacity);
void stack_ring_buffer_destroy(StackRingBuffer *buffer);
size_t stack_ring_buffer_read(StackRingBuffer *buffer, float *data, size_t count, size_t stride);
size_t stack_ring_buffer_write(StackRingBuffer *buffer, const float *data, size_t count, size_t stride);

#endif
