// Includes:
#include "StackCue.h"
#include <list>
#include <cstring>
using namespace std;

typedef list<StackCue*> stackcue_list_t;
typedef list<StackCue*>::iterator stackcue_list_iterator_t;

#define SCL_GET_LIST(_scl) ((stackcue_list_t*)(((StackCueList*)(_scl))->cues))

void stack_cue_list_init(StackCueList *cue_list, uint16_t channels)
{
	// Default to a two-channel set up
	cue_list->channels = channels;
	
	// Initialise a list
	cue_list->cues = (void*)new stackcue_list_t();
	
	// Allocate our buffer (fixed at 32k samples size for now)
	cue_list->buffer_len = 262144;
	cue_list->buffer = new int16_t[channels * cue_list->buffer_len];
	memset(cue_list->buffer, 0, channels * cue_list->buffer_len);
	cue_list->buffer_idx = 0;
	cue_list->buffer_time = 0;
}

void stack_cue_list_destroy(StackCueList *cue_list)
{
	delete [] cue_list->buffer;
	delete (stackcue_list_t*)cue_list->cues;
}

size_t stack_cue_list_write_audio(StackCueList *cue_list, size_t ptr, int16_t *data, uint16_t channels, size_t samples, bool interleaved)
{
	// data should contain [channels * samples] audio samples
	
	// TODO: THIS ONLY WORKS FOR A SINGLE STREAM CURRENTLY!!!!
	
	// If the caller doesn't know where to write to
	if (ptr == -1)
	{
		ptr = (cue_list->buffer_idx) % cue_list->buffer_len;
	}
	
	// If the entire data set fits at the current start of our ring buffer
	if (ptr + samples < cue_list->buffer_len)
	{
		// We want interleaved data in our ring buffer, so if we have
		// interleaved data, we can just copy it in
		if (interleaved)
		{
			memcpy(&cue_list->buffer[ptr * cue_list->channels], data, channels * samples * sizeof(int16_t));
		}
		else
		{
			for (size_t ch = 0; ch < channels; ch++)
			{
				// Calculate source and destination pointers
				int16_t *src = &data[ch * samples];
				int16_t *dst = &cue_list->buffer[(ptr * cue_list->channels) + ch];
				
				// Copy one channels worth of data
				for (size_t i = 0; i < samples; i++)
				{
					*dst = *src;
					
					// Increment source pointer
					src++;
					
					// Increment destination point
					*dst += channels;
				}
			}
		}
	}
	else
	{
		if (interleaved)
		{
			// Copy whatever fits at the end of our ring buffer
			memcpy(&cue_list->buffer[ptr * cue_list->channels], data, channels * (cue_list->buffer_len - ptr) * sizeof(int16_t));
			
			// Copy the rest to the beginning
			memcpy(cue_list->buffer, &data[channels * (cue_list->buffer_len - ptr)], channels * (samples - (cue_list->buffer_len - ptr)) * sizeof(int16_t));
		}
		else
		{
			// TODO: Not yet implemented
		}
	}

	// Return the new write pointer	
	return (ptr + samples) % cue_list->buffer_len;
}

size_t stack_cue_list_count(StackCueList *cue_list)
{
	return SCL_GET_LIST(cue_list)->size();
}

void stack_cue_list_append(StackCueList *cue_list, StackCue *cue)
{
	SCL_GET_LIST(cue_list)->push_back(cue);
}

void *stack_cue_list_iter_front(StackCueList *cue_list)
{
	stackcue_list_iterator_t* result = new stackcue_list_iterator_t;
	*result	= (SCL_GET_LIST(cue_list)->begin());
	return result;
}

void *stack_cue_list_iter_next(void *iter)
{
	return (void*)&(++(*(stackcue_list_iterator_t*)(iter)));
}

StackCue *stack_cue_list_iter_get(void *iter)
{
	return *(*(stackcue_list_iterator_t*)(iter));
}

void stack_cue_list_iter_free(void *iter)
{
	delete (stackcue_list_iterator_t*)iter;
}

bool stack_cue_list_iter_at_end(StackCueList *cue_list, void *iter)
{
	return (*(stackcue_list_iterator_t*)(iter)) == SCL_GET_LIST(cue_list)->end();
}

void stack_cue_list_pulse(StackCueList *cue_list)
{
	// Get a single clock time for all the cues
	stack_time_t clocktime = stack_get_clock_time();
	
	// Initial clock setup when we're ready on the first pulse
	if (cue_list->buffer_time == 0)
	{
		cue_list->buffer_time = clocktime;
	}
	
	// Iterate over all the cues
	for (auto iter = SCL_GET_LIST(cue_list)->begin(); iter != SCL_GET_LIST(cue_list)->end(); ++iter)
	{
		if ((*iter)->state >= STACK_CUE_STATE_PLAYING_PRE && (*iter)->state <= STACK_CUE_STATE_PLAYING_POST)
		{
			stack_cue_pulse(*iter, clocktime);
		}
	}
	
	// Write data to the audio streams if necessary (i.e. if there's less than 200ms left in the buffer)
	if (clocktime > cue_list->buffer_time)
	{
		//fprintf(stderr, "C: %ld, T: %ld, D: %ld, S: %lu\n", clocktime, cue_list->buffer_time, cue_list->buffer_time - clocktime, total_samps);
		
		size_t samples_out = 256;
		
		// See if we can write an entire memory block at once
		if (cue_list->buffer_idx + samples_out < cue_list->buffer_len)
		{	
			// Write to device
			stack_audio_device_write(cue_list->audio_device, (const char*)&cue_list->buffer[cue_list->buffer_idx * cue_list->channels], samples_out * cue_list->channels * sizeof(int16_t));
			
			// Wipe buffer
			memset(&cue_list->buffer[cue_list->buffer_idx * cue_list->channels], 0, samples_out * cue_list->channels * sizeof(int16_t));
		}
		else
		{
			// Write the stuff that's at the end of the memory block
			stack_audio_device_write(cue_list->audio_device, (const char*)&cue_list->buffer[cue_list->buffer_idx * cue_list->channels], (cue_list->buffer_len - cue_list->buffer_idx) * cue_list->channels * sizeof(int16_t));
			
			// Write stuff that's at the beginning of the memory block
			stack_audio_device_write(cue_list->audio_device, (const char*)cue_list->buffer, (samples_out - (cue_list->buffer_len - cue_list->buffer_idx)) * cue_list->channels * sizeof(int16_t));

			// Wipe buffer at the end
			memset(&cue_list->buffer[cue_list->buffer_idx * cue_list->channels], 0, (cue_list->buffer_len - cue_list->buffer_idx) * cue_list->channels * sizeof(int16_t));

			// Wipe buffer at the beginning
			memset(cue_list->buffer, 0, (samples_out - (cue_list->buffer_len - cue_list->buffer_idx)) * cue_list->channels * sizeof(int16_t));
		}
		
		// Increment ring buffer and wrap around
		cue_list->buffer_idx = (cue_list->buffer_idx + samples_out) % cue_list->buffer_len;
		cue_list->buffer_time += ((stack_time_t)samples_out * NANOSECS_PER_SEC) / cue_list->audio_device->sample_rate;
	}	
}
