// Includes:
#include "StackApp.h"
#include "StackActionCue.h"
#include <cstring>
#include <cstdlib>
#include <string>
#include <math.h>
#include <json/json.h>

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
	cue->target = STACK_CUE_UID_NONE;
	cue->action = STACK_ACTION_CUE_STOP;
	cue->builder = NULL;
	cue->action_tab = NULL;
	stack_cue_set_action_time(STACK_CUE(cue), 1);

	return STACK_CUE(cue);
}

/// Destroys a action cue
static void stack_action_cue_destroy(StackCue *cue)
{
	// Tidy up
	if (STACK_ACTION_CUE(cue)->builder)
	{
		// Remove our reference to the action tab
		g_object_ref(STACK_ACTION_CUE(cue)->action_tab);

		// Destroy the top level widget in the builder
		gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(STACK_ACTION_CUE(cue)->builder, "window1")));
		
		// Unref the builder
		g_object_unref(STACK_ACTION_CUE(cue)->builder);
	}
	
	// Call parent destructor
	stack_cue_destroy_base(cue);
}

////////////////////////////////////////////////////////////////////////////////
// PROPERTY SETTERS

// Sets the target cue of an action cue
void stack_action_cue_set_target(StackActionCue *cue, StackCue *target)
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
}

// Sets the performed action of an action cue
void stack_action_cue_set_action(StackActionCue *cue, StackActionCueAction action)
{
	cue->action = action;
}

////////////////////////////////////////////////////////////////////////////////
// UI CALLBACKS

/// Called when the Target Cue button is pressed
static void acp_cue_changed(GtkButton *widget, gpointer user_data)
{
	// Get the cue
	StackActionCue *cue = STACK_ACTION_CUE(user_data);
	
	// Get the parent window
	StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->action_tab));

	// Call the dialog and get the new target
	StackCue *new_target = stack_select_cue_dialog(window, stack_cue_get_by_uid(cue->target));

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
		button_text = std::string(cue_number) + ": " + std::string(new_target->name);
		
		// Update the UI
		gtk_button_set_label(widget, button_text.c_str());
	}
		
	// Fire an updated-selected-cue signal to signal the UI to change (we might
	// have changed state)
	g_signal_emit_by_name((gpointer)window, "update-selected-cue");
}

/// Called when the action changes
static void acp_action_changed(GtkToggleButton *widget, gpointer user_data)
{
	StackActionCue *cue = STACK_ACTION_CUE(user_data);
	
	// Get pointers to the four radio button options
	GtkToggleButton* r1 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(cue->builder, "acpActionTypePlay"));
	GtkToggleButton* r2 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(cue->builder, "acpActionTypePause"));
	GtkToggleButton* r3 = GTK_TOGGLE_BUTTON(gtk_builder_get_object(cue->builder, "acpActionTypeStop"));
	
	// Determine which one is toggled on
	if (widget == r1 && gtk_toggle_button_get_active(r1)) { stack_action_cue_set_action(cue, STACK_ACTION_CUE_PLAY); }
	if (widget == r2 && gtk_toggle_button_get_active(r2)) { stack_action_cue_set_action(cue, STACK_ACTION_CUE_PAUSE); }
	if (widget == r3 && gtk_toggle_button_get_active(r3)) { stack_action_cue_set_action(cue, STACK_ACTION_CUE_STOP); }
	
	// No need to update the UI on this one (currently)
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
	
	// If we don't have a valid target (unknown cue, or self) then we can't play
	if (stack_cue_get_by_uid(STACK_ACTION_CUE(cue)->target) == NULL || STACK_ACTION_CUE(cue)->target == cue->uid)
	{
		fprintf(stderr, "stack_action_cue_play(): Invalid target cue: %llx\n", STACK_ACTION_CUE(cue)->target);
		stack_cue_set_state(cue, STACK_CUE_STATE_ERROR);
	}

	return true;
}

/// Update the cue based on time
static void stack_action_cue_pulse(StackCue *cue, stack_time_t clocktime)
{
	// Get the cue state before the base class potentially updates it
	StackCueState pre_pulse_state = cue->state;

	// Call superclass
	stack_cue_pulse_base(cue, clocktime);

	// Get the target
	StackCue *target = stack_cue_get_by_uid(STACK_ACTION_CUE(cue)->target);

	// If the target is valid	
	if (target != NULL)
	{
		// We have zero action time so we need to detect the transition from pre->something or playing->something
		if ((pre_pulse_state == STACK_CUE_STATE_PLAYING_PRE && cue->state != STACK_CUE_STATE_PLAYING_PRE) ||
			(pre_pulse_state == STACK_CUE_STATE_PLAYING_ACTION && cue->state != STACK_CUE_STATE_PLAYING_ACTION))
		{
			fprintf(stderr, "stack_action_cue_pulse(): Triggering action\n");

			switch (STACK_ACTION_CUE(cue)->action)
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
	
	// Load the UI
	GtkBuilder *builder = gtk_builder_new_from_file("StackActionCue.ui");
	acue->builder = builder;
	acue->action_tab = GTK_WIDGET(gtk_builder_get_object(builder, "acpGrid"));

	// Set up callbacks
	gtk_builder_add_callback_symbol(builder, "acp_cue_changed", G_CALLBACK(acp_cue_changed));
	gtk_builder_add_callback_symbol(builder, "acp_action_changed", G_CALLBACK(acp_action_changed));
	
	// Connect the signals
	gtk_builder_connect_signals(builder, (gpointer)cue);

	// Add an extra reference to the action tab - we're about to remove it's
	// parent and we don't want it to get garbage collected
	g_object_ref(acue->action_tab);

	// The tab has a parent window in the UI file - unparent the tab container from it
	gtk_widget_unparent(acue->action_tab);
	
	// Append the tab (and show it, because it starts off hidden...)
	gtk_notebook_append_page(notebook, acue->action_tab, label);
	gtk_widget_show(acue->action_tab);
	
	// Set the values: cue
	StackCue *target_cue = stack_cue_get_by_uid(acue->target);
	if (target_cue)
	{
		char cue_number[32];
		stack_cue_id_to_string(target_cue->id, cue_number, 32);

		// Build the string
		std::string button_text;
		button_text = std::string(cue_number) + ": " + std::string(target_cue->name);
	
		// Set the button text
		gtk_button_set_label(GTK_BUTTON(gtk_builder_get_object(builder, "acpCue")), button_text.c_str());
	}
	
	// Update action option
	switch (acue->action)
	{
		case STACK_ACTION_CUE_PLAY:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "acpActionTypePlay")), true);
			break;

		case STACK_ACTION_CUE_PAUSE:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "acpActionTypePause")), true);
			break;

		case STACK_ACTION_CUE_STOP:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "acpActionTypeStop")), true);
			break;
	}
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
	
	// We don't need the top level window, so destroy it (GtkBuilder doesn't
	// destroy top-level windows itself)
	gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(STACK_ACTION_CUE(cue)->builder, "window1")));

	// Destroy the builder
	g_object_unref(STACK_ACTION_CUE(cue)->builder);

	// Remove our reference to the action tab
	g_object_ref(STACK_ACTION_CUE(cue)->action_tab);

	// Be tidy	
	STACK_ACTION_CUE(cue)->builder = NULL;
	STACK_ACTION_CUE(cue)->action_tab = NULL;
}

/// Saves the details of this cue as JSON
static char *stack_action_cue_to_json(StackCue *cue)
{
	StackActionCue *acue = STACK_ACTION_CUE(cue);

	// Build JSON
	Json::Value cue_root;
	cue_root["target"] = (Json::UInt64)acue->target;
	cue_root["action"] = acue->action;
	
	// Write out JSON string and return (to be free'd by 
	// stack_fade_cue_free_json)
	Json::FastWriter writer;
	return strdup(writer.write(cue_root).c_str());
}

/// Frees JSON strings as returned by stack_action_cue_to_json
static void stack_action_cue_free_json(char *json_data)
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
		fprintf(stderr, "stack_action_cue_from_json(): Missing StackActionCue class\n");
		return;
	}
	
	Json::Value& cue_data = cue_root["StackActionCue"];

	// Load in values from JSON
	stack_action_cue_set_target(STACK_ACTION_CUE(cue), stack_cue_get_by_uid(stack_cue_list_remap(STACK_CUE(cue)->parent, (cue_uid_t)cue_data["target"].asUInt64())));
	stack_action_cue_set_action(STACK_ACTION_CUE(cue), (StackActionCueAction)cue_data["action"].asInt());
}

////////////////////////////////////////////////////////////////////////////////
// CLASS REGISTRATION

// Registers StackActionCue with the application
void stack_action_cue_register()
{
	// Register built in cue types
	StackCueClass* action_cue_class = new StackCueClass{ "StackActionCue", "StackCue", stack_action_cue_create, stack_action_cue_destroy, stack_action_cue_play, NULL, NULL, stack_action_cue_pulse, stack_action_cue_set_tabs, stack_action_cue_unset_tabs, stack_action_cue_to_json, stack_action_cue_free_json, stack_action_cue_from_json };
	stack_register_cue_class(action_cue_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_initialise_plugin()
{
	stack_action_cue_register();
	return true;
}

