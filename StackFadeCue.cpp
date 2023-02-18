// Includes:
#include "StackApp.h"
#include "StackFadeCue.h"
#include "StackAudioCue.h"
#include "StackLog.h"
#include <cstring>
#include <cstdlib>
#include <string>
#include <cmath>
#include <json/json.h>

////////////////////////////////////////////////////////////////////////////////
// CREATION AND DESTRUCTION

/// Creates a fade cue
static StackCue* stack_fade_cue_create(StackCueList *cue_list)
{
	// Allocate the cue
	StackFadeCue* cue = new StackFadeCue();

	// Initialise the superclass
	stack_cue_init(&cue->super, cue_list);

	// Make this class a StackFadeCue
	cue->super._class_name = "StackFadeCue";

	// We start in error state until we have a target
	stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_ERROR);

	// Set the default fade (action) time to be five seconds
	stack_cue_set_action_time(STACK_CUE(cue), 5 * NANOSECS_PER_SEC);

	// Initialise our variables
	cue->target = STACK_CUE_UID_NONE;
	cue->target_volume = -INFINITY;
	cue->stop_target = true;
	cue->profile = STACK_FADE_PROFILE_EXP;
	cue->builder = NULL;
	cue->fade_tab = NULL;
	cue->target_cue_id_string[0] = '\0';

	// Initialise superclass variables
	stack_cue_set_name(STACK_CUE(cue), "${action} cue ${target}");

	return STACK_CUE(cue);
}

/// Destroys a fade cue
static void stack_fade_cue_destroy(StackCue *cue)
{
	// Tidy up
	if (STACK_FADE_CUE(cue)->builder)
	{
		// Remove our reference to the fade tab
		g_object_ref(STACK_FADE_CUE(cue)->fade_tab);

		// Destroy the top level widget in the builder
		gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(STACK_FADE_CUE(cue)->builder, "window1")));

		// Unref the builder
		g_object_unref(STACK_FADE_CUE(cue)->builder);
	}

	// Call parent destructor
	stack_cue_destroy_base(cue);
}

////////////////////////////////////////////////////////////////////////////////
// PROPERTY SETTERS

// Sets the target cue of a fade cue
void stack_fade_cue_set_target(StackFadeCue *cue, StackCue *target)
{
	if (target == NULL)
	{
		cue->target = STACK_CUE_UID_NONE;
		stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_ERROR);
	}
	else
	{
		cue->target = target->uid;
		stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_STOPPED);
	}

	// Notify cue list that we've changed
	stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue));

	// Notify UI to update (name might have changed as a result)
	StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->fade_tab));
	g_signal_emit_by_name((gpointer)window, "update-selected-cue");
}

/// Sets the change in volume of the target cue
void stack_fade_cue_set_target_volume(StackFadeCue *cue, double volume)
{
	if (volume < -49.99)
	{
		cue->target_volume = -INFINITY;
	}
	else
	{
		cue->target_volume = volume;
	}

	// Notify cue list that we've changed
	stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue));
}

/// Sets whether the Stop Target flag is enabled
void stack_fade_cue_set_stop_target(StackFadeCue *cue, bool stop_target)
{
	cue->stop_target = stop_target;

	// Notify cue list that we've changed
	stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue));

	// Notify UI to update (name might have changed as a result)
	StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->fade_tab));
	g_signal_emit_by_name((gpointer)window, "update-selected-cue");
}

/// Sets the fade profile
void stack_fade_cue_set_profile(StackFadeCue *cue, StackFadeProfile profile)
{
	cue->profile = profile;

	// Notify cue list that we've changed
	stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue));
}

////////////////////////////////////////////////////////////////////////////////
// UI CALLBACKS

/// Called when the Target Cue button is pressed
static void fcp_cue_changed(GtkButton *widget, gpointer user_data)
{
	// Get the cue
	StackFadeCue *cue = STACK_FADE_CUE(user_data);

	// Get the parent window
	StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->fade_tab));

	// Call the dialog and get the new target
	StackCue *new_target = stack_select_cue_dialog(window, stack_cue_get_by_uid(cue->target), STACK_CUE(cue));

	// Update the cue
	stack_fade_cue_set_target(cue, new_target);

	// Store the cue in the target and update the state
	if (new_target == NULL)
	{
		// Update the UI
		gtk_button_set_label(widget, "Select Cue...");
	}
	else
	{
		// Build cue number
		char cue_number[32];
		stack_cue_id_to_string(new_target->id, cue_number, 32);

		// Build the string
		std::string button_text;
		button_text = std::string(cue_number) + ": " + std::string(stack_cue_get_rendered_name(new_target));

		// Update the UI
		gtk_button_set_label(widget, button_text.c_str());
	}

	// Fire an updated-selected-cue signal to signal the UI to change (we might
	// have changed state)
	g_signal_emit_by_name((gpointer)window, "update-selected-cue");
}

/// Called when the volume slider changes
static void fcp_volume_changed(GtkRange *range, gpointer user_data)
{
	StackFadeCue *cue = STACK_FADE_CUE(user_data);

	// Get the volume and store it
	double vol_db = gtk_range_get_value(range);
	stack_fade_cue_set_target_volume(cue, vol_db);

	// Build a string of the volume
	char buffer[32];
	if (cue->target_volume < -49.99)
	{
		snprintf(buffer, 32, "-Inf dB");
	}
	else
	{
		snprintf(buffer, 32, "%.2f dB", cue->target_volume);
	}

	// Get the volume label and update it's value
	gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(cue->builder, "fcpVolumeValueLabel")), buffer);
}

/// Called when the fade time edit box loses focus
static gboolean fcp_fade_time_changed(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	StackFadeCue *cue = STACK_FADE_CUE(user_data);

	// Set the time
	stack_cue_set_action_time(STACK_CUE(cue), stack_time_string_to_ns(gtk_entry_get_text(GTK_ENTRY(widget))));

	// Update the UI
	char buffer[32];
	stack_format_time_as_string(STACK_CUE(cue)->action_time, buffer, 32);
	gtk_entry_set_text(GTK_ENTRY(widget), buffer);

	// Fire an updated-selected-cue signal to signal the UI to change
	g_signal_emit_by_name((gpointer)gtk_widget_get_toplevel(widget), "update-selected-cue");

	return false;
}

/// Called when the fade profile changes
static void fcp_profile_changed(GtkToggleButton *widget, gpointer user_data)
{
	StackFadeCue *cue = STACK_FADE_CUE(user_data);

	// Get pointers to the four radio button options
	GtkToggleButton* r1 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(cue->builder, "fcpFadeTypeLinear"));
	GtkToggleButton* r2 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(cue->builder, "fcpFadeTypeQuadratic"));
	GtkToggleButton* r3 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(cue->builder, "fcpFadeTypeExponential"));
	GtkToggleButton* r4 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(cue->builder, "fcpFadeTypeInvExponential"));

	// Determine which one is toggled on
	if (widget == r1 && gtk_toggle_button_get_active(r1)) { stack_fade_cue_set_profile(cue, STACK_FADE_PROFILE_LINEAR); }
	if (widget == r2 && gtk_toggle_button_get_active(r2)) { stack_fade_cue_set_profile(cue, STACK_FADE_PROFILE_QUAD); }
	if (widget == r3 && gtk_toggle_button_get_active(r3)) { stack_fade_cue_set_profile(cue, STACK_FADE_PROFILE_EXP); }
	if (widget == r4 && gtk_toggle_button_get_active(r4)) { stack_fade_cue_set_profile(cue, STACK_FADE_PROFILE_INVEXP); }

	// No need to update the UI on this one (currently)
}

/// Called when the Stop Target checkbox changes
static void fcp_stop_target_changed(GtkToggleButton *widget, gpointer user_data)
{
	// Update the variable
	stack_fade_cue_set_stop_target(STACK_FADE_CUE(user_data), gtk_toggle_button_get_active(widget));
}

////////////////////////////////////////////////////////////////////////////////
// BASE CUE OPERATIONS

/// Start the cue playing
static bool stack_fade_cue_play(StackCue *cue)
{
	// Call the superclass
	if (!stack_cue_play_base(cue))
	{
		return false;
	}

	// Get the target cue
	StackCue *target = stack_cue_get_by_uid(STACK_FADE_CUE(cue)->target);

	// If we've found the target and it's a StackAudioCue...
	if (target != NULL && strcmp(target->_class_name, "StackAudioCue") == 0)
	{
		// ...take note of the current cue volume
		STACK_FADE_CUE(cue)->playback_start_target_volume = STACK_AUDIO_CUE(target)->playback_live_volume;
	}

	return true;
}

/// Update the cue based on time
static void stack_fade_cue_pulse(StackCue *cue, stack_time_t clocktime)
{
	// Get the cue state before the base class potentially updates it
	StackCueState pre_pulse_state = cue->state;

	// Call superclass
	stack_cue_pulse_base(cue, clocktime);

	// Get the target
	StackCue *target = stack_cue_get_by_uid(STACK_FADE_CUE(cue)->target);

	// If the target is valid
	if (target != NULL)
	{
		// If the cue state has changed to stopped on this pulse, and we're
		// supposed to stop our target, then do so
		if (pre_pulse_state == STACK_CUE_STATE_PLAYING_ACTION && cue->state != STACK_CUE_STATE_PLAYING_ACTION)
		{
			// Jump to end volume (handles zero-second, non-stopping fades)
			STACK_AUDIO_CUE(target)->playback_live_volume = STACK_FADE_CUE(cue)->target_volume;

			// If we're supposed to stop our target, do so
			if (STACK_FADE_CUE(cue)->stop_target)
			{
				// Stop the target
				stack_cue_stop(target);
			}
		}
		else if (cue->state == STACK_CUE_STATE_PLAYING_ACTION)
		{
			// This if statement is to avoid divide by zero errors
			if (cue->action_time > 0.0)
			{
				// Get the current fade cue action times
				stack_time_t run_action_time;
				stack_cue_get_running_times(cue, clocktime, NULL, &run_action_time, NULL, NULL, NULL, NULL);

				// Calculate a ratio of how far through the queue we are
				double time_scaler = (double)run_action_time / (double)cue->action_time;

				// Convert start and end volumes from dB to linear scalars
				double vstart = stack_db_to_scalar(STACK_FADE_CUE(cue)->playback_start_target_volume);
				double vend = stack_db_to_scalar(STACK_FADE_CUE(cue)->target_volume);
				double vrange = vstart - vend;

				switch (STACK_FADE_CUE(cue)->profile)
				{
					case STACK_FADE_PROFILE_LINEAR:
						// time_scaler goes 0.0 -> 1.0
						STACK_AUDIO_CUE(target)->playback_live_volume = stack_scalar_to_db(vstart - vrange * time_scaler);
						break;

					case STACK_FADE_PROFILE_QUAD:
						// time_scaler goes 0.0 -> 1.0. Build a vol_scaler that is a two-part square curve from 0.0->0.5 then 0.5->1.0
						double vol_scaler;
						if (time_scaler < 0.5)
						{
							vol_scaler = (time_scaler * time_scaler * 2.0);
						}
						else
						{
							vol_scaler = 1.0 - ((1.0 - time_scaler) * (1.0 - time_scaler) * 2.0);
						}

						// Scale between vstart and vend based on vol_scaler and convert back to dB
						STACK_AUDIO_CUE(target)->playback_live_volume = stack_scalar_to_db(vstart - vrange * vol_scaler);
						break;

					case STACK_FADE_PROFILE_EXP:
						//STACK_AUDIO_CUE(target)->playback_live_volume = stack_scalar_to_db(vstart - (vstart - vend) * (1.0 - (((4.0 / pow(time_scaler + 1.0, 2.0)) - 1.0) / 3.0)));
						STACK_AUDIO_CUE(target)->playback_live_volume = stack_scalar_to_db(vstart - vrange * (1.0 - pow(1.0 - time_scaler, 2.0)));

						break;

					case STACK_FADE_PROFILE_INVEXP:
						//STACK_AUDIO_CUE(target)->playback_live_volume =
						STACK_AUDIO_CUE(target)->playback_live_volume = stack_scalar_to_db(vstart - vrange * pow(time_scaler, 2.0));
						break;
				}
			}
			else
			{
				// Immediately jump to target volume
				STACK_AUDIO_CUE(target)->playback_live_volume = STACK_FADE_CUE(cue)->target_volume;
			}
		}
	}
}

/// Sets up the tabs for the fade cue
static void stack_fade_cue_set_tabs(StackCue *cue, GtkNotebook *notebook)
{
	StackFadeCue *fcue = STACK_FADE_CUE(cue);

	// Create the tab
	GtkWidget *label = gtk_label_new("Fade");

	// Load the UI
	GtkBuilder *builder = gtk_builder_new_from_file("StackFadeCue.ui");
	fcue->builder = builder;
	fcue->fade_tab = GTK_WIDGET(gtk_builder_get_object(builder, "fcpGrid"));

	// Set up callbacks
	gtk_builder_add_callback_symbol(builder, "fcp_cue_changed", G_CALLBACK(fcp_cue_changed));
	gtk_builder_add_callback_symbol(builder, "fcp_fade_time_changed", G_CALLBACK(fcp_fade_time_changed));
	gtk_builder_add_callback_symbol(builder, "fcp_volume_changed", G_CALLBACK(fcp_volume_changed));
	gtk_builder_add_callback_symbol(builder, "fcp_profile_changed", G_CALLBACK(fcp_profile_changed));
	gtk_builder_add_callback_symbol(builder, "fcp_stop_target_changed", G_CALLBACK(fcp_stop_target_changed));

	// Connect the signals
	gtk_builder_connect_signals(builder, (gpointer)cue);

	// Add an extra reference to the fade tab - we're about to remove it's
	// parent and we don't want it to get garbage collected
	g_object_ref(fcue->fade_tab);

	// The tab has a parent window in the UI file - unparent the tab container from it
	gtk_widget_unparent(fcue->fade_tab);

	// Append the tab (and show it, because it starts off hidden...)
	gtk_notebook_append_page(notebook, fcue->fade_tab, label);
	gtk_widget_show(fcue->fade_tab);

	// Set the values: cue
	StackCue *target_cue = stack_cue_get_by_uid(fcue->target);
	if (target_cue)
	{
		char cue_number[32];
		stack_cue_id_to_string(target_cue->id, cue_number, 32);

		// Build the string
		std::string button_text;
		button_text = std::string(cue_number) + ": " + std::string(stack_cue_get_rendered_name(target_cue));

		// Set the button text
		gtk_button_set_label(GTK_BUTTON(gtk_builder_get_object(builder, "fcpCue")), button_text.c_str());
	}

	// Set the values: fade time
	char buffer[32];	// Warning: used multiple times!
	stack_format_time_as_string(STACK_CUE(fcue)->action_time, buffer, 32);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(builder, "fcpFadeTime")), buffer);

	// Set the values: stop target
	if (fcue->stop_target)
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "fcpStopTarget")), true);
	}
	else
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "fcpStopTarget")), false);
	}

	// Update fade type option
	switch (fcue->profile)
	{
		case STACK_FADE_PROFILE_LINEAR:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "fcpFadeTypeLinear")), true);
			break;

		case STACK_FADE_PROFILE_QUAD:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "fcpFadeTypeQuadratic")), true);
			break;

		case STACK_FADE_PROFILE_EXP:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "fcpFadeTypeExponential")), true);
			break;

		case STACK_FADE_PROFILE_INVEXP:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "fcpFadeTypeInvExponential")), true);
			break;
	}

	// Set the values: volume
	if (fcue->target_volume < -49.99)
	{
		snprintf(buffer, 32, "-Inf dB");
	}
	else
	{
		snprintf(buffer, 32, "%.2f dB", fcue->target_volume);
	}
	gtk_range_set_value(GTK_RANGE(gtk_builder_get_object(builder, "fcpVolume")), fcue->target_volume);
	gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(builder, "fcpVolumeValueLabel")), buffer);
}

/// Removes the properties tabs for a fade cue
static void stack_fade_cue_unset_tabs(StackCue *cue, GtkNotebook *notebook)
{
	// Find our media page
	gint page = gtk_notebook_page_num(notebook, STACK_FADE_CUE(cue)->fade_tab);

	// If we've found the page, remove it
	if (page >= 0)
	{
		gtk_notebook_remove_page(notebook, page);
	}

	// We don't need the top level window, so destroy it (GtkBuilder doesn't
	// destroy top-level windows itself)
	gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(STACK_FADE_CUE(cue)->builder, "window1")));

	// Destroy the builder
	g_object_unref(STACK_FADE_CUE(cue)->builder);

	// Remove our reference to the fade tab
	g_object_ref(STACK_FADE_CUE(cue)->fade_tab);

	// Be tidy
	STACK_FADE_CUE(cue)->builder = NULL;
	STACK_FADE_CUE(cue)->fade_tab = NULL;
}

/// Saves the details of this cue as JSON
static char *stack_fade_cue_to_json(StackCue *cue)
{
	StackFadeCue *fcue = STACK_FADE_CUE(cue);

	// Build JSON
	Json::Value cue_root;
	cue_root["target"] = (Json::UInt64)fcue->target;
	if (std::isfinite(fcue->target_volume))
	{
		cue_root["target_volume"] = fcue->target_volume;
	}
	else
	{
		cue_root["target_volume"] = "-Infinite";
	}
	cue_root["stop_target"] = fcue->stop_target;
	cue_root["profile"] = fcue->profile;

	// Write out JSON string and return (to be free'd by
	// stack_fade_cue_free_json)
	Json::FastWriter writer;
	return strdup(writer.write(cue_root).c_str());
}

/// Frees JSON strings as returned by stack_fade_cue_to_json
static void stack_fade_cue_free_json(char *json_data)
{
	free(json_data);
}

/// Re-initialises this cue from JSON Data
void stack_fade_cue_from_json(StackCue *cue, const char *json_data)
{
	Json::Value cue_root;
	Json::Reader reader;

	// Parse JSON data
	reader.parse(json_data, json_data + strlen(json_data), cue_root, false);

	// Get the data that's pertinent to us
	if (!cue_root.isMember("StackFadeCue"))
	{
		stack_log("stack_fade_cue_from_json(): Missing StackFadeCue class\n");
		return;
	}

	Json::Value& cue_data = cue_root["StackFadeCue"];

	// Load in values from JSON
	stack_fade_cue_set_target(STACK_FADE_CUE(cue), stack_cue_get_by_uid(stack_cue_list_remap(STACK_CUE(cue)->parent, (cue_uid_t)cue_data["target"].asUInt64())));
	if (cue_data["target_volume"].isString() && cue_data["target_volume"].asString() == "-Infinite")
	{
		stack_fade_cue_set_target_volume(STACK_FADE_CUE(cue), -INFINITY);
	}
	else
	{
		stack_fade_cue_set_target_volume(STACK_FADE_CUE(cue), cue_data["target_volume"].asDouble());
	}
	stack_fade_cue_set_stop_target(STACK_FADE_CUE(cue), cue_data["stop_target"].asBool());
	stack_fade_cue_set_profile(STACK_FADE_CUE(cue), (StackFadeProfile)cue_data["profile"].asInt());
}

/// Gets the error message for the cue
void stack_fade_cue_get_error(StackCue *cue, char *message, size_t size)
{
	if (STACK_FADE_CUE(cue)->target == STACK_CUE_UID_NONE)
	{
		snprintf(message, size, "No target cue chosen");
	}
	else
	{
		strncpy(message, "", size);
	}
}

const char *stack_fade_cue_get_field(StackCue *cue, const char *field)
{
	if (strcmp(field, "action") == 0)
	{
		if (STACK_FADE_CUE(cue)->stop_target)
		{
			return "Fade and stop";
		}
		else
		{
			return "Fade";
		}
	}
	if (strcmp(field, "target") == 0)
	{
		if (STACK_FADE_CUE(cue)->target == STACK_CUE_UID_NONE)
		{
			return "<no target>";
		}

		StackCue *target_cue = stack_cue_list_get_cue_by_uid(cue->parent, STACK_FADE_CUE(cue)->target);
		if (target_cue == NULL)
		{
			return "<invalid target>";
		}

		stack_cue_id_to_string(target_cue->id, STACK_FADE_CUE(cue)->target_cue_id_string, 32);
		return STACK_FADE_CUE(cue)->target_cue_id_string;
	}

	// Call the super class if we didn't return anything
	return stack_cue_get_field_base(cue, field);
}

////////////////////////////////////////////////////////////////////////////////
// CLASS REGISTRATION

// Registers StackFadeCue with the application
void stack_fade_cue_register()
{
	// Register built in cue types
	StackCueClass* fade_cue_class = new StackCueClass{ "StackFadeCue", "StackCue", stack_fade_cue_create, stack_fade_cue_destroy, stack_fade_cue_play, NULL, NULL, stack_fade_cue_pulse, stack_fade_cue_set_tabs, stack_fade_cue_unset_tabs, stack_fade_cue_to_json, stack_fade_cue_free_json, stack_fade_cue_from_json, stack_fade_cue_get_error, NULL, NULL, stack_fade_cue_get_field };
	stack_register_cue_class(fade_cue_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_fade_cue_register();
	return true;
}

