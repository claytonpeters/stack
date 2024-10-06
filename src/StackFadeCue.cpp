// Includes:
#include "StackApp.h"
#include "StackFadeCue.h"
#include "StackGtkHelper.h"
#include "StackAudioCue.h"
#include "StackAudioLevelsTab.h"
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

/// Common property callback code - marks the cue list as changed and fires an
/// update-selected-cue event
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

/// Target Cue property change callback
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

		// Update the levels tab to show the possibly different set of levels
		size_t input_channels = 0;
		if (new_target != NULL)
		{
			input_channels = stack_cue_get_active_channels(new_target, NULL, false);
		}
		stack_audio_levels_tab_populate(cue->levels_tab, input_channels, STACK_CUE(cue)->parent->channels, true, NULL);
	}

	stack_fade_cue_ccb_common(property, version, STACK_FADE_CUE(user_data));
}

/// Master Volume property change callback
static void stack_fade_cue_ccb_master_volume(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	StackFadeCue *cue = STACK_FADE_CUE(user_data);

	if (cue->fade_tab != NULL && version == STACK_PROPERTY_VERSION_DEFINED)
	{
		double master_volume = STACK_FADE_CUE_DEFAULT_TARGET_VOLUME;
		stack_property_get_double(property, version, &master_volume);

		// Build a string of the volume
		char buffer[32];
		if (master_volume < -59.99)
		{
			snprintf(buffer, 32, "-Inf dB");
		}
		else
		{
			snprintf(buffer, 32, "%.2f dB", master_volume);
		}
	}

	stack_fade_cue_ccb_common(property, version, STACK_FADE_CUE(user_data));
}

/// Stop Target property change callback
static void stack_fade_cue_ccb_stop_target(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	stack_fade_cue_ccb_common(property, version, STACK_FADE_CUE(user_data));
}

/// Fade Profile property change callback
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

/// Validates any of the properties that are volumes, i.e. msater volume, each
/// channel volume, and each crosspoint
double stack_fade_cue_validate_volume(StackPropertyDouble *property, StackPropertyVersion version, const double value, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackFadeCue* cue = STACK_FADE_CUE(user_data);

		// We count anything less than -60dB as silence
		if (value < -59.99)
		{
			return -INFINITY;
		}
	}

	return value;
}

/// Pause or resumes change callbacks on variables
static void stack_fade_cue_pause_change_callbacks(StackCue *cue, bool pause)
{
    stack_property_pause_change_callback(stack_cue_get_property(cue, "target"), pause);
    stack_property_pause_change_callback(stack_cue_get_property(cue, "master_volume"), pause);
    stack_property_pause_change_callback(stack_cue_get_property(cue, "stop_target"), pause);
    stack_property_pause_change_callback(stack_cue_get_property(cue, "profile"), pause);

	// TODO: We should pause callbacks on channel volume and crosspoints
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
	cue->playback_start_master_volume = 0.0;
	cue->playback_start_channel_volumes = NULL;
	cue->playback_start_crosspoints = NULL;

	// Add our properties
	StackProperty *target = stack_property_create("target", STACK_PROPERTY_TYPE_UINT64);
	stack_cue_add_property(STACK_CUE(cue), target);
	stack_property_set_uint64(target, STACK_PROPERTY_VERSION_DEFINED, STACK_FADE_CUE_DEFAULT_TARGET);
	stack_property_set_changed_callback(target, stack_fade_cue_ccb_target, (void*)cue);

	StackProperty *master_volume = stack_property_create("master_volume", STACK_PROPERTY_TYPE_DOUBLE);
	stack_cue_add_property(STACK_CUE(cue), master_volume);
	stack_property_set_double(master_volume, STACK_PROPERTY_VERSION_DEFINED, STACK_FADE_CUE_DEFAULT_TARGET_VOLUME);
	stack_property_set_changed_callback(master_volume, stack_fade_cue_ccb_master_volume, (void*)cue);
	stack_property_set_validator(master_volume, (stack_property_validator_t)stack_fade_cue_validate_volume, (void*)cue);
	stack_property_set_nullable(master_volume, true);

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

/// Sets the target cue of a fade cue
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
void stack_fade_cue_set_master_volume(StackFadeCue *cue, double master_volume, bool is_null)
{
	StackProperty *property = stack_cue_get_property(STACK_CUE(cue), "master_volume");
	stack_property_set_null(property, STACK_PROPERTY_VERSION_DEFINED, is_null);
	if (!is_null)
	{
		stack_property_set_double(property, STACK_PROPERTY_VERSION_DEFINED, master_volume);
	}
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
	stack_fade_cue_set_master_volume(cue, vol_db, false);
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

/// Returns, optionally creating if necessary, the StackProperty object for the
/// given input channel.
/// @param cue The fade cue
/// @param channel The channel to get the property for. Note that channel is
/// one-based, not zero. If channel zero is givem, the master volume property is
/// returned
/// @param create Whether to create the property if it does not exist
StackProperty *stack_fade_cue_get_volume_property(StackCue *cue, size_t channel, bool create)
{
	StackProperty *property = NULL;

	if (channel == 0)
	{
		// This one always exists
		property = stack_cue_get_property(STACK_CUE(cue), "master_volume");
	}
	else
	{
		// Create some new properties
		char property_name[64];
		snprintf(property_name, 64, "input_%lu_volume", channel);
		property = stack_cue_get_property(STACK_CUE(cue), property_name);

		// If the property does not yet exist, create it
		if (property == NULL && create)
		{
			// Create the property
			property = stack_property_create(property_name, STACK_PROPERTY_TYPE_DOUBLE);

			// Property should be nullable in case the user doesn't want to change it,
			// and should default to NULL
			stack_property_set_nullable(property, true);
			stack_property_set_null(property, STACK_PROPERTY_VERSION_DEFINED, true);
			stack_property_set_null(property, STACK_PROPERTY_VERSION_LIVE, true);

			stack_property_set_changed_callback(property, stack_fade_cue_ccb_master_volume, (void*)cue);
			stack_property_set_validator(property, (stack_property_validator_t)stack_fade_cue_validate_volume, (void*)cue);
			stack_cue_add_property(STACK_CUE(cue), property);
		}
	}

	return property;
}

/// Returns, optionally creating if necessary, the StackProperty object for the
/// given crosspoint
/// @param cue The fade cue
/// @param input_channel The input channel to get the property for. Note that
/// unlike stack_fade_cue_get_volume_property, this channel is zero-based
/// @param output_channel The output channel to get the property for. Note that
/// unlike stack_fade_cue_get_volume_property, this channel is zero-based
/// @param create Whether to create the property if it does not exist
StackProperty *stack_fade_cue_get_crosspoint_property(StackCue *cue, size_t input_channel, size_t output_channel, bool create)
{
	// Build property name
	char property_name[64];
	snprintf(property_name, 64, "crosspoint_%lu_%lu", output_channel, input_channel);

	// Get the property, creating it with a default value if it does not exist
	StackProperty *property = stack_cue_get_property(STACK_CUE(cue), property_name);
	if (property == NULL && create)
	{
		// Property does not exist - create it
		property = stack_property_create(property_name, STACK_PROPERTY_TYPE_DOUBLE);
		stack_property_set_validator(property, (stack_property_validator_t)stack_fade_cue_validate_volume, (void*)cue);

		// Property is nullable - in case the user doesn't want the fade cue to touch this
		stack_property_set_nullable(property, true);

		// Default crosspoints to NULL
		stack_property_set_null(property, STACK_PROPERTY_VERSION_DEFINED, true);
		stack_property_set_null(property, STACK_PROPERTY_VERSION_LIVE, true);

		stack_cue_add_property(cue, property);
	}

	return property;
}

/// Calculate the new value of a fading property
/// @param cue The fade cue which is used to determine the target cue
/// @param source_property The property on the fade cue that we want the faded
/// value of
/// @param profile The profile of the fade. This is passed in as this function
/// is often called repeatedly for many properties, so this stops this function
/// needing to read the profile property repeatedly
/// @param time_scalar A value between zero and one that determines how far
/// through the fade the returned value should be. Again this is passed in as
/// it's the same for every property
/// @param initial_volume The value of the property on the target cue before the
/// fade started
static double stack_fade_cue_fade_property(StackFadeCue *cue, const StackProperty *source_property, StackFadeProfile profile, double time_scaler, double initial_volume)
{
	// Determine the volume we're working towards
	double target_volume = STACK_FADE_CUE_DEFAULT_TARGET_VOLUME;
	stack_property_get_double(source_property, STACK_PROPERTY_VERSION_LIVE, &target_volume);

	// Convert start and end volumes from dB to linear scalars
	const double vstart = stack_db_to_scalar(initial_volume);
	const double vend = stack_db_to_scalar(target_volume);
	const double vrange = vstart - vend;

	double new_volume;
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

	return new_volume;
}

/// Sets the volumes on the target cue to their final volumes
static void stack_fade_cue_set_to_complete(StackCue *cue)
{
	// Get the target
	cue_uid_t target_uid = STACK_CUE_UID_NONE;
	stack_property_get_uint64(stack_cue_get_property(cue, "target"), STACK_PROPERTY_VERSION_LIVE, &target_uid);
	StackCue *target = stack_cue_get_by_uid(target_uid);

	// If the target is valid
	if (target == NULL)
	{
		return;
	}

	// Jump to end volume (handles zero-second, non-stopping fades)
	double new_volume = STACK_FADE_CUE_DEFAULT_TARGET_VOLUME;
	const StackProperty *fade_master_volume = stack_cue_get_property(cue, "master_volume");
	if (!stack_property_get_null(fade_master_volume, STACK_PROPERTY_VERSION_LIVE))
	{
		stack_property_get_double(fade_master_volume, STACK_PROPERTY_VERSION_LIVE, &new_volume);
		stack_property_set_double(stack_cue_get_property(target, "master_volume"), STACK_PROPERTY_VERSION_LIVE, new_volume);
	}

	// Perform the per-channel fades
	size_t input_channels = 0;
	input_channels = stack_cue_get_active_channels(target, NULL, false);
	for (size_t channel = 0; channel < input_channels; channel++)
	{
		// Get the property
		const StackProperty *channel_volume_fade = stack_fade_cue_get_volume_property(cue, channel + 1, false);

		// Skip channel volumes that have never been created
		if (channel_volume_fade == NULL)
		{
			continue;
		}

		// Skip channel volumes that are unset
		if (stack_property_get_null(channel_volume_fade, STACK_PROPERTY_VERSION_LIVE))
		{
			continue;
		}

		// Jump to end volume
		new_volume = STACK_FADE_CUE_DEFAULT_TARGET_VOLUME;
		stack_property_get_double(channel_volume_fade, STACK_PROPERTY_VERSION_LIVE, &new_volume);
		stack_property_set_double(stack_cue_get_property(target, stack_property_get_name(channel_volume_fade)), STACK_PROPERTY_VERSION_LIVE, new_volume);
	}

	// Perform the per-crosspoint fades
	for (size_t input_channel = 0; input_channel < input_channels; input_channel++)
	{
		for (size_t output_channel = 0; output_channel < cue->parent->channels; output_channel++)
		{
			// Get the property
			const StackProperty *crosspoint_fade = stack_fade_cue_get_crosspoint_property(cue, input_channel, output_channel, false);

			// Skip channel volumes that have never been created
			if (crosspoint_fade == NULL)
			{
				continue;
			}

			// Skip channel volumes that are unset
			if (stack_property_get_null(crosspoint_fade, STACK_PROPERTY_VERSION_LIVE))
			{
				continue;
			}

			// Jump to end volume
			new_volume = STACK_FADE_CUE_DEFAULT_TARGET_VOLUME;
			stack_property_get_double(crosspoint_fade, STACK_PROPERTY_VERSION_LIVE, &new_volume);
			stack_property_set_double(stack_cue_get_property(target, stack_property_get_name(crosspoint_fade)), STACK_PROPERTY_VERSION_LIVE, new_volume);
		}
	}
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

	// If we've not got a target, return early
	if (target == NULL)
	{
		return true;
	}

	// Store the current volume of the target
	stack_property_get_double(stack_cue_get_property(target, "master_volume"), STACK_PROPERTY_VERSION_LIVE, &(STACK_FADE_CUE(cue)->playback_start_master_volume));

	// ...copy the current values (of the simple properties)
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "target"));
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "master_volume"));
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "profile"));
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "stop_target"));

	// Get the number of channels the target hsa
	size_t input_channels = 0;
	input_channels = stack_cue_get_active_channels(target, NULL, false);

	// If we've not got any input channels, return early
	if (input_channels == 0)
	{
		return true;
	}

	// Allocate memory for start volumes
	StackFadeCue *fcue = STACK_FADE_CUE(cue);
	fcue->playback_start_channel_volumes = new double[input_channels];
	fcue->playback_start_crosspoints = new double[input_channels * cue->parent->channels];

	// For each input channel
	for (size_t input_channel = 0; input_channel < input_channels; input_channel++)
	{
		// Iterate for all the crosspoints
		for (size_t output_channel = 0; output_channel < cue->parent->channels; output_channel++)
		{
			// Get the property from the fade cue, skipping if the property doesn't exist
			StackProperty *crosspoint_fade = stack_fade_cue_get_crosspoint_property(cue, input_channel, output_channel, false);
			if (crosspoint_fade == NULL)
			{
				continue;
			}

			// Get the property from the target
			StackProperty *crosspoint_target = stack_cue_get_property(target, stack_property_get_name(crosspoint_fade));
			size_t index = input_channel * cue->parent->channels + output_channel;
			if (crosspoint_target != NULL)
			{
				stack_property_copy_defined_to_live(crosspoint_fade);
				stack_property_get_double(crosspoint_target, STACK_PROPERTY_VERSION_LIVE, &fcue->playback_start_crosspoints[index]);
			}
		}

		// Get the property from the fade cue skipping if the property doesn't exist
		StackProperty *channel_volume_fade = stack_fade_cue_get_volume_property(cue, input_channel + 1, false);
		if (channel_volume_fade == NULL)
		{
			continue;
		}

		// Get the property from the target
		StackProperty *channel_volume_target = stack_cue_get_property(target, stack_property_get_name(channel_volume_fade));
		if (channel_volume_target != NULL)
		{
			stack_property_copy_defined_to_live(channel_volume_fade);
			stack_property_get_double(channel_volume_target, STACK_PROPERTY_VERSION_LIVE, &fcue->playback_start_channel_volumes[input_channel]);
		}
	}

	return true;
}

/// Stops the cue from playing
static void stack_fade_cue_stop(StackCue *cue)
{
	// Call the superclass
	stack_cue_stop_base(cue);

	// Tidy up the initial volume caches
	StackFadeCue *fcue = STACK_FADE_CUE(cue);
	if (fcue->playback_start_channel_volumes != NULL)
	{
		delete [] fcue->playback_start_channel_volumes;
		fcue->playback_start_channel_volumes = NULL;
	}
	if (fcue->playback_start_crosspoints != NULL)
	{
		delete [] fcue->playback_start_crosspoints;
		fcue->playback_start_crosspoints = NULL;
	}
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

	// If the target is valid
	if (target == NULL)
	{
		return;
	}

	// Get our values
	bool stop_target = false;
	stack_property_get_bool(stack_cue_get_property(cue, "stop_target"), STACK_PROPERTY_VERSION_LIVE, &stop_target);

	// If the cue state has changed to stopped on this pulse, and we're
	// supposed to stop our target, then do so
	if (pre_pulse_state == STACK_CUE_STATE_PLAYING_ACTION && cue->state != STACK_CUE_STATE_PLAYING_ACTION)
	{
		stack_fade_cue_set_to_complete(cue);

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

			// Calculate a ratio of how far through the queue we are
			const double time_scaler = (double)run_action_time / (double)cue_action_time;

			// Get the fade profile
			StackFadeProfile profile = STACK_FADE_CUE_DEFAULT_PROFILE;
			stack_property_get_int32(stack_cue_get_property(cue, "profile"), STACK_PROPERTY_VERSION_LIVE, (int32_t*)&profile);

			// Perform the fade on the master
			const StackProperty *master_volume = stack_cue_get_property(cue, "master_volume");
			if (!stack_property_get_null(master_volume, STACK_PROPERTY_VERSION_LIVE))
			{
				double new_volume = STACK_FADE_CUE_DEFAULT_TARGET_VOLUME;
				new_volume = stack_fade_cue_fade_property(STACK_FADE_CUE(cue), master_volume, profile, time_scaler, STACK_FADE_CUE(cue)->playback_start_master_volume);
				stack_property_set_double(stack_cue_get_property(target, "master_volume"), STACK_PROPERTY_VERSION_LIVE, new_volume);
			}

			// Perform the per-channel fades
			size_t input_channels = 0;
			input_channels = stack_cue_get_active_channels(target, NULL, false);
			for (size_t channel = 0; channel < input_channels; channel++)
			{
				// Get the property
				const StackProperty *channel_volume_fade = stack_fade_cue_get_volume_property(cue, channel + 1, false);

				// Skip channel volumes that have never been created
				if (channel_volume_fade == NULL)
				{
					continue;
				}

				// Skip channel volumes that are unset
				if (stack_property_get_null(channel_volume_fade, STACK_PROPERTY_VERSION_LIVE))
				{
					continue;
				}

				double new_volume = STACK_FADE_CUE_DEFAULT_TARGET_VOLUME;
				new_volume = stack_fade_cue_fade_property(STACK_FADE_CUE(cue), channel_volume_fade, profile, time_scaler, STACK_FADE_CUE(cue)->playback_start_channel_volumes[channel]);
				stack_property_set_double(stack_cue_get_property(target, stack_property_get_name(channel_volume_fade)), STACK_PROPERTY_VERSION_LIVE, new_volume);
			}

			// Perform the per-crosspoint fades
			for (size_t input_channel = 0; input_channel < input_channels; input_channel++)
			{
				for (size_t output_channel = 0; output_channel < cue->parent->channels; output_channel++)
				{
					// Get the property
					const StackProperty *crosspoint_fade = stack_fade_cue_get_crosspoint_property(cue, input_channel, output_channel, false);

					// Skip channel volumes that have never been created
					if (crosspoint_fade == NULL)
					{
						continue;
					}

					// Skip channel volumes that are unset
					if (stack_property_get_null(crosspoint_fade, STACK_PROPERTY_VERSION_LIVE))
					{
						continue;
					}

					double new_volume = STACK_FADE_CUE_DEFAULT_TARGET_VOLUME;
					size_t index = input_channel * cue->parent->channels + output_channel;
					new_volume = stack_fade_cue_fade_property(STACK_FADE_CUE(cue), crosspoint_fade, profile, time_scaler, STACK_FADE_CUE(cue)->playback_start_crosspoints[index]);
					stack_property_set_double(stack_cue_get_property(target, stack_property_get_name(crosspoint_fade)), STACK_PROPERTY_VERSION_LIVE, new_volume);
				}
			}
		}
		else
		{
			stack_fade_cue_set_to_complete(cue);
		}
	}
}

/// Sets up the tabs for the fade cue
static void stack_fade_cue_set_tabs(StackCue *cue, GtkNotebook *notebook)
{
	StackFadeCue *fcue = STACK_FADE_CUE(cue);

	// Create the tab
	GtkWidget *fade_label = gtk_label_new("Fade");
	GtkWidget *levels_label = gtk_label_new("Levels");

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

	// Set up the levels tab
	fcue->levels_tab = stack_audio_levels_tab_new(cue, stack_fade_cue_get_volume_property, stack_fade_cue_get_crosspoint_property);

	// Append the tab (and show it, because it starts off hidden...)
	gtk_notebook_append_page(notebook, fcue->fade_tab, fade_label);
	gtk_notebook_append_page(notebook, fcue->levels_tab->root, levels_label);
	gtk_widget_show(fcue->fade_tab);
	gtk_widget_show(fcue->levels_tab->root);

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

	size_t input_channels = 0;
	if (target_cue != NULL)
	{
		input_channels = stack_cue_get_active_channels(target_cue, NULL, false);
	}
	stack_audio_levels_tab_populate(fcue->levels_tab, input_channels, cue->parent->channels, true, NULL);

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

	// Find our levels page
	page = gtk_notebook_page_num(notebook, STACK_FADE_CUE(cue)->levels_tab->root);

	// If we've found the page, remove it
	if (page >= 0)
	{
		gtk_notebook_remove_page(notebook, page);
	}

	// Destroy the levels tab
	stack_audio_levels_tab_destroy(STACK_FADE_CUE(cue)->levels_tab);
	STACK_FADE_CUE(cue)->levels_tab = NULL;
}

/// Saves the details of this cue as JSON
static char *stack_fade_cue_to_json(StackCue *cue)
{
	StackFadeCue *fcue = STACK_FADE_CUE(cue);

	// Build JSON
	Json::Value cue_root;
	stack_property_write_json(stack_cue_get_property(cue, "target"), &cue_root);
	stack_property_write_json(stack_cue_get_property(cue, "master_volume"), &cue_root);
	stack_property_write_json(stack_cue_get_property(cue, "stop_target"), &cue_root);
	stack_property_write_json(stack_cue_get_property(cue, "profile"), &cue_root);

	// If we've got a target, and the target has a file
	size_t input_channels = 0;
	cue_uid_t target_uid = STACK_CUE_UID_NONE;
	stack_property_get_uint64(stack_cue_get_property(cue, "target"), STACK_PROPERTY_VERSION_DEFINED, &target_uid);
	StackCue *target = stack_cue_get_by_uid(target_uid);
	if (target != NULL)
	{
		input_channels = stack_cue_get_active_channels(target, NULL, false);
		cue_root["_last_input_channels"] = input_channels;

		for (size_t channel = 0; channel < input_channels; channel++)
		{
			const StackProperty *channel_volume = stack_fade_cue_get_volume_property(cue, channel + 1, false);
			if (channel_volume != NULL)
			{
				stack_property_write_json(channel_volume, &cue_root);
			}
		}

		for (size_t input_channel = 0; input_channel < input_channels; input_channel++)
		{
			for (size_t output_channel = 0; output_channel < cue->parent->channels; output_channel++)
			{
				const StackProperty *crosspoint = stack_fade_cue_get_crosspoint_property(cue, input_channel, output_channel, false);
				if (crosspoint != NULL)
				{
					stack_property_write_json(crosspoint, &cue_root);
				}
			}
		}
	}

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
	if (cue_data.isMember("master_volume"))
	{
		if (cue_data["master_volume"].isString() && cue_data["master_volume"].asString() == "-Infinite")
		{
			stack_fade_cue_set_master_volume(STACK_FADE_CUE(cue), -INFINITY, false);
		}
		else if (cue_data["master_volume"].isNull())
		{
			stack_fade_cue_set_master_volume(STACK_FADE_CUE(cue), 0.0, true);
		}
		else
		{
			stack_fade_cue_set_master_volume(STACK_FADE_CUE(cue), cue_data["master_volume"].asDouble(), false);
		}
	}
	// Older versions used target_volume before we enforced a well-known name
	else if (cue_data.isMember("target_volume"))
	{
		if (cue_data["target_volume"].isString() && cue_data["target_volume"].asString() == "-Infinite")
		{
			stack_fade_cue_set_master_volume(STACK_FADE_CUE(cue), -INFINITY, false);
		}
		// Note: older versions didn't have a null value possible here
		else
		{
			stack_fade_cue_set_master_volume(STACK_FADE_CUE(cue), cue_data["target_volume"].asDouble(), false);
		}
	}
	stack_fade_cue_set_stop_target(STACK_FADE_CUE(cue), cue_data["stop_target"].asBool());
	stack_fade_cue_set_profile(STACK_FADE_CUE(cue), (StackFadeProfile)cue_data["profile"].asInt());

	if (cue_data.isMember("_last_input_channels") && cue_data["_last_input_channels"].isUInt())
	{
		size_t input_channels = cue_data["_last_input_channels"].asUInt();

		for (size_t channel = 0; channel < input_channels; channel++)
		{
			char property_name[64];
			snprintf(property_name, 64, "input_%lu_volume", channel + 1);
			if (cue_data.isMember(property_name))
			{
				StackProperty *channel_property = stack_fade_cue_get_volume_property(cue, channel + 1, true);
				if (cue_data[property_name].isString() && cue_data[property_name].asString() == "-Infinite")
				{
					stack_property_set_double(channel_property, STACK_PROPERTY_VERSION_DEFINED, -INFINITY);
					stack_property_set_null(channel_property, STACK_PROPERTY_VERSION_DEFINED, false);
				}
				else if (cue_data[property_name].isNull())
				{
					stack_property_set_double(channel_property, STACK_PROPERTY_VERSION_DEFINED, 0.0);
					stack_property_set_null(channel_property, STACK_PROPERTY_VERSION_DEFINED, true);
				}
				else
				{
					stack_property_set_double(channel_property, STACK_PROPERTY_VERSION_DEFINED, cue_data[property_name].asDouble());
					stack_property_set_null(channel_property, STACK_PROPERTY_VERSION_DEFINED, false);
				}
			}
		}

		for (size_t input_channel = 0; input_channel < input_channels; input_channel++)
		{
			for (size_t output_channel = 0; output_channel < cue->parent->channels; output_channel++)
			{
				char property_name[64];
				snprintf(property_name, 64, "crosspoint_%lu_%lu", output_channel, input_channel);

				if (cue_data.isMember(property_name))
				{
					StackProperty *crosspoint_property = stack_fade_cue_get_crosspoint_property(cue, input_channel, output_channel, true);
					if (cue_data[property_name].isString() && cue_data[property_name].asString() == "-Infinite")
					{
						stack_property_set_double(crosspoint_property, STACK_PROPERTY_VERSION_DEFINED, -INFINITY);
						stack_property_set_null(crosspoint_property, STACK_PROPERTY_VERSION_DEFINED, false);
					}
					else if (cue_data[property_name].isNull())
					{
						stack_property_set_double(crosspoint_property, STACK_PROPERTY_VERSION_DEFINED, 0.0);
						stack_property_set_null(crosspoint_property, STACK_PROPERTY_VERSION_DEFINED, true);
					}
					else
					{
						stack_property_set_double(crosspoint_property, STACK_PROPERTY_VERSION_DEFINED, cue_data[property_name].asDouble());
						stack_property_set_null(crosspoint_property, STACK_PROPERTY_VERSION_DEFINED, false);
					}
				}
			}
		}
	}
}

/// Gets the error message for the cue
bool stack_fade_cue_get_error(StackCue *cue, char *message, size_t size)
{
	cue_uid_t target_uid = STACK_CUE_UID_NONE;
	stack_property_get_uint64(stack_cue_get_property(cue, "target"), STACK_PROPERTY_VERSION_LIVE, &target_uid);

	if (target_uid == STACK_CUE_UID_NONE)
	{
		snprintf(message, size, "No target cue chosen");
		return true;
	}

	// Default condition: no error
	strncpy(message, "", size);
	return false;
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
	StackCueClass* fade_cue_class = new StackCueClass{ "StackFadeCue", "StackCue", "Fade Cue", stack_fade_cue_create, stack_fade_cue_destroy, stack_fade_cue_play, NULL, stack_fade_cue_stop, stack_fade_cue_pulse, stack_fade_cue_set_tabs, stack_fade_cue_unset_tabs, stack_fade_cue_to_json, stack_fade_cue_free_json, stack_fade_cue_from_json, stack_fade_cue_get_error, NULL, NULL, stack_fade_cue_get_field, stack_fade_cue_get_icon, NULL, NULL };
	stack_register_cue_class(fade_cue_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_fade_cue_register();
	return true;
}

