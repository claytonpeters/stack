// Includes:
#include "StackApp.h"
#include "StackAudioCue.h"
#include "MPEGAudioFile.h"
#include <cstring>
#include <cstdlib>
#include <math.h>
#include <json/json.h>
#ifndef NO_MP3LAME
#include <lame/lame.h>
#endif
#include <vector>
#include <time.h>

// RIFF Wave header structure
#pragma pack(push, 2)
typedef struct WaveHeader
{
	char chunk_id[4];
	uint32_t chunk_size;
	char format[4];
	char subchunk_1_id[4];
	uint32_t subchunk_1_size;
	uint16_t audio_format;
	uint16_t num_channels;
	uint32_t sample_rate;
	uint32_t byte_rate;
	uint16_t block_align;
	uint16_t bits_per_sample;
	char subchunk_2_id[4];
	uint32_t subchunk_2_size;
} WaveHeader;
#pragma pack(pop)

// Wave File Information
typedef struct FileDataWave
{
	WaveHeader header;
	stack_time_t file_length;
} FileDataWave;

#ifndef NO_MP3LAME
// We need some additional data for MP3 playback
typedef struct PlaybackDataMP3
{
	hip_t decoder;
	uint8_t *read_buffer;
	int32_t read_buffer_size;
	int16_t *left;
	int16_t *right;
	int16_t *left_ring;
	int16_t *right_ring;
	size_t ring_read_pointer;
	size_t ring_write_pointer;
	size_t ring_used;
	uint32_t size;
} PlaybackDataMP3;

// MP3 File information
typedef struct FileDataMP3
{
	uint32_t sample_rate;
	uint16_t num_channels;
	uint32_t byte_rate;
	uint16_t bits_per_sample;
	size_t audio_size;
	stack_time_t file_length;
	std::vector<MP3FrameInfo> frames;
} FileDataMP3;
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
	
	// Initialise our variables: cue data
	cue->file = strdup("");
	cue->media_start_time = 0;
	cue->media_end_time = 0;
	cue->file_length = 0;
	cue->play_volume = 0.0;
	cue->builder = NULL;
	cue->media_tab = NULL;
	cue->format = STACK_AUDIO_FILE_FORMAT_NONE;
	cue->file_data = NULL;

	// Initialise our variables: playback
	cue->playback_data_sent = 0;
	cue->playback_live_volume = 0.0;
	cue->playback_file = NULL;
	cue->playback_file_stream = NULL;
	cue->playback_audio_ptr = 0;
	cue->playback_data = NULL;

	// Initialise our variables: preview
	cue->preview_thread_run = false;
	cue->preview_surface = NULL;
	cue->preview_cr = NULL;
	cue->preview_start = 0;
	cue->preview_end = 0;
	cue->preview_widget = NULL;

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
	cue->preview_end = 9;
}

// Destroys an audio cue
static void stack_audio_cue_destroy(StackCue *cue)
{
	StackAudioCue *acue = STACK_AUDIO_CUE(cue);

	// Our tidyup here
	free(acue->file);
	
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

static void stack_audio_cue_free_file_data(StackAudioCue *cue)
{
	if (cue->file_data != NULL)
	{
		if (cue->format == STACK_AUDIO_FILE_FORMAT_WAVE)
		{
			delete (FileDataWave*)cue->file_data;
		}
#ifndef NO_MP3LAME
		else if (cue->format == STACK_AUDIO_FILE_FORMAT_MP3)
		{
			delete (FileDataMP3*)cue->file_data;
		}
#endif
	}
}

#ifndef NO_MP3LAME
static size_t stack_audio_cue_skip_id3(GInputStream *stream)
{
	return mpeg_audio_file_skip_id3(stream);
}

static bool stack_audio_cue_process_mp3(GInputStream *stream, FileDataMP3 *mp3_data, bool need_length)
{
	uint32_t frames = 0;
	uint64_t samples = 0;

	// Logging:
	fprintf(stderr, "stack_audio_cue_process_mp3(): Processing file...\n");

	if (!mpeg_audio_file_find_frames(stream, &mp3_data->num_channels, &mp3_data->sample_rate, &frames, &samples, &mp3_data->frames))
	{
		fprintf(stderr, "stack_audio_cue_process_mp3(): Processing failed\n");
		return false;
	}

	// Return our information
	mp3_data->byte_rate = mp3_data->num_channels * mp3_data->sample_rate * sizeof(short);
	mp3_data->bits_per_sample = 16;
	if (need_length)
	{
		mp3_data->audio_size = samples * mp3_data->num_channels * sizeof(short);
	}
	else
	{
		mp3_data->audio_size = 0;
	}

	// Return whether we succesfully parsed the header
	return true;
}
#endif

static bool stack_audio_cue_read_wavehdr(GInputStream *stream, FileDataWave *wave_data)
{
	if (g_input_stream_read(stream, &wave_data->header, 44, NULL, NULL) != 44)
	{
		fprintf(stderr, "stack_audio_cue_read_wavehdr(): Failed to read 44 byte header\n");
		return false;
	}
	
	// Check for RIFF header
	if (!(wave_data->header.chunk_id[0] == 'R' && wave_data->header.chunk_id[1] == 'I' && wave_data->header.chunk_id[2] == 'F' && wave_data->header.chunk_id[3] == 'F'))
	{
		fprintf(stderr, "stack_audio_cue_read_wavehdr(): Failed to find RIFF header\n");
		return false;
	}
		
	// Check for WAVE format
	if (!(wave_data->header.format[0] == 'W' && wave_data->header.format[1] == 'A' && wave_data->header.format[2] == 'V' && wave_data->header.format[3] == 'E'))
	{
		fprintf(stderr, "stack_audio_cue_read_wavehdr(): Failed to find WAVE format\n");
		return false;
	}
				
	// Check for 'fmt ' subchunk
	if (!(wave_data->header.subchunk_1_id[0] == 'f' && wave_data->header.subchunk_1_id[1] == 'm' && wave_data->header.subchunk_1_id[2] == 't' && wave_data->header.subchunk_1_id[3] == ' '))
	{
		fprintf(stderr, "stack_audio_cue_read_wavehdr(): Failed to find fmt chunk\n");
		return false;
	}

	// Check the audio_format. We support PCM (1) and IEEE-float (3)
	if (wave_data->header.audio_format != 1 && (wave_data->header.audio_format != 3 && wave_data->header.bits_per_sample != 32))
	{
		fprintf(stderr, "stack_audio_cue_read_wavehdr(): Non-PCM audio format (%d) is not supported\n", wave_data->header.audio_format);
		return false;
	}

	// If the byte rate isn't given, calculate it
	if (wave_data->header.byte_rate == 0)
	{
		wave_data->header.byte_rate = wave_data->header.sample_rate * wave_data->header.num_channels * wave_data->header.bits_per_sample / 8;
	}
					
	// Check that our second subchunk is "data"
	if (wave_data->header.subchunk_2_id[0] == 'd' && wave_data->header.subchunk_2_id[1] == 'a' && wave_data->header.subchunk_2_id[2] == 't' && wave_data->header.subchunk_2_id[3] == 'a')
	{
		return true;
	}

	// If our second subchunk is not data, search for the subchunk
						
	// Seek past chunk 2 (we've already read it's size as part of the normal
	// WaveHeader structure)
	g_seekable_seek(G_SEEKABLE(stream), wave_data->header.subchunk_2_size, G_SEEK_CUR, NULL, NULL);

	// Find the data chunk
	bool found_data_chunk = false;
	while (!found_data_chunk)
	{
		char chunk_id[4];
		uint32_t subchunk_size;

		// Read chunk ID and size
		if (g_input_stream_read(stream, &chunk_id, 4, NULL, NULL) == 4 && g_input_stream_read(stream, &subchunk_size, 4, NULL, NULL) == 4)
		{
			// If the chunk ID is data
			if (chunk_id[0] == 'd' && chunk_id[1] == 'a' && chunk_id[2] == 't' && chunk_id[3] == 'a')
			{
				// Stop searching
				found_data_chunk = true;
			}
			else
			{
				// Seek past the chunk
				g_seekable_seek(G_SEEKABLE(stream), subchunk_size, G_SEEK_CUR, NULL, NULL);
			}
		}
		else
		{
			fprintf(stderr, "stack_audio_cue_read_wavehdr(): Failed to read chunk ID\n");
			return false;
		}
	}
	
	return true;
}

StackAudioFileFormat stack_audio_cue_read_file_header(GFile *file, GFileInputStream *stream, void **file_data)
{
	// Validate parameters
	if (file_data == NULL)
	{
		return STACK_AUDIO_FILE_FORMAT_NONE;
	}

	if (file == NULL || stream == NULL)
	{
		*file_data = NULL;
		return STACK_AUDIO_FILE_FORMAT_NONE;
	}

	// Attempt to read a wave header
	FileDataWave wave_data;
	if (stack_audio_cue_read_wavehdr((GInputStream*)stream, &wave_data))
	{
		// Query the file info
		GFileInfo* file_info = g_file_query_info(file, "standard::size", G_FILE_QUERY_INFO_NONE, NULL, NULL);
		if (file_info != NULL)
		{
			// Get the file size and work out the length of the file in seconds
			goffset size = g_file_info_get_size(file_info);
			double seconds = double(size - 44) / double(wave_data.header.byte_rate);
		
			// Debug
			fprintf(stderr, "stack_audio_cue_read_file_header(): Wave: File info: %ld bytes / %d byte rate = %.4f seconds\n", size, wave_data.header.byte_rate, seconds);
			
			// Store the file length
			wave_data.file_length = (stack_time_t)(seconds * 1.0e9);

			// Store the data
			*file_data = new FileDataWave(wave_data);
			
			// Tidy up
			g_object_unref(file_info);

			// Set the result
			return STACK_AUDIO_FILE_FORMAT_WAVE;
		}
		else
		{
			fprintf(stderr, "stack_audio_cue_read_file_header(): Failed to get file info\n");
		}
	}
#ifndef NO_MP3LAME
	else
	{
		fprintf(stderr, "stack_audio_cue_file_read_header(): Didn't find Wave header, trying MP3\n");

		// Reset back to start of file
		if (!g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL))
		{
			fprintf(stderr, "stack_audio_cue_file_read_header(): Seek failed. MP3 Header will likely fail\n");
		}

		FileDataMP3 mp3_data;
		if (stack_audio_cue_process_mp3((GInputStream*)stream, &mp3_data, true))
		{
			double seconds = double(mp3_data.audio_size) / double(mp3_data.byte_rate);

			// Debug
			fprintf(stderr, "stack_audio_cue_file_header(): MP3: File info: %ld bytes / %d byte rate = %.4f seconds\n", mp3_data.audio_size, mp3_data.byte_rate, seconds);
			
			// Store the file length
			mp3_data.file_length = (stack_time_t)(seconds * 1.0e9);

			// Store the data
			*file_data = new FileDataMP3(mp3_data);

			return STACK_AUDIO_FILE_FORMAT_MP3;
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

	return STACK_AUDIO_FILE_FORMAT_NONE;
}

bool stack_audio_cue_set_file(StackAudioCue *cue, const char *uri)
{
	// Result
	bool result = false;

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

	// Read the file header
	void *file_data;
	StackAudioFileFormat format = stack_audio_cue_read_file_header(file, stream, &file_data);
	
	// If we successfully got a file, then update the cue
	if (format != STACK_AUDIO_FILE_FORMAT_NONE)
	{
		// Store the file data
		stack_audio_cue_free_file_data(cue);
		cue->file_data = file_data;
		cue->format = format;
	
		if (format == STACK_AUDIO_FILE_FORMAT_WAVE)
		{
			cue->file_length = ((FileDataWave*)cue->file_data)->file_length;
		}
#ifndef NO_MP3LAME
		else if (format == STACK_AUDIO_FILE_FORMAT_MP3)
		{
			cue->file_length = ((FileDataMP3*)cue->file_data)->file_length;
		}
#endif

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
		
		// Update the name if we don't currently have one
		if (STACK_CUE(cue)->name == NULL || strlen(STACK_CUE(cue)->name) == 0)
		{
			gchar* filename = g_filename_from_uri(uri, NULL, NULL);
			stack_cue_set_name(STACK_CUE(cue), basename(filename));
			free(filename);
		}

		// Update the cue action time;
		stack_audio_cue_update_action_time(cue);

		// Reset the audio preview to the full new file
		stack_audio_cue_preview_tidy(cue);
		cue->preview_start = 0;
		cue->preview_end = cue->file_length;

		// We succeeded
		result = true;
	}

	// Tidy up
	g_object_unref(stream);
	g_object_unref(file);

	// Notify cue list that we've changed
	stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue));

	return result;
}

// Thread to generate the code preview
static void stack_audio_cue_preview_thread(StackAudioCue *cue)
{
	fprintf(stderr, "stack_audio_cue_preview_thread(): started\n");

	// Open the file
	GFile *file = g_file_new_for_uri(cue->file);
	if (file == NULL)
	{
		fprintf(stderr, "stack_audio_cue_preview_thread(): file open failed\n");
		return;
	}
	
	// Open a stream
	GFileInputStream *stream = g_file_read(file, NULL, NULL);
	if (stream == NULL)
	{
		fprintf(stderr, "stack_audio_cue_preview_thread(): stream open failed\n");
		g_object_unref(file);
		return;
	}

	// Flag that the thread is running
	cue->preview_thread_run = true;

	// Read the file header
	void *file_data;
	StackAudioFileFormat format = stack_audio_cue_read_file_header(file, stream, &file_data);

	// Initialise drawing: fill the background
	cairo_set_source_rgb(cue->preview_cr, 0.1, 0.1, 0.1);
	cairo_paint(cue->preview_cr);

	// Initialise drawing: prepare for lines
	cairo_set_antialias(cue->preview_cr, CAIRO_ANTIALIAS_FAST);
	cairo_set_line_width(cue->preview_cr, 1.0);
	cairo_set_source_rgb(cue->preview_cr, 0.0, 0.8, 0.0);
	cairo_move_to(cue->preview_cr, 0.0, cue->preview_height / 2.0);

	// We force a redraw periodically, get the current time
	time_t last_redraw_time = time(NULL);

	// If we successfully got a file, we can start previewing
	if (format == STACK_AUDIO_FILE_FORMAT_WAVE)
	{
		// Handy pointer
		FileDataWave *wave_data = (FileDataWave*)file_data;

		size_t samples_per_channel = 1024;
		size_t total_samples = samples_per_channel * wave_data->header.num_channels;
			
		// Set up buffer for the number of samples of each channel
		size_t bytes_to_read = total_samples * wave_data->header.bits_per_sample / 8;

		// Allocate buffers		
		char *read_buffer = new char[bytes_to_read];
		bool no_more_data = false;
		uint64_t sample = 0;

		while (cue->preview_thread_run && !no_more_data)
		{
			// Read data
			gssize bytes_read = g_input_stream_read((GInputStream*)stream, read_buffer, bytes_to_read, NULL, NULL);

			// If we've read data
			if (bytes_read > 0)
			{
				// If we attempted to read past the end of the file (or at least if we
				// didn't get the number of bytes we were expecting), work out how many
				// samples we got based on the number of bytes read
				if (bytes_read < bytes_to_read)
				{
					samples_per_channel = bytes_read / (wave_data->header.num_channels * wave_data->header.bits_per_sample / 8);
					total_samples = samples_per_channel * wave_data->header.num_channels;
				}

				// Calculate the width of a single sample on the surface
				uint64_t samples_in_file = (uint64_t)((double)wave_data->file_length / 1.0e9 * (double)wave_data->header.sample_rate);
				double sample_width = cue->preview_width / (double)samples_in_file;
				double half_preview_height = cue->preview_height / 2.0;

				// Draw the audio data. We sum all the channels together to draw a single waveform
				if (wave_data->header.bits_per_sample == 8)
				{
					// Calculate the highest value given 8-bits and the number of channels
					float range_max = 128.0 * (float)wave_data->header.num_channels;

					int8_t *ibp = (int8_t*)read_buffer;
					for (size_t i = 0; i < samples_per_channel; i++, sample++)
					{
						// Sum all the channels
						float y = 0;
						for (size_t j = 0; j < wave_data->header.num_channels; j++)
						{
							y += (float)*(ibp++);
						}

						// Scale y back in to -1.0 < y < 1.0
						y /= range_max;

						// Draw the line
						cairo_line_to(cue->preview_cr, (double)sample * sample_width, half_preview_height * (1.0 - y));
					}
				}
				else if (wave_data->header.bits_per_sample == 16)
				{
					// Calculate the highest value given 16-bits and the number of channels
					float range_max = 32768.0 * (float)wave_data->header.num_channels;

					int16_t *ibp = (int16_t*)read_buffer;
					for (size_t i = 0; i < samples_per_channel; i++, sample++)
					{
						// Sum all the channels
						float y = 0;
						for (size_t j = 0; j < wave_data->header.num_channels; j++)
						{
							y += (float)*(ibp++);
						}

						// Scale y back in to -1.0 < y < 1.0
						y /= range_max;

						// Draw the line
						cairo_line_to(cue->preview_cr, (double)sample * sample_width, half_preview_height * (1.0 - y));
					}
				}
				else if (wave_data->header.bits_per_sample == 32)
				{
					// Calculate the highest value given floating point (-1.0 < x < 1.0) 
					// and the number of channels
					float range_max = 1.0 * (float)wave_data->header.num_channels;

					float *ibp = (float*)read_buffer;
					for (size_t i = 0; i < samples_per_channel; i++, sample++)
					{
						// Sum all the channels
						float y = 0;
						for (size_t j = 0; j < wave_data->header.num_channels; j++)
						{
							y += *(ibp++);
						}

						// Scale y back in to -1.0 < y < 1.0
						y /= range_max;

						// Draw the line
						cairo_line_to(cue->preview_cr, (double)sample * sample_width, half_preview_height * (1.0 - y));
					}

				}

				// Intentionally slow this down so that we don't
				// chew through CPU that might be needed for playback
				usleep(500);

				// Redraw once a second
				if (time(NULL) - last_redraw_time > 1)
				{
					cairo_stroke(cue->preview_cr);
					gtk_widget_queue_draw(cue->preview_widget);
					last_redraw_time = time(NULL);
				}
			}
			else
			{
				no_more_data = true;

				// We've finished - force a redraw
				cairo_stroke(cue->preview_cr);
				gtk_widget_queue_draw(cue->preview_widget);
			}
		}
			
		// Tidy up
		delete [] read_buffer;

		// Tidy up
		delete wave_data;	
	}
#ifndef NO_MP3LAME
	else if (format == STACK_AUDIO_FILE_FORMAT_MP3)
	{
		// Handy pointer
		FileDataMP3 *mp3_data = (FileDataMP3*)file_data;

		// Tidy up
		delete mp3_data;	
	}
#endif
	else
	{
		fprintf(stderr, "stack_audio_cue_preview_thread(): Cannot preview: unknown file format\n");
	}

	// Tidy up
	g_object_unref(stream);
	g_object_unref(file);

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

static void acp_play_section_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
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

	// Draw the text
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_move_to(cr, 0.0, text_size.height + 2);
	cairo_show_text(cr, "0:00.000");
	stack_format_time_as_string(cue->file_length, time_buffer, 32);
	cairo_text_extents(cr, time_buffer, &text_size);
	cairo_move_to(cr, width - text_size.width - 2, text_size.height + 2);
	cairo_show_text(cr, time_buffer);

	// TODO: Draw more
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
		stack_audio_cue_preview_generate(cue, 0, 0, width, graph_height);
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
	double section_left = (double)width * (double)cue->media_start_time / (double)cue->file_length;
	double section_right = (double)width * (double)cue->media_end_time / (double)cue->file_length;
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
		audio_cue->playback_file = NULL;
		return false;
	}

	if (audio_cue->format == STACK_AUDIO_FILE_FORMAT_WAVE)
	{
		// Skip to the appropriate point in the file
		g_seekable_seek(G_SEEKABLE(audio_cue->playback_file_stream), 
			sizeof(WaveHeader) + (audio_cue->media_start_time * (stack_time_t)((FileDataWave*)audio_cue->file_data)->header.byte_rate / NANOSECS_PER_SEC),
			G_SEEK_CUR, NULL, NULL);
	}
#ifndef NO_MP3LAME
	else if (audio_cue->format == STACK_AUDIO_FILE_FORMAT_MP3)
	{
		// Initialise playback
		audio_cue->playback_data = (void*)new PlaybackDataMP3;
		PlaybackDataMP3* pbdata = (PlaybackDataMP3*)(audio_cue->playback_data);
		pbdata->decoder = hip_decode_init();
		pbdata->read_buffer = new uint8_t[1024];
		pbdata->read_buffer_size = 1024;
		pbdata->size = 100000;
		pbdata->left = new int16_t[pbdata->size];
		pbdata->right = new int16_t[pbdata->size];
		pbdata->left_ring = new int16_t[pbdata->size];
		pbdata->right_ring = new int16_t[pbdata->size];
		pbdata->ring_read_pointer = 0;
		pbdata->ring_write_pointer = 0;
		pbdata->ring_used = 0;

		FileDataMP3* file_data = (FileDataMP3*)audio_cue->file_data;
		int64_t start_sample = ((double)file_data->sample_rate * (double)audio_cue->media_start_time / NANOSECS_PER_SEC);

		// Iterate through the known frames
		for (auto frame : file_data->frames)
		{
			// If the frame we're looking at contains our start_sample
			if (frame.sample_position + frame.frame_size_samples >= start_sample)
			{
				// Seek to the start of the frame
				fprintf(stderr, "start_sample = %d, frame sample_position = %d, frame byte_position = %d\n", start_sample, frame.sample_position, frame.byte_position);
				g_seekable_seek(G_SEEKABLE(audio_cue->playback_file_stream), frame.byte_position, G_SEEK_SET, NULL, NULL);
				break;
			}
		}

		// Skip past the ID3 tags (if there are any)
		//size_t bytes_read = stack_audio_cue_skip_id3((GInputStream*)audio_cue->playback_file_stream);
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
		delete [] pbdata->left_ring;
		delete [] pbdata->right_ring;

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
	bool no_more_data = false;

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
	float base_audio_scaler = stack_db_to_scalar(audio_cue->playback_live_volume);
	
	// If we've sent no data, or we've running behind, or we're about to need more data
	while (!no_more_data && (audio_cue->playback_data_sent == 0 || audio_cue->playback_data_sent < action_time + 20000000))
	{
		stack_time_t just_sent = 0;

		if (audio_cue->format == STACK_AUDIO_FILE_FORMAT_WAVE)
		{
			FileDataWave* wave_data = ((FileDataWave*)audio_cue->file_data);

			size_t samples_per_channel = 1024;
			size_t total_samples = samples_per_channel * wave_data->header.num_channels;
			
			// Set up buffer for the number of samples of each channel
			size_t bytes_to_read = total_samples * wave_data->header.bits_per_sample / 8;

			// Allocate buffers		
			char *read_buffer = new char[bytes_to_read];
			float *out_buffer = new float[total_samples];
			
			// Read data
			gssize bytes_read = g_input_stream_read((GInputStream*)audio_cue->playback_file_stream, read_buffer, bytes_to_read, NULL, NULL);

			// If we've read data
			if (bytes_read > 0)
			{
				// If we attempted to read past the end of the file (or at least if we
				// didn't get the number of bytes we were expecting), work out how many
				// samples we got based on the number of bytes read
				if (bytes_read < bytes_to_read)
				{
					samples_per_channel = bytes_read / (wave_data->header.num_channels * wave_data->header.bits_per_sample / 8);
					total_samples = samples_per_channel * wave_data->header.num_channels;
				}

				// Calculate audio scaler. Volume (fading) is taken from base_audio_scaler,
				// and the rest convert audio to -1.0 < y < 1.0
				float audio_scaler = base_audio_scaler;
				if (wave_data->header.bits_per_sample == 8)
				{
					audio_scaler /= 128.0f;
				}
				else if (wave_data->header.bits_per_sample == 16)
				{
					audio_scaler /= 32768.0f;
				}
				// (32-bit audio is already in the correct range as it's floating point)

				// Convert to float and apply audio scaling
				float *obp = out_buffer;
				if (wave_data->header.bits_per_sample == 8)
				{
					int8_t *ibp = (int8_t*)read_buffer;
					for (size_t i = 0; i < total_samples; i++)
					{
						*(obp++) = *(ibp++) * audio_scaler;
					}
				}
				else if (wave_data->header.bits_per_sample == 16)
				{
					int16_t *ibp = (int16_t*)read_buffer;
					for (size_t i = 0; i < total_samples; i++)
					{
						*(obp++) = *(ibp++) * audio_scaler;
					}
				}
				else if (wave_data->header.bits_per_sample == 32)
				{
					float *ibp = (float*)read_buffer;
					for (size_t i = 0; i < total_samples; i++)
					{
						*(obp++) = *(ibp++) * audio_scaler;
					}
				}
				
				// Write the audio data to the cue list, which handles the output
				if (wave_data->header.num_channels == 1)
				{
					// Write the same to both output channels (assumes two for now)
					stack_cue_list_write_audio(cue->parent, audio_cue->playback_audio_ptr, 0, out_buffer, samples_per_channel, 1);
					audio_cue->playback_audio_ptr = stack_cue_list_write_audio(cue->parent, audio_cue->playback_audio_ptr, 1, out_buffer, samples_per_channel, 1);
				}
				else
				{
					size_t initial_audio_ptr = audio_cue->playback_audio_ptr;

					for (uint16_t channel = 0; channel < wave_data->header.num_channels; channel++)
					{
						audio_cue->playback_audio_ptr = stack_cue_list_write_audio(cue->parent, initial_audio_ptr, channel, &out_buffer[channel], samples_per_channel, wave_data->header.num_channels);
					}
				}
			}
			else
			{
				// Break out of the outer loop
				no_more_data = true;
			}
			
			// Tidy up
			delete [] read_buffer;
			delete [] out_buffer;
		
			// Keep track of how much data we've sent to the audio device
			just_sent = ((stack_time_t)samples_per_channel * NANOSECS_PER_SEC) / (stack_time_t)wave_data->header.sample_rate;
		}
#ifndef NO_MP3LAME
		else if (audio_cue->format == STACK_AUDIO_FILE_FORMAT_MP3)
		{
			PlaybackDataMP3* pbdata = (PlaybackDataMP3*)(audio_cue->playback_data);
			gssize data_read = 0;
			int total_samples = 0;

			// MP3s always decode to 16-bit signed integer
			float audio_scaler = base_audio_scaler / 32768.0;

			// While we don't have enough to play in the ring buffers (at least 1024 samples)
			while (pbdata->ring_used < 1024)
			{
				// Read some more data
				data_read = g_input_stream_read((GInputStream*)audio_cue->playback_file_stream, pbdata->read_buffer, pbdata->read_buffer_size, NULL, NULL);
				if (data_read <= 0)
				{
					// Assume EOF, break out of both the inner most and outermost loop
					no_more_data = true;
					break;
				}

				// Decode more of the MP3 (ignoring headers), putting it into our left/right (non-ring) buffers
				int decoded_samples = hip_decode(pbdata->decoder, pbdata->read_buffer, data_read, pbdata->left, pbdata->right);
				if (decoded_samples > 0)
				{
					// If it can all be written to the ring buffers at once
					if (pbdata->ring_write_pointer + decoded_samples <= pbdata->size)
					{
						// Write left and right data into ring buffers
						memcpy(&pbdata->left_ring[pbdata->ring_write_pointer], pbdata->left, sizeof(int16_t) * decoded_samples);
						memcpy(&pbdata->right_ring[pbdata->ring_write_pointer], pbdata->right, sizeof(int16_t) * decoded_samples);
					}
					else
					{
						// Write left and right into end of ring buffer
						memcpy(&pbdata->left_ring[pbdata->ring_write_pointer], pbdata->left, sizeof(int16_t) * (pbdata->size - pbdata->ring_write_pointer));
						memcpy(&pbdata->right_ring[pbdata->ring_write_pointer], pbdata->right, sizeof(int16_t) * (pbdata->size - pbdata->ring_write_pointer));

						// Write remaining left and right into start of ring buffer
						memcpy(pbdata->left_ring, &pbdata->left[pbdata->size - pbdata->ring_write_pointer], sizeof(int16_t) * (decoded_samples - (pbdata->size - pbdata->ring_write_pointer)));
						memcpy(pbdata->right_ring, &pbdata->right[pbdata->size - pbdata->ring_write_pointer], sizeof(int16_t) * (decoded_samples - (pbdata->size - pbdata->ring_write_pointer)));
					}

					// Update ring buffer details
					pbdata->ring_write_pointer = (pbdata->ring_write_pointer + decoded_samples) % pbdata->size;
					pbdata->ring_used += decoded_samples;
				}
				else if (decoded_samples < 0)
				{
					fprintf(stderr, "stack_audio_cue_pulse(): Call returned -1\n");
					break;
				}
			}

			// If there's data in the ring buffer
			if (pbdata->ring_used > 0)
			{
				// Figure out how much to write, up to 1024 samples
				if (pbdata->ring_used >= 1024)
				{
					total_samples = 1024;
				}
				else
				{
					total_samples = pbdata->ring_used;
				}

				// Allocate a buffer for float conversion and scaling
				float *out_buffer = new float[total_samples];

				// Convert to float, and apply audio scaling to left channel
				if (pbdata->ring_read_pointer + total_samples < pbdata->size)
				{
					float *obp = out_buffer;
					int16_t *ibp = (int16_t*)&pbdata->left_ring[pbdata->ring_read_pointer];
					for (size_t i = 0; i < total_samples; i++)
					{
						*(obp++) = *(ibp++) * audio_scaler;
					}
				}
				else
				{
					float *obp = out_buffer;

					// End of ring buffer
					int16_t *ibp = (int16_t*)&pbdata->left_ring[pbdata->ring_read_pointer];
					for (size_t i = pbdata->ring_read_pointer; i < pbdata->size; i++)
					{
						*(obp++) = *(ibp++) * audio_scaler;
					}

					// Start of ring buffer
					ibp = (int16_t*)pbdata->left_ring;
					for (size_t i = pbdata->size - pbdata->ring_read_pointer; i < total_samples; i++)
					{
						*(obp++) = *(ibp++) * audio_scaler;
					}
				}

				// Write left buffer to stream (don't update the stream pointer this time)
				stack_cue_list_write_audio(cue->parent, audio_cue->playback_audio_ptr, 0, out_buffer, total_samples, 1);

				// Convert to float, and apply audio scaling to right channel
				if (pbdata->ring_read_pointer + total_samples < pbdata->size)
				{
					float *obp = out_buffer;
					int16_t *ibp = (int16_t*)&pbdata->right_ring[pbdata->ring_read_pointer];
					for (size_t i = 0; i < total_samples; i++)
					{
						*(obp++) = *(ibp++) * audio_scaler;
					}
				}
				else
				{
					float *obp = out_buffer;

					// End of ring buffer
					int16_t *ibp = (int16_t*)&pbdata->right_ring[pbdata->ring_read_pointer];
					for (size_t i = pbdata->ring_read_pointer; i < pbdata->size; i++)
					{
						*(obp++) = *(ibp++) * audio_scaler;
					}

					// Start of ring buffer
					ibp = (int16_t*)pbdata->right_ring;
					for (size_t i = pbdata->size - pbdata->ring_read_pointer; i < total_samples; i++)
					{
						*(obp++) = *(ibp++) * audio_scaler;
					}
				}

				// Write right buffer to stream
				audio_cue->playback_audio_ptr = stack_cue_list_write_audio(cue->parent, audio_cue->playback_audio_ptr, 1, out_buffer, total_samples, 1);

				// Tidy up
				delete [] out_buffer;
			
				// Update ring buffer details
				pbdata->ring_read_pointer = (pbdata->ring_read_pointer + total_samples) % pbdata->size;
				pbdata->ring_used -= total_samples;

				// Keep track of how much data we've sent to the audio device
				just_sent = ((stack_time_t)(total_samples) * NANOSECS_PER_SEC) / (stack_time_t)((FileDataMP3*)audio_cue->file_data)->sample_rate;
			}
		}
#endif
		else
		{
			// Unknown format, so we have no more data
			no_more_data = true;
		}

		audio_cue->playback_data_sent += just_sent;
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
	if (STACK_AUDIO_CUE(cue)->format == STACK_AUDIO_FILE_FORMAT_NONE)
	{
		snprintf(message, size, "Invalid audio file");
	}
	else
	{
		snprintf(message, size, "");
	}
}

// Registers StackAudioCue with the application
void stack_audio_cue_register()
{
	// Register cue types
	StackCueClass* audio_cue_class = new StackCueClass{ "StackAudioCue", "StackCue", stack_audio_cue_create, stack_audio_cue_destroy, stack_audio_cue_play, NULL, stack_audio_cue_stop, stack_audio_cue_pulse, stack_audio_cue_set_tabs, stack_audio_cue_unset_tabs, stack_audio_cue_to_json, stack_audio_cue_free_json, stack_audio_cue_from_json, stack_audio_cue_get_error };
	stack_register_cue_class(audio_cue_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_audio_cue_register();
	return true;
}

