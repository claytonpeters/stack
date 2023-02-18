// Includes:
#include "StackApp.h"
#include "StackAudioDevice.h"
#include <cstring>

struct StackShowSettingsDialogData
{
	GtkDialog *dialog;
	GtkBuilder *builder;
};

void sss_audio_provider_changed(GtkComboBox *widget, gpointer user_data)
{
	StackShowSettingsDialogData *dialog_data = (StackShowSettingsDialogData*)user_data;

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

void sss_audio_device_changed(GtkComboBox *widget, gpointer user_data)
{
	StackShowSettingsDialogData *dialog_data = (StackShowSettingsDialogData*)user_data;

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

void sss_sample_rate_changed(GtkComboBox *widget, gpointer user_data)
{
	StackShowSettingsDialogData *dialog_data = (StackShowSettingsDialogData*)user_data;
}

void sss_channels_changed(GtkComboBox *widget, gpointer user_data)
{
	StackShowSettingsDialogData *dialog_data = (StackShowSettingsDialogData*)user_data;
}

void sss_show_dialog(StackAppWindow *window)
{
	StackShowSettingsDialogData dialog_data;

	// Build the dialog
	dialog_data.builder = gtk_builder_new_from_file("StackShowSettings.ui");
	dialog_data.dialog = GTK_DIALOG(gtk_builder_get_object(dialog_data.builder, "StackShowSettingsDialog"));
	gtk_window_set_transient_for(GTK_WINDOW(dialog_data.dialog), GTK_WINDOW(window));
	gtk_window_set_default_size(GTK_WINDOW(dialog_data.dialog), 550, 300);
	gtk_dialog_add_buttons(dialog_data.dialog, "OK", 1, "Cancel", 2, NULL);
	gtk_dialog_set_default_response(dialog_data.dialog, 2);

	// Set up the callbacks - other
	gtk_builder_add_callback_symbol(dialog_data.builder, "sss_audio_provider_changed", G_CALLBACK(sss_audio_provider_changed));
	gtk_builder_add_callback_symbol(dialog_data.builder, "sss_audio_device_changed", G_CALLBACK(sss_audio_device_changed));
	gtk_builder_add_callback_symbol(dialog_data.builder, "sss_sample_rate_changed", G_CALLBACK(sss_sample_rate_changed));
	gtk_builder_add_callback_symbol(dialog_data.builder, "sss_channels_changed", G_CALLBACK(sss_channels_changed));

	// Connect the signals
	gtk_builder_connect_signals(dialog_data.builder, (gpointer)&dialog_data);

	// Fill in the dialog
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowName")), stack_cue_list_get_show_name(window->cue_list));
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowDesigner")), stack_cue_list_get_show_designer(window->cue_list));
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowRevision")), stack_cue_list_get_show_revision(window->cue_list));

	// Iterate over audio providers
	GtkComboBoxText *audio_providers_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(dialog_data.builder, "sssAudioProviderCombo"));
	auto class_iter = stack_audio_device_class_iter_front();
	while (!stack_audio_device_class_iter_at_end(class_iter))
	{
		// Get the next class in the iteration
		const StackAudioDeviceClass *c = stack_audio_device_class_iter_get(class_iter);

		// If it's not the base class (as that can't output audio)
		if (strcmp(c->class_name, "StackAudioDevice") != 0)
		{
			gtk_combo_box_text_append(audio_providers_combo, c->class_name, c->get_friendly_name_func());
		}

		// Iterate
		stack_audio_device_class_iter_next(class_iter);
	}
	stack_audio_device_class_iter_free(class_iter);

	// This call blocks until the dialog goes away
	gint result = gtk_dialog_run(dialog_data.dialog);

	// If the result is OK (see gtk_dialog_add_buttons above)
	if (result == 1)
	{
		// Save show settings
		stack_cue_list_set_show_name(window->cue_list, gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowName"))));
		stack_cue_list_set_show_designer(window->cue_list, gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowDesigner"))));
		stack_cue_list_set_show_revision(window->cue_list, gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(dialog_data.builder, "sssShowRevision"))));

		// Save Audio settings

		// Save Video settings

		// Save MIDI settings
	}

	// Destroy the dialog
	gtk_widget_destroy(GTK_WIDGET(dialog_data.dialog));
	g_object_unref(dialog_data.builder);
}

