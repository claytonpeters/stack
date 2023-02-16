// Includes:
#include "StackApp.h"
#include "StackAudioCue.h"
#include "MPEGAudioFile.h"
#include <cstring>
#include <cstdlib>
#include <math.h>
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
	cue->play_volume = 0.0;
	cue->builder = NULL;
	cue->media_tab = NULL;

	// Initialise our variables: playback
	cue->playback_data_sent = 0;
	cue->playback_live_volume = 0.0;
	cue->playback_file = NULL;
	cue->playback_audio_ptr = 0;
	cue->resampler = NULL;

	// Initialise our variables: preview
	cue->preview_thread_run = false;
	cue->preview_surface = NULL;
	cue->preview_cr = NULL;
	cue->preview_start = 0;
	cue->preview_end = 0;
	cue->preview_width = 0;
	cue->preview_height = 0;
	cue->preview_widget = NULL;
	cue->last_preview_redraw_time = 0;

	// Change some superclass variables
	stack_cue_set_name(STACK_CUE(cue), "${filename}");

	return STACK_CUE(cue);
}

// Tidy up the preview
static void stack_audio_cue_preview_tidy(StackAudioCue *cue)
{
	// If the preview thread is running
	if (cue->preview_thread_run)
	{
		// Tell the thread to stop
		cue->preview_thread_run = false;
		if (cue->preview_thread.joinable())
		{
			cue->preview_thread.join();
		}
	}

	// Tidy up
	if (cue->preview_surface)
	{
		cairo_surface_destroy(cue->preview_surface);
		cue->preview_surface = NULL;
	}
	if (cue->preview_cr)
	{
		cairo_destroy(cue->preview_cr);
		cue->preview_cr = NULL;
	}

	// Reset variables
	cue->preview_start = 0;
	cue->preview_end = 0;
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
	if (acue->builder)
	{
		// Remove our reference to the media tab
		g_object_unref(acue->media_tab);

		// Remove our reference to the preview widget
		g_object_unref(acue->preview_widget);

		// Destroy the top level widget in the builder
		gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(acue->builder, "window1")));

		// Unref the builder
		g_object_unref(acue->builder);
	}

	// Tidy up preview
	stack_audio_cue_preview_tidy(acue);

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
		stack_audio_cue_preview_tidy(cue);
		cue->preview_start = 0;
		cue->preview_end = new_playback_file->length;

		// We succeeded
		result = true;
	}

	// Notify cue list that we've changed
	stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue));

	return result;
}

static uint64_t stack_audio_cue_preview_render_points(const StackAudioCue *cue, const float *data, uint32_t sample_rate, uint16_t num_channels, uint32_t frames, uint64_t preview_start_samples, uint64_t data_start_frames)
{
	uint64_t frame = data_start_frames;

	// Calculate the preview width in samples
	float preview_width_samples = (float)sample_rate * (float)(cue->preview_end - cue->preview_start) / NANOSECS_PER_SEC_F;
	double sample_width = cue->preview_width / (double)preview_width_samples;
	double half_preview_height = cue->preview_height / 2.0;

	// Calculate the highest value given floating point (-1.0 < x < 1.0)
	// and the number of channels
	float scalar = 1.0 / (float)num_channels;

	// Draw the audio data. We sum all the channels together to draw a single waveform
	for (size_t i = 0; i < frames; i++, frame++)
	{
		// Sum all the channels
		float y = 0;
		for (size_t channel = 0; channel < num_channels; channel++)
		{
			y += *(data++);
		}

		// Scale y back in to -1.0 < y < 1.0
		y *= scalar;

		// Draw the line
		cairo_line_to(cue->preview_cr, (double)(frame - preview_start_samples) * sample_width, half_preview_height * (1.0 - y));
	}

	return frame;
}

// Thread to generate the code preview
static void stack_audio_cue_preview_thread(StackAudioCue *cue)
{
	fprintf(stderr, "stack_audio_cue_preview_thread(): started\n");

	// Open the file
	StackAudioFile *file = stack_audio_file_create(cue->file);
	if (file == NULL)
	{
		fprintf(stderr, "stack_audio_cue_preview_thread(): file open failed\n");
		return;
	}

	// Flag that the thread is running
	cue->preview_thread_run = true;

	// Initialise drawing: fill the background
	cairo_set_source_rgb(cue->preview_cr, 0.1, 0.1, 0.1);
	cairo_paint(cue->preview_cr);

	// Initialise drawing: prepare for lines
	cairo_set_antialias(cue->preview_cr, CAIRO_ANTIALIAS_FAST);
	cairo_set_line_width(cue->preview_cr, 1.0);
	cairo_set_source_rgb(cue->preview_cr, 0.0, 0.8, 0.0);
	cairo_move_to(cue->preview_cr, 0.0, cue->preview_height / 2.0);

	// We force a redraw periodically, get the current time
	stack_time_t last_redraw_time = stack_get_clock_time();

	// Seek to the right time in the file
	stack_audio_file_seek(file, cue->preview_start);

	// Convert out start and end times from seconds to samples
	uint64_t preview_start_samples = stack_time_to_samples(cue->preview_start, file->sample_rate);
	uint64_t preview_end_samples = stack_time_to_samples(cue->preview_end, file->sample_rate);

	size_t frames = 1024;
	size_t samples = frames * file->channels;

	// Allocate buffers
	float *read_buffer = new float[samples];
	bool no_more_data = false;
	uint64_t sample = preview_start_samples;

	// Calculate the width of a single sample on the surface
	uint64_t samples_in_file = (uint64_t)((double)file->length / 1.0e9 * (double)file->sample_rate);

	while (cue->preview_thread_run && !no_more_data && sample < preview_end_samples)
	{
		// Get some more data
		size_t frames_read = stack_audio_file_read(file, read_buffer, frames);

		// If we've read data
		if (frames_read > 0)
		{
			// Render the samples
			sample = stack_audio_cue_preview_render_points(cue, read_buffer, file->sample_rate, file->channels, frames_read, preview_start_samples, sample);

			// Intentionally slow this down so that we don't chew through
			// CPU that might be needed for playback
			usleep(125);

			// Redraw a few times per second
			if (stack_get_clock_time() - last_redraw_time > NANOSECS_PER_SEC / 25)
			{
				cairo_stroke(cue->preview_cr);
				if (cue->media_tab != NULL)
				{
					gtk_widget_queue_draw(cue->preview_widget);
				}
				last_redraw_time = stack_get_clock_time();
			}
		}
		else
		{
			no_more_data = true;
		}
	}

	// Tidy up
	delete [] read_buffer;

	// We've finished - force a redraw
	cairo_stroke(cue->preview_cr);
	if (cue->media_tab != NULL)
	{
		gtk_widget_queue_draw(cue->preview_widget);
	}

	// Tidy up
	stack_audio_file_destroy(file);

	// Finished
	cue->preview_thread_run = false;

	return;
}

// Generates a new audio preview
// @param cue The cue to preview
// @param start The starting time of the preview (on the left hand side of the image)
// @param end The ending time of the preview (on the right hand side of the image)
// @param width The width of the preview image
// @param height The height of the preview image
static void stack_audio_cue_preview_generate(StackAudioCue *cue, stack_time_t start, stack_time_t end, int width, int height)
{
	// Wiat for any other thread to terminate
	if (cue->preview_thread_run)
	{
		cue->preview_thread_run = false;
		cue->preview_thread.join();
	}

	// Destroy any current surface
	if (cue->preview_surface)
	{
		cairo_surface_destroy(cue->preview_surface);
		cue->preview_surface = NULL;
	}
	if (cue->preview_cr)
	{
		cairo_destroy(cue->preview_cr);
		cue->preview_cr = NULL;
	}

	// Store details
	cue->preview_start = start;
	cue->preview_end = end;
	cue->preview_width = width;
	cue->preview_height = height;

	// Create a new surface
	cue->preview_surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, (int)width, (int)height);
	cue->preview_cr = cairo_create(cue->preview_surface);
	cairo_set_source_rgb(cue->preview_cr, 0.0, 0.0, 0.0);
	cairo_paint(cue->preview_cr);

	// Start a new preview thread (re-join any existing thread to wait for it to die)
	if (cue->preview_thread.joinable())
	{
		cue->preview_thread.join();
	}
	cue->preview_thread = std::thread(stack_audio_cue_preview_thread, cue);
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
		snprintf(text_buffer, 128, "(File length is %s)", time_buffer);
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
	if (cue->file && cue->media_start_time > cue->playback_file->length)
	{
		cue->media_start_time = cue->playback_file->length;
	}

	// Update the action_time
	stack_audio_cue_update_action_time(cue);

	// Update the UI
	char buffer[32];
	stack_format_time_as_string(cue->media_start_time, buffer, 32);
	gtk_entry_set_text(GTK_ENTRY(widget), buffer);

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
	if (cue->file && cue->media_end_time > cue->playback_file->length)
	{
		cue->media_end_time = cue->playback_file->length;
	}

	// Update the action time
	stack_audio_cue_update_action_time(cue);

	// Update the UI
	char buffer[32];
	stack_format_time_as_string(cue->media_end_time, buffer, 32);
	gtk_entry_set_text(GTK_ENTRY(widget), buffer);

	// Fire an updated-selected-cue signal to signal the UI to change
	StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(widget);
	g_signal_emit_by_name((gpointer)window, "update-selected-cue");

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

static void acp_play_section_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	// Positions where we draw text
	static const size_t num_increments = 25;
	static const stack_time_t increments[] = {   1 * NANOSECS_PER_MILLISEC,   2 * NANOSECS_PER_MILLISEC,   5 * NANOSECS_PER_MILLISEC,
	                                            10 * NANOSECS_PER_MILLISEC,  20 * NANOSECS_PER_MILLISEC,  50 * NANOSECS_PER_MILLISEC,
	                                           100 * NANOSECS_PER_MILLISEC, 200 * NANOSECS_PER_MILLISEC, 500 * NANOSECS_PER_MILLISEC,
	                                             1 * NANOSECS_PER_SEC,        2 * NANOSECS_PER_SEC,        5 * NANOSECS_PER_SEC,
	                                            10 * NANOSECS_PER_SEC,       20 * NANOSECS_PER_SEC,       30 * NANOSECS_PER_SEC,
	                                             1 * NANOSECS_PER_MINUTE,     2 * NANOSECS_PER_MINUTE,     5 * NANOSECS_PER_MINUTE,
	                                            10 * NANOSECS_PER_MINUTE,    20 * NANOSECS_PER_MINUTE,    30 * NANOSECS_PER_MINUTE,
	                                            60 * NANOSECS_PER_MINUTE };

	guint width, height, graph_height, half_graph_height, top_bar_height;
	cairo_text_extents_t text_size;
	char time_buffer[32];

	// Get the cue
	StackAudioCue *cue = STACK_AUDIO_CUE(data);

	// Get the size of the component
	width = gtk_widget_get_allocated_width(widget);
	height = gtk_widget_get_allocated_height(widget);

	// Fill the background
	cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
	cairo_paint(cr);

	// Fill the background behind the text
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
	cairo_text_extents(cr, "0:00.000", &text_size);
	cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
	cairo_rectangle(cr, 0.0, 0.0, width, text_size.height + 4);
	cairo_fill(cr);

	// Figure out the height of the graph itself (so less the timing bar)
	top_bar_height = text_size.height + 4;
	graph_height = height - top_bar_height;
	half_graph_height = graph_height / 2;

	// Decide if we need to (re)generate a preview
	bool generate_preview = false;
	if (cue->preview_cr == NULL || cue->preview_surface == NULL)
	{
		generate_preview = true;
	}
	else if (cue->preview_cr != NULL && cue->preview_surface != NULL)
	{
		if (cue->preview_width != width || cue->preview_height != graph_height)
		{
			generate_preview = true;
		}
	}

	// Generate a preview if required
	if (generate_preview)
	{
		stack_audio_cue_preview_generate(cue, cue->preview_start, cue->preview_end, width, graph_height);
	}
	else
	{
		// If we have a surface, draw it
		if (cue->preview_cr != NULL && cue->preview_surface != NULL)
		{
			cairo_set_source_surface(cr, cue->preview_surface, 0, top_bar_height);
			cairo_rectangle(cr, 0.0, top_bar_height, width, graph_height);
			cairo_fill(cr);
		}
	}

	// Draw the zero line
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
	cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
	cairo_move_to(cr, 0.0, top_bar_height + half_graph_height);
	cairo_line_to(cr, width, top_bar_height + half_graph_height);
	cairo_stroke(cr);

	// Calculate where the playback section appears on the graph
	double preview_length = cue->preview_end - cue->preview_start;
	double section_left = (double)width * (double)cue->media_start_time / preview_length;
	double section_right = (double)width * (double)cue->media_end_time / preview_length;
	double section_width = section_right - section_left;

	// Draw the selected section
	cairo_set_source_rgba(cr, 0.0, 0.0, 1.0, 0.25);
	cairo_rectangle(cr, section_left, top_bar_height, section_width, graph_height);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0.0, 0.0, 1.0, 0.5);
	cairo_move_to(cr, section_left, top_bar_height);
	cairo_line_to(cr, section_left, height);
	cairo_move_to(cr, section_right, top_bar_height);
	cairo_line_to(cr, section_right, height);
	cairo_stroke(cr);

	// Setup for text drawing
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_move_to(cr, 0.0, text_size.height + 2);

	// Draw zero position
	stack_format_time_as_string(cue->preview_start, time_buffer, 32);
	cairo_show_text(cr, time_buffer);
	cairo_text_extents(cr, time_buffer, &text_size);
	cairo_set_line_width(cr, 1.0);

	// Store details of zero position
	float last_text_x = 0.0f, last_text_width = text_size.width;
	stack_time_t last_time = cue->preview_start;

	// Draw labels until the end
	bool found_label = true;
	while (found_label && last_text_x < (float)width)
	{
		// Identify the next time that we can draw without overlap
		stack_time_t new_time = 0;
		float new_text_x = 0.0;
		found_label = false;
		for (size_t i = 0; i < num_increments; i++)
		{
			new_time = last_time + increments[i];
			new_text_x = (float)width * (float)(new_time - cue->preview_start) / (float)(cue->preview_end - cue->preview_start);

			// Determine if this doesn't overlap (32 is our tidiness zone)
			if (new_text_x > last_text_x + last_text_width + 32)
			{
				found_label = true;
				break;
			}
		}

		// Draw a label
		if (found_label)
		{
			// Build the time string
			stack_format_time_as_string(new_time, time_buffer, 32);

			// Draw the text
			cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
			cairo_text_extents(cr, time_buffer, &text_size);
			cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
			cairo_move_to(cr, floor(new_text_x) + 1, text_size.height + 2);
			cairo_show_text(cr, time_buffer);

			// Draw a tick mark (3 pixels high)
			cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
			cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
			cairo_move_to(cr, floor(new_text_x), 0.0);
			cairo_line_to(cr, floor(new_text_x), top_bar_height + 3);
			cairo_stroke(cr);

			// Store the position of this label
			last_text_x = new_text_x;
			last_text_width = text_size.width;
			last_time = new_time;
		}
	}

	// Whilst in playback, draw a playback marker
	if (STACK_CUE(cue)->state == STACK_CUE_STATE_PLAYING_ACTION)
	{
		stack_time_t action_time;
		stack_cue_get_running_times(STACK_CUE(cue), stack_get_clock_time(), NULL, NULL, NULL, NULL, NULL, &action_time);
		float playback_x = (float)width * (float)(action_time + cue->media_start_time - cue->preview_start) / (float)(cue->preview_end - cue->preview_start);
		if (playback_x > 0.0 && playback_x < width)
		{
			cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
			cairo_set_source_rgb(cr, 1.8, 1.8, 0.8);
			cairo_move_to(cr, playback_x, 0);
			cairo_line_to(cr, playback_x, height);
			cairo_stroke(cr);
		}
	}
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
		// ...then we need to reset the pointer into the audio buffer
		audio_cue->playback_audio_ptr = -1;

		return true;
	}

	// Initialise playback
	audio_cue->playback_data_sent = 0;
	audio_cue->playback_live_volume = audio_cue->play_volume;
	audio_cue->playback_audio_ptr = -1;

	// If the sample rate of the file does not match the playback device, set
	// up a resampler
	if (audio_cue->playback_file->sample_rate != audio_cue->super.parent->audio_device->sample_rate)
	{
		audio_cue->resampler = stack_resampler_create(audio_cue->playback_file->sample_rate, audio_cue->super.parent->audio_device->sample_rate, audio_cue->playback_file->channels);
	}

	// Seek to the right point in the file
	stack_audio_file_seek(audio_cue->playback_file, audio_cue->media_start_time);

	return true;
}

// Queues a redraw of the preview widget. Designed to be called from
// gdk_threads_add_idle so as to be UI-thread-safe
static gboolean stack_audio_cue_threaded_redraw_widget(gpointer user_data)
{
	StackAudioCue *cue = STACK_AUDIO_CUE(user_data);
	if (cue->media_tab != NULL)
	{
		gtk_widget_queue_draw(cue->preview_widget);
	}

	return G_SOURCE_REMOVE;
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
	if (audio_cue->media_tab != NULL)
	{
		gdk_threads_add_idle(stack_audio_cue_threaded_redraw_widget, cue);
	}
}

// Called when we're pulsed (every few milliseconds whilst the cue is in playback)
static void stack_audio_cue_pulse(StackCue *cue, stack_time_t clocktime)
{
	// Call the super class
	stack_cue_pulse_base(cue, clocktime);

	// Redraw the preview periodically whilst in playback, but schedule this on
	// te main UI thread
	if (stack_get_clock_time() - STACK_AUDIO_CUE(cue)->last_preview_redraw_time > 33 * NANOSECS_PER_MILLISEC)
	{
		// Only redraw if the media tab is in the UI (i.e. we're the selected cue)
		if (STACK_AUDIO_CUE(cue)->media_tab != NULL)
		{
			STACK_AUDIO_CUE(cue)->last_preview_redraw_time = stack_get_clock_time();
			gdk_threads_add_idle(stack_audio_cue_threaded_redraw_widget, cue);
		}
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
	scue->preview_widget = GTK_WIDGET(gtk_builder_get_object(builder, "acpPlaySectionUI"));

	// Set up callbacks
	gtk_builder_add_callback_symbol(builder, "acp_file_changed", G_CALLBACK(acp_file_changed));
	gtk_builder_add_callback_symbol(builder, "acp_trim_start_changed", G_CALLBACK(acp_trim_start_changed));
	gtk_builder_add_callback_symbol(builder, "acp_trim_end_changed", G_CALLBACK(acp_trim_end_changed));
	gtk_builder_add_callback_symbol(builder, "acp_volume_changed", G_CALLBACK(acp_volume_changed));
	gtk_builder_add_callback_symbol(builder, "acp_play_section_draw", G_CALLBACK(acp_play_section_draw));

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
		snprintf(text_buffer, 128, "(File length is %s)", time_buffer);
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
	if (isfinite(acue->play_volume))
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
		fprintf(stderr, "stack_audio_cue_from_json(): Missing StackFadeCue class\n");
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
	STACK_AUDIO_CUE(cue)->media_start_time = cue_data["media_start_time"].asInt64();
	STACK_AUDIO_CUE(cue)->media_end_time = cue_data["media_end_time"].asInt64();
	stack_cue_set_action_time(STACK_CUE(cue), STACK_AUDIO_CUE(cue)->media_end_time - STACK_AUDIO_CUE(cue)->media_start_time);

	// Load playback volume
	if (cue_data["play_volume"].isString() && cue_data["play_volume"].asString() == "-Infinite")
	{
		STACK_AUDIO_CUE(cue)->play_volume = -INFINITY;
	}
	else
	{
		STACK_AUDIO_CUE(cue)->play_volume = cue_data["play_volume"].asDouble();
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
		StackAudioCue *audio_cue = STACK_AUDIO_CUE(cue);

		// TODO  Do this properly once we've implemented cross-points
		size_t channel_count = 1;
		if (audio_cue->playback_file->channels == 2)
		{
			channel_count = 2;
		}

		if (channels != NULL)
		{
			channels[0] = true;
			if (channel_count == 2)
			{
				channels[1] = true;
			}
		}

		return channel_count;
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

