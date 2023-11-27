// Includes:
#include "StackApp.h"
#include "StackLog.h"
#include "StackKeyTrigger.h"
#include <json/json.h>

////////////////////////////////////////////////////////////////////////////////
// TRIGGER ACTION

gboolean stack_key_trigger_run_action(GtkWidget* self, GdkEventKey *event, gpointer user_data)
{
	StackKeyTrigger *trigger = STACK_KEY_TRIGGER(user_data);

	// If the right key was pressed/released
	if (event->type == trigger->event_type && event->keyval == trigger->keyval)
	{
		// Get the cue and the action
		StackCue *cue = STACK_TRIGGER(trigger)->cue;
		StackTriggerAction action = stack_trigger_get_action(STACK_TRIGGER(trigger));

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
	}

	// Always allow other handlers to trigger
	return false;
}

// Removes any existing signal handler
void stack_key_trigger_remove_handler(StackKeyTrigger *trigger)
{
	// Remove any signal handler we set
	if (trigger->handler_id != 0 && trigger->sclw != NULL)
	{
		g_signal_handler_disconnect(trigger->sclw, trigger->handler_id);
		stack_log("stack_key_trigger_remove_handler(): Handler removed\n");
		trigger->sclw = NULL;
		trigger->handler_id = 0;
	}
}

// Sets up the handler, removing any existing handler in the process
void stack_key_trigger_set_handler(StackKeyTrigger *trigger, guint event_type, guint keyval)
{
	// Validate the parameters
	if (trigger == NULL || (event_type != GDK_KEY_PRESS && event_type != GDK_KEY_RELEASE) || keyval == 0)
	{
		return;
	}

	// Get the application windows
	GtkApplication *gtk_app = GTK_APPLICATION(g_application_get_default());
	if (gtk_app == NULL)
	{
		stack_log("stack_key_trigger_set_handler(): Failed to get application instance\n");
	}
	GList *windows = gtk_application_get_windows(gtk_app);
	if (windows == NULL)
	{
		stack_log("stack_key_trigger_set_handler(): Failed to enumerate Stack windows\n");
	}

	StackCueList *parent = STACK_TRIGGER(trigger)->cue->parent;

	// Find the window running the cue list for this cue
	while (windows != NULL)
	{
		StackAppWindow *window = (StackAppWindow*)windows->data;
		if (window != NULL && (window->cue_list == parent || window->loading_cue_list == parent))
		{
			// Remove any existing handlers we set
			stack_key_trigger_remove_handler(trigger);

			// Store our details
			trigger->sclw = window->sclw;
			trigger->event_type = event_type;
			trigger->keyval = keyval;

			// Set up the handler
			if (event_type == GDK_KEY_PRESS)
			{
				trigger->handler_id = g_signal_connect(window->sclw, "key-press-event", G_CALLBACK(stack_key_trigger_run_action), (gpointer)trigger);
				stack_log("stack_key_trigger_set_handler(): Key press signal attached, handler %d\n", trigger->handler_id);
			}
			else if (event_type == GDK_KEY_RELEASE)
			{
				trigger->handler_id = g_signal_connect(window->sclw, "key-release-event", G_CALLBACK(stack_key_trigger_run_action), (gpointer)trigger);
				stack_log("stack_key_trigger_set_handler(): Key release signal attached, handler %d\n", trigger->handler_id);
			}
			else
			{
				stack_log("stack_key_trigger_set_handler(): Unknown event type\n");
			}

			break;
		}

		// Iterate
		windows = windows->next;
	}
}

////////////////////////////////////////////////////////////////////////////////
// CREATION AND DESTRUCTION

/// Creates a key trigger
StackTrigger* stack_key_trigger_create(StackCue *cue)
{
	// Allocate the trigger
	StackKeyTrigger *trigger = new StackKeyTrigger();

	// Initialise the superclass
	stack_trigger_init(&trigger->super, cue);

	// Make this class a StackKeyTrigger
	STACK_TRIGGER(trigger)->_class_name = "StackKeyTrigger";

	// Initial setup
	trigger->handler_id = 0;
	trigger->sclw = NULL;
	trigger->keyval = 0;
	trigger->event_type = GDK_KEY_RELEASE;
	trigger->description = strdup("");

	return STACK_TRIGGER(trigger);
}

/// Destroys a key trigger
void stack_key_trigger_destroy(StackTrigger *trigger)
{
	// Remove the handler
	stack_key_trigger_remove_handler(STACK_KEY_TRIGGER(trigger));

	if (STACK_KEY_TRIGGER(trigger)->description != NULL)
	{
		free(STACK_KEY_TRIGGER(trigger)->description);
	}

	// Call parent destructor
	stack_trigger_destroy_base(trigger);
}

////////////////////////////////////////////////////////////////////////////////
// OVERRIDDEN FUNCTIONS

// Return either "Key Pressed" or "Key Released" as the text depending on what
// the trigger is configured for
const char* stack_key_trigger_get_name(StackTrigger *trigger)
{
	if (STACK_KEY_TRIGGER(trigger)->event_type == GDK_KEY_PRESS)
	{
		return "Key Pressed";
	}
	else if (STACK_KEY_TRIGGER(trigger)->event_type == GDK_KEY_RELEASE)
	{
		return "Key Released";
	}

	return "Key Action";
}

// Returns the name of the key we're triggered off
const char* stack_key_trigger_get_event_text(StackTrigger *trigger)
{
	if (STACK_KEY_TRIGGER(trigger)->keyval == 0)
	{
		return "<not set>";
	}
	else
	{
		return gdk_keyval_name(STACK_KEY_TRIGGER(trigger)->keyval);
	}
}

// Returns the user-specified description
const char* stack_key_trigger_get_description(StackTrigger *trigger)
{
	return STACK_KEY_TRIGGER(trigger)->description;
}

char *stack_key_trigger_to_json(StackTrigger *trigger)
{
	Json::Value trigger_root;

	trigger_root["description"] = STACK_KEY_TRIGGER(trigger)->description;
	trigger_root["keyval"] = STACK_KEY_TRIGGER(trigger)->keyval;
	trigger_root["event_type"] = STACK_KEY_TRIGGER(trigger)->event_type;

	Json::FastWriter writer;
	return strdup(writer.write(trigger_root).c_str());
}

void stack_key_trigger_free_json(StackTrigger *trigger, char *json_data)
{
	free(json_data);
}

void stack_key_trigger_from_json(StackTrigger *trigger, const char *json_data)
{
	Json::Value trigger_root;
	Json::Reader reader;

	// Call the superclass version
	stack_trigger_from_json_base(trigger, json_data);

	// Parse JSON data
	reader.parse(json_data, json_data + strlen(json_data), trigger_root, false);

	// Get the data that's pertinent to us
	Json::Value& stack_trigger_data = trigger_root["StackKeyTrigger"];

	StackKeyTrigger *key_trigger = STACK_KEY_TRIGGER(trigger);
	if (key_trigger->description != NULL)
	{
		free(key_trigger->description);
	}
	key_trigger->description = strdup(stack_trigger_data["description"].asString().c_str());
	key_trigger->keyval = stack_trigger_data["keyval"].asUInt64();
	key_trigger->event_type = stack_trigger_data["event_type"].asUInt64();

	// Set up the handler
	stack_key_trigger_set_handler(key_trigger, key_trigger->event_type, key_trigger->keyval);
}
////////////////////////////////////////////////////////////////////////////////
// CONFIGURATION USER INTERFACE

void stack_key_trigger_set_button_label(StackKeyTrigger *trigger, GtkButton *button)
{
	if (trigger->keyval == 0)
	{
		gtk_button_set_label(button, "<not set>");
	}
	else
	{
		gtk_button_set_label(button, gdk_keyval_name(trigger->keyval));
	}
}

gboolean ktd_key_keypress(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	StackKeyTrigger *trigger = STACK_KEY_TRIGGER(user_data);
	if (trigger->grabbed)
	{
		// Disallow Escape and Space as we use those specifically for other things
		if (event->keyval != GDK_KEY_Escape && event->keyval != GDK_KEY_space)
		{
			trigger->keyval = event->keyval;
			gdk_seat_ungrab(gdk_display_get_default_seat(gdk_display_get_default()));
			trigger->grabbed = false;
			stack_key_trigger_set_button_label(trigger, GTK_BUTTON(widget));
		}

		// If the user hits escape, just ungrab
		if (event->keyval == GDK_KEY_Escape)
		{
			stack_key_trigger_set_button_label(trigger, GTK_BUTTON(widget));
			gdk_seat_ungrab(gdk_display_get_default_seat(gdk_display_get_default()));
			trigger->grabbed = false;
		}

		// Don't let the normal handler run
		return true;
	}

	return false;
}

void ktd_key_button_clicked(GtkButton *button, gpointer user_data)
{
	StackKeyTrigger *trigger = STACK_KEY_TRIGGER(user_data);
	GdkSeat* seat = gdk_display_get_default_seat(gdk_display_get_default());

	if (trigger->grabbed)
	{
		gdk_seat_ungrab(seat);
		stack_key_trigger_set_button_label(trigger, button);
		trigger->grabbed = false;
	}
	else
	{
		GdkGrabStatus status = gdk_seat_grab(seat, gtk_widget_get_window(GTK_WIDGET(button)), GDK_SEAT_CAPABILITY_KEYBOARD, true, NULL, NULL, NULL, NULL);

		if (status == GDK_GRAB_SUCCESS)
		{
			trigger->grabbed = true;
			gtk_button_set_label(button, "Press a key...");
		}
	}
}

bool stack_key_trigger_show_config_ui(StackTrigger *trigger, GtkWidget *parent, bool new_trigger)
{
	bool result = false;
	StackKeyTrigger *key_trigger = STACK_KEY_TRIGGER(trigger);

	// Build the dialog
	GtkBuilder *builder = gtk_builder_new_from_resource("/org/stack/ui/StackKeyTrigger.ui");
	GtkDialog *dialog = GTK_DIALOG(gtk_builder_get_object(builder, "keyTriggerDialog"));
	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));

	// Callbacks
	gtk_builder_add_callback_symbol(builder, "ktd_key_button_clicked", G_CALLBACK(ktd_key_button_clicked));
	gtk_builder_add_callback_symbol(builder, "ktd_key_keypress", G_CALLBACK(ktd_key_keypress));
	gtk_builder_connect_signals(builder, trigger);

	// Set up response buttons
	gtk_dialog_add_buttons(dialog, "Cancel", 2, "OK", 1, NULL);
	gtk_dialog_set_default_response(dialog, 1);

	// We use these a lot
	GtkToggleButton *ktdEventPress = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "ktdEventPress"));
	GtkToggleButton *ktdEventRelease = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "ktdEventRelease"));
	GtkToggleButton *ktdActionStop = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "ktdActionStop"));
	GtkToggleButton *ktdActionPause = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "ktdActionPause"));
	GtkToggleButton *ktdActionPlay = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "ktdActionPlay"));
	GtkEntry *ktdDescriptionEntry = GTK_ENTRY(gtk_builder_get_object(builder, "ktdDescriptionEntry"));

	// Reset this just in case the UI cleared
	key_trigger->grabbed = false;

	// Set the values on the dialog
	gtk_entry_set_text(ktdDescriptionEntry, key_trigger->description);
	stack_key_trigger_set_button_label(key_trigger, GTK_BUTTON(gtk_builder_get_object(builder, "ktdKeyButton")));
	switch (key_trigger->event_type)
	{
		case GDK_KEY_PRESS:
			gtk_toggle_button_set_active(ktdEventPress, true);
			break;
		default:
		case GDK_KEY_RELEASE:
			gtk_toggle_button_set_active(ktdEventRelease, true);
			break;
	}
	switch (trigger->action)
	{
		case STACK_TRIGGER_ACTION_STOP:
			gtk_toggle_button_set_active(ktdActionStop, true);
			break;
		case STACK_TRIGGER_ACTION_PAUSE:
			gtk_toggle_button_set_active(ktdActionPause, true);
			break;
		default:
		case STACK_TRIGGER_ACTION_PLAY:
			gtk_toggle_button_set_active(ktdActionPlay, true);
			break;
	}

	// Run the dialog
	guint old_keyval = key_trigger->keyval;
	gint response = gtk_dialog_run(dialog);

	switch (response)
	{
		case 1:	// OK
			// Store the action
			if (gtk_toggle_button_get_active(ktdActionStop))
			{
				trigger->action = STACK_TRIGGER_ACTION_STOP;
			}
			else if (gtk_toggle_button_get_active(ktdActionPause))
			{
				trigger->action = STACK_TRIGGER_ACTION_PAUSE;
			}
			else if (gtk_toggle_button_get_active(ktdActionPlay))
			{
				trigger->action = STACK_TRIGGER_ACTION_PLAY;
			}

			// Store the event
			if (gtk_toggle_button_get_active(ktdEventPress))
			{
				key_trigger->event_type = GDK_KEY_PRESS;
			}
			else if (gtk_toggle_button_get_active(ktdEventRelease))
			{
				key_trigger->event_type = GDK_KEY_RELEASE;
			}

			// Update the handler
			stack_key_trigger_set_handler(key_trigger, key_trigger->event_type, key_trigger->keyval);

			// Update the description
			if (key_trigger->description != NULL)
			{
				free(key_trigger->description);
				key_trigger->description = strdup(gtk_entry_get_text(ktdDescriptionEntry));
			}

			result = true;
			break;
		case 2: // Cancel
			// Reset the trigger key
			key_trigger->keyval = old_keyval;
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

// Registers StackKeyTrigger with the application
void stack_key_trigger_register()
{
	// Register built in cue types
	StackTriggerClass* key_trigger_class = new StackTriggerClass{
		"StackKeyTrigger",
		"StackTrigger",
		stack_key_trigger_create,
		stack_key_trigger_destroy,
		stack_key_trigger_get_name,
		stack_key_trigger_get_event_text,
		stack_key_trigger_get_description,
		NULL, //get_action
		stack_key_trigger_to_json,
		stack_key_trigger_free_json,
		stack_key_trigger_from_json,
		stack_key_trigger_show_config_ui
	};
	stack_register_trigger_class(key_trigger_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_key_trigger_register();
	return true;
}
