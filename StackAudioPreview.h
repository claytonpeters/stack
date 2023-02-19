#ifndef _STACKAUDIOPREVIEW_H_INCLUDED
#define _STACKAUDIOPREVIEW_H_INCLUDED

// Includes:
#include <gtk/gtk.h>

struct StackAudioPreview
{
	GtkWidget super;

	// The URI of the file
	char *file;

	// Is the thread for drawing to the off-screen surface running?
	bool thread_running;

	// Cairo handle for off-screen surface
	cairo_t *buffer_cr;

	// Off-screen cairo surface
	cairo_surface_t *buffer_surface;

	// The size of the current off-screen surface
	int surface_width;
	int surface_height;

	// The start and end times of the audio visible in the preview
	stack_time_t start_time;
	stack_time_t end_time;

	// The start and end times of the selected section
	stack_time_t sel_start_time;
	stack_time_t sel_end_time;

	// Whether to show a playback marker
	bool show_playback_marker;

	// The time of the playback_marker
	stack_time_t playback_time;

	// The thread generating the preview image
	std::thread render_thread;

	// Time of last redraw of audio preview during playback
	stack_time_t last_redraw_time;
};

struct StackAudioPreviewClass
{
	GtkWidgetClass super;
};

// Define our macro for casting
#define STACK_AUDIO_PREVIEW(obj)       G_TYPE_CHECK_INSTANCE_CAST(obj, stack_audio_preview_get_type(), StackAudioPreview)
#define STACK_AUDIO_PREVIEW_CLASS(cls) G_TYPE_CHECK_CLASS_CAST(cls, stack_audio_preview_get_type(), StackAudioPreviewClass)
#define IS_STACK_AUDIO_PREVIEW(obj)    G_TYPE_CHECK_INSTANCE_TYPE(obj, stack_audio_preview_get_type())

// Additional functions:
GType stack_audio_preview_get_type();
GtkWidget *stack_audio_preview_new();
void stack_audio_preview_set_file(StackAudioPreview *preview, char *file);
void stack_audio_preview_set_view_range(StackAudioPreview *preview, stack_time_t start, stack_time_t end);
void stack_audio_preview_set_playback(StackAudioPreview *preview, stack_time_t playback_time);
void stack_audio_preview_show_playback(StackAudioPreview *preview, bool show_playback);
void stack_audio_preview_set_selection(StackAudioPreview *preview, stack_time_t start, stack_time_t end);

#endif
