// Includes:
#include "StackApp.h"
#include "StackGroupCue.h"
#include "StackLog.h"
#include <cstring>
#include <cstdlib>
#include <string>
#include <cmath>
#include <json/json.h>

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
	cue->builder = NULL;
	cue->group_tab = NULL;

	// Initialise superclass variables
	stack_cue_set_name(STACK_CUE(cue), "Group");

	return STACK_CUE(cue);
}

/// Destroys a action cue
static void stack_group_cue_destroy(StackCue *cue)
{
	// Tidy up
	if (STACK_GROUP_CUE(cue)->builder)
	{
		// Remove our reference to the action tab
		g_object_unref(STACK_GROUP_CUE(cue)->group_tab);

		// Destroy the top level widget in the builder
		gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(STACK_GROUP_CUE(cue)->builder, "window1")));

		// Unref the builder
		g_object_unref(STACK_GROUP_CUE(cue)->builder);
	}

	// Call parent destructor
	stack_cue_destroy_base(cue);
}

////////////////////////////////////////////////////////////////////////////////
// PROPERTY SETTERS

////////////////////////////////////////////////////////////////////////////////
// UI CALLBACKS

////////////////////////////////////////////////////////////////////////////////
// BASE CUE OPERATIONS

/// Start the cue playing
static bool stack_group_cue_play(StackCue *cue)
{
	// Call the superclass
	if (!stack_cue_play_base(cue))
	{
		return false;
	}

	return true;
}

/// Update the cue based on time
static void stack_group_cue_pulse(StackCue *cue, stack_time_t clocktime)
{
	// Get the cue state before the base class potentially updates it
	StackCueState pre_pulse_state = cue->state;

	// Call superclass
	stack_cue_pulse_base(cue, clocktime);
}

/// Sets up the tabs for the action cue
static void stack_group_cue_set_tabs(StackCue *cue, GtkNotebook *notebook)
{
	StackGroupCue *acue = STACK_GROUP_CUE(cue);

	// Create the tab
	GtkWidget *label = gtk_label_new("Group");

	// Load the UI
	GtkBuilder *builder = gtk_builder_new_from_resource("/org/stack/ui/StackGroupCue.ui");
	acue->builder = builder;
	acue->group_tab = GTK_WIDGET(gtk_builder_get_object(builder, "gcpGrid"));

	// Connect the signals
	gtk_builder_connect_signals(builder, (gpointer)cue);

	// Add an extra reference to the action tab - we're about to remove it's
	// parent and we don't want it to get garbage collected
	g_object_ref(acue->group_tab);

	// The tab has a parent window in the UI file - unparent the tab container from it
	gtk_widget_unparent(acue->group_tab);

	// Append the tab (and show it, because it starts off hidden...)
	gtk_notebook_append_page(notebook, acue->group_tab, label);
	gtk_widget_show(acue->group_tab);
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

	// We don't need the top level window, so destroy it (GtkBuilder doesn't
	// destroy top-level windows itself)
	gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(STACK_GROUP_CUE(cue)->builder, "window1")));

	// Destroy the builder
	g_object_unref(STACK_GROUP_CUE(cue)->builder);

	// Remove our reference to the action tab
	g_object_unref(STACK_GROUP_CUE(cue)->group_tab);

	// Be tidy
	STACK_GROUP_CUE(cue)->builder = NULL;
	STACK_GROUP_CUE(cue)->group_tab = NULL;
}

/// Saves the details of this cue as JSON
static char *stack_group_cue_to_json(StackCue *cue)
{
	StackGroupCue *acue = STACK_GROUP_CUE(cue);

	// Build JSON
	Json::Value cue_root;

	// Write out JSON string and return (to be free'd by
	// stack_fade_cue_free_json)
	Json::FastWriter writer;
	return strdup(writer.write(cue_root).c_str());
}

/// Frees JSON strings as returned by stack_group_cue_to_json
static void stack_group_cue_free_json(char *json_data)
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

////////////////////////////////////////////////////////////////////////////////
// CLASS REGISTRATION

// Registers StackGroupCue with the application
void stack_group_cue_register()
{
	// Register built in cue types
	StackCueClass* action_cue_class = new StackCueClass{ "StackGroupCue", "StackCue", stack_group_cue_create, stack_group_cue_destroy, stack_group_cue_play, NULL, NULL, stack_group_cue_pulse, stack_group_cue_set_tabs, stack_group_cue_unset_tabs, stack_group_cue_to_json, stack_group_cue_free_json, stack_group_cue_from_json, stack_group_cue_get_error, NULL, NULL, NULL, stack_group_cue_get_icon };
	stack_register_cue_class(action_cue_class);
}
