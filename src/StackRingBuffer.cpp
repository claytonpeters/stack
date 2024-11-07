// Includes:
#include "StackRingBuffer.h"
#include <cstring>

StackRingBuffer *stack_ring_buffer_create(size_t capacity)
{
	StackRingBuffer *buffer = new StackRingBuffer;

	buffer->start_pointer = new float[capacity];
	buffer->end_pointer = &buffer->start_pointer[capacity];
	buffer->capacity = capacity;
	stack_ring_buffer_reset(buffer);

	return buffer;
}

void stack_ring_buffer_destroy(StackRingBuffer *buffer)
{
	delete [] buffer->start_pointer;
	delete buffer;
}

size_t stack_ring_buffer_read(StackRingBuffer *buffer, float *data, size_t count, size_t stride = 1)
{
	// Limit to the amount of data we actually have in the buffer
	if (count > buffer->used)
	{
		count = buffer->used;
	}

	// If our output buffer is continuous
	if (stride == 1)
	{
		// If we can copy the data out in one go
		if (buffer->read_pointer + count < buffer->end_pointer)
		{
			memcpy(data, buffer->read_pointer, sizeof(float) * count);
			buffer->read_pointer += count;
		}
		else
		{
			const size_t end_distance = buffer->end_pointer - buffer->read_pointer;
			memcpy(data, buffer->read_pointer, sizeof(float) * end_distance);
			memcpy(data + end_distance, buffer->start_pointer, sizeof(float) * (count - end_distance));
			buffer->read_pointer = buffer->start_pointer + count - end_distance;
		}
	}
	else
	{
		size_t end_distance = buffer->end_pointer - buffer->read_pointer;
		if (end_distance > count)
		{
			end_distance = count;
		}

		// Reading from current position to (up to) end of buffer
		for (size_t i = 0; i < end_distance; i++)
		{
			*data = *buffer->read_pointer++;
			data += stride;
		}
		// If we have reached the end of the buffer, jump back to the beginning
		if (buffer->read_pointer == buffer->end_pointer)
		{
			buffer->read_pointer = buffer->start_pointer;
		}
		// Write to the start
		for (size_t i = end_distance; i < count; i++)
		{
			*data = *buffer->read_pointer++;
			data += stride;
		}
	}

	buffer->used -= count;
	return count;
}

size_t stack_ring_buffer_write(StackRingBuffer *buffer, const float *data, size_t count, size_t stride = 1)
{
	// If the amount of data is more than the capacity, then we only need to write
	// a maximum of 'capacity' samples from the end of the buffer
	if (count > buffer->capacity)
	{
		data = data + count - buffer->capacity;
		count = buffer->capacity;
	}

	// If our input data is continuous
	if (stride == 1)
	{
		// If we can write the whole chunk in one go
		if (buffer->write_pointer + count < buffer->end_pointer)
		{
			memcpy(buffer->write_pointer, data, sizeof(float) * count);
			buffer->write_pointer += count;
		}
		else
		{
			const size_t end_distance = buffer->end_pointer - buffer->write_pointer;
			memcpy(buffer->write_pointer, data, sizeof(float) * end_distance);
			memcpy(buffer->start_pointer, data + end_distance, sizeof(float) * (count - end_distance));
			buffer->write_pointer = buffer->start_pointer + count - end_distance;
		}
	}
	else
	{
		size_t end_distance = buffer->end_pointer - buffer->write_pointer;
		if (end_distance > count)
		{
			end_distance = count;
		}

		// Writing from current position to (up to) end of buffer
		for (size_t i = 0; i < end_distance; i++)
		{
			*buffer->write_pointer++ = *data;
			data += stride;
		}
		// If we have reached the end of the buffer, jump back to the beginning
		if (buffer->write_pointer == buffer->end_pointer)
		{
			buffer->write_pointer = buffer->start_pointer;
		}
		// Write to the start
		for (size_t i = end_distance; i < count; i++)
		{
			*buffer->write_pointer++ = *data;
			data += stride;
		}
	}

	buffer->used += count;
	if (buffer->used > buffer->capacity)
	{
		buffer->used = buffer->capacity;
	}

	return count;
}

void stack_ring_buffer_reset(StackRingBuffer *buffer)
{
	buffer->used = 0;
	buffer->read_pointer = buffer->start_pointer;
	buffer->write_pointer = buffer->start_pointer;
}

size_t stack_ring_buffer_skip(StackRingBuffer *buffer, size_t count)
{
	// If we're skipping more than we currently have in the buffer, it's easier
	// to just reset entirely
	if (count >= buffer->used)
	{
		stack_ring_buffer_reset(buffer);
		return 0;
	}

	// Move the write pointer forward
	buffer->read_pointer += count;

	// If the write pointer is now past the end of the buffer, then subtract the
	// length of the buffer to bring us back inside and to the right position
	// relative to the start. We need not modulus this as we know count must be
	// less than capacity, because count has already been checked to be less
	// than used in the if statement above
	if (buffer->read_pointer > buffer->end_pointer)
	{
		buffer->read_pointer -= buffer->capacity;
	}

	// Update how much data is left
	buffer->used -= count;

	return buffer->used;
}
