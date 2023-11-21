// Includes:
#include "StackApp.h"
#include "StackLog.h"
#include "StackExecCue.h"
#include <cstring>
#include <cstdlib>
#include <string>
#include <cmath>
#include <json/json.h>
#include <sys/wait.h>

// Global: A single instance of our builder so we don't have to keep reloading
// it every time we change the selected cue
static GtkBuilder *sac_builder = NULL;

// Global: A single instace of our icon
static GdkPixbuf *icon = NULL;

static void stack_exec_cue_ccb_command(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackExecCue* cue = STACK_EXEC_CUE(user_data);

		// Notify cue list that we've changed
		stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue), property);

		// Fire an updated-selected-cue signal to signal the UI to change (we might
		// have changed state)
		if (cue->exec_tab)
		{
			StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->exec_tab));
			g_signal_emit_by_name((gpointer)window, "update-selected-cue");
		}
	}
}

/// Pause or resumes change callbacks on variables
static void stack_exec_cue_pause_change_callbacks(StackCue *cue, bool pause)
{
	stack_property_pause_change_callback(stack_cue_get_property(cue, "command"), pause);
}

////////////////////////////////////////////////////////////////////////////////
// CREATION AND DESTRUCTION

/// Creates a action cue
static StackCue* stack_exec_cue_create(StackCueList *cue_list)
{
	// Allocate the cue
	StackExecCue* cue = new StackExecCue();

	// Initialise the superclass
	stack_cue_init(&cue->super, cue_list);

	// Make this class a StackExecCue
	cue->super._class_name = "StackExecCue";

	// We start in error state until we have a target
	stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_ERROR);

	// Initialise our variables
	cue->exec_tab = NULL;
	stack_cue_set_action_time(STACK_CUE(cue), 1);

	// Add our properties
	StackProperty *command = stack_property_create("command", STACK_PROPERTY_TYPE_STRING);
	stack_cue_add_property(STACK_CUE(cue), command);
	stack_property_set_changed_callback(command, stack_exec_cue_ccb_command, (void*)cue);

	// Initialise superclass variables
	stack_cue_set_name(STACK_CUE(cue), "Execute command");

	return STACK_CUE(cue);
}

/// Destroys a action cue
static void stack_exec_cue_destroy(StackCue *cue)
{
	// Call parent destructor
	stack_cue_destroy_base(cue);
}

////////////////////////////////////////////////////////////////////////////////
// PROPERTY SETTERS

// Sets the performed action of an action cue
void stack_exec_cue_set_command(StackExecCue *cue, const char *command)
{
	stack_property_set_string(stack_cue_get_property(STACK_CUE(cue), "command"), STACK_PROPERTY_VERSION_DEFINED, command);

	if (strlen(command) == 0)
	{
		stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_ERROR);
	}
	else
	{
		if (STACK_CUE(cue)->state == STACK_CUE_STATE_ERROR)
		{
			stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_STOPPED);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
// UI CALLBACKS

/// Called when the Command is changed
static gboolean ecp_command_changed(GtkButton *widget, GdkEvent *event, gpointer user_data)
{
	// Get the cue
	StackExecCue *cue = STACK_EXEC_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);

	// Get the parent window
	StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->exec_tab));

	stack_exec_cue_set_command(cue, gtk_entry_get_text(GTK_ENTRY(widget)));

	return false;
}

////////////////////////////////////////////////////////////////////////////////
// BASE CUE OPERATIONS

/// Start the cue playing
static bool stack_exec_cue_play(StackCue *cue)
{
	// Call the superclass
	if (!stack_cue_play_base(cue))
	{
		return false;
	}

	// Copy the variables to live
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "command"));

	// Get the command
	char *command = NULL;
	stack_property_get_string(stack_cue_get_property(cue, "command"), STACK_PROPERTY_VERSION_LIVE, &command);

	// If we don't have a valid cmmand then we can't play
	if (command == NULL || strlen(command) == 0)
	{
		stack_log("ERROR: command is '%s'\n", command);
		stack_cue_set_state(cue, STACK_CUE_STATE_ERROR);
		return false;
	}

	return true;
}

/// Update the cue based on time
static void stack_exec_cue_pulse(StackCue *cue, stack_time_t clocktime)
{
	// Get the cue state before the base class potentially updates it
	StackCueState pre_pulse_state = cue->state;

	// Call superclass
	stack_cue_pulse_base(cue, clocktime);

	// Get the target cue UID
	char *command = NULL;
	stack_property_get_string(stack_cue_get_property(cue, "command"), STACK_PROPERTY_VERSION_LIVE, &command);

	// We have zero action time so we need to detect the transition from pre->something or playing->something
	if ((pre_pulse_state == STACK_CUE_STATE_PLAYING_PRE && cue->state != STACK_CUE_STATE_PLAYING_PRE) ||
		(pre_pulse_state == STACK_CUE_STATE_PLAYING_ACTION && cue->state != STACK_CUE_STATE_PLAYING_ACTION))
	{
		// Build our args annoyingly as non-const char arrays
		char bin_sh[8];
		char dash_c[3];
		strcpy(bin_sh, "/bin/sh");
		strcpy(dash_c, "-c");
		char *argv[4] = {bin_sh, dash_c, command, NULL};

		// Spawn asynchronously
		g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
	}
}

/// Sets up the tabs for the action cue
static void stack_exec_cue_set_tabs(StackCue *cue, GtkNotebook *notebook)
{
	StackExecCue *acue = STACK_EXEC_CUE(cue);

	// Create the tab
	GtkWidget *label = gtk_label_new("Execute");

	// Load the UI (if we haven't already)
	if (sac_builder == NULL)
	{
		sac_builder = gtk_builder_new_from_resource("/org/stack/ui/StackExecCue.ui");

		// Set up callbacks
		gtk_builder_add_callback_symbol(sac_builder, "ecp_command_changed", G_CALLBACK(ecp_command_changed));

		// Connect the signals
		gtk_builder_connect_signals(sac_builder, NULL);
	}
	acue->exec_tab = GTK_WIDGET(gtk_builder_get_object(sac_builder, "ecpGrid"));

	// Pause change callbacks on the properties
	stack_exec_cue_pause_change_callbacks(cue, true);

	// Add an extra reference to the action tab - we're about to remove it's
	// parent and we don't want it to get garbage collected
	g_object_ref(acue->exec_tab);

	// The tab has a parent window in the UI file - unparent the tab container from it
	gtk_widget_unparent(acue->exec_tab);

	// Append the tab (and show it, because it starts off hidden...)
	gtk_notebook_append_page(notebook, acue->exec_tab, label);
	gtk_widget_show(acue->exec_tab);

	// Set the values: cue
	char *command = NULL;
	stack_property_get_string(stack_cue_get_property(cue, "command"), STACK_PROPERTY_VERSION_DEFINED, &command);

	// Set the button text
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(sac_builder, "ecpCommand")), command != NULL ? command : "");

	// Resume change callbacks on the properties
	stack_exec_cue_pause_change_callbacks(cue, false);
}

/// Removes the properties tabs for a action cue
static void stack_exec_cue_unset_tabs(StackCue *cue, GtkNotebook *notebook)
{
	// Find our media page
	gint page = gtk_notebook_page_num(notebook, STACK_EXEC_CUE(cue)->exec_tab);

	// If we've found the page, remove it
	if (page >= 0)
	{
		gtk_notebook_remove_page(notebook, page);
	}

	// Remove our reference to the action tab
	g_object_unref(STACK_EXEC_CUE(cue)->exec_tab);

	// Be tidy
	STACK_EXEC_CUE(cue)->exec_tab = NULL;
}

/// Saves the details of this cue as JSON
static char *stack_exec_cue_to_json(StackCue *cue)
{
	StackExecCue *acue = STACK_EXEC_CUE(cue);

	// Build JSON
	Json::Value cue_root;

	// We do nothing here as we only have properties
	stack_property_write_json(stack_cue_get_property(cue, "command"), &cue_root);

	// Write out JSON string and return (to be free'd by
	// stack_fade_cue_free_json)
	Json::FastWriter writer;
	return strdup(writer.write(cue_root).c_str());
}

/// Frees JSON strings as returned by stack_exec_cue_to_json
static void stack_exec_cue_free_json(char *json_data)
{
	free(json_data);
}

/// Re-initialises this cue from JSON Data
void stack_exec_cue_from_json(StackCue *cue, const char *json_data)
{
	Json::Value cue_root;
	Json::Reader reader;

	// Parse JSON data
	reader.parse(json_data, json_data + strlen(json_data), cue_root, false);

	// Get the data that's pertinent to us
	if (!cue_root.isMember("StackExecCue"))
	{
		stack_log("stack_exec_cue_from_json(): Missing StackExecCue class\n");
		return;
	}

	Json::Value& cue_data = cue_root["StackExecCue"];

	// Load in values from JSON
	stack_exec_cue_set_command(STACK_EXEC_CUE(cue), cue_data["command"].asString().c_str());
}

/// Gets the error message for the cue
void stack_exec_cue_get_error(StackCue *cue, char *message, size_t size)
{
	// Get the target
	char *command = NULL;
	stack_property_get_string(stack_cue_get_property(cue, "command"), STACK_PROPERTY_VERSION_DEFINED, &command);

	if (command == NULL || strlen(command) == 0)
	{
		snprintf(message, size, "No command chosen");
	}
	else
	{
		strncpy(message, "", size);
	}
}

/// Returns the icon for a cue
/// @param cue The cue to get the icon of
GdkPixbuf *stack_audio_cue_get_icon(StackCue *cue)
{
	return icon;
}

////////////////////////////////////////////////////////////////////////////////
// CLASS REGISTRATION

// Registers StackExecCue with the application
void stack_exec_cue_register()
{
	// Load the icons
	icon = gdk_pixbuf_new_from_resource("/org/stack/icons/stackexeccue.png", NULL);

	// Register built in cue types
	StackCueClass* exec_cue_class = new StackCueClass{ "StackExecCue", "StackCue", stack_exec_cue_create, stack_exec_cue_destroy, stack_exec_cue_play, NULL, NULL, stack_exec_cue_pulse, stack_exec_cue_set_tabs, stack_exec_cue_unset_tabs, stack_exec_cue_to_json, stack_exec_cue_free_json, stack_exec_cue_from_json, stack_exec_cue_get_error, NULL, NULL, NULL, stack_audio_cue_get_icon };
	stack_register_cue_class(exec_cue_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_exec_cue_register();
	return true;
}
