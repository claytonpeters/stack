// Includes:
#include "StackApp.h"
#include "StackGroupCue.h"
#include "StackLog.h"
#include <cstring>
#include <cstdlib>
#include <string>
#include <cmath>
#include <json/json.h>
#include <list>
#include <set>

// Global: A single instance of our builder so we don't have to keep reloading
// it every time we change the selected cue
static GtkBuilder *sgc_builder = NULL;

static void stack_group_cue_ccb_action(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackGroupCue* cue = STACK_GROUP_CUE(user_data);

		// Notify cue list that we've changed
		stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue), property);

		// Fire an updated-selected-cue signal to signal the UI to change (we might
		// have changed state)
		if (cue->group_tab)
		{
			StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->group_tab));
			g_signal_emit_by_name((gpointer)window, "update-selected-cue");
		}
	}
}

/// Pause or resumes change callbacks on variables
static void stack_group_cue_pause_change_callbacks(StackCue *cue, bool pause)
{
	stack_property_pause_change_callback(stack_cue_get_property(cue, "action"), pause);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "volume"), pause);
}

////////////////////////////////////////////////////////////////////////////////
// CREATION AND DESTRUCTION

/// Creates a action cue
static StackCue* stack_group_cue_create(StackCueList *cue_list)
{
	// Allocate the cue
	StackGroupCue* cue = new StackGroupCue();

	// Initialise the superclass
	stack_cue_init(&cue->super, cue_list);

	// Make this class a StackGroupCue
	cue->super._class_name = "StackGroupCue";

	// Initialise our variables
	STACK_CUE(cue)->can_have_children = true;
	cue->group_tab = NULL;
	cue->cues = new StackCueStdList;
	cue->played_cues = new std::list<cue_uid_t>;

	// Initialise superclass variables
	stack_cue_set_name(STACK_CUE(cue), "Group");

	StackProperty *action = stack_property_create("action", STACK_PROPERTY_TYPE_INT32);
	stack_cue_add_property(STACK_CUE(cue), action);
	stack_property_set_int32(action, STACK_PROPERTY_VERSION_DEFINED, STACK_GROUP_CUE_ENTER);
	stack_property_set_changed_callback(action, stack_group_cue_ccb_action, (void*)cue);

	// This property is not exposed to the user and is only used for live volume changes
	StackProperty *volume = stack_property_create("play_volume", STACK_PROPERTY_TYPE_DOUBLE);
	stack_cue_add_property(STACK_CUE(cue), volume);

	return STACK_CUE(cue);
}

/// Destroys a action cue
static void stack_group_cue_destroy(StackCue *cue)
{
	for (auto cue : *STACK_GROUP_CUE(cue)->cues)
	{
		stack_cue_destroy(cue);
	}

	delete STACK_GROUP_CUE(cue)->cues;
	delete STACK_GROUP_CUE(cue)->played_cues;

	// Call parent destructor
	stack_cue_destroy_base(cue);
}

////////////////////////////////////////////////////////////////////////////////
// PROPERTY SETTERS

// Sets the performed action of an action cue
void stack_group_cue_set_action(StackGroupCue *cue, StackGroupCueAction action)
{
	stack_property_set_int32(stack_cue_get_property(STACK_CUE(cue), "action"), STACK_PROPERTY_VERSION_DEFINED, action);
}

////////////////////////////////////////////////////////////////////////////////
// UI CALLBACKS

/// Called when the action changes
static void gcp_action_changed(GtkToggleButton *widget, gpointer user_data)
{
	StackGroupCue *cue = STACK_GROUP_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);

	// Get pointers to the four radio button options
	GtkToggleButton* r1 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(sgc_builder, "gcpActionTypeEnter"));
	GtkToggleButton* r2 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(sgc_builder, "gcpActionTypeTriggerAll"));
	GtkToggleButton* r3 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(sgc_builder, "gcpActionTypeTriggerRandom"));
	GtkToggleButton* r4 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(sgc_builder, "gcpActionTypeTriggerPlaylist"));
	GtkToggleButton* r5 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(sgc_builder, "gcpActionTypeTriggerShuffledPlaylist"));

	// Determine which one is toggled on
	if (widget == r1 && gtk_toggle_button_get_active(r1)) { stack_group_cue_set_action(cue, STACK_GROUP_CUE_ENTER); }
	if (widget == r2 && gtk_toggle_button_get_active(r2)) { stack_group_cue_set_action(cue, STACK_GROUP_CUE_TRIGGER_ALL); }
	if (widget == r3 && gtk_toggle_button_get_active(r3)) { stack_group_cue_set_action(cue, STACK_GROUP_CUE_TRIGGER_RANDOM); }
	if (widget == r4 && gtk_toggle_button_get_active(r4)) { stack_group_cue_set_action(cue, STACK_GROUP_CUE_TRIGGER_PLAYLIST); }
	if (widget == r5 && gtk_toggle_button_get_active(r5)) { stack_group_cue_set_action(cue, STACK_GROUP_CUE_TRIGGER_SHUFFLED_PLAYLIST); }
}

////////////////////////////////////////////////////////////////////////////////
// BASE CUE OPERATIONS

static void stack_group_cue_trigger_children(StackGroupCue *gcue)
{
	// Get the current action
	int32_t action = STACK_GROUP_CUE_ENTER;
	stack_property_get_int32(stack_cue_get_property(STACK_CUE(gcue), "action"), STACK_PROPERTY_VERSION_LIVE, &action);
	
	switch (action)
	{
		case STACK_GROUP_CUE_ENTER:
			// This actually needs to do nothing (except maybe tell the cue list where to go)
			break;
		case STACK_GROUP_CUE_TRIGGER_ALL:
			// All we need to do here is trigger all the cues
			for (auto child : *gcue->cues)
			{
				stack_cue_play(child);
			}
			break;
		case STACK_GROUP_CUE_TRIGGER_RANDOM:
			// All we need to do here is select a cue at random and play it
			size_t index, target_index;
			index = 0;
			target_index = rand() % gcue->cues->size();
			for (auto child : *gcue->cues)
			{
				if (index == target_index)
				{
					// Set the length of the action to be that of the child's total time
					stack_time_t child_pre_time = 0, child_action_time = 0, child_post_time = 0;
					stack_property_get_int64(stack_cue_get_property(child, "pre_time"), STACK_PROPERTY_VERSION_DEFINED, &child_pre_time);
					stack_property_get_int64(stack_cue_get_property(child, "action_time"), STACK_PROPERTY_VERSION_DEFINED, &child_action_time);
					stack_property_get_int64(stack_cue_get_property(child, "post_time"), STACK_PROPERTY_VERSION_DEFINED, &child_post_time);
					stack_property_set_int64(stack_cue_get_property(STACK_CUE(gcue), "action_time"), STACK_PROPERTY_VERSION_DEFINED, child_pre_time + child_action_time + child_post_time);

					// We also need to set the live time here as we could already be playing by the time this is caled
					stack_property_set_int64(stack_cue_get_property(STACK_CUE(gcue), "action_time"), STACK_PROPERTY_VERSION_LIVE, child_pre_time + child_action_time + child_post_time);

					stack_cue_play(child);
					break;
				}
				index++;
			}
			break;
		case STACK_GROUP_CUE_TRIGGER_PLAYLIST:
			StackCue *first_cue;
			first_cue = *gcue->cues->begin();

			// Start first cue, and then keep track of what we have and haven't played
			stack_cue_play(first_cue);
			
			// Add the cue to the list of played cues
			gcue->played_cues->push_front(first_cue->uid);
			break;
		case STACK_GROUP_CUE_TRIGGER_SHUFFLED_PLAYLIST:
			// Start random cue, and then keep track of what we have and haven't played
			size_t s_index, s_target_index;
			s_index = 0;
			s_target_index = rand() % gcue->cues->size();
			for (auto child : *gcue->cues)
			{
				if (s_index == s_target_index)
				{
					// Play the cue
					stack_cue_play(child);

					// Add the cue to the list of played cues
					gcue->played_cues->push_front(child->uid);
					break;
				}
				s_index++;
			}
			break;
	}
}

/// Start the cue playing
static bool stack_group_cue_play(StackCue *cue)
{
	// Get the current action
	int32_t action = STACK_GROUP_CUE_ENTER;
	stack_property_get_int32(stack_cue_get_property(cue, "action"), STACK_PROPERTY_VERSION_DEFINED, &action);

	// TODO: Ideally the following would have been calculated beforehand, e.g. 
	// by some notification that children have changed
	// Iterate throught the child cues and find the largest action time
	stack_time_t action_time = 0;
	if (action == STACK_GROUP_CUE_TRIGGER_ALL)
	{
		for (auto child : *STACK_GROUP_CUE(cue)->cues)
		{
			stack_time_t child_pre_time = 0, child_action_time = 0, child_post_time = 0, child_total_time = 0;
			stack_property_get_int64(stack_cue_get_property(child, "pre_time"), STACK_PROPERTY_VERSION_DEFINED, &child_pre_time);
			stack_property_get_int64(stack_cue_get_property(child, "action_time"), STACK_PROPERTY_VERSION_DEFINED, &child_action_time);
			stack_property_get_int64(stack_cue_get_property(child, "post_time"), STACK_PROPERTY_VERSION_DEFINED, &child_post_time);
			child_total_time = child_pre_time + child_action_time + child_post_time;
			if (child_total_time > action_time)
			{
				action_time = child_total_time;
			}
		}
	}
	else if (action == STACK_GROUP_CUE_TRIGGER_PLAYLIST || action == STACK_GROUP_CUE_TRIGGER_SHUFFLED_PLAYLIST)
	{
		for (auto child : *STACK_GROUP_CUE(cue)->cues)
		{
			stack_time_t child_pre_time = 0, child_action_time = 0, child_post_time = 0;
			stack_property_get_int64(stack_cue_get_property(child, "pre_time"), STACK_PROPERTY_VERSION_DEFINED, &child_pre_time);
			stack_property_get_int64(stack_cue_get_property(child, "action_time"), STACK_PROPERTY_VERSION_DEFINED, &child_action_time);
			stack_property_get_int64(stack_cue_get_property(child, "post_time"), STACK_PROPERTY_VERSION_DEFINED, &child_post_time);
			action_time += child_pre_time + child_action_time + child_post_time;
		}
	}
	else if (action == STACK_GROUP_CUE_ENTER)
	{
		// We essentially do nothing here
		action_time = -1;
	}
	stack_property_set_int64(stack_cue_get_property(cue, "action_time"), STACK_PROPERTY_VERSION_DEFINED, action_time);

	// Store the state before we call the superclass
	StackCueState pre_state = cue->state;

	// Call the superclass
	if (!stack_cue_play_base(cue))
	{
		return false;
	}

	// If we were paused and are now playing the action, retrigger any paused cues
	if (pre_state == STACK_CUE_STATE_PAUSED && cue->state == STACK_CUE_STATE_PLAYING_ACTION)
	{
		for (auto child : *STACK_GROUP_CUE(cue)->cues)
		{
			if (child->state >= STACK_CUE_STATE_PAUSED)
			{
				stack_cue_play(child);
			}
		}
	}
	else
	{
		// Wipe the played cue playlist
		STACK_GROUP_CUE(cue)->played_cues->clear();
	}

	// Initialise playback
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "action"));
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "play_volume"));

	// Get the pre-wait time for the cue
	stack_time_t pre_time = 0;
	stack_property_get_int64(stack_cue_get_property(cue, "pre_time"), STACK_PROPERTY_VERSION_LIVE, &pre_time);

	// If the pre-wait time is zero, we should trigger the children now
	if (pre_time == 0)
	{
		stack_group_cue_trigger_children(STACK_GROUP_CUE(cue));
	}

	return true;
}

/// Pause the cue (and thus also any child cues)
static void stack_group_cue_pause(StackCue *cue)
{
	// Call the superclass
	stack_cue_pause_base(cue);

	for (auto child : *STACK_GROUP_CUE(cue)->cues)
	{
		if (child->state >= STACK_CUE_STATE_PLAYING_PRE && child->state <= STACK_CUE_STATE_PLAYING_POST)
		{
			stack_cue_pause(child);
		}
	}
}

/// Stop the cue playing (and thus also any child cues)
static void stack_group_cue_stop(StackCue *cue)
{
	// Call the superclass
	stack_cue_stop_base(cue);

	for (auto child : *STACK_GROUP_CUE(cue)->cues)
	{
		if ((child->state >= STACK_CUE_STATE_PLAYING_PRE && child->state <= STACK_CUE_STATE_PLAYING_POST) || child->state == STACK_CUE_STATE_PAUSED)
		{
			stack_cue_stop(child);
		}
	}
}

/// Update the cue based on time
static void stack_group_cue_pulse(StackCue *cue, stack_time_t clocktime)
{
	StackGroupCue *gcue = STACK_GROUP_CUE(cue);

	// Get the cue state before the base class potentially updates it
	StackCueState pre_pulse_state = cue->state;

	// Call superclass
	stack_cue_pulse_base(cue, clocktime);

	// Note that we don't lock/unlock the cue list here as StackCueList has
	// already done this prior to calling this function

	if (cue->state == STACK_CUE_STATE_PLAYING_ACTION && pre_pulse_state != STACK_CUE_STATE_PLAYING_ACTION)
	{
		stack_group_cue_trigger_children(gcue);
	}

	// Get the current action
	int32_t action = STACK_GROUP_CUE_ENTER;
	stack_property_get_int32(stack_cue_get_property(cue, "action"), STACK_PROPERTY_VERSION_LIVE, &action);

	// If we're playing and we're some kind of playlist
	if (cue->state == STACK_CUE_STATE_PLAYING_ACTION && (action == STACK_GROUP_CUE_TRIGGER_PLAYLIST || action == STACK_GROUP_CUE_TRIGGER_SHUFFLED_PLAYLIST))
	{
		// Get the details of the cue that was last playing
		cue_uid_t last_played_cue_uid = gcue->played_cues->front();
		StackCue *last_played_cue = stack_cue_get_by_uid(last_played_cue_uid);

		// If that cue still exists, and is now stopped
		if (last_played_cue != NULL && last_played_cue->parent_cue == cue && last_played_cue->state == STACK_CUE_STATE_STOPPED)
		{
			// For normal playlists
			if (action == STACK_GROUP_CUE_TRIGGER_PLAYLIST)
			{
				// Iterate over the child cues
				for (auto citer = gcue->cues->begin(); citer != gcue->cues->end(); ++citer)
				{
					// Look for the last played cue
					StackCue *child = *citer;
					if (child->uid == last_played_cue_uid)
					{
						// Go to the next cue
						citer++;

						// If we're at the end of the list, stop the cue
						if (citer == gcue->cues->end())
						{
							stack_cue_stop(cue);
							break;
						}
						// ...otherwise, play the next cue and add to the list
						else
						{
							child = *citer;
							stack_cue_play(child);
							gcue->played_cues->push_front(child->uid);
						}
						break;
					}
				}
			}
			else if (action == STACK_GROUP_CUE_TRIGGER_SHUFFLED_PLAYLIST)
			{
				std::set<cue_uid_t> remaining_set;

				// Add in to our set all the child cue UIDs
				for (auto child : *gcue->cues)
				{
					remaining_set.insert(child->uid);
				}

				// Remove any cues that we've already played
				for (auto played : *gcue->played_cues)
				{
					remaining_set.erase(played);
				}
				
				// If there's nothing left, stop
				if (remaining_set.empty())
				{
					stack_cue_stop(cue);
				}
				// ...otherwise play a random cue from the remainder
				else
				{
					size_t s_index, s_target_index;
					s_index = 0;
					s_target_index = rand() % remaining_set.size();
					for (auto remainder_uid : remaining_set)
					{
						if (s_index == s_target_index)
						{
							// Play the cue
							stack_cue_play(stack_cue_get_by_uid(remainder_uid));

							// Add the cue to the list of played cues
							gcue->played_cues->push_front(remainder_uid);
							break;
						}
						s_index++;
					}
				}
			}
		}
	}
}

/// Returns which cuelist channels the cue is actively wanting to send audio to
size_t stack_group_cue_get_active_channels(StackCue *cue, bool *channels)
{
	// If we're not in playback then we're not sending data
	if (cue->state != STACK_CUE_STATE_PLAYING_ACTION)
	{
		return 0;
	}
	else
	{
		// We return as many channels as the parent cue list has
		if (channels != NULL)
		{
			for (size_t i = 0; i < cue->parent->channels; i++)
			{
				channels[i] = true;
			}
		}
		return cue->parent->channels;
	}
}

/// Gets the audio for the cue. This is _very_ similar to stack_cue_list_populate_buffers
/// but with several notable differences
size_t stack_group_cue_get_audio(StackCue *cue, float *buffer, size_t frames)
{
	StackGroupCue *gcue = STACK_GROUP_CUE(cue);
	StackCueList *cue_list = cue->parent;

	// TODO: Determine a more appropriate size for this
	size_t request_samples = frames;

	// Allocate a buffer for mixing our new data in to
	float *new_data = buffer;
	bool *new_clipped = new bool[cue_list->channels];
	bool *active_channels_cache = new bool[cue_list->channels];
	memset(new_data, 0, cue_list->channels * request_samples * sizeof(float));

	float *cue_data = NULL;
	size_t cue_data_size = 0;

	// Calculate audio scalar (using the live playback volume). Also use this to
	// scale from 16-bit signed int to 0.0-1.0 range
	double playback_live_volume = 0.0;
	stack_property_get_double(stack_cue_get_property(cue, "play_volume"), STACK_PROPERTY_VERSION_LIVE, &playback_live_volume);
	double base_audio_scaler = stack_db_to_scalar(playback_live_volume);

	// Allocate a buffer for new cue data
	for (auto cue : *gcue->cues)
	{
		// Reset clipping marker array (otherwise we'll set clipped on each subsequent cue)
		memset(new_clipped, 0, cue_list->channels * sizeof(bool));

		// Get the list of active_channels
		memset(active_channels_cache, 0, cue_list->channels * sizeof(bool));
		size_t active_channel_count = stack_cue_get_active_channels(cue, active_channels_cache);

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
			if (!active_channels_cache[dest_channel])
			{
				continue;
			}

			// cue_data is multiplexed, containing active_channel_count channels
			// new_data is multiplexed, containing cue_list->channels channels
			float *read_pointer = &cue_data[source_channel];
			float *end_pointer = &cue_data[active_channel_count * samples_received];
			float *write_pointer = &new_data[dest_channel];
			float channel_rms = 0.0;
			while (read_pointer < end_pointer)
			{
				// Keep track of the RMS whilst we're already looping
				const float value = *read_pointer * base_audio_scaler;
				channel_rms += value * value;

				// Write out the data
				*write_pointer += value;

				// Check for clipping
				if (value > 1.0)
				{
					new_clipped[dest_channel] = true;
				}

				// Move to next sample
				write_pointer += cue_list->channels;
				read_pointer += active_channel_count;
			}

			// Finish off the RMS calculation
			cue_list->rms_cache[source_channel] = stack_scalar_to_db(sqrtf(channel_rms / (float)samples_received));

			// Start the next channel
			source_channel++;
		}

		// Create or update RMS data.
		// TODO: This feels dirty to edit the RMS data of the parent cue list
		// like this. We should make this into a function
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

	// Note that we don't calculate the RMS for the group cue itself here as the
	// parent cue list does that for us

	// Tidy up
	if (cue_data != NULL)
	{
		delete [] cue_data;
	}

	// Tidy up
	delete [] new_clipped;
	delete [] active_channels_cache;

	return frames;
}

/// Sets up the tabs for the action cue
static void stack_group_cue_set_tabs(StackCue *cue, GtkNotebook *notebook)
{
	StackGroupCue *gcue = STACK_GROUP_CUE(cue);

	// Create the tab
	GtkWidget *label = gtk_label_new("Group");

	// Load the UI
	if (sgc_builder == NULL)
	{
		sgc_builder = gtk_builder_new_from_resource("/org/stack/ui/StackGroupCue.ui");

		// Set up callbacks
		gtk_builder_add_callback_symbol(sgc_builder, "gcp_action_changed", G_CALLBACK(gcp_action_changed));

		// Connect the signals
		gtk_builder_connect_signals(sgc_builder, NULL);
	}
	gcue->group_tab = GTK_WIDGET(gtk_builder_get_object(sgc_builder, "gcpGrid"));

	// Pause change callbacks on the properties
	stack_group_cue_pause_change_callbacks(cue, true);

	// Add an extra reference to the action tab - we're about to remove it's
	// parent and we don't want it to get garbage collected
	g_object_ref(gcue->group_tab);

	// The tab has a parent window in the UI file - unparent the tab container from it
	gtk_widget_unparent(gcue->group_tab);

	// Append the tab (and show it, because it starts off hidden...)
	gtk_notebook_append_page(notebook, gcue->group_tab, label);
	gtk_widget_show(gcue->group_tab);

	// Update action option
	int32_t action = STACK_GROUP_CUE_ENTER;
	stack_property_get_int32(stack_cue_get_property(cue, "action"), STACK_PROPERTY_VERSION_DEFINED, &action);
	switch (action)
	{
		case STACK_GROUP_CUE_ENTER:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(sgc_builder, "gcpActionTypeEnter")), true);
			break;
		case STACK_GROUP_CUE_TRIGGER_ALL:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(sgc_builder, "gcpActionTypeTriggerAll")), true);
			break;
		case STACK_GROUP_CUE_TRIGGER_RANDOM:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(sgc_builder, "gcpActionTypeTriggerRandom")), true);
			break;
		case STACK_GROUP_CUE_TRIGGER_PLAYLIST:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(sgc_builder, "gcpActionTypeTriggerPlaylist")), true);
			break;
		case STACK_GROUP_CUE_TRIGGER_SHUFFLED_PLAYLIST:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(sgc_builder, "gcpActionTypeTriggerShuffledPlaylist")), true);
			break;
	}

	// Resume change callbacks on the properties
	stack_group_cue_pause_change_callbacks(cue, false);
}

/// Removes the properties tabs for a action cue
static void stack_group_cue_unset_tabs(StackCue *cue, GtkNotebook *notebook)
{
	// Find our media page
	gint page = gtk_notebook_page_num(notebook, STACK_GROUP_CUE(cue)->group_tab);

	// If we've found the page, remove it
	if (page >= 0)
	{
		gtk_notebook_remove_page(notebook, page);
	}

	// Remove our reference to the action tab
	g_object_unref(STACK_GROUP_CUE(cue)->group_tab);

	// Be tidy
	STACK_GROUP_CUE(cue)->group_tab = NULL;
}

/// Saves the details of this cue as JSON
static char *stack_group_cue_to_json(StackCue *cue)
{
	StackGroupCue *gcue = STACK_GROUP_CUE(cue);

	// Build JSON
	Json::Value root;

	root["cues"] = Json::Value(Json::ValueType::arrayValue);

	// Iterate over all the cues
	for (auto cue : *gcue->cues)
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

	// Write out properties
	stack_property_write_json(stack_cue_get_property(cue, "action"), &root);

	// Write out JSON string and return (to be free'd by
	// stack_fade_cue_free_json)
	Json::FastWriter writer;
	return strdup(writer.write(root).c_str());
}

/// Frees JSON strings as returned by stack_group_cue_to_json
static void stack_group_cue_free_json(StackCue *cue, char *json_data)
{
	free(json_data);
}

/// Re-initialises this cue from JSON Data
void stack_group_cue_from_json(StackCue *cue, const char *json_data)
{
	Json::Value cue_root;
	Json::Reader reader;

	// Parse JSON data
	reader.parse(json_data, json_data + strlen(json_data), cue_root, false);

	// Get the data that's pertinent to us
	if (!cue_root.isMember("StackGroupCue"))
	{
		stack_log("stack_group_cue_from_json(): Missing StackGroupCue class\n");
		return;
	}

	Json::Value& cue_data = cue_root["StackGroupCue"];

	// Read our properties
	stack_group_cue_set_action(STACK_GROUP_CUE(cue), (StackGroupCueAction)cue_data["action"].asInt());

	// If we have some cues...
	if (cue_data.isMember("cues"))
	{
		Json::Value& cues_root = cue_data["cues"];

		if (cues_root.isArray())
		{
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
					stack_log("stack_group_cue_from_json(): Cue missing 'class' parameter, skipping\n");
					continue;
				}

				// Make sure we have a base class
				if (!cue_json.isMember("StackCue"))
				{
					cue_json["_skip"] = 1;
					stack_log("stack_group_cue_from_json(): Cue missing 'StackCue' class, skipping\n");
					continue;
				}

				// Make sure we have a UID
				if (!cue_json["StackCue"].isMember("uid"))
				{
					cue_json["_skip"] = 1;
					stack_log("stack_group_cue_from_json(): Cue missing UID, skipping\n");
					continue;
				}

				// Create a new cue of the correct type
				const char *class_name = cue_json["class"].asString().c_str();
				StackCue *child_cue = stack_cue_new(class_name, cue->parent);
				if (child_cue == NULL)
				{
					stack_log("stack_group_cue_from_json(): Failed to create cue of type '%s', skipping\n", class_name);
					cue_json["_skip"] = 1;

					// TODO: It would be nice if we have some sort of "error cue" which
					// contained the JSON for the cue, so we didn't just drop cues from
					// the stack

					continue;
				}

				// Get the UID of the newly created cue and put a mapping from
				// the old UID to the new UID. Also store it in the JSON object
				// so that we can re-use it on the second loop
				(*cue->parent->uid_remap)[cue_json["StackCue"]["uid"].asUInt64()] = child_cue->uid;
				cue_json["_new_uid"] = (Json::UInt64)child_cue->uid;

				// Call base constructor
				stack_cue_from_json_base(child_cue, cue_json.toStyledString().c_str());

				// Append the cue to the child list
				child_cue->parent_cue = cue;
				STACK_GROUP_CUE(cue)->cues->push_back(child_cue);
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
			}
		}
		else
		{
			stack_log("stack_group_cue_from_json(): 'cues' is not an array\n");
		}
	}
	else
	{
		stack_log("stack_group_cue_from_json(): Missing 'cues' option\n");
	}
}

/// Gets the error message for the cue
void stack_group_cue_get_error(StackCue *cue, char *message, size_t size)
{
	strncpy(message, "", size);
}

/// Returns the icon for a cue
/// @param cue The cue to get the icon of
GdkPixbuf *stack_group_cue_get_icon(StackCue *cue)
{
	return NULL;
}

/// Returns the list of child cues for the cue
/// @param cue The cue to get the children of
StackCueStdList *stack_group_cue_get_children(StackCue *cue)
{
	return STACK_GROUP_CUE(cue)->cues;
}

/// Returns the next cue to go to after this cue
/// @param cue The cue to get the next cue of
StackCue *stack_group_cue_get_next_cue(StackCue *cue)
{
	// Get the current action
	// TODO: Should this be VERSION_LIVE if we're playing?
	int32_t action = STACK_GROUP_CUE_ENTER;
	stack_property_get_int32(stack_cue_get_property(cue, "action"), STACK_PROPERTY_VERSION_DEFINED, &action);

	// If our action is to enter the group, and we have cues in the group, then
	// return the first child cue
	if (action == STACK_GROUP_CUE_ENTER && STACK_GROUP_CUE(cue)->cues->size() != 0)
	{
		return *STACK_GROUP_CUE(cue)->cues->begin();
	}

	// In all other cases, just do the default
	return stack_cue_get_next_cue_base(cue);
}

////////////////////////////////////////////////////////////////////////////////
// CLASS REGISTRATION

// Registers StackGroupCue with the application
void stack_group_cue_register()
{
	// Register built in cue types
	StackCueClass* action_cue_class = new StackCueClass{ "StackGroupCue", "StackCue", stack_group_cue_create, stack_group_cue_destroy, stack_group_cue_play, stack_group_cue_pause, stack_group_cue_stop, stack_group_cue_pulse, stack_group_cue_set_tabs, stack_group_cue_unset_tabs, stack_group_cue_to_json, stack_group_cue_free_json, stack_group_cue_from_json, stack_group_cue_get_error, stack_group_cue_get_active_channels, stack_group_cue_get_audio, NULL, stack_group_cue_get_icon, stack_group_cue_get_children, stack_group_cue_get_next_cue };
	stack_register_cue_class(action_cue_class);
}
