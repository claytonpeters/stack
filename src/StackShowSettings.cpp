// Includes:
#include "StackApp.h"
#include "StackAudioDevice.h"
#include "StackLog.h"
#include <gtk/gtkmessagedialog.h>
#include <cstring>

struct StackShowSettingsDialogData
{
	GtkDialog *dialog;
	GtkBuilder *builder;
	bool audio_device_changed;
};

extern "C" void sss_audio_provider_changed(GtkComboBox *widget, gpointer user_data)
{
	StackShowSettingsDialogData *dialog_data = (StackShowSettingsDialogData*)user_data;
	dialog_data->audio_device_changed = true;

	// Get the widgets we need to change
	GtkComboBoxText *devices_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(dialog_data->builder, "sssAudioDeviceCombo"));
	GtkComboBoxText *sample_rate_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(dialog_data->builder, "sssSampleRateCombo"));
	GtkComboBoxText *channels_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(dialog_data->builder, "sssChannelsCombo"));

	// Clear all the combos
	gtk_combo_box_text_remove_all(devices_combo);
	gtk_combo_box_text_remove_all(sample_rate_combo);
	gtk_combo_box_text_remove_all(channels_combo);

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
				snprintf(friendly_name, 1024, "%s (%s)", devices[i].desc, devices[i].name);
				gtk_combo_box_text_append(devices_combo, devices[i].name, friendly_name);
			}
		}
	}
}

extern "C" void sss_audio_device_changed(GtkComboBox *widget, gpointer user_data)
{
	StackShowSettingsDialogData *dialog_data = (StackShowSettingsDialogData*)user_data;
	dialog_data->audio_device_changed = true;

	// Get the widgets we need to change
	GtkComboBoxText *sample_rate_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(dialog_data->builder, "sssSampleRateCombo"));
	GtkComboBoxText *channels_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(dialog_data->builder, "sssChannelsCombo"));

	// Clear all the combos
	gtk_combo_box_text_remove_all(sample_rate_combo);
	gtk_combo_box_text_remove_all(channels_combo);

	// Get the name of the chosen provider class, and the device
	const gchar *provider_class = gtk_combo_box_get_active_id(GTK_COMBO_BOX(gtk_builder_get_object(dialog_data->builder, "sssAudioProviderCombo")));
	const gchar *device_name = gtk_combo_box_get_active_id(widget);
	if (provider_class == NULL || device_name == NULL)
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
				if (strcmp(devices[i].name, device_name) == 0)
				{
					for (size_t j = devices[i].min_channels; j <= devices[i].max_channels; j++)
					{
						char channel_value[16];
						snprintf(channel_value, 16, "%lu", j);
						gtk_combo_box_text_append(channels_combo, channel_value, channel_value);
					}
					for (size_t j = 0; j < devices[i].num_rates; j++)
					{
						char sample_rate_value[16];
						snprintf(sample_rate_value, 16, "%d", devices[i].rates[j]);
						gtk_combo_box_text_append(sample_rate_combo, sample_rate_value, sample_rate_value);
					}
				}
			}
		}
	}

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

void sss_show_dialog(StackAppWindow *window)
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
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowName")), stack_cue_list_get_show_name(window->cue_list));
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowDesigner")), stack_cue_list_get_show_designer(window->cue_list));
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowRevision")), stack_cue_list_get_show_revision(window->cue_list));

	// Get the widgets we need to look at
	GtkComboBox *audio_providers_combo = GTK_COMBO_BOX(gtk_builder_get_object(dialog_data.builder, "sssAudioProviderCombo"));
	GtkComboBox *devices_combo = GTK_COMBO_BOX(gtk_builder_get_object(dialog_data.builder, "sssAudioDeviceCombo"));
	GtkComboBox *sample_rate_combo = GTK_COMBO_BOX(gtk_builder_get_object(dialog_data.builder, "sssSampleRateCombo"));
	GtkComboBox *channels_combo = GTK_COMBO_BOX(gtk_builder_get_object(dialog_data.builder, "sssChannelsCombo"));

	// Iterate over audio providers
	auto class_iter = stack_audio_device_class_iter_front();
	while (!stack_audio_device_class_iter_at_end(class_iter))
	{
		// Get the next class in the iteration
		const StackAudioDeviceClass *c = stack_audio_device_class_iter_get(class_iter);

		// If it's not the base class (as that can't output audio)
		if (strcmp(c->class_name, "StackAudioDevice") != 0)
		{
			gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(audio_providers_combo), c->class_name, c->get_friendly_name_func());
		}

		// Iterate
		stack_audio_device_class_iter_next(class_iter);
	}
	stack_audio_device_class_iter_free(class_iter);

	if (window->cue_list && window->cue_list->audio_device)
	{
		char buffer[16];

		// Set the audio provider
		gtk_combo_box_set_active_id(audio_providers_combo, window->cue_list->audio_device->_class_name);

		// Set the audio device
		gtk_combo_box_set_active_id(devices_combo, window->cue_list->audio_device->device_name);

		// Set the audio sample rate
		snprintf(buffer, 16, "%u", window->cue_list->audio_device->sample_rate);
		gtk_combo_box_set_active_id(sample_rate_combo, buffer);

		// Set the audio channels
		snprintf(buffer, 16, "%u", window->cue_list->audio_device->channels);
		gtk_combo_box_set_active_id(channels_combo, buffer);

		// Reset this to false as setting the active IDs fire the callbacks
		// (which is useful as it populates the combos for us)
		dialog_data.audio_device_changed = false;
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
			const gchar *device = gtk_combo_box_get_active_id(devices_combo);
			const gchar *sample_rate_text = gtk_combo_box_get_active_id(sample_rate_combo);
			const gchar *channels_text = gtk_combo_box_get_active_id(channels_combo);

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
				// here feels wrong
				StackAudioDevice *new_device = stack_audio_device_new(device_class, device, channels, sample_rate, saw_get_audio_from_cuelist, window);
				stack_cue_list_set_audio_device(window->cue_list, new_device);

				dialog_succeeded = true;
			}
		}
		else
		{
			// Audio device didn't change, so everything is fine
			dialog_succeeded = true;
		}

		if (dialog_succeeded)
		{
			// Save show settings
			stack_cue_list_set_show_name(window->cue_list, gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowName"))));
			stack_cue_list_set_show_designer(window->cue_list, gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowDesigner"))));
			stack_cue_list_set_show_revision(window->cue_list, gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowRevision"))));
		}
	}

	// Destroy the dialog
	gtk_widget_destroy(GTK_WIDGET(dialog_data.dialog));
	g_object_unref(dialog_data.builder);
}
