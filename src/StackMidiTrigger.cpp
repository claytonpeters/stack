// Includes:
#include "StackApp.h"
#include "StackLog.h"
#include "StackMidiTrigger.h"
#include "StackGtkHelper.h"
#include "StackJson.h"

static GtkBuilder *mtd_builder = NULL;

////////////////////////////////////////////////////////////////////////////////
// HELPER FUNCTIONS

// Wrapper to show a generic error message dialog
void stack_midi_trigger_show_error(GtkDialog *parent, const char *title, const char *message)
{
	GtkWidget *message_dialog = gtk_message_dialog_new(GTK_WINDOW(parent), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", title);
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(message_dialog), "%s", message);
	gtk_window_set_title(GTK_WINDOW(message_dialog), "Error");
	gtk_dialog_run(GTK_DIALOG(message_dialog));
	gtk_widget_destroy(message_dialog);
}

// Determines whether the event type is valid for the trigger
bool stack_midi_trigger_is_event_type_valid(uint8_t event_type)
{
	switch (event_type)
	{
		case STACK_MIDI_EVENT_NOTE_OFF:
		case STACK_MIDI_EVENT_NOTE_ON:
		case STACK_MIDI_EVENT_NOTE_AFTERTOUCH:
		case STACK_MIDI_EVENT_CONTROLLER:
		case STACK_MIDI_EVENT_PROGRAM_CHANGE:
		case STACK_MIDI_EVENT_CHANNEL_AFTERTOUCH:
		case STACK_MIDI_EVENT_PITCH_BEND:
			return true;
			break;
		default:
			return false;
			break;
	}
}

////////////////////////////////////////////////////////////////////////////////
// ACTION FUNCTIONS

static void stack_midi_trigger_thread(void *user_data)
{
	stack_log("stack_midi_trigger_thread(0x%016llx): Thread started\n", user_data);
	StackTrigger *trigger = STACK_TRIGGER(user_data);
	StackMidiTrigger *midi_trigger = STACK_MIDI_TRIGGER(user_data);
	StackCueList *cue_list = trigger->cue->parent;

	midi_trigger->thread_running = true;

	while (midi_trigger->thread_running)
	{
		// Don't attempt to create a receiver if the trigger isn't configured yet
		if (!(midi_trigger->flags & STACK_MIDI_TRIGGER_CONFIGURED))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			continue;
		}

		// Try to create a receiver if we don't have one
		if (midi_trigger->receiver == NULL)
		{
			StackMidiDevice *device = stack_cue_list_get_midi_device(cue_list, midi_trigger->midi_patch);
			if (device == NULL)
			{
				// The patch doesn't exist, sleep for a bit and try again
				stack_log("stack_midi_trigger_thread(0x%016llx): Unknown MIDI patch: %s\n", midi_trigger, midi_trigger->midi_patch);
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				continue;
			}
			midi_trigger->receiver = stack_midi_device_add_receiver(device);
			stack_log("stack_midi_trigger_thread(0x%016llx): Receiver opened\n", user_data);
		}

		// Get the next event. If this returns false, then the receiver is being
		// removed and we should go back to trying to create a receiver
		StackMidiEvent *event = NULL;
		if (!stack_midi_device_get_event(midi_trigger->receiver, &event))
		{
			stack_log("stack_midi_trigger_thread(0x%016llx): Receiver closed\n", user_data);
			midi_trigger->receiver = NULL;
			continue;
		}

		if (!event->is_long)
		{
			StackMidiShortEvent *short_event = event->types.short_event;
			bool unused = false, has_param2 = true;
			stack_midi_event_get_descriptor(short_event->event_type, &unused, &has_param2);

			// Validate the event type matches
			bool match_type = (short_event->event_type == midi_trigger->event.event_type);

			// Validate the channel matches (including "any")
			bool match_channel = ((midi_trigger->flags & STACK_MIDI_TRIGGER_ANY_CHANNEL) || (short_event->channel == midi_trigger->event.channel));

			// Validate that param 1 matches (including "any")
			bool match_param1 = ((midi_trigger->flags & STACK_MIDI_TRIGGER_ANY_PARAM1) || (short_event->param1 == midi_trigger->event.param1));

			// Validate that param 2 matches (including "any"), if the event type uses param2
			bool match_param2 = (!has_param2) || ((midi_trigger->flags & STACK_MIDI_TRIGGER_ANY_PARAM2) || (short_event->param2 == midi_trigger->event.param2));

			// If everything matches then we should trigger
			if (match_type && match_channel && match_param1 && match_param2)
			{
				stack_trigger_do_action(trigger);
			}
		}

		// Free the event
		stack_midi_event_free(event);
	}

	midi_trigger->thread_running = false;
	stack_log("stack_midi_trigger_thread(0x%016llx): Thread exited\n", user_data);
}

////////////////////////////////////////////////////////////////////////////////
// CREATION AND DESTRUCTION

/// Creates a key trigger
StackTrigger* stack_midi_trigger_create(StackCue *cue)
{
	// Allocate the trigger
	StackMidiTrigger *trigger = new StackMidiTrigger();

	// Initialise the superclass
	stack_trigger_init(&trigger->super, cue);

	// Make this class a StackMidiTrigger
	STACK_TRIGGER(trigger)->_class_name = "StackMidiTrigger";

	// Initial setup
	trigger->description = strdup("");
	trigger->midi_patch = strdup("");
	trigger->event.event_type = STACK_MIDI_EVENT_NOTE_ON;
	trigger->event.channel = 1;
	trigger->event.param1 = 0;
	trigger->event.param2 = 0;
	trigger->receiver = NULL;
	trigger->flags = 0;
	strncpy(trigger->event_text, "", 128);
	trigger->thread_running = false;
	trigger->thread = std::thread(stack_midi_trigger_thread, (void*)trigger);

	return STACK_TRIGGER(trigger);
}

/// Destroys a key trigger
void stack_midi_trigger_destroy(StackTrigger *trigger)
{
	StackMidiTrigger *midi_trigger = STACK_MIDI_TRIGGER(trigger);

	if (midi_trigger->thread_running)
	{
		// Tell the thread to stop running
		midi_trigger->thread_running = false;

		// Close the receiver (otherwise the thread will be blocked)
		if (midi_trigger->receiver != NULL)
		{
			StackMidiDevice *midi_device = stack_cue_list_get_midi_device(trigger->cue->parent, midi_trigger->midi_patch);
			if (midi_device != NULL)
			{
				stack_midi_device_remove_receiver(midi_device, midi_trigger->receiver);
			}
		}

		// Wait for the thread to exit
		midi_trigger->thread.join();
	}

	if (midi_trigger->description != NULL)
	{
		free(midi_trigger->description);
	}

	if (midi_trigger->midi_patch != NULL)
	{
		free(midi_trigger->midi_patch);
	}

	// Call parent destructor
	stack_trigger_destroy_base(trigger);
}

////////////////////////////////////////////////////////////////////////////////
// OVERRIDDEN FUNCTIONS

const char *stack_midi_trigger_get_name(StackTrigger *trigger)
{
	return "MIDI Event";
}

// Returns the name of the key we're triggered off
const char *stack_midi_trigger_get_event_text(StackTrigger *trigger)
{
	StackMidiTrigger *midi_trigger = STACK_MIDI_TRIGGER(trigger);

	if (!(midi_trigger->flags & STACK_MIDI_TRIGGER_CONFIGURED))
	{
		strncpy(midi_trigger->event_text, "Invalid configuration", sizeof(midi_trigger->event_text));
		return midi_trigger->event_text;
	}

	// event_describe takes an event rather than a shortevent
	StackMidiEvent event;
	event.is_long = false;
	event.types.short_event = &midi_trigger->event;
	event.ref_count = 1;

	stack_midi_event_describe(midi_trigger->event_text, sizeof(midi_trigger->event_text), &event, midi_trigger->flags & STACK_MIDI_TRIGGER_ANY_CHANNEL, midi_trigger->flags & STACK_MIDI_TRIGGER_ANY_PARAM1, midi_trigger->flags & STACK_MIDI_TRIGGER_ANY_PARAM2);
	return midi_trigger->event_text;
}

// Returns the user-specified description
const char *stack_midi_trigger_get_description(StackTrigger *trigger)
{
	return STACK_MIDI_TRIGGER(trigger)->description;
}

char *stack_midi_trigger_to_json(StackTrigger *trigger)
{
	Json::Value trigger_root;
	StackMidiTrigger *midi_trigger = STACK_MIDI_TRIGGER(trigger);

	trigger_root["description"] = midi_trigger->description;
	trigger_root["midi_patch"] = midi_trigger->midi_patch;
	trigger_root["event_type"] = midi_trigger->event.event_type;
	trigger_root["event_channel"] = midi_trigger->event.channel;
	trigger_root["event_param1"] = midi_trigger->event.param1;
	trigger_root["event_param2"] = midi_trigger->event.param2;
	trigger_root["any_channel"] = (bool)(midi_trigger->flags & STACK_MIDI_TRIGGER_ANY_CHANNEL);
	trigger_root["any_param1"] = (bool)(midi_trigger->flags & STACK_MIDI_TRIGGER_ANY_PARAM1);
	trigger_root["any_param2"] = (bool)(midi_trigger->flags & STACK_MIDI_TRIGGER_ANY_PARAM2);

	Json::StreamWriterBuilder builder;
	return strdup(Json::writeString(builder, trigger_root).c_str());
}

void stack_midi_trigger_free_json(StackTrigger *trigger, char *json_data)
{
	free(json_data);
}

void stack_midi_trigger_from_json(StackTrigger *trigger, const char *json_data)
{
	Json::Value trigger_root;
	bool configured = true;

	// Call the superclass version
	stack_trigger_from_json_base(trigger, json_data);

	// Parse JSON data
	stack_json_read_string(json_data, &trigger_root);

	// Get the data that's pertinent to us
	Json::Value& trigger_data = trigger_root["StackMidiTrigger"];

	StackMidiTrigger *midi_trigger = STACK_MIDI_TRIGGER(trigger);
	if (midi_trigger->description != NULL)
	{
		free(midi_trigger->description);
	}
	midi_trigger->description = strdup(trigger_data["description"].asString().c_str());

	if (midi_trigger->midi_patch != NULL)
	{
		free(midi_trigger->midi_patch);
	}
	midi_trigger->midi_patch = strdup(trigger_data["midi_patch"].asString().c_str());
	if (strlen(midi_trigger->midi_patch) == 0)
	{
		stack_log("stack_midi_trigger_from_json(0x%016llx): Invalid MIDI patch\n", trigger);
		configured = false;
	}

	if (trigger_data.isMember("event_type"))
	{
		midi_trigger->event.event_type = (uint8_t)trigger_data["event_type"].asUInt();
		if (!stack_midi_trigger_is_event_type_valid(midi_trigger->event.event_type))
		{
			stack_log("stack_midi_trigger_from_json(0x%016llx): Invalid event type\n", trigger);
			midi_trigger->event.event_type = 0;
			configured = false;
		}
	}

	if (trigger_data.isMember("event_channel"))
	{
		midi_trigger->event.channel = (uint8_t)trigger_data["event_channel"].asUInt();

		// If the channel is invalid, mark us as unconfigured
		if (midi_trigger->event.channel < 1 || midi_trigger->event.channel > 16)
		{
			stack_log("stack_midi_trigger_from_json(0x%016llx): Invalid MIDI channel\n", trigger);
			midi_trigger->event.channel = 0;
			configured = false;
		}
	}

	if (trigger_data.isMember("event_param1"))
	{
		uint32_t param = trigger_data["event_param1"].asUInt();
		if (param > 127)
		{
			stack_log("stack_midi_trigger_from_json(0x%016llx): Invalid value for parameter 1\n", trigger);
			param = 255;
			configured = false;
		}
		midi_trigger->event.param1 = (uint8_t)param;
	}

	if (trigger_data.isMember("event_param2"))
	{
		uint32_t param = trigger_data["event_param2"].asUInt();
		if (param > 127)
		{
			stack_log("stack_midi_trigger_from_json(0x%016llx): Invalid value for parameter 2\n", trigger);
			param = 255;
			configured = false;
		}
		midi_trigger->event.param2 = (uint8_t)param;
	}

	midi_trigger->flags = 0;

	if (trigger_data.isMember("any_channel") && trigger_data["any_channel"].asBool())
	{
		midi_trigger->flags |= STACK_MIDI_TRIGGER_ANY_CHANNEL;
	}

	if (trigger_data.isMember("any_param1") && trigger_data["any_param1"].asBool())
	{
		midi_trigger->flags |= STACK_MIDI_TRIGGER_ANY_PARAM1;
	}

	if (trigger_data.isMember("any_param2") && trigger_data["any_param2"].asBool())
	{
		midi_trigger->flags |= STACK_MIDI_TRIGGER_ANY_PARAM2;
	}

	if (configured)
	{
		midi_trigger->flags |= STACK_MIDI_TRIGGER_CONFIGURED;
	}
}

////////////////////////////////////////////////////////////////////////////////
// CONFIGURATION USER INTERFACE

void mtd_update_parameters()
{
	// Get the new event type
	GtkComboBox *mtdEventType = GTK_COMBO_BOX(gtk_builder_get_object(mtd_builder, "mtdEventType"));
	const char *new_event_type = gtk_combo_box_get_active_id(mtdEventType);
	int new_event_type_int = 0;
	if (new_event_type != NULL)
	{
		new_event_type_int = atoi(new_event_type);
	}

	// Get the widgets we want to update
	GtkLabel *mtdParam1 = GTK_LABEL(gtk_builder_get_object(mtd_builder, "mtdParam1Label"));
	GtkLabel *mtdParam2 = GTK_LABEL(gtk_builder_get_object(mtd_builder, "mtdParam2Label"));
	GtkEntry *mtdParam2Entry = GTK_ENTRY(gtk_builder_get_object(mtd_builder, "mtdParam2Entry"));
	GtkToggleButton *mtdParam2Any = GTK_TOGGLE_BUTTON(gtk_builder_get_object(mtd_builder, "mtdParam2Any"));

	char param1_name[16];
	char param2_name[16];

	int param_count = stack_midi_event_get_param_names(new_event_type_int, param1_name, sizeof(param1_name), param2_name, sizeof(param2_name));
	bool show_param2 = (param_count == 2);

	// Set the labels and show/hide as necessary
	strncat(param1_name, ":", sizeof(param1_name) - 1);
	strncat(param2_name, ":", sizeof(param2_name) - 1);
	gtk_label_set_text(mtdParam1, param1_name);
	gtk_label_set_text(mtdParam2, param2_name);

	// Show/hide parameter two as necessary
	gtk_widget_set_visible(GTK_WIDGET(mtdParam2), show_param2);
	gtk_widget_set_visible(GTK_WIDGET(mtdParam2Entry), show_param2);
	gtk_widget_set_visible(GTK_WIDGET(mtdParam2Any), show_param2);
}

extern "C" void mtd_event_type_changed(GtkComboBox *widget, gpointer user_data)
{
	mtd_update_parameters();
}

bool stack_midi_trigger_show_config_ui(StackTrigger *trigger, GtkWidget *parent, bool new_trigger)
{
	bool result = false;
	StackMidiTrigger *midi_trigger = STACK_MIDI_TRIGGER(trigger);

	// Build the dialog
	mtd_builder = gtk_builder_new_from_resource("/org/stack/ui/StackMidiTrigger.ui");
	GtkDialog *dialog = GTK_DIALOG(gtk_builder_get_object(mtd_builder, "midiTriggerDialog"));
	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));

	// Callbacks
	gtk_builder_add_callback_symbol(mtd_builder, "mtd_event_type_changed", G_CALLBACK(mtd_event_type_changed));
	gtk_builder_connect_signals(mtd_builder, trigger);

	// Set up response buttons
	gtk_dialog_add_buttons(dialog, "Cancel", 2, "OK", 1, NULL);
	gtk_dialog_set_default_response(dialog, 1);

	// We use these a lot
	GtkEntry *mtdDescriptionEntry = GTK_ENTRY(gtk_builder_get_object(mtd_builder, "mtdDescriptionEntry"));
	GtkEntry *mtdMidiPatchEntry = GTK_ENTRY(gtk_builder_get_object(mtd_builder, "mtdMidiPatchEntry"));
	GtkEntry *mtdChannelEntry = GTK_ENTRY(gtk_builder_get_object(mtd_builder, "mtdChannelEntry"));
	GtkEntry *mtdParam1Entry = GTK_ENTRY(gtk_builder_get_object(mtd_builder, "mtdParam1Entry"));
	GtkEntry *mtdParam2Entry = GTK_ENTRY(gtk_builder_get_object(mtd_builder, "mtdParam2Entry"));
	GtkComboBox *mtdEventType = GTK_COMBO_BOX(gtk_builder_get_object(mtd_builder, "mtdEventType"));
	GtkToggleButton *mtdChannelAny = GTK_TOGGLE_BUTTON(gtk_builder_get_object(mtd_builder, "mtdChannelAny"));
	GtkToggleButton *mtdParam1Any = GTK_TOGGLE_BUTTON(gtk_builder_get_object(mtd_builder, "mtdParam1Any"));
	GtkToggleButton *mtdParam2Any = GTK_TOGGLE_BUTTON(gtk_builder_get_object(mtd_builder, "mtdParam2Any"));
	GtkToggleButton *mtdActionStop = GTK_TOGGLE_BUTTON(gtk_builder_get_object(mtd_builder, "mtdActionStop"));
	GtkToggleButton *mtdActionPause = GTK_TOGGLE_BUTTON(gtk_builder_get_object(mtd_builder, "mtdActionPause"));
	GtkToggleButton *mtdActionPlay = GTK_TOGGLE_BUTTON(gtk_builder_get_object(mtd_builder, "mtdActionPlay"));

	// Set up helpers for entry boxes (note that we don't do this on param1 as it can be a note name)
	stack_limit_gtk_entry_int(mtdChannelEntry, false);
	stack_limit_gtk_entry_int(mtdParam2Entry, false);

	// Set the values on the dialog
	gtk_entry_set_text(mtdDescriptionEntry, midi_trigger->description);

	if (!new_trigger)
	{
		char buffer[64];

		// Set description ventry value
		gtk_entry_set_text(mtdDescriptionEntry, midi_trigger->description);

		// Set MIDI patch entry value
		gtk_entry_set_text(mtdMidiPatchEntry, midi_trigger->midi_patch);

		// Set event type choice
		snprintf(buffer, sizeof(buffer), "%u", midi_trigger->event.event_type);
		gtk_combo_box_set_active_id(mtdEventType, buffer);

		// Set channel entry values and check box
		if (midi_trigger->flags & STACK_MIDI_TRIGGER_ANY_CHANNEL)
		{
			strncpy(buffer, "", sizeof(buffer));
		}
		else
		{
			if (midi_trigger->event.channel >= 1 && midi_trigger->event.channel <= 16)
			{
				snprintf(buffer, sizeof(buffer), "%u", midi_trigger->event.channel);
			}
			else
			{
				strncpy(buffer, "", sizeof(buffer));
			}
		}
		gtk_entry_set_text(mtdChannelEntry, buffer);
		gtk_toggle_button_set_active(mtdChannelAny, midi_trigger->flags & STACK_MIDI_TRIGGER_ANY_CHANNEL);

		// Set parameter entry values and check boxes
		if (midi_trigger->flags & STACK_MIDI_TRIGGER_ANY_PARAM1)
		{
			strncpy(buffer, "", sizeof(buffer));
		}
		else
		{
			// For pitch bend, unpack the 14-bit value from the two 7-bit values
			if (midi_trigger->event.event_type == STACK_MIDI_EVENT_PITCH_BEND)
			{
				uint16_t pitch_bend = ((uint16_t)midi_trigger->event.param2 << 7) | midi_trigger->event.param1;
				if (pitch_bend < 8192)
				{
					snprintf(buffer, sizeof(buffer), "%u", pitch_bend);
				}
				else
				{
					strncpy(buffer, "", sizeof(buffer));
				}
			}
			else
			{
				if (midi_trigger->event.param1 < 128)
				{
					// Determine if parameter one could be expressed as a note
					bool param1_is_note = false;
					stack_midi_event_get_descriptor(midi_trigger->event.event_type, &param1_is_note, NULL);

					if (param1_is_note)
					{
						stack_midi_event_get_note_name(buffer, sizeof(buffer) - 1, midi_trigger->event.param1);
					}
					else
					{
						snprintf(buffer, sizeof(buffer), "%u", midi_trigger->event.param1);
					}
				}
				else
				{
					strncpy(buffer, "", sizeof(buffer));
				}
			}
		}
		gtk_entry_set_text(mtdParam1Entry, buffer);
		gtk_toggle_button_set_active(mtdParam1Any, midi_trigger->flags & STACK_MIDI_TRIGGER_ANY_PARAM1);
		if (midi_trigger->flags & STACK_MIDI_TRIGGER_ANY_PARAM2)
		{
			strncpy(buffer, "", sizeof(buffer));
		}
		else
		{
			if (midi_trigger->event.param2 < 128)
			{
				snprintf(buffer, sizeof(buffer), "%u", midi_trigger->event.param2);
			}
			else
			{
				strncpy(buffer, "", sizeof(buffer));
			}
		}
		gtk_entry_set_text(mtdParam2Entry, buffer);
		gtk_toggle_button_set_active(mtdParam2Any, midi_trigger->flags & STACK_MIDI_TRIGGER_ANY_PARAM2);

		switch (trigger->action)
		{
			case STACK_TRIGGER_ACTION_STOP:
				gtk_toggle_button_set_active(mtdActionStop, true);
				break;
			case STACK_TRIGGER_ACTION_PAUSE:
				gtk_toggle_button_set_active(mtdActionPause, true);
				break;
			default:
			case STACK_TRIGGER_ACTION_PLAY:
				gtk_toggle_button_set_active(mtdActionPlay, true);
				break;
		}

		// Update the parameters based on what is chosen
		mtd_update_parameters();
	}

	bool dialog_done = false;

	while (!dialog_done)
	{
		// Run the dialog
		gint response = gtk_dialog_run(dialog);

		switch (response)
		{
			case 1:	// OK
				const char *new_description, *new_midi_patch, *new_event_type, *new_channel, *new_param1, *new_param2;
				bool new_channel_any, new_param1_any, new_param2_any;
				int32_t new_channel_int, new_param1_int, new_param2_int, new_event_type_int;

				// Grab the new values from our fields
				new_description = gtk_entry_get_text(mtdDescriptionEntry);
				new_midi_patch = gtk_entry_get_text(mtdMidiPatchEntry);
				new_event_type = gtk_combo_box_get_active_id(mtdEventType);
				if (new_event_type != NULL)
				{
					new_event_type_int = atoi(new_event_type);
				}
				else
				{
					new_event_type = 0;
				}
				new_channel = gtk_entry_get_text(mtdChannelEntry);
				new_channel_int = atoi(new_channel);
				new_channel_any = gtk_toggle_button_get_active(mtdChannelAny);
				if (new_channel_any)
				{
					new_channel_int = 1;
				}
				new_param1 = gtk_entry_get_text(mtdParam1Entry);
				new_param1_int = atoi(new_param1);
				new_param1_any = gtk_toggle_button_get_active(mtdParam1Any);
				if (new_param1_any)
				{
					new_param1_int = 0;
				}
				new_param2 = gtk_entry_get_text(mtdParam2Entry);
				new_param2_int = atoi(new_param2);
				new_param2_any = gtk_toggle_button_get_active(mtdParam2Any);
				if (new_param2_any)
				{
					new_param2_int = 0;
				}

				// Validate MIDI patch
				if (strlen(new_midi_patch) == 0)
				{
					stack_midi_trigger_show_error(dialog, "No MIDI Patch specified", "You must enter the name of a MIDI patch to listen for MIDI events on");
					continue;
				}

				// Validate event type
				if (new_event_type == NULL || strlen(new_event_type) == 0)
				{
					stack_midi_trigger_show_error(dialog, "No event type chosen", "You must choose the type of MIDI event to trigger on");
					continue;
				}

				// Figure out what the parameters mean
				bool has_param2;
				bool param1_is_note;

				if (!stack_midi_event_get_descriptor(new_event_type_int, &param1_is_note, &has_param2))
				{
					// We should never get here
					stack_midi_trigger_show_error(dialog, "Invalid event type chosen", "You must choose a valid event type");
					continue;
				}

				// Validate channel
				if (!new_channel_any)
				{
					if (strlen(new_channel) == 0)
					{
						stack_midi_trigger_show_error(dialog, "No channel specified", "You must ener the channel number (1-16) to listen for MIDI events on, or choose 'Any'");
						continue;
					}

					if (new_channel_int < 1 || new_channel_int > 16)
					{
						stack_midi_trigger_show_error(dialog, "Invalid channel specified", "MIDI channel number must be in the range of 1 to 16");
						continue;
					}
				}

				// Validate param1
				if (!new_param1_any)
				{
					if (strlen(new_param1) == 0)
					{
						stack_midi_trigger_show_error(dialog, "No parameter specified", "You must ener the parameter value (0-127) for the event, or choose 'Any'");
						continue;
					}

					// If param 1 can be a note, and it looks like a note has been entered
					if (param1_is_note && ((new_param1[0] >= 'A' && new_param1[0] <= 'G') || (new_param1[0] >= 'a' && new_param1[0] <= 'g')))
					{
						// Convert note name to value
						new_param1_int = stack_midi_event_note_name_to_value(new_param1);
					}

					if (new_event_type_int == STACK_MIDI_EVENT_PITCH_BEND)
					{
						// Pitch bend is a 14-bit value that we split in to two
						if (new_param1_int < 0 || new_param1_int > 0x3fff)
						{
							stack_midi_trigger_show_error(dialog, "Invalid parameter specified", "The value of the pitch bend must be in the range of 0 to 8191");
							continue;
						}
					}
					else
					{
						if (new_param1_int < 0 || new_param1_int > 127)
						{
							stack_midi_trigger_show_error(dialog, "Invalid parameter one specified", "The value of the event parameters must be in the range of 0 to 127 (or a valid note name, e.g. C#4)");
							continue;
						}
					}
				}

				// Validate param2 (if applicable)
				if (!new_param2_any && has_param2)
				{
					// Despite being a two-parameter event, for the UI we present the
					// pitch bend as a single 14-bit text entry
					if (new_event_type_int != STACK_MIDI_EVENT_PITCH_BEND)
					{
						if (strlen(new_param2) == 0)
						{
							stack_midi_trigger_show_error(dialog, "No parameter specified", "You must ener the parameter value (0-127) for the event, or choose 'Any'");
							continue;
						}

						if (new_param2_int < 0 || new_param2_int > 127)
						{
							stack_midi_trigger_show_error(dialog, "Invalid parameter two specified", "The value of the event parameters must be in the range of 0 to 127");
							continue;
						}
					}
				}

				// Store the action
				if (gtk_toggle_button_get_active(mtdActionStop))
				{
					trigger->action = STACK_TRIGGER_ACTION_STOP;
				}
				else if (gtk_toggle_button_get_active(mtdActionPause))
				{
					trigger->action = STACK_TRIGGER_ACTION_PAUSE;
				}
				else if (gtk_toggle_button_get_active(mtdActionPlay))
				{
					trigger->action = STACK_TRIGGER_ACTION_PLAY;
				}

				// Update the description
				if (midi_trigger->description != NULL)
				{
					free(midi_trigger->description);
				}
				midi_trigger->description = strdup(gtk_entry_get_text(mtdDescriptionEntry));

				// Update the patch
				if (midi_trigger->midi_patch != NULL)
				{
					// If we had an existing receiver
					if (midi_trigger->receiver != NULL)
					{
						// Find the old device and remove the receiver
						StackMidiDevice *old_device = stack_cue_list_get_midi_device(trigger->cue->parent, midi_trigger->midi_patch);
						if (old_device != NULL)
						{
							stack_midi_device_remove_receiver(old_device, midi_trigger->receiver);
						}
					}

					// Free the old patch name
					free(midi_trigger->midi_patch);
				}
				midi_trigger->midi_patch = strdup(gtk_entry_get_text(mtdMidiPatchEntry));

				// Update the event
				midi_trigger->event.event_type = new_event_type_int;
				midi_trigger->event.channel = new_channel_int;
				if (new_event_type_int != STACK_MIDI_EVENT_PITCH_BEND)
				{
					midi_trigger->event.param1 = new_param1_int;
					midi_trigger->event.param2 = new_param2_int;
				}
				else
				{
					// Split the 14-bit value in to the two parameters
					midi_trigger->event.param1 = (uint8_t)(new_param1_int & 0x0000007f);
					midi_trigger->event.param2 = (uint8_t)((new_param1_int >> 7) & 0x0000007f);
				}

				// Figure out the new flags
				int new_flags;
				new_flags = STACK_MIDI_TRIGGER_CONFIGURED;
				if (new_channel_any)
				{
					new_flags |= STACK_MIDI_TRIGGER_ANY_CHANNEL;
				}
				if (new_param1_any)
				{
					new_flags |= STACK_MIDI_TRIGGER_ANY_PARAM1;
				}
				if (new_param2_any)
				{
					new_flags |= STACK_MIDI_TRIGGER_ANY_PARAM2;
				}
				midi_trigger->flags = new_flags;

				// Break out of the loop
				dialog_done = true;
				result = true;
				break;
			case 2: // Cancel
				// Break out of the loop
				dialog_done = true;
				result = false;
				break;
		}
	}

	// Destroy the dialog
	gtk_widget_destroy(GTK_WIDGET(dialog));

	// Free the builder
	g_object_unref(mtd_builder);
	mtd_builder = NULL;

	return result;
}

////////////////////////////////////////////////////////////////////////////////
// CLASS REGISTRATION

// Registers StackMidiTrigger with the application
void stack_midi_trigger_register()
{
	// Register built in cue types
	StackTriggerClass* midi_trigger_class = new StackTriggerClass{
		"StackMidiTrigger",
		"StackTrigger",
		"MIDI Event",
		stack_midi_trigger_create,
		stack_midi_trigger_destroy,
		stack_midi_trigger_get_name,
		stack_midi_trigger_get_event_text,
		stack_midi_trigger_get_description,
		NULL, //get_action
		stack_midi_trigger_to_json,
		stack_midi_trigger_free_json,
		stack_midi_trigger_from_json,
		stack_midi_trigger_show_config_ui,
		NULL,
		NULL,
		NULL
	};
	stack_register_trigger_class(midi_trigger_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_midi_trigger_register();
	return true;
}
