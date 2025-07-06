// Includes:
#include "StackApp.h"
#include "StackAudioDevice.h"
#include "StackMidiDevice.h"
#include "StackLog.h"
#include <gtk/gtkmessagedialog.h>
#include <cstring>

#define SAMD_FIELD_PATCH       0
#define SAMD_FIELD_TYPE        1
#define SAMD_FIELD_DEVICE      2
#define SAMD_FIELD_POINTER     3
#define SAMD_FIELD_CLASSNAME   4
#define SAMD_FIELD_HWDEVICE    5
#define SAMD_FIELD_CHANGED     6
#define SAMD_FIELD_DESCRIPTION 7

#define SANP_FIELD_PATCH       0
#define SANP_FIELD_HOST        1
#define SANP_FIELD_PORT        2

struct StackShowSettingsDialogData
{
	GtkDialog *dialog;
	GtkBuilder *builder;
	bool audio_device_changed;
};

struct StackAddMidiDeviceDialogData
{
	StackShowSettingsDialogData *parent_data;
	GtkDialog *dialog;
	GtkBuilder *builder;
	bool midi_device_changed;
};

struct StackAddNetworkPatchDialogData
{
	StackShowSettingsDialogData *parent_data;
	GtkDialog *dialog;
	GtkBuilder *builder;
};

extern "C" void sss_audio_provider_changed(GtkComboBox *widget, gpointer user_data)
{
	StackShowSettingsDialogData *dialog_data = (StackShowSettingsDialogData*)user_data;
	dialog_data->audio_device_changed = true;

	// Get the widgets we need to change
	GtkComboBoxText *audio_devices_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(dialog_data->builder, "sssAudioDeviceCombo"));
	GtkComboBoxText *sample_rate_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(dialog_data->builder, "sssSampleRateCombo"));
	GtkComboBoxText *channels_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(dialog_data->builder, "sssChannelsCombo"));

	// Clear the device list combo
	gtk_combo_box_text_remove_all(audio_devices_combo);

	// Get the name of the chosen provider class
	const gchar *provider_class = gtk_combo_box_get_active_id(widget);
	if (provider_class == NULL)
	{
		return;
	}

	// Get the class itself
	const StackAudioDeviceClass *sadc = stack_audio_device_get_class(provider_class);

	// Get the available devices for the class
	if (sadc->get_outputs_func != NULL)
	{
		StackAudioDeviceDesc *devices = NULL;
		size_t num_outputs = sadc->get_outputs_func(&devices);
		if (devices != NULL && num_outputs > 0)
		{
			for (size_t i = 0; i < num_outputs; i++)
			{
				char friendly_name[1024];
				snprintf(friendly_name, 1024, "%s %s", devices[i].name, devices[i].desc);
				gtk_combo_box_text_append(audio_devices_combo, devices[i].name, friendly_name);
			}
		}
	}
}

extern "C" void sss_audio_device_changed(GtkComboBox *widget, gpointer user_data)
{
	StackShowSettingsDialogData *dialog_data = (StackShowSettingsDialogData*)user_data;
	dialog_data->audio_device_changed = true;
}

extern "C" void sss_sample_rate_changed(GtkComboBox *widget, gpointer user_data)
{
	StackShowSettingsDialogData *dialog_data = (StackShowSettingsDialogData*)user_data;
	dialog_data->audio_device_changed = true;
}

extern "C" void sss_channels_changed(GtkComboBox *widget, gpointer user_data)
{
	StackShowSettingsDialogData *dialog_data = (StackShowSettingsDialogData*)user_data;
	dialog_data->audio_device_changed = true;
}

void sss_enable_osc_widgets(StackShowSettingsDialogData *dialog_data, bool enabled)
{
	gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(dialog_data->builder, "sssLabelOSCBindAddress")), enabled);
	gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(dialog_data->builder, "sssEntryOSCBindAddress")), enabled);
	gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(dialog_data->builder, "sssLabelOSCPort")), enabled);
	gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(dialog_data->builder, "sssEntryOSCPort")), enabled);
}

extern "C" void sss_enable_osc_toggled(GtkToggleButton *widget, gpointer user_data)
{
	StackShowSettingsDialogData *dialog_data = (StackShowSettingsDialogData*)user_data;
	sss_enable_osc_widgets(dialog_data, gtk_toggle_button_get_active(widget));
}

// TODO: Once we've rearchitected what we pass to StackAudioDevice in terms of
// function pointer and user_data (see the other TODO at the bottom of this
// function), then StackAppWindow here should become a generic GtkWindow* so
// that we can parent the dialog more appropriately (e.g. when being shown
// from another dialog)
void sss_show_dialog(StackAppWindow *window, StackCueList *cue_list, int tab)
{
	StackShowSettingsDialogData dialog_data;

	// Build the dialog
	dialog_data.builder = gtk_builder_new_from_resource("/org/stack/ui/StackShowSettings.ui");
	dialog_data.dialog = GTK_DIALOG(gtk_builder_get_object(dialog_data.builder, "StackShowSettingsDialog"));
	dialog_data.audio_device_changed = false;
	gtk_window_set_transient_for(GTK_WINDOW(dialog_data.dialog), GTK_WINDOW(window));
	gtk_window_set_default_size(GTK_WINDOW(dialog_data.dialog), 550, 300);
	gtk_dialog_add_buttons(dialog_data.dialog, "OK", 1, "Cancel", 2, NULL);
	gtk_dialog_set_default_response(dialog_data.dialog, 2);

	// Connect the signals
	gtk_builder_connect_signals(dialog_data.builder, (gpointer)&dialog_data);

	// Fill in the dialog
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowName")), stack_cue_list_get_show_name(cue_list));
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowDesigner")), stack_cue_list_get_show_designer(cue_list));
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowRevision")), stack_cue_list_get_show_revision(cue_list));

	// Get the widgets we need to look at
	GtkNotebook *notebook = GTK_NOTEBOOK(gtk_builder_get_object(dialog_data.builder, "sssNotebook"));
	GtkComboBox *audio_providers_combo = GTK_COMBO_BOX(gtk_builder_get_object(dialog_data.builder, "sssAudioProviderCombo"));
	GtkComboBox *audio_devices_combo = GTK_COMBO_BOX(gtk_builder_get_object(dialog_data.builder, "sssAudioDeviceCombo"));
	GtkComboBoxText *sample_rate_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(dialog_data.builder, "sssSampleRateCombo"));
	GtkComboBoxText *channels_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(dialog_data.builder, "sssChannelsCombo"));
	GtkListStore *midi_liststore = GTK_LIST_STORE(gtk_builder_get_object(dialog_data.builder, "sssMidiListStore"));
	GtkListStore *network_liststore = GTK_LIST_STORE(gtk_builder_get_object(dialog_data.builder, "sssNetworkListStore"));
	GtkTreeModel *midi_tree_model = GTK_TREE_MODEL(midi_liststore);
	GtkTreeModel *network_tree_model = GTK_TREE_MODEL(network_liststore);

	// Iterate over audio providers
	for (auto class_iter : *stack_audio_device_class_get_map())
	{
		// Get the next class in the iteration
		const StackAudioDeviceClass *c = class_iter.second;

		// If it's not the base class (as that can't output audio)
		if (strcmp(c->class_name, "StackAudioDevice") != 0)
		{
			gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(audio_providers_combo), c->class_name, c->get_friendly_name_func());
		}
	}

	if (cue_list && cue_list->audio_device)
	{
		char buffer[16];

		// Set the audio provider
		gtk_combo_box_set_active_id(audio_providers_combo, cue_list->audio_device->_class_name);

		// Set the audio device
		gtk_combo_box_set_active_id(audio_devices_combo, cue_list->audio_device->device_name);

		// Set the audio sample rate
		snprintf(buffer, 16, "%u", cue_list->audio_device->sample_rate);
		GtkWidget *sample_rate_entry = gtk_bin_get_child(GTK_BIN(sample_rate_combo));
		gtk_entry_set_text(GTK_ENTRY(sample_rate_entry), buffer);

		// Set the audio channels
		snprintf(buffer, 16, "%u", cue_list->audio_device->channels);
		GtkWidget *channels_entry = gtk_bin_get_child(GTK_BIN(channels_combo));
		gtk_entry_set_text(GTK_ENTRY(channels_entry), buffer);

		// Reset this to false as setting the active IDs fire the callbacks
		// (which is useful as it populates the combos for us)
		dialog_data.audio_device_changed = false;
	}

	// Fill in the MIDI devices list
	for (auto iter = stack_cue_list_midi_devices_begin(cue_list); iter != stack_cue_list_midi_devices_end(cue_list); ++iter)
	{
		GtkTreeIter new_item;
		gtk_list_store_append(midi_liststore, &new_item);

		const StackMidiDeviceClass *device_class = stack_midi_device_get_class(iter->second->_class_name);

		char buffer[256];
		snprintf(buffer, 256, "%s %s", iter->second->descriptor.name, iter->second->descriptor.desc);

		// Set the contents of the item
		gtk_list_store_set(midi_liststore, &new_item,
			SAMD_FIELD_PATCH, iter->first.c_str(),
			SAMD_FIELD_TYPE, device_class->get_friendly_name_func(),
			SAMD_FIELD_DEVICE, buffer,
			SAMD_FIELD_POINTER, iter->second,
			SAMD_FIELD_CLASSNAME, iter->second->_class_name,
			SAMD_FIELD_HWDEVICE, iter->second->descriptor.name,
			SAMD_FIELD_CHANGED, false,
			SAMD_FIELD_DESCRIPTION, iter->second->descriptor.desc,
			-1);
	}

	// Fill in the network patches list
	for (auto iter = stack_cue_list_network_patches_begin(cue_list); iter != stack_cue_list_network_patches_end(cue_list); ++iter)
	{
		GtkTreeIter new_item;
		gtk_list_store_append(network_liststore, &new_item);

		// Set the contents of the item
		gtk_list_store_set(network_liststore, &new_item,
			SANP_FIELD_PATCH, iter->first.c_str(),
			SANP_FIELD_HOST, iter->second->host,
			SANP_FIELD_PORT, iter->second->port,
			-1);
	}

	// Populate the inbound OSC details
	sss_enable_osc_widgets(&dialog_data, cue_list->osc_enabled);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(dialog_data.builder, "sssCheckEnableOSC")), cue_list->osc_enabled);
	if (cue_list->osc_enabled)
	{
		gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssEntryOSCBindAddress")), cue_list->osc_socket->bind_addr);
		char port_buffer[16];
		snprintf(port_buffer, 16, "%u", cue_list->osc_socket->bind_port);
		gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssEntryOSCPort")), port_buffer);
	}

	if (tab != STACK_SETTINGS_TAB_DEFAULT)
	{
		gtk_notebook_set_current_page(notebook, tab);
	}

	// Loop until there are no error saving or Cancel is clicked
	bool dialog_succeeded = false;
	while (!dialog_succeeded)
	{
		// This call blocks until the dialog is closed with a button
		gint result = gtk_dialog_run(dialog_data.dialog);

		// If the user hit Cancel then break out of the loop
		if (result == 2)
		{
			dialog_succeeded = true;
			break;
		}

		// Save Audio settings
		if (dialog_data.audio_device_changed)
		{
			// Pull the IDs from the combo boxes
			const gchar *device_class = gtk_combo_box_get_active_id(audio_providers_combo);
			const gchar *device = gtk_combo_box_get_active_id(audio_devices_combo);
			gchar *sample_rate_text = gtk_combo_box_text_get_active_text(sample_rate_combo);
			gchar *channels_text = gtk_combo_box_text_get_active_text(channels_combo);

			// Determine if everything was selected
			GtkWidget *message_dialog = NULL;
			if (device_class == NULL)
			{
				message_dialog = gtk_message_dialog_new(GTK_WINDOW(dialog_data.dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "No audio provider selected");
				gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(message_dialog), "You must select an provider for audio playback to save the settings.");
			}
			else if (device == NULL)
			{
				message_dialog = gtk_message_dialog_new(GTK_WINDOW(dialog_data.dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "No audio device selected");
				gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(message_dialog), "You must select an audio playback device to save the settings.");
			}
			else if (sample_rate_text == NULL)
			{
				message_dialog = gtk_message_dialog_new(GTK_WINDOW(dialog_data.dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "No audio sample rate selected");
				gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(message_dialog), "You must select the audio playback sample rate to save the settings.");
			}
			else if (channels_text == NULL)
			{
				message_dialog = gtk_message_dialog_new(GTK_WINDOW(dialog_data.dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "No audio channel count selected");
				gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(message_dialog), "You must select the number of audio channels for the playback device to save the settings.");
			}

			if (message_dialog != NULL)
			{
				gtk_window_set_title(GTK_WINDOW(message_dialog), "Error");
				gtk_dialog_run(GTK_DIALOG(message_dialog));
				gtk_widget_destroy(message_dialog);
			}
			else
			{
				int sample_rate = atoi(sample_rate_text);
				int channels = atoi(channels_text);

				// TODO: Having this function pointer from StackWindow hard-coded
				// here feels wrong. I think saw_get_audio_from_cuelist should be
				// replaced with a function on stack_cue_list, and "window" here
				// should be "cue_list"
				StackAudioDevice *new_device = stack_audio_device_new(device_class, device, channels, sample_rate, saw_get_audio_from_cuelist, window);
				stack_cue_list_set_audio_device(cue_list, new_device);

				dialog_succeeded = true;
			}

			// Tidy up
			g_free(sample_rate_text);
			g_free(channels_text);
		}
		else
		{
			// Audio device didn't change, so everything is fine
			dialog_succeeded = true;
		}

		// Get the OSC settings and validate them
		bool osc_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(dialog_data.builder, "sssCheckEnableOSC")));
		const gchar *osc_bind_address = gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssEntryOSCBindAddress")));
		int32_t osc_port = atoi(gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssEntryOSCPort"))));
		if (dialog_succeeded && osc_enabled)
		{
			GtkWidget *message_dialog = NULL;

			// Validate bind address
			if (osc_bind_address == 0 || strlen(osc_bind_address) == 0)
			{
				message_dialog = gtk_message_dialog_new(GTK_WINDOW(dialog_data.dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Invalid OSC bind address");
				gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(message_dialog), "If OSC is enabled, you must specify a bind address.\nYou can use 0.0.0.0 to bind on all interfaces.");
			}

			// Validate port
			if (osc_port <= 0 || osc_port >= 65535)
			{
				message_dialog = gtk_message_dialog_new(GTK_WINDOW(dialog_data.dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Invalid OSC port");
				gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(message_dialog), "If OSC is enabled, you must specify a valid port number from 1-65535.");
			}

			// Show error message if we have one
			if (message_dialog != NULL)
			{
				gtk_window_set_title(GTK_WINDOW(message_dialog), "Error");
				gtk_dialog_run(GTK_DIALOG(message_dialog));
				gtk_widget_destroy(message_dialog);
				dialog_succeeded = false;
			}
		}

		if (dialog_succeeded)
		{
			// Save show settings
			stack_cue_list_set_show_name(cue_list, gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowName"))));
			stack_cue_list_set_show_designer(cue_list, gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowDesigner"))));
			stack_cue_list_set_show_revision(cue_list, gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowRevision"))));

			// Iterate over the items in the liststore
			GtkTreeIter new_devices_iter;
			for (bool iter_result = gtk_tree_model_get_iter_first(midi_tree_model, &new_devices_iter); iter_result; iter_result = gtk_tree_model_iter_next(midi_tree_model, &new_devices_iter))
			{
				// Get the details about the device from the list
				GValue name_value = G_VALUE_INIT;
				GValue classname_value = G_VALUE_INIT;
				GValue device_value = G_VALUE_INIT;
				GValue changed_value = G_VALUE_INIT;
				GValue desc_value = G_VALUE_INIT;
				gtk_tree_model_get_value(midi_tree_model, &new_devices_iter, SAMD_FIELD_PATCH, &name_value);
				gtk_tree_model_get_value(midi_tree_model, &new_devices_iter, SAMD_FIELD_CLASSNAME, &classname_value);
				gtk_tree_model_get_value(midi_tree_model, &new_devices_iter, SAMD_FIELD_HWDEVICE, &device_value);
				gtk_tree_model_get_value(midi_tree_model, &new_devices_iter, SAMD_FIELD_CHANGED, &changed_value);
				gtk_tree_model_get_value(midi_tree_model, &new_devices_iter, SAMD_FIELD_DESCRIPTION, &desc_value);
				const char *name = g_value_get_string(&name_value);
				const char *classname = g_value_get_string(&classname_value);
				const char *device = g_value_get_string(&device_value);
				const char *desc = g_value_get_string(&desc_value);
				bool mdev_changed = g_value_get_boolean(&changed_value);

				// See if the device exists
				stack_cue_list_lock(cue_list);
				StackMidiDevice *existing_device = stack_cue_list_get_midi_device(cue_list, name);
				if (existing_device != NULL)
				{
					if (mdev_changed)
					{
						// Exists - replace it
						stack_cue_list_delete_midi_device(cue_list, name);

						// Early unlock of the cue list as the device delete could take time
						stack_cue_list_unlock(cue_list);

						// Destroy the device
						stack_midi_device_destroy(existing_device);

						// Add the new MIDI device
						stack_cue_list_lock(cue_list);
						StackMidiDevice *new_device = stack_midi_device_new(classname, device, desc);
						stack_cue_list_add_midi_device(cue_list, name, new_device);
					}
				}
				else
				{
					// Doesn't exist - add the new device
					StackMidiDevice *new_device = stack_midi_device_new(classname, device, desc);
					if (new_device != NULL)
					{
						stack_cue_list_add_midi_device(cue_list, name, new_device);
					}
				}

				stack_cue_list_unlock(cue_list);
			}

			// Lock whilst we make changes
			stack_cue_list_lock(cue_list);

			// Iterate over devices in the cue list MIDI devices and remove ones that no longer exist
			std::vector<std::string> to_remove;
			for (auto iter = stack_cue_list_midi_devices_begin(cue_list); iter != stack_cue_list_midi_devices_end(cue_list); ++iter)
			{
				bool found = false;

				// Iterate over the items in the liststore
				for (bool iter_result = gtk_tree_model_get_iter_first(midi_tree_model, &new_devices_iter); iter_result; iter_result = gtk_tree_model_iter_next(midi_tree_model, &new_devices_iter))
				{
					// Get the patch name from the list
					GValue name_value = G_VALUE_INIT;
					gtk_tree_model_get_value(midi_tree_model, &new_devices_iter, 0, &name_value);

					// Compare it
					if (strcmp(g_value_get_string(&name_value), iter->first.c_str()) == 0)
					{
						found = true;
						break;
					}
				}

				if (!found)
				{
					// Add to the list of devices to remove (we can't modify the map whilst iterating)
					to_remove.push_back(iter->first);
				}
			}

			// It's easiest to just clear the patches and re-add them whilst the list is locked

			stack_cue_list_clear_network_patches(cue_list);
			// Iterate over the items in the network liststore
			GtkTreeIter new_patches_iter;
			for (bool iter_result = gtk_tree_model_get_iter_first(network_tree_model, &new_patches_iter); iter_result; iter_result = gtk_tree_model_iter_next(network_tree_model, &new_patches_iter))
			{
				// Get the details about the device from the list
				GValue name_value = G_VALUE_INIT;
				GValue host_value = G_VALUE_INIT;
				GValue port_value = G_VALUE_INIT;
				gtk_tree_model_get_value(network_tree_model, &new_patches_iter, SANP_FIELD_PATCH, &name_value);
				gtk_tree_model_get_value(network_tree_model, &new_patches_iter, SANP_FIELD_HOST, &host_value);
				gtk_tree_model_get_value(network_tree_model, &new_patches_iter, SANP_FIELD_PORT, &port_value);
				const char *name = g_value_get_string(&name_value);
				const char *host = g_value_get_string(&host_value);
				guint port = g_value_get_uint(&port_value);

				stack_cue_list_add_network_patch(cue_list, name, host, port);
			}

			// Unlock
			stack_cue_list_unlock(cue_list);

			// Actually remove the items that weren't found
			for (auto patch_name : to_remove)
			{
				// Remove from the cue list, note that we do the cuelist lock
				// around this, as deleting a device could take some time if,
				// for example, a read thread takes a while to stop
				stack_cue_list_lock(cue_list);
				StackMidiDevice *device = stack_cue_list_get_midi_device(cue_list, patch_name.c_str());
				stack_cue_list_delete_midi_device(cue_list, patch_name.c_str());
				stack_cue_list_unlock(cue_list);

				// Destroy the device
				stack_midi_device_destroy(device);
			}

			// Update OSC config
			if (cue_list->osc_enabled && !osc_enabled)
			{
				// Disabling OSC
				stack_osc_socket_destroy(cue_list->osc_socket);
				cue_list->osc_socket = NULL;
				cue_list->osc_enabled = false;
			}
			else if (!cue_list->osc_enabled && osc_enabled)
			{
				// Enabling OSC
				cue_list->osc_enabled = true;
				cue_list->osc_socket = stack_osc_socket_create(osc_bind_address, osc_port, "/", cue_list);
			}
			// Unlock
			stack_cue_list_unlock(cue_list);
		}
	}

	// Destroy the dialog
	gtk_widget_destroy(GTK_WIDGET(dialog_data.dialog));
	g_object_unref(dialog_data.builder);
}

void samd_dialog_init(StackAddMidiDeviceDialogData *midi_dialog_data, StackShowSettingsDialogData *dialog_data)
{
	midi_dialog_data->parent_data = dialog_data;
	midi_dialog_data->builder = gtk_builder_new_from_resource("/org/stack/ui/StackAddMidiDevice.ui");
	midi_dialog_data->dialog = GTK_DIALOG(gtk_builder_get_object(midi_dialog_data->builder, "StackAddMidiDeviceDialog"));
	gtk_window_set_transient_for(GTK_WINDOW(midi_dialog_data->dialog), GTK_WINDOW(dialog_data->dialog));
	gtk_dialog_add_buttons(midi_dialog_data->dialog, "OK", 1, "Cancel", 2, NULL);
	gtk_dialog_set_default_response(midi_dialog_data->dialog, 2);

	// Connect the signals
	gtk_builder_connect_signals(midi_dialog_data->builder, (gpointer)midi_dialog_data);

	// Iterate over MIDI providers, adding them to the list
	GtkComboBox *midi_providers_combo = GTK_COMBO_BOX(gtk_builder_get_object(midi_dialog_data->builder, "samdMidiProviderCombo"));
	for (auto class_iter : *stack_midi_device_class_get_map())
	{
		// Get the next class in the iteration
		const StackMidiDeviceClass *c = class_iter.second;

		// If it's not the base class
		if (strcmp(c->class_name, "StackMidiDevice") != 0)
		{
			gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(midi_providers_combo), c->class_name, c->get_friendly_name_func());
		}
	}
}

bool samd_validate_settings(StackAddMidiDeviceDialogData *midi_dialog_data, GtkTreeIter *location)
{
	// Get the widgets we need to look at
	GtkEntry *patch_name_entry = GTK_ENTRY(gtk_builder_get_object(midi_dialog_data->builder, "samdPatchNameEntry"));
	GtkComboBoxText *midi_providers_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(midi_dialog_data->builder, "samdMidiProviderCombo"));
	GtkComboBoxText *midi_devices_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(midi_dialog_data->builder, "samdMidiDeviceCombo"));

	// Pull the Text/IDs from the combo boxes
	const gchar *patch_name = gtk_entry_get_text(patch_name_entry);
	const gchar *classname = gtk_combo_box_get_active_id(GTK_COMBO_BOX(midi_providers_combo));
	const gchar *type = gtk_combo_box_text_get_active_text(midi_providers_combo);
	const gchar *hw_device = gtk_combo_box_get_active_id(GTK_COMBO_BOX(midi_devices_combo));
	const gchar *device = gtk_combo_box_text_get_active_text(midi_devices_combo);

	// Determine if everything was selected
	GtkWidget *message_dialog = NULL;
	if (strlen(patch_name) == 0)
	{
		message_dialog = gtk_message_dialog_new(GTK_WINDOW(midi_dialog_data->dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "No patch name entered");
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(message_dialog), "You must enter a name for the MIDI device to save the settings.");
	}
	else if (classname == NULL)
	{
		message_dialog = gtk_message_dialog_new(GTK_WINDOW(midi_dialog_data->dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "No MIDI provider selected");
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(message_dialog), "You must select a MIDI provider for MIDI to save the settings.");
	}
	else if (hw_device == NULL)
	{
		message_dialog = gtk_message_dialog_new(GTK_WINDOW(midi_dialog_data->dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "No MIDI device selected");
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(message_dialog), "You must select a MIDI device to save the settings.");
	}

	if (message_dialog != NULL)
	{
		gtk_window_set_title(GTK_WINDOW(message_dialog), "Error");
		gtk_dialog_run(GTK_DIALOG(message_dialog));
		gtk_widget_destroy(message_dialog);
		return false;
	}
	else
	{
		GtkListStore *liststore = GTK_LIST_STORE(gtk_builder_get_object(midi_dialog_data->parent_data->builder, "sssMidiListStore"));
		GtkTreeIter new_item;

		// If a location wasn't given, add a new item
		if (location == NULL)
		{
			location = &new_item;
			gtk_list_store_append(liststore, location);
		}

		const char *desc = strchr(device, ' ') + 1;

		// Set the contents of the item
		gtk_list_store_set(liststore, location,
			SAMD_FIELD_PATCH, patch_name,
			SAMD_FIELD_TYPE, type,
			SAMD_FIELD_DEVICE, device,
			SAMD_FIELD_POINTER, NULL,  // Indicates to the parent dialog that it needs creating
			SAMD_FIELD_CLASSNAME, classname,
			SAMD_FIELD_HWDEVICE, hw_device,
			SAMD_FIELD_CHANGED, true,
			SAMD_FIELD_DESCRIPTION, desc,
			-1);

		return true;
	}
}

extern "C" void sss_midi_add_clicked(void *widget, gpointer user_data)
{
	// Initialise the dialog
	StackShowSettingsDialogData *dialog_data = (StackShowSettingsDialogData*)user_data;
	StackAddMidiDeviceDialogData midi_dialog_data;
	samd_dialog_init(&midi_dialog_data, dialog_data);

	// Loop until there are no error saving or Cancel is clicked
	bool dialog_succeeded = false;
	while (!dialog_succeeded)
	{
		// This call blocks until the dialog is closed with a button
		gint result = gtk_dialog_run(midi_dialog_data.dialog);

		// If the user hit Cancel then break out of the loop
		if (result == 2)
		{
			dialog_succeeded = true;
			break;
		}

		// Validate and update
		dialog_succeeded = samd_validate_settings(&midi_dialog_data, NULL);
	}

	// Destroy the dialog
	gtk_widget_destroy(GTK_WIDGET(midi_dialog_data.dialog));
	g_object_unref(midi_dialog_data.builder);
}

extern "C" void sss_midi_edit_clicked(void *widget, gpointer user_data)
{
	// Initialise the dialog
	StackShowSettingsDialogData *dialog_data = (StackShowSettingsDialogData*)user_data;
	StackAddMidiDeviceDialogData midi_dialog_data;
	samd_dialog_init(&midi_dialog_data, dialog_data);

	// Get the current selection
	GtkListStore *liststore = GTK_LIST_STORE(gtk_builder_get_object(dialog_data->builder, "sssMidiListStore"));
	GtkTreeView *treeview = GTK_TREE_VIEW(gtk_builder_get_object(dialog_data->builder, "sssMidiTreeView"));
	GtkTreeSelection *selection = gtk_tree_view_get_selection(treeview);
	GtkTreeIter iter;
	if (!gtk_tree_selection_get_selected(selection, NULL, &iter))
	{
		// Nothing was selected
		return;
	}

	// Get the contents of the list store
	gchar *patch_name = NULL, *class_name = NULL, *device_name = NULL;
	gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, SAMD_FIELD_PATCH, &patch_name, -1);
	gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, SAMD_FIELD_CLASSNAME, &class_name, -1);
	gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, SAMD_FIELD_HWDEVICE, &device_name, -1);

	// Get the widgets we need to look at
	GtkEntry *patch_name_entry = GTK_ENTRY(gtk_builder_get_object(midi_dialog_data.builder, "samdPatchNameEntry"));
	GtkComboBox *midi_providers_combo = GTK_COMBO_BOX(gtk_builder_get_object(midi_dialog_data.builder, "samdMidiProviderCombo"));
	GtkComboBox *midi_devices_combo = GTK_COMBO_BOX(gtk_builder_get_object(midi_dialog_data.builder, "samdMidiDeviceCombo"));

	// Set the values
	gtk_entry_set_text(patch_name_entry, patch_name);
	gtk_combo_box_set_active_id(midi_providers_combo, class_name);
	gtk_combo_box_set_active_id(midi_devices_combo, device_name);

	// Loop until there are no error saving or Cancel is clicked
	bool dialog_succeeded = false;
	while (!dialog_succeeded)
	{
		// This call blocks until the dialog is closed with a button
		gint result = gtk_dialog_run(midi_dialog_data.dialog);

		// If the user hit Cancel then break out of the loop
		if (result == 2)
		{
			dialog_succeeded = true;
			break;
		}

		// Validate and update
		if (midi_dialog_data.midi_device_changed)
		{
			dialog_succeeded = samd_validate_settings(&midi_dialog_data, &iter);
		}
		else
		{
			// Nothing change
			dialog_succeeded = true;
		}
	}

	// Destroy the dialog
	gtk_widget_destroy(GTK_WIDGET(midi_dialog_data.dialog));
	g_object_unref(midi_dialog_data.builder);
}

extern "C" void sss_midi_remove_clicked(void *widget, gpointer user_data)
{
	StackShowSettingsDialogData *dialog_data = (StackShowSettingsDialogData*)user_data;

	// Get the selected item
	GtkListStore *liststore = GTK_LIST_STORE(gtk_builder_get_object(dialog_data->builder, "sssMidiListStore"));
	GtkTreeView *treeview = GTK_TREE_VIEW(gtk_builder_get_object(dialog_data->builder, "sssMidiTreeView"));
	GtkTreeSelection *selection = gtk_tree_view_get_selection(treeview);
	GtkTreeIter iter;
	if (!gtk_tree_selection_get_selected(selection, NULL, &iter))
	{
		return;
	}

	// Remove from the list store
	gtk_list_store_remove(liststore, &iter);
}

extern "C" void samd_patch_name_changed(GtkEntry *widget, gpointer user_data)
{
	StackAddMidiDeviceDialogData *dialog_data = (StackAddMidiDeviceDialogData*)user_data;
	dialog_data->midi_device_changed = true;
}

extern "C" void samd_midi_provider_changed(GtkComboBox *widget, gpointer user_data)
{
	StackAddMidiDeviceDialogData *dialog_data = (StackAddMidiDeviceDialogData*)user_data;
	dialog_data->midi_device_changed = true;

	// Get the widgets we need to change
	GtkComboBoxText *midi_devices_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(dialog_data->builder, "samdMidiDeviceCombo"));

	// Clear the device list combo
	gtk_combo_box_text_remove_all(midi_devices_combo);

	// Get the name of the chosen provider class
	const gchar *provider_class = gtk_combo_box_get_active_id(widget);
	if (provider_class == NULL)
	{
		return;
	}

	// Get the class itself
	const StackMidiDeviceClass *smdc = stack_midi_device_get_class(provider_class);

	// Get the available devices for the class
	if (smdc->get_outputs_func != NULL)
	{
		StackMidiDeviceDesc *devices = NULL;
		size_t num_outputs = smdc->get_outputs_func(&devices);
		if (devices != NULL && num_outputs > 0)
		{
			for (size_t i = 0; i < num_outputs; i++)
			{
				char friendly_name[1024];
				snprintf(friendly_name, 1024, "%s %s", devices[i].name, devices[i].desc);
				gtk_combo_box_text_append(midi_devices_combo, devices[i].name, friendly_name);
			}
		}
	}
}

extern "C" void samd_midi_device_changed(GtkComboBox *widget, gpointer user_data)
{
	StackAddMidiDeviceDialogData *dialog_data = (StackAddMidiDeviceDialogData*)user_data;
	dialog_data->midi_device_changed = true;
}

bool sanp_validate_settings(StackAddNetworkPatchDialogData *network_dialog_data, GtkTreeIter *location)
{
	// Get the widgets we need to look at
	GtkEntry *patch_name_entry = GTK_ENTRY(gtk_builder_get_object(network_dialog_data->builder, "sanpPatchNameEntry"));
	GtkEntry *host_entry = GTK_ENTRY(gtk_builder_get_object(network_dialog_data->builder, "sanpHostEntry"));
	GtkEntry *port_entry = GTK_ENTRY(gtk_builder_get_object(network_dialog_data->builder, "sanpPortEntry"));

	// Pull the Text from the boxes
	const gchar *patch_name = gtk_entry_get_text(patch_name_entry);
	const gchar *host = gtk_entry_get_text(host_entry);
	const gchar *port = gtk_entry_get_text(port_entry);
	guint port_number = (guint)atoi(port);

	// Determine if everything was selected
	GtkWidget *message_dialog = NULL;
	if (strlen(patch_name) == 0)
	{
		message_dialog = gtk_message_dialog_new(GTK_WINDOW(network_dialog_data->dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "No patch name entered");
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(message_dialog), "You must enter a name for the network patch to save the settings.");
	}
	else if (strlen(host) == 0)
	{
		message_dialog = gtk_message_dialog_new(GTK_WINDOW(network_dialog_data->dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "No host entered");
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(message_dialog), "You must enter a hostname or IP address to save the settings.");
	}
	else if (strlen(port) == 0 || port == 0)
	{
		message_dialog = gtk_message_dialog_new(GTK_WINDOW(network_dialog_data->dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "No port entered");
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(message_dialog), "You must enter a port number to save the settings.");
	}

	if (message_dialog != NULL)
	{
		gtk_window_set_title(GTK_WINDOW(message_dialog), "Error");
		gtk_dialog_run(GTK_DIALOG(message_dialog));
		gtk_widget_destroy(message_dialog);
		return false;
	}
	else
	{
		GtkListStore *liststore = GTK_LIST_STORE(gtk_builder_get_object(network_dialog_data->parent_data->builder, "sssNetworkListStore"));
		GtkTreeIter new_item;

		// If a location wasn't given, add a new item
		if (location == NULL)
		{
			location = &new_item;
			gtk_list_store_append(liststore, location);
		}

		// Set the contents of the item
		gtk_list_store_set(liststore, location,
			SANP_FIELD_PATCH, patch_name,
			SANP_FIELD_HOST, host,
			SANP_FIELD_PORT, port_number,
			-1);

		return true;
	}
}

void sanp_dialog_init(StackAddNetworkPatchDialogData *network_dialog_data, StackShowSettingsDialogData *dialog_data)
{
	network_dialog_data->parent_data = dialog_data;
	network_dialog_data->builder = gtk_builder_new_from_resource("/org/stack/ui/StackAddNetworkPatch.ui");
	network_dialog_data->dialog = GTK_DIALOG(gtk_builder_get_object(network_dialog_data->builder, "StackAddNetworkPatchDialog"));
	gtk_window_set_transient_for(GTK_WINDOW(network_dialog_data->dialog), GTK_WINDOW(dialog_data->dialog));
	gtk_dialog_add_buttons(network_dialog_data->dialog, "OK", 1, "Cancel", 2, NULL);
	gtk_dialog_set_default_response(network_dialog_data->dialog, 2);

	// Connect the signals
	gtk_builder_connect_signals(network_dialog_data->builder, (gpointer)network_dialog_data);
}

extern "C" void sss_network_add_clicked(void *widget, gpointer user_data)
{
	// Initialise the dialog
	StackShowSettingsDialogData *dialog_data = (StackShowSettingsDialogData*)user_data;
	StackAddNetworkPatchDialogData network_dialog_data;
	sanp_dialog_init(&network_dialog_data, dialog_data);

	// Loop until there are no error saving or Cancel is clicked
	bool dialog_succeeded = false;
	while (!dialog_succeeded)
	{
		// This call blocks until the dialog is closed with a button
		gint result = gtk_dialog_run(network_dialog_data.dialog);

		// If the user hit Cancel then break out of the loop
		if (result == 2)
		{
			dialog_succeeded = true;
			break;
		}

		// Validate and update
		dialog_succeeded = sanp_validate_settings(&network_dialog_data, NULL);
	}

	// Destroy the dialog
	gtk_widget_destroy(GTK_WIDGET(network_dialog_data.dialog));
	g_object_unref(network_dialog_data.builder);
}

extern "C" void sss_network_edit_clicked(void *widget, gpointer user_data)
{
	// Initialise the dialog
	StackShowSettingsDialogData *dialog_data = (StackShowSettingsDialogData*)user_data;
	StackAddNetworkPatchDialogData network_dialog_data;
	sanp_dialog_init(&network_dialog_data, dialog_data);

	// Get the current selection
	GtkListStore *liststore = GTK_LIST_STORE(gtk_builder_get_object(dialog_data->builder, "sssNetworkListStore"));
	GtkTreeView *treeview = GTK_TREE_VIEW(gtk_builder_get_object(dialog_data->builder, "sssNetworkTreeView"));
	GtkTreeSelection *selection = gtk_tree_view_get_selection(treeview);
	GtkTreeIter iter;
	if (!gtk_tree_selection_get_selected(selection, NULL, &iter))
	{
		// Nothing was selected
		return;
	}

	// Get the contents of the list store
	gchar *patch_name = NULL, *host = NULL;
	guint port = 0;
	gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, SANP_FIELD_PATCH, &patch_name, -1);
	gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, SANP_FIELD_HOST, &host, -1);
	gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, SANP_FIELD_PORT, &port, -1);
	char port_buffer[16];
	snprintf(port_buffer, sizeof(port_buffer), "%u", port);

	// Get the widgets we need to look at
	GtkEntry *patch_name_entry = GTK_ENTRY(gtk_builder_get_object(network_dialog_data.builder, "sanpPatchNameEntry"));
	GtkEntry *host_entry = GTK_ENTRY(gtk_builder_get_object(network_dialog_data.builder, "sanpHostEntry"));
	GtkEntry *port_entry = GTK_ENTRY(gtk_builder_get_object(network_dialog_data.builder, "sanpPortEntry"));

	// Set the values
	gtk_entry_set_text(patch_name_entry, patch_name);
	gtk_entry_set_text(host_entry, host);
	gtk_entry_set_text(port_entry, port_buffer);

	// Loop until there are no error saving or Cancel is clicked
	bool dialog_succeeded = false;
	while (!dialog_succeeded)
	{
		// This call blocks until the dialog is closed with a button
		gint result = gtk_dialog_run(network_dialog_data.dialog);

		// If the user hit Cancel then break out of the loop
		if (result == 2)
		{
			dialog_succeeded = true;
			break;
		}

		// Validate and update
		dialog_succeeded = sanp_validate_settings(&network_dialog_data, &iter);
	}

	// Destroy the dialog
	gtk_widget_destroy(GTK_WIDGET(network_dialog_data.dialog));
	g_object_unref(network_dialog_data.builder);
}

extern "C" void sss_network_remove_clicked(void *widget, gpointer user_data)
{
	StackShowSettingsDialogData *dialog_data = (StackShowSettingsDialogData*)user_data;

	// Get the selected item
	GtkListStore *liststore = GTK_LIST_STORE(gtk_builder_get_object(dialog_data->builder, "sssNetworkListStore"));
	GtkTreeView *treeview = GTK_TREE_VIEW(gtk_builder_get_object(dialog_data->builder, "sssNetworkTreeView"));
	GtkTreeSelection *selection = gtk_tree_view_get_selection(treeview);
	GtkTreeIter iter;
	if (!gtk_tree_selection_get_selected(selection, NULL, &iter))
	{
		return;
	}

	// Remove from the list store
	gtk_list_store_remove(liststore, &iter);
}
