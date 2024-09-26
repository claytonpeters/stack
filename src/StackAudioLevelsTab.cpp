// Includes:
#include "StackAudioLevelsTab.h"
#include "StackGtkHelper.h"
#include "StackCue.h"
#include <math.h>

StackAudioLevelsTab *stack_audio_levels_tab_new(StackCue *cue, salt_get_volume_property_t get_volume_property, salt_get_crosspoint_property_t get_crosspoint_property)
{
	StackAudioLevelsTab *result = new StackAudioLevelsTab;

	result->master_scale = NULL;
	result->master_entry = NULL;
	result->channel_scales = NULL;
	result->channel_entries = NULL;
	result->cue = cue;
	result->get_volume_property = get_volume_property;
	result->get_crosspoint_property = get_crosspoint_property;

	result->root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_margin_start(result->root, 4);
	gtk_widget_set_margin_end(result->root, 4);
	gtk_widget_set_margin_top(result->root, 4);
	gtk_widget_set_margin_bottom(result->root, 4);

	return result;
}

void stack_audio_levels_tab_destroy(StackAudioLevelsTab *tab)
{
	if (tab->channel_scales)
	{
		delete [] tab->channel_scales;
	}

	if (tab->channel_entries)
	{
		delete [] tab->channel_entries;
	}

	delete tab;
}

typedef void (*ToggleButtonCallback)(GtkToggleButton*, gpointer);

static void stack_audio_levels_tab_create_slider(GtkWidget *parent, const char *title, const char *identifier, GtkWidget **scale, GtkWidget **entry)
{
	char name_buffer[256];

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(parent), vbox, false, false, 4);

	GtkWidget *label = gtk_label_new(title);
	gtk_box_pack_start(GTK_BOX(vbox), label, false, false, 4);

	*scale = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, -60.0, 20.0, 0.1);
	snprintf(name_buffer, 256, "range_%s", identifier);
	gtk_widget_set_name(*scale, name_buffer);
	gtk_range_set_inverted(GTK_RANGE(*scale), true);
	gtk_range_set_show_fill_level(GTK_RANGE(*scale), true);

	// Add on some marks ever 20dB, naming a few
	gtk_scale_add_mark(GTK_SCALE(*scale), 20.0, GTK_POS_RIGHT, "20dB");
	gtk_scale_add_mark(GTK_SCALE(*scale), 10.0, GTK_POS_RIGHT, NULL);
	gtk_scale_add_mark(GTK_SCALE(*scale), 0.0, GTK_POS_RIGHT, "0dB");
	for (float mark = -10.0f; mark > -51.0f; mark -= 10.0f)
	{
		gtk_scale_add_mark(GTK_SCALE(*scale), mark, GTK_POS_RIGHT, NULL);
	}
	gtk_scale_add_mark(GTK_SCALE(*scale), -60.0, GTK_POS_RIGHT, "-Inf");

	gtk_scale_set_draw_value(GTK_SCALE(*scale), false);
	gtk_box_pack_start(GTK_BOX(vbox), *scale, true, true, 4);

	*entry = gtk_entry_new();
	snprintf(name_buffer, 256, "entry_%s", identifier);
	gtk_widget_set_name(*entry, name_buffer);
	gtk_entry_set_width_chars(GTK_ENTRY(*entry), 6);
	gtk_entry_set_alignment(GTK_ENTRY(*entry), 0.5);
	stack_limit_gtk_entry_float(GTK_ENTRY(*entry), true);
	gtk_box_pack_start(GTK_BOX(vbox), *entry, false, false, 4);

	gtk_widget_show(label);
	gtk_widget_show(*scale);
	gtk_widget_show(*entry);
	gtk_widget_show(vbox);
}

void stack_audio_levels_tab_format_volume(char *buffer, size_t size, double volume, bool hide_minus_inf)
{
	if (!std::isfinite(volume))
	{
		if (hide_minus_inf)
		{
			buffer[0] = '\0';
		}
		else
		{
			strncpy(buffer, "-Inf", size);
		}
	}
	else
	{
		snprintf(buffer, size, "%.2f", volume);
	}
}

void stack_audio_levels_tab_set_level(GtkWidget *scale, GtkWidget *entry, double volume, bool is_null, bool hide_minus_inf)
{
	char buffer[32];
	if (is_null)
	{
		buffer[0] = '\0';
		volume = -INFINITY;
	}
	else
	{
		stack_audio_levels_tab_format_volume(buffer, 32, volume, hide_minus_inf);
	}

	if (scale != NULL)
	{
		gtk_range_set_value(GTK_RANGE(scale), volume);
	}

	if (entry != NULL)
	{
		gtk_entry_set_text(GTK_ENTRY(entry), buffer);
	}
}

static void stack_audio_levels_tab_range_callback(GtkRange *range, gpointer user_data)
{
	StackAudioLevelsTab *tab = STACK_AUDIO_LEVELS_TAB(user_data);
	GtkWidget *entry;

	// Determine if its master or channel we're changing
	size_t channel = 0;
	const gchar *name = gtk_widget_get_name(GTK_WIDGET(range));
	if (strstr(name, "range_channel_") != NULL)
	{
		channel = atoi(&name[14]);
		entry = tab->channel_entries[channel - 1];
	}
	else
	{
		entry = tab->master_entry;
	}

	// Update the property and get the validated version
	StackProperty *property = tab->get_volume_property(tab->cue, channel, false);
	double vol_db = gtk_range_get_value(range);
	if (property != NULL)
	{
		if (stack_property_get_nullable(property))
		{
			stack_property_set_null(property, STACK_PROPERTY_VERSION_DEFINED, false);
		}
		stack_property_set_double(property, STACK_PROPERTY_VERSION_DEFINED, vol_db);
		stack_property_get_double(property, STACK_PROPERTY_VERSION_DEFINED, &vol_db);
	}

	// Update UI to match
	stack_audio_levels_tab_set_level(GTK_WIDGET(range), GTK_WIDGET(entry), vol_db, false, !stack_property_get_nullable(property));
}

static double stack_audio_levels_tab_entry_to_property(GtkEntry *entry, StackProperty *property, bool *out_is_null)
{
	const gchar *text = gtk_entry_get_text(entry);
	double vol_db = -INFINITY;
	bool is_nullable = stack_property_get_nullable(property);
	bool is_null = false;

	// If property is nullable and no text entered, set to null
	if (strlen(text) == 0)
	{
		if (is_nullable)
		{
			is_null = true;
			stack_property_set_null(property, STACK_PROPERTY_VERSION_DEFINED, true);
		}
	}
	else
	{
		vol_db = atof(text);
	}

	// If we have a non-null value (or if we're not nullable)
	if (!is_null)
	{
		if (is_nullable)
		{
			stack_property_set_null(property, STACK_PROPERTY_VERSION_DEFINED, false);
		}
		stack_property_set_double(property, STACK_PROPERTY_VERSION_DEFINED, vol_db);
		stack_property_get_double(property, STACK_PROPERTY_VERSION_DEFINED, &vol_db);
	}

	// Return is_null if wated
	if (out_is_null != NULL)
	{
		*out_is_null = is_null;
	}

	return vol_db;
}

static void stack_audio_levels_tab_entry_callback(GtkEntry *entry, GdkEventFocus *event, gpointer user_data)
{
	StackAudioLevelsTab *tab = STACK_AUDIO_LEVELS_TAB(user_data);
	GtkWidget *scale;

	// Determine if its master or channel we're changing
	size_t channel = 0;
	const gchar *name = gtk_widget_get_name(GTK_WIDGET(entry));
	if (strstr(name, "entry_channel_") != NULL)
	{
		channel = atoi(&name[14]);
		scale = tab->channel_scales[channel - 1];
	}
	else
	{
		scale = tab->master_scale;
	}

	// Update the property and get the validated version
	StackProperty *property = tab->get_volume_property(tab->cue, channel, false);
	if (property != NULL)
	{
		bool is_null = false;
		double vol_db = stack_audio_levels_tab_entry_to_property(entry, property, &is_null);

		// Update UI to match
		stack_audio_levels_tab_set_level(GTK_WIDGET(scale), GTK_WIDGET(entry), vol_db, is_null, !stack_property_get_nullable(property));
	}
}

static gboolean stack_audio_levels_tab_crosspoint_entry_changed(GtkWidget *widget, GdkEventFocus *event, gpointer user_data)
{
	StackAudioLevelsTab *tab = STACK_AUDIO_LEVELS_TAB(user_data);

	// Build property name
	const gchar *ch_out_in = gtk_widget_get_name(widget);

	// Determine which property to modify
	size_t output_channel = atoi(ch_out_in);
	size_t input_channel = atoi(&strchr(ch_out_in, '_')[1]);
	StackProperty *property = tab->get_crosspoint_property(tab->cue, input_channel, output_channel, true);

	if (property != NULL)
	{
		bool is_null = false;
		double vol_db = stack_audio_levels_tab_entry_to_property(GTK_ENTRY(widget), property, &is_null);
		stack_audio_levels_tab_set_level(NULL, widget, vol_db, is_null, !stack_property_get_nullable(property));
	}

	return FALSE;
}

void stack_audio_levels_tab_populate(StackAudioLevelsTab *tab, size_t input_channels, size_t output_channels, bool show_crosspoints, GCallback affect_live_cb)
{
	char buffer[64];
	char name_buffer[64];

	// Remove anything currently in the tab (in case we're being repopulated)
	gtk_container_foreach(GTK_CONTAINER(tab->root), (GtkCallback)[](GtkWidget *widget, gpointer user_data) -> void {
		gtk_widget_destroy(widget);
	}, NULL);

	if (affect_live_cb != NULL)
	{
		GtkWidget *check_live = gtk_check_button_new_with_label("Affect Live");
		// TODO: gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_live), cue->affect_live);
		gtk_widget_set_tooltip_text(check_live, "If ticked, changes to levels made whilst the cue is playing will take effect immediately");
		gtk_widget_show(check_live);
		gtk_box_pack_start(GTK_BOX(tab->root), check_live, false, false, 4);
		g_signal_connect(check_live, "toggled", affect_live_cb, (gpointer)tab->cue);
	}

	// MASTER
	stack_audio_levels_tab_create_slider(tab->root, "Master", "master", &tab->master_scale, &tab->master_entry);

	// Range signal handler
	g_signal_connect(tab->master_scale, "value-changed", G_CALLBACK(stack_audio_levels_tab_range_callback), (gpointer)tab);
	g_signal_connect(tab->master_entry, "focus-out-event", G_CALLBACK(stack_audio_levels_tab_entry_callback), (gpointer)tab);

	// Set the initial master level
	StackProperty *master_prop = tab->get_volume_property(tab->cue, 0, true);
	bool is_null = stack_property_get_null(master_prop, STACK_PROPERTY_VERSION_DEFINED);
	double vol_db = 0.0;
	stack_property_get_double(master_prop, STACK_PROPERTY_VERSION_DEFINED, &vol_db);
	stack_audio_levels_tab_set_level(tab->master_scale, tab->master_entry, vol_db, is_null, !stack_property_get_nullable(master_prop));

	// CHANNELS
	if (input_channels > 0)
	{
		// Re-create arrays
		if (tab->channel_scales != NULL)
		{
			delete [] tab->channel_scales;
		}
		tab->channel_scales = new GtkWidget*[input_channels];
		if (tab->channel_entries != NULL)
		{
			delete [] tab->channel_entries;
		}
		tab->channel_entries = new GtkWidget*[input_channels];

		for (size_t channel = 0; channel < input_channels; channel++)
		{
			// Create the slider
			snprintf(buffer, 64, "Ch. %lu", channel + 1);
			snprintf(name_buffer, 64, "channel_%lu", channel + 1);
			stack_audio_levels_tab_create_slider(tab->root, buffer, name_buffer, &tab->channel_scales[channel], &tab->channel_entries[channel]);

			// Set signals for these faders
			g_signal_connect(tab->channel_scales[channel], "value-changed", G_CALLBACK(stack_audio_levels_tab_range_callback), (gpointer)tab);
			g_signal_connect(tab->channel_entries[channel], "focus-out-event", G_CALLBACK(stack_audio_levels_tab_entry_callback), (gpointer)tab);

			// Get the property
			StackProperty *ch_vol_prop = tab->get_volume_property(tab->cue, channel + 1, true);
			bool is_null = stack_property_get_null(ch_vol_prop, STACK_PROPERTY_VERSION_DEFINED);
			double volume = 0.0;
			stack_property_get_double(ch_vol_prop, STACK_PROPERTY_VERSION_DEFINED, &volume);

			// Set the initial level
			stack_audio_levels_tab_set_level(tab->channel_scales[channel], tab->channel_entries[channel], volume, is_null, !stack_property_get_nullable(ch_vol_prop));
		}
	}

	// CROSSPOINTS
	if (show_crosspoints)
	{
		GtkWidget *cp_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
		GtkWidget *cp_label = gtk_label_new("Crosspoints");
		gtk_widget_show(cp_label);
		gtk_box_pack_start(GTK_BOX(cp_box), cp_label, false, true, 4);

		GtkWidget *cp_grid = gtk_grid_new();
		gtk_grid_set_row_spacing(GTK_GRID(cp_grid), 4);
		gtk_grid_set_column_spacing(GTK_GRID(cp_grid), 4);

		if (input_channels > 0 && output_channels > 0)
		{
			// Column and row for labels
			gtk_grid_insert_row(GTK_GRID(cp_grid), 0);
			gtk_grid_insert_column(GTK_GRID(cp_grid), 0);

			// Columns and rows for entry
			for (size_t row = 0; row < input_channels; row++)
			{
				gtk_grid_insert_row(GTK_GRID(cp_grid), 0);
			}
			for (size_t column = 0; column < output_channels; column++)
			{
				gtk_grid_insert_column(GTK_GRID(cp_grid), 0);
			}

			// Populate labels
			for (size_t row = 0; row < input_channels; row++)
			{
				snprintf(buffer, 64, "In %lu", row + 1);
				GtkWidget *label = gtk_label_new(buffer);
				gtk_widget_show(label);
				gtk_grid_attach(GTK_GRID(cp_grid), label, 0, row + 1, 1, 1);
			}
			for (size_t column = 0; column < output_channels; column++)
			{
				snprintf(buffer, 64, "Out %lu", column + 1);
				GtkWidget *label = gtk_label_new(buffer);
				gtk_widget_show(label);
				gtk_grid_attach(GTK_GRID(cp_grid), label, column + 1, 0, 1, 1);
			}

			// Populate entry boxes
			for (size_t row = 0; row < input_channels; row++)
			{
				for (size_t column = 0; column < output_channels; column++)
				{
					// Create entry box
					GtkWidget *crosspoint = gtk_entry_new();
					gtk_entry_set_width_chars(GTK_ENTRY(crosspoint), 6);
					gtk_entry_set_alignment(GTK_ENTRY(crosspoint), 0.5);
					stack_limit_gtk_entry_float(GTK_ENTRY(crosspoint), true);
					gtk_widget_show(crosspoint);
					gtk_grid_attach(GTK_GRID(cp_grid), crosspoint, column + 1, row + 1, 1, 1);

					// Set the entry name to the output channel, followed by the input channel
					snprintf(buffer, 64, "%lu_%lu", column, row);
					gtk_widget_set_name(crosspoint, buffer);
					g_signal_connect(crosspoint, "focus-out-event", G_CALLBACK(stack_audio_levels_tab_crosspoint_entry_changed), (gpointer)tab);

					// Set value
					StackProperty *property = tab->get_crosspoint_property(tab->cue, row, column, true);
					if (stack_property_get_null(property, STACK_PROPERTY_VERSION_DEFINED))
					{
						buffer[0] = '\0';
					}
					else
					{
						double cp_value = 0.0;
						stack_property_get_double(property, STACK_PROPERTY_VERSION_DEFINED, &cp_value);
						stack_audio_levels_tab_format_volume(buffer, 64, cp_value, true);
					}
					gtk_entry_set_text(GTK_ENTRY(crosspoint), buffer);
				}
			}
		}

		gtk_widget_show(cp_grid);
		gtk_widget_show(cp_box);
		gtk_box_pack_start(GTK_BOX(cp_box), cp_grid, false, false, 4);
		gtk_box_pack_start(GTK_BOX(tab->root), cp_box, false, false, 4);
	}
}

