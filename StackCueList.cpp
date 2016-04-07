// Includes:
#include "StackCue.h"
#include <list>
#include <map>
#include <cstring>
#include <json/json.h>
using namespace std;

typedef list<StackCue*> stackcue_list_t;
typedef list<StackCue*>::iterator stackcue_list_iterator_t;
typedef map<cue_uid_t, cue_uid_t> uid_remap_t;

#define SCL_GET_LIST(_scl) ((stackcue_list_t*)(((StackCueList*)(_scl))->cues))
#define SCL_UID_REMAP(_scl) (*((uid_remap_t*)(_scl)))

/// Creates a new cue list
/// @param channels The number of audio channels to support
StackCueList *stack_cue_list_new(uint16_t channels)
{
	StackCueList *cue_list = new StackCueList();
	
	// Empty variables
	cue_list->audio_device = NULL;
	cue_list->changed = false;
	cue_list->uri = NULL;
	
	// Default to a two-channel set up
	cue_list->channels = channels;
	
	// Initialise a list
	cue_list->cues = (void*)new stackcue_list_t();
	
	// Initialise a remap map
	cue_list->uid_remap = (void*)new uid_remap_t();
	
	// Allocate our buffer (fixed at 32k samples size for now)
	cue_list->buffer_len = 32768;
	cue_list->buffer = new float[channels * cue_list->buffer_len];
	memset(cue_list->buffer, 0, channels * cue_list->buffer_len * sizeof(float));
	cue_list->buffer_idx = 0;
	cue_list->buffer_time = 0;
	
	return cue_list;
}

/// Destroys a cue list, freeing up any associated memory
/// @param cue_list The cue list to destroy
void stack_cue_list_destroy(StackCueList *cue_list)
{
	if (cue_list)
	{
		// Destroy the audio device
		if (cue_list->audio_device)
		{
			stack_audio_device_destroy(cue_list->audio_device);
		}

		// If we've got a URI, free that
		if (cue_list->uri)
		{	
			free(cue_list->uri);
		}

		// Tidy up memory
		delete [] cue_list->buffer;
		delete (stackcue_list_t*)cue_list->cues;
		delete (uid_remap_t*)cue_list->uid_remap;
		delete cue_list;
	}
	else
	{
		fprintf(stderr, "stack_cue_list_destroy(): Attempted to destroy NULL!\n");
	}
}

/// Writes audio to the cue lists audio buffer
/// @param cue_list The cue list whose buffer is to be written to
/// @param ptr The index within the buffer to write to (-1 to write at the read pointer)
/// @param data The data to write
/// @param channels The number of channels in the input data
/// @param samples The number of samples in the input data
/// @param interleaved If true, then the input data is interleaved as one sample per channel. If false then the data is 'samples' samples of each channel
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

/// Returns the number of cues in the cue list
/// @param cue_list The cue list
size_t stack_cue_list_count(StackCueList *cue_list)
{
	return SCL_GET_LIST(cue_list)->size();
}

/// Appends a cue in the cue list
/// @param cue_list The cue list
/// @param cue The cue to append.
void stack_cue_list_append(StackCueList *cue_list, StackCue *cue)
{
	SCL_GET_LIST(cue_list)->push_back(cue);
}

/// Returns an iterator to the front of the cue list
/// @param cue_list The cue list
/// @returns An iterator
void *stack_cue_list_iter_front(StackCueList *cue_list)
{
	stackcue_list_iterator_t* result = new stackcue_list_iterator_t;
	*result	= (SCL_GET_LIST(cue_list)->begin());
	return result;
}

/// Increments an interator to the next cue
/// @param iter A cue list iterator as returned by stack_cue_list_iter_front for example
void *stack_cue_list_iter_next(void *iter)
{
	return (void*)&(++(*(stackcue_list_iterator_t*)(iter)));
}

/// Gets the cue at the current location of the iterator
/// @param iter A cue list iterator as returned by stack_cue_list_iter_front/next
StackCue *stack_cue_list_iter_get(void *iter)
{
	return *(*(stackcue_list_iterator_t*)(iter));
}

/// Frees a cue list iterator as returned by stack_cue_list_iter_front
/// @param iter A cue list iterator
void stack_cue_list_iter_free(void *iter)
{
	delete (stackcue_list_iterator_t*)iter;
}

/// Determines if the iterator is currently at the end of the cue list
/// @param cue_list The cue list
/// @param iter A cue list iterator
bool stack_cue_list_iter_at_end(StackCueList *cue_list, void *iter)
{
	return (*(stackcue_list_iterator_t*)(iter)) == SCL_GET_LIST(cue_list)->end();
}

/// Pulses all the cues in the cue list so that they may do their time-based operations
/// @param cue_list The cue list
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

/// Locks a mutex for the cue list
/// @param cue_list The cue list
void stack_cue_list_lock(StackCueList *cue_list)
{
	cue_list->lock.lock();
}

/// Unlocks a mutex for the cue list
/// @param cue_list The cue list
void stack_cue_list_unlock(StackCueList *cue_list)
{
	cue_list->lock.unlock();
}

/// Saves the cue list to a file
/// @param cue_list The cue list
/// @param uri The URI of the path to save to (e.g. file:///home/blah/test.stack)
/// @returns A boolean indicating whether the save was successful
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
	
	// We've saved the cue list, so set it to not have been changed
	cue_list->changed = false;
	
	return true;
}

/// Creates a new cue list using the contents of a file
/// @param uri The URI of the path to read from (e.g. file:///home/blah/test.stack)
/// @returns A new cue list or NULL on error
StackCueList *stack_cue_list_new_from_file(const char *uri)
{
	// Open the file
	GFile *file = g_file_new_for_uri(uri);
	if (file == NULL)
	{
		fprintf(stderr, "stack_cue_list_new_from_file(): Failed to open file\n");
		return NULL;
	}
	
	// Get a read stream
	GFileInputStream *stream = g_file_read(file, NULL, NULL);
	if (stream == NULL)
	{
		fprintf(stderr, "stack_cue_list_new_from_file(): Failed to get file stream\n");	
		g_object_unref(file);
		return NULL;
	}

	// Query the file info (to get the file size)
	GFileInfo* file_info = g_file_query_info(file, "standard::size", G_FILE_QUERY_INFO_NONE, NULL, NULL);
	if (file_info == NULL)
	{
		fprintf(stderr, "stack_cue_list_new_from_file(): Failed to get file information\n");	
		g_object_unref(stream);
		g_object_unref(file);
		return NULL;
	}

	// Get the file size
	goffset size = g_file_info_get_size(file_info);

	// Allocate memory
	std::vector<char> json_buffer(size + 1);
	
	// Read the file in to the buffer
	if (g_input_stream_read(G_INPUT_STREAM(stream), &json_buffer[0], size, NULL, NULL) != size)
	{
		fprintf(stderr, "stack_cue_list_new_from_file(): Failed to read file\n");
		g_object_unref(stream);
		g_object_unref(file);
		return NULL;
	}
	
	// Null-terminate the string
	json_buffer[size] = '\0';

	// We don't need the file any more, tidy up
	g_object_unref(stream);
	g_object_unref(file);
	
	// Parse the JSON
	Json::Reader reader;
	Json::Value cue_list_root;
	reader.parse(&json_buffer[0], cue_list_root);

	// Validation: check we have 'channels'
	if (!cue_list_root.isMember("channels"))
	{
		fprintf(stderr, "stack_cue_list_new_from_file(): Missing 'channels' option\n");
		return NULL;
	}

	// Validation: check we have between 1 and 64 channels
	if (cue_list_root["channels"].asUInt() == 0 || cue_list_root["channels"].asUInt() > 64)
	{
		fprintf(stderr, "stack_cue_list_new_from_file(): Invalid number of 'channels' specified\n");
		return NULL;
	}
	
	// Generate a new cue list
	StackCueList *cue_list = stack_cue_list_new(cue_list_root["channels"].asUInt());

	// If we have some cues...
	if (cue_list_root.isMember("cues"))
	{
		Json::Value& cues_root = cue_list_root["cues"];
		
		if (cues_root.isArray())
		{
			// Iterate over the cues, creating their instances, and populating
			// just their base classes (we need to have built a UID map)
			for (auto iter = cues_root.begin(); iter != cues_root.end(); ++iter)
			{
				Json::Value& cue_json = *iter;
				
				// Make sure we have a class parameter
				if (!cue_json.isMember("class"))
				{
					cue_json["_skip"] = 1;
					fprintf(stderr, "stack_cue_list_from_file(): Cue missing 'class' parameter, skipping\n");
					continue;
				}
				
				// Make sure we have a base class
				if (!cue_json.isMember("StackCue"))
				{
					cue_json["_skip"] = 1;
					fprintf(stderr, "stack_cue_list_from_file(): Cue missing 'StackCue' class, skipping\n");
					continue;
				}
				
				// Make sure we have a UID
				if (!cue_json["StackCue"].isMember("uid"))
				{
					cue_json["_skip"] = 1;
					fprintf(stderr, "stack_cue_list_from_file(): Cue missing UID, skipping\n");
					continue;
				}
				
				// Create a new cue of the correct type
				const char *class_name = cue_json["class"].asString().c_str();
				StackCue *cue = stack_cue_new(class_name, cue_list);

				// Get the UID of the newly created cue and put a mapping from
				// the old UID to the new UID. Also store it in the JSON object
				// so that we can re-use it on the second loop
				SCL_UID_REMAP(cue_list->uid_remap)[cue_json["StackCue"]["uid"].asUInt64()] = cue->uid;
				cue_json["_new_uid"] = (Json::UInt64)cue->uid;
				
				// Ensure we made a cue
				if (cue == NULL)
				{
					cue_json["_skip"] = 1;
					fprintf(stderr, "stack_cue_from_file(): Failed to create cue of type '%s', skipping\n", cue_json["class"].asString().c_str());
					continue;
				}

				// Call base constructor
				stack_cue_from_json_base(cue, cue_json.toStyledString().c_str());
				
				// Append the cue to the cue list
				stack_cue_list_append(cue_list, cue);
			}

			// Iterate over the cues again calling their actual constructor
			for (auto iter = cues_root.begin(); iter != cues_root.end(); ++iter)
			{
				Json::Value& cue_json = *iter;
				
				// Skip over cues we skipped because of errors last time
				if (cue_json.isMember("_skip"))
				{
					continue;
				}
				
				// Call the appropriate overloaded function
				stack_cue_from_json(stack_cue_get_by_uid(cue_json["_new_uid"].asUInt64()), cue_json.toStyledString().c_str());
			}
		}
		else
		{
			fprintf(stderr, "stack_cue_list_new_from_file(): 'cues' is not an array\n");
		}
	}
	else
	{
		fprintf(stderr, "stack_cue_list_new_from_file(): Missing 'cues' option\n");
	}
	
	// Flag that the cue list hasn't changed
	cue_list->changed = false;
	
	// Store the URI of the file we opened
	cue_list->uri = strdup(uri);
	
	return cue_list;
}

/// Returns the new UID of a cue after it's been remapped during opening
/// @param cue_list The cue list
/// @param old_uid The old UID that exists in the file we opened
cue_uid_t stack_cue_list_remap(StackCueList *cue_list, cue_uid_t old_uid)
{
	return SCL_UID_REMAP(cue_list->uid_remap)[old_uid];
}

/// Called by child cues to let us know that something about them has changed
/// @param cue_list The cue list
/// @param cue The cue that caused the change (currently unused)
void stack_cue_list_changed(StackCueList *cue_list, StackCue *cue)
{
	cue_list->changed = true;
}

/// Removes a cue from the cue list
/// @param cue_list The cue list
/// @param cue The cue to remove
void stack_cue_list_remove(StackCueList *cue_list, StackCue *cue)
{
	// Iterate over all the cues
	for (auto iter = SCL_GET_LIST(cue_list)->begin(); iter != SCL_GET_LIST(cue_list)->end(); ++iter)
	{
		// If we've found the cue
		if (*iter == cue)
		{
			// Erase it
			SCL_GET_LIST(cue_list)->erase(iter);
			
			// Note that the cue list has been modified
			stack_cue_list_changed(cue_list, cue);
			
			// Stop searching
			return;
		}
	}
}

/// Gets the cue that follows 'cue' in the 'cue_list'
/// @param cue_list The cue list
/// @param cue The cue to find the following cue of
StackCue *stack_cue_list_get_cue_after(StackCueList *cue_list, StackCue *cue)
{
	// Iterate over all the cues
	for (auto iter = SCL_GET_LIST(cue_list)->begin(); iter != SCL_GET_LIST(cue_list)->end(); ++iter)
	{
		// If we've found the cue
		if (*iter == cue)
		{
			// Increment the iterator to the next cue
			++iter;
			
			// If we're now past the end of the cue list
			if (iter == SCL_GET_LIST(cue_list)->end())
			{
				return NULL;
			}
			else
			{
				return *iter;
			}
		}
	}
	
	return NULL;
}

