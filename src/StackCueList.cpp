// Includes:
#include "StackCue.h"
#include "StackLog.h"
#include <list>
#include <map>
#include <cstring>
#include <cmath>
#include <json/json.h>
using namespace std;

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
	cue_list->cues = new StackCueStdList();

	// Initialise the ring buffers
	cue_list->buffers = new StackRingBuffer*[channels];
	for (uint16_t i = 0; i < channels; i++)
	{
		cue_list->buffers[i] = stack_ring_buffer_create(32768);
	}

	// Initialise a remap map
	cue_list->uid_remap = new map<cue_uid_t, cue_uid_t>();

	// Initialise a map for storing RMS data
	cue_list->rms_data = new map<cue_uid_t, StackChannelRMSData*>();

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

	// Iterate over the cue list
	while (cue_list->cues->size() != 0)
	{
		// Get the cue
		StackCue *cue = *cue_list->cues->begin();

		// Remove it from the cue list
		stack_cue_list_remove(cue_list, cue);

		// Destroy the cue
		stack_cue_destroy(cue);
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

	// Tidy up ring buffers
	for (uint16_t i = 0; i < cue_list->channels; i++)
	{
		stack_ring_buffer_destroy(cue_list->buffers[i]);
	}
	delete [] cue_list->buffers;

	// Tidy up rms data
	for (auto iter : *cue_list->rms_data)
	{
		delete [] iter.second;
	}

	// Tidy up memory
	delete cue_list->cues;
	delete cue_list->uid_remap;
	delete cue_list->rms_data;

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
	return cue_list->cues->size();
}

/// Appends a cue in the cue list
/// @param cue_list The cue list
/// @param cue The cue to append.
void stack_cue_list_append(StackCueList *cue_list, StackCue *cue)
{
	cue_list->changed = true;
	cue_list->cues->push_back(cue);

	// Add a remap that remaps back to itself so that stack_*_cue_from_json
	// can continue to work with cues in the cue list when pasting from the
	// clipboard (as we only remap at load)
	(*cue_list->uid_remap)[cue->uid] = cue->uid;
}

/// Moves an existing cue within the stack
/// @param cue_list The cue list
/// @param cue The cue to be moved
/// @param dest The cue that represents the location of the new cue
/// @param before Whether to insert the cue before (true) or after (false) the cue given by dest
/// @param dest_in_child Whether the destination will make the cue a child
void stack_cue_list_move(StackCueList *cue_list, StackCue *cue, StackCue *dest, bool before, bool dest_in_child)
{
	StackCueStdList* cues = cue_list->cues;
	bool found_src = false, found_dest = false;

	if (cue_list == NULL || cue == NULL || dest == NULL)
	{
		stack_log("stack_cue_list_move(): NULL pointer passed!");
		return;
	}

	// Don't allow group cues inside group cues
	if (cue->can_have_children && dest_in_child)
	{
		stack_log("stack_cue_list_move(): Cues with children can't be nested\n");
		return;
	}

	if (cue->parent_cue != NULL)
	{
		StackCueStdList *children = stack_cue_get_children(cue->parent_cue);

		// Search for the cue to move
		for (auto iter = children->begin(); iter != children->end(); ++iter)
		{
			if (*iter == cue)
			{
				// If found, erase it from the list temporarily and stop searching
				cue_list->changed = true;
				children->erase(iter);
				found_src = true;

				// Cue is no longer a child
				cue->parent_cue = NULL;
				break;
			}
		}
	}
	else
	{
		// Search for the cue to move
		for (auto iter = cues->begin(); iter != cues->end(); ++iter)
		{
			if (*iter == cue)
			{
				// If found, erase it from the list temporarily and stop searching
				cue_list->changed = true;
				cues->erase(iter);
				found_src = true;
				break;
			}
		}
	}

	// If we found the cue, put it back in at it's new location
	if (found_src)
	{
		for (auto iter = cues->recursive_begin(); iter != cues->recursive_end(); ++iter)
		{
			if (*iter == dest)
			{
				found_dest = true;

				if (iter.is_child())
				{
					// If we're not expecting the destination to be in a child,
					// leave the child list in the appropriate direction
					if (!dest_in_child)
					{
						iter.leave_child(!before);
					}
				}

				// Our destination cue could have changed by the above logic, so
				// update it
				dest = *iter;

				if (dest_in_child)
				{
					StackCueStdList *children = NULL;

					// If we're expecting the destination to be a child, but the
					// destination we've found is not a child, then move into the
					// child and flip our before flag (i.e. after parent becomes
					// before first child)
					if (!iter.is_child())
					{
						children = stack_cue_get_children(dest);
						cue->parent_cue = dest;

						// Only move forward if the cue has children (as otherwise we'll
						// move forward to the next cue)
						if (children->size() != 0)
						{
							++iter;
							before = true;
							dest = *iter;
						}
					}
					else
					{
						// The cue we're going to become a child of is the parent of the destination cue
						children = stack_cue_get_children(dest->parent_cue);
						cue->parent_cue = dest->parent_cue;
					}

					if (!before)
					{
						// If we're inserting after the destination cue, increment the
						// iterator (as std::list::insert inserts before the iterator)
						++iter;
						if (!iter.is_child())
						{
							// If we're no longer in a child, which would be a mistake
							// then go back and just append to the list instead
							--iter;
							children->push_back(cue);
						}
						else
						{
							children->insert(iter.child_iterator(), cue);
						}
					}
					else
					{
						if (children->size() == 0)
						{
							children->push_back(cue);
						}
						else
						{
							children->insert(iter.child_iterator(), cue);
						}
					}
				}
				else
				{
					// If we're inserting after the destination cue, increment the
					// iterator (as std::list::insert inserts before the iterator)
					if (!before)
					{
						++iter;
						// If we're not expecting the destination to be in a child,
						// leave the child list in the appropriate direction
						if (iter.is_child() && !dest_in_child)
						{
							iter.leave_child(!before);
						}
					}

					// Re-insert the cue in the main list
					if (iter != cues->recursive_end())
					{
						cues->insert(iter.main_iterator(), cue);
					}
					else
					{
						stack_cue_list_append(cue_list, cue);
					}
				}

				// Stop searching
				break;
			}
		}
	}

	// This is an error condition, but just so we don't lose the cue, add it
	// back to the end of the list if we didn't find the destination
	if (!found_dest)
	{
		stack_log("stack_cue_list_move(): ERROR: Didn't find destination, re-adding to main cue list\n");
		stack_cue_list_append(cue_list, cue);
	}
}

/// Returns an iterator to the cue list that is positioned on a certain cue, or
/// at the end if the cue does not exist
/// @param cue_list The cue list
/// @param cue_uid The cue ID to jump to
/// @param index A pointer to receive the index of the cue in the list. Can be NULL;
/// @returns An iterator
StackCueStdList::iterator stack_cue_list_iter_at(StackCueList *cue_list, cue_uid_t cue_uid, size_t *index)
{
	size_t local_index = 0;

	StackCueStdList::iterator result = cue_list->cues->begin();
	while (result != cue_list->cues->end() && (*result)->uid != cue_uid)
	{
		local_index++;
		result++;
	}

	// Return the index
	if (index != NULL)
	{
		if (result == cue_list->cues->end())
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

/// Returns a recursive iterator to the cue list that is positioned on a
/// certain cue, or at the end if the cue does not exist
/// @param cue_list The cue list
/// @param cue_uid The cue ID to jump to
/// @param index A pointer to receive the index of the cue in the list. Can be NULL;
/// @returns A recursive iterator
StackCueStdList::recursive_iterator stack_cue_list_recursive_iter_at(StackCueList *cue_list, cue_uid_t cue_uid, size_t *index)
{
	size_t local_index = 0;

	StackCueStdList::recursive_iterator result = cue_list->cues->recursive_begin();
	while (result != cue_list->cues->recursive_end() && (*result)->uid != cue_uid)
	{
		local_index++;
		result++;
	}

	// Return the index
	if (index != NULL)
	{
		if (result == cue_list->cues->recursive_end())
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
	static const stack_time_t peak_hold_time = 2 * NANOSECS_PER_SEC;

	// Lock the cue list
	stack_cue_list_lock(cue_list);

	// Get a single clock time for all the cues
	stack_time_t clocktime = stack_get_clock_time();

	// Iterate over all the cues. Note that we do this recursively, so that:
	// a) cues with children don't need to pulse their children
	// b) child cues can be played irrespective of whether their parent is playing
	for (auto citer = cue_list->cues->recursive_begin(); citer != cue_list->cues->recursive_end(); ++citer)
	{
		StackCue *cue = *citer;

		// If the cue is in one of the playing states
		auto cue_state = cue->state;
		if (cue_state >= STACK_CUE_STATE_PLAYING_PRE && cue_state <= STACK_CUE_STATE_PLAYING_POST)
		{
			// Pulse the cue
			stack_cue_pulse(cue, clocktime);
		}

		// Update per-cue RMS peaks
		StackChannelRMSData *rms = stack_cue_list_get_rms_data(cue_list, cue->uid);
		if (rms != NULL)
		{
			size_t channel_count = stack_cue_get_active_channels(cue, NULL, true);
			for (size_t channel = 0; channel < channel_count; channel++)
			{
				if (clocktime - rms[channel].peak_time > peak_hold_time)
				{
					rms[channel].peak_level -= 0.1;
				}
			}
		}
	}

	// Update master RMS peaks
	for (size_t channel = 0; channel < cue_list->channels; channel++)
	{
		if (clocktime - cue_list->master_rms_data[channel].peak_time > peak_hold_time)
		{
			cue_list->master_rms_data[channel].peak_level -= 0.1;
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
	for (auto citer = cue_list->cues->recursive_begin(); citer != cue_list->cues->recursive_end(); ++citer)
	{
		StackCue *cue = *citer;
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
	root["config"] = Json::Value(Json::ValueType::objectValue);

	// Iterate over all the cues
	for (auto cue : *cue_list->cues)
	{
		// Get the JSON representation of the cue
		Json::Value cue_root;
		Json::Reader reader;
		char *cue_json_data = stack_cue_to_json(cue);
		reader.parse(cue_json_data, cue_root);
		stack_cue_free_json(cue, cue_json_data);

		// Add it to the cues entry
		root["cues"].append(cue_root);
	}

	// Iterate over the trigger classes
	for (auto citer : *stack_trigger_class_map_get())
	{
		// Get the class name
		const char *class_name = citer.second->class_name;

		char *trigger_config_json_data = stack_trigger_config_to_json(class_name);
		if (trigger_config_json_data != NULL)
		{
			Json::Reader reader;
			Json::Value trigger_config_root;
			reader.parse(trigger_config_json_data, trigger_config_root);
			root["config"][class_name] = trigger_config_root;
			stack_trigger_config_free_json(class_name, trigger_config_json_data);
		}
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

StackCue *stack_cue_list_create_cue_from_json(StackCueList *cue_list, Json::Value &cue_json, bool construct)
{
	// Make sure we have a class parameter
	if (!cue_json.isMember("class"))
	{
		stack_log("stack_cue_list_create_cue_from_json(): Cue missing 'class' parameter, skipping\n");
		return NULL;
	}

	// Make sure we have a base class
	if (!cue_json.isMember("StackCue"))
	{
		stack_log("stack_cue_list_create_cue_from_json(): Cue missing 'StackCue' class, skipping\n");
		return NULL;
	}

	// Make sure we have a UID
	if (!cue_json["StackCue"].isMember("uid"))
	{
		stack_log("stack_cue_list_create_cue_from_json(): Cue missing UID, skipping\n");
		return NULL;
	}

	// Create a new cue of the correct type
	const char *class_name = cue_json["class"].asString().c_str();
	StackCue *cue = stack_cue_new(class_name, cue_list);
	if (cue == NULL)
	{
		stack_log("stack_cue_list_create_cue_from_json(): Failed to create cue of type '%s', skipping\n", class_name);
		return NULL;
	}

	// Get the UID of the newly created cue and put a mapping from
	// the old UID to the new UID. Also store it in the JSON object
	// so that we can re-use it on the second loop
	(*cue_list->uid_remap)[cue_json["StackCue"]["uid"].asUInt64()] = cue->uid;
	cue_json["_new_uid"] = (Json::UInt64)cue->uid;

	// Call base constructor
	stack_cue_from_json_base(cue, cue_json.toStyledString().c_str());

	if (construct)
	{
		stack_cue_from_json(stack_cue_get_by_uid(cue_json["_new_uid"].asUInt64()), cue_json.toStyledString().c_str());
	}

	return cue;
}

StackCue *stack_cue_list_create_cue_from_json_string(StackCueList *cue_list, const char* json, bool construct)
{
	Json::Reader reader;
	Json::Value cue_root;
	if (!reader.parse(json, cue_root))
	{
		stack_log("stack_cue_list_new_file_file(): Failed to parse show JSON\n");
		return NULL;
	}

	return stack_cue_list_create_cue_from_json(cue_list, cue_root, construct);
}

/// Creates a new cue list using the contents of a file
/// @param uri The URI of the path to read from (e.g. file:///home/blah/test.stack)
/// @param callback The callback function to notify on progress
/// @param user_data Arbitrary data to pass to the callback function
/// @returns A new cue list or NULL on error
StackCueList *stack_cue_list_new_from_file(const char *uri, stack_cue_list_load_callback_t callback, void* user_data)
{
	if (callback != NULL)
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
	if (!reader.parse(&json_buffer[0], cue_list_root))
	{
		stack_log("stack_cue_list_new_file_file(): Failed to parse show JSON\n");
		return NULL;
	}

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

	// If we have some config...
	if (cue_list_root.isMember("config"))
	{
		// Iterate over the trigger classes
		for (auto citer : *stack_trigger_class_map_get())
		{
			const char *class_name = citer.second->class_name;
			if (cue_list_root["config"].isMember(class_name))
			{
				stack_trigger_config_from_json(class_name, cue_list_root["config"][class_name].toStyledString().c_str());
			}
		}
	}

	// If we have some cues...
	if (cue_list_root.isMember("cues"))
	{
		Json::Value& cues_root = cue_list_root["cues"];

		if (cues_root.isArray())
		{
			if (callback)
			{
				callback(cue_list, 0.1, "Preparing cue list...", user_data);
			}

			// Iterate over the cues, creating their instances, and populating
			// just their base classes (we need to have built a UID map)
			int cue_count = 0;
			for (auto iter = cues_root.begin(); iter != cues_root.end(); ++iter)
			{
				Json::Value& cue_json = *iter;

				// Create a new cue of the correct type
				StackCue *cue = stack_cue_list_create_cue_from_json(cue_list, cue_json, false);
				if (cue == NULL)
				{
					// TODO: It would be nice if we have some sort of "error cue" which
					// contained the JSON for the cue, so we didn't just drop cues from
					// the stack
					cue_json["_skip"] = 1;
					continue;
				}

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
		callback(cue_list, 1.0, "Ready", user_data);
	}

	return cue_list;
}

/// Returns the new UID of a cue after it's been remapped during opening
/// @param cue_list The cue list
/// @param old_uid The old UID that exists in the file we opened
cue_uid_t stack_cue_list_remap(StackCueList *cue_list, cue_uid_t old_uid)
{
	return (*cue_list->uid_remap)[old_uid];
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
		// Create or update RMS data
		auto rms_iter = cue_list->rms_data->find(cue->uid);
		if (rms_iter != cue_list->rms_data->end())
		{
			StackChannelRMSData *rms_data = rms_iter->second;
			delete [] rms_data;
			cue_list->rms_data->erase(rms_iter);
		}
	}
}

/// Removes a cue from the cue list
/// @param cue_list The cue list
/// @param cue The cue to remove
void stack_cue_list_remove(StackCueList *cue_list, StackCue *cue)
{
	// Iterate over all the cues
	for (auto iter = cue_list->cues->recursive_begin(); iter != cue_list->cues->recursive_end(); ++iter)
	{
		// If we've found the cue
		if (*iter == cue)
		{
			if (iter.is_child())
			{
				stack_cue_get_children(cue->parent_cue)->erase(iter.child_iterator());
			}
			else
			{
				// Erase it
				cue_list->cues->erase(iter.main_iterator());
			}

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
	for (auto iter = cue_list->cues->begin(); iter != cue_list->cues->end(); ++iter)
	{
		// If we've found the cue
		if (*iter == cue)
		{
			// Increment the iterator to the next cue
			++iter;

			// If we're now past the end of the cue list
			if (iter == cue_list->cues->end())
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
	for (auto cue : *cue_list->cues)
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
	for (auto cue : *cue_list->cues)
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

	// Iterate over all the cues, searching for the maximum cue ID
	for (auto iter = cue_list->cues->recursive_begin(); iter != cue_list->cues->recursive_end(); ++iter)
	{
		const StackCue* cue = *iter;
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

	// TODO: Determine a more appropriate size for this
	size_t request_samples = samples;

	// Allocate a buffer for mixing our new data in to
	float *new_data = new float[cue_list->channels * request_samples];
	bool *new_clipped = new bool[cue_list->channels];
	memset(new_data, 0, cue_list->channels * request_samples * sizeof(float));
	memset(new_clipped, 0, cue_list->channels * sizeof(bool));

	float *cue_data = NULL;
	size_t cue_data_size = 0;

	// Get audio data for cues recursively, with some exceptions. We iterate
	// recursively so that child cues can be played outside of the context of
	// their parent.
	// TODO: I'm not sure I like this. Maybe we should call get_audio on any
	// cue that has children.
	for (auto citer = cue_list->cues->recursive_begin(); citer != cue_list->cues->recursive_end(); ++citer)
	{
		StackCue *cue = *citer;

		// If the cue is a child and the parent is playing, don't get the audio
		// as we'll have gotten it from the parent already
		if (cue->parent_cue != NULL && cue->parent_cue->state == STACK_CUE_STATE_PLAYING_ACTION)
		{
			continue;
		}

		// Reset clipping marker array (otherwise we'll set clipped on each subsequent cue)
		memset(new_clipped, 0, cue_list->channels * sizeof(bool));

		// Get the list of active_channels
		memset(cue_list->active_channels_cache, 0, cue_list->channels * sizeof(bool));
		size_t active_channel_count = stack_cue_get_active_channels(cue, cue_list->active_channels_cache, true);

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
				if (value > 1.0)
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
		auto rms_iter = cue_list->rms_data->find(cue->uid);
		if (rms_iter == cue_list->rms_data->end())
		{
			StackChannelRMSData *new_rms_data = new StackChannelRMSData[active_channel_count];
			for (size_t i = 0; i < active_channel_count; i++)
			{
				new_rms_data[i].current_level = cue_list->rms_cache[i];
				new_rms_data[i].peak_level = cue_list->rms_cache[i];
				new_rms_data[i].peak_time = stack_get_clock_time();
				new_rms_data[i].clipped = new_clipped[i];
			}
			(*cue_list->rms_data)[cue->uid] = new_rms_data;
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
	auto rms_iter = cue_list->rms_data->find(uid);
	if (rms_iter != cue_list->rms_data->end())
	{
		return rms_iter->second;
	}

	return NULL;
}

StackChannelRMSData *stack_cue_list_add_rms_data(StackCueList *cue_list, cue_uid_t uid, size_t channels)
{
	auto rms_iter = cue_list->rms_data->find(uid);
	if (rms_iter == cue_list->rms_data->end())
	{
		StackChannelRMSData *new_rms_data = new StackChannelRMSData[channels];
		for (size_t channel = 0; channel < channels; channel++)
		{
			new_rms_data[channel].current_level = 0.0;
			new_rms_data[channel].peak_level = 0.0;
			new_rms_data[channel].peak_time = 0;
			new_rms_data[channel].clipped = false;
		}

		(*cue_list->rms_data)[uid] = new_rms_data;
		return new_rms_data;
	}
	else
	{
		StackChannelRMSData *old_rms_data = rms_iter->second;
		return old_rms_data;
	}
}

StackCueStdList::recursive_iterator& StackCueStdList::recursive_iterator::leave_child(bool forward)
{
	if (in_child)
	{
		in_child = false;

		if (forward)
		{
			main_iter++;
		}

		child_iter = cue_list.end();
	}

	return *this;
}

StackCueStdList::recursive_iterator& StackCueStdList::recursive_iterator::operator++()
{
	if (in_child)
	{
		child_iter++;
		if (child_iter == stack_cue_get_children(*main_iter)->end())
		{
			in_child = false;
			main_iter++;
			child_iter = cue_list.end();
		}
	}
	else
	{
		if ((*main_iter)->can_have_children && stack_cue_get_children(*main_iter)->size() > 0)
		{
			child_iter = stack_cue_get_children(*main_iter)->begin();
			in_child = true;
		}
		else
		{
			main_iter++;
		}
	}

	return *this;
}

StackCueStdList::recursive_iterator& StackCueStdList::recursive_iterator::operator--()
{
	if (in_child)
	{
		child_iter--;
		if (child_iter == stack_cue_get_children(*main_iter)->end())
		{
			in_child = false;
			child_iter = cue_list.end();
		}
	}
	else
	{
		main_iter--;

		if (main_iter != cue_list.end() && (*main_iter)->can_have_children && stack_cue_get_children(*main_iter)->size() > 0)
		{
			child_iter = stack_cue_get_children(*main_iter)->end();
			child_iter--;
			in_child = true;
		}
	}

	return *this;
}
