// Includes:
#include "StackApp.h"
#include "StackLog.h"
#include "StackActionCue.h"
#include <cstring>
#include <cstdlib>
#include <string>
#include <cmath>
#include <json/json.h>

// Global: A single instance of our builder so we don't have to keep reloading
// it every time we change the selected cue
static GtkBuilder *sac_builder = NULL;

// Global: A single instace of each of our icons
static GdkPixbuf *icon_pause = NULL;
static GdkPixbuf *icon_play = NULL;
static GdkPixbuf *icon_stop = NULL;

static void stack_action_cue_ccb_target(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackActionCue* cue = STACK_ACTION_CUE(user_data);

		// Notify cue list that we've changed
		stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue), property);

		// Fire an updated-selected-cue signal to signal the UI to change (we might
		// have changed state)
		if (cue->action_tab)
		{
			StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->action_tab));
			g_signal_emit_by_name((gpointer)window, "update-selected-cue");
		}
	}
}

static void stack_action_cue_ccb_action(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackActionCue* cue = STACK_ACTION_CUE(user_data);

		// Notify cue list that we've changed
		stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue), property);

		// Fire an updated-selected-cue signal to signal the UI to change (we might
		// have changed state)
		if (cue->action_tab)
		{
			StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->action_tab));
			g_signal_emit_by_name((gpointer)window, "update-selected-cue");
		}
	}
}

/// Pause or resumes change callbacks on variables
static void stack_action_cue_pause_change_callbacks(StackCue *cue, bool pause)
{
	stack_property_pause_change_callback(stack_cue_get_property(cue, "action"), pause);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "target"), pause);
}

////////////////////////////////////////////////////////////////////////////////
// CREATION AND DESTRUCTION

/// Creates a action cue
static StackCue* stack_action_cue_create(StackCueList *cue_list)
{
	// Allocate the cue
	StackActionCue* cue = new StackActionCue();

	// Initialise the superclass
	stack_cue_init(&cue->super, cue_list);

	// Make this class a StackActionCue
	cue->super._class_name = "StackActionCue";

	// We start in error state until we have a target
	stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_ERROR);

	// Initialise our variables
	cue->action_tab = NULL;
	stack_cue_set_action_time(STACK_CUE(cue), 1);
	cue->target_cue_id_string[0] = '\0';

	// Add our properties
	StackProperty *target = stack_property_create("target", STACK_PROPERTY_TYPE_UINT64);
	stack_cue_add_property(STACK_CUE(cue), target);
	stack_property_set_uint64(target, STACK_PROPERTY_VERSION_DEFINED, STACK_CUE_UID_NONE);
	stack_property_set_changed_callback(target, stack_action_cue_ccb_target, (void*)cue);

	StackProperty *action = stack_property_create("action", STACK_PROPERTY_TYPE_INT32);
	stack_cue_add_property(STACK_CUE(cue), action);
	stack_property_set_int32(action, STACK_PROPERTY_VERSION_DEFINED, STACK_ACTION_CUE_STOP);
	stack_property_set_changed_callback(action, stack_action_cue_ccb_action, (void*)cue);

	// Initialise superclass variables
	stack_cue_set_name(STACK_CUE(cue), "${action} cue ${target}");

	return STACK_CUE(cue);
}

/// Destroys a action cue
static void stack_action_cue_destroy(StackCue *cue)
{
	// Call parent destructor
	stack_cue_destroy_base(cue);
}

////////////////////////////////////////////////////////////////////////////////
// PROPERTY SETTERS

// Sets the target cue of an action cue
void stack_action_cue_set_target(StackActionCue *cue, StackCue *target)
{
	cue_uid_t new_target = STACK_CUE_UID_NONE;

	if (target == NULL)
	{
		stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_ERROR);
	}
	else
	{
		new_target = target->uid;
		stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_STOPPED);
	}

	// Update the property
	stack_property_set_uint64(stack_cue_get_property(STACK_CUE(cue), "target"), STACK_PROPERTY_VERSION_DEFINED, new_target);
}

// Sets the performed action of an action cue
void stack_action_cue_set_action(StackActionCue *cue, StackActionCueAction action)
{
	stack_property_set_int32(stack_cue_get_property(STACK_CUE(cue), "action"), STACK_PROPERTY_VERSION_DEFINED, action);
}

////////////////////////////////////////////////////////////////////////////////
// UI CALLBACKS

/// Called when the Target Cue button is pressed
static void acp_cue_changed(GtkButton *widget, gpointer user_data)
{
	// Get the cue
	StackActionCue *cue = STACK_ACTION_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);

	// Get the parent window
	StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->action_tab));

	// Call the dialog and get the new target
	cue_uid_t current_target = STACK_CUE_UID_NONE;
	stack_property_get_uint64(stack_cue_get_property(STACK_CUE(cue), "target"), STACK_PROPERTY_VERSION_DEFINED, &current_target);
	StackCue *new_target = stack_select_cue_dialog(window, stack_cue_get_by_uid(current_target), STACK_CUE(cue));

	// Update the cue
	stack_action_cue_set_target(cue, new_target);

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
}

/// Called when the action changes
static void acp_action_changed(GtkToggleButton *widget, gpointer user_data)
{
	StackActionCue *cue = STACK_ACTION_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);

	// Get pointers to the four radio button options
	GtkToggleButton* r1 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(sac_builder, "acpActionTypePlay"));
	GtkToggleButton* r2 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(sac_builder, "acpActionTypePause"));
	GtkToggleButton* r3 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(sac_builder, "acpActionTypeStop"));

	// Determine which one is toggled on
	if (widget == r1 && gtk_toggle_button_get_active(r1)) { stack_action_cue_set_action(cue, STACK_ACTION_CUE_PLAY); }
	if (widget == r2 && gtk_toggle_button_get_active(r2)) { stack_action_cue_set_action(cue, STACK_ACTION_CUE_PAUSE); }
	if (widget == r3 && gtk_toggle_button_get_active(r3)) { stack_action_cue_set_action(cue, STACK_ACTION_CUE_STOP); }
}

////////////////////////////////////////////////////////////////////////////////
// BASE CUE OPERATIONS

/// Start the cue playing
static bool stack_action_cue_play(StackCue *cue)
{
	// Call the superclass
	if (!stack_cue_play_base(cue))
	{
		return false;
	}

	// Get the target cue UID
	cue_uid_t target = STACK_CUE_UID_NONE;
	stack_property_get_uint64(stack_cue_get_property(cue, "target"), STACK_PROPERTY_VERSION_DEFINED, &target);

	// If we don't have a valid target (unknown cue, or self) then we can't play
	if (stack_cue_get_by_uid(target) == NULL || target == cue->uid)
	{
		stack_log("stack_action_cue_play(): Invalid target cue: %lx\n", target);
		stack_cue_set_state(cue, STACK_CUE_STATE_ERROR);
		return false;
	}

	// Copy the variables to live
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "action"));
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "target"));

	return true;
}

/// Update the cue based on time
static void stack_action_cue_pulse(StackCue *cue, stack_time_t clocktime)
{
	// Get the cue state before the base class potentially updates it
	StackCueState pre_pulse_state = cue->state;

	// Call superclass
	stack_cue_pulse_base(cue, clocktime);

	// Get the target cue UID
	cue_uid_t target_uid = STACK_CUE_UID_NONE;
	stack_property_get_uint64(stack_cue_get_property(cue, "target"), STACK_PROPERTY_VERSION_LIVE, &target_uid);

	// Get the target cue UID
	int32_t action = STACK_ACTION_CUE_STOP;
	stack_property_get_int32(stack_cue_get_property(cue, "action"), STACK_PROPERTY_VERSION_LIVE, &action);

	// Get the target
	StackCue *target = stack_cue_get_by_uid(target_uid);

	// If the target is valid
	if (target != NULL)
	{
		// We have zero action time so we need to detect the transition from pre->something or playing->something
		if ((pre_pulse_state == STACK_CUE_STATE_PLAYING_PRE && cue->state != STACK_CUE_STATE_PLAYING_PRE) ||
			(pre_pulse_state == STACK_CUE_STATE_PLAYING_ACTION && cue->state != STACK_CUE_STATE_PLAYING_ACTION))
		{
			switch (action)
			{
				case STACK_ACTION_CUE_PLAY:
					stack_cue_play(target);
					break;

				case STACK_ACTION_CUE_PAUSE:
					stack_cue_pause(target);
					break;

				case STACK_ACTION_CUE_STOP:
					stack_cue_stop(target);
					break;
			}
		}
	}
}

/// Sets up the tabs for the action cue
static void stack_action_cue_set_tabs(StackCue *cue, GtkNotebook *notebook)
{
	StackActionCue *acue = STACK_ACTION_CUE(cue);

	// Create the tab
	GtkWidget *label = gtk_label_new("Action");

	// Load the UI (if we haven't already)
	if (sac_builder == NULL)
	{
		sac_builder = gtk_builder_new_from_resource("/org/stack/ui/StackActionCue.ui");

		// Set up callbacks
		gtk_builder_add_callback_symbol(sac_builder, "acp_cue_changed", G_CALLBACK(acp_cue_changed));
		gtk_builder_add_callback_symbol(sac_builder, "acp_action_changed", G_CALLBACK(acp_action_changed));

		// Connect the signals
		gtk_builder_connect_signals(sac_builder, NULL);
	}
	acue->action_tab = GTK_WIDGET(gtk_builder_get_object(sac_builder, "acpGrid"));

	// Pause change callbacks on the properties
	stack_action_cue_pause_change_callbacks(cue, true);

	// Add an extra reference to the action tab - we're about to remove it's
	// parent and we don't want it to get garbage collected
	g_object_ref(acue->action_tab);

	// The tab has a parent window in the UI file - unparent the tab container from it
	gtk_widget_unparent(acue->action_tab);

	// Append the tab (and show it, because it starts off hidden...)
	gtk_notebook_append_page(notebook, acue->action_tab, label);
	gtk_widget_show(acue->action_tab);

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
		gtk_button_set_label(GTK_BUTTON(gtk_builder_get_object(sac_builder, "acpCue")), button_text.c_str());
	}
	else
	{
		gtk_button_set_label(GTK_BUTTON(gtk_builder_get_object(sac_builder, "acpCue")), "Select Cue...");
	}

	// Update action option
	int32_t action = STACK_ACTION_CUE_STOP;
	stack_property_get_int32(stack_cue_get_property(cue, "action"), STACK_PROPERTY_VERSION_DEFINED, &action);
	switch (action)
	{
		case STACK_ACTION_CUE_PLAY:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(sac_builder, "acpActionTypePlay")), true);
			break;

		case STACK_ACTION_CUE_PAUSE:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(sac_builder, "acpActionTypePause")), true);
			break;

		case STACK_ACTION_CUE_STOP:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(sac_builder, "acpActionTypeStop")), true);
			break;
	}

	// Resume change callbacks on the properties
	stack_action_cue_pause_change_callbacks(cue, false);
}

/// Removes the properties tabs for a action cue
static void stack_action_cue_unset_tabs(StackCue *cue, GtkNotebook *notebook)
{
	// Find our media page
	gint page = gtk_notebook_page_num(notebook, STACK_ACTION_CUE(cue)->action_tab);

	// If we've found the page, remove it
	if (page >= 0)
	{
		gtk_notebook_remove_page(notebook, page);
	}

	// Remove our reference to the action tab
	g_object_unref(STACK_ACTION_CUE(cue)->action_tab);

	// Be tidy
	STACK_ACTION_CUE(cue)->action_tab = NULL;
}

/// Saves the details of this cue as JSON
static char *stack_action_cue_to_json(StackCue *cue)
{
	StackActionCue *acue = STACK_ACTION_CUE(cue);

	// Build JSON
	Json::Value cue_root;

	// We do nothing here as we only have properties
	stack_property_write_json(stack_cue_get_property(cue, "target"), &cue_root);
	stack_property_write_json(stack_cue_get_property(cue, "action"), &cue_root);

	// Write out JSON string and return (to be free'd by
	// stack_fade_cue_free_json)
	Json::FastWriter writer;
	return strdup(writer.write(cue_root).c_str());
}

/// Frees JSON strings as returned by stack_action_cue_to_json
static void stack_action_cue_free_json(StackCue *cue, char *json_data)
{
	free(json_data);
}

/// Re-initialises this cue from JSON Data
void stack_action_cue_from_json(StackCue *cue, const char *json_data)
{
	Json::Value cue_root;
	Json::Reader reader;

	// Parse JSON data
	reader.parse(json_data, json_data + strlen(json_data), cue_root, false);

	// Get the data that's pertinent to us
	if (!cue_root.isMember("StackActionCue"))
	{
		stack_log("stack_action_cue_from_json(): Missing StackActionCue class\n");
		return;
	}

	Json::Value& cue_data = cue_root["StackActionCue"];

	// Load in values from JSON
	stack_action_cue_set_target(STACK_ACTION_CUE(cue), stack_cue_get_by_uid(stack_cue_list_remap(STACK_CUE(cue)->parent, (cue_uid_t)cue_data["target"].asUInt64())));
	stack_action_cue_set_action(STACK_ACTION_CUE(cue), (StackActionCueAction)cue_data["action"].asInt());
}

/// Gets the error message for the cue
bool stack_action_cue_get_error(StackCue *cue, char *message, size_t size)
{
	// Get the target
	cue_uid_t target_uid = STACK_CUE_UID_NONE;
	stack_property_get_uint64(stack_cue_get_property(cue, "target"), STACK_PROPERTY_VERSION_DEFINED, &target_uid);

	if (target_uid == STACK_CUE_UID_NONE)
	{
		snprintf(message, size, "No target cue chosen");
		return true;
	}

	// Default condition: no error
	strncpy(message, "", size);
	return false;
}

const char *stack_action_cue_get_field(StackCue *cue, const char *field)
{
	if (strcmp(field, "action") == 0)
	{
		int32_t action = STACK_ACTION_CUE_STOP;
		stack_property_get_int32(stack_cue_get_property(cue, "action"), STACK_PROPERTY_VERSION_DEFINED, &action);

		switch (action)
		{
			case STACK_ACTION_CUE_PLAY:
				return "Play";
			case STACK_ACTION_CUE_PAUSE:
				return "Pause";
			case STACK_ACTION_CUE_STOP:
				return "Stop";
		}
	}
	else if (strcmp(field, "target") == 0)
	{
		cue_uid_t target_uid = STACK_CUE_UID_NONE;
		stack_property_get_uint64(stack_cue_get_property(cue, "target"), STACK_PROPERTY_VERSION_DEFINED, &target_uid);

		if (target_uid == STACK_CUE_UID_NONE)
		{
			return "<no target>";
		}

		StackCue *target_cue = stack_cue_list_get_cue_by_uid(cue->parent, target_uid);
		if (target_cue == NULL)
		{
			return "<invalid target>";
		}

		stack_cue_id_to_string(target_cue->id, STACK_ACTION_CUE(cue)->target_cue_id_string, 32);
		return STACK_ACTION_CUE(cue)->target_cue_id_string;
	}

	// Call the super class if we didn't return anything
	return stack_cue_get_field_base(cue, field);
}

/// Returns the icon for a cue
/// @param cue The cue to get the icon of
GdkPixbuf *stack_action_cue_get_icon(StackCue *cue)
{
	int32_t action = STACK_ACTION_CUE_STOP;
	stack_property_get_int32(stack_cue_get_property(cue, "action"), STACK_PROPERTY_VERSION_DEFINED, &action);

	switch (action)
	{
		case STACK_ACTION_CUE_PLAY:
			return icon_play;
		case STACK_ACTION_CUE_PAUSE:
			return icon_pause;
		case STACK_ACTION_CUE_STOP:
			return icon_stop;
	}

	return NULL;
}

////////////////////////////////////////////////////////////////////////////////
// CLASS REGISTRATION

// Registers StackActionCue with the application
void stack_action_cue_register()
{
	// Load the icons
	icon_pause = gdk_pixbuf_new_from_resource("/org/stack/icons/stackactioncue-pause.png", NULL);
	icon_play = gdk_pixbuf_new_from_resource("/org/stack/icons/stackactioncue-play.png", NULL);
	icon_stop = gdk_pixbuf_new_from_resource("/org/stack/icons/stackactioncue-stop.png", NULL);

	// Register built in cue types
	StackCueClass* action_cue_class = new StackCueClass{ "StackActionCue", "StackCue", "Action Cue", stack_action_cue_create, stack_action_cue_destroy, stack_action_cue_play, NULL, NULL, stack_action_cue_pulse, stack_action_cue_set_tabs, stack_action_cue_unset_tabs, stack_action_cue_to_json, stack_action_cue_free_json, stack_action_cue_from_json, stack_action_cue_get_error, NULL, NULL, stack_action_cue_get_field, stack_action_cue_get_icon, NULL, NULL };
	stack_register_cue_class(action_cue_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_action_cue_register();
	return true;
}

