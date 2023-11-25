// Includes:
#include <dlfcn.h>
#include <sys/types.h>
#include <dirent.h>
#include <cstring>
#include "StackApp.h"
#include "StackTrigger.h"
#include "StackLog.h"

// GTK stuff
G_DEFINE_TYPE(StackApp, stack_app, GTK_TYPE_APPLICATION);

// Application initialisation
static void stack_app_init(StackApp *app)
{
	// Initialise plugin bases
	stack_cue_initsystem();
	stack_trigger_initsystem();
	stack_audio_device_initsystem();

	//// LOAD PLUGINS

	// Open the current directory
	DIR* dir = opendir(".");
	if (opendir != NULL)
	{
		struct dirent *entry;

		// Iterate over the items in the directory
		while ((entry = readdir(dir)) != NULL)
		{
			// Get the length of the entry
			size_t name_length = strlen(entry->d_name);

			// If the last three characters of the filename are ".so"
			if (name_length > 3 &&
			    entry->d_name[name_length - 3] == '.' &&
			    entry->d_name[name_length - 2] == 's' &&
			    entry->d_name[name_length - 1] == 'o')
			{
				stack_log("stack_app_init(): Loading %s...\n", entry->d_name);

				// We only have the filename, which if we pass to dlopen it'll look on
				// the library search path rather than where the file actually is, so
				// add on the directory we're searching
				char entry_relative_path[NAME_MAX + 3];
				snprintf(entry_relative_path, NAME_MAX + 3, "./%s", entry->d_name);

				// Attempt to open the entry as a dynamic library
				void *dl_handle = dlopen(entry_relative_path, RTLD_NOW);

				if (dl_handle != NULL)
				{
					// If we succeeded, try to locate the stack_init_plugin symbol
					bool (*sip_ptr)(void);
					*(void **)(&sip_ptr) = dlsym(dl_handle, "stack_init_plugin");

					if (sip_ptr != NULL)
					{
						stack_log("stack_app_init(): Initialising %s...\n", entry->d_name);
						// If we found the symbol, call it to initialise the plugin
						if (!sip_ptr())
						{
							stack_log("stack_app_init(): Plugin didn't initialise properly");
						}
					}
					else
					{
						stack_log("stack_app_init(): %s is not a Stack plugin\n", entry->d_name);
						// This is not a Stack plugin, close the library
						dlclose(dl_handle);
					}
				}
				else
				{
					stack_log("stack_app_init(): Failed to load %s: %s\n", entry->d_name, dlerror());
				}
			}
		}

		// Tidy up
		closedir(dir);
	}
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

