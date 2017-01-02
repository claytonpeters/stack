// Includes:
#include "StackApp.h"
#include "StackAudioCue.h"
#include "StackFadeCue.h"
#include "StackActionCue.h"
#include "StackPulseAudioDevice.h"

// GTK stuff
G_DEFINE_TYPE(StackApp, stack_app, GTK_TYPE_APPLICATION);

// Application initialisation
static void stack_app_init(StackApp *app)
{
	// Initialise
	stack_cue_initsystem();
	stack_audio_cue_register();
	stack_fade_cue_register();
	stack_action_cue_register();
	stack_audio_device_initsystem();
	stack_pulse_audio_device_register();
}

// Files are passed to the application
static void stack_app_open(GApplication *app, GFile **files, gint file_count, const gchar *hint)
{
	// Iterate over all the files
	for (gint i = 0; i < file_count; i++)
	{
		// Create a new window for each file
		StackAppWindow *window;
		window = stack_app_window_new((StackApp*)app);
		stack_app_window_open(window, files[i]);
		gtk_window_present(GTK_WINDOW(window));
	}
}

// Application activation
static void stack_app_activate(GApplication *app)
{
	StackAppWindow *window;
	window = stack_app_window_new((StackApp*)app);
	gtk_window_present(GTK_WINDOW(window));
}

// Class initialisation
static void stack_app_class_init(StackAppClass *cls)
{
	G_APPLICATION_CLASS(cls)->activate = stack_app_activate;
	G_APPLICATION_CLASS(cls)->open = stack_app_open;
}

// Creates a new Stack application
StackApp *stack_app_new(void)
{
	return (StackApp*)g_object_new(stack_app_get_type(), "application-id", "org.gtk.stack", "flags", G_APPLICATION_HANDLES_OPEN, NULL);
}

