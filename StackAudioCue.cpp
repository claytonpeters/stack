// Includes:
#include "StackApp.h"
#include "StackLog.h"
#include "StackAudioCue.h"
#include "StackGtkEntryHelper.h"
#include "MPEGAudioFile.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <json/json.h>
#include <vector>
#include <time.h>

// Creates an audio cue
static StackCue* stack_audio_cue_create(StackCueList *cue_list)
{
	// Allocate the cue
	StackAudioCue* cue = new StackAudioCue();

	// Initialise the superclass
	stack_cue_init(&cue->super, cue_list);

	// Make this class a StackAudioCue
	cue->super._class_name = "StackAudioCue";

	// We start in error state until we have a file
	stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_ERROR);

	// Initialise our variables: cue data
	cue->file = strdup("");
	cue->short_filename = (char*)strdup("");
	cue->media_start_time = 0;
	cue->media_end_time = 0;
	cue->loops = 1;
	cue->play_volume = 0.0;
	cue->builder = NULL;
	cue->media_tab = NULL;

	// Initialise our variables: preview
	cue->preview_widget = NULL;

	// Initialise our variables: playback
	cue->playback_live_volume = 0.0;
	cue->playback_loops = 0;
	cue->playback_file = NULL;
	cue->resampler = NULL;

	// Change some superclass variables
	stack_cue_set_name(STACK_CUE(cue), "${filename}");

	return STACK_CUE(cue);
}

// Destroys an audio cue
static void stack_audio_cue_destroy(StackCue *cue)
{
	StackAudioCue *acue = STACK_AUDIO_CUE(cue);

	// Our tidy up here
	free(acue->file);
	free(acue->short_filename);

	// Tidy up our file
	if (acue->playback_file != NULL)
	{
		stack_audio_file_destroy(acue->playback_file);
	}

	// Tidy up our resampler
	if (acue->resampler != NULL)
	{
		stack_resampler_destroy(acue->resampler);
	}

	// Tidy up
	if (acue->builder != NULL)
	{
		// Destroy the top level widget in the builder
		gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(acue->builder, "window1")));

		// Unref the builder
		g_object_unref(acue->builder);
	}
	if (acue->media_tab != NULL)
	{
		// Remove our reference to the media tab
		g_object_unref(acue->media_tab);
	}
	if (acue->preview_widget != NULL)
	{
		g_object_unref(acue->preview_widget);
	}

	// Call parent destructor
	stack_cue_destroy_base(cue);
}

static void stack_audio_cue_update_action_time(StackAudioCue *cue)
{
	// Re-calculate the action time
	stack_time_t action_time;
	if (cue->media_end_time > cue->media_start_time)
	{
		action_time = cue->media_end_time - cue->media_start_time;
	}
	else
	{
		action_time = 0.0;
	}

	stack_cue_set_action_time(STACK_CUE(cue), action_time);
}

bool stack_audio_cue_set_file(StackAudioCue *cue, const char *uri)
{
	// Result
	bool result = false;

	// Check to see if file was previously empty
	bool was_empty = (strlen(cue->file) == 0) && (cue->media_start_time == 0) && (cue->media_end_time == 0);

	StackAudioFile *new_playback_file = stack_audio_file_create(uri);

	// If we successfully got a file, then update the cue
	if (new_playback_file != NULL)
	{
		// Update the cue state
		stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_STOPPED);

		// Store the file data
		if (cue->playback_file != NULL)
		{
			stack_audio_file_destroy(cue->playback_file);
		}
		cue->playback_file = new_playback_file;

		// Store the filename
		free(cue->file);
		cue->file = strdup(uri);
		free(cue->short_filename);
		cue->short_filename = g_filename_from_uri(uri, NULL, NULL);

		// Reset the media start/end times to the whole file
		cue->media_start_time = 0;
		cue->media_end_time = new_playback_file->length;

		// Update the cue action time;
		stack_audio_cue_update_action_time(cue);

		// Reset the audio preview to the full new file
		if (cue->preview_widget)
		{
			stack_audio_preview_set_file(cue->preview_widget, cue->file);
			stack_audio_preview_set_view_range(cue->preview_widget, 0, new_playback_file->length);
			stack_audio_preview_set_selection(cue->preview_widget, cue->media_start_time, cue->media_end_time);
		}

		// We succeeded
		result = true;
	}

	// Notify cue list that we've changed
	stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue));

	return result;
}

static void acp_file_changed(GtkFileChooserButton *widget, gpointer user_data)
{
	StackAudioCue *cue = STACK_AUDIO_CUE(user_data);

	// Get the filename
	gchar* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widget));
	gchar* uri = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(widget));

	// Change the file
	if (stack_audio_cue_set_file(cue, uri))
	{
		// Display the file length
		char time_buffer[32], text_buffer[128];
		stack_format_time_as_string(cue->playback_file->length, time_buffer, 32);
		snprintf(text_buffer, 128, "(Length is %s)", time_buffer);
		gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(cue->builder, "acpFileLengthLabel")), text_buffer);

		// Update the UI
		stack_format_time_as_string(cue->media_start_time, time_buffer, 32);
		gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(cue->builder, "acpTrimStart")), time_buffer);
		stack_format_time_as_string(cue->media_end_time, time_buffer, 32);
		gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(cue->builder, "acpTrimEnd")), time_buffer);
	}

	// Tidy up
	g_free(filename);
	g_free(uri);

	// Fire an updated-selected-cue signal to signal the UI to change (we might
	// have changed state)
	StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget));
	g_signal_emit_by_name((gpointer)window, "update-selected-cue");
	g_signal_emit_by_name((gpointer)window, "update-cue-properties", STACK_CUE(cue));
}

static gboolean acp_trim_start_changed(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	StackAudioCue *cue = STACK_AUDIO_CUE(user_data);

	// Set the time
	cue->media_start_time = stack_time_string_to_ns(gtk_entry_get_text(GTK_ENTRY(widget)));

	// Keep it inside the bounds of our file
	if (cue->media_start_time < 0)
	{
		cue->media_start_time = 0;
	}
	if (cue->playback_file && cue->media_start_time > cue->playback_file->length)
	{
		cue->media_start_time = cue->playback_file->length;
	}

	// Update the action_time
	stack_audio_cue_update_action_time(cue);

	// Update the UI
	char buffer[32];
	stack_format_time_as_string(cue->media_start_time, buffer, 32);
	gtk_entry_set_text(GTK_ENTRY(widget), buffer);
	stack_audio_preview_set_selection(cue->preview_widget, cue->media_start_time, cue->media_end_time);

	// Fire an updated-selected-cue signal to signal the UI to change
	StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(widget);
	g_signal_emit_by_name((gpointer)window, "update-selected-cue");

	// Notify cue list that we've changed
	stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue));

	return false;
}

static gboolean acp_trim_end_changed(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	StackAudioCue *cue = STACK_AUDIO_CUE(user_data);

	// Set the time
	cue->media_end_time = stack_time_string_to_ns(gtk_entry_get_text(GTK_ENTRY(widget)));

	// Keep it inside the bounds of our file
	if (cue->media_end_time < 0)
	{
		cue->media_end_time = 0;
	}
	if (cue->playback_file && cue->media_end_time > cue->playback_file->length)
	{
		cue->media_end_time = cue->playback_file->length;
	}

	// Update the action time
	stack_audio_cue_update_action_time(cue);

	// Update the UI
	char buffer[32];
	stack_format_time_as_string(cue->media_end_time, buffer, 32);
	gtk_entry_set_text(GTK_ENTRY(widget), buffer);
	stack_audio_preview_set_selection(cue->preview_widget, cue->media_start_time, cue->media_end_time);

	// Fire an updated-selected-cue signal to signal the UI to change
	StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(widget);
	g_signal_emit_by_name((gpointer)window, "update-selected-cue");

	// Notify cue list that we've changed
	stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue));

	return false;
}

static gboolean acp_loops_changed(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	StackAudioCue *cue = STACK_AUDIO_CUE(user_data);

	// Set the loops
	cue->loops = atoi(gtk_entry_get_text(GTK_ENTRY(widget)));

	// Update the UI to reformat
	char buffer[32];
	snprintf(buffer, 32, "%d", cue->loops);
	gtk_entry_set_text(GTK_ENTRY(widget), buffer);

	// Notify cue list that we've changed
	stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue));

	return false;
}

static void acp_volume_changed(GtkRange *range, gpointer user_data)
{
	StackAudioCue *cue = STACK_AUDIO_CUE(user_data);

	// Get the volume
	double vol_db = gtk_range_get_value(range);

	char buffer[32];

	if (vol_db < -49.99)
	{
		cue->play_volume = -INFINITY;
		snprintf(buffer, 32, "-Inf dB");
	}
	else
	{
		cue->play_volume = vol_db;
		snprintf(buffer, 32, "%.2f dB", vol_db);
	}

	// Get the volume label and update it's value
	gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(cue->builder, "acpVolumeValueLabel")), buffer);

	// Notify cue list that we've changed
	stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue));
}

// Called when we're being played
static bool stack_audio_cue_play(StackCue *cue)
{
	// Get the state of the cue before we call the base function
	StackCueState pre_play_state = cue->state;

	// Call the super class
	if (!stack_cue_play_base(cue))
	{
		return false;
	}

	// For tidiness
	StackAudioCue *audio_cue = STACK_AUDIO_CUE(cue);

	// If we were paused before...
	if (pre_play_state == STACK_CUE_STATE_PAUSED)
	{
		return true;
	}

	// Initialise playback
	audio_cue->playback_live_volume = audio_cue->play_volume;
	audio_cue->playback_loops = 0;

	// If the sample rate of the file does not match the playback device, set
	// up a resampler
	if (audio_cue->playback_file->sample_rate != audio_cue->super.parent->audio_device->sample_rate)
	{
		audio_cue->resampler = stack_resampler_create(audio_cue->playback_file->sample_rate, audio_cue->super.parent->audio_device->sample_rate, audio_cue->playback_file->channels);
	}

	// Seek to the right point in the file
	stack_audio_file_seek(audio_cue->playback_file, audio_cue->media_start_time);

	// Show the playback marker on the UI
	if (audio_cue->preview_widget != NULL)
	{
		stack_audio_preview_set_playback(audio_cue->preview_widget, audio_cue->media_start_time);
		stack_audio_preview_show_playback(audio_cue->preview_widget, true);
	}

	return true;
}

static void stack_audio_cue_stop(StackCue *cue)
{
	// Call the superclass (this handles stopping the cue after the action time)
	stack_cue_stop_base(cue);

	// For tidiness
	StackAudioCue *audio_cue = STACK_AUDIO_CUE(cue);

	// Tidy up the resampler
	if (audio_cue->resampler != NULL)
	{
		stack_resampler_destroy(audio_cue->resampler);
		audio_cue->resampler = NULL;
	}

	// Queue a redraw of the media tab if our UI is active (to hide the playback marker)
	if (audio_cue->preview_widget != NULL)
	{
		stack_audio_preview_show_playback(audio_cue->preview_widget, false);
	}
}

// Called when we're pulsed (every few milliseconds whilst the cue is in playback)
static void stack_audio_cue_pulse(StackCue *cue, stack_time_t clocktime)
{
	StackAudioCue *audio_cue = STACK_AUDIO_CUE(cue);

	// Get the current action time
	stack_time_t run_action_time;
	stack_cue_get_running_times(cue, clocktime, NULL, &run_action_time, NULL, NULL, NULL, NULL);

	// If we're in playback, but we've reached the end of our time
	if (cue->state == STACK_CUE_STATE_PLAYING_ACTION && run_action_time == cue->action_time)
	{
		bool loop = false;

		// Determine if we should loop
		if (audio_cue->loops <= 0)
		{
			loop = true;
		}
		else if (audio_cue->loops > 1)
		{
			audio_cue->playback_loops++;

			if (audio_cue->playback_loops < audio_cue->loops)
			{
				loop = true;
			}
		}

		// If we are looping, we need to reset the cue start
		if (loop)
		{
			// Set the cue start time to now minus the amount of time the pre-
			// wait should have taken so it looks like we should have just
			// started the action
			cue->start_time = clocktime - cue->pre_time;

			// Reset any pause times
			cue->pause_time = 0;
			cue->paused_time = 0;
			cue->pause_paused_time = 0;

			// Seek back in the file
			stack_audio_file_seek(audio_cue->playback_file, audio_cue->media_start_time);

			// We need to reset the resampler as we may have told it
			// we're finished
			if (audio_cue->resampler)
			{
				stack_resampler_destroy(audio_cue->resampler);
				audio_cue->resampler = stack_resampler_create(audio_cue->playback_file->sample_rate, audio_cue->super.parent->audio_device->sample_rate, audio_cue->playback_file->channels);
			}
		}
	}

	// Call the super class. Note that we do this _after_ the looping code above
	// becaues the super class will stop the cue at the end of the action time
	stack_cue_pulse_base(cue, clocktime);

	// Redraw the preview periodically whilst in playback, and we're the selected
	// queue
	if (audio_cue->media_tab != NULL && audio_cue->preview_widget != NULL && stack_get_clock_time() - audio_cue->preview_widget->last_redraw_time > 33 * NANOSECS_PER_MILLISEC)
	{
		audio_cue->preview_widget->last_redraw_time = stack_get_clock_time();
		stack_audio_preview_set_playback(audio_cue->preview_widget, audio_cue->media_start_time + run_action_time);
	}
}

// Sets up the properties tabs for an audio cue
static void stack_audio_cue_set_tabs(StackCue *cue, GtkNotebook *notebook)
{
	StackAudioCue *scue = STACK_AUDIO_CUE(cue);

	// Create the tab
	GtkWidget *label = gtk_label_new("Media");

	// Load the UI
	GtkBuilder *builder = gtk_builder_new_from_file("StackAudioCue.ui");
	scue->builder = builder;
	scue->media_tab = GTK_WIDGET(gtk_builder_get_object(builder, "acpGrid"));

	// We keep the preview widget and re-add it to the UI each time so as not
	// to need to reload the preview every time
	if (scue->preview_widget == NULL)
	{
		scue->preview_widget = STACK_AUDIO_PREVIEW(stack_audio_preview_new());
		if (scue->file && scue->playback_file)
		{
			stack_audio_preview_set_file(scue->preview_widget, scue->file);
			stack_audio_preview_set_view_range(scue->preview_widget, 0, scue->playback_file->length);
			stack_audio_preview_set_selection(scue->preview_widget, scue->media_start_time, scue->media_end_time);
			stack_audio_preview_show_playback(scue->preview_widget, cue->state >= STACK_CUE_STATE_PLAYING_PRE && cue->state <= STACK_CUE_STATE_PLAYING_POST);
		}

		// Stop it from being GC'd
		g_object_ref(scue->preview_widget);
	}

	// Put the custom preview widget in the UI
	gtk_widget_set_visible(GTK_WIDGET(scue->preview_widget), true);
	gtk_box_pack_start(GTK_BOX(gtk_builder_get_object(builder, "acpPreviewBox")), GTK_WIDGET(scue->preview_widget), true, true, 0);

	// Set up callbacks
	gtk_builder_add_callback_symbol(builder, "acp_file_changed", G_CALLBACK(acp_file_changed));
	gtk_builder_add_callback_symbol(builder, "acp_trim_start_changed", G_CALLBACK(acp_trim_start_changed));
	gtk_builder_add_callback_symbol(builder, "acp_trim_end_changed", G_CALLBACK(acp_trim_end_changed));
	gtk_builder_add_callback_symbol(builder, "acp_loops_changed", G_CALLBACK(acp_loops_changed));
	gtk_builder_add_callback_symbol(builder, "acp_volume_changed", G_CALLBACK(acp_volume_changed));

	// Apply input limiting
	stack_limit_gtk_entry_time(GTK_ENTRY(gtk_builder_get_object(builder, "acpTrimStart")), false);
	stack_limit_gtk_entry_time(GTK_ENTRY(gtk_builder_get_object(builder, "acpTrimEnd")), false);
	stack_limit_gtk_entry_int(GTK_ENTRY(gtk_builder_get_object(builder, "acpLoops")), true);

	// Connect the signals
	gtk_builder_connect_signals(builder, (gpointer)cue);

	// Add an extra reference to the media tab - we're about to remove it's
	// parent and we don't want it to get garbage collected
	g_object_ref(scue->media_tab);

	// The tab has a parent window in the UI file - unparent the tab container from it
	gtk_widget_unparent(scue->media_tab);

	// Append the tab (and show it, because it starts off hidden...)
	gtk_notebook_append_page(notebook, scue->media_tab, label);
	gtk_widget_show(scue->media_tab);

	// Set the values: file
	if (strlen(scue->file) != 0)
	{
		// Set the filename
		gtk_file_chooser_set_uri(GTK_FILE_CHOOSER(gtk_builder_get_object(builder, "acpFile")), scue->file);

		// Display the file length
		char time_buffer[32], text_buffer[128];
		stack_format_time_as_string(scue->playback_file->length, time_buffer, 32);
		snprintf(text_buffer, 128, "(Length is %s)", time_buffer);
		gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(builder, "acpFileLengthLabel")), text_buffer);
	}

	// Set the values: volume
	char buffer[32];
	if (scue->play_volume < -49.99)
	{
		snprintf(buffer, 32, "-Inf dB");
	}
	else
	{
		snprintf(buffer, 32, "%.2f dB", scue->play_volume);
	}
	gtk_range_set_value(GTK_RANGE(gtk_builder_get_object(builder, "acpVolume")), scue->play_volume);
	gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(builder, "acpVolumeValueLabel")), buffer);

	// Set the values: trim start
	stack_format_time_as_string(scue->media_start_time, buffer, 32);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(builder, "acpTrimStart")), buffer);

	// Set the values: trim end
	stack_format_time_as_string(scue->media_end_time, buffer, 32);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(builder, "acpTrimEnd")), buffer);

	// Set the values: loops
	snprintf(buffer, 32, "%d", scue->loops);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(builder, "acpLoops")), buffer);
}

// Removes the properties tabs for an audio cue
static void stack_audio_cue_unset_tabs(StackCue *cue, GtkNotebook *notebook)
{
	// Find our media page
	gint page = gtk_notebook_page_num(notebook, ((StackAudioCue*)cue)->media_tab);

	// If we've found the page, remove it
	if (page >= 0)
	{
		gtk_notebook_remove_page(notebook, page);
	}

	// We don't need the top level window, so destroy it (GtkBuilder doesn't
	// destroy top-level windows itself)
	gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(((StackAudioCue*)cue)->builder, "window1")));

	// Destroy the builder
	g_object_unref(((StackAudioCue*)cue)->builder);
	g_object_unref(((StackAudioCue*)cue)->media_tab);

	// Be tidy
	((StackAudioCue*)cue)->builder = NULL;
	((StackAudioCue*)cue)->media_tab = NULL;
}

static char *stack_audio_cue_to_json(StackCue *cue)
{
	StackAudioCue *acue = STACK_AUDIO_CUE(cue);

	// Build JSON
	Json::Value cue_root;
	cue_root["file"] = acue->file;
	cue_root["media_start_time"] = (Json::Int64)acue->media_start_time;
	cue_root["media_end_time"] = (Json::Int64)acue->media_end_time;
	cue_root["loops"] = (Json::Int64)acue->loops;
	if (std::isfinite(acue->play_volume))
	{
		cue_root["play_volume"] = acue->play_volume;
	}
	else
	{
		cue_root["play_volume"] = "-Infinite";
	}

	// Write out JSON string and return (to be free'd by stack_audio_cue_free_json)
	Json::FastWriter writer;
	return strdup(writer.write(cue_root).c_str());
}

static void stack_audio_cue_free_json(char *json_data)
{
	free(json_data);
}

// Re-initialises this cue from JSON Data
void stack_audio_cue_from_json(StackCue *cue, const char *json_data)
{
	Json::Value cue_root;
	Json::Reader reader;

	// Parse JSON data
	reader.parse(json_data, json_data + strlen(json_data), cue_root, false);

	// Get the data that's pertinent to us
	if (!cue_root.isMember("StackAudioCue"))
	{
		stack_log("stack_audio_cue_from_json(): Missing StackFadeCue class\n");
		return;
	}

	Json::Value& cue_data = cue_root["StackAudioCue"];

	// Load in our audio file, and set the state based on whether it succeeds
	if (stack_audio_cue_set_file(STACK_AUDIO_CUE(cue), cue_data["file"].asString().c_str()))
	{
		stack_cue_set_state(cue, STACK_CUE_STATE_STOPPED);
	}
	else
	{
		stack_cue_set_state(cue, STACK_CUE_STATE_ERROR);
	}

	// Load media start and end times
	if (cue_data.isMember("media_start_time"))
	{
		STACK_AUDIO_CUE(cue)->media_start_time = cue_data["media_start_time"].asInt64();
	}
	else
	{
		STACK_AUDIO_CUE(cue)->media_start_time = 0;
	}
	if (cue_data.isMember("media_end_time"))
	{
		STACK_AUDIO_CUE(cue)->media_end_time = cue_data["media_end_time"].asInt64();
	}
	else
	{
		STACK_AUDIO_CUE(cue)->media_end_time = STACK_AUDIO_CUE(cue)->playback_file->length;
	}
	stack_cue_set_action_time(STACK_CUE(cue), STACK_AUDIO_CUE(cue)->media_end_time - STACK_AUDIO_CUE(cue)->media_start_time);

	// Load loops
	if (cue_data.isMember("loops"))
	{
		STACK_AUDIO_CUE(cue)->loops = (int32_t)cue_data["loops"].asInt64();
	}
	else
	{
		STACK_AUDIO_CUE(cue)->loops = 1;
	}

	// Load playback volume
	if (cue_data.isMember("play_volume"))
	{
		if (cue_data["play_volume"].isString() && cue_data["play_volume"].asString() == "-Infinite")
		{
			STACK_AUDIO_CUE(cue)->play_volume = -INFINITY;
		}
		else
		{
			STACK_AUDIO_CUE(cue)->play_volume = cue_data["play_volume"].asDouble();
		}
	}
	else
	{
		STACK_AUDIO_CUE(cue)->play_volume = 0.0;
	}
}

/// Gets the error message for the cue
void stack_audio_cue_get_error(StackCue *cue, char *message, size_t size)
{
	if (STACK_AUDIO_CUE(cue)->file == NULL || strlen(STACK_AUDIO_CUE(cue)->file) == 0)
	{
		snprintf(message, size, "No audio file selected");
	}
	if (STACK_AUDIO_CUE(cue)->playback_file == NULL)
	{
		snprintf(message, size, "Invalid audio file");
	}
	else
	{
		strncpy(message, "", size);
	}
}

/// Returns which cuelist channels the cue is actively wanting to send audio to
size_t stack_audio_cue_get_active_channels(StackCue *cue, bool *channels)
{
	// If we're not in playback then we're not sending data
	if (cue->state != STACK_CUE_STATE_PLAYING_ACTION)
	{
		return 0;
	}
	else
	{
		// TODO  Do this properly once we've implemented cross-points
		if (channels != NULL)
		{
			channels[0] = true;
			channels[1] = true;
		}
		return 2;
	}
}

/// Returns audio
size_t stack_audio_cue_get_audio(StackCue *cue, float *buffer, size_t frames)
{
	// We don't need to do anything if we're not in the playing state
	if (cue->state != STACK_CUE_STATE_PLAYING_ACTION)
	{
		return 0;
	}

	// For tidiness
	StackAudioCue *audio_cue = STACK_AUDIO_CUE(cue);

	// Calculate audio scalar (using the live playback volume). Also use this to
	// scale from 16-bit signed int to 0.0-1.0 range
	float base_audio_scaler = stack_db_to_scalar(audio_cue->playback_live_volume);

	size_t frames_to_return = 0;

	if (audio_cue->resampler == NULL)
	{
		// Get some more data from the file
		frames_to_return = stack_audio_file_read(audio_cue->playback_file, buffer, frames);
	}
	else
	{
		bool eof = false;
		size_t frames_read = 0;

		size_t current_size = stack_resampler_get_buffered_size(audio_cue->resampler);
		while (current_size < frames && !eof)
		{
			// Get some more data from the file
			frames_read = stack_audio_file_read(audio_cue->playback_file, buffer, frames);

			// Give it to the resampler for resampling
			current_size = stack_resampler_push(audio_cue->resampler, buffer, frames_read);

			// If we've hit the end of the file, stop reading
			if (frames_read < frames)
			{
				eof = true;
			}
		}

		// Keep pushing NULLs to the resampler when there's no more data
		// so that it finishes resampling what it has
		if (eof)
		{
			current_size = stack_resampler_push(audio_cue->resampler, NULL, 0);
		}

		// Determine how much data we can output
		size_t fetch_frames = current_size < frames ? current_size : frames;

		// Get the samples out of the resampler
		frames_to_return = stack_resampler_get_frames(audio_cue->resampler, buffer, fetch_frames);
	}

	// Determine how many samles we got
	size_t samples = frames_to_return * audio_cue->playback_file->channels;

	// Do some scaling for volume
	for (size_t i = 0; i < samples; i++)
	{
		buffer[i] *= base_audio_scaler;
	}

	// TODO: This is only temporary, but for mono files, remap them to
	// two channel for now
	if (audio_cue->playback_file->channels == 1 && frames_to_return >= 1)
	{
		for (size_t i = frames_to_return - 1; i >= 1; i--)
		{
			buffer[i * 2] = buffer[i];
			buffer[i * 2 + 1] = buffer[i];
		}
	}

	return frames_to_return;
}

const char *stack_audio_cue_get_field_base(StackCue *cue, const char *field)
{
	if (strcmp(field, "filepath") == 0)
	{
		if (strlen(STACK_AUDIO_CUE(cue)->file) == 0)
		{
			return "<no file selected>";
		}

		return STACK_AUDIO_CUE(cue)->file;
	}
	else if (strcmp(field, "filename") == 0)
	{
		if (strlen(STACK_AUDIO_CUE(cue)->short_filename) == 0)
		{
			return "<no file selected>";
		}

		char *last_slash = strrchr(STACK_AUDIO_CUE(cue)->short_filename, '/');
		if (last_slash != NULL)
		{
			return &last_slash[1];
		}
		else
		{
			return STACK_AUDIO_CUE(cue)->short_filename;
		}
	}

	// Call the super class if we didn't return anything
	return stack_cue_get_field_base(cue, field);
}

// Registers StackAudioCue with the application
void stack_audio_cue_register()
{
	// Register cue types
	StackCueClass* audio_cue_class = new StackCueClass{ "StackAudioCue", "StackCue", stack_audio_cue_create, stack_audio_cue_destroy, stack_audio_cue_play, NULL, stack_audio_cue_stop, stack_audio_cue_pulse, stack_audio_cue_set_tabs, stack_audio_cue_unset_tabs, stack_audio_cue_to_json, stack_audio_cue_free_json, stack_audio_cue_from_json, stack_audio_cue_get_error, stack_audio_cue_get_active_channels, stack_audio_cue_get_audio, stack_audio_cue_get_field_base };
	stack_register_cue_class(audio_cue_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_audio_cue_register();
	return true;
}
