// Includes:
#include "StackCue.h"
#include <cstring>
#include <json/json.h>

// The base StackCue create function. This always returns NULL as we disallow the creation of
// base StackCue objects.
StackCue *stack_cue_create_base(StackCueList *cue_list)
{
	fprintf(stderr, "stack_cue_create_base(): Objects of type StackCue cannot be created\n");
	return NULL;
}

// Destroys a base cue
void stack_cue_destroy_base(StackCue *cue)
{
	// Free the super class stuff
	free(cue->name);
	free(cue->notes);
	
	// Delete ourselves
	delete cue;
}

// Plays a base cue
bool stack_cue_play_base(StackCue *cue)
{
	fprintf(stderr, "stack_cue_play_base()\n");

	// We can only play a cue that is stopped, prepared or paused
	if (cue->state != STACK_CUE_STATE_STOPPED && cue->state != STACK_CUE_STATE_PREPARED && cue->state != STACK_CUE_STATE_PAUSED)
	{
		// We didn't start playing, return false
		return false;
	}
	
	// Get the clock time
	stack_time_t clocktime = stack_get_clock_time();
	
	if (cue->state == STACK_CUE_STATE_PAUSED)
	{
		// Calculate how long we've been paused on this instance of us being
		// paused
		stack_time_t this_pause_time = clocktime - cue->pause_time;
		
		// Update the total cue paused time
		cue->paused_time = cue->pause_paused_time + this_pause_time;
		cue->pause_time = 0;
		cue->pause_paused_time = 0;

		// Calculated the time we've been elapsed (this is up-to-the-nanosecond)
		stack_time_t clock_elapsed = (clocktime - cue->start_time);
		stack_time_t cue_elapsed = clock_elapsed - cue->paused_time;
		
		// Put us in the right playing state depending on where we are
		if (cue_elapsed < cue->pre_time)
		{
			cue->state = STACK_CUE_STATE_PLAYING_PRE;
		}
		else if (cue_elapsed < cue->pre_time + cue->action_time)
		{
			cue->state = STACK_CUE_STATE_PLAYING_ACTION;
		}
		else
		{
			cue->state = STACK_CUE_STATE_PLAYING_POST;
		}
	}
	else
	{
		// Cue is stopped (and possibly prepared), start the cue
		cue->start_time = clocktime;

		if (cue->pre_time > 0)
		{
			cue->state = STACK_CUE_STATE_PLAYING_PRE;
		}
		else
		{
			// We should always get at least one pulse in action state even if
			// our action time is zero, as we may want to do instantaneous things
			// like immediate fades, for example
			cue->state = STACK_CUE_STATE_PLAYING_ACTION;
		}
	}
	
	return true;
}

// Pauses a base cue
void stack_cue_pause_base(StackCue *cue)
{
	fprintf(stderr, "stack_cue_pause_base()\n");

	cue->pause_time = stack_get_clock_time();
	cue->pause_paused_time = cue->paused_time;
}

// Stops a base cue
void stack_cue_stop_base(StackCue *cue)
{
	fprintf(stderr, "stack_cue_stop_base()\n");

	cue->start_time = 0;
	cue->pause_time = 0;
	cue->paused_time = 0;
	cue->pause_paused_time = 0;
	cue->state = STACK_CUE_STATE_STOPPED;
}

// Provides the pulse for a base cue
void stack_cue_pulse_base(StackCue *cue, stack_time_t clocktime)
{
	// We don't need to do anything if the cue is stopped or in error state
	if (cue->state == STACK_CUE_STATE_STOPPED || cue->state == STACK_CUE_STATE_ERROR)
	{
		return;
	}
	
	// Get the current cue action times
	stack_time_t run_pre_time, run_action_time, run_post_time;
	stack_cue_get_running_times(cue, clocktime, &run_pre_time, &run_action_time, &run_post_time, NULL, NULL, NULL);

	// If pre hasn't finished, return
	if (cue->state == STACK_CUE_STATE_PLAYING_PRE && run_pre_time < cue->pre_time)
	{
		// Do nothing;
		return;
	}
	
	// If we're in pre, but pre has finished
	if (cue->state == STACK_CUE_STATE_PLAYING_PRE && run_action_time > 0)
	{
		// If we're still in action time
		if (run_action_time < cue->action_time)
		{
			// Change us to the action state
			fprintf(stderr, "stack_cue_pulse_base(): Moving from pre to action\n");
			cue->state = STACK_CUE_STATE_PLAYING_ACTION;
			return;
		}

		// We're no longer in action time, see if post is still running
		if (run_post_time > 0)
		{
			if (run_post_time < cue->post_time)
			{
				// Change us to the post state
				fprintf(stderr, "stack_cue_pulse_base(): Moving from pre to post\n");
				cue->state = STACK_CUE_STATE_PLAYING_POST;
				return;
			}
			else
			{
				// Stop the cue
				fprintf(stderr, "stack_cue_pulse_base(): Moving from pre to stopped\n");
				stack_cue_stop(cue);
				return;
			}
		}
	}
	
	// If we're in action, but action time has finished
	if (cue->state == STACK_CUE_STATE_PLAYING_ACTION && run_action_time == cue->action_time)
	{
		// Check if we're still in post time
		if (run_post_time < cue->post_time)
		{
			// Change us to the post state
			fprintf(stderr, "stack_cue_pulse_base(): Moving from action to post\n");
			cue->state = STACK_CUE_STATE_PLAYING_POST;
			return;
		}
		else
		{
			// Stop the cue
			fprintf(stderr, "stack_cue_pulse_base(): Moving from action to stopped\n");
			stack_cue_stop(cue);
		}
	}
		
	// If we're in post, but post time has finished
	if (cue->state == STACK_CUE_STATE_PLAYING_POST && run_post_time == cue->post_time)
	{
		// Stop the cue
		fprintf(stderr, "stack_cue_pulse_base(): Moving from post to stopped\n");
		stack_cue_stop(cue);
		return;
	}
}

// Does nothing for the base class - the base tabs are always there
void stack_cue_set_tabs_base(StackCue *cue, GtkNotebook *notebook)
{
	return;
}

// Does nothing for the base class - we never remove the base tabs
void stack_cue_unset_tabs_base(StackCue *cue, GtkNotebook *notebook)
{
	return;
}

// Returns a JSON string that represents the data contained within the base
// class
char *stack_cue_to_json_base(StackCue *cue)
{
	Json::Value cue_root;

	cue_root["r"] = cue->r;
	cue_root["g"] = cue->g;
	cue_root["b"] = cue->b;
	cue_root["id"] = cue->id;
	cue_root["uid"] = (Json::UInt64)cue->uid;
	cue_root["name"] = cue->name;
	cue_root["notes"] = cue->notes;
	cue_root["pre_time"] = (Json::Int64)cue->pre_time;
	cue_root["post_time"] = (Json::Int64)cue->post_time;
	cue_root["action_time"] = (Json::Int64)cue->action_time;
	cue_root["post_trigger"] = cue->post_trigger;

	// Write out the JSON string and return it (to be free'd by 
	// stack_cue_free_json_base)
	Json::FastWriter writer;
	return strdup(writer.write(cue_root).c_str());
}

// Frees JSON strings returned by stack_cue_to_json_base
void stack_cue_free_json_base(char *json_data)
{
	free(json_data);
}

// Re-initialises this cue from JSON Data
void stack_cue_from_json_base(StackCue *cue, const char *json_data)
{
	fprintf(stderr, "stack_cue_from_json_base()\n");
	
	Json::Value cue_root;
	Json::Reader reader;
	
	// Parse JSON data
	reader.parse(json_data, json_data + strlen(json_data), cue_root, false);
	
	// Get the data that's pertinent to us
	Json::Value& stack_cue_data = cue_root["StackCue"];
	
	// Copy the data in to the cue
	stack_cue_set_color(cue, stack_cue_data["r"].asDouble(), stack_cue_data["g"].asDouble(), stack_cue_data["b"].asDouble());
	stack_cue_set_id(cue, stack_cue_data["id"].asInt());
	stack_cue_set_name(cue, stack_cue_data["name"].asString().c_str());
	stack_cue_set_notes(cue, stack_cue_data["notes"].asString().c_str());
	stack_cue_set_pre_time(cue, stack_cue_data["pre_time"].asInt64());
	stack_cue_set_post_time(cue, stack_cue_data["post_time"].asInt64());
	stack_cue_set_action_time(cue, stack_cue_data["action_time"].asInt64());
	stack_cue_set_post_trigger(cue, (StackCueWaitTrigger)stack_cue_data["post_trigger"].asInt());
}

// Sets the cue number / ID
// @param cue The cue to change
// @param id The new cue number
void stack_cue_set_id(StackCue *cue, cue_id_t id)
{
	cue->id = id;
	stack_cue_list_changed(cue->parent, cue);
}

// Sets the cue name
// @param cue The cue to change
// @param name The new cue name
void stack_cue_set_name(StackCue *cue, const char *name)
{
	free(cue->name);
	cue->name = strdup(name);
	stack_cue_list_changed(cue->parent, cue);
}

// Sets the cue notes
// @param cue The cue to change
// @param notes The new cue notes
void stack_cue_set_notes(StackCue *cue, const char *notes)
{
	free(cue->notes);
	cue->notes = strdup(notes);
	stack_cue_list_changed(cue->parent, cue);
}

// Sets the cue pre-wait time
// @param cue The cue to change
// @param pre_time The new cue pre-wait time
void stack_cue_set_pre_time(StackCue *cue, stack_time_t pre_time)
{
	cue->pre_time = pre_time;
	stack_cue_list_changed(cue->parent, cue);
}

// Sets the cue action time
// @param cue The cue to change
// @param action_time The new cue action time
void stack_cue_set_action_time(StackCue *cue, stack_time_t action_time)
{
	cue->action_time = action_time;
	stack_cue_list_changed(cue->parent, cue);
}

// Sets the cue post-wait time
// @param cue The cue to change
// @param post_time The new cue post-wait time
void stack_cue_set_post_time(StackCue *cue, stack_time_t post_time)
{
	cue->post_time = post_time;
	stack_cue_list_changed(cue->parent, cue);
}

// Sets the cue state
// @param cue The cue to change
// @param state The new cue state
void stack_cue_set_state(StackCue *cue, StackCueState state)
{
	cue->state = state;
	stack_cue_list_changed(cue->parent, cue);
}

// Sets the cue color
// @param cue The cue to change
// @param r The red component of the color
// @param g The green component of the color
// @param b The blue component of the color
void stack_cue_set_color(StackCue *cue, uint8_t r, uint8_t g, uint8_t b)
{
	cue->r = r;
	cue->g = g;
	cue->b = b;
	stack_cue_list_changed(cue->parent, cue);
}

// Sets the cue post-wait trigger
// @param cue The cue to change
// @param post_trigger The new cue post-wait trigger
void stack_cue_set_post_trigger(StackCue *cue, StackCueWaitTrigger post_trigger)
{
	cue->post_trigger = post_trigger;
	stack_cue_list_changed(cue->parent, cue);
}

