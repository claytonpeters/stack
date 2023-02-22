// Includes:
#include "StackAudioFile.h"
#include "StackAudioPreview.h"
#include "StackLog.h"
#include <cmath>

// Provides an implementation of stack_audio_preview_get_type
G_DEFINE_TYPE(StackAudioPreview, stack_audio_preview, GTK_TYPE_WIDGET)

GtkWidget *stack_audio_preview_new()
{
	// Create the new object
	GtkWidget *widget = GTK_WIDGET(g_object_new(stack_audio_preview_get_type(), NULL, NULL));
	StackAudioPreview *preview = STACK_AUDIO_PREVIEW(widget);

	return widget;
}

// Tidy up the preview
static void stack_audio_preview_tidy_buffer(StackAudioPreview *preview)
{
	// Tell the thread to stop
	preview->thread_running = false;

	// Stop the thread if it's running
	if (preview->render_thread.joinable())
	{
		preview->render_thread.join();
	}

	// Tidy up
	if (preview->buffer_surface)
	{
		cairo_surface_destroy(preview->buffer_surface);
		preview->buffer_surface = NULL;
	}
	if (preview->buffer_cr)
	{
		cairo_destroy(preview->buffer_cr);
		preview->buffer_cr = NULL;
	}

	// Reset variables
	preview->start_time = 0;
	preview->end_time = 0;
}

// Queues a redraw of the preview widget. Designed to be called from
// gdk_threads_add_idle so as to be UI-thread-safe
static gboolean stack_audio_preview_idle_redraw(gpointer user_data)
{
	gtk_widget_queue_draw(GTK_WIDGET(user_data));
	return G_SOURCE_REMOVE;
}

static uint64_t stack_audio_preview_render_points(const StackAudioPreview *preview, const StackAudioFile *file, const float *data, uint32_t frames, uint64_t preview_start_samples, uint64_t data_start_frames, bool downsample)
{
	uint64_t frame = data_start_frames;

	// Calculate the preview width in samples
	float preview_width_samples = (float)file->sample_rate * (float)(preview->end_time - preview->start_time) / NANOSECS_PER_SEC_F;
	double sample_width = preview->surface_width / (double)preview_width_samples;
	double half_preview_height = preview->surface_height / 2.0;

	// Calculate the highest value given floating point (-1.0 < x < 1.0)
	// and the number of channels
	float scalar = 1.0 / (float)file->channels;

	// Setup for min/max calculation
	float min = std::numeric_limits<float>::max();
	float max = std::numeric_limits<float>::min();

	// Draw the audio data. We sum all the channels together to draw a single waveform
	for (size_t i = 0; i < frames; i++, frame++)
	{
		// Sum all the channels
		float y = 0;
		for (size_t channel = 0; channel < file->channels; channel++)
		{
			y += *(data++);
		}

		// Scale y back in to -1.0 < y < 1.0
		y *= scalar;

		if (!downsample)
		{
			// If we're not downsampling, draw the line
			cairo_line_to(preview->buffer_cr, (double)(frame - preview_start_samples) * sample_width, half_preview_height * (1.0 - y));
		}
		else
		{
			// If we're downsampling, just calculate the min and max for the block
			if (y > max) { max = y; }
			if (y < min) { min = y; }
		}
	}

	// If we're downsampling, we just draw a single vertical line from min to max
	if (downsample)
	{
		// Our x position is the position of the sample in the middle of our frame
		const double x = (double)(data_start_frames + (frames / 2) - preview_start_samples) * sample_width;

		// Draw the line
		cairo_move_to(preview->buffer_cr, x, half_preview_height * (1.0 - min));
		cairo_line_to(preview->buffer_cr, x, half_preview_height * (1.0 - max));
	}

	return frame;
}


// Thread to generate the code preview
static void stack_audio_preview_render_thread(StackAudioPreview *preview)
{
	// Set the thread name
	pthread_setname_np(pthread_self(), "stack-preview");

	stack_log("stack_audio_preview_preview_thread(): started\n");

	// Open the file
	StackAudioFile *file = stack_audio_file_create(preview->file);
	if (file == NULL)
	{
		stack_log("stack_audio_preview_preview_thread(): file open failed\n");
		return;
	}

	// Flag that the thread is running
	preview->thread_running = true;

	// Initialise drawing: fill the background
	cairo_set_source_rgb(preview->buffer_cr, 0.1, 0.1, 0.1);
	cairo_paint(preview->buffer_cr);

	// Initialise drawing: prepare for lines
	cairo_set_antialias(preview->buffer_cr, CAIRO_ANTIALIAS_FAST);
	cairo_set_source_rgb(preview->buffer_cr, 0.0, 0.8, 0.0);

	// We force a redraw periodically, get the current time
	stack_time_t last_redraw_time = stack_get_clock_time();

	// Seek to the right time in the file
	stack_audio_file_seek(file, preview->start_time);

	// Convert out start and end times from seconds to samples
	uint64_t preview_start_samples = stack_time_to_samples(preview->start_time, file->sample_rate);
	uint64_t preview_end_samples = stack_time_to_samples(preview->end_time, file->sample_rate);

	// Calculate how many audio frames fit in to one pixel on the preview
	size_t frames_per_pixel = file->frames / preview->surface_width;

	// Default to not downsampling (render every frame)
	bool downsample = false;
	size_t frames = 1024;

	// Determine if we should downsample
	if (frames_per_pixel > 10)
	{
		frames = frames_per_pixel;
		downsample = true;

		// Bump up the line width a smidge to prevent tiny gaps in the
		// waveform caused by rounding errors of it not being _exactly_
		// one pixel between blocks of frames
		cairo_set_line_width(preview->buffer_cr, 1.1);
	}
	else
	{
		cairo_set_line_width(preview->buffer_cr, 1.0);
		cairo_move_to(preview->buffer_cr, 0.0, preview->surface_height / 2.0);
	}

	// Calculate how many samples we need to read
	size_t samples = frames * file->channels;

	// Allocate buffers
	float *read_buffer = new float[samples];
	bool no_more_data = false;
	uint64_t sample = preview_start_samples;

	// Calculate the width of a single sample on the surface
	uint64_t samples_in_file = (uint64_t)((double)file->length / 1.0e9 * (double)file->sample_rate);

	while (preview->thread_running && !no_more_data && sample < preview_end_samples)
	{
		// Get some more data
		size_t frames_read = stack_audio_file_read(file, read_buffer, frames);

		// If we've read data
		if (frames_read > 0)
		{
			// Render the samples
			sample = stack_audio_preview_render_points(preview, file, read_buffer, frames_read, preview_start_samples, sample, downsample);

			// Redraw a few times per second
			if (stack_get_clock_time() - last_redraw_time > NANOSECS_PER_SEC / 25)
			{
				cairo_stroke(preview->buffer_cr);
				gdk_threads_add_idle(stack_audio_preview_idle_redraw, preview);
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
	stack_audio_file_destroy(file);

	// We've finished - force a redraw
	cairo_stroke(preview->buffer_cr);
	gdk_threads_add_idle(stack_audio_preview_idle_redraw, preview);

	// Finished
	preview->thread_running = false;

	return;
}

// Generates a new audio preview
// @param preview The cue to preview
// @param start The starting time of the preview (on the left hand side of the image)
// @param end The ending time of the preview (on the right hand side of the image)
// @param width The width of the preview image
// @param height The height of the preview image
static void stack_audio_preview_generate(StackAudioPreview *preview, stack_time_t start, stack_time_t end, int width, int height)
{
	stack_audio_preview_tidy_buffer(preview);

	// Store details
	preview->start_time = start;
	preview->end_time = end;
	preview->surface_width = width;
	preview->surface_height = height;

	// Create a new surface
	preview->buffer_surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
	preview->buffer_cr = cairo_create(preview->buffer_surface);
	cairo_set_source_rgb(preview->buffer_cr, 0.1, 0.1, 0.1);
	cairo_paint(preview->buffer_cr);

	// Start a new thead for the rendering
	preview->render_thread = std::thread(stack_audio_preview_render_thread, preview);
}

static gboolean stack_audio_preview_draw(GtkWidget *widget, cairo_t *cr)
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

	cairo_text_extents_t text_size;
	char time_buffer[32];

	StackAudioPreview *preview = STACK_AUDIO_PREVIEW(widget);

	// Get details
	guint width = gtk_widget_get_allocated_width(widget);
	guint height = gtk_widget_get_allocated_height(widget);

	// Fill the background behind the text
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
	cairo_text_extents(cr, "0:00.000", &text_size);
	cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
	cairo_rectangle(cr, 0.0, 0.0, width, text_size.height + 4);
	cairo_fill(cr);

	// Fill the background
	cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
	cairo_paint(cr);

	// Figure out the height of the graph itself (so less the timing bar)
	guint top_bar_height = text_size.height + 4;
	guint graph_height = height - top_bar_height;
	guint half_graph_height = graph_height / 2;

	if (preview->file != NULL)
	{
		// Decide if we need to (re)generate a preview
		bool generate_preview = false;
		if (preview->buffer_cr == NULL || preview->buffer_surface == NULL)
		{
			generate_preview = true;
		}
		else if (preview->buffer_cr != NULL && preview->buffer_surface != NULL)
		{
			if (preview->surface_width != width || preview->surface_height != graph_height)
			{
				generate_preview = true;
			}
		}

		// Generate a preview if required
		if (generate_preview)
		{
			stack_audio_preview_generate(preview, preview->start_time, preview->end_time, width, graph_height);
		}
		else
		{
			// If we have a surface, draw it
			if (preview->buffer_cr != NULL && preview->buffer_surface != NULL)
			{
				cairo_set_source_surface(cr, preview->buffer_surface, 0, top_bar_height);
				cairo_rectangle(cr, 0.0, top_bar_height, width, graph_height);
				cairo_fill(cr);
			}
		}
	}

	// Draw the zero line
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
	cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
	cairo_move_to(cr, 0.0, top_bar_height + half_graph_height);
	cairo_line_to(cr, width, top_bar_height + half_graph_height);
	cairo_stroke(cr);

	// Calculate where the playback section appears on the graph
	double fp_width = (double)width;
	double preview_length = preview->end_time - preview->start_time;
	double section_left = fp_width * (double)preview->sel_start_time / preview_length;
	double section_right = fp_width * (double)preview->sel_end_time / preview_length;
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
	stack_format_time_as_string(preview->start_time, time_buffer, 32);
	cairo_show_text(cr, time_buffer);
	cairo_text_extents(cr, time_buffer, &text_size);
	cairo_set_line_width(cr, 1.0);

	// Store details of zero position
	double last_text_x = 0.0f, last_text_width = text_size.width;
	stack_time_t last_time = preview->start_time;

	// Draw labels until the end
	bool found_label = true;
	while (found_label && last_text_x < fp_width)
	{
		// Identify the next time that we can draw without overlap
		stack_time_t new_time = 0;
		double new_text_x = 0.0;
		found_label = false;
		for (size_t i = 0; i < num_increments; i++)
		{
			new_time = last_time + increments[i];
			new_text_x = fp_width * (double)(new_time - preview->start_time) / preview_length;

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
			cairo_move_to(cr, round(new_text_x) + 1, text_size.height + 2);
			cairo_show_text(cr, time_buffer);

			// Draw a tick mark (3 pixels high)
			cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
			cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
			cairo_move_to(cr, round(new_text_x), 0.0);
			cairo_line_to(cr, round(new_text_x), top_bar_height + 3);
			cairo_stroke(cr);

			// Store the position of this label
			last_text_x = new_text_x;
			last_text_width = text_size.width;
			last_time = new_time;
		}
	}

	// Whilst in playback, draw a playback marker
	if (preview->show_playback_marker)
	{
		double playback_x = fp_width * (double)(preview->playback_time - preview->start_time) / preview_length;
		if (playback_x > 0.0 && playback_x < width)
		{
			cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
			cairo_set_source_rgb(cr, 1.0, 1.0, 0.0);
			cairo_move_to(cr, playback_x, 0);
			cairo_line_to(cr, playback_x, height);
			cairo_stroke(cr);
		}
	}

	return false;
}

static void stack_audio_preview_button(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	// TODO
}

static void stack_audio_preview_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
	// TODO
}

static void stack_audio_preview_init(StackAudioPreview *preview)
{
	gtk_widget_set_has_window(GTK_WIDGET(preview), true);
	preview->window = NULL;
	preview->file = NULL;
	preview->thread_running = false;
	preview->buffer_cr = NULL;
	preview->buffer_surface = NULL;
	preview->surface_width = 0;
	preview->surface_height = 0;
	preview->start_time = 0;
	preview->end_time = 0;
	preview->sel_start_time = 0;
	preview->sel_end_time = 0;
	preview->show_playback_marker = false;
	preview->playback_time = 0;
	preview->last_redraw_time = 0;

	g_signal_connect(preview, "button-press-event", G_CALLBACK(stack_audio_preview_button), NULL);
	g_signal_connect(preview, "scroll-event", G_CALLBACK(stack_audio_preview_scroll), NULL);
}

static void stack_audio_preview_realize(GtkWidget *widget)
{
	// Note that the Gtk+ docs say you should usually chain up here... but most
	// examples I've found don't, and I've yet to make anything work when I do

	GtkAllocation allocation;
	gtk_widget_get_allocation(widget, &allocation);

	GdkWindowAttr attr;
	attr.x = allocation.x;
	attr.y = allocation.y;
	attr.width = allocation.width;
	attr.height = allocation.height;
	attr.wclass = GDK_INPUT_OUTPUT;
	attr.window_type = GDK_WINDOW_CHILD;
	attr.event_mask = gtk_widget_get_events(widget) | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK;
	attr.visual = gtk_widget_get_visual(widget);

	GdkWindow *parent = gtk_widget_get_parent_window(widget);
	STACK_AUDIO_PREVIEW(widget)->window = gdk_window_new(parent, &attr, GDK_WA_WMCLASS | GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL);

	// Register our window with the widget
	gtk_widget_set_window(widget, STACK_AUDIO_PREVIEW(widget)->window);
	gtk_widget_register_window(widget, STACK_AUDIO_PREVIEW(widget)->window);
	gtk_widget_set_realized(widget, true);
}

static void stack_audio_preview_unrealize(GtkWidget *widget)
{
	gtk_widget_set_realized(widget, false);
	gtk_widget_unregister_window(widget, STACK_AUDIO_PREVIEW(widget)->window);
	gtk_widget_set_window(widget, NULL);

	gdk_window_destroy(STACK_AUDIO_PREVIEW(widget)->window);
	STACK_AUDIO_PREVIEW(widget)->window = NULL;
}

static void stack_audio_preview_map(GtkWidget *widget)
{
	// Chain up
	GTK_WIDGET_CLASS(stack_audio_preview_parent_class)->map(widget);

	gdk_window_show(STACK_AUDIO_PREVIEW(widget)->window);
}

static void stack_audio_preview_unmap(GtkWidget *widget)
{
	gdk_window_hide(STACK_AUDIO_PREVIEW(widget)->window);

	// Chain up
	GTK_WIDGET_CLASS(stack_audio_preview_parent_class)->unmap(widget);
}

void stack_audio_preview_set_file(StackAudioPreview *preview, char *file)
{
	// Store the filename
	if (preview->file)
	{
		free(preview->file);
	}
	preview->file = strdup(file);

	// Wipe our buffers to force a redraw
	stack_audio_preview_tidy_buffer(preview);
	gdk_threads_add_idle(stack_audio_preview_idle_redraw, preview);
}

void stack_audio_preview_set_view_range(StackAudioPreview *preview, stack_time_t start, stack_time_t end)
{
	preview->start_time = start;
	preview->end_time = end;
	gdk_threads_add_idle(stack_audio_preview_idle_redraw, preview);
}

void stack_audio_preview_set_playback(StackAudioPreview *preview, stack_time_t playback_time)
{
	stack_time_t previous_playback_time = preview->playback_time;
	preview->playback_time = playback_time;

	// We only need to redraw if the marker is show
	if (preview->show_playback_marker)
	{
		// Determine the previous and current position of the marker on the
		// actual widget in pixels
		double fp_width = (float)gtk_widget_get_allocated_width(GTK_WIDGET(preview));
		double last_playback_x = round(fp_width * (double)(previous_playback_time - preview->start_time) / (double)(preview->end_time - preview->start_time));
		double new_playback_x = round(fp_width * (double)(preview->playback_time - preview->start_time) / (double)(preview->end_time - preview->start_time));

		// Only redraw if it wasn't the same
		if (last_playback_x != new_playback_x)
		{
			gdk_threads_add_idle(stack_audio_preview_idle_redraw, preview);
		}
	}
}

void stack_audio_preview_show_playback(StackAudioPreview *preview, bool show_playback)
{
	if (preview->show_playback_marker != show_playback)
	{
		preview->show_playback_marker = show_playback;
		gdk_threads_add_idle(stack_audio_preview_idle_redraw, preview);
	}
}

void stack_audio_preview_set_selection(StackAudioPreview *preview, stack_time_t start, stack_time_t end)
{
	preview->sel_start_time = start;
	preview->sel_end_time = end;
	gtk_widget_queue_draw(GTK_WIDGET(preview));
}

static void stack_audio_preview_finalize(GObject *obj)
{
	StackAudioPreview *preview = STACK_AUDIO_PREVIEW(obj);

	// Stop any render thread and tidy up
	stack_audio_preview_tidy_buffer(preview);

	// Destroy our audio file
	if (preview->file != NULL)
	{
		free(preview->file);
	}

	// Chain up
	G_OBJECT_CLASS(stack_audio_preview_parent_class)->finalize(obj);
}

static void stack_audio_preview_class_init(StackAudioPreviewClass *cls)
{
	// Things we need to override at the class level
	GObjectClass *object_cls = G_OBJECT_CLASS(cls);
	object_cls->finalize = stack_audio_preview_finalize;

	// Things we need to override at the widget level
	GtkWidgetClass *widget_cls = GTK_WIDGET_CLASS(cls);
	widget_cls->draw = stack_audio_preview_draw;
	widget_cls->realize = stack_audio_preview_realize;
	widget_cls->unrealize = stack_audio_preview_unrealize;
	widget_cls->map = stack_audio_preview_map;
	widget_cls->unmap = stack_audio_preview_unmap;
}
