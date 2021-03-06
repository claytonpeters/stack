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
	cue_list->state_change_func = NULL;
	cue_list->state_change_func_data = NULL;
	cue_list->show_name = strdup("");
	cue_list->show_designer = strdup("");
	cue_list->show_revision = strdup("");
	
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
	if (cue_list == NULL)
	{
		fprintf(stderr, "stack_cue_list_destroy(): Attempted to destroy NULL!\n");
		return;
	}

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

	// Tidy up show information
	if (cue_list->show_name)
	{
		free(cue_list->show_name);
	}
	if (cue_list->show_designer)
	{
		free(cue_list->show_designer);
	}
	if (cue_list->show_revision)
	{
		free(cue_list->show_revision);
	}

	// Tidy up memory
	delete [] cue_list->buffer;
	delete (stackcue_list_t*)cue_list->cues;
	delete (uid_remap_t*)cue_list->uid_remap;
	delete cue_list;
}

/// Writes one channel of audio data to the cue lists audio buffer. 'data' 
/// should contain 'samples' audio samples. If 'interleaving' is greater than 
/// one, then 'data' should contain 'samples * interleaving' samples, with only 
/// very 'interleaving' samples being used. 
/// @param cue_list The cue list whose buffer is to be written to
/// @param ptr The index within the buffer to write to (-1 to write at the read pointer)
/// @param data The data to write
/// @param channel The zero-based index of the output channel to write to
/// @param samples The number of samples in the input data
/// @param interleaved If greater than one, then the input data is interleaved as one sample per channel for this many channels. If 0 or 1, read 'samples' continuos samples.
size_t stack_cue_list_write_audio(StackCueList *cue_list, size_t ptr, uint16_t channel, float *data, size_t samples, uint16_t interleaving)
{
	uint16_t cue_list_channels = cue_list->channels;

	// If the caller doesn't know where to write to, just write to the read pointer
	if (ptr == -1)
	{
		ptr = (cue_list->buffer_idx) % cue_list->buffer_len;
	}
	
	// If interleaving is 0, the default to one sample
	if (interleaving == 0)
	{
		interleaving = 1;
	}

	// If the entire data set fits at the current start of our ring buffer
	if (ptr + samples < cue_list->buffer_len)
	{
		// Calculate source and destination pointers
		float *src = data;
		float *dst = &cue_list->buffer[(ptr * cue_list_channels) + channel];

		for (size_t i = 0; i < samples; i++)
		{
			*dst += *src;
			
			// Increment source pointer
			src += interleaving;
			
			// Increment destination point
			dst += cue_list_channels;
		}
	}
	else
	{
		// Copy whatever fits at the end of our ring buffer
		float *src = data;
		float *dst = &cue_list->buffer[(ptr * cue_list_channels) + channel];
		size_t count = cue_list->buffer_len - ptr;
		for (size_t i = 0; i < count; i++)
		{
			*dst += *src;
			
			// Increment source pointer
			src += interleaving;
			
			// Increment destination point
			dst += cue_list_channels;
		}
		
		// Copy the rest to the beginning
		src = &data[interleaving * (cue_list->buffer_len - ptr)];
		dst = &cue_list->buffer[channel];
		count = samples - count;
		for (size_t i = 0; i < count; i++)
		{
			*dst += *src;
			
			// Increment source pointer
			src += interleaving;
			
			// Increment destination point
			dst += cue_list_channels;
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
	cue_list->changed = true;
	SCL_GET_LIST(cue_list)->push_back(cue);
}

/// Moves an existing cue within the stack
/// @param cue_list The cue list
/// @param cue The cue to be moved
/// @param index The new position of the cue within the cue list. If greater than the length of the list, it will be put at the end
void stack_cue_list_move(StackCueList *cue_list, StackCue *cue, size_t index)
{
	stackcue_list_t* cues = SCL_GET_LIST(cue_list);
	bool found = false;

	// Search for the cue to move
	for (auto iter = cues->begin(); iter != cues->end(); ++iter)
	{
		if (*iter == cue)
		{
			// If found, erase it from the list temporarily and stop searching
			cue_list->changed = true;
			cues->erase(iter);
			found = true;
			break;
		}
	}

	// If we found the cue, but it back in at it's new location
	if (found)
	{
		// If it's past the end of the list, the append it
		if (index >= cues->size())
		{
			stack_cue_list_append(cue_list, cue);
		}
		else
		{
			// std::list requires an iterator for postion, so find one
			size_t i = 0;
			for (auto iter = cues->begin(); iter != cues->end(); ++iter)
			{
				if (i == index)
				{
					cues->insert(iter, cue);
					break;
				}
				else
				{
					i++;
				}
			}
		}
	}
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

/// Increments an iterator to the next cue
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
		// If the cue is in one of the playing states
		auto cue_state = (*iter)->state;	
		if (cue_state >= STACK_CUE_STATE_PLAYING_PRE && cue_state <= STACK_CUE_STATE_PLAYING_POST)
		{
			// Pulse the cue
			stack_cue_pulse(*iter, clocktime);
		}
	}

	// Track how long the cue pulses took
	stack_time_t cue_pulse_time = stack_get_clock_time() - clocktime;

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
	
	size_t index, byte_count, blocks_written = 0;
	size_t block_size_samples = 1024;
	stack_time_t block_size_time = ((stack_time_t)block_size_samples * NANOSECS_PER_SEC) / (stack_time_t)cue_list->audio_device->sample_rate;
	
	// Write data to the audio streams if necessary (i.e. if there's less than 3x our block size in the buffer)
	while (clocktime > cue_list->buffer_time - block_size_time * 3)
	{
		// Keep track of how many blocks we've written
		blocks_written++;

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
		cue_list->buffer_time += block_size_time;
	}	

	// Statistics: Gather how long it took to do this pulse
	stack_time_t pulse_time = stack_get_clock_time() - clocktime;
	//fprintf(stderr, "stack_cue_list_pulse(): Pulse took %lldns (of which cues: %lldns) (wrote %d blocks)\n", pulse_time, cue_pulse_time, blocks_written);
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
	root["show_name"] = cue_list->show_name;
	root["designer"] = cue_list->show_designer;
	root["revision"] = cue_list->show_revision;
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
/// @param callback The callback function to notify on progress
/// @param user_data Arbitrary data to pass to the callback function
/// @returns A new cue list or NULL on error
StackCueList *stack_cue_list_new_from_file(const char *uri, stack_cue_list_load_callback_t callback, void* user_data)
{
	if (callback)
	{
		callback(NULL, 0, "Opening file...", user_data);
	}

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
	
	if (callback)
	{
		callback(NULL, 0.01, "Reading file...", user_data);
	}

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
	if (callback)
	{
		callback(NULL, 0.03, "Parsing file...", user_data);
	}
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

	// Load show information
	if (cue_list_root.isMember("show_name"))
	{
		stack_cue_list_set_show_name(cue_list, cue_list_root["show_name"].asCString());
	}
	if (cue_list_root.isMember("designer"))
	{
		stack_cue_list_set_show_designer(cue_list, cue_list_root["designer"].asCString());
	}
	if (cue_list_root.isMember("revision"))
	{
		stack_cue_list_set_show_revision(cue_list, cue_list_root["revision"].asCString());
	}

	// If we have some cues...
	if (cue_list_root.isMember("cues"))
	{
		Json::Value& cues_root = cue_list_root["cues"];
		
		if (cues_root.isArray())
		{
			if (callback)
			{
				callback(NULL, 0.1, "Preparing cue list...", user_data);
			}

			// Iterate over the cues, creating their instances, and populating
			// just their base classes (we need to have built a UID map)
			int cue_count = 0;
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
				if (cue == NULL)
				{
					fprintf(stderr, "stack_cue_list_from_file(): Failed to create cue of type '%s', skipping\n", class_name);
					cue_json["_skip"] = 1;

					// TODO: It would be nice if we have some sort of "error cue" which
					// contained the JSON for the cue, so we didn't just drop cues from
					// the stack
					
					continue;
				}

				// Get the UID of the newly created cue and put a mapping from
				// the old UID to the new UID. Also store it in the JSON object
				// so that we can re-use it on the second loop
				SCL_UID_REMAP(cue_list->uid_remap)[cue_json["StackCue"]["uid"].asUInt64()] = cue->uid;
				cue_json["_new_uid"] = (Json::UInt64)cue->uid;
				
				// Call base constructor
				stack_cue_from_json_base(cue, cue_json.toStyledString().c_str());
				
				// Append the cue to the cue list
				stack_cue_list_append(cue_list, cue);
				cue_count++;
			}

			if (callback)
			{
				callback(cue_list, 0.2, "Initialising cues...", user_data);
			}

			// Iterate over the cues again calling their actual constructor
			int prepared_cues = 0;
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
				prepared_cues++;

				if (callback)
				{
					// We count the first 20% as parsing the cue list
					double progress = 0.2 + ((double)prepared_cues * 0.8 / (double)cue_count);
					callback(cue_list, progress, "Initialising cues...", user_data);
				}

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
	
	if (callback)
	{
		callback(NULL, 1.0, "Ready", user_data);
	}

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
/// (but not their state!)
/// @param cue_list The cue list
/// @param cue The cue that caused the change (currently unused)
void stack_cue_list_changed(StackCueList *cue_list, StackCue *cue)
{
	cue_list->changed = true;
}

/// Called by child cues to let us know that their state has changed
/// @param cue_list The cue list
/// @param cue The cue that caused the change (currently unused)
void stack_cue_list_state_changed(StackCueList *cue_list, StackCue *cue)
{
	if (cue_list->state_change_func != NULL)
	{
		cue_list->state_change_func(cue_list, cue, cue_list->state_change_func_data);
	}
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

/// Gets the cue number that should be inserted next
/// @param cue_list The cue list to search
cue_id_t stack_cue_list_get_next_cue_number(StackCueList *cue_list)
{
	cue_id_t max_cue_id = 0;

	// Iterate over all the cues, seraching for the maximum cue ID
	for (auto iter = SCL_GET_LIST(cue_list)->begin(); iter != SCL_GET_LIST(cue_list)->end(); ++iter)
	{
		if ((*iter)->id > max_cue_id)
		{
			max_cue_id = (*iter)->id;
		}
	}

	// Edge case: there are no cues in the list so return cue number 1
	if (max_cue_id == 0)
	{
		return 1000;
	}

	// Return the next integer cue number
	return max_cue_id - (max_cue_id % 1000) + 1000;
}

const char *stack_cue_list_get_show_name(StackCueList *cue_list)
{
	if (cue_list != NULL)
	{
		return cue_list->show_name;
	}

	return NULL;
}

const char *stack_cue_list_get_show_designer(StackCueList *cue_list)
{
	if (cue_list != NULL)
	{
		return cue_list->show_designer;
	}

	return NULL;
}

const char *stack_cue_list_get_show_revision(StackCueList *cue_list)
{
	if (cue_list != NULL)
	{
		return cue_list->show_revision;
	}

	return NULL;
}

bool stack_cue_list_set_show_name(StackCueList *cue_list, const char *show_name)
{
	if (cue_list != NULL)
	{
		if (cue_list->show_name != NULL)
		{
			free(cue_list->show_name);
		}

		if (show_name != NULL)
		{
			cue_list->show_name = strdup(show_name);
		}
		else
		{
			cue_list->show_name = strdup("");
		}

		return true;
	}

	return false;
}

bool stack_cue_list_set_show_designer(StackCueList *cue_list, const char *show_designer)
{
	if (cue_list != NULL)
	{
		if (cue_list->show_designer != NULL)
		{
			free(cue_list->show_designer);
		}

		if (show_designer != NULL)
		{
			cue_list->show_designer = strdup(show_designer);
		}
		else
		{
			cue_list->show_designer = strdup("");
		}

		return true;
	}

	return false;
}

bool stack_cue_list_set_show_revision(StackCueList *cue_list, const char *show_revision)
{
	if (cue_list != NULL)
	{
		if (cue_list->show_revision != NULL)
		{
			free(cue_list->show_revision);
		}

		if (show_revision != NULL)
		{
			cue_list->show_revision = strdup(show_revision);
		}
		else
		{
			cue_list->show_revision = strdup("");
		}

		return true;
	}

	return false;
}

