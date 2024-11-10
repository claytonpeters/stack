// Includes:
#include "StackApp.h"
#include "StackLog.h"
#include "StackTimeTrigger.h"
#include "StackGtkHelper.h"
#include "StackJson.h"
#include <list>

// The list of active triggers for the thread
std::list<StackTimeTrigger*> trigger_list;

// The mutex lock around our list (might not be necessary as everything happens
// on the UI thread)
std::mutex list_mutex;

// Whether the timer is running
bool timer_running = false;

////////////////////////////////////////////////////////////////////////////////
// ACTION FUNCTIONS

gboolean stack_time_trigger_timer(void *user_data)
{
	gboolean result = true;

	// Prevent multi-thread acces
	list_mutex.lock();

	// If the length of the list is zero, stop the timer
	if (trigger_list.size() == 0)
	{
		stack_log("stack_time_trigger_timer(): No time triggers left, stopping timer\n");
		timer_running = false;
		result = false;
	}
	else
	{
		// Current time UTC in both timestamp and struct formats
		time_t current_time;
		time(&current_time);
		struct tm today_time_struct;
		gmtime_r(&current_time, &today_time_struct);

		for (auto time_trigger : trigger_list)
		{
			// Skip unconfigured triggers
			if (!time_trigger->configured)
			{
				continue;
			}

			// Calculate the next trigger time if we don't have one
			if (time_trigger->next_trigger_ts == 0)
			{
				struct tm next_time_struct;

				if (time_trigger->year == 0 && time_trigger->month == 0 && time_trigger->day == 0)
				{
					memcpy(&next_time_struct, &today_time_struct, sizeof(struct tm));
					next_time_struct.tm_year = today_time_struct.tm_year;
					next_time_struct.tm_mon = today_time_struct.tm_mon;
					next_time_struct.tm_mday = today_time_struct.tm_mday;
				}
				else
				{
					next_time_struct.tm_year = time_trigger->year - 1900;
					next_time_struct.tm_mon = time_trigger->month - 1;
					next_time_struct.tm_mday = time_trigger->day;
					next_time_struct.tm_isdst = 0;
					next_time_struct.tm_gmtoff = 0;
				}

				next_time_struct.tm_hour = time_trigger->hour;
				next_time_struct.tm_min = time_trigger->minute;
				next_time_struct.tm_sec = time_trigger->second;

				time_trigger->next_trigger_ts = mktime(&next_time_struct);

				if (time_trigger->next_trigger_ts < current_time)
				{
					// Calculated cue time is in the past - check repeat interval to
					// see if we should fire again
					if (time_trigger->repeat != 0)
					{
						// See how many times the trigger has been missed (floored
						// because of integer arithmetic)
						uint32_t missed = (current_time - time_trigger->next_trigger_ts) / time_trigger->repeat;

						// Add on that many plus one (so that we're in the future)
						// repeat intervals to get the next trigger time
						time_trigger->next_trigger_ts += (missed + 1) * time_trigger->repeat;
					}
					else
					{
						time_trigger->next_trigger_ts = 0;
					}
				}

				// Mostly for debugging
				if (time_trigger->next_trigger_ts >= current_time)
				{
					stack_log("stack_time_trigger_timer(): Trigger 0x%016lx for cue %016lx is scheduled for %d\n", time_trigger, STACK_TRIGGER(time_trigger)->cue->uid, time_trigger->next_trigger_ts);
				}
			}

			// If the trigger time has passed (and we haven't already fired), fire the action
			if (time_trigger->next_trigger_ts != 0 && current_time >= time_trigger->next_trigger_ts && time_trigger->next_trigger_ts > time_trigger->last_trigger_ts)
			{
				// Get the cue and the action
				StackCue *cue = STACK_TRIGGER(time_trigger)->cue;
				StackTriggerAction action = stack_trigger_get_action(STACK_TRIGGER(time_trigger));

				// Run the correct action
				stack_cue_list_lock(cue->parent);
				switch (action)
				{
					case STACK_TRIGGER_ACTION_STOP:
						stack_cue_stop(cue);
						break;
					case STACK_TRIGGER_ACTION_PAUSE:
						stack_cue_pause(cue);
						break;
					case STACK_TRIGGER_ACTION_PLAY:
						stack_cue_play(cue);
						break;
				}
				stack_cue_list_unlock(cue->parent);

				// Record when we were last triggered
				time_trigger->last_trigger_ts = time_trigger->next_trigger_ts;

				// Recalculate the next time
				if (time_trigger->repeat != 0)
				{
					time_trigger->next_trigger_ts += time_trigger->repeat;
					stack_log("stack_time_trigger_timer(): Trigger 0x%016lx for cue %016lx is next scheduled for %d\n", time_trigger, STACK_TRIGGER(time_trigger)->cue->uid, time_trigger->next_trigger_ts);
				}
				else
				{
					// Not repeating, so we never fire again
					time_trigger->next_trigger_ts = 0;
				}
			}
		}
	}

	list_mutex.unlock();

	return result;
}

////////////////////////////////////////////////////////////////////////////////
// CREATION AND DESTRUCTION

/// Creates a key trigger
StackTrigger* stack_time_trigger_create(StackCue *cue)
{
	// Allocate the trigger
	StackTimeTrigger *trigger = new StackTimeTrigger();

	// Initialise the superclass
	stack_trigger_init(&trigger->super, cue);

	// Make this class a StackTimeTrigger
	STACK_TRIGGER(trigger)->_class_name = "StackTimeTrigger";

	// Initial setup
	trigger->description = strdup("");
	trigger->year = 0;
	trigger->month = 0;
	trigger->day = 0;
	trigger->hour = 0;
	trigger->minute = 0;
	trigger->second = 0;
	trigger->repeat = 0;
	trigger->last_trigger_ts = 0;
	trigger->next_trigger_ts = 0;
	trigger->configured = false;
	strncpy(trigger->event_text, "", 128);

	// Add us to the list of triggers
	list_mutex.lock();
	trigger_list.push_back(trigger);

	// Create a timer if we don't have one
	if (!timer_running)
	{
		timer_running = true;
		stack_log("stack_time_trigger_create(): Creating timer\n");
		g_timeout_add(250, stack_time_trigger_timer, NULL);
	}

	// We're done with list actions now
	list_mutex.unlock();

	return STACK_TRIGGER(trigger);
}

/// Destroys a key trigger
void stack_time_trigger_destroy(StackTrigger *trigger)
{
	// Remove ourselves from the list
	list_mutex.lock();
	trigger_list.remove(STACK_TIME_TRIGGER(trigger));
	list_mutex.unlock();

	if (STACK_TIME_TRIGGER(trigger)->description != NULL)
	{
		free(STACK_TIME_TRIGGER(trigger)->description);
	}

	// Call parent destructor
	stack_trigger_destroy_base(trigger);
}

////////////////////////////////////////////////////////////////////////////////
// OVERRIDDEN FUNCTIONS

// Return either "Key Pressed" or "Key Released" as the text depending on what
// the trigger is configured for
const char* stack_time_trigger_get_name(StackTrigger *trigger)
{
	return "Timer";
}

// Returns the name of the key we're triggered off
const char* stack_time_trigger_get_event_text(StackTrigger *trigger)
{
	StackTimeTrigger *time_trigger = STACK_TIME_TRIGGER(trigger);

	uint32_t scale = 1;
	const char *unit = "seconds";
	if (time_trigger->repeat % 86400 == 0)
	{
		unit = "day(s)";
		scale = 86400;
	}
	else if (time_trigger->repeat % 3600 == 0)
	{
		unit = "hour(s)";
		scale = 3600;
	}
	else if (time_trigger->repeat % 60 == 0)
	{
		unit = "minute(s)";
		scale = 60;
	}

	if (time_trigger->repeat == 0)
	{
		if (time_trigger->year == 0 && time_trigger->month == 0 && time_trigger->day == 0)
		{
			snprintf(time_trigger->event_text, 128, "At %02d:%02d:%02d", time_trigger->hour, time_trigger->minute, time_trigger->second);
		}
		else
		{
			snprintf(time_trigger->event_text, 128, "At %d-%02d-%02d %02d:%02d:%02d", time_trigger->year, time_trigger->month, time_trigger->day, time_trigger->hour, time_trigger->minute, time_trigger->second);
		}
	}
	else
	{
		if (time_trigger->year == 0 && time_trigger->month == 0 && time_trigger->day == 0)
		{
			snprintf(time_trigger->event_text, 128, "Every %d %s after %02d:%02d:%02d", time_trigger->repeat / scale, unit, time_trigger->hour, time_trigger->minute, time_trigger->second);
		}
		else
		{
			snprintf(time_trigger->event_text, 128, "Every %d %s after %d-%02d-%02d %02d:%02d:%02d", time_trigger->repeat / scale, unit, time_trigger->year, time_trigger->month, time_trigger->day, time_trigger->hour, time_trigger->minute, time_trigger->second);
		}
	}
	return time_trigger->event_text;
}

// Returns the user-specified description
const char* stack_time_trigger_get_description(StackTrigger *trigger)
{
	return STACK_TIME_TRIGGER(trigger)->description;
}

char *stack_time_trigger_to_json(StackTrigger *trigger)
{
	Json::Value trigger_root;
	StackTimeTrigger *time_trigger = STACK_TIME_TRIGGER(trigger);

	trigger_root["description"] = time_trigger->description;
	trigger_root["year"] = time_trigger->year;
	trigger_root["month"] = time_trigger->month;
	trigger_root["day"] = time_trigger->day;
	trigger_root["hour"] = time_trigger->hour;
	trigger_root["minute"] = time_trigger->minute;
	trigger_root["second"] = time_trigger->second;
	trigger_root["repeat"] = time_trigger->repeat;

	Json::StreamWriterBuilder builder;
	return strdup(Json::writeString(builder, trigger_root).c_str());
}

void stack_time_trigger_free_json(StackTrigger *trigger, char *json_data)
{
	free(json_data);
}

void stack_time_trigger_from_json(StackTrigger *trigger, const char *json_data)
{
	Json::Value trigger_root;

	// Call the superclass version
	stack_trigger_from_json_base(trigger, json_data);

	// Parse JSON data
	stack_json_read_string(json_data, &trigger_root);

	// Get the data that's pertinent to us
	Json::Value& trigger_data = trigger_root["StackTimeTrigger"];

	StackTimeTrigger *time_trigger = STACK_TIME_TRIGGER(trigger);
	if (time_trigger->description != NULL)
	{
		free(time_trigger->description);
	}
	time_trigger->description = strdup(trigger_data["description"].asString().c_str());

	if (trigger_data.isMember("year"))
	{
		time_trigger->year = trigger_data["year"].asInt();
	}

	if (trigger_data.isMember("month"))
	{
		time_trigger->month = trigger_data["month"].asInt();
	}

	if (trigger_data.isMember("day"))
	{
		time_trigger->day = trigger_data["day"].asInt();
	}

	if (trigger_data.isMember("hour"))
	{
		time_trigger->hour = trigger_data["hour"].asInt();
	}

	if (trigger_data.isMember("minute"))
	{
		time_trigger->minute = trigger_data["minute"].asInt();
	}

	if (trigger_data.isMember("second"))
	{
		time_trigger->second = trigger_data["second"].asInt();
	}

	if (trigger_data.isMember("repeat"))
	{
		time_trigger->repeat = trigger_data["repeat"].asInt();
	}
}

////////////////////////////////////////////////////////////////////////////////
// CONFIGURATION USER INTERFACE

bool stack_time_trigger_show_config_ui(StackTrigger *trigger, GtkWidget *parent, bool new_trigger)
{
	bool result = false;
	StackTimeTrigger *time_trigger = STACK_TIME_TRIGGER(trigger);

	// Build the dialog
	GtkBuilder *builder = gtk_builder_new_from_resource("/org/stack/ui/StackTimeTrigger.ui");
	GtkDialog *dialog = GTK_DIALOG(gtk_builder_get_object(builder, "timeTriggerDialog"));
	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));

	// Callbacks
	//gtk_builder_add_callback_symbol(builder, "ttd_key_button_clicked", G_CALLBACK(ttd_key_button_clicked));
	//gtk_builder_add_callback_symbol(builder, "ttd_key_keypress", G_CALLBACK(ttd_key_keypress));
	gtk_builder_connect_signals(builder, trigger);

	// Set up response buttons
	gtk_dialog_add_buttons(dialog, "Cancel", 2, "OK", 1, NULL);
	gtk_dialog_set_default_response(dialog, 1);

	// We use these a lot
	GtkEntry *ttdDescriptionEntry = GTK_ENTRY(gtk_builder_get_object(builder, "ttdDescriptionEntry"));
	GtkEntry *ttdDateEntry = GTK_ENTRY(gtk_builder_get_object(builder, "ttdDateEntry"));
	GtkEntry *ttdTimeEntry = GTK_ENTRY(gtk_builder_get_object(builder, "ttdTimeEntry"));
	GtkEntry *ttdRepeatEntry = GTK_ENTRY(gtk_builder_get_object(builder, "ttdRepeatEntry"));
	GtkComboBox *ttdRepeatUnitCombo = GTK_COMBO_BOX(gtk_builder_get_object(builder, "ttdRepeatUnitCombo"));
	GtkToggleButton *ttdActionStop = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "ttdActionStop"));
	GtkToggleButton *ttdActionPause = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "ttdActionPause"));
	GtkToggleButton *ttdActionPlay = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "ttdActionPlay"));

	// Set helpers
	stack_limit_gtk_entry_int(ttdRepeatEntry, false);
	stack_limit_gtk_entry_time(ttdTimeEntry, false);
	stack_limit_gtk_entry_date(ttdDateEntry);

	// Set the values on the dialog
	gtk_entry_set_text(ttdDescriptionEntry, time_trigger->description);
	char buffer[64];
	if (time_trigger->year != 0)
	{
		snprintf(buffer, 64, "%d-%02d-%02d", time_trigger->year, time_trigger->month, time_trigger->day);
		gtk_entry_set_text(ttdDateEntry, buffer);
	}
	if (!new_trigger)
	{
		snprintf(buffer, 64, "%02d:%02d:%02d", time_trigger->hour, time_trigger->minute, time_trigger->second);
		gtk_entry_set_text(ttdTimeEntry, buffer);

		if (time_trigger->repeat != 0)
		{
			guint scale = 1;
			if (time_trigger->repeat % 86400 == 0)
			{
				scale = 86400;
			}
			else if (time_trigger->repeat % 3600 == 0)
			{
				scale = 3600;
			}
			else if (time_trigger->repeat % 60 == 0)
			{
				scale = 60;
			}
			snprintf(buffer, 64, "%d", time_trigger->repeat / scale);
			gtk_entry_set_text(ttdRepeatEntry, buffer);
			snprintf(buffer, 64, "%d", scale);
			gtk_combo_box_set_active_id(ttdRepeatUnitCombo, buffer);
		}
		else
		{
			gtk_combo_box_set_active_id(ttdRepeatUnitCombo, "86400");
		}
	}
	else
	{
		// Default to days as that's probably the most common use case for this
		// trigger (sound check, house preset, etc.)
		gtk_combo_box_set_active_id(ttdRepeatUnitCombo, "86400");
	}

	switch (trigger->action)
	{
		case STACK_TRIGGER_ACTION_STOP:
			gtk_toggle_button_set_active(ttdActionStop, true);
			break;
		case STACK_TRIGGER_ACTION_PAUSE:
			gtk_toggle_button_set_active(ttdActionPause, true);
			break;
		default:
		case STACK_TRIGGER_ACTION_PLAY:
			gtk_toggle_button_set_active(ttdActionPlay, true);
			break;
	}

	// Run the dialog
	gint response = gtk_dialog_run(dialog);

	switch (response)
	{
		case 1:	// OK
			// Store the action
			if (gtk_toggle_button_get_active(ttdActionStop))
			{
				trigger->action = STACK_TRIGGER_ACTION_STOP;
			}
			else if (gtk_toggle_button_get_active(ttdActionPause))
			{
				trigger->action = STACK_TRIGGER_ACTION_PAUSE;
			}
			else if (gtk_toggle_button_get_active(ttdActionPlay))
			{
				trigger->action = STACK_TRIGGER_ACTION_PLAY;
			}

			// Update the description
			if (time_trigger->description != NULL)
			{
				free(time_trigger->description);
			}
			time_trigger->description = strdup(gtk_entry_get_text(ttdDescriptionEntry));

			// Update repeat interval
			time_trigger->repeat = atoi(gtk_entry_get_text(ttdRepeatEntry)) * atoi(gtk_combo_box_get_active_id(ttdRepeatUnitCombo));

			// Update date/time
			if (strlen(gtk_entry_get_text(ttdDateEntry)) == 0)
			{
				time_trigger->year = 0;
				time_trigger->month = 0;
				time_trigger->day = 0;
			}
			else
			{
				sscanf(gtk_entry_get_text(ttdDateEntry), "%hu-%02hhu-%02hhu", &time_trigger->year, &time_trigger->month, &time_trigger->day);
			}
			sscanf(gtk_entry_get_text(ttdTimeEntry), "%02hhu:%02hhu:%02hhu", &time_trigger->hour, &time_trigger->minute, &time_trigger->second);

			// Reset the trigger timestamps so the timer recalculates the trigger time
			time_trigger->next_trigger_ts = 0;
			time_trigger->last_trigger_ts = 0;

			// Mark this trigger as ready to be used
			time_trigger->configured = true;
			result = true;
			break;
		case 2: // Cancel
			result = false;
			break;
	}

	// Destroy the dialog
	gtk_widget_destroy(GTK_WIDGET(dialog));

	// Free the builder
	g_object_unref(builder);

	return result;
}

////////////////////////////////////////////////////////////////////////////////
// CLASS REGISTRATION

// Registers StackTimeTrigger with the application
void stack_time_trigger_register()
{
	// Register built in cue types
	StackTriggerClass* time_trigger_class = new StackTriggerClass{
		"StackTimeTrigger",
		"StackTrigger",
		"Wall-clock Time",
		stack_time_trigger_create,
		stack_time_trigger_destroy,
		stack_time_trigger_get_name,
		stack_time_trigger_get_event_text,
		stack_time_trigger_get_description,
		NULL, //get_action
		stack_time_trigger_to_json,
		stack_time_trigger_free_json,
		stack_time_trigger_from_json,
		stack_time_trigger_show_config_ui,
		NULL,
		NULL,
		NULL
	};
	stack_register_trigger_class(time_trigger_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_time_trigger_register();
	return true;
}
