// Includes:
#include "StackCue.h"
#include "StackLog.h"
#include <list>
#include <map>
#include <cstring>
#include <cmath>
#include <json/json.h>
using namespace std;

typedef list<StackCue*> stackcue_list_t;
typedef list<StackCue*>::iterator stackcue_list_iterator_t;
typedef map<cue_uid_t, cue_uid_t> uid_remap_t;
typedef map<cue_uid_t, StackChannelRMSData*> rms_map_t;

#define SCL_GET_LIST(_scl) ((stackcue_list_t*)(((StackCueList*)(_scl))->cues))
#define SCL_UID_REMAP(_scl) (*((uid_remap_t*)(_scl)))

// Pre-definitions:
static void stack_cue_list_pulse_thread(StackCueList *cue_list);

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
	cue_list->channels = channels;
	cue_list->active_channels_cache = new bool[cue_list->channels];
	cue_list->rms_cache = new float[cue_list->channels];

	// Initialise a list
	cue_list->cues = (void*)new stackcue_list_t();

	// Initialise the ring buffers
	cue_list->buffers = new StackRingBuffer*[channels];
	for (uint16_t i = 0; i < channels; i++)
	{
		cue_list->buffers[i] = stack_ring_buffer_create(32768);
	}

	// Initialise a remap map
	cue_list->uid_remap = (void*)new uid_remap_t();

	// Initialise a map for storing RMS data
	cue_list->rms_data = (void*)new rms_map_t();

	// Initialise master RMS data
	cue_list->master_rms_data = new StackChannelRMSData[channels];
	for (size_t i = 0; i < channels; i++)
	{
		cue_list->master_rms_data[i].current_level = -INFINITY;
		cue_list->master_rms_data[i].peak_level = -INFINITY;
		cue_list->master_rms_data[i].peak_time = 0;
	}

	// Start the cue list pulsing thread
	cue_list->kill_thread = false;
	cue_list->pulse_thread = std::thread(stack_cue_list_pulse_thread, cue_list);

#if HAVE_LIBPROTOBUF_C == 1
	// Create the RPC socket
	cue_list->rpc_socket = stack_rpc_socket_create(NULL, NULL, cue_list);
#endif

	return cue_list;
}

/// Destroys a cue list, freeing up any associated memory
/// @param cue_list The cue list to destroy
void stack_cue_list_destroy(StackCueList *cue_list)
{
	if (cue_list == NULL)
	{
		stack_log("stack_cue_list_destroy(): Attempted to destroy NULL!\n");
		return;
	}

#if HAVE_LIBPROTOBUF_C == 1
	// Close the RPC socket before anything else to stop any remote access
	if (cue_list->rpc_socket)
	{
		stack_rpc_socket_destroy(cue_list->rpc_socket);
	}
#endif

	// Stop the pulse thread
	cue_list->kill_thread = true;
	cue_list->pulse_thread.join();

	// Destroy the audio device
	if (cue_list->audio_device)
	{
		stack_audio_device_destroy(cue_list->audio_device);
	}

	// Lock the cue list
	stack_cue_list_lock(cue_list);

	// Get an iterator over the cue list
	void *citer = stack_cue_list_iter_front(cue_list);

	// Iterate over the cue list
	while (!stack_cue_list_iter_at_end(cue_list, citer))
	{
		// Get the cue
		StackCue *cue = stack_cue_list_iter_get(citer);

		// Remove it from the cue list
		stack_cue_list_remove(cue_list, cue);

		// Destroy the cue
		stack_cue_destroy(cue);

		// Iterate by repeatedly grabbing the top of the list
		stack_cue_list_iter_free(citer);
		citer = stack_cue_list_iter_front(cue_list);
	}

	// Free the iterator
	stack_cue_list_iter_free(citer);

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

	// Tidy up ring buffers
	for (uint16_t i = 0; i < cue_list->channels; i++)
	{
		stack_ring_buffer_destroy(cue_list->buffers[i]);
	}
	delete [] cue_list->buffers;

	// Tidy up rms data
	for (auto iter : *(rms_map_t*)cue_list->rms_data)
	{
		delete [] iter.second;
	}

	// Tidy up memory
	delete (stackcue_list_t*)cue_list->cues;
	delete (uid_remap_t*)cue_list->uid_remap;
	delete (rms_map_t*)cue_list->rms_data;

	// Tidy up caches
	delete [] cue_list->active_channels_cache;
	delete [] cue_list->rms_cache;
	delete [] cue_list->master_rms_data;

	// Unlock the cue list
	stack_cue_list_unlock(cue_list);

	// Free ourselves
	delete cue_list;
}

void stack_cue_list_set_audio_device(StackCueList *cue_list, StackAudioDevice *audio_device)
{
	size_t old_channels = 0, new_channels = 0;

	// Stop all the current cues and destroy the audio device
	stack_cue_list_stop_all(cue_list);
	if (cue_list->audio_device != NULL)
	{
		old_channels = cue_list->audio_device->channels;
		stack_audio_device_destroy(cue_list->audio_device);
	}

	// Store the new audio device
	cue_list->audio_device = audio_device;

	if (audio_device == NULL)
	{
		return;
	}

	// Lock
	stack_cue_list_lock(cue_list);

	new_channels = audio_device->channels;

	if (new_channels != old_channels)
	{
		if (cue_list->active_channels_cache != NULL)
		{
			delete [] cue_list->active_channels_cache;
		}
		cue_list->active_channels_cache = new bool[new_channels];

		if (cue_list->rms_cache != NULL)
		{
			delete [] cue_list->rms_cache;
		}
		cue_list->rms_cache = new float[new_channels];

		// Re-initialise the ring buffers
		if (cue_list->buffers != NULL)
		{
			for (size_t i = 0; i < old_channels; i++)
			{
				stack_ring_buffer_destroy(cue_list->buffers[i]);
			}
			delete [] cue_list->buffers;
		}
		cue_list->buffers = new StackRingBuffer*[new_channels];
		for (size_t i = 0; i < new_channels; i++)
		{
			cue_list->buffers[i] = stack_ring_buffer_create(32768);
		}

		// Re-initialise master RMS data
		if (cue_list->master_rms_data != NULL)
		{
			delete [] cue_list->master_rms_data;
		}
		cue_list->master_rms_data = new StackChannelRMSData[new_channels];
		for (size_t i = 0; i < new_channels; i++)
		{
			cue_list->master_rms_data[i].current_level = -INFINITY;
			cue_list->master_rms_data[i].peak_level = -INFINITY;
			cue_list->master_rms_data[i].peak_time = 0;
		}

		cue_list->channels = new_channels;
	}

	// Unlock
	stack_cue_list_unlock(cue_list);
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

/// Returns an iterator to the cue list that is positioned on a certain cue, or
/// at the end if the cue does not exist
/// @param cue_list The cue list
/// @param cue_uid The cue ID to jump to
/// @param index A pointer to receive the index of the cue in the list. Can be NULL;
/// @returns An iterator
void *stack_cue_list_iter_at(StackCueList *cue_list, cue_uid_t cue_uid, size_t *index)
{
	size_t local_index = 0;

	stackcue_list_iterator_t* result = new stackcue_list_iterator_t;
	*result	= (SCL_GET_LIST(cue_list)->begin());
	while (*result != SCL_GET_LIST(cue_list)->end() && (**result)->uid != cue_uid)
	{
		local_index++;
		(*result)++;
	}

	// Return the index
	if (index != NULL)
	{
		if (*result == SCL_GET_LIST(cue_list)->end())
		{
			*index = -1;
		}
		else
		{
			*index = local_index;
		}
	}

	return result;
}

/// Increments an iterator to the next cue
/// @param iter A cue list iterator as returned by stack_cue_list_iter_front for example
void *stack_cue_list_iter_next(void *iter)
{
	return (void*)&(++(*(stackcue_list_iterator_t*)(iter)));
}

/// Derements an iterator to the next cue
/// @param iter A cue list iterator as returned by stack_cue_list_iter_front for example
void *stack_cue_list_iter_prev(void *iter)
{
	return (void*)&(--(*(stackcue_list_iterator_t*)(iter)));
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

// Callback for cue pulsing timer
static void stack_cue_list_pulse_thread(StackCueList *cue_list)
{
	// Set the thread name
	pthread_setname_np(pthread_self(), "stack-pulse");

	// Loop until we're being destroyed
	while (!cue_list->kill_thread)
	{
		// Send pulses to all the active cues
		stack_cue_list_pulse(cue_list);

		// Sleep for a millisecond
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	return;
}


/// Pulses all the cues in the cue list so that they may do their time-based operations
/// @param cue_list The cue list
void stack_cue_list_pulse(StackCueList *cue_list)
{
	static stack_time_t underflow_time = 0;

	// Lock the cue list
	stack_cue_list_lock(cue_list);

	// Get a single clock time for all the cues
	stack_time_t clocktime = stack_get_clock_time();

	// Iterate over all the cues
	for (auto cue : *SCL_GET_LIST(cue_list))
	{
		// If the cue is in one of the playing states
		auto cue_state = cue->state;
		if (cue_state >= STACK_CUE_STATE_PLAYING_PRE && cue_state <= STACK_CUE_STATE_PLAYING_POST)
		{
			// Pulse the cue
			stack_cue_pulse(cue, clocktime);
		}
	}

	// Track how long the cue pulses took
	//stack_time_t cue_pulse_time = stack_get_clock_time() - clocktime;

	// Unlock the cue list
	stack_cue_list_unlock(cue_list);

	//stack_log("stack_cue_list_pulse(): Pulse took %lldns\n", cue_pulse_time);
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

/// Stops all the cues in the cue list
/// @param cue_list The cue list
void stack_cue_list_stop_all(StackCueList *cue_list)
{
	// Lock the cue list
	stack_cue_list_lock(cue_list);

	// Iterate over all the cues
	for (auto cue : *SCL_GET_LIST(cue_list))
	{
		if ((cue->state >= STACK_CUE_STATE_PLAYING_PRE && cue->state <= STACK_CUE_STATE_PLAYING_POST) || cue->state == STACK_CUE_STATE_PAUSED)
		{
			// Stop the cue
			stack_cue_stop(cue);
		}
	}

	// Unlock the cue list
	stack_cue_list_unlock(cue_list);
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
	for (auto cue : *SCL_GET_LIST(cue_list))
	{
		// Get the JSON representation of the cue
		Json::Value cue_root;
		Json::Reader reader;
		char *cue_json_data = stack_cue_to_json(cue);
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
		stack_log("stack_cue_list_new_from_file(): Failed to open file\n");
		return NULL;
	}

	// Get a read stream
	GFileInputStream *stream = g_file_read(file, NULL, NULL);
	if (stream == NULL)
	{
		stack_log("stack_cue_list_new_from_file(): Failed to get file stream\n");
		g_object_unref(file);
		return NULL;
	}

	// Query the file info (to get the file size)
	GFileInfo* file_info = g_file_query_info(file, "standard::size", G_FILE_QUERY_INFO_NONE, NULL, NULL);
	if (file_info == NULL)
	{
		stack_log("stack_cue_list_new_from_file(): Failed to get file information\n");
		g_object_unref(stream);
		g_object_unref(file);
		return NULL;
	}

	// Get the file size
	goffset size = g_file_info_get_size(file_info);

	// Tidy up
	g_object_unref(file_info);

	// Allocate memory
	std::vector<char> json_buffer(size + 1);

	if (callback)
	{
		callback(NULL, 0.01, "Reading file...", user_data);
	}

	// Read the file in to the buffer
	if (g_input_stream_read(G_INPUT_STREAM(stream), &json_buffer[0], size, NULL, NULL) != size)
	{
		stack_log("stack_cue_list_new_from_file(): Failed to read file\n");
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
		stack_log("stack_cue_list_new_from_file(): Missing 'channels' option\n");
		return NULL;
	}

	// Validation: check we have between 1 and 64 channels
	if (cue_list_root["channels"].asUInt() == 0 || cue_list_root["channels"].asUInt() > 64)
	{
		stack_log("stack_cue_list_new_from_file(): Invalid number of 'channels' specified\n");
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
					stack_log("stack_cue_list_from_file(): Cue missing 'class' parameter, skipping\n");
					continue;
				}

				// Make sure we have a base class
				if (!cue_json.isMember("StackCue"))
				{
					cue_json["_skip"] = 1;
					stack_log("stack_cue_list_from_file(): Cue missing 'StackCue' class, skipping\n");
					continue;
				}

				// Make sure we have a UID
				if (!cue_json["StackCue"].isMember("uid"))
				{
					cue_json["_skip"] = 1;
					stack_log("stack_cue_list_from_file(): Cue missing UID, skipping\n");
					continue;
				}

				// Create a new cue of the correct type
				const char *class_name = cue_json["class"].asString().c_str();
				StackCue *cue = stack_cue_new(class_name, cue_list);
				if (cue == NULL)
				{
					stack_log("stack_cue_list_from_file(): Failed to create cue of type '%s', skipping\n", class_name);
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
			stack_log("stack_cue_list_new_from_file(): 'cues' is not an array\n");
		}
	}
	else
	{
		stack_log("stack_cue_list_new_from_file(): Missing 'cues' option\n");
	}

	// Flag that the cue list hasn't changed
	stack_log("stack_cue_list_new_from_file(): Marking cue list as unchanged\n");
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
/// @param property The property that caused the change (could be NULL)
void stack_cue_list_changed(StackCueList *cue_list, StackCue *cue, StackProperty *property)
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

	// If the cue has stopped, remove any RMS data
	if (cue->state == STACK_CUE_STATE_STOPPED)
	{
		rms_map_t* rms_map = (rms_map_t*)(cue_list->rms_data);
		// Create or update RMS data
		auto rms_iter = rms_map->find(cue->uid);
		if (rms_iter != rms_map->end())
		{
			StackChannelRMSData *rms_data = rms_iter->second;
			delete [] rms_data;
			rms_map->erase(rms_iter);
		}
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
			stack_cue_list_changed(cue_list, cue, NULL);

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

StackCue *stack_cue_list_get_cue_by_uid(StackCueList *cue_list, cue_uid_t uid)
{
	// Iterate over all the cues
	for (auto cue : *SCL_GET_LIST(cue_list))
	{
		if (cue->uid == uid)
		{
			return cue;
		}
	}

	return NULL;
}

StackCue *stack_cue_list_get_cue_by_index(StackCueList *cue_list, size_t index)
{
	size_t count = 0;

	// Iterate over all the cues
	for (auto cue : *SCL_GET_LIST(cue_list))
	{
		if (count == index)
		{
			return cue;
		}
		count++;
	}

	return NULL;
}

/// Gets the cue number that should be inserted next
/// @param cue_list The cue list to search
cue_id_t stack_cue_list_get_next_cue_number(StackCueList *cue_list)
{
	cue_id_t max_cue_id = 0;

	// Iterate over all the cues, seraching for the maximum cue ID
	for (auto cue : *SCL_GET_LIST(cue_list))
	{
		if (cue->id > max_cue_id)
		{
			max_cue_id = cue->id;
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

static void stack_cue_list_set_string(char **dest, const char *new_value)
{
	if (*dest != NULL)
	{
		free(*dest);
	}

	*dest = strdup(new_value != NULL ? new_value : "");
}

bool stack_cue_list_set_show_name(StackCueList *cue_list, const char *show_name)
{
	if (cue_list != NULL)
	{
		stack_cue_list_set_string(&cue_list->show_name, show_name);
		return true;
	}

	return false;
}

bool stack_cue_list_set_show_designer(StackCueList *cue_list, const char *show_designer)
{
	if (cue_list != NULL)
	{
		stack_cue_list_set_string(&cue_list->show_designer, show_designer);
		return true;
	}

	return false;
}

bool stack_cue_list_set_show_revision(StackCueList *cue_list, const char *show_revision)
{
	if (cue_list != NULL)
	{
		stack_cue_list_set_string(&cue_list->show_revision, show_revision);
		return true;
	}

	return false;
}

void stack_cue_list_populate_buffers(StackCueList *cue_list, size_t samples)
{
	stack_cue_list_lock(cue_list);

	rms_map_t* rms_data = (rms_map_t*)cue_list->rms_data;

	// TODO: Determine a more appropriate size for this
	size_t request_samples = samples;

	// Allocate a buffer for mixing our new data in to
	float *new_data = new float[cue_list->channels * request_samples];
	bool *new_clipped = new bool[cue_list->channels];
	memset(new_data, 0, cue_list->channels * request_samples * sizeof(float));
	memset(new_clipped, 0, cue_list->channels * sizeof(bool));

	float *cue_data = NULL;
	size_t cue_data_size = 0;

	// Allocate a buffer for new cue data
	for (auto cue : *SCL_GET_LIST(cue_list))
	{
		// Get the list of active_channels
		memset(cue_list->active_channels_cache, 0, cue_list->channels * sizeof(bool));
		size_t active_channel_count = stack_cue_get_active_channels(cue, cue_list->active_channels_cache);

		// Skip cues with no active channels
		if (active_channel_count == 0)
		{
			continue;
		}

		// Ensure our cue-data buffer is large enough for new cue audio
		if (cue_data_size < active_channel_count * request_samples)
		{
			if (cue_data != NULL)
			{
				delete [] cue_data;
			}
			cue_data_size = active_channel_count * request_samples;
			cue_data = new float[cue_data_size];
		}

		// Get the audio data from the cue
		size_t samples_received = stack_cue_get_audio(cue, cue_data, request_samples);

		if (samples_received <= 0)
		{
			continue;
		}

		// Add this cues data on to the new data
		size_t source_channel = 0;
		for (size_t dest_channel = 0; dest_channel < cue_list->channels; dest_channel++)
		{
			// Only need to do something if the channel is active
			if (!cue_list->active_channels_cache[dest_channel])
			{
				continue;
			}

			// cue_data is multiplexed, containing active_channel_count channels
			// new_data is NOT multiplexed (it's faster to write this to the ring buffer)
			float *read_pointer = &cue_data[source_channel];
			float *end_pointer = &cue_data[active_channel_count * samples_received];
			float *write_pointer = &new_data[dest_channel * request_samples];
			float channel_rms = 0.0;
			while (read_pointer < end_pointer)
			{
				// Keep track of the RMS whilst we're already looping
				const float value = *read_pointer;
				channel_rms += value * value;

				// Write out the data
				*write_pointer += value;

				// Check for clipping
				if (*write_pointer > 1.0)
				{
					new_clipped[dest_channel] = true;
				}

				// Move to next sample
				write_pointer++;
				read_pointer += active_channel_count;
			}

			// Finish off the RMS calculation
			cue_list->rms_cache[source_channel] = stack_scalar_to_db(sqrtf(channel_rms / (float)samples_received));

			// Start the next channel
			source_channel++;
		}

		// Create or update RMS data
		auto rms_iter = rms_data->find(cue->uid);
		if (rms_iter == rms_data->end())
		{
			StackChannelRMSData *new_rms_data = new StackChannelRMSData[active_channel_count];
			for (size_t i = 0; i < active_channel_count; i++)
			{
				new_rms_data[i].current_level = cue_list->rms_cache[i];
				new_rms_data[i].peak_level = cue_list->rms_cache[i];
				new_rms_data[i].peak_time = stack_get_clock_time();
				new_rms_data[i].clipped = new_clipped[i];
			}
			(*rms_data)[cue->uid] = new_rms_data;
		}
		else
		{
			StackChannelRMSData *rms_data = rms_iter->second;
			for (size_t i = 0; i < active_channel_count; i++)
			{
				rms_data[i].current_level = cue_list->rms_cache[i];
				if (cue_list->rms_cache[i] >= rms_data[i].peak_level)
				{
					rms_data[i].peak_level = cue_list->rms_cache[i];
					rms_data[i].peak_time = stack_get_clock_time();
				}
				rms_data[i].clipped = new_clipped[i];
			}
		}
	}

	// Tidy up
	if (cue_data != NULL)
	{
		delete [] cue_data;
	}

	// Write the new data into the ring buffers
	for (size_t channel = 0; channel < cue_list->channels; channel++)
	{
		// Calculate RMS
		StackChannelRMSData *mc_rms_data = &cue_list->master_rms_data[channel];
		float channel_rms = 0.0;
		float *channel_data = &new_data[channel * request_samples];
		// Calculate master RMS
		mc_rms_data->clipped = false;
		for (size_t i = 0; i < request_samples; i++)
		{
			channel_rms += channel_data[i] * channel_data[i];
			if (channel_data[i] > 1.0)
			{
				mc_rms_data->clipped = true;
			}
		}
		mc_rms_data->current_level = stack_scalar_to_db(sqrtf(channel_rms / (float)request_samples));
		if (mc_rms_data->current_level >= mc_rms_data->peak_level)
		{
			mc_rms_data->peak_level = mc_rms_data->current_level;
			mc_rms_data->peak_time = stack_get_clock_time();
		}

		stack_ring_buffer_write(cue_list->buffers[channel], &new_data[channel * request_samples], request_samples, 1);
	}

	// Tidy up
	delete [] new_data;
	delete [] new_clipped;

	stack_cue_list_unlock(cue_list);

}

void stack_cue_list_get_audio(StackCueList *cue_list, float *buffer, size_t samples, size_t channel_count, size_t *channels)
{
	// Determine if there's enough data in ALL the buffers (regardless of
	bool need_audio_from_cues = false;
	for (uint16_t i = 0; i < cue_list->channels; i++)
	{
		if (cue_list->buffers[i]->used < samples)
		{
			need_audio_from_cues = true;
		}
	}

	// Get more data if required
	if (need_audio_from_cues)
	{
		stack_cue_list_populate_buffers(cue_list, samples);
	}

	for (size_t idx = 0; idx < channel_count; idx++)
	{
		size_t channel = channels[idx];
		size_t received = stack_ring_buffer_read(cue_list->buffers[channel], buffer + idx, samples, channel_count);

		// TODO: Do something with "received"
	}
}

StackChannelRMSData *stack_cue_list_get_rms_data(StackCueList *cue_list, cue_uid_t uid)
{
	rms_map_t* rms_data = (rms_map_t*)cue_list->rms_data;
	auto rms_iter = rms_data->find(uid);
	if (rms_iter != rms_data->end())
	{
		return rms_iter->second;
	}

	return NULL;
}
