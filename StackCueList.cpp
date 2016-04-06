// Includes:
#include "StackCue.h"
#include <list>
#include <cstring>
#include <json/json.h>
using namespace std;

typedef list<StackCue*> stackcue_list_t;
typedef list<StackCue*>::iterator stackcue_list_iterator_t;

#define SCL_GET_LIST(_scl) ((stackcue_list_t*)(((StackCueList*)(_scl))->cues))

StackCueList *stack_cue_list_new(uint16_t channels)
{
	StackCueList *cue_list = new StackCueList();
	
	// Empty variables
	cue_list->audio_device = NULL;
	
	// Default to a two-channel set up
	cue_list->channels = channels;
	
	// Initialise a list
	cue_list->cues = (void*)new stackcue_list_t();
	
	// Allocate our buffer (fixed at 32k samples size for now)
	cue_list->buffer_len = 32768;
	cue_list->buffer = new float[channels * cue_list->buffer_len];
	memset(cue_list->buffer, 0, channels * cue_list->buffer_len * sizeof(float));
	cue_list->buffer_idx = 0;
	cue_list->buffer_time = 0;
	
	return cue_list;
}

void stack_cue_list_destroy(StackCueList *cue_list)
{
	// Destroy the audio device
	if (cue_list->audio_device)
	{
		stack_audio_device_destroy(cue_list->audio_device);
	}
	
	// Tidy up memory
	delete [] cue_list->buffer;
	delete (stackcue_list_t*)cue_list->cues;
}

size_t stack_cue_list_write_audio(StackCueList *cue_list, size_t ptr, float *data, uint16_t channels, size_t samples, bool interleaved)
{
	// data should contain [channels * samples] audio samples
	
	// If the caller doesn't know where to write to, just write to the read pointer
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
			// Calculate source and destination pointers
			float *src = data;
			float *dst = &cue_list->buffer[(ptr * cue_list->channels)];
			size_t count = channels * samples;

			for (size_t i = 0; i < count; i++)
			{
				*dst += *src;
				
				// Increment source pointer
				src++;
				
				// Increment destination point
				dst++;
			}
		}
		else
		{
			for (size_t ch = 0; ch < channels; ch++)
			{
				// Calculate source and destination pointers
				float *src = &data[ch * samples];
				float *dst = &cue_list->buffer[(ptr * cue_list->channels) + ch];
				
				// Copy one channels worth of data
				for (size_t i = 0; i < samples; i++)
				{
					*dst += *src;
					
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
			float *src = data;
			float *dst = &cue_list->buffer[(ptr * cue_list->channels)];
			size_t count = channels * (cue_list->buffer_len - ptr);
			for (size_t i = 0; i < count; i++)
			{
				*dst += *src;
				
				// Increment source pointer
				src++;
				
				// Increment destination point
				dst++;
			}
			
			// Copy the rest to the beginning
			src = &data[channels * (cue_list->buffer_len - ptr)];
			dst = cue_list->buffer;
			count = channels * (samples - (cue_list->buffer_len - ptr));
			for (size_t i = 0; i < count; i++)
			{
				*dst += *src;
				
				// Increment source pointer
				src++;
				
				// Increment destination point
				dst++;
			}
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
	static stack_time_t underflow_time = 0;
	
	// Get a single clock time for all the cues
	stack_time_t clocktime = stack_get_clock_time();
	
	// Iterate over all the cues
	for (auto iter = SCL_GET_LIST(cue_list)->begin(); iter != SCL_GET_LIST(cue_list)->end(); ++iter)
	{
		if ((*iter)->state >= STACK_CUE_STATE_PLAYING_PRE && (*iter)->state <= STACK_CUE_STATE_PLAYING_POST)
		{
			stack_cue_pulse(*iter, clocktime);
		}
	}

	// If we don't have an audio device configured, don't attempt to read audio
	// buffer and write out to the device
	if (cue_list->audio_device == NULL)
	{
		return;
	}
	
	// Initial clock setup when we're ready on the first pulse
	if (cue_list->buffer_time == 0)
	{
		cue_list->buffer_time = clocktime;
	}
		
	size_t index, byte_count;
	size_t block_size_samples = 1024;
	stack_time_t block_size_time = ((stack_time_t)block_size_samples * NANOSECS_PER_SEC) / (stack_time_t)cue_list->audio_device->sample_rate;
	
	// Write data to the audio streams if necessary (i.e. if there's less than 2x our block size in the buffer)
	if (clocktime > cue_list->buffer_time - block_size_time * 2)
	{
		// A test attempt at underflow detection
		if (clocktime > cue_list->buffer_time)
		{
			underflow_time += clocktime - cue_list->buffer_time;
			fprintf(stderr, "UNDERFLOW: %ldus (total: %ldus)\n", (clocktime - cue_list->buffer_time) / 1000, underflow_time / 1000);
		}
		
		// See if we can write an entire memory block at once
		if (cue_list->buffer_idx + block_size_samples < cue_list->buffer_len)
		{	
			// Calculate data index and size
			index = cue_list->buffer_idx * cue_list->channels;
			byte_count = block_size_samples * cue_list->channels * sizeof(float);
			
			// Write to device
			stack_audio_device_write(cue_list->audio_device, (const char*)&cue_list->buffer[index], byte_count);
			
			// Wipe buffer
			memset(&cue_list->buffer[index], 0, byte_count);
		}
		else
		{
			// Calculate data index and size
			size_t index = cue_list->buffer_idx * cue_list->channels;
			size_t byte_count = (cue_list->buffer_len - cue_list->buffer_idx) * cue_list->channels * sizeof(float);

			// Write the stuff that's at the end of the memory block
			stack_audio_device_write(cue_list->audio_device, (const char*)&cue_list->buffer[index], byte_count);
			
			// Wipe buffer at the end
			memset(&cue_list->buffer[index], 0, byte_count);

			// Calculate remaining size
			byte_count = (block_size_samples - (cue_list->buffer_len - cue_list->buffer_idx)) * cue_list->channels * sizeof(float);

			// Write stuff that's at the beginning of the memory block
			stack_audio_device_write(cue_list->audio_device, (const char*)cue_list->buffer, byte_count);

			// Wipe buffer at the beginning
			memset(cue_list->buffer, 0, byte_count);
		}
		
		// Increment ring buffer and wrap around
		cue_list->buffer_idx = (cue_list->buffer_idx + block_size_samples) % cue_list->buffer_len;
		cue_list->buffer_time += ((stack_time_t)block_size_samples * NANOSECS_PER_SEC) / cue_list->audio_device->sample_rate;
	}	
}

void stack_cue_list_lock(StackCueList *cue_list)
{
	cue_list->lock.lock();
}

void stack_cue_list_unlock(StackCueList *cue_list)
{
	cue_list->lock.unlock();
}

bool stack_cue_list_save(StackCueList *cue_list, const char *uri)
{
	// TODO: Better error checking
	
	// Open the file
	GFile *file = g_file_new_for_uri(uri);
	if (file == NULL)
	{
		return false;
	}
	
	// Get a write strea
	GFileOutputStream *stream = g_file_replace(file, NULL, false, G_FILE_CREATE_NONE, NULL, NULL);
	if (stream == NULL)
	{
		g_object_unref(file);
		return false;
	}
	
	Json::Value root;
	root["show_name"] = "";
	root["designer"] = "";
	root["channels"] = cue_list->channels;
	root["cues"] = Json::Value(Json::ValueType::arrayValue);
	
	// Iterate over all the cues
	for (auto iter = SCL_GET_LIST(cue_list)->begin(); iter != SCL_GET_LIST(cue_list)->end(); ++iter)
	{
		// Get the JSON representation of the cue
		Json::Value cue_root;
		Json::Reader reader;
		char *cue_json_data = stack_cue_to_json(*iter);
		reader.parse(cue_json_data, cue_root);
		stack_cue_free_json(cue_json_data);
		
		// Add it to the cues entry
		root["cues"].append(cue_root);
	}

	Json::StyledWriter writer;
	std::string output = writer.write(root);

	// Write to the output stream
	g_output_stream_printf(G_OUTPUT_STREAM(stream), NULL, NULL, NULL, "%s", output.c_str());
	
	// Tidy up
	g_object_unref(stream);
	g_object_unref(file);
	
	return true;
}

