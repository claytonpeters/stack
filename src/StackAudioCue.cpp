// Includes:
#include "StackApp.h"
#include "StackLog.h"
#include "StackAudioCue.h"
#include "StackAudioLevelsTab.h"
#include "StackGtkHelper.h"
#include "StackJson.h"
#include "MPEGAudioFile.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <time.h>

// Global: The folder the last file-chooser dialog was in
static gchar *last_file_chooser_folder = NULL;

// Global: A single instance of our builder so we don't have to keep reloading
// it every time we change the selected cue
static GtkBuilder *sac_builder = NULL;

// Global: A single instace of our icon
static GdkPixbuf *icon = NULL;

// Pre-define this:
StackProperty *stack_audio_cue_get_volume_property(StackCue *cue, size_t channel, bool create);

typedef void (*ToggleButtonCallback)(GtkToggleButton*, gpointer);

static void stack_audio_cue_update_action_time(StackAudioCue *cue)
{
	// Re-calculate the action time
	stack_time_t media_start_time = 0, media_end_time = 0, action_time;
	stack_property_get_int64(stack_cue_get_property(STACK_CUE(cue), "media_start_time"), STACK_PROPERTY_VERSION_DEFINED, &media_start_time);
	stack_property_get_int64(stack_cue_get_property(STACK_CUE(cue), "media_end_time"), STACK_PROPERTY_VERSION_DEFINED, &media_end_time);
	if (media_end_time > media_start_time)
	{
		double rate = 1.0;
		stack_property_get_double(stack_cue_get_property(STACK_CUE(cue), "rate"), STACK_PROPERTY_VERSION_DEFINED, &rate);

		action_time = (stack_time_t)((double)(media_end_time - media_start_time) / rate);
	}
	else
	{
		action_time = 0;
	}

	stack_cue_set_action_time(STACK_CUE(cue), action_time);
}

static void stack_audio_cue_ccb_file(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		size_t old_channels = 0, new_channels = 0;
		StackAudioCue* cue = STACK_AUDIO_CUE(user_data);

		// Update the cue state so we're not trying to use the audiofile whilst
		// we're changing it
		stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_STOPPED);

		// Tidy up the existing file
		if (cue->playback_file != NULL)
		{
			old_channels = cue->playback_file->channels;
			stack_audio_file_destroy(cue->playback_file);
		}

		// Attempt to create the new audiofile object
		char *uri = NULL;
		stack_property_get_string(property, STACK_PROPERTY_VERSION_DEFINED, &uri);
		StackAudioFile *new_playback_file = stack_audio_file_create(uri);

		// Store the new file (regardless of whether it succeeded)
		cue->playback_file = new_playback_file;

		// Store the filename
		free(cue->short_filename);
		cue->short_filename = g_filename_from_uri(uri, NULL, NULL);

		stack_time_t new_file_length = 0;

		// If it didn't succeed....
		if (new_playback_file == NULL)
		{
			// Set the state to error
			stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_ERROR);
		}
		else
		{
			// Grab the new file length and channel count
			new_file_length = new_playback_file->length;
			new_channels = new_playback_file->channels;
		}

		// Reset the media start/end times to the whole file
		stack_property_set_int64(stack_cue_get_property(STACK_CUE(cue), "media_start_time"), STACK_PROPERTY_VERSION_DEFINED, 0);
		stack_property_set_int64(stack_cue_get_property(STACK_CUE(cue), "media_end_time"), STACK_PROPERTY_VERSION_DEFINED, new_file_length);

		// Update the cue action time;
		stack_audio_cue_update_action_time(cue);

		// Reset the audio preview to the full new file
		if (cue->preview_widget)
		{
			stack_audio_preview_set_file(cue->preview_widget, uri);
			stack_audio_preview_set_view_range(cue->preview_widget, 0, new_file_length);
			stack_audio_preview_set_selection(cue->preview_widget, 0, new_file_length);
		}

		// Fire an updated-selected-cue signal to signal the UI to change
		if (cue->media_tab)
		{
			StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->media_tab));
			g_signal_emit_by_name((gpointer)window, "update-selected-cue");
		}

		// Update tabs if we're active
		if (cue->levels_tab)
		{
			size_t input_channels = 0;
			if (cue->playback_file != NULL)
			{
				input_channels = cue->playback_file->channels;
			}

			stack_audio_levels_tab_populate(cue->levels_tab, input_channels, STACK_CUE(cue)->parent->channels, true, cue->affect_live, (GCallback)(ToggleButtonCallback)[](GtkToggleButton *self, gpointer user_data) -> void {
				StackAudioCue *cue = STACK_AUDIO_CUE(user_data);
				cue->affect_live = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self));
			});
		}

		// Determine what properties we need to create/destroy
		if (new_channels > old_channels)
		{
			// Create new properties
			for (size_t channel = old_channels; channel < new_channels; channel++)
			{
				stack_audio_cue_get_volume_property(STACK_CUE(cue), channel + 1, true);
			}
		}
		else if (new_channels < old_channels)
		{
			// Delete some old properties
			for (size_t channel = new_channels; channel < old_channels; channel++)
			{
				StackProperty *property = stack_audio_cue_get_volume_property(STACK_CUE(cue), channel + 1, false);
				stack_cue_remove_property(STACK_CUE(cue), property->name);
				stack_property_destroy(property);
			}
		}
	}
}

static void stack_audio_cue_ccb_media_time(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackAudioCue* cue = STACK_AUDIO_CUE(user_data);

		// Notify cue list that we've changed
		stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue), property);

		// The action time needs recalculating
		stack_audio_cue_update_action_time(cue);

		// Fire an updated-selected-cue signal to signal the UI to change
		if (cue->media_tab)
		{
			// Get the updated times
			stack_time_t media_start_time = 0, media_end_time = 0;
			stack_property_get_int64(stack_cue_get_property(STACK_CUE(cue), "media_start_time"), STACK_PROPERTY_VERSION_DEFINED, &media_start_time);
			stack_property_get_int64(stack_cue_get_property(STACK_CUE(cue), "media_end_time"), STACK_PROPERTY_VERSION_DEFINED, &media_end_time);

			// Update the audio preview selection
			stack_audio_preview_set_selection(cue->preview_widget, media_start_time, media_end_time);

			// Update the entry boxes
			char buffer[32];
			stack_format_time_as_string(media_start_time, buffer, 32);
			gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(sac_builder, "acpTrimStart")), buffer);
			stack_format_time_as_string(media_end_time, buffer, 32);
			gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(sac_builder, "acpTrimEnd")), buffer);

			// Update the UI
			StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->media_tab));
			g_signal_emit_by_name((gpointer)window, "update-selected-cue");
		}
	}
}

static void stack_audio_cue_ccb_loops(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackAudioCue* cue = STACK_AUDIO_CUE(user_data);

		// Notify cue list that we've changed
		stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue), property);

		// Fire an updated-selected-cue signal to signal the UI to change
		if (cue->media_tab)
		{
			// Update the UI to reformat
			int32_t loops = 1;
			stack_property_get_int32(stack_cue_get_property(STACK_CUE(cue), "loops"), STACK_PROPERTY_VERSION_DEFINED, &loops);
			char buffer[32];
			snprintf(buffer, 32, "%d", loops);
			gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(sac_builder, "acpLoops")), buffer);

			StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->media_tab));
			g_signal_emit_by_name((gpointer)window, "update-selected-cue");
		}
	}
}

// This is used by master, per-channel and crosspoints
static void stack_audio_cue_ccb_volume(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackAudioCue* cue = STACK_AUDIO_CUE(user_data);

		// Notify cue list that we've changed
		stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue), property);

		// Fire an updated-selected-cue signal to signal the UI to change
		if (cue->media_tab)
		{
			StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->media_tab));
			g_signal_emit_by_name((gpointer)window, "update-selected-cue");
		}

		if (cue->affect_live)
		{
			double volume = 0.0f;
			stack_property_get_double(property, STACK_PROPERTY_VERSION_DEFINED, &volume);
			stack_property_set_double(property, STACK_PROPERTY_VERSION_LIVE, volume);
		}
	}
}

static void stack_audio_cue_ccb_rate(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackAudioCue* cue = STACK_AUDIO_CUE(user_data);

		// Notify cue list that we've changed
		stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue), property);

		// The action time needs recalculating
		stack_audio_cue_update_action_time(cue);

		// Fire an updated-selected-cue signal to signal the UI to change
		if (cue->media_tab)
		{
			StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->media_tab));
			g_signal_emit_by_name((gpointer)window, "update-selected-cue");

			double rate = 1.0;
			stack_property_get_double(property, STACK_PROPERTY_VERSION_DEFINED, &rate);

			char buffer[32];
			snprintf(buffer, 32, "%.2f", rate);
			gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(sac_builder, "acpRate")), buffer);
		}
	}
}

int64_t stack_audio_cue_validate_media_start_time(StackPropertyUInt64 *property, StackPropertyVersion version, const int64_t value, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackAudioCue* cue = STACK_AUDIO_CUE(user_data);

		// Can't be past the end of the file
		if (cue->playback_file && value > cue->playback_file->length)
		{
			return cue->playback_file->length;
		}

		// Can't be greater than our end time
		stack_time_t media_end_time = 0;
		stack_property_get_int64(stack_cue_get_property(STACK_CUE(cue), "media_end_time"), STACK_PROPERTY_VERSION_DEFINED, &media_end_time);
		if (value > media_end_time)
		{
			return media_end_time;
		}
	}

	return value;
}

int64_t stack_audio_cue_validate_media_end_time(StackPropertyUInt64 *property, StackPropertyVersion version, const int64_t value, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackAudioCue* cue = STACK_AUDIO_CUE(user_data);

		// Can't be past the end of the file
		if (cue->playback_file && value > cue->playback_file->length)
		{
			return cue->playback_file->length;
		}

		// Can't be earlier than our start time
		stack_time_t media_start_time = 0;
		stack_property_get_int64(stack_cue_get_property(STACK_CUE(cue), "media_start_time"), STACK_PROPERTY_VERSION_DEFINED, &media_start_time);
		if (value < media_start_time)
		{
			return media_start_time;
		}
	}

	return value;
}

double stack_audio_cue_validate_volume(StackPropertyDouble *property, StackPropertyVersion version, const double value, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackAudioCue* cue = STACK_AUDIO_CUE(user_data);

		// We count anything less than -60dB as silence
		if (value < -59.99)
		{
			return -INFINITY;
		}
	}

	return value;
}

double stack_audio_cue_validate_rate(StackPropertyDouble *property, StackPropertyVersion version, const double value, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackAudioCue* cue = STACK_AUDIO_CUE(user_data);

		// Don't go below 0.01x rate
		if (value < 0.01)
		{
			return 0.01;
		}
	}

	return value;
}

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
	cue->short_filename = (char*)strdup("");
	cue->media_tab = NULL;
	cue->levels_tab = NULL;
	cue->master_scale = NULL;
	cue->channel_scales = NULL;
	cue->affect_live = false;
	cue->playback_buffer = NULL;
	cue->playback_buffer_frames = 0;

	// Add our properties
	StackProperty *file = stack_property_create("file", STACK_PROPERTY_TYPE_STRING);
	stack_cue_add_property(STACK_CUE(cue), file);
	stack_property_set_changed_callback(file, stack_audio_cue_ccb_file, (void*)cue);

	StackProperty *media_start_time = stack_property_create("media_start_time", STACK_PROPERTY_TYPE_INT64);
	stack_cue_add_property(STACK_CUE(cue), media_start_time);
	stack_property_set_changed_callback(media_start_time, stack_audio_cue_ccb_media_time, (void*)cue);
	stack_property_set_validator(media_start_time, (stack_property_validator_t)stack_audio_cue_validate_media_start_time, (void*)cue);

	StackProperty *media_end_time = stack_property_create("media_end_time", STACK_PROPERTY_TYPE_INT64);
	stack_cue_add_property(STACK_CUE(cue), media_end_time);
	stack_property_set_changed_callback(media_end_time, stack_audio_cue_ccb_media_time, (void*)cue);
	stack_property_set_validator(media_end_time, (stack_property_validator_t)stack_audio_cue_validate_media_end_time, (void*)cue);

	StackProperty *loops = stack_property_create("loops", STACK_PROPERTY_TYPE_INT32);
	stack_cue_add_property(STACK_CUE(cue), loops);
	stack_property_set_int32(loops, STACK_PROPERTY_VERSION_DEFINED, 1);
	stack_property_set_changed_callback(loops, stack_audio_cue_ccb_loops, (void*)cue);

	StackProperty *volume = stack_property_create("master_volume", STACK_PROPERTY_TYPE_DOUBLE);
	stack_cue_add_property(STACK_CUE(cue), volume);
	stack_property_set_changed_callback(volume, stack_audio_cue_ccb_volume, (void*)cue);
	stack_property_set_validator(volume, (stack_property_validator_t)stack_audio_cue_validate_volume, (void*)cue);

	StackProperty *rate = stack_property_create("rate", STACK_PROPERTY_TYPE_DOUBLE);
	stack_cue_add_property(STACK_CUE(cue), rate);
	stack_property_set_double(rate, STACK_PROPERTY_VERSION_DEFINED, 1.0);
	stack_property_set_changed_callback(rate, stack_audio_cue_ccb_rate, (void*)cue);
	stack_property_set_validator(rate, (stack_property_validator_t)stack_audio_cue_validate_rate, (void*)cue);

	// Initialise our variables: preview
	cue->preview_widget = NULL;

	// Initialise our variables: playback
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

bool stack_audio_cue_set_file(StackAudioCue *cue, const char *uri)
{
	stack_property_set_string(stack_cue_get_property(STACK_CUE(cue), "file"), STACK_PROPERTY_VERSION_DEFINED, uri);
	return cue->playback_file != NULL;
}

static void acp_file_changed(GtkFileChooserButton *widget, gpointer user_data)
{
	// Handy pointers
	StackAudioCue *cue = STACK_AUDIO_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);
	GtkFileChooser *chooser = GTK_FILE_CHOOSER(widget);

	// Get the filename
	gchar* filename = gtk_file_chooser_get_filename(chooser);
	gchar* uri = gtk_file_chooser_get_uri(chooser);

	// Change the file
	if (stack_audio_cue_set_file(cue, uri))
	{
		// Store the last folder
		if (last_file_chooser_folder == NULL)
		{
			g_free(last_file_chooser_folder);
		}
		last_file_chooser_folder = gtk_file_chooser_get_current_folder(chooser);

		// Display the file length
		char time_buffer[32], text_buffer[128];
		stack_format_time_as_string(cue->playback_file->length, time_buffer, 32);
		snprintf(text_buffer, 128, "(Length is %s)", time_buffer);
		gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(sac_builder, "acpFileLengthLabel")), text_buffer);

		// Update the UI
		stack_time_t media_start_time = 0, media_end_time = 0;
		stack_property_get_int64(stack_cue_get_property(STACK_CUE(cue), "media_start_time"), STACK_PROPERTY_VERSION_DEFINED, &media_start_time);
		stack_property_get_int64(stack_cue_get_property(STACK_CUE(cue), "media_end_time"), STACK_PROPERTY_VERSION_DEFINED, &media_end_time);
		stack_format_time_as_string(media_start_time, time_buffer, 32);
		gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(sac_builder, "acpTrimStart")), time_buffer);
		stack_format_time_as_string(media_end_time, time_buffer, 32);
		gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(sac_builder, "acpTrimEnd")), time_buffer);
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
	StackAudioCue *cue = STACK_AUDIO_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);

	// Set the time (this will do the bounds checking in the validator)
	stack_time_t media_start_time = stack_time_string_to_ns(gtk_entry_get_text(GTK_ENTRY(widget)));
	stack_property_set_int64(stack_cue_get_property(STACK_CUE(cue), "media_start_time"), STACK_PROPERTY_VERSION_DEFINED, media_start_time);

	return false;
}

static gboolean acp_trim_end_changed(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	StackAudioCue *cue = STACK_AUDIO_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);

	// Set the time (this will do the bounds checking in the validator)
	stack_time_t media_end_time = stack_time_string_to_ns(gtk_entry_get_text(GTK_ENTRY(widget)));
	stack_property_set_int64(stack_cue_get_property(STACK_CUE(cue), "media_end_time"), STACK_PROPERTY_VERSION_DEFINED, media_end_time);

	return false;
}

static void acp_trim_preview_changed(StackAudioPreview *preview, guint64 start, guint64 end, gpointer user_data)
{
	StackAudioCue *cue = STACK_AUDIO_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(preview)))->selected_cue);

	// Set the time
	stack_property_set_int64(stack_cue_get_property(STACK_CUE(cue), "media_start_time"), STACK_PROPERTY_VERSION_DEFINED, start);
	stack_property_set_int64(stack_cue_get_property(STACK_CUE(cue), "media_end_time"), STACK_PROPERTY_VERSION_DEFINED, end);
}

static gboolean acp_loops_changed(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	StackAudioCue *cue = STACK_AUDIO_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);

	// Set the loops
	int32_t loops = atoi(gtk_entry_get_text(GTK_ENTRY(widget)));
	stack_property_set_int32(stack_cue_get_property(STACK_CUE(cue), "loops"), STACK_PROPERTY_VERSION_DEFINED, loops);

	return false;
}

static gboolean acp_rate_changed(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	StackAudioCue *cue = STACK_AUDIO_CUE(((StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(widget)))->selected_cue);

	// Set the loops
	double rate = atof(gtk_entry_get_text(GTK_ENTRY(widget)));
	stack_property_set_double(stack_cue_get_property(STACK_CUE(cue), "rate"), STACK_PROPERTY_VERSION_DEFINED, rate);

	return false;
}

// Note that channel is one-based, not zero
StackProperty *stack_audio_cue_get_volume_property(StackCue *cue, size_t channel, bool create)
{
	StackProperty *property = NULL;

	if (channel == 0)
	{
		// This one always exists
		property = stack_cue_get_property(STACK_CUE(cue), "master_volume");
	}
	else
	{
		// Create some new properties
		char property_name[64];
		snprintf(property_name, 64, "input_%lu_volume", channel);
		property = stack_cue_get_property(STACK_CUE(cue), property_name);

		// If the property does not yet exist, create it
		if (property == NULL && create)
		{
			property = stack_property_create(property_name, STACK_PROPERTY_TYPE_DOUBLE);
			stack_property_set_changed_callback(property, stack_audio_cue_ccb_volume, (void*)cue);
			stack_property_set_validator(property, (stack_property_validator_t)stack_audio_cue_validate_volume, (void*)cue);
			stack_cue_add_property(STACK_CUE(cue), property);
		}
	}

	return property;
}

StackProperty *stack_audio_cue_get_crosspoint_property(StackCue *cue, size_t input_channel, size_t output_channel, bool create)
{
	// Build property name
	char property_name[64];
	snprintf(property_name, 64, "crosspoint_%lu_%lu", output_channel, input_channel);

	// Get the property, creating it with a default value if it does not exist
	StackProperty *property = stack_cue_get_property(STACK_CUE(cue), property_name);
	if (property == NULL && create)
	{
		// Property does not exist - create it
		property = stack_property_create(property_name, STACK_PROPERTY_TYPE_DOUBLE);
		stack_property_set_changed_callback(property, stack_audio_cue_ccb_volume, (void*)cue);
		stack_property_set_validator(property, (stack_property_validator_t)stack_audio_cue_validate_volume, (void*)cue);

		// Set the value to 0.0 if the output channel is the same as the input
		// channels (i.e. create a one-to-one mapping), otherwise set it to -INFINITY.
		// One exception: if the file is single-channel, then it's more likely that
		// we're going to want to map to stereo, so do that
		double initial_value = -INFINITY;
		if (STACK_AUDIO_CUE(cue)->playback_file != NULL)
		{
			if (STACK_AUDIO_CUE(cue)->playback_file->channels == 1 && output_channel < 2)
			{
				initial_value = 0.0;
			}

			if (output_channel == input_channel)
			{
				initial_value = 0.0;
			}
		}
		stack_property_set_double(property, STACK_PROPERTY_VERSION_DEFINED, initial_value);
		stack_property_set_double(property, STACK_PROPERTY_VERSION_LIVE, initial_value);

		stack_cue_add_property(cue, property);
	}

	return property;
}

static double stack_audio_cue_get_crosspoint(StackAudioCue *cue, size_t input_channel, size_t output_channel, StackPropertyVersion version)
{
	double result;
	char property_name[64];

	// Build property name
	snprintf(property_name, 64, "crosspoint_%lu_%lu", output_channel, input_channel);

	// Get the property, creating it with a default value if it does not exist
	StackProperty *property = stack_audio_cue_get_crosspoint_property(STACK_CUE(cue), input_channel, output_channel, true);
	stack_property_get_double(property, version, &result);
	return result;
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

	// Ensure we have an audio device
	if (audio_cue->super.parent->audio_device == NULL)
	{
		// Set cue state to error
		stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_ERROR);
		return false;
	}

	// Initialise playback
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "file"));
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "media_start_time"));
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "media_end_time"));
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "loops"));
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "master_volume"));
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "rate"));
	for (size_t channel = 0; channel < audio_cue->playback_file->channels; channel++)
	{
		stack_property_copy_defined_to_live(stack_audio_cue_get_volume_property(cue, channel + 1, false));
		for (size_t output_channel = 0; output_channel < cue->parent->channels; output_channel++)
		{
			char property_name[64];
			snprintf(property_name, 64, "crosspoint_%lu_%lu", output_channel, channel);
			stack_property_copy_defined_to_live(stack_cue_get_property(cue, property_name));
		}
	}
	audio_cue->playback_loops = 0;

	// Get the custom playback rate
	double rate = 1.0;
	stack_property_get_double(stack_cue_get_property(cue, "rate"), STACK_PROPERTY_VERSION_LIVE, &rate);

	// If the sample rate of the file does not match the playback device, set
	// up a resampler
	int32_t playback_sample_rate = (int32_t)((double)audio_cue->playback_file->sample_rate * rate);
	if ((double)playback_sample_rate != audio_cue->super.parent->audio_device->sample_rate)
	{
		audio_cue->resampler = stack_resampler_create(playback_sample_rate, audio_cue->super.parent->audio_device->sample_rate, audio_cue->playback_file->channels);
	}

	// Get the start time
	stack_time_t media_start_time = 0;
	stack_property_get_int64(stack_cue_get_property(cue, "media_start_time"), STACK_PROPERTY_VERSION_LIVE, &media_start_time);

	// Seek to the right point in the file
	stack_audio_file_seek(audio_cue->playback_file, media_start_time);

	// Show the playback marker on the UI
	if (audio_cue->preview_widget != NULL)
	{
		stack_audio_preview_set_playback(audio_cue->preview_widget, media_start_time);
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

	// Tidy up the playback buffer
	if (audio_cue->playback_buffer != NULL)
	{
		delete [] audio_cue->playback_buffer;
		audio_cue->playback_buffer = NULL;
		audio_cue->playback_buffer_frames = 0;
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

	// Get the current action time and complete action time
	stack_time_t run_action_time = 0, cue_action_time = 0;
	stack_cue_get_running_times(cue, clocktime, NULL, &run_action_time, NULL, NULL, NULL, NULL);
	stack_property_get_int64(stack_cue_get_property(cue, "action_time"), STACK_PROPERTY_VERSION_LIVE, &cue_action_time);

	// Get the custom playback rate
	double rate = 1.0;
	stack_property_get_double(stack_cue_get_property(cue, "rate"), STACK_PROPERTY_VERSION_LIVE, &rate);

	// If we're in playback, but we've reached the end of our time
	if (cue->state == STACK_CUE_STATE_PLAYING_ACTION && run_action_time == cue_action_time)
	{
		bool loop = false;

		// Get the number of loops the user wanted
		uint32_t media_end_time = 0;
		int32_t loops = 1;
		stack_property_get_int32(stack_cue_get_property(cue, "loops"), STACK_PROPERTY_VERSION_LIVE, &loops);

		// Determine if we should loop
		if (loops <= 0)
		{
			loop = true;
		}
		else if (loops > 1)
		{
			audio_cue->playback_loops++;

			if (audio_cue->playback_loops < loops)
			{
				loop = true;
			}
		}

		// If we are looping, we need to reset the cue start
		if (loop)
		{
			stack_time_t cue_pre_time = 0;
			stack_time_t media_start_time = 0;
			stack_property_get_int64(stack_cue_get_property(cue, "pre_time"), STACK_PROPERTY_VERSION_LIVE, &cue_pre_time);
			stack_property_get_int64(stack_cue_get_property(cue, "media_start_time"), STACK_PROPERTY_VERSION_LIVE, &media_start_time);

			// Set the cue start time to now minus the amount of time the pre-
			// wait should have taken so it looks like we should have just
			// started the action
			cue->start_time = clocktime - cue_pre_time;

			// Reset any pause times
			cue->pause_time = 0;
			cue->paused_time = 0;
			cue->pause_paused_time = 0;

			// Seek back in the file
			stack_audio_file_seek(audio_cue->playback_file, media_start_time);

			// We need to reset the resampler as we may have told it
			// we're finished
			if (audio_cue->resampler)
			{
				stack_resampler_destroy(audio_cue->resampler);
				audio_cue->resampler = stack_resampler_create((int32_t)((double)audio_cue->playback_file->sample_rate * rate), audio_cue->super.parent->audio_device->sample_rate, audio_cue->playback_file->channels);
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
		stack_time_t media_start_time = 0;
		stack_property_get_int64(stack_cue_get_property(cue, "media_start_time"), STACK_PROPERTY_VERSION_LIVE, &media_start_time);
		audio_cue->preview_widget->last_redraw_time = stack_get_clock_time();
		stack_audio_preview_set_playback(audio_cue->preview_widget, media_start_time + (stack_time_t)((double)run_action_time) * rate);
	}
}

// Sets up the properties tabs for an audio cue
static void stack_audio_cue_set_tabs(StackCue *cue, GtkNotebook *notebook)
{
	StackAudioCue *audio_cue = STACK_AUDIO_CUE(cue);

	// Create the tabs
	GtkWidget *media_label = gtk_label_new("Media");
	GtkWidget *levels_label = gtk_label_new("Levels");

	// Load the UI (if we haven't already)
	if (sac_builder == NULL)
	{
		sac_builder = gtk_builder_new_from_resource("/org/stack/ui/StackAudioCue.ui");

		// Set up callbacks
		gtk_builder_add_callback_symbol(sac_builder, "acp_file_changed", G_CALLBACK(acp_file_changed));
		gtk_builder_add_callback_symbol(sac_builder, "acp_trim_start_changed", G_CALLBACK(acp_trim_start_changed));
		gtk_builder_add_callback_symbol(sac_builder, "acp_trim_end_changed", G_CALLBACK(acp_trim_end_changed));
		gtk_builder_add_callback_symbol(sac_builder, "acp_loops_changed", G_CALLBACK(acp_loops_changed));
		gtk_builder_add_callback_symbol(sac_builder, "acp_rate_changed", G_CALLBACK(acp_rate_changed));

		// Apply input limiting
		stack_limit_gtk_entry_time(GTK_ENTRY(gtk_builder_get_object(sac_builder, "acpTrimStart")), false);
		stack_limit_gtk_entry_time(GTK_ENTRY(gtk_builder_get_object(sac_builder, "acpTrimEnd")), false);
		stack_limit_gtk_entry_int(GTK_ENTRY(gtk_builder_get_object(sac_builder, "acpLoops")), true);
		stack_limit_gtk_entry_float(GTK_ENTRY(gtk_builder_get_object(sac_builder, "acpRate")), false);

		// Connect the signals
		gtk_builder_connect_signals(sac_builder, NULL);
	}
	audio_cue->media_tab = GTK_WIDGET(gtk_builder_get_object(sac_builder, "acpGrid"));

	// Extract some properties
	char *file = NULL;
	stack_time_t media_start_time = 0, media_end_time = 0;
	double volume = 0.0;
	int32_t loops = 1;
	double rate = 1.0;
	stack_property_get_int32(stack_cue_get_property(cue, "loops"), STACK_PROPERTY_VERSION_DEFINED, &loops);
	stack_property_get_double(stack_cue_get_property(cue, "master_volume"), STACK_PROPERTY_VERSION_DEFINED, &volume);
	stack_property_get_string(stack_cue_get_property(cue, "file"), STACK_PROPERTY_VERSION_DEFINED, &file);
	stack_property_get_int64(stack_cue_get_property(cue, "media_start_time"), STACK_PROPERTY_VERSION_DEFINED, &media_start_time);
	stack_property_get_int64(stack_cue_get_property(cue, "media_end_time"), STACK_PROPERTY_VERSION_DEFINED, &media_end_time);
	stack_property_get_double(stack_cue_get_property(cue, "rate"), STACK_PROPERTY_VERSION_DEFINED, &rate);

	// We keep the preview widget and re-add it to the UI each time so as not
	// to need to reload the preview every time
	if (audio_cue->preview_widget == NULL)
	{
		audio_cue->preview_widget = STACK_AUDIO_PREVIEW(stack_audio_preview_new());

		if (file && audio_cue->playback_file)
		{
			stack_audio_preview_set_file(audio_cue->preview_widget, file);
			stack_audio_preview_set_view_range(audio_cue->preview_widget, 0, audio_cue->playback_file->length);
			stack_audio_preview_set_selection(audio_cue->preview_widget, media_start_time, media_end_time);
			stack_audio_preview_show_playback(audio_cue->preview_widget, cue->state >= STACK_CUE_STATE_PLAYING_PRE && cue->state <= STACK_CUE_STATE_PLAYING_POST);
		}

		// Connect signal
		g_signal_connect(audio_cue->preview_widget, "selection-changed", G_CALLBACK(acp_trim_preview_changed), cue);

		// Stop it from being GC'd
		g_object_ref(audio_cue->preview_widget);
	}

	// Put the custom preview widget in the UI
	gtk_widget_set_visible(GTK_WIDGET(audio_cue->preview_widget), true);
	gtk_widget_set_size_request(GTK_WIDGET(audio_cue->preview_widget), 100, 125);
	gtk_box_pack_start(GTK_BOX(gtk_builder_get_object(sac_builder, "acpPreviewBox")), GTK_WIDGET(audio_cue->preview_widget), true, true, 0);

	// Use the last selected folder as the default location for the file chooser
	if (last_file_chooser_folder != NULL)
	{
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(gtk_builder_get_object(sac_builder, "acpFile")), last_file_chooser_folder);
	}

	// Add an extra reference to the media tab - we're about to remove it's
	// parent and we don't want it to get garbage collected
	g_object_ref(audio_cue->media_tab);

	// The tab has a parent window in the UI file - unparent the tab container from it
	gtk_widget_unparent(audio_cue->media_tab);

	// Append the tab (and show it, because it starts off hidden...)
	audio_cue->levels_tab = stack_audio_levels_tab_new(cue, stack_audio_cue_get_volume_property, stack_audio_cue_get_crosspoint_property);
	gtk_notebook_append_page(notebook, audio_cue->media_tab, media_label);
	gtk_notebook_append_page(notebook, audio_cue->levels_tab->root, levels_label);
	size_t input_channels = 0;
	if (audio_cue->playback_file != NULL)
	{
		input_channels = audio_cue->playback_file->channels;
	}
	stack_audio_levels_tab_populate(audio_cue->levels_tab, input_channels, cue->parent->channels, true, STACK_AUDIO_CUE(cue)->affect_live, (GCallback)(ToggleButtonCallback)[](GtkToggleButton *self, gpointer user_data) -> void {
		StackAudioCue *cue = STACK_AUDIO_CUE(user_data);
		cue->affect_live = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self));
	});
	gtk_widget_show(audio_cue->media_tab);
	gtk_widget_show(audio_cue->levels_tab->root);

	// Set the values: file
	if (file && strlen(file) != 0)
	{
		// Set the filename
		gtk_file_chooser_set_uri(GTK_FILE_CHOOSER(gtk_builder_get_object(sac_builder, "acpFile")), file);

		if (audio_cue->playback_file != NULL)
		{
			// Display the file length
			char time_buffer[32], text_buffer[128];
			stack_format_time_as_string(audio_cue->playback_file->length, time_buffer, 32);
			snprintf(text_buffer, 128, "(Length is %s)", time_buffer);
			gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(sac_builder, "acpFileLengthLabel")), text_buffer);
		}
	}
	else
	{
		gtk_file_chooser_set_uri(GTK_FILE_CHOOSER(gtk_builder_get_object(sac_builder, "acpFile")), "");
	}

	// Set the values: trim start
	char buffer[32];
	stack_format_time_as_string(media_start_time, buffer, 32);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(sac_builder, "acpTrimStart")), buffer);

	// Set the values: trim end
	stack_format_time_as_string(media_end_time, buffer, 32);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(sac_builder, "acpTrimEnd")), buffer);

	// Set the values: loops
	snprintf(buffer, 32, "%d", loops);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(sac_builder, "acpLoops")), buffer);

	// Set the values: loops
	snprintf(buffer, 32, "%.2f", rate);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(sac_builder, "acpRate")), buffer);
}

// Removes the properties tabs for an audio cue
static void stack_audio_cue_unset_tabs(StackCue *cue, GtkNotebook *notebook)
{
	StackAudioCue *audio_cue = STACK_AUDIO_CUE(cue);

	// Find our media page
	gint page = gtk_notebook_page_num(notebook, audio_cue->media_tab);

	// If we've found the page, remove it
	if (page >= 0)
	{
		gtk_notebook_remove_page(notebook, page);
	}

	// Find our levels page
	page = gtk_notebook_page_num(notebook, audio_cue->levels_tab->root);

	// If we've found the page, remove it
	if (page >= 0)
	{
		gtk_notebook_remove_page(notebook, page);
	}

	// Destroy the levels tab
	stack_audio_levels_tab_destroy(audio_cue->levels_tab);
	audio_cue->levels_tab = NULL;

	// Remove the preview from the UI as we re-use it, and if we don't remove
	// it seems to get stuck parented to a non-existent object and thus never
	// works again
	gtk_container_remove(GTK_CONTAINER(gtk_builder_get_object(sac_builder, "acpPreviewBox")), GTK_WIDGET(audio_cue->preview_widget));

	// Be tidy
	g_object_unref(audio_cue->media_tab);
	audio_cue->media_tab = NULL;
	if (audio_cue->channel_scales != NULL)
	{
		delete [] audio_cue->channel_scales;
		audio_cue->channel_scales = NULL;
	}
	if (audio_cue->channel_values != NULL)
	{
		delete [] audio_cue->channel_values;
		audio_cue->channel_values = NULL;
	}
}

static char *stack_audio_cue_to_json(StackCue *cue)
{
	StackAudioCue *audio_cue = STACK_AUDIO_CUE(cue);

	// Build JSON
	Json::Value cue_root;
	stack_property_write_json(stack_cue_get_property(cue, "file"), &cue_root);
	stack_property_write_json(stack_cue_get_property(cue, "media_start_time"), &cue_root);
	stack_property_write_json(stack_cue_get_property(cue, "media_end_time"), &cue_root);
	stack_property_write_json(stack_cue_get_property(cue, "loops"), &cue_root);
	stack_property_write_json(stack_cue_get_property(cue, "rate"), &cue_root);
	stack_property_write_json(stack_cue_get_property(cue, "master_volume"), &cue_root);

	// If we've got a file
	size_t input_channels = 0;
	if (audio_cue->playback_file != NULL)
	{
		input_channels = audio_cue->playback_file->channels;
		cue_root["_last_input_channels"] = (int32_t)input_channels;
	}

	// Write channel volumes to JSON
	StackProperty *ch_vol_prop = NULL;
	for (size_t channel = 1; channel <= input_channels; channel++)
	{
		ch_vol_prop = stack_audio_cue_get_volume_property(cue, channel, false);
		if (ch_vol_prop != NULL)
		{
			stack_property_write_json(ch_vol_prop, &cue_root);
		}
	}

	// Write crosspoints to JSON
	size_t output_channels = cue->parent->channels;
	for (size_t input_channel = 0; input_channel < input_channels; input_channel++)
	{
		for (size_t output_channel = 0; output_channel < output_channels; output_channel++)
		{
			StackProperty *crosspoint_prop = stack_audio_cue_get_crosspoint_property(cue, input_channel, output_channel, false);
			if (crosspoint_prop != NULL)
			{
				stack_property_write_json(crosspoint_prop, &cue_root);
			}
		}
	}

	// Write out JSON string and return (to be free'd by stack_audio_cue_free_json)
	Json::StreamWriterBuilder builder;
	return strdup(Json::writeString(builder, cue_root).c_str());
}

static void stack_audio_cue_free_json(StackCue *cue, char *json_data)
{
	free(json_data);
}

// Re-initialises this cue from JSON Data
void stack_audio_cue_from_json(StackCue *cue, const char *json_data)
{
	Json::Value cue_root;

	// Parse JSON data
	stack_json_read_string(json_data, &cue_root);

	// Get the data that's pertinent to us
	if (!cue_root.isMember("StackAudioCue"))
	{
		stack_log("stack_audio_cue_from_json(): Missing StackAudioCue class\n");
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
		stack_property_set_int64(stack_cue_get_property(cue, "media_start_time"), STACK_PROPERTY_VERSION_DEFINED, cue_data["media_start_time"].asUInt64());
	}
	else
	{
		stack_property_set_int64(stack_cue_get_property(cue, "media_start_time"), STACK_PROPERTY_VERSION_DEFINED, 0);
	}
	if (cue_data.isMember("media_end_time"))
	{
		stack_property_set_int64(stack_cue_get_property(cue, "media_end_time"), STACK_PROPERTY_VERSION_DEFINED, cue_data["media_end_time"].asUInt64());
	}
	else
	{
		stack_property_set_int64(stack_cue_get_property(cue, "media_end_time"), STACK_PROPERTY_VERSION_DEFINED, 0);
	}
	stack_audio_cue_update_action_time(STACK_AUDIO_CUE(cue));

	// Load loops
	if (cue_data.isMember("loops"))
	{
		stack_property_set_int32(stack_cue_get_property(cue, "loops"), STACK_PROPERTY_VERSION_DEFINED, (int32_t)cue_data["loops"].asInt64());
	}
	else
	{
		stack_property_set_int32(stack_cue_get_property(cue, "loops"), STACK_PROPERTY_VERSION_DEFINED, 1);
	}

	// Load rate
	if (cue_data.isMember("rate"))
	{
		stack_property_set_double(stack_cue_get_property(cue, "rate"), STACK_PROPERTY_VERSION_DEFINED, cue_data["rate"].asDouble());
	}
	else
	{
		stack_property_set_double(stack_cue_get_property(cue, "rate"), STACK_PROPERTY_VERSION_DEFINED, 1.0);
	}

	// Load playback volume
	if (cue_data.isMember("master_volume"))
	{
		double volume = 0.0;
		if (cue_data["master_volume"].isString() && cue_data["master_volume"].asString() == "-Infinite")
		{
			volume = -INFINITY;
		}
		else
		{
			volume = cue_data["master_volume"].asDouble();
		}
		stack_property_set_double(stack_cue_get_property(cue, "master_volume"), STACK_PROPERTY_VERSION_DEFINED, volume);
	}
	// Older versions used play_volume before we enforced a well-known name
	else if (cue_data.isMember("play_volume"))
	{
		double volume = 0.0;
		if (cue_data["play_volume"].isString() && cue_data["play_volume"].asString() == "-Infinite")
		{
			volume = -INFINITY;
		}
		else
		{
			volume = cue_data["play_volume"].asDouble();
		}
		stack_property_set_double(stack_cue_get_property(cue, "master_volume"), STACK_PROPERTY_VERSION_DEFINED, volume);
	}
	else
	{
		stack_property_set_double(stack_cue_get_property(cue, "master_volume"), STACK_PROPERTY_VERSION_DEFINED, 0.0);
	}

	if (cue_data.isMember("_last_input_channels") && cue_data["_last_input_channels"].isUInt())
	{
		size_t input_channels = cue_data["_last_input_channels"].asUInt();

		for (size_t channel = 0; channel < input_channels; channel++)
		{
			char property_name[64];
			snprintf(property_name, 64, "input_%lu_volume", channel + 1);
			if (cue_data.isMember(property_name))
			{
				StackProperty *channel_property = stack_audio_cue_get_volume_property(cue, channel + 1, true);
				if (cue_data[property_name].isString() && cue_data[property_name].asString() == "-Infinite")
				{
					stack_property_set_double(channel_property, STACK_PROPERTY_VERSION_DEFINED, -INFINITY);
				}
				else
				{
					stack_property_set_double(channel_property, STACK_PROPERTY_VERSION_DEFINED, cue_data[property_name].asDouble());
				}
			}
		}

		for (size_t input_channel = 0; input_channel < input_channels; input_channel++)
		{
			for (size_t output_channel = 0; output_channel < cue->parent->channels; output_channel++)
			{
				char property_name[64];
				snprintf(property_name, 64, "crosspoint_%lu_%lu", output_channel, input_channel);

				if (cue_data.isMember(property_name))
				{
					StackProperty *crosspoint_property = stack_audio_cue_get_crosspoint_property(cue, input_channel, output_channel, true);
					if (cue_data[property_name].isString() && cue_data[property_name].asString() == "-Infinite")
					{
						stack_property_set_double(crosspoint_property, STACK_PROPERTY_VERSION_DEFINED, -INFINITY);
					}
					else
					{
						stack_property_set_double(crosspoint_property, STACK_PROPERTY_VERSION_DEFINED, cue_data[property_name].asDouble());
					}
				}
			}
		}
	}
}

/// Gets the error message for the cue
bool stack_audio_cue_get_error(StackCue *cue, char *message, size_t size)
{
	char *file = NULL;
	stack_property_get_string(stack_cue_get_property(cue, "file"), STACK_PROPERTY_VERSION_DEFINED, &file);
	if (file == NULL || strlen(file) == 0)
	{
		snprintf(message, size, "No audio file selected");
		return true;
	}
	else if (STACK_AUDIO_CUE(cue)->playback_file == NULL)
	{
		snprintf(message, size, "Invalid audio file");
		return true;
	}
	else if (cue->parent->audio_device == NULL)
	{
		snprintf(message, size, "No audio device");
		return true;
	}

	// Default condition: no error
	strncpy(message, "", size);
	return false;
}

/// Returns which cuelist channels the cue is actively wanting to send audio to
size_t stack_audio_cue_get_active_channels(StackCue *cue, bool *channels, bool live)
{
	if (!live)
	{
		if (STACK_AUDIO_CUE(cue)->playback_file != NULL)
		{
			return STACK_AUDIO_CUE(cue)->playback_file->channels;
		}
		else
		{
			return 0;
		}
	}

	// If we're not in playback then we're not sending data
	if (cue->state != STACK_CUE_STATE_PLAYING_ACTION)
	{
		return 0;
	}
	else
	{
		// We currently return data for all channels in the cue list.
		// TODO: Ideally we should return no data for channels where the
		// crosspoints are such that we'd never return audio
		if (channels != NULL)
		{
			for (size_t channel = 0; channel < cue->parent->channels; channel++)
			{
				channels[channel] = true;
			}
		}
		return cue->parent->channels;
	}
}

/// Returns audio
size_t stack_audio_cue_get_audio(StackCue *cue, float *buffer, size_t frames)
{
	// We don't need to do anything if we're not in the playing state or no frames were requested
	if (cue->state != STACK_CUE_STATE_PLAYING_ACTION || frames == 0)
	{
		return 0;
	}

	// For tidiness
	StackAudioCue *audio_cue = STACK_AUDIO_CUE(cue);

	// Store these locally for efficiency
	const size_t input_channels = audio_cue->playback_file->channels;
	const size_t output_channels = cue->parent->channels;

	// Make sure we have enough room in our playback buffer
	if (audio_cue->playback_buffer == NULL || frames > audio_cue->playback_buffer_frames)
	{
		if (audio_cue->playback_buffer != NULL)
		{
			delete [] audio_cue->playback_buffer;
		}
		audio_cue->playback_buffer = new float[frames * input_channels];
		audio_cue->playback_buffer_frames = frames;
	}

	size_t frames_to_return = 0;

	if (audio_cue->resampler == NULL)
	{
		// Get some more data from the file
		frames_to_return = stack_audio_file_read(audio_cue->playback_file, audio_cue->playback_buffer, frames);
	}
	else
	{
		bool eof = false;
		size_t frames_read = 0;

		size_t current_size = stack_resampler_get_buffered_size(audio_cue->resampler);
		while (current_size < frames && !eof)
		{
			// Get some more data from the file
			frames_read = stack_audio_file_read(audio_cue->playback_file, audio_cue->playback_buffer, frames);

			// Give it to the resampler for resampling
			current_size = stack_resampler_push(audio_cue->resampler, audio_cue->playback_buffer, frames_read);

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
		frames_to_return = stack_resampler_get_frames(audio_cue->resampler, audio_cue->playback_buffer, fetch_frames);
	}

	// Set the initial output to zero
	memset(buffer, 0, frames_to_return * output_channels * sizeof(float));

	// Calculate audio scalar (using the live playback volume). Also use this to
	// scale from 16-bit signed int to 0.0-1.0 range
	double playback_live_volume = 0.0;
	stack_property_get_double(stack_cue_get_property(cue, "master_volume"), STACK_PROPERTY_VERSION_LIVE, &playback_live_volume);
	float base_audio_scaler = (float)stack_db_to_scalar(playback_live_volume);

	// For each output channel in the cue list
	for (size_t output_channel = 0; output_channel < output_channels; output_channel++)
	{
		// For each input channel in the file
		for (size_t input_channel = 0; input_channel < input_channels; input_channel++)
		{
			// Get the output volume for the channel
			double channel_volume = 0.0;
			StackProperty *property = stack_audio_cue_get_volume_property(cue, input_channel + 1, false);
			stack_property_get_double(property, STACK_PROPERTY_VERSION_LIVE, &channel_volume);

			// Get the total scalar for the master volume, the channel volume and the crosspoint
			float scalar = base_audio_scaler * (float)stack_db_to_scalar(channel_volume) * (float)stack_db_to_scalar(stack_audio_cue_get_crosspoint(audio_cue, input_channel, output_channel, STACK_PROPERTY_VERSION_LIVE));

			// Skip channels where the scalar is zero to not waste time
			if (scalar > 0.0)
			{
				// Set up our pointers
				float *read_pointer = &audio_cue->playback_buffer[input_channel];
				float *read_end_pointer = &audio_cue->playback_buffer[frames_to_return * input_channels + input_channel];
				float *write_pointer = &buffer[output_channel];

				// Perform the copying and scaling
				for (; read_pointer < read_end_pointer; write_pointer += output_channels, read_pointer += input_channels)
				{
					*write_pointer += *read_pointer * scalar;
				}
			}
		}
	}

	return frames_to_return;
}

const char *stack_audio_cue_get_field(StackCue *cue, const char *field)
{
	if (strcmp(field, "filepath") == 0)
	{
		char *file = NULL;
		stack_property_get_string(stack_cue_get_property(cue, "file"), STACK_PROPERTY_VERSION_DEFINED, &file);
		if (strlen(file) == 0)
		{
			return "<no file selected>";
		}

		return file;
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

/// Returns the icon for a cue
/// @param cue The cue to get the icon of
GdkPixbuf *stack_audio_cue_get_icon(StackCue *cue)
{
	return icon;
}

// Registers StackAudioCue with the application
void stack_audio_cue_register()
{
	// Load the icon
	icon = gdk_pixbuf_new_from_resource("/org/stack/icons/stackaudiocue.png", NULL);

	// Register cue types
	StackCueClass* audio_cue_class = new StackCueClass{ "StackAudioCue", "StackCue", "Audio Cue", stack_audio_cue_create, stack_audio_cue_destroy, stack_audio_cue_play, NULL, stack_audio_cue_stop, stack_audio_cue_pulse, stack_audio_cue_set_tabs, stack_audio_cue_unset_tabs, stack_audio_cue_to_json, stack_audio_cue_free_json, stack_audio_cue_from_json, stack_audio_cue_get_error, stack_audio_cue_get_active_channels, stack_audio_cue_get_audio, stack_audio_cue_get_field, stack_audio_cue_get_icon, NULL, NULL };
	stack_register_cue_class(audio_cue_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_audio_cue_register();
	return true;
}
