// Includes:
#include "StackApp.h"
#include "StackLog.h"
#include "StackMidiCue.h"
#include "StackJson.h"
#include "StackGtkHelper.h"
#include <cstring>
#include <cstdlib>
#include <string>
#include <cmath>

// Global: A single instance of our builder so we don't have to keep reloading
// it every time we change the selected cue
static GtkBuilder *smc_builder = NULL;

// Global: A single instace of our icon
static GdkPixbuf *icon = NULL;

// Pre-defs:
bool stack_midi_cue_get_error(StackCue *cue, char *message, size_t size);

////////////////////////////////////////////////////////////////////////////////
// HELPERS

// Populate the values of the parameter edit boxes, based on event type
static void stack_midi_cue_update_param_ui(uint8_t event_type, uint8_t param1, uint8_t param2)
{
	char param1_buffer[8], param2_buffer[8];

	if (event_type == STACK_MIDI_EVENT_PITCH_BEND)
	{
		// Unpack the 14-bit value from the two 7-bit values
		snprintf(param1_buffer, sizeof(param1_buffer) - 1, "%u", ((uint16_t)param2 << 7) | (uint16_t)param1);
		strncpy(param2_buffer, "", sizeof(param2_buffer) - 1);
	}
	else
	{
		// Determine if parameter one could be expressed as a note
		bool param1_is_note = false;
		stack_midi_event_get_descriptor(event_type, &param1_is_note, NULL);

		if (param1_is_note)
		{
			stack_midi_event_get_note_name(param1_buffer, sizeof(param1_buffer) - 1, param1);
		}
		else
		{
			snprintf(param1_buffer, sizeof(param1_buffer) - 1, "%u", param1);
		}

		snprintf(param2_buffer, sizeof(param2_buffer) - 1, "%u", param2);
	}

	// Actually update the UI
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(smc_builder, "mcpParam1Entry")), param1_buffer);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(smc_builder, "mcpParam2Entry")), param2_buffer);
}

////////////////////////////////////////////////////////////////////////////////
// PROPERTY CHANGE CALLBACKS

static void stack_midi_cue_ccb_generic(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackMidiCue* cue = STACK_MIDI_CUE(user_data);

		// Notify cue list that we've changed
		stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue), property);

		// Update our error status
		stack_midi_cue_get_error(STACK_CUE(cue), NULL, 0);

		// Fire an updated-selected-cue signal to signal the UI to change (we might
		// have changed state)
		if (cue->midi_tab)
		{
			StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->midi_tab));
			g_signal_emit_by_name((gpointer)window, "update-selected-cue");
		}
	}
}

static void stack_midi_cue_ccb_channel(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackMidiCue* cue = STACK_MIDI_CUE(user_data);

		// Notify cue list that we've changed
		stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue), property);

		// Update our error status
		stack_midi_cue_get_error(STACK_CUE(cue), NULL, 0);

		// Fire an updated-selected-cue signal to signal the UI to change (we might
		// have changed state)
		if (cue->midi_tab)
		{
			StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->midi_tab));
			g_signal_emit_by_name((gpointer)window, "update-selected-cue");

			uint8_t channel;
			char buffer[8];
			stack_property_get_uint8(property, STACK_PROPERTY_VERSION_DEFINED, &channel);
			snprintf(buffer, 8, "%u", channel);
			gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(smc_builder, "mcpChannelEntry")), buffer);
		}
	}
}

static void stack_midi_cue_ccb_param1(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackMidiCue* cue = STACK_MIDI_CUE(user_data);

		// Notify cue list that we've changed
		stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue), property);

		// Update our error status
		stack_midi_cue_get_error(STACK_CUE(cue), NULL, 0);

		// Fire an updated-selected-cue signal to signal the UI to change (we might
		// have changed state)
		if (cue->midi_tab)
		{
			StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->midi_tab));
			g_signal_emit_by_name((gpointer)window, "update-selected-cue");

			uint8_t event_type, param1, param2;

			// Determine the event type as it affects what param1 could be
			stack_property_get_uint8(stack_cue_get_property(STACK_CUE(cue), "event_type"), STACK_PROPERTY_VERSION_DEFINED, &event_type);
			stack_property_get_uint8(property, STACK_PROPERTY_VERSION_DEFINED, &param1);

			// We need the value of param2 for this as well
			stack_property_get_uint8(stack_cue_get_property(STACK_CUE(cue), "param2"), STACK_PROPERTY_VERSION_DEFINED, &param2);

			// Update the UI
			stack_midi_cue_update_param_ui(event_type, param1, param2);
		}
	}
}

static void stack_midi_cue_ccb_param2(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackMidiCue* cue = STACK_MIDI_CUE(user_data);

		// Notify cue list that we've changed
		stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue), property);

		// Update our error status
		stack_midi_cue_get_error(STACK_CUE(cue), NULL, 0);

		// Fire an updated-selected-cue signal to signal the UI to change (we might
		// have changed state)
		if (cue->midi_tab)
		{
			StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->midi_tab));
			g_signal_emit_by_name((gpointer)window, "update-selected-cue");

			uint8_t event_type, param1, param2;

			// Determine the event type as it affects what param1 could be
			stack_property_get_uint8(stack_cue_get_property(STACK_CUE(cue), "event_type"), STACK_PROPERTY_VERSION_DEFINED, &event_type);
			stack_property_get_uint8(property, STACK_PROPERTY_VERSION_DEFINED, &param2);

			// We need the value of param1 for this as well
			stack_property_get_uint8(stack_cue_get_property(STACK_CUE(cue), "param1"), STACK_PROPERTY_VERSION_DEFINED, &param1);

			// Update the UI
			stack_midi_cue_update_param_ui(event_type, param1, param2);
		}
	}
}

/// Pause or resumes change callbacks on variables
static void stack_midi_cue_pause_change_callbacks(StackCue *cue, bool pause)
{
	stack_property_pause_change_callback(stack_cue_get_property(cue, "midi_patch"), pause);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "event_type"), pause);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "channel"), pause);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "param1"), pause);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "param2"), pause);
}

////////////////////////////////////////////////////////////////////////////////
// PROPERTY VALIDATORS

uint8_t stack_midi_cue_validate_channel(StackPropertyUInt8 *property, StackPropertyVersion version, const uint8_t value, void *user_data)
{
	// Limit the value from 1 to 16
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		if (value < 1)
		{
			return 1;
		}
		if (value > 16)
		{
			return 16;
		}
	}

	return value;
}

uint8_t stack_midi_cue_validate_parameter(StackPropertyUInt8 *property, StackPropertyVersion version, const uint8_t value, void *user_data)
{
	// Limit the value from 0 to 127
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		if (value > 127)
		{
			return 127;
		}
	}

	return value;
}

////////////////////////////////////////////////////////////////////////////////
// CREATION AND DESTRUCTION

/// Creates an MIDI cue
static StackCue* stack_midi_cue_create(StackCueList *cue_list)
{
	// Allocate the cue
	StackMidiCue* cue = new StackMidiCue();

	// Initialise the superclass
	stack_cue_init(&cue->super, cue_list);

	// Make this class a StackMidiCue
	cue->super._class_name = "StackMidiCue";

	// We start in error state until we have an event type
	stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_ERROR);

	// Initialise our variables
	cue->midi_tab = NULL;
	stack_cue_set_action_time(STACK_CUE(cue), 1);

	StackProperty *midi_patch = stack_property_create("midi_patch", STACK_PROPERTY_TYPE_STRING);
	stack_cue_add_property(STACK_CUE(cue), midi_patch);
	stack_property_set_changed_callback(midi_patch, stack_midi_cue_ccb_generic, (void*)cue);

	StackProperty *event_type = stack_property_create("event_type", STACK_PROPERTY_TYPE_UINT8);
	stack_cue_add_property(STACK_CUE(cue), event_type);
	stack_property_set_changed_callback(event_type, stack_midi_cue_ccb_generic, (void*)cue);
	stack_property_set_uint8(event_type, STACK_PROPERTY_VERSION_DEFINED, STACK_MIDI_EVENT_NOTE_ON);

	StackProperty *channel = stack_property_create("channel", STACK_PROPERTY_TYPE_UINT8);
	stack_cue_add_property(STACK_CUE(cue), channel);
	stack_property_set_changed_callback(channel, stack_midi_cue_ccb_channel, (void*)cue);
	stack_property_set_validator(channel, (stack_property_validator_t)stack_midi_cue_validate_channel, (void*)cue);
	stack_property_set_uint8(channel, STACK_PROPERTY_VERSION_DEFINED, 1);

	StackProperty *param1 = stack_property_create("param1", STACK_PROPERTY_TYPE_UINT8);
	stack_cue_add_property(STACK_CUE(cue), param1);
	stack_property_set_changed_callback(param1, stack_midi_cue_ccb_param1, (void*)cue);
	stack_property_set_validator(param1, (stack_property_validator_t)stack_midi_cue_validate_parameter, (void*)cue);

	StackProperty *param2 = stack_property_create("param2", STACK_PROPERTY_TYPE_UINT8);
	stack_cue_add_property(STACK_CUE(cue), param2);
	stack_property_set_changed_callback(param2, stack_midi_cue_ccb_param2, (void*)cue);
	stack_property_set_validator(param2, (stack_property_validator_t)stack_midi_cue_validate_parameter, (void*)cue);

	// Initialise superclass variables
	stack_cue_set_name(STACK_CUE(cue), "MIDI ${type} event, channel ${channel}");

	return STACK_CUE(cue);
}

/// Destroys an MIDI cue
static void stack_midi_cue_destroy(StackCue *cue)
{
	// Call parent destructor
	stack_cue_destroy_base(cue);
}

////////////////////////////////////////////////////////////////////////////////
// PROPERTY SETTERS

// Sets the MIDI patch
void stack_midi_cue_set_midi_patch(StackMidiCue *cue, const char *midi_patch)
{
	stack_property_set_string(stack_cue_get_property(STACK_CUE(cue), "midi_patch"), STACK_PROPERTY_VERSION_DEFINED, midi_patch);
}

// Sets the event type
void stack_midi_cue_set_event_type(StackMidiCue *cue, uint8_t event_type)
{
	stack_property_set_uint8(stack_cue_get_property(STACK_CUE(cue), "event_type"), STACK_PROPERTY_VERSION_DEFINED, event_type);
}

// Sets the channel
void stack_midi_cue_set_channel(StackMidiCue *cue, uint8_t channel)
{
	stack_property_set_uint8(stack_cue_get_property(STACK_CUE(cue), "channel"), STACK_PROPERTY_VERSION_DEFINED, channel);
}

// Sets parameter 1
void stack_midi_cue_set_param1(StackMidiCue *cue, uint8_t param1)
{
	stack_property_set_uint8(stack_cue_get_property(STACK_CUE(cue), "param1"), STACK_PROPERTY_VERSION_DEFINED, param1);
}

// Sets parameter 2
void stack_midi_cue_set_param2(StackMidiCue *cue, uint8_t param2)
{
	stack_property_set_uint8(stack_cue_get_property(STACK_CUE(cue), "param2"), STACK_PROPERTY_VERSION_DEFINED, param2);
}

////////////////////////////////////////////////////////////////////////////////
// UI CALLBACKS

/// Called when the MIDI Patch is changed
static void mcp_midi_patch_changed(GtkEntry *widget, GdkEvent *event, gpointer user_data)
{
	// Get the cue
	StackMidiCue *cue = STACK_MIDI_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);

	const char *new_patch = gtk_entry_get_text(widget);
	stack_midi_cue_set_midi_patch(cue, new_patch);

	return;
}

/// Called when the Event Type is changed
static void mcp_event_type_changed(GtkComboBox *widget, gpointer user_data)
{
	// Get the cue
	StackMidiCue *cue = STACK_MIDI_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);

	const char *event_type = gtk_combo_box_get_active_id(widget);
	int event_type_int = atoi(event_type);

	// Get the widgets we want to update
	GtkLabel *mcpParam1 = GTK_LABEL(gtk_builder_get_object(smc_builder, "mcpParam1Label"));
	GtkLabel *mcpParam2 = GTK_LABEL(gtk_builder_get_object(smc_builder, "mcpParam2Label"));
	GtkEntry *mcpParam2Entry = GTK_ENTRY(gtk_builder_get_object(smc_builder, "mcpParam2Entry"));

	char param1_name[16];
	char param2_name[16];

	int param_count = stack_midi_event_get_param_names(event_type_int, param1_name, sizeof(param1_name), param2_name, sizeof(param2_name));
	bool show_param2 = (param_count == 2);

	// Set the labels and show/hide as necessary
	strncat(param1_name, ":", sizeof(param1_name) - 1);
	strncat(param2_name, ":", sizeof(param2_name) - 1);
	gtk_label_set_text(mcpParam1, param1_name);
	gtk_label_set_text(mcpParam2, param2_name);

	// Param 1 could have changed from a note name to a value or vice versa, so
	// reset the value
	uint8_t param1 = 0, param2 = 0;
	stack_property_get_uint8(stack_cue_get_property(STACK_CUE(cue), "param1"), STACK_PROPERTY_VERSION_DEFINED, &param1);
	stack_property_get_uint8(stack_cue_get_property(STACK_CUE(cue), "param2"), STACK_PROPERTY_VERSION_DEFINED, &param2);

	// Update the UI
	stack_midi_cue_update_param_ui(event_type_int, param1, param2);

	// Show/hide parameter two as necessary
	gtk_widget_set_visible(GTK_WIDGET(mcpParam2), show_param2);
	gtk_widget_set_visible(GTK_WIDGET(mcpParam2Entry), show_param2);

	stack_midi_cue_set_event_type(cue, (uint8_t)event_type_int);

	return;
}

/// Called when the Channel is changed
static void mcp_channel_changed(GtkEntry *widget, GdkEvent *event, gpointer user_data)
{
	// Get the cue
	StackMidiCue *cue = STACK_MIDI_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);

	const char *new_channel = gtk_entry_get_text(widget);
	int new_channel_int = atoi(new_channel);
	stack_midi_cue_set_channel(cue, (uint8_t)new_channel_int);

	return;
}

/// Called when parameter one is changed
static void mcp_param1_changed(GtkEntry *widget, GdkEvent *event, gpointer user_data)
{
	// Get the cue
	StackMidiCue *cue = STACK_MIDI_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);

	const char *new_param1 = gtk_entry_get_text(widget);
	int new_param1_int = atoi(new_param1);

	// See what event type we are
	GtkComboBox *mcpEventType = GTK_COMBO_BOX(gtk_builder_get_object(smc_builder, "mcpEventType"));
	int event_type = atoi(gtk_combo_box_get_active_id(mcpEventType));

	// Determine if parameter one could be expressed as a note
	bool param1_is_note = false;
	stack_midi_event_get_descriptor(event_type, &param1_is_note, NULL);

	if (event_type == STACK_MIDI_EVENT_PITCH_BEND)
	{
		// The validate callbacks only deal with the 0-127 range nicely, so
		// validate this here
		if (new_param1_int > 16383)
		{
			new_param1_int = 16383;
		}

		// Handle 14-bit entry of pitch bend, note that for change callback reasons
		// (they update the textboxes with validated values), we must set param 2
		// first
		stack_midi_cue_set_param2(cue, (uint8_t)((new_param1_int >> 7) & 0x0000007f));
		stack_midi_cue_set_param1(cue, (uint8_t)(new_param1_int & 0x0000007f));

		return;
	}

	if (param1_is_note)
	{
		// Convert note name to value if that's how it's specified
		if (param1_is_note && ((new_param1[0] >= 'A' && new_param1[0] <= 'G') || (new_param1[0] >= 'a' && new_param1[0] <= 'g')))
		{
			new_param1_int = stack_midi_event_note_name_to_value(new_param1);
		}
	}

	stack_midi_cue_set_param1(cue, (uint8_t)new_param1_int);

	return;
}

/// Called when parameter two is changed
static void mcp_param2_changed(GtkEntry *widget, GdkEvent *event, gpointer user_data)
{
	// Get the cue
	StackMidiCue *cue = STACK_MIDI_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);

	const char *new_param2 = gtk_entry_get_text(widget);
	int new_param2_int = atoi(new_param2);
	stack_midi_cue_set_param2(cue, (uint8_t)new_param2_int);

	return;
}

/// Called when the patch settings button is clicked
static void mcp_patch_settings_clicked(GtkWidget *widget, gpointer user_data)
{
	StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(widget);
	sss_show_dialog(window, window->cue_list, STACK_SETTINGS_TAB_MIDI);
}

////////////////////////////////////////////////////////////////////////////////
// BASE CUE OPERATIONS

/// Start the cue playing
static bool stack_midi_cue_play(StackCue *cue)
{
	// Call the superclass
	if (!stack_cue_play_base(cue))
	{
		return false;
	}

	// Validate we have no errors
	if (stack_midi_cue_get_error(cue, NULL, 0))
	{
		return false;
	}

	// Copy the variables to live
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "midi_patch"));
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "event_type"));
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "channel"));
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "param1"));
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "param2"));

	return true;
}

/// Update the cue based on time
static void stack_midi_cue_pulse(StackCue *cue, stack_time_t clocktime)
{
	// Get the cue state before the base class potentially updates it
	StackCueState pre_pulse_state = cue->state;

	// Call superclass
	stack_cue_pulse_base(cue, clocktime);

	// We have zero action time so we need to detect the transition from pre->something or playing->something
	if ((pre_pulse_state == STACK_CUE_STATE_PLAYING_PRE && cue->state != STACK_CUE_STATE_PLAYING_PRE) ||
		(pre_pulse_state == STACK_CUE_STATE_PLAYING_ACTION && cue->state != STACK_CUE_STATE_PLAYING_ACTION))
	{
		// Get the value of our properties
		char *midi_patch = NULL;
		uint8_t event_type = STACK_MIDI_EVENT_NOTE_ON, channel = 1, param1 = 0, param2 = 0;
		stack_property_get_string(stack_cue_get_property(cue, "midi_patch"), STACK_PROPERTY_VERSION_LIVE, &midi_patch);
		stack_property_get_uint8(stack_cue_get_property(cue, "event_type"), STACK_PROPERTY_VERSION_LIVE, &event_type);
		stack_property_get_uint8(stack_cue_get_property(cue, "channel"), STACK_PROPERTY_VERSION_LIVE, &channel);
		stack_property_get_uint8(stack_cue_get_property(cue, "param1"), STACK_PROPERTY_VERSION_LIVE, &param1);
		stack_property_get_uint8(stack_cue_get_property(cue, "param2"), STACK_PROPERTY_VERSION_LIVE, &param2);

		// Attempt to get the device
		if (midi_patch != NULL)
		{
			StackMidiDevice *midi_device = stack_cue_list_get_midi_device(cue->parent, midi_patch);
			if (midi_device != NULL)
			{
				StackMidiEvent *event = stack_midi_event_new_short(event_type, channel, param1, param2);
				stack_midi_device_send_event(midi_device, event);
				stack_midi_event_free(event);
			}
		}
	}
}

/// Sets up the tabs for the MIDI cue
static void stack_midi_cue_set_tabs(StackCue *cue, GtkNotebook *notebook)
{
	StackMidiCue *acue = STACK_MIDI_CUE(cue);

	// Create the tab
	GtkWidget *label = gtk_label_new("MIDI Event");

	// Load the UI (if we haven't already)
	if (smc_builder == NULL)
	{
		smc_builder = gtk_builder_new_from_resource("/org/stack/ui/StackMidiCue.ui");

		// Set up callbacks
		gtk_builder_add_callback_symbol(smc_builder, "mcp_midi_patch_changed", G_CALLBACK(mcp_midi_patch_changed));
		gtk_builder_add_callback_symbol(smc_builder, "mcp_event_type_changed", G_CALLBACK(mcp_event_type_changed));
		gtk_builder_add_callback_symbol(smc_builder, "mcp_channel_changed", G_CALLBACK(mcp_channel_changed));
		gtk_builder_add_callback_symbol(smc_builder, "mcp_param1_changed", G_CALLBACK(mcp_param1_changed));
		gtk_builder_add_callback_symbol(smc_builder, "mcp_param2_changed", G_CALLBACK(mcp_param2_changed));
		gtk_builder_add_callback_symbol(smc_builder, "mcp_patch_settings_clicked", G_CALLBACK(mcp_patch_settings_clicked));

		// Connect the signals
		gtk_builder_connect_signals(smc_builder, NULL);

		// Set up the limits on the entry (not on param 1 as it could be a note name)
		stack_limit_gtk_entry_int(GTK_ENTRY(gtk_builder_get_object(smc_builder, "mcpChannelEntry")), false);
		stack_limit_gtk_entry_int(GTK_ENTRY(gtk_builder_get_object(smc_builder, "mcpParam2Entry")), false);
	}
	acue->midi_tab = GTK_WIDGET(gtk_builder_get_object(smc_builder, "mcpGrid"));

	// Pause change callbacks on the properties
	stack_midi_cue_pause_change_callbacks(cue, true);

	// Add an extra reference to the MIDI tab - we're about to remove it's
	// parent and we don't want it to get garbage collected
	g_object_ref(acue->midi_tab);

	// The tab has a parent window in the UI file - unparent the tab container from it
	gtk_widget_unparent(acue->midi_tab);

	// Append the tab (and show it, because it starts off hidden...)
	gtk_notebook_append_page(notebook, acue->midi_tab, label);
	gtk_widget_show(acue->midi_tab);

	// Set the values
	char *midi_patch = NULL;
	char buffer[8];
	uint8_t event_type = STACK_MIDI_EVENT_NOTE_ON, channel = 1, param1 = 0, param2 = 0;
	stack_property_get_string(stack_cue_get_property(cue, "midi_patch"), STACK_PROPERTY_VERSION_DEFINED, &midi_patch);
	stack_property_get_uint8(stack_cue_get_property(cue, "event_type"), STACK_PROPERTY_VERSION_DEFINED, &event_type);
	stack_property_get_uint8(stack_cue_get_property(cue, "channel"), STACK_PROPERTY_VERSION_DEFINED, &channel);
	stack_property_get_uint8(stack_cue_get_property(cue, "param1"), STACK_PROPERTY_VERSION_DEFINED, &param1);
	stack_property_get_uint8(stack_cue_get_property(cue, "param2"), STACK_PROPERTY_VERSION_DEFINED, &param2);

	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(smc_builder, "mcpMidiPatchEntry")), midi_patch != NULL ? midi_patch : "");
	snprintf(buffer, sizeof(buffer) - 1, "%u", event_type);
	gtk_combo_box_set_active_id(GTK_COMBO_BOX(gtk_builder_get_object(smc_builder, "mcpEventType")), buffer);
	snprintf(buffer, sizeof(buffer) - 1, "%u", channel);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(smc_builder, "mcpChannelEntry")), buffer);
	stack_midi_cue_update_param_ui(event_type, param1, param2);

	// Resume change callbacks on the properties
	stack_midi_cue_pause_change_callbacks(cue, false);
}

/// Removes the properties tabs for a MIDI cue
static void stack_midi_cue_unset_tabs(StackCue *cue, GtkNotebook *notebook)
{
	// Find our media page
	gint page = gtk_notebook_page_num(notebook, STACK_MIDI_CUE(cue)->midi_tab);

	// If we've found the page, remove it
	if (page >= 0)
	{
		gtk_notebook_remove_page(notebook, page);
	}

	// Remove our reference to the MIDI tab
	g_object_unref(STACK_MIDI_CUE(cue)->midi_tab);

	// Be tidy
	STACK_MIDI_CUE(cue)->midi_tab = NULL;
}

/// Saves the details of this cue as JSON
static char *stack_midi_cue_to_json(StackCue *cue)
{
	StackMidiCue *acue = STACK_MIDI_CUE(cue);

	// Build JSON
	Json::Value cue_root;

	// We do nothing here as we only have properties
	stack_property_write_json(stack_cue_get_property(cue, "midi_patch"), &cue_root);
	stack_property_write_json(stack_cue_get_property(cue, "event_type"), &cue_root);
	stack_property_write_json(stack_cue_get_property(cue, "channel"), &cue_root);
	stack_property_write_json(stack_cue_get_property(cue, "param1"), &cue_root);
	stack_property_write_json(stack_cue_get_property(cue, "param2"), &cue_root);

	// Write out JSON string and return (to be free'd by
	// stack_fade_cue_free_json)
	Json::StreamWriterBuilder builder;
	return strdup(Json::writeString(builder, cue_root).c_str());
}

/// Frees JSON strings as returned by stack_midi_cue_to_json
static void stack_midi_cue_free_json(StackCue *cue, char *json_data)
{
	free(json_data);
}

/// Re-initialises this cue from JSON Data
void stack_midi_cue_from_json(StackCue *cue, const char *json_data)
{
	Json::Value cue_root;

	// Parse JSON data
	stack_json_read_string(json_data, &cue_root);

	// Get the data that's pertinent to us
	if (!cue_root.isMember("StackMidiCue"))
	{
		stack_log("stack_midi_cue_from_json(): Missing StackMidiCue class\n");
		return;
	}

	Json::Value& cue_data = cue_root["StackMidiCue"];

	// Load in values from JSON
	if (cue_data.isMember("midi_patch"))
	{
		stack_midi_cue_set_midi_patch(STACK_MIDI_CUE(cue), cue_data["midi_patch"].asString().c_str());
	}
	if (cue_data.isMember("event_type"))
	{
		stack_midi_cue_set_event_type(STACK_MIDI_CUE(cue), cue_data["event_type"].asUInt());
	}
	if (cue_data.isMember("channel"))
	{
		stack_midi_cue_set_channel(STACK_MIDI_CUE(cue), cue_data["channel"].asUInt());
	}
	if (cue_data.isMember("param1"))
	{
		stack_midi_cue_set_param1(STACK_MIDI_CUE(cue), cue_data["param1"].asUInt());
	}
	if (cue_data.isMember("param2"))
	{
		stack_midi_cue_set_param2(STACK_MIDI_CUE(cue), cue_data["param2"].asUInt());
	}

	// Update our error status
	stack_midi_cue_get_error(cue, NULL, 0);
}

/// Gets the error message for the cue
bool stack_midi_cue_get_error(StackCue *cue, char *message, size_t size)
{
	// Get the MIDI patch
	char *midi_patch = NULL;
	stack_property_get_string(stack_cue_get_property(cue, "midi_patch"), STACK_PROPERTY_VERSION_DEFINED, &midi_patch);

	if (midi_patch == NULL || strlen(midi_patch) == 0)
	{
		if (message != NULL)
		{
			snprintf(message, size, "No MIDI patch specified");
		}

		if (cue->state == STACK_CUE_STATE_STOPPED)
		{
			cue->state = STACK_CUE_STATE_ERROR;
		}

		return true;
	}

	// Attempt to find the MIDI device
	StackMidiDevice *device = stack_cue_list_get_midi_device(cue->parent, midi_patch);
	if (device == NULL)
	{
		if (message != NULL)
		{
			snprintf(message, size, "Given MIDI patch does not exist");
		}

		if (cue->state == STACK_CUE_STATE_STOPPED)
		{
			cue->state = STACK_CUE_STATE_ERROR;
		}
		return true;
	}

	// Default condition: no error
	if (message != NULL)
	{
		strncpy(message, "", size);
	}

	if (cue->state == STACK_CUE_STATE_ERROR)
	{
		cue->state = STACK_CUE_STATE_STOPPED;
	}

	return false;
}

const char *stack_midi_cue_get_field(StackCue *cue, const char *field)
{
	uint8_t value = 0;
	StackMidiCue *mcue = STACK_MIDI_CUE(cue);

	if (strcmp(field, "type") == 0)
	{
		stack_property_get_uint8(stack_cue_get_property(cue, "event_type"), STACK_PROPERTY_VERSION_DEFINED, &value);
		stack_midi_event_get_name_from_type(mcue->field_event, sizeof(mcue->field_event) - 1, value);
		return mcue->field_event;
	}
	else if (strcmp(field, "channel") == 0)
	{
		stack_property_get_uint8(stack_cue_get_property(cue, "channel"), STACK_PROPERTY_VERSION_DEFINED, &value);
		snprintf(mcue->field_channel, sizeof(mcue->field_channel) - 1, "%u", value);
		return mcue->field_channel;
	}
	else if (strcmp(field, "param1") == 0)
	{
		stack_property_get_uint8(stack_cue_get_property(cue, "param1"), STACK_PROPERTY_VERSION_DEFINED, &value);
		snprintf(mcue->field_param1, sizeof(mcue->field_param1) - 1, "%u", value);
		return mcue->field_param1;
	}
	else if (strcmp(field, "param2") == 0)
	{
		stack_property_get_uint8(stack_cue_get_property(cue, "param2"), STACK_PROPERTY_VERSION_DEFINED, &value);
		snprintf(mcue->field_param2, sizeof(mcue->field_param2) - 1, "%u", value);
		return mcue->field_param2;
	}

	// Call the super class if we didn't return anything
	return stack_cue_get_field_base(cue, field);
}

/// Returns the icon for a cue
/// @param cue The cue to get the icon of
GdkPixbuf *stack_midi_cue_get_icon(StackCue *cue)
{
	return icon;
}

////////////////////////////////////////////////////////////////////////////////
// CLASS REGISTRATION

// Registers StackMidiCue with the application
void stack_midi_cue_register()
{
	// Load the icons
	icon = gdk_pixbuf_new_from_resource("/org/stack/icons/stackmidicue.png", NULL);

	// Register built in cue types
	StackCueClass* midi_cue_class = new StackCueClass{ "StackMidiCue", "StackCue", "MIDI Cue", stack_midi_cue_create, stack_midi_cue_destroy, stack_midi_cue_play, NULL, NULL, stack_midi_cue_pulse, stack_midi_cue_set_tabs, stack_midi_cue_unset_tabs, stack_midi_cue_to_json, stack_midi_cue_free_json, stack_midi_cue_from_json, stack_midi_cue_get_error, NULL, NULL, stack_midi_cue_get_field, stack_midi_cue_get_icon, NULL, NULL };
	stack_register_cue_class(midi_cue_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_midi_cue_register();
	return true;
}
