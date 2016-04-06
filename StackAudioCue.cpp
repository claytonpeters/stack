// Includes:
#include "StackApp.h"
#include "StackAudioCue.h"
#include <cstring>
#include <cstdlib>
#include <cmath>

// Creates an audio cue
static StackCue* stack_audio_cue_create(StackCueList *cue_list)
{
	// Debug
	fprintf(stderr, "stack_audio_cue_create() called\n");
	
	// Allocate the cue
	StackAudioCue* cue = new StackAudioCue();
	
	// Initialise the superclass
	stack_cue_init(&cue->super, cue_list);

	// Make this class a StackAudioCue
	cue->super._class_name = "StackAudioCue";
	
	// We start in error state until we have a file
	stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_ERROR);
	
	// Initialise our variables
	cue->file = strdup("");
	//cue->fade_in_time = 0;
	//cue->fade_out_time = 0;
	cue->media_start_time = 0;
	cue->media_end_time = 0;
	//cue->start_volume = -INFINITY;
	cue->play_volume = 0.0;
	//cue->end_volume = -INFINITY;
	cue->builder = NULL;
	cue->media_tab = NULL;

	return STACK_CUE(cue);
}

// Destroys an audio cue
static void stack_audio_cue_destroy(StackCue *cue)
{
	// Debug
	fprintf(stderr, "stack_audio_cue_destroy() called\n");

	// Our tidyup here
	free(STACK_AUDIO_CUE(cue)->file);
	
	// TODO: What do we do with builder and media_tab?
	
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

static bool stack_audio_cue_read_wavehdr(GInputStream *stream, WaveHeader *header)
{
	if (g_input_stream_read(stream, header, 44, NULL, NULL) == 44)
	{
		// Check for RIFF header
		if (header->chunk_id[0] == 'R' && header->chunk_id[1] == 'I' && header->chunk_id[2] == 'F' && header->chunk_id[3] == 'F')
		{
			// Check for WAVE format
			if (header->format[0] == 'W' && header->format[1] == 'A' && header->format[2] == 'V' && header->format[3] == 'E')
			{
				// Check for 'fmt ' subchunl
				if (header->subchunk_1_id[0] == 'f' && header->subchunk_1_id[1] == 'm' && header->subchunk_1_id[2] == 't' && header->subchunk_1_id[3] == ' ')
				{
					if (header->byte_rate == 0)
					{
						header->byte_rate = header->sample_rate * header->num_channels * header->bits_per_sample / 8;
					}
					
					return true;
				}
				else
				{
					fprintf(stderr, "stack_audio_cue_read_wavehdr(): Failed to find fmt chunk\n");
				}
			}
			else
			{
				fprintf(stderr, "stack_audio_cue_read_wavehdr(): Failed to find WAVE format\n");
			}
		}
		else
		{
			fprintf(stderr, "stack_audio_cue_read_wavehdr(): Failed to find RIFF header\n");
		}
	}
	else
	{
		fprintf(stderr, "stack_audio_cue_read_wavehdr(): Failed to read 44 byte header\n");
	}
	
	return false;
}

bool stack_audio_cue_set_file(StackAudioCue *cue, const char *uri)
{
	// Check to see if file was previously empty
	bool was_empty = (strlen(cue->file) == 0) && (cue->media_start_time == 0) && (cue->media_end_time == 0);
	
	// Open the file
	GFile *file = g_file_new_for_uri(uri);
	if (file == NULL)
	{
		return false;
	}
	
	// Open a stream
	GFileInputStream *stream = g_file_read(file, NULL, NULL);
	if (stream == NULL)
	{
		g_object_unref(file);
		return false;
	}

	// Assume we fail to start with
	bool result = false;
	
	// Attempt to read a wave header
	WaveHeader header;
	if (stack_audio_cue_read_wavehdr((GInputStream*)stream, &header))
	{						
		// Query the file info
		GFileInfo* file_info = g_file_query_info(file, "standard::size", G_FILE_QUERY_INFO_NONE, NULL, NULL);
		if (file_info != NULL)
		{
			// Store the filename
			free(cue->file);
			cue->file = strdup(uri);

			// Get the file size and work out the length of the file in seconds
			goffset size = g_file_info_get_size(file_info);
			double seconds = double(size - 44) / double(header.byte_rate);
		
			// Debug
			fprintf(stderr, "acp_file_changed(): File info: %ld bytes / %d byte rate = %.4f seconds\n", size, header.byte_rate, seconds);
			
			// Store the file length
			cue->file_length = (stack_time_t)(seconds * 1.0e9);
			
			// Update the cue state
			stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_STOPPED);
			
			// If we were previously empty, or if this new file is shorter
			// than the previous file, update the media end point
			if (was_empty || cue->file_length < cue->media_end_time)
			{
				cue->media_end_time = cue->file_length;
			}
			
			// If the media start point is past the length of the file, reset it
			if (cue->media_start_time > cue->file_length)
			{
				cue->media_start_time = 0;
			}
			
			// Update the cue action time;
			stack_audio_cue_update_action_time(cue);
			
			// Tidy up
			g_object_unref(file_info);
			
			// Set the result
			result = true;
		}
		else
		{
			fprintf(stderr, "acp_file_changed(): Failed to get file info\n");
		}
	}
	else
	{
		fprintf(stderr, "acp_file_changed(): Failed to read header\n");
	}
		
	// Tidy up
	g_object_unref(stream);
	g_object_unref(file);
	
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
		stack_format_time_as_string(cue->file_length, time_buffer, 32);
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
	if (cue->media_start_time > cue->file_length)
	{
		cue->media_start_time = cue->file_length;
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
	if (cue->media_end_time > cue->file_length)
	{
		cue->media_end_time = cue->file_length;
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
}

// Called when we're being played
static bool stack_audio_cue_play(StackCue *cue)
{
	// Call the super class
	if (!stack_cue_play_base(cue))
	{
		return false;
	}
	
	// For tidiness
	StackAudioCue *audio_cue = STACK_AUDIO_CUE(cue);
	
	// Initialise playback
	audio_cue->playback_data_sent = 0;
	audio_cue->playback_live_volume = audio_cue->play_volume;
	audio_cue->playback_audio_ptr = -1;
	
	// Initialise playback: open the file
	audio_cue->playback_file = g_file_new_for_uri(audio_cue->file);
	if (audio_cue->playback_file == NULL)
	{
		fprintf(stderr, "stack_audio_cue_play(): Failed to open playback file\n");
		stack_cue_set_state(cue, STACK_CUE_STATE_ERROR);
		return false;
	}
	
	// Initialise playback: get a stream
	audio_cue->playback_file_stream = g_file_read(audio_cue->playback_file, NULL, NULL);
	if (audio_cue->playback_file_stream == NULL)
	{
		fprintf(stderr, "stack_audio_cue_play(): Failed to open playback stream\n");
		stack_cue_set_state(cue, STACK_CUE_STATE_ERROR);
		g_object_unref(audio_cue->playback_file);
		return false;
	}

	if (!stack_audio_cue_read_wavehdr((GInputStream*)audio_cue->playback_file_stream, &audio_cue->playback_header))
	{
		fprintf(stderr, "stack_audio_cue_play(): Failed to read header\n");	
		stack_cue_set_state(cue, STACK_CUE_STATE_ERROR);
		g_object_unref(audio_cue->playback_file);
		g_object_unref(audio_cue->playback_file_stream);
		return false;
	}
	
	// Skip to the appropriate point in the file
	g_seekable_seek(G_SEEKABLE(audio_cue->playback_file_stream), audio_cue->media_start_time * (stack_time_t)audio_cue->playback_header.byte_rate / NANOSECS_PER_SEC, G_SEEK_CUR, NULL, NULL);
	
	return true;
}

static void stack_audio_cue_stop(StackCue *cue)
{
	// Call the superclass (this handles stopping the cue after the action time)
	stack_cue_stop_base(cue);
	
	// For tidiness
	StackAudioCue *audio_cue = STACK_AUDIO_CUE(cue);

	// Tidy up the open file stream...
	if (audio_cue->playback_file_stream != NULL)
	{
		g_object_unref(audio_cue->playback_file_stream);
		audio_cue->playback_file_stream = NULL;
	}

	// ...and then the file
	if (STACK_AUDIO_CUE(cue)->playback_file != NULL)
	{
		g_object_unref(audio_cue->playback_file);
		audio_cue->playback_file = NULL;
	}	
}

// Called when we're pulsed (every few milliseconds whilst the cue is in playback)
static void stack_audio_cue_pulse(StackCue *cue, stack_time_t clocktime)
{
	// Call the super class
	stack_cue_pulse_base(cue, clocktime);
	
	// We don't need to do anything if we're not in the playing state
	if (cue->state != STACK_CUE_STATE_PLAYING_ACTION)
	{
		return;
	}

	// For tidiness
	StackAudioCue *audio_cue = STACK_AUDIO_CUE(cue);
	
	// Get the current cue action times
	stack_time_t action_time;
	stack_cue_get_running_times(cue, clocktime, NULL, &action_time, NULL, NULL, NULL, NULL);
	
	// Calculate audio scalar (using the live playback volume). Also use this to
	// scale from 16-bit signed int to 0.0-1.0 range
	float audio_scaler = stack_db_to_scalar(audio_cue->playback_live_volume) / 32768.0;
	
	// If we've sent no data, or we've running behind, or we're about to need more data
	if (audio_cue->playback_data_sent == 0 || audio_cue->playback_data_sent < action_time + 20000000)
	{
		size_t samples = 1024;
		size_t out_buffer_size = samples * audio_cue->playback_header.num_channels;
		
		// Set up buffer for the number of samples of each channel
		size_t bytes_to_read = samples * audio_cue->playback_header.num_channels * audio_cue->playback_header.bits_per_sample / 8;

		// Allocate buffers		
		char *read_buffer = new char[bytes_to_read];
		float *out_buffer = new float[samples * out_buffer_size];
		
		// Read data
		g_input_stream_read((GInputStream*)audio_cue->playback_file_stream, read_buffer, bytes_to_read, NULL, NULL);

		// Convert to float Apply audio scaling
		float *obp = out_buffer;
		int16_t *ibp = (int16_t*)read_buffer;
		for (size_t i = 0; i < out_buffer_size; i++)
		{
			*(obp++) = *(ibp++) * audio_scaler;
		}
		
		// Write the audio data to the cue list, which handles the output
		audio_cue->playback_audio_ptr = stack_cue_list_write_audio(cue->parent, audio_cue->playback_audio_ptr, out_buffer, audio_cue->playback_header.num_channels, samples, true);
		
		// Tidy up
		delete [] read_buffer;
		delete [] out_buffer;
		
		// Keep track of how much data we've sent to the audio device
		audio_cue->playback_data_sent += ((stack_time_t)samples * NANOSECS_PER_SEC) / (stack_time_t)audio_cue->playback_header.sample_rate;
	}
}

// Sets up the properties tabs for an audio cue
static void stack_audio_cue_set_tabs(StackCue *cue, GtkNotebook *notebook)
{
	StackAudioCue *scue = STACK_AUDIO_CUE(cue);
	
	// Debug
	fprintf(stderr, "stack_audio_cue_set_tabs() called\n");

	// Create the tab
	GtkWidget *label = gtk_label_new("Media");
	
	// Load the UI
	GtkBuilder *builder = gtk_builder_new_from_file("StackAudioCue.ui");
	scue->builder = builder;
	scue->media_tab = GTK_WIDGET(gtk_builder_get_object(builder, "acpGrid"));

	// Set up callbacks
	gtk_builder_add_callback_symbol(builder, "acp_file_changed", G_CALLBACK(acp_file_changed));
	gtk_builder_add_callback_symbol(builder, "acp_trim_start_changed", G_CALLBACK(acp_trim_start_changed));
	gtk_builder_add_callback_symbol(builder, "acp_trim_end_changed", G_CALLBACK(acp_trim_end_changed));
	gtk_builder_add_callback_symbol(builder, "acp_volume_changed", G_CALLBACK(acp_volume_changed));
	
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
		stack_format_time_as_string(scue->file_length, time_buffer, 32);
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
	// Debug
	fprintf(stderr, "stack_audio_cue_unset_tabs() called\n");
	
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

// Registers StackAudioCue with the application
void stack_audio_cue_register()
{
	// Register cue types
	StackCueClass* audio_cue_class = new StackCueClass{ "StackAudioCue", "StackCue", stack_audio_cue_create, stack_audio_cue_destroy, stack_audio_cue_play, NULL, stack_audio_cue_stop, stack_audio_cue_pulse, stack_audio_cue_set_tabs, stack_audio_cue_unset_tabs };
	stack_register_cue_class(audio_cue_class);
}

