// Includes:
#include "StackApp.h"
#include "StackAudioCue.h"
#include <cstring>
#include <cstdlib>
#include <math.h>
#include <json/json.h>
#ifndef NO_MP3LAME
#include <lame.h>
#endif

#ifndef NO_MP3LAME
// We need some additional data for MP3 playback
typedef struct PlaybackDataMP3
{
	hip_t decoder;
	uint8_t *read_buffer;
	int32_t read_buffer_size;
	int16_t *left;
	int16_t *right;
	uint32_t size;
} PlaybackDataMP3;
#endif

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
	// Our tidyup here
	free(STACK_AUDIO_CUE(cue)->file);
	
	// Tidy up
	if (STACK_AUDIO_CUE(cue)->builder)
	{
		// Remove our reference to the media tab
		g_object_ref(STACK_AUDIO_CUE(cue)->media_tab);

		// Destroy the top level widget in the builder
		gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(STACK_AUDIO_CUE(cue)->builder, "window1")));
		
		// Unref the builder
		g_object_unref(STACK_AUDIO_CUE(cue)->builder);
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

#ifndef NO_MP3LAME
static bool stack_audio_cue_read_mp3hdr(GInputStream *stream, WaveHeader *header, bool need_length)
{
	// Will receive the MP3 header from LAME
	mp3data_struct mp3header = {0};

	// Get a LAME MP3 decoder
	hip_t decoder = hip_decode_init();
	if (decoder == NULL)
	{
		fprintf(stderr, "stack_audio_cue_read_mp3hdr(): Failed to initialise MP3 decoder\n");
		return false;
	}

	// Allocate buffers
	uint8_t *buffer = new uint8_t[1024];
	short *garbage = new short[100000];

	// Read the entire file (as some MP3s don't know how long they are until
	// they are entirely decoded)
	uint32_t total_samples_per_channel = 0;
	while (g_input_stream_read(stream, buffer, 1024, NULL, NULL) > 0)
	{
		// Decode the MP3 (and get headers and ignoring the amount of data)
		int decoded_samples = hip_decode_headers(decoder, buffer, 1024, garbage, garbage, &mp3header);
		if (decoded_samples > 0)
		{
			total_samples_per_channel += decoded_samples;
		}
		else if (decoded_samples < 0)
		{
			fprintf(stderr, "stack_audio_cue_read_mp3hdr(): Call returned -1\n");
			break;
		}

		// If we've parsed the header and we don't need the length, then exit
		if (mp3header.header_parsed == 1 && !need_length)
		{
			break;
		}
	}

	// Tidy up
	delete [] buffer;
	delete [] garbage;
	hip_decode_exit(decoder);

	// Build a fake WaveHeader
	header->chunk_id[0] = 'R';
	header->chunk_id[1] = 'I';
	header->chunk_id[2] = 'F';
	header->chunk_id[2] = 'F';
	header->chunk_size = 0;
	header->format[0] = 'W';
	header->format[1] = 'A';
	header->format[2] = 'V';
	header->format[3] = 'E';
	header->subchunk_1_id[0] = 'f';
	header->subchunk_1_id[1] = 'm';
	header->subchunk_1_id[2] = 't';
	header->subchunk_1_id[3] = ' ';
	header->subchunk_1_size = 0;
	header->audio_format = 0;
	header->num_channels = mp3header.stereo;
	header->sample_rate = mp3header.samplerate;
	header->byte_rate = header->num_channels * header->sample_rate * sizeof(short);
	header->block_align = 0;
	header->bits_per_sample = 16;
	header->subchunk_2_id[0] = 'M';
	header->subchunk_2_id[1] = 'P';
	header->subchunk_2_id[2] = '3';
	header->subchunk_2_id[3] = 'A';
	if (need_length)
	{
		header->subchunk_2_size = total_samples_per_channel * header->num_channels * sizeof(short);
	}
	else
	{
		header->subchunk_2_size = 0;
	}

	// Return whether we succesfully parsed the header
	return (mp3header.header_parsed == 1);
}
#endif

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
				// Check for 'fmt ' subchunk
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
			// Get the file size and work out the length of the file in seconds
			goffset size = g_file_info_get_size(file_info);
			double seconds = double(size - 44) / double(header.byte_rate);
		
			// Debug
			fprintf(stderr, "acp_file_changed(): Wave: File info: %ld bytes / %d byte rate = %.4f seconds\n", size, header.byte_rate, seconds);
			
			// Store the file length
			cue->file_length = (stack_time_t)(seconds * 1.0e9);

			// Store the format
			cue->format = STACK_AUDIO_FILE_FORMAT_WAVE;
			
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
#ifndef NO_MP3LAME
	else
	{
		fprintf(stderr, "acp_file_changed(): Didn't find Wave header, trying MP3\n");

		// Reset back to start of file
		if (!g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL))
		{
			fprintf(stderr, "acp_file_changed(): Seek failed. MP3 Header will likely fail\n");
		}

		if (stack_audio_cue_read_mp3hdr((GInputStream*)stream, &header, true))
		{
			// We store the audio bytes in subchunk_2_size in our fake Wave Header
			double seconds = double(header.subchunk_2_size) / double(header.byte_rate);

			// Debug
			fprintf(stderr, "acp_file_changed(): MP3: File info: %ld bytes / %d byte rate = %.4f seconds\n", header.subchunk_2_size, header.byte_rate, seconds);
			
			// Store the file length
			cue->file_length = (stack_time_t)(seconds * 1.0e9);
			
			// Store the format
			cue->format = STACK_AUDIO_FILE_FORMAT_MP3;

			// Set the result
			result = true;
		}
		else
		{
			fprintf(stderr, "acp_file_changed(): Failed to read header\n");
		}
	}
#else
	else
	{
		fprintf(stderr, "acp_file_changed(): Failed to read header\n");
	}
#endif
	
	// If we successfully got a file, then update the cue
	if (result)
	{
		// Store the filename
		free(cue->file);
		cue->file = strdup(uri);

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

	}

	// Tidy up
	g_object_unref(stream);
	g_object_unref(file);

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

	if (audio_cue->format == STACK_AUDIO_FILE_FORMAT_WAVE)
	{
		if (!stack_audio_cue_read_wavehdr((GInputStream*)audio_cue->playback_file_stream, &audio_cue->playback_header))
		{
			fprintf(stderr, "stack_audio_cue_play(): Failed to read wave header\n");	
			stack_cue_set_state(cue, STACK_CUE_STATE_ERROR);
			g_object_unref(audio_cue->playback_file);
			g_object_unref(audio_cue->playback_file_stream);
			return false;
		}
		
		// Skip to the appropriate point in the file
		g_seekable_seek(G_SEEKABLE(audio_cue->playback_file_stream), audio_cue->media_start_time * (stack_time_t)audio_cue->playback_header.byte_rate / NANOSECS_PER_SEC, G_SEEK_CUR, NULL, NULL);
	}
#ifndef NO_MP3LAME
	else if (audio_cue->format == STACK_AUDIO_FILE_FORMAT_MP3)
	{
		// Read the header
		if (!stack_audio_cue_read_mp3hdr((GInputStream*)audio_cue->playback_file_stream, &audio_cue->playback_header, false))
		{
			fprintf(stderr, "stack_audio_cue_play(): Failed to read MP3 header\n");	
			stack_cue_set_state(cue, STACK_CUE_STATE_ERROR);
			g_object_unref(audio_cue->playback_file);
			g_object_unref(audio_cue->playback_file_stream);
			return false;
		}

		// Reset back to start of file
		if (!g_seekable_seek(G_SEEKABLE(audio_cue->playback_file_stream), 0, G_SEEK_SET, NULL, NULL))
		{
			fprintf(stderr, "stack_audio_cue_play(): Header read, but seek reset failed\n");
			return false;
		}

		// Initialise playback
		audio_cue->playback_data = (void*)new PlaybackDataMP3;
		PlaybackDataMP3* pbdata = (PlaybackDataMP3*)(audio_cue->playback_data);
		pbdata->decoder = hip_decode_init();
		pbdata->read_buffer = new uint8_t[1024];
		pbdata->read_buffer_size = 1024;
		pbdata->left = new int16_t[100000];
		pbdata->right = new int16_t[100000];
		pbdata->size = 100000;
	}
#endif
	
	return true;
}

static void stack_audio_cue_stop(StackCue *cue)
{
	// Call the superclass (this handles stopping the cue after the action time)
	stack_cue_stop_base(cue);
	
	// For tidiness
	StackAudioCue *audio_cue = STACK_AUDIO_CUE(cue);

#ifndef NO_MP3LAME
	// If we're an MP3 with playback data...
	if (audio_cue->format == STACK_AUDIO_FILE_FORMAT_MP3 && audio_cue->playback_data != NULL)
	{
		PlaybackDataMP3* pbdata = (PlaybackDataMP3*)(audio_cue->playback_data);

		// Tidy up the decoder
		hip_decode_exit(pbdata->decoder);

		// Tidy up our buffers
		delete [] pbdata->read_buffer;
		delete [] pbdata->left;
		delete [] pbdata->right;

		// Tidy up the playback data structure
		delete pbdata;
		audio_cue->playback_data = NULL;
	}
#endif

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
		if (audio_cue->format == STACK_AUDIO_FILE_FORMAT_WAVE)
		{
			size_t samples_per_channel = 1024;
			size_t total_samples = samples_per_channel * audio_cue->playback_header.num_channels;
			
			// Set up buffer for the number of samples of each channel
			size_t bytes_to_read = total_samples * audio_cue->playback_header.bits_per_sample / 8;

			// Allocate buffers		
			char *read_buffer = new char[bytes_to_read];
			float *out_buffer = new float[total_samples];
			
			// Read data
			gssize bytes_read = g_input_stream_read((GInputStream*)audio_cue->playback_file_stream, read_buffer, bytes_to_read, NULL, NULL);

			// If we attempted to read past the end of the file (or at least if we
			// didn't get the number of bytes we were expecting), work out how many
			// samples we got based on the number of bytes read
			if (bytes_read < bytes_to_read)
			{
				samples_per_channel = bytes_read / (audio_cue->playback_header.num_channels * audio_cue->playback_header.bits_per_sample / 8);
				total_samples = samples_per_channel * audio_cue->playback_header.num_channels;
			}

			// Convert to float Apply audio scaling
			float *obp = out_buffer;
			int16_t *ibp = (int16_t*)read_buffer;
			for (size_t i = 0; i < total_samples; i++)
			{
				*(obp++) = *(ibp++) * audio_scaler;
			}
			
			// Write the audio data to the cue list, which handles the output
			if (audio_cue->playback_header.num_channels == 1)
			{
				// Write the same to both output channels (assumes two for now)
				stack_cue_list_write_audio(cue->parent, audio_cue->playback_audio_ptr, 0, out_buffer, samples_per_channel, 1);
				audio_cue->playback_audio_ptr = stack_cue_list_write_audio(cue->parent, audio_cue->playback_audio_ptr, 1, out_buffer, samples_per_channel, 1);
			}
			else
			{
				size_t initial_audio_ptr = audio_cue->playback_audio_ptr;

				for (uint16_t channel = 0; channel < audio_cue->playback_header.num_channels; channel++)
				{
					audio_cue->playback_audio_ptr = stack_cue_list_write_audio(cue->parent, initial_audio_ptr, channel, &out_buffer[channel], samples_per_channel, audio_cue->playback_header.num_channels);
				}
			}
			
			// Tidy up
			delete [] read_buffer;
			delete [] out_buffer;
		
			// Keep track of how much data we've sent to the audio device
			audio_cue->playback_data_sent += ((stack_time_t)samples_per_channel * NANOSECS_PER_SEC) / (stack_time_t)audio_cue->playback_header.sample_rate;
		}
#ifndef NO_MP3LAME
		else if (audio_cue->format == STACK_AUDIO_FILE_FORMAT_MP3)
		{
			PlaybackDataMP3* pbdata = (PlaybackDataMP3*)(audio_cue->playback_data);
			gssize data_read = 0;
			int total_samples = 0;

			// While there's data left and we don't have enough (at least 1024 samples)
			while (total_samples < 1024 && (data_read = g_input_stream_read((GInputStream*)audio_cue->playback_file_stream, pbdata->read_buffer, pbdata->read_buffer_size, NULL, NULL)) > 0)
			{
				// Decode more of the MP3 (ignoring headers), putting the data 
				// at the end of the buffer
				int decoded_samples = hip_decode(pbdata->decoder, pbdata->read_buffer, data_read, &pbdata->left[total_samples], &pbdata->right[total_samples]);
				if (decoded_samples > 0)
				{
					total_samples += decoded_samples;
				}
				else if (decoded_samples < 0)
				{
					fprintf(stderr, "stack_audio_cue_pulse(): Call returned -1\n");
					break;
				}
			}

			if (total_samples > 0)
			{
				// Allocate a buffer for float conversion and scaling
				float *out_buffer = new float[total_samples];

				// Convert to float, and apply audio scaling to left channel
				float *obp = out_buffer;
				int16_t *ibp = (int16_t*)pbdata->left;
				for (size_t i = 0; i < total_samples; i++)
				{
					*(obp++) = *(ibp++) * audio_scaler;
				}

				// Write left buffer to stream (don't update the stream pointer this time)
				stack_cue_list_write_audio(cue->parent, audio_cue->playback_audio_ptr, 0, out_buffer, total_samples, 1);

				// Convert to float, and apply audio scaling to right channel
				obp = out_buffer;
				ibp = (int16_t*)pbdata->right;
				for (size_t i = 0; i < total_samples; i++)
				{
					*(obp++) = *(ibp++) * audio_scaler;
				}

				// Write right buffer to stream
				audio_cue->playback_audio_ptr = stack_cue_list_write_audio(cue->parent, audio_cue->playback_audio_ptr, 1, out_buffer, total_samples, 1);

				// Tidy up
				delete [] out_buffer;
			
				// Keep track of how much data we've sent to the audio device
				audio_cue->playback_data_sent += ((stack_time_t)(total_samples) * NANOSECS_PER_SEC) / (stack_time_t)audio_cue->playback_header.sample_rate;
			}
		}
#endif
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

// Registers StackAudioCue with the application
void stack_audio_cue_register()
{
	// Register cue types
	StackCueClass* audio_cue_class = new StackCueClass{ "StackAudioCue", "StackCue", stack_audio_cue_create, stack_audio_cue_destroy, stack_audio_cue_play, NULL, stack_audio_cue_stop, stack_audio_cue_pulse, stack_audio_cue_set_tabs, stack_audio_cue_unset_tabs, stack_audio_cue_to_json, stack_audio_cue_free_json, stack_audio_cue_from_json };
	stack_register_cue_class(audio_cue_class);
}

