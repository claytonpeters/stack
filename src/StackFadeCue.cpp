// Includes:
#include "StackApp.h"
#include "StackFadeCue.h"
#include "StackGtkEntryHelper.h"
#include "StackAudioCue.h"
#include "StackLog.h"
#include <cstring>
#include <cstdlib>
#include <string>
#include <cmath>
#include <json/json.h>

static const cue_uid_t STACK_FADE_CUE_DEFAULT_TARGET = STACK_CUE_UID_NONE;
static const double STACK_FADE_CUE_DEFAULT_TARGET_VOLUME = -INFINITY;
static const StackFadeProfile STACK_FADE_CUE_DEFAULT_PROFILE = STACK_FADE_PROFILE_EXP;
static const bool STACK_FADE_CUE_DEFAULT_STOP_TARGET = true;

// Global: A single instance of our builder so we don't have to keep reloading
// it every time we change the selected cue
static GtkBuilder *sfc_builder = NULL;

// Global: A single instace of our icon
static GdkPixbuf *icon = NULL;

static void stack_fade_cue_ccb_common(StackProperty *property, StackPropertyVersion version, StackFadeCue *cue)
{
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		// Notify cue list that we've changed
		stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue), property);

		// Notify UI to update (name might have changed as a result)
		if (cue->fade_tab != NULL)
		{
			StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->fade_tab));
			g_signal_emit_by_name((gpointer)window, "update-selected-cue");
		}
	}
}

static void stack_fade_cue_ccb_target(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	StackFadeCue *cue = STACK_FADE_CUE(user_data);

	if (cue->fade_tab != NULL && version == STACK_PROPERTY_VERSION_DEFINED)
	{
		// Get the new UID
		cue_uid_t new_target_uid = STACK_FADE_CUE_DEFAULT_TARGET;
		stack_property_get_uint64(property, version, &new_target_uid);

		// Get the cue for the new UID
		StackCue *new_target = stack_cue_get_by_uid(new_target_uid);

		// Get the Select Cue button
		GtkButton *button = GTK_BUTTON(gtk_builder_get_object(sfc_builder, "fcpCue"));

		// Store the cue in the target and update the state
		if (new_target == NULL)
		{
			// Update the UI
			gtk_button_set_label(button, "Select Cue...");
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
			gtk_button_set_label(button, button_text.c_str());
		}
	}

	stack_fade_cue_ccb_common(property, version, STACK_FADE_CUE(user_data));
}

static void stack_fade_cue_ccb_target_volume(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	StackFadeCue *cue = STACK_FADE_CUE(user_data);

	if (cue->fade_tab != NULL && version == STACK_PROPERTY_VERSION_DEFINED)
	{
		double target_volume = STACK_FADE_CUE_DEFAULT_TARGET_VOLUME;
		stack_property_get_double(property, version, &target_volume);

		// Build a string of the volume
		char buffer[32];
		if (target_volume < -49.99)
		{
			snprintf(buffer, 32, "-Inf dB");
		}
		else
		{
			snprintf(buffer, 32, "%.2f dB", target_volume);
		}

		// Get the volume label and update it's value
		gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(sfc_builder, "fcpVolumeValueLabel")), buffer);
	}

	stack_fade_cue_ccb_common(property, version, STACK_FADE_CUE(user_data));
}

static void stack_fade_cue_ccb_stop_target(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	stack_fade_cue_ccb_common(property, version, STACK_FADE_CUE(user_data));
}

static void stack_fade_cue_ccb_profile(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	stack_fade_cue_ccb_common(property, version, STACK_FADE_CUE(user_data));
}

// The existence of this function is currently a bodge to update the action time
// on the UI. This should ideally be done by StackCueBase
static void stack_fade_cue_ccb_action_time(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	stack_fade_cue_ccb_common(property, version, STACK_FADE_CUE(user_data));
}

// Pause or resumes change callbacks on variables
static void stack_fade_cue_pause_change_callbacks(StackCue *cue, bool pause)
{
    stack_property_pause_change_callback(stack_cue_get_property(cue, "target"), pause);
    stack_property_pause_change_callback(stack_cue_get_property(cue, "target_volume"), pause);
    stack_property_pause_change_callback(stack_cue_get_property(cue, "stop_target"), pause);
    stack_property_pause_change_callback(stack_cue_get_property(cue, "profile"), pause);
}

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
	sfc_builder = NULL;
	cue->fade_tab = NULL;
	cue->target_cue_id_string[0] = '\0';
	cue->playback_start_target_volume = 0.0;

	// Add our properties
	StackProperty *target = stack_property_create("target", STACK_PROPERTY_TYPE_UINT64);
	stack_cue_add_property(STACK_CUE(cue), target);
	stack_property_set_uint64(target, STACK_PROPERTY_VERSION_DEFINED, STACK_FADE_CUE_DEFAULT_TARGET);
	stack_property_set_changed_callback(target, stack_fade_cue_ccb_target, (void*)cue);

	StackProperty *target_volume = stack_property_create("target_volume", STACK_PROPERTY_TYPE_DOUBLE);
	stack_cue_add_property(STACK_CUE(cue), target_volume);
	stack_property_set_double(target_volume, STACK_PROPERTY_VERSION_DEFINED, STACK_FADE_CUE_DEFAULT_TARGET_VOLUME);
	stack_property_set_changed_callback(target_volume, stack_fade_cue_ccb_target_volume, (void*)cue);

	StackProperty *stop_target = stack_property_create("stop_target", STACK_PROPERTY_TYPE_BOOL);
	stack_cue_add_property(STACK_CUE(cue), stop_target);
	stack_property_set_bool(stop_target, STACK_PROPERTY_VERSION_DEFINED, STACK_FADE_CUE_DEFAULT_STOP_TARGET);
	stack_property_set_changed_callback(stop_target, stack_fade_cue_ccb_stop_target, (void*)cue);

	StackProperty *profile = stack_property_create("profile", STACK_PROPERTY_TYPE_INT32);
	stack_cue_add_property(STACK_CUE(cue), profile);
	stack_property_set_int32(profile, STACK_PROPERTY_VERSION_DEFINED, STACK_FADE_CUE_DEFAULT_PROFILE);
	stack_property_set_changed_callback(profile, stack_fade_cue_ccb_profile, (void*)cue);

	// Override the behaviour of action time change
	stack_property_set_changed_callback(stack_cue_get_property(STACK_CUE(cue), "action_time"), stack_fade_cue_ccb_action_time, cue);

	// Initialise superclass variables
	stack_cue_set_name(STACK_CUE(cue), "${action} cue ${target}");

	return STACK_CUE(cue);
}

/// Destroys a fade cue
static void stack_fade_cue_destroy(StackCue *cue)
{
	// Tidy up
	if (STACK_FADE_CUE(cue)->fade_tab)
	{
		// Remove our reference to the fade tab
		g_object_unref(STACK_FADE_CUE(cue)->fade_tab);
	}

	// Call parent destructor
	stack_cue_destroy_base(cue);
}

////////////////////////////////////////////////////////////////////////////////
// PROPERTY SETTERS

// Sets the target cue of a fade cue
void stack_fade_cue_set_target(StackFadeCue *cue, StackCue *target)
{
	cue_uid_t target_uid;

	if (target == NULL)
	{
		target_uid = STACK_CUE_UID_NONE;
		stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_ERROR);
	}
	else
	{
		target_uid = target->uid;
		stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_STOPPED);
	}

	stack_property_set_uint64(stack_cue_get_property(STACK_CUE(cue), "target"), STACK_PROPERTY_VERSION_DEFINED, target_uid);
}

/// Sets the change in volume of the target cue
void stack_fade_cue_set_target_volume(StackFadeCue *cue, double target_volume)
{
	if (target_volume < -49.99)
	{
		target_volume = -INFINITY;
	}

	stack_property_set_double(stack_cue_get_property(STACK_CUE(cue), "target_volume"), STACK_PROPERTY_VERSION_DEFINED, target_volume);
}

/// Sets whether the Stop Target flag is enabled
void stack_fade_cue_set_stop_target(StackFadeCue *cue, bool stop_target)
{
	stack_property_set_bool(stack_cue_get_property(STACK_CUE(cue), "stop_target"), STACK_PROPERTY_VERSION_DEFINED, stop_target);
}

/// Sets the fade profile
void stack_fade_cue_set_profile(StackFadeCue *cue, StackFadeProfile profile)
{
	stack_property_set_int32(stack_cue_get_property(STACK_CUE(cue), "profile"), STACK_PROPERTY_VERSION_DEFINED, profile);
}

////////////////////////////////////////////////////////////////////////////////
// UI CALLBACKS

/// Called when the Target Cue button is pressed
static void fcp_cue_changed(GtkButton *widget, gpointer user_data)
{
	// Get the cue
	StackFadeCue *cue = STACK_FADE_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);

	// Get the parent window
	StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->fade_tab));

	// Call the dialog and get the new target
	cue_uid_t current_target_uid = STACK_FADE_CUE_DEFAULT_TARGET;
	stack_property_get_uint64(stack_cue_get_property(STACK_CUE(cue), "target"), STACK_PROPERTY_VERSION_DEFINED, &current_target_uid);
	StackCue *current_target = stack_cue_get_by_uid(current_target_uid);

	StackCue *new_target = stack_select_cue_dialog(window, current_target, STACK_CUE(cue));

	// Update the cue
	stack_fade_cue_set_target(cue, new_target);
}

/// Called when the volume slider changes
static void fcp_volume_changed(GtkRange *range, gpointer user_data)
{
	StackFadeCue *cue = STACK_FADE_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(range)))->selected_cue);

	// Get the volume and store it
	double vol_db = gtk_range_get_value(range);
	stack_fade_cue_set_target_volume(cue, vol_db);
}

/// Called when the fade time edit box loses focus
static gboolean fcp_fade_time_changed(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	StackFadeCue *cue = STACK_FADE_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);

	// Set the time
	stack_cue_set_action_time(STACK_CUE(cue), stack_time_string_to_ns(gtk_entry_get_text(GTK_ENTRY(widget))));

	// Update the UI
	char buffer[32];
	stack_time_t cue_action_time = 0;
	stack_property_get_int64(stack_cue_get_property(STACK_CUE(cue), "action_time"), STACK_PROPERTY_VERSION_DEFINED, &cue_action_time);
	stack_format_time_as_string(cue_action_time, buffer, 32);
	gtk_entry_set_text(GTK_ENTRY(widget), buffer);

	return false;
}

/// Called when the fade profile changes
static void fcp_profile_changed(GtkToggleButton *widget, gpointer user_data)
{
	StackFadeCue *cue = STACK_FADE_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);

	// Get pointers to the four radio button options
	GtkToggleButton* r1 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(sfc_builder, "fcpFadeTypeLinear"));
	GtkToggleButton* r2 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(sfc_builder, "fcpFadeTypeQuadratic"));
	GtkToggleButton* r3 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(sfc_builder, "fcpFadeTypeExponential"));
	GtkToggleButton* r4 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(sfc_builder, "fcpFadeTypeInvExponential"));

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
	StackFadeCue *cue = STACK_FADE_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);
	stack_fade_cue_set_stop_target(cue, gtk_toggle_button_get_active(widget));
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

	// Get the target
	cue_uid_t target_uid = STACK_CUE_UID_NONE;
	stack_property_get_uint64(stack_cue_get_property(cue, "target"), STACK_PROPERTY_VERSION_DEFINED, &target_uid);
	StackCue *target = stack_cue_get_by_uid(target_uid);

	// If we've found the target and it's a StackAudioCue...
	if (target != NULL && strcmp(target->_class_name, "StackAudioCue") == 0)
	{
		// Store the current volume of the target
		stack_property_get_double(stack_cue_get_property(target, "play_volume"), STACK_PROPERTY_VERSION_LIVE, &(STACK_FADE_CUE(cue)->playback_start_target_volume));

		// ...copy the current values
		stack_property_copy_defined_to_live(stack_cue_get_property(cue, "target"));
		stack_property_copy_defined_to_live(stack_cue_get_property(cue, "target_volume"));
		stack_property_copy_defined_to_live(stack_cue_get_property(cue, "profile"));
		stack_property_copy_defined_to_live(stack_cue_get_property(cue, "stop_target"));
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
	cue_uid_t target_uid = STACK_CUE_UID_NONE;
	stack_property_get_uint64(stack_cue_get_property(cue, "target"), STACK_PROPERTY_VERSION_LIVE, &target_uid);
	StackCue *target = stack_cue_get_by_uid(target_uid);

	// Get our values
	bool stop_target = false;
	stack_property_get_bool(stack_cue_get_property(cue, "stop_target"), STACK_PROPERTY_VERSION_LIVE, &stop_target);

	// If the target is valid
	if (target != NULL)
	{
		double new_volume = STACK_FADE_CUE_DEFAULT_TARGET_VOLUME;

		// If the cue state has changed to stopped on this pulse, and we're
		// supposed to stop our target, then do so
		if (pre_pulse_state == STACK_CUE_STATE_PLAYING_ACTION && cue->state != STACK_CUE_STATE_PLAYING_ACTION)
		{
			// Jump to end volume (handles zero-second, non-stopping fades)
			stack_property_get_double(stack_cue_get_property(cue, "target_volume"), STACK_PROPERTY_VERSION_LIVE, &new_volume);

			// If we're supposed to stop our target, do so
			if (stop_target)
			{
				// Stop the target
				stack_cue_stop(target);
			}
		}
		else if (cue->state == STACK_CUE_STATE_PLAYING_ACTION)
		{
			stack_time_t cue_action_time = 0;
			stack_property_get_int64(stack_cue_get_property(STACK_CUE(cue), "action_time"), STACK_PROPERTY_VERSION_LIVE, &cue_action_time);
			// This if statement is to avoid divide by zero errors
			if (cue_action_time > 0.0)
			{
				// Get the current fade cue action times
				stack_time_t run_action_time;
				stack_cue_get_running_times(cue, clocktime, NULL, &run_action_time, NULL, NULL, NULL, NULL);

				// Determine the volume we're working towards
				double target_volume = STACK_FADE_CUE_DEFAULT_TARGET_VOLUME;
				stack_property_get_double(stack_cue_get_property(cue, "target_volume"), STACK_PROPERTY_VERSION_LIVE, &target_volume);

				// Calculate a ratio of how far through the queue we are
				double time_scaler = (double)run_action_time / (double)cue_action_time;

				// Convert start and end volumes from dB to linear scalars
				double vstart = stack_db_to_scalar(STACK_FADE_CUE(cue)->playback_start_target_volume);
				double vend = stack_db_to_scalar(target_volume);
				double vrange = vstart - vend;

				// Get the fade profile
				StackFadeProfile profile = STACK_FADE_CUE_DEFAULT_PROFILE;
				stack_property_get_int32(stack_cue_get_property(cue, "profile"), STACK_PROPERTY_VERSION_LIVE, (int32_t*)&profile);

				switch (profile)
				{
					case STACK_FADE_PROFILE_LINEAR:
						// time_scaler goes 0.0 -> 1.0
						new_volume = stack_scalar_to_db(vstart - vrange * time_scaler);
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
						new_volume = stack_scalar_to_db(vstart - vrange * vol_scaler);
						break;

					case STACK_FADE_PROFILE_EXP:
						new_volume = stack_scalar_to_db(vstart - vrange * (1.0 - pow(1.0 - time_scaler, 2.0)));

						break;

					case STACK_FADE_PROFILE_INVEXP:
						new_volume = stack_scalar_to_db(vstart - vrange * pow(time_scaler, 2.0));
						break;
				}
			}
			else
			{
				// Immediately jump to target volume
				stack_property_get_double(stack_cue_get_property(cue, "target_volume"), STACK_PROPERTY_VERSION_LIVE, &new_volume);
			}
		}

		// Set the new volume
		// TODO: Once StackAudioCue uses properties, change this
		stack_property_set_double(stack_cue_get_property(STACK_CUE(target), "play_volume"), STACK_PROPERTY_VERSION_LIVE, new_volume);
	}
}

/// Sets up the tabs for the fade cue
static void stack_fade_cue_set_tabs(StackCue *cue, GtkNotebook *notebook)
{
	StackFadeCue *fcue = STACK_FADE_CUE(cue);

	// Create the tab
	GtkWidget *label = gtk_label_new("Fade");

	// Load the UI (if we haven't already)
	if (sfc_builder == NULL)
	{
		sfc_builder = gtk_builder_new_from_resource("/org/stack/ui/StackFadeCue.ui");

		// Set up callbacks
		gtk_builder_add_callback_symbol(sfc_builder, "fcp_cue_changed", G_CALLBACK(fcp_cue_changed));
		gtk_builder_add_callback_symbol(sfc_builder, "fcp_fade_time_changed", G_CALLBACK(fcp_fade_time_changed));
		gtk_builder_add_callback_symbol(sfc_builder, "fcp_volume_changed", G_CALLBACK(fcp_volume_changed));
		gtk_builder_add_callback_symbol(sfc_builder, "fcp_profile_changed", G_CALLBACK(fcp_profile_changed));
		gtk_builder_add_callback_symbol(sfc_builder, "fcp_stop_target_changed", G_CALLBACK(fcp_stop_target_changed));

		// Connect the signals
		gtk_builder_connect_signals(sfc_builder, NULL);

		// Apply input limitin
		stack_limit_gtk_entry_time(GTK_ENTRY(gtk_builder_get_object(sfc_builder, "fcpFadeTime")), false);
	}
	fcue->fade_tab = GTK_WIDGET(gtk_builder_get_object(sfc_builder, "fcpGrid"));

	// Pause change callbacks on the properties
	stack_fade_cue_pause_change_callbacks(cue, true);

	// Add an extra reference to the fade tab - we're about to remove it's
	// parent and we don't want it to get garbage collected
	g_object_ref(fcue->fade_tab);

	// The tab has a parent window in the UI file - unparent the tab container from it
	gtk_widget_unparent(fcue->fade_tab);

	// Append the tab (and show it, because it starts off hidden...)
	gtk_notebook_append_page(notebook, fcue->fade_tab, label);
	gtk_widget_show(fcue->fade_tab);

	// Set the values: cue
	cue_uid_t target_uid = STACK_CUE_UID_NONE;
	stack_property_get_uint64(stack_cue_get_property(cue, "target"), STACK_PROPERTY_VERSION_DEFINED, &target_uid);
	StackCue *target_cue = stack_cue_get_by_uid(target_uid);
	if (target_cue)
	{
		char cue_number[32];
		stack_cue_id_to_string(target_cue->id, cue_number, 32);

		// Build the string
		std::string button_text;
		button_text = std::string(cue_number) + ": " + std::string(stack_cue_get_rendered_name(target_cue));

		// Set the button text
		gtk_button_set_label(GTK_BUTTON(gtk_builder_get_object(sfc_builder, "fcpCue")), button_text.c_str());
	}
	else
	{
		gtk_button_set_label(GTK_BUTTON(gtk_builder_get_object(sfc_builder, "fcpCue")), "Select Cue...");
	}

	// Set the values: fade time
	char buffer[32];	// Warning: used multiple times!
	stack_time_t cue_action_time = 0;
	stack_property_get_int64(stack_cue_get_property(cue, "action_time"), STACK_PROPERTY_VERSION_DEFINED, &cue_action_time);
	stack_format_time_as_string(cue_action_time, buffer, 32);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(sfc_builder, "fcpFadeTime")), buffer);

	// Set the values: stop target
	bool stop_target = STACK_FADE_CUE_DEFAULT_STOP_TARGET;
	stack_property_get_bool(stack_cue_get_property(cue, "stop_target"), STACK_PROPERTY_VERSION_DEFINED, &stop_target);
	if (stop_target)
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(sfc_builder, "fcpStopTarget")), true);
	}
	else
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(sfc_builder, "fcpStopTarget")), false);
	}

	// Update fade type option
	StackFadeProfile profile = STACK_FADE_CUE_DEFAULT_PROFILE;
	stack_property_get_int32(stack_cue_get_property(cue, "profile"), STACK_PROPERTY_VERSION_DEFINED, (int32_t*)&profile);
	switch (profile)
	{
		case STACK_FADE_PROFILE_LINEAR:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(sfc_builder, "fcpFadeTypeLinear")), true);
			break;

		case STACK_FADE_PROFILE_QUAD:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(sfc_builder, "fcpFadeTypeQuadratic")), true);
			break;

		case STACK_FADE_PROFILE_EXP:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(sfc_builder, "fcpFadeTypeExponential")), true);
			break;

		case STACK_FADE_PROFILE_INVEXP:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(sfc_builder, "fcpFadeTypeInvExponential")), true);
			break;
	}

	// Set the values: volume
	double target_volume = STACK_FADE_CUE_DEFAULT_TARGET_VOLUME;
	stack_property_get_double(stack_cue_get_property(cue, "target_volume"), STACK_PROPERTY_VERSION_DEFINED, &target_volume);
	if (target_volume < -49.99)
	{
		snprintf(buffer, 32, "-Inf dB");
	}
	else
	{
		snprintf(buffer, 32, "%.2f dB", target_volume);
	}
	gtk_range_set_value(GTK_RANGE(gtk_builder_get_object(sfc_builder, "fcpVolume")), target_volume);
	gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(sfc_builder, "fcpVolumeValueLabel")), buffer);

	// Resume change callbacks on the properties
	stack_fade_cue_pause_change_callbacks(cue, false);
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

	// Remove our reference to the fade tab
	g_object_unref(STACK_FADE_CUE(cue)->fade_tab);
	STACK_FADE_CUE(cue)->fade_tab = NULL;
}

/// Saves the details of this cue as JSON
static char *stack_fade_cue_to_json(StackCue *cue)
{
	StackFadeCue *fcue = STACK_FADE_CUE(cue);

	// Build JSON
	Json::Value cue_root;
	stack_property_write_json(stack_cue_get_property(cue, "target"), &cue_root);
	double target_volume = STACK_FADE_CUE_DEFAULT_TARGET_VOLUME;
	stack_property_get_double(stack_cue_get_property(cue, "target_volume"), STACK_PROPERTY_VERSION_DEFINED, &target_volume);
	if (std::isfinite(target_volume))
	{
		cue_root["target_volume"] = target_volume;
	}
	else
	{
		cue_root["target_volume"] = "-Infinite";
	}
	stack_property_write_json(stack_cue_get_property(cue, "stop_target"), &cue_root);
	stack_property_write_json(stack_cue_get_property(cue, "profile"), &cue_root);

	// Write out JSON string and return (to be free'd by
	// stack_fade_cue_free_json)
	Json::FastWriter writer;
	return strdup(writer.write(cue_root).c_str());
}

/// Frees JSON strings as returned by stack_fade_cue_to_json
static void stack_fade_cue_free_json(StackCue *cue, char *json_data)
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
	cue_uid_t target_uid = STACK_CUE_UID_NONE;
	stack_property_get_uint64(stack_cue_get_property(cue, "target"), STACK_PROPERTY_VERSION_LIVE, &target_uid);

	if (target_uid == STACK_CUE_UID_NONE)
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
		bool stop_target = STACK_FADE_CUE_DEFAULT_STOP_TARGET;
		stack_property_get_bool(stack_cue_get_property(cue, "stop_target"), STACK_PROPERTY_VERSION_DEFINED, &stop_target);

		if (stop_target)
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
		cue_uid_t target_uid = STACK_CUE_UID_NONE;
		stack_property_get_uint64(stack_cue_get_property(cue, "target"), STACK_PROPERTY_VERSION_DEFINED, &target_uid);

		if (target_uid == STACK_CUE_UID_NONE)
		{
			return "<no target>";
		}

		StackCue *target_cue = stack_cue_get_by_uid(target_uid);
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

/// Returns the icon for a cue
/// @param cue The cue to get the icon of
GdkPixbuf *stack_fade_cue_get_icon(StackCue *cue)
{
	return icon;
}

////////////////////////////////////////////////////////////////////////////////
// CLASS REGISTRATION

// Registers StackFadeCue with the application
void stack_fade_cue_register()
{
	// Load the icon
	icon = gdk_pixbuf_new_from_resource("/org/stack/icons/stackfadecue.png", NULL);

	// Register built in cue types
	StackCueClass* fade_cue_class = new StackCueClass{ "StackFadeCue", "StackCue", stack_fade_cue_create, stack_fade_cue_destroy, stack_fade_cue_play, NULL, NULL, stack_fade_cue_pulse, stack_fade_cue_set_tabs, stack_fade_cue_unset_tabs, stack_fade_cue_to_json, stack_fade_cue_free_json, stack_fade_cue_from_json, stack_fade_cue_get_error, NULL, NULL, stack_fade_cue_get_field, stack_fade_cue_get_icon };
	stack_register_cue_class(fade_cue_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_fade_cue_register();
	return true;
}

