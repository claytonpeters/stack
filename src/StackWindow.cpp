// Includes:
#include "StackApp.h"
#include "StackAudioDevice.h"
#include "StackLog.h"
#include "StackGtkEntryHelper.h"
#include <cstring>
#include <cstdlib>
#include <cmath>

// GTK stuff
G_DEFINE_TYPE(StackAppWindow, stack_app_window, GTK_TYPE_APPLICATION_WINDOW);
#define STACK_APP_WINDOW(_w) ((StackAppWindow*)(_w))

// Structure used to contain show opening data
struct ShowLoadingData
{
	StackAppWindow* window;
	char* uri;
	GtkDialog* dialog;
	GtkBuilder* builder;
	StackCueList* new_cue_list;
	const char *message;
	double progress;
	bool finished;
};

// Pre-define some function definitions:
static void saw_cue_stop_all_clicked(void* widget, gpointer user_data);
static void saw_remove_inactive_cue_widgets(StackAppWindow *window);

// Callback when loading a show to update our loading dialog
static void saw_open_file_callback(StackCueList *cue_list, double progress, const char *message, void *data)
{
	// Get our show loading data
	ShowLoadingData *sld = (ShowLoadingData*)data;

	// Update our show loading data
	sld->message = message;
	sld->progress = progress;

	stack_log("saw_open_file_callback: %s (%.2f%%)\n", message, progress * 100.0);
}

// Timer to update Show Loading dialog and close it when complete
static gboolean saw_open_file_timer(gpointer data)
{
	// Get our show loading data
	ShowLoadingData *sld = (ShowLoadingData*)data;

	if (sld->finished)
	{
		// If we're finished, close the dialog. Check if the pointer
		// is non-NULL as the dialog may have already been closed
		if (sld->dialog)
		{
			gtk_dialog_response(sld->dialog, 0);
		}
	}
	else
	{
		// Otherwise, update our dialog
		gtk_level_bar_set_value(GTK_LEVEL_BAR(gtk_builder_get_object(sld->builder, "sldProgress")), sld->progress);
		if (sld->message != NULL)
		{
			gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(sld->builder, "sldLoadingActionLabel")), sld->message);
		}
	}

	// Return FALSE when we're finished to kill the timer
	return (gboolean)!sld->finished;
}

// Thread to open a file off of the UI thread
static void saw_open_file_thread(ShowLoadingData *sld)
{
	// Wait for dialog a little - it's better UX
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	// Open the file
	sld->new_cue_list = stack_cue_list_new_from_file(sld->uri, saw_open_file_callback, (void*)sld);

	// Note that we've finished and exit the thread
	sld->finished = true;
	return;
}

// Helper function that updates the list store with updated information about
// the currently selected cue. A wrapper around saw_update_list_store_from_cue.
// Also callable via the signal "update-selected-cue", hence the gpointer rather
// than an explicit StackAppWindow
static void saw_update_selected_cue(gpointer user_data)
{
	StackAppWindow *window = STACK_APP_WINDOW(user_data);
	StackCue *cue = window->selected_cue;
	if (cue)
	{
		stack_cue_list_widget_update_cue(window->sclw, cue->uid, 0);
	}
}

struct StackCueListUpdateData {
	StackAppWindow *window;
	StackCue *cue;
};

static gboolean saw_update_cue_main_thread(gpointer user_data)
{
	// Update the list data
	StackCueListUpdateData* data = (StackCueListUpdateData*)user_data;
	stack_cue_list_widget_update_cue(data->window->sclw, data->cue->uid, 0);

	// Tidy up
	delete data;
	return G_SOURCE_REMOVE;
}

// Helper function that updates the list store with updated information about
// a specific cue. A wrapper around saw_update_list_store_from_cue.
// Also callable via the signal "update-cue", hence the gpointer rather
// than an explicit StackAppWindow
static void saw_update_cue(gpointer user_data, StackCue* cue)
{
	StackAppWindow *window = STACK_APP_WINDOW(user_data);
	if (cue)
	{
		StackCueListUpdateData* data = new StackCueListUpdateData{window, cue};

		// Do this on the UI thread so as not to cause thread-safety issues
		gdk_threads_add_idle(saw_update_cue_main_thread, data);
	}
}

// Clears the entire list store. Also deselects any active cue
static void saw_clear_list_store(StackAppWindow *window)
{
	// Remove tabs from any currently selected cue
	if (window->selected_cue != NULL)
	{
		stack_cue_unset_tabs(window->selected_cue, window->notebook);
		window->selected_cue = NULL;
	}
}

static void saw_cue_state_changed(StackCueList *cue_list, StackCue *cue, void *user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Update the UI
	stack_cue_list_widget_update_cue(window->sclw, cue->uid, 0);
}

// Updates a pre/post wait time on the properties panel
static void saw_ucp_wait(StackAppWindow *window, StackCue *cue, bool pre)
{
	char waitTime[64];
	stack_time_t ctime = 0;
	stack_property_get_int64(stack_cue_get_property(cue, pre ? "pre_time" : "post_time"), STACK_PROPERTY_VERSION_DEFINED, &ctime);

	// Update cue post-wait time (rounding nanoseconds to seconds with three decimal places)
	stack_format_time_as_string(ctime, waitTime, 64);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(window->builder, pre ? "sawPreWait" : "sawPostWait")), waitTime);
}

// Updates the cue properties window with information from the given 'cue'.
// Can also be called by the signal 'update-cue-properties'
static void saw_update_cue_properties(gpointer user_data, StackCue *cue)
{
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Pause change callbacks on the properties
	stack_property_pause_change_callback(stack_cue_get_property(cue, "r"), true);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "g"), true);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "b"), true);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "name"), true);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "notes"), true);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "pre_time"), true);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "post_time"), true);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "post_trigger"), true);

	// Update cue number
	char cue_number[32];
	stack_cue_id_to_string(cue->id, cue_number, 32);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(window->builder, "sawCueNumber")), cue_number);

	// Update cue color
	uint8_t r = 0, g = 0, b = 0;
	stack_property_get_uint8(stack_cue_get_property(cue, "r"), STACK_PROPERTY_VERSION_DEFINED, &r);
	stack_property_get_uint8(stack_cue_get_property(cue, "g"), STACK_PROPERTY_VERSION_DEFINED, &g);
	stack_property_get_uint8(stack_cue_get_property(cue, "b"), STACK_PROPERTY_VERSION_DEFINED, &b);
	GdkRGBA cueColor = {(double)r / 255.0, (double)g / 255.0, (double)b / 255.0, 1.0};
	gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(gtk_builder_get_object(window->builder, "sawCueColor")), &cueColor);

	// Update cue name
	char *cue_name = NULL;
	stack_property_get_string(stack_cue_get_property(cue, "name"), STACK_PROPERTY_VERSION_DEFINED, &cue_name);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(window->builder, "sawCueName")), cue_name);

	// Update cue notes
	char *cue_notes = NULL;
	stack_property_get_string(stack_cue_get_property(cue, "notes"), STACK_PROPERTY_VERSION_DEFINED, &cue_notes);
	GtkTextView *textview = GTK_TEXT_VIEW(gtk_builder_get_object(window->builder, "sawCueNotes"));
	GtkTextBuffer* buffer = gtk_text_view_get_buffer(textview);
	gtk_text_buffer_set_text(buffer, cue_notes, -1);

	// Update cue pre-wait and post-wait times
	saw_ucp_wait(window, cue, true);
	saw_ucp_wait(window, cue, false);

	// Update post-wait trigger option (and enable/disable post-wait time as necessary)
	int32_t cue_post_trigger = STACK_CUE_WAIT_TRIGGER_NONE;
	stack_property_get_int32(stack_cue_get_property(cue, "post_trigger"), STACK_PROPERTY_VERSION_DEFINED, &cue_post_trigger);
	switch (cue_post_trigger)
	{
		case STACK_CUE_WAIT_TRIGGER_NONE:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(window->builder, "sawPostWaitTrigger1")), true);
			gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(window->builder, "sawPostWait")), false);
			break;

		case STACK_CUE_WAIT_TRIGGER_IMMEDIATE:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(window->builder, "sawPostWaitTrigger2")), true);
			gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(window->builder, "sawPostWait")), true);
			break;

		case STACK_CUE_WAIT_TRIGGER_AFTERPRE:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(window->builder, "sawPostWaitTrigger3")), true);
			gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(window->builder, "sawPostWait")), true);
			break;

		case STACK_CUE_WAIT_TRIGGER_AFTERACTION:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(window->builder, "sawPostWaitTrigger4")), true);
			gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(window->builder, "sawPostWait")), true);
			break;
	}

	// Resume change callbacks on the properties
	stack_property_pause_change_callback(stack_cue_get_property(cue, "r"), false);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "g"), false);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "b"), false);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "name"), false);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "notes"), false);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "pre_time"), false);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "post_time"), false);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "post_trigger"), false);

}

// Selects the first cue on the list
static void saw_select_first_cue(StackAppWindow *window)
{
	void *iter = stack_cue_list_iter_front(window->cue_list);
	StackCue *cue = stack_cue_list_iter_get(iter);
	stack_cue_list_widget_select_single_cue(window->sclw, cue->uid);
	stack_cue_list_iter_free(iter);
}

// Selects the last cue on the list
static void saw_select_last_cue(StackAppWindow *window)
{
	void *iter = stack_cue_list_iter_front(window->cue_list);
	StackCue *cue = NULL;

	while (!stack_cue_list_iter_at_end(window->cue_list, iter))
	{
		cue = stack_cue_list_iter_get(iter);
		stack_cue_list_iter_next(iter);
	}

	if (cue != NULL)
	{
		stack_cue_list_widget_select_single_cue(window->sclw, cue->uid);
	}

	stack_cue_list_iter_free(iter);
}

// Selects the next cue in the list
static void saw_select_next_cue(StackAppWindow *window, bool skip_automatic = false)
{
	cue_uid_t old_uid = window->sclw->primary_selection;

	// If there is a selection
	if (old_uid != STACK_CUE_UID_NONE)
	{
		// Get the current cue (before moving)
		StackCue *old_cue = stack_cue_get_by_uid(old_uid);

		// Get an iterator into the cue list at that point
		void *iter = stack_cue_list_iter_at(window->cue_list, old_uid, NULL);

		// Move the iterator forward one
		stack_cue_list_iter_next(iter);

		if (!stack_cue_list_iter_at_end(window->cue_list, iter))
		{
			// If we're skipping past cues that are triggered by auto-
			// continue/follow on the cue before them
			if (skip_automatic)
			{
				// Start searching
				bool searching = true;
				bool reached_end = false;
				while (searching)
				{
					// Examine the cue
					int32_t cue_post_trigger = STACK_CUE_WAIT_TRIGGER_NONE;
					stack_property_get_int32(stack_cue_get_property(old_cue, "post_trigger"), STACK_PROPERTY_VERSION_DEFINED, &cue_post_trigger);

					// If this cue we would trigger the next cue
					if (cue_post_trigger != STACK_CUE_WAIT_TRIGGER_NONE)
					{
						// We need to keep searching forward. Get the "next" cue, so
						// the loop can check to see if we need to skip again on the
						// next ieration
						old_cue = stack_cue_list_iter_get(iter);
						stack_cue_list_iter_next(iter);

						if (stack_cue_list_iter_at_end(window->cue_list, iter))
						{
							// If we can't move any further forward, stop searching
							searching = false;
							reached_end = true;
						}
					}
					else
					{
						// The previous doesn't auto trigger, stop searching
						searching = false;
					}
				}

				// If we reached the end of the cue list
				if (reached_end)
				{
					// Just select the last cue
					saw_select_last_cue(window);
				}
				else
				{
					stack_cue_list_widget_select_single_cue(window->sclw, stack_cue_list_iter_get(iter)->uid);
				}
			}
			else
			{
				stack_cue_list_widget_select_single_cue(window->sclw, stack_cue_list_iter_get(iter)->uid);
			}
		}

		// Tidy up
		stack_cue_list_iter_free(iter);
	}
}

size_t saw_get_audio_from_cuelist(size_t samples, float *buffer, void *user_data)
{
	StackAppWindow *window = STACK_APP_WINDOW(user_data);
	StackCueList *cue_list = window->cue_list;

	// TODO: Once we can properly map audio devices to cue list channels, we need to change this
	size_t channels[] = {0, 1};
	stack_cue_list_get_audio(cue_list, buffer, samples, 2, channels);

	return samples;
}

// Sets up an initial playback device
static void saw_setup_default_device(StackAppWindow *window)
{
	// DEBUG: Open a PulseAudio device
	const StackAudioDeviceClass *sadc = stack_audio_device_get_class("StackPulseAudioDevice");
	if (sadc)
	{
		StackAudioDeviceDesc *devices = NULL;
		size_t num_outputs = sadc->get_outputs_func(&devices);

		if (devices != NULL && num_outputs > 0)
		{
			// Create a PulseAudio device using the default device
			StackAudioDevice *device = stack_audio_device_new("StackPulseAudioDevice", NULL, 2, 44100, saw_get_audio_from_cuelist, window);

			// Store the audio device in the cue list
			window->cue_list->audio_device = device;
		}

		// Free the list of devices
		sadc->free_outputs_func(&devices, num_outputs);
	}
}

// Menu callback
static void saw_file_save_as_clicked(void* widget, gpointer user_data)
{
	// Run a Save dialog
	GtkWidget *dialog = gtk_file_chooser_dialog_new("Save Show As", GTK_WINDOW(user_data), GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
	gint response = gtk_dialog_run(GTK_DIALOG(dialog));

	// If the user chose to Save...
	if (response == GTK_RESPONSE_ACCEPT)
	{
		// Get the chosen URI
		gchar *uri = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(dialog));

		// Save the cue list to the file
		stack_cue_list_lock(STACK_APP_WINDOW(user_data)->cue_list);
		stack_cue_list_save(STACK_APP_WINDOW(user_data)->cue_list, uri);
		stack_cue_list_unlock(STACK_APP_WINDOW(user_data)->cue_list);

		// Update the title bar
		char title_buffer[512];
		snprintf(title_buffer, 512, "%s - Stack", uri);
		gtk_window_set_title(GTK_WINDOW(user_data), title_buffer);

		// Tidy up
		g_free(uri);
	}

	gtk_widget_destroy(dialog);
}

// Menu/toolbar callback
static void saw_file_save_clicked(void* widget, gpointer user_data)
{
	// If the current cue list has no URI
	if (STACK_APP_WINDOW(user_data)->cue_list->uri == NULL)
	{
		// ...call Save As instead
		saw_file_save_as_clicked(widget, user_data);
	}
	else
	{
		// ...otherwise overwrite
		stack_cue_list_lock(STACK_APP_WINDOW(user_data)->cue_list);
		stack_cue_list_save(STACK_APP_WINDOW(user_data)->cue_list, STACK_APP_WINDOW(user_data)->cue_list->uri);
		stack_cue_list_unlock(STACK_APP_WINDOW(user_data)->cue_list);
	}
}

// Menu/toolbar callback
static void saw_file_open_clicked(void* widget, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// See if the current cue list has changed
	if (window->cue_list->changed)
	{
		// Make the user decide if they want to save changes
		GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "The current show has changed. Do you wish to save your changes?");
		gtk_dialog_add_button(GTK_DIALOG(dialog), "_Save Changes", GTK_RESPONSE_YES);
		gtk_dialog_add_button(GTK_DIALOG(dialog), "_Discard Changes", GTK_RESPONSE_NO);
		gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
		gint result = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);

		// If they press Cancel, then stop doing anything
		if (result == GTK_RESPONSE_CANCEL)
		{
			return;
		}
		else if (result == GTK_RESPONSE_YES)
		{
			saw_file_save_clicked(widget, user_data);

			// If the file wasn't saved as a result of the above...
			if (window->cue_list->changed)
			{
				// Don't open a file
				return;
			}
		}
	}

	// Create an Open dialog
	GtkWidget *dialog = gtk_file_chooser_dialog_new("Open Show", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);

	// Add filters to it (the file chooser takes ownership, so we don't have to tidy them up)
	GtkFileFilter *stack_filter = gtk_file_filter_new();
	gtk_file_filter_add_pattern(stack_filter, "*.stack");
	gtk_file_filter_set_name(stack_filter, "Stack Show Files (*.stack)");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), stack_filter);
	GtkFileFilter *all_filter = gtk_file_filter_new();
	gtk_file_filter_add_pattern(all_filter, "*");
	gtk_file_filter_set_name(all_filter, "All Files (*)");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all_filter);

	// Run the dialog
	gint response = gtk_dialog_run(GTK_DIALOG(dialog));

	// If the user chose to Open...
	if (response == GTK_RESPONSE_ACCEPT)
	{
		// Get the chosen URI
		//gchar *uri = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(dialog));

		// Get the file and open it
		GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
		gtk_widget_destroy(dialog);
		stack_app_window_open(window, file);

		// Tidy up
		//g_free(uri);
		g_object_unref(file);
	}
	else
	{
		gtk_widget_destroy(dialog);
	}
}

// Menu/toolbar callback
static void saw_file_new_clicked(void* widget, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// See if the current cue list has changed
	if (window->cue_list->changed)
	{
		// Make the user decide if they want to save changes
		GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "The current show has changed. Do you wish to save your changes?");
		gtk_dialog_add_button(GTK_DIALOG(dialog), "_Save Changes", GTK_RESPONSE_YES);
		gtk_dialog_add_button(GTK_DIALOG(dialog), "_Discard Changes", GTK_RESPONSE_NO);
		gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
		gint result = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);

		// If they press Cancel, then stop doing anything
		if (result == GTK_RESPONSE_CANCEL)
		{
			return;
		}
		else if (result == GTK_RESPONSE_YES)
		{
			saw_file_save_clicked(widget, user_data);

			// If the file wasn't saved as a result of the above...
			if (window->cue_list->changed)
			{
				// Don't start a new file
				return;
			}
		}
	}

	// Stop all the cues
	saw_cue_stop_all_clicked(widget, (gpointer)window);

	// Tidy up the widgets from the now-stopped cuews
	saw_remove_inactive_cue_widgets(window);

	// Deselect any selected tab
	if (window->selected_cue != NULL)
	{
		stack_cue_unset_tabs(window->selected_cue, window->notebook);
		window->selected_cue = NULL;
	}

	// We don't need to worry about the UI timer, as that's running on the same
	// thread as the event loop that is handling this event handler

	// Destroy the old cue list
	stack_cue_list_destroy(window->cue_list);

	// Initialise a new cue list, defaulting to two channels
	window->cue_list = stack_cue_list_new(2);
	window->cue_list->state_change_func = saw_cue_state_changed;
	window->cue_list->state_change_func_data = (void*)window;
	stack_cue_list_widget_set_cue_list(window->sclw, window->cue_list);

	// Refresh the cue list
	gtk_window_set_title(GTK_WINDOW(window), "Stack");

	// Setup the default device
	saw_setup_default_device(window);
}

// Menu callback
static void saw_file_quit_clicked(void* item, gpointer user_data)
{
	gtk_window_close((GtkWindow*)user_data);
}

// Menu callback
static void saw_edit_cut_clicked(void* widget, gpointer user_data)
{
	stack_log("Edit -> Cut clicked\n");
}

// Menu callback
static void saw_edit_copy_clicked(void* widget, gpointer user_data)
{
	stack_log("Edit -> Copy clicked\n");
}

// Menu callback
static void saw_edit_paste_clicked(void* widget, gpointer user_data)
{
	stack_log("Edit -> Paste clicked\n");
}

// Menu callback
static void saw_edit_delete_clicked(void* widget, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// First determine the cue we should select after all selected cues have
	// been deleted. This is the cue after the bottom-most selected cue
	cue_uid_t new_selection = STACK_CUE_UID_NONE;
	bool previous_was_selected = false;
	cue_uid_t previous_cue = STACK_CUE_UID_NONE;

	// Iterate over the cue list
	void *iter = stack_cue_list_iter_front(window->cue_list);
	while (!stack_cue_list_iter_at_end(window->cue_list, iter))
	{
		StackCue *cue = stack_cue_list_iter_get(iter);
		bool is_selected = stack_cue_list_widget_is_cue_selected(window->sclw, cue->uid);

		// If the current cue is not selected, and the previous cue was, mark
		// this cue as the new selection
		if (!is_selected && previous_was_selected)
		{
			new_selection = cue->uid;
			previous_was_selected = false;
		}

		// Keep track of the previous cue and its selection state
		if (is_selected)
		{
			previous_was_selected = true;
		}
		previous_cue = cue->uid;

		// Iterate
		stack_cue_list_iter_next(iter);
	}

	// It is possible here (for example if we've selected the last items in the
	// list) that the new selection is still nothing, but we account for that
	// later
	//
	// Tidy up
	stack_cue_list_iter_free(iter);

	// Iterate over all the cues and delete the ones that are selected
	iter = stack_cue_list_iter_front(window->cue_list);
	while (!stack_cue_list_iter_at_end(window->cue_list, iter))
	{
		StackCue *cue = stack_cue_list_iter_get(iter);

		// If the cue is selected
		if (stack_cue_list_widget_is_cue_selected(window->sclw, cue->uid))
		{
			// If this is the currently primary selection, deselect it properly
			if (cue == window->selected_cue)
			{
				stack_cue_unset_tabs(window->selected_cue, window->notebook);
				window->selected_cue = NULL;
			}

			// Iterate to the next cue now or the iterator will become invalid
			stack_cue_list_iter_next(iter);

			// Remove the cue from the cue list
			stack_cue_list_lock(window->cue_list);
			stack_cue_list_remove(window->cue_list, cue);
			stack_cue_list_unlock(window->cue_list);

			// Destroy the cue
			stack_cue_destroy(cue);
		}
		else
		{
			// Iterate
			stack_cue_list_iter_next(iter);
		}
	}

	// Tidy up
	stack_cue_list_iter_free(iter);

	// Redraw the whole list widget
	stack_cue_list_widget_list_modified(window->sclw);

	// If we have a new selection, select it, otherwise select the last cue in
	// the cue list
	if (new_selection != STACK_CUE_UID_NONE)
	{
		stack_cue_list_widget_select_single_cue(window->sclw, new_selection);
	}
	else
	{
		saw_select_last_cue(window);
	}
}

// Menu callback
static gboolean saw_edit_select_all_clicked(void* widget, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Only do this if the cue list has focus
	if (gtk_window_get_focus(GTK_WINDOW(window)) == GTK_WIDGET(window->sclw))
	{
		auto iter = stack_cue_list_iter_front(window->cue_list);
		for (; !stack_cue_list_iter_at_end(window->cue_list, iter); stack_cue_list_iter_next(iter))
		{
			stack_cue_list_widget_add_to_selection(window->sclw, stack_cue_list_iter_get(iter)->uid);
		}
		stack_cue_list_iter_free(iter);

		// Don't let GTK pass on the event
		return true;
	}

	// GTK should pass to another widget
	return false;
}

// Edit -> Show Settings
static void saw_edit_show_settings_clicked(void* widget, gpointer user_data)
{
	sss_show_dialog(STACK_APP_WINDOW(user_data));
}

// Menu callback
static void saw_cue_add_group_clicked(void* widget, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Lock the cue list
	stack_cue_list_lock(window->cue_list);

	// Create the new cue
	StackCue* new_cue = STACK_CUE(stack_cue_new("StackGroupCue", window->cue_list));
	if (new_cue == NULL)
	{
		stack_cue_list_unlock(window->cue_list);
		return;
	}

	// Add the list to our cue stack
	stack_cue_list_append(window->cue_list, STACK_CUE(new_cue));

	// Unlock the cue list
	stack_cue_list_unlock(window->cue_list);

	// Update that last row in the list store with the basics of the cue
	stack_cue_list_widget_update_cue(window->sclw, new_cue->uid, 0);

	// Select the new cue
	saw_select_last_cue(window);

}

static void saw_generic_add_cue(StackAppWindow *window, const char *type)
{
	// Lock the cue list
	stack_cue_list_lock(window->cue_list);

	// Create the new cue
	StackCue* new_cue = STACK_CUE(stack_cue_new(type, window->cue_list));
	if (new_cue == NULL)
	{
		stack_cue_list_unlock(window->cue_list);
		return;
	}

	// Add the list to our cue stack
	stack_cue_list_append(window->cue_list, STACK_CUE(new_cue));

	// Unlock the cue list
	stack_cue_list_unlock(window->cue_list);

	// Update that last row in the list store with the basics of the cue
	stack_cue_list_widget_update_cue(window->sclw, new_cue->uid, 0);

	// Select the new cue
	saw_select_last_cue(window);
}

// Menu callback
static void saw_cue_add_audio_clicked(void* widget, gpointer user_data)
{
	saw_generic_add_cue(STACK_APP_WINDOW(user_data), "StackAudioCue");
}

// Menu callback
static void saw_cue_add_fade_clicked(void* widget, gpointer user_data)
{
	saw_generic_add_cue(STACK_APP_WINDOW(user_data), "StackFadeCue");
}

// Menu callback
static void saw_cue_add_action_clicked(void* widget, gpointer user_data)
{
	saw_generic_add_cue(STACK_APP_WINDOW(user_data), "StackActionCue");
}

// Menu callback
static void saw_cue_renumber_clicked(void* widget, gpointer user_data)
{
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Show the dialog, and if the user clicked OK, update the UI
	if (src_show_dialog(window))
	{
		// Lock the cue list
		stack_cue_list_lock(window->cue_list);

		// Get an iterator over the cue list
		void *citer = stack_cue_list_iter_front(window->cue_list);

		// Iterate over the cue list
		while (!stack_cue_list_iter_at_end(window->cue_list, citer))
		{
			// Get the cue
			StackCue *cue = stack_cue_list_iter_get(citer);

			// Update the row
			stack_cue_list_widget_update_cue(window->sclw, cue->uid, 0);

			// Iterate
			citer = stack_cue_list_iter_next(citer);
		}

		// Unlock the cue list
		stack_cue_list_unlock(window->cue_list);

		// Free the iterator
		stack_cue_list_iter_free(citer);

		// The selected cue is in bounds of the selection, so update the cue
		// properties panel too
		if (window->selected_cue != NULL)
		{
			saw_update_cue_properties(window, window->selected_cue);
		}
	}
}

// Menu callback
static void saw_view_active_cue_list_clicked(void* widget, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	GtkPaned *hPanel = GTK_PANED(gtk_builder_get_object(window->builder, "sawHPanel"));
	gint position = gtk_paned_get_position(hPanel);
	gint window_width, window_height;
	gtk_window_get_size(GTK_WINDOW(window), &window_width, &window_height);

	// If the panel is at the window width (given some tolerance)
	if (position >= window_width - 5)
	{
		// Set it to be at 75% of the width of the window (so 25% wide)
		gtk_paned_set_position(hPanel, window_width * 3 / 4);

		// Show the panel
		gtk_widget_set_visible(GTK_WIDGET(gtk_builder_get_object(window->builder, "sawActiveCuesBox")), true);
	}
	else
	{
		// Hide it by setting it to the width of the window
		gtk_paned_set_position(hPanel, window_width);

		// Make the panel invisible so Gtk doesn't spend time rendering it
		gtk_widget_set_visible(GTK_WIDGET(gtk_builder_get_object(window->builder, "sawActiveCuesBox")), false);
	}
}

// Menu callback
static void saw_help_about_clicked(void* widget, gpointer user_data)
{
	// Build an about dialog
	GtkAboutDialog *about = GTK_ABOUT_DIALOG(gtk_about_dialog_new());
	gtk_about_dialog_set_program_name(about, "Stack");
	gtk_about_dialog_set_version(about, "Version 0.1.20231118-1");
	gtk_about_dialog_set_copyright(about, "Copyright (c) 2023 Clayton Peters");
	gtk_about_dialog_set_comments(about, "A GTK+ based sound cueing application for theatre");
	gtk_about_dialog_set_website(about, "https://github.com/claytonpeters/stack");
	gtk_about_dialog_set_logo(about, STACK_APP_WINDOW(user_data)->icon);
	gtk_window_set_transient_for(GTK_WINDOW(about), GTK_WINDOW(user_data));

	// Show the dialog
	gtk_dialog_run(GTK_DIALOG(about));

	// Destroy the dialog on response
	gtk_widget_destroy(GTK_WIDGET(about));
}

// Menu/toolbar callback
static void saw_cue_play_clicked(void* widget, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	if (window->selected_cue != NULL)
	{
		stack_cue_list_lock(window->cue_list);
		stack_cue_play(window->selected_cue);
		stack_cue_list_unlock(window->cue_list);
		stack_cue_list_widget_update_cue(window->sclw, window->selected_cue->uid, 0);
		// Select the next cue that isn't automatically triggered by a follow
		saw_select_next_cue(window, true);
	}
}

// Menu/toolbar callback
static void saw_cue_stop_clicked(void* widget, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	if (window->selected_cue != NULL)
	{
		stack_cue_list_lock(window->cue_list);
		stack_cue_stop(window->selected_cue);
		stack_cue_list_unlock(window->cue_list);
		stack_cue_list_widget_update_cue(window->sclw, window->selected_cue->uid, 0);
	}
}

// Menu/toolbar callback
static void saw_cue_stop_all_clicked(void* widget, gpointer user_data)
{
	stack_cue_list_stop_all(STACK_APP_WINDOW(user_data)->cue_list);
}

static void saw_remove_inactive_cue_widgets(StackAppWindow *window)
{
	// Get the UI item to remov the cue from
	GtkBox *active_cues = GTK_BOX(gtk_builder_get_object(window->builder, "sawActiveCuesBox"));

	auto iter = stack_cue_list_iter_front(window->cue_list);
	while (!stack_cue_list_iter_at_end(window->cue_list, iter))
	{
		StackCue *cue = stack_cue_list_iter_get(iter);

		// We're looking for cues that are stopped but have widgets
		if (cue->state == STACK_CUE_STATE_STOPPED)
		{
			auto find_widget = window->active_cue_widgets.find(cue->uid);
			if (find_widget != window->active_cue_widgets.end())
			{
				StackActiveCueWidget *widget = find_widget->second;

				// Note that this should also destroy the children so we don't
				// need to delete them (as their refcount should hit zero)
				gtk_container_remove(GTK_CONTAINER(active_cues), GTK_WIDGET(widget->vbox));

				// Tidy up the structure
				delete widget;

				// Remove from the map
				window->active_cue_widgets.erase(find_widget);
			}
		}

		// Iterate to next cue
		stack_cue_list_iter_next(iter);
	}

	// Tidy up
	stack_cue_list_iter_free(iter);
}

static void saw_add_or_update_active_cue_widget(StackAppWindow *window, StackCue *cue)
{
	StackActiveCueWidget *cue_widget;

	// See if we already have the cue in the UI
	auto find_widget = window->active_cue_widgets.find(cue->uid);
	if (find_widget == window->active_cue_widgets.end())
	{
		// Create a container for our details
		cue_widget = new StackActiveCueWidget;
		cue_widget->cue_uid = cue->uid;
		cue_widget->cue_list = window->cue_list;

		// Create the container for the cue details
		cue_widget->vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 4));
		gtk_widget_set_visible(GTK_WIDGET(cue_widget->vbox), true);

		// Widget for cue name
		cue_widget->name = GTK_LABEL(gtk_label_new(""));
		gtk_widget_set_visible(GTK_WIDGET(cue_widget->name), true);
		gtk_label_set_xalign(cue_widget->name, 0.0);
		gtk_label_set_ellipsize(cue_widget->name, PANGO_ELLIPSIZE_END);
		PangoAttrList *attrs = pango_attr_list_new();
		PangoAttribute *attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
		pango_attr_list_insert(attrs, attr);
		gtk_label_set_attributes(cue_widget->name, attrs);

		// Widget for cue name
		cue_widget->time = GTK_LABEL(gtk_label_new(""));
		gtk_widget_set_visible(GTK_WIDGET(cue_widget->time), true);
		gtk_label_set_xalign(cue_widget->time, 0.0);

		// Widget for levels
		cue_widget->meter = STACK_LEVEL_METER(stack_level_meter_new(2, -90.0, 0.0));
		gtk_widget_set_visible(GTK_WIDGET(cue_widget->meter), true);
		GValue height_request = G_VALUE_INIT;
		g_value_init(&height_request, G_TYPE_INT);
		g_value_set_int(&height_request, 20);
		g_object_set_property(G_OBJECT(cue_widget->meter), "height-request", &height_request);

		// Get the UI item to add the cue to
		GtkBox *active_cues = GTK_BOX(gtk_builder_get_object(window->builder, "sawActiveCuesBox"));

		// Pack everything
		gtk_box_pack_start(cue_widget->vbox, GTK_WIDGET(cue_widget->name), false, false, 0);
		gtk_box_pack_start(cue_widget->vbox, GTK_WIDGET(cue_widget->time), false, false, 0);
		gtk_box_pack_start(cue_widget->vbox, GTK_WIDGET(cue_widget->meter), false, false, 2);
		gtk_box_pack_start(active_cues, GTK_WIDGET(cue_widget->vbox), false, false, 4);

		// Store in our map
		window->active_cue_widgets[cue->uid] = cue_widget;
	}
	else
	{
		cue_widget = find_widget->second;
	}

	// Get live running time
	stack_time_t rpre, raction, rpost;
	stack_time_t current_time = stack_get_clock_time();
	stack_cue_get_running_times(cue, current_time, &rpre, &raction, &rpost, NULL, NULL, NULL);

	// Update the name of the cue. We compare the strings first to avoid a more
	// expensive redraw in Gtk if it hasn't changed
	if (strcmp(gtk_label_get_text(cue_widget->name), stack_cue_get_rendered_name(cue)) != 0)
	{
		gtk_label_set_text(cue_widget->name, stack_cue_get_rendered_name(cue));
	}

	size_t channel_count = stack_cue_get_active_channels(cue, NULL);
	StackChannelRMSData *rms = stack_cue_list_get_rms_data(window->cue_list, cue->uid);
	if (rms != NULL)
	{
		for (size_t i = 0; i < channel_count; i++)
		{
			stack_level_meter_set_level_and_peak(cue_widget->meter, i, rms[i].current_level, rms[i].peak_level);

			const stack_time_t peak_hold_time = 2 * NANOSECS_PER_SEC;
			// TODO: This should probably be in StackCueList instead
			if (current_time - rms[i].peak_time > peak_hold_time)
			{
				rms[i].peak_level -= 1.0;
			}
		}
	}

	if (channel_count == 0 || rms == NULL)
	{
		// Reset the levels if we have no RMS data
		stack_level_meter_reset(cue_widget->meter);
	}

	// Format the times
	stack_time_t first_time = 0, second_time = 0;
	char time_text[64] = {0};
	if (cue->state == STACK_CUE_STATE_PLAYING_PRE)
	{
		strncat(time_text, "Pre: ", 63);
		first_time = rpre;
		stack_property_get_int64(stack_cue_get_property(cue, "pre_time"), STACK_PROPERTY_VERSION_LIVE, &second_time);
	}
	else if (cue->state == STACK_CUE_STATE_PLAYING_ACTION)
	{
		first_time = raction;
		stack_property_get_int64(stack_cue_get_property(cue, "action_time"), STACK_PROPERTY_VERSION_LIVE, &second_time);
	}
	else if (cue->state == STACK_CUE_STATE_PLAYING_POST)
	{
		strncat(time_text, "Post: ", 63);
		first_time = rpost;
		stack_property_get_int64(stack_cue_get_property(cue, "post_time"), STACK_PROPERTY_VERSION_LIVE, &second_time);
	}
	stack_format_time_as_string(first_time, &time_text[strlen(time_text)], 64 - strlen(time_text));
	strncat(time_text, " / ", 63);
	stack_format_time_as_string(second_time, &time_text[strlen(time_text)], 64 - strlen(time_text));

	// Update the contents
	gtk_label_set_text(cue_widget->time, time_text);
}

// Callback for UI timer
static gboolean saw_ui_timer(gpointer user_data)
{
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Let the app know that we're running
	if (window->timer_state == 0)
	{
		window->timer_state = 1;
	}

	// If the timer is supposed to be stopping...
	if (window->timer_state == 2)
	{
		// Set the timer state to stopped
		window->timer_state = 3;

		// Prevent timer re-occurence
		return false;
	}

	// Lock the cue list
	stack_cue_list_lock(window->cue_list);

	// Get an iterator over the cue list
	void *citer = stack_cue_list_iter_front(window->cue_list);

	// Iterate over the cue list
	while (!stack_cue_list_iter_at_end(window->cue_list, citer))
	{
		// Get the cue
		StackCue *cue = stack_cue_list_iter_get(citer);

		if (cue->state >= STACK_CUE_STATE_PLAYING_PRE && cue->state <= STACK_CUE_STATE_PLAYING_POST)
		{
			// Update the row (times only)
			stack_cue_list_widget_update_cue(window->sclw, cue->uid, 4);

			// Update active cue panel
			saw_add_or_update_active_cue_widget(window, cue);
		}

		// Iterate
		citer = stack_cue_list_iter_next(citer);
	}

	// Remove any inactive cues
	saw_remove_inactive_cue_widgets(window);

	stack_time_t current_time = stack_get_clock_time();

	// Update the master RMS data
	for (size_t i = 0; i < window->cue_list->channels; i++)
	{
		stack_level_meter_set_level_and_peak(window->master_out_meter, i, window->cue_list->master_rms_data[i].current_level, window->cue_list->master_rms_data[i].peak_level);

		const stack_time_t peak_hold_time = 2 * NANOSECS_PER_SEC;
		// TODO: This should probably be in StackCueList instead
		if (current_time - window->cue_list->master_rms_data[i].peak_time > peak_hold_time)
		{
			window->cue_list->master_rms_data[i].peak_level -= 1.0;
		}
	}

	// Unlock the cue list
	stack_cue_list_unlock(window->cue_list);

	// Free the iterator
	stack_cue_list_iter_free(citer);

	return true;
}

// Callback for when the selected cue changes
static void saw_cue_selected(GtkTreeSelection *selection, cue_uid_t uid, gpointer user_data)
{
	// Extract the window from the parameters
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	StackCue *cue = stack_cue_get_by_uid(uid);

	// Gtk fires this even if we reselect the same cue, so only change
	// the cue if it actually changed
	if (cue != window->selected_cue)
	{
		// Keep track of what notebook page we were on
		gint page = gtk_notebook_get_current_page(window->notebook);

		// Remove tabs from previously selected cue
		if (window->selected_cue != NULL)
		{
			stack_cue_unset_tabs(window->selected_cue, window->notebook);
		}

		// Update the window
		window->selected_cue = cue;
		saw_update_cue_properties(window, cue);

		// Put on the correct tabs
		stack_cue_set_tabs(cue, window->notebook);

		// Try and put us back on the same notebook page
		gtk_notebook_set_current_page(window->notebook, page);
	}
}

// Callback for when the window is destroyed
static void saw_destroy(GtkWidget* widget, gpointer user_data)
{
	stack_log("saw_destroy() called\n");

	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Clear the list store
	saw_clear_list_store(window);

	// Instruct the timer to stop
	stack_log("saw_destroy(): Waiting for UI timer to stop\n");
	window->timer_state = 2;

	// Wait for the timer to stop
	while (window->timer_state != 3)
	{
		// Run an event loop to let the timer fire
		while (gtk_events_pending())
		{
			gtk_main_iteration();
		}
	}

	stack_log("saw_destroy(): Timer stopped\n");

	// Destroy the cue list
	stack_cue_list_destroy(window->cue_list);
}

// Cue property change (done on focus-out)
static gboolean saw_cue_number_changed(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Only attempt to change if we have a selected cue
	if (window->selected_cue == NULL)
	{
		return false;
	}

	// Set the name
	stack_cue_set_id(window->selected_cue, stack_cue_string_to_id(gtk_entry_get_text(GTK_ENTRY(widget))));

	// Update the UI
	char cue_number[32];
	stack_cue_id_to_string(window->selected_cue->id, cue_number, 32);
	gtk_entry_set_text(GTK_ENTRY(widget), cue_number);
	stack_cue_list_widget_update_cue(window->sclw, window->selected_cue->uid, 2);

	return false;
}

// Cue property change
static void saw_cue_color_changed(GtkColorButton *widget, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Only attempt to change if we have a selected cue
	if (window->selected_cue == NULL)
	{
		return;
	}

	// Set the color
	GdkRGBA rgba;
	gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(widget), &rgba);
	stack_cue_set_color(window->selected_cue, (uint8_t)(rgba.red * 255.0), (uint8_t)(rgba.green * 255.0), (uint8_t)(rgba.blue * 255.0));

	// Update the UI
	stack_cue_list_widget_update_cue(window->sclw, window->selected_cue->uid, 0);
}

// Cue property change (done on focus-out)
static gboolean saw_cue_name_changed(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Only attempt to change if we have a selected cue
	if (window->selected_cue == NULL)
	{
		return false;
	}

	// Set the name
	stack_cue_set_name(window->selected_cue, gtk_entry_get_text(GTK_ENTRY(widget)));

	// Update the UI
	stack_cue_list_widget_update_cue(window->sclw, window->selected_cue->uid, 3);

	return false;
}

// Cue property change (done on focus-out)
static gboolean saw_cue_notes_changed(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Only attempt to change if we have a selected cue
	if (window->selected_cue == NULL)
	{
		return false;
	}

	// Set the notes
	GtkTextIter start, end;
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
	gtk_text_buffer_get_start_iter(buffer, &start);
	gtk_text_buffer_get_end_iter(buffer, &end);
	stack_cue_set_notes(window->selected_cue, gtk_text_buffer_get_text(buffer, &start, &end, false));

	// Update the UI
	stack_cue_list_widget_update_cue(window->sclw, window->selected_cue->uid, 0);

	return false;
}

// Cue property change (done on focus-out)
static gboolean saw_cue_prewait_changed(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Only attempt to change if we have a selected cue
	if (window->selected_cue == NULL)
	{
		return false;
	}

	// Set the time
	stack_cue_set_pre_time(window->selected_cue, stack_time_string_to_ns(gtk_entry_get_text(GTK_ENTRY(widget))));

	// Update the UI
	saw_ucp_wait(window, window->selected_cue, true);
	stack_cue_list_widget_update_cue(window->sclw, window->selected_cue->uid, 4);

	return false;
}

// Cue property change (done on focus-out)
static gboolean saw_cue_postwait_changed(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Only attempt to change if we have a selected cue
	if (window->selected_cue == NULL)
	{
		return false;
	}

	// Set the time
	stack_cue_set_post_time(window->selected_cue, stack_time_string_to_ns(gtk_entry_get_text(GTK_ENTRY(widget))));

	// Update the UI
	saw_ucp_wait(window, window->selected_cue, false);
	stack_cue_list_widget_update_cue(window->sclw, window->selected_cue->uid, 4);

	return false;
}

// Cue property change (done on focus-out)
static void saw_cue_postwait_trigger_changed(GtkRadioButton *widget, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Only attempt to change if we have a selected cue
	if (window->selected_cue == NULL)
	{
		return;
	}

	// Get pointers to the four radio button options
	GtkRadioButton* r1 = GTK_RADIO_BUTTON(gtk_builder_get_object(window->builder, "sawPostWaitTrigger1"));
	GtkRadioButton* r2 = GTK_RADIO_BUTTON(gtk_builder_get_object(window->builder, "sawPostWaitTrigger2"));
	GtkRadioButton* r3 = GTK_RADIO_BUTTON(gtk_builder_get_object(window->builder, "sawPostWaitTrigger3"));
	GtkRadioButton* r4 = GTK_RADIO_BUTTON(gtk_builder_get_object(window->builder, "sawPostWaitTrigger4"));

	// Determine which one is toggled on
	StackCueWaitTrigger trigger = STACK_CUE_WAIT_TRIGGER_NONE;
	if (widget == r1 && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r1)))
	{
		trigger = STACK_CUE_WAIT_TRIGGER_NONE;
		gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(window->builder, "sawPostWait")), false);
	}
	if (widget == r2 && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r2)))
	{
		trigger = STACK_CUE_WAIT_TRIGGER_IMMEDIATE;
		gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(window->builder, "sawPostWait")), true);
	}
	if (widget == r3 && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r3)))
	{
		trigger = STACK_CUE_WAIT_TRIGGER_AFTERPRE;
		gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(window->builder, "sawPostWait")), true);
	}
	if (widget == r4 && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r4)))
	{
		trigger = STACK_CUE_WAIT_TRIGGER_AFTERACTION;
		gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(window->builder, "sawPostWait")), true);
	}

	// Set the cue post-wait trigger
	stack_cue_set_post_trigger(window->selected_cue, trigger);

	// No need to update the UI on this one (currently)
}

// Key event whilst treeview has focus
static gboolean saw_cue_list_key_event(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// On key release
	if (event->type == GDK_KEY_RELEASE)
	{
		// Only attempt to invoke if we have a selected cue and delete was hit
		if (window->selected_cue != NULL && ((GdkEventKey*)event)->keyval == GDK_KEY_Delete)
		{
			// Call Edit -> Delete
			saw_edit_delete_clicked(widget, user_data);
			return true;
		}

		// If Escape was hit
		if (((GdkEventKey*)event)->keyval == GDK_KEY_Escape)
		{
			// Last time escape was pressed (note: static!)
			static stack_time_t last_escape_time = 0;

			// Only run if Escape is pressed twice within a short time (350ms)
			if (last_escape_time > 0 && stack_get_clock_time() - last_escape_time < 350000000)
			{
				// Call Stop All
				saw_cue_stop_all_clicked(widget, user_data);
			}

			// Keep track of the last time escape was pressed
			last_escape_time = stack_get_clock_time();

			return true;
		}

		// If F2 (rename) was hit
		if (((GdkEventKey*)event)->keyval == GDK_KEY_F2)
		{
			// Jump to the cue properties page and focus the name field
			gtk_notebook_set_current_page(window->notebook, 0);
			gtk_widget_grab_focus(GTK_WIDGET(gtk_builder_get_object(window->builder, "sawCueName")));
		}
	}
	else if (event->type == GDK_KEY_PRESS)
	{
		// If Space was hit
		if (((GdkEventKey*)event)->keyval == GDK_KEY_space)
		{
			// Call Play
			saw_cue_play_clicked(widget, user_data);
			return true;
		}
	}

	return false;
}

// Callback for a file being dropped
void saw_file_dropped(GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *data, guint info, guint time, gpointer user_data)
{
	StackAppWindow *window = STACK_APP_WINDOW(user_data);
	gchar **files = gtk_selection_data_get_uris(data);
	if (files != NULL)
	{
		// Iterate over the list of files
		size_t index = 0;
		while (files[index] != NULL)
		{
			stack_log("saw_file_dropped: files[%d] = %s\n", index, files[index]);

			// Lock the cue list
			stack_cue_list_lock(window->cue_list);

			// Create the new cue
			StackCue* new_cue = STACK_CUE(stack_cue_new("StackAudioCue", window->cue_list));
			if (new_cue != NULL)
			{
				// Set the file on the cue
				stack_property_set_string(stack_cue_get_property(new_cue, "file"), STACK_PROPERTY_VERSION_DEFINED, (const char*)files[index]);

				// Add the list to our cue stack
				stack_cue_list_append(window->cue_list, STACK_CUE(new_cue));
			}

			// Unlock the cue list
			stack_cue_list_unlock(window->cue_list);

			// Update that last row in the list store with the basics of the cue
			if (new_cue != NULL)
			{
				stack_cue_list_widget_update_cue(window->sclw, new_cue->uid, 0);
			}

			// Iterate
			index++;
		}

		// Select the new cue (or the last of the new cues)
		saw_select_last_cue(window);
	}

	// Tidy up
	g_free(files);

	// Tell our drag source we're done
	gtk_drag_finish(context, true, false, time);
}

// Initialises the window
static void stack_app_window_init(StackAppWindow *window)
{
	stack_log("stack_app_window_init()\n");

	// Object set up:
	window->selected_cue = NULL;
	window->use_custom_style = true;
	window->active_cue_widgets = stack_cue_widget_map_t();

	// Set up window signal handlers
	g_signal_connect(window, "destroy", G_CALLBACK(saw_destroy), (gpointer)window);
	g_signal_connect(window, "update-selected-cue", G_CALLBACK(saw_update_selected_cue), (gpointer)window);
	g_signal_connect(window, "update-cue", G_CALLBACK(saw_update_cue), (gpointer)window);
	g_signal_connect(window, "update-cue-properties", G_CALLBACK(saw_update_cue_properties), (gpointer)window);

	// Initialise this windows cue stack, defaulting to two channels
	window->cue_list = stack_cue_list_new(2);
	window->cue_list->state_change_func = saw_cue_state_changed;
	window->cue_list->state_change_func_data = (void*)window;

	if (window->use_custom_style)
	{
		// Read our CSS file and generate a CSS provider
		GtkCssProvider *cssp = gtk_css_provider_new();
		gtk_css_provider_load_from_resource(cssp, "/org/stack/ui/stack.css");

		// Get the screen
		GdkScreen *screen = gdk_screen_get_default();

		// Change the context
		gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(cssp), GTK_STYLE_PROVIDER_PRIORITY_USER);

		// Tidy up
		g_object_unref(cssp);
	}

	// Read the builder file
	window->builder = gtk_builder_new_from_resource("/org/stack/ui/window.ui");

	// Setup the callbacks - main UI
	gtk_builder_add_callback_symbol(window->builder, "saw_file_new_clicked", G_CALLBACK(saw_file_new_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_file_open_clicked", G_CALLBACK(saw_file_open_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_file_save_clicked", G_CALLBACK(saw_file_save_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_file_save_as_clicked", G_CALLBACK(saw_file_save_as_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_file_quit_clicked", G_CALLBACK(saw_file_quit_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_edit_cut_clicked", G_CALLBACK(saw_edit_cut_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_edit_copy_clicked", G_CALLBACK(saw_edit_copy_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_edit_paste_clicked", G_CALLBACK(saw_edit_paste_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_edit_delete_clicked", G_CALLBACK(saw_edit_delete_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_edit_select_all_clicked", G_CALLBACK(saw_edit_select_all_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_edit_show_settings_clicked", G_CALLBACK(saw_edit_show_settings_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_add_group_clicked", G_CALLBACK(saw_cue_add_group_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_add_audio_clicked", G_CALLBACK(saw_cue_add_audio_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_add_fade_clicked", G_CALLBACK(saw_cue_add_fade_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_add_action_clicked", G_CALLBACK(saw_cue_add_action_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_renumber_clicked", G_CALLBACK(saw_cue_renumber_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_play_clicked", G_CALLBACK(saw_cue_play_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_stop_clicked", G_CALLBACK(saw_cue_stop_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_stop_all_clicked", G_CALLBACK(saw_cue_stop_all_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_view_active_cue_list_clicked", G_CALLBACK(saw_view_active_cue_list_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_help_about_clicked", G_CALLBACK(saw_help_about_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_selected", G_CALLBACK(saw_cue_selected));

	// Set up the callbacks - default property pages
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_number_changed", G_CALLBACK(saw_cue_number_changed));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_color_changed", G_CALLBACK(saw_cue_color_changed));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_name_changed", G_CALLBACK(saw_cue_name_changed));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_notes_changed", G_CALLBACK(saw_cue_notes_changed));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_prewait_changed", G_CALLBACK(saw_cue_prewait_changed));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_postwait_changed", G_CALLBACK(saw_cue_postwait_changed));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_postwait_trigger_changed", G_CALLBACK(saw_cue_postwait_trigger_changed));

	// Connect the signals
	gtk_builder_connect_signals(window->builder, (gpointer)window);

	// Get the StackAppWindow
	GObject* wintpl = gtk_builder_get_object(window->builder, "StackAppWindow");

	// Set the window icon
	window->icon = gdk_pixbuf_new_from_resource("/org/stack/icons/stack-icon-256.png", NULL);
	gtk_window_set_icon(GTK_WINDOW(window), window->icon);

	// Get the StackAppWindow's child
	GObject* contents = gtk_builder_get_object(window->builder, "StackAppWindowContents");

	// Setup custom list view
	window->sclw = STACK_CUE_LIST_WIDGET(stack_cue_list_widget_new());
	window->sclw->cue_list = window->cue_list;
	gtk_container_remove(GTK_CONTAINER(gtk_builder_get_object(window->builder, "sawVPanel")), GTK_WIDGET(gtk_builder_get_object(window->builder, "sawCueListPlaceholder")));
	gtk_paned_add1(GTK_PANED(gtk_builder_get_object(window->builder, "sawVPanel")), GTK_WIDGET(window->sclw));
	gtk_widget_set_visible(GTK_WIDGET(window->sclw), true);

	// Master Out Meter: Get the UI item to add the cue to
	GtkBox *active_cues = GTK_BOX(gtk_builder_get_object(window->builder, "sawActiveCuesBox"));

	// Master Out Meter: Create
	window->master_out_meter = STACK_LEVEL_METER(stack_level_meter_new(2, -90.0, 0.0));
	gtk_widget_set_visible(GTK_WIDGET(window->master_out_meter), true);
	GValue height_request = G_VALUE_INIT;
	g_value_init(&height_request, G_TYPE_INT);
	g_value_set_int(&height_request, 20);
	g_object_set_property(G_OBJECT(window->master_out_meter), "height-request", &height_request);
	gtk_box_pack_start(active_cues, GTK_WIDGET(window->master_out_meter), false, false, 4);
	gtk_box_reorder_child(active_cues, GTK_WIDGET(window->master_out_meter), 1);

	// Reparent so that this StackAppWindow instance has the
	gtk_container_remove(GTK_CONTAINER(wintpl), GTK_WIDGET(contents));
	gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(contents));

	// Set up the window
	gtk_window_set_title(GTK_WINDOW(window), "Stack");
	gtk_window_set_default_size(GTK_WINDOW(window), 950, 650);

	// Set up accelerators
	GtkAccelGroup* ag = GTK_ACCEL_GROUP(gtk_builder_get_object(window->builder, "sawAccelGroup"));
	gtk_window_add_accel_group(GTK_WINDOW(window), ag);

	// Add on the non-stock accelerators that we're using
	gtk_accel_group_connect(ag, 'G', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE, g_cclosure_new(G_CALLBACK(saw_cue_add_group_clicked), window, NULL));
	gtk_accel_group_connect(ag, '1', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE, g_cclosure_new(G_CALLBACK(saw_cue_add_audio_clicked), window, NULL));
	gtk_accel_group_connect(ag, '2', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE, g_cclosure_new(G_CALLBACK(saw_cue_add_fade_clicked), window, NULL));
	gtk_accel_group_connect(ag, '3', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE, g_cclosure_new(G_CALLBACK(saw_cue_add_action_clicked), window, NULL));
	gtk_accel_group_connect(ag, 'A', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE, g_cclosure_new(G_CALLBACK(saw_edit_select_all_clicked), window, NULL));

	// Apply some input validation
	stack_limit_gtk_entry_float(GTK_ENTRY(gtk_builder_get_object(window->builder, "sawCueNumber")), false);
	stack_limit_gtk_entry_time(GTK_ENTRY(gtk_builder_get_object(window->builder, "sawPostWait")), false);
	stack_limit_gtk_entry_time(GTK_ENTRY(gtk_builder_get_object(window->builder, "sawPreWait")), false);

	// Store some things in our class for easiness
	window->notebook = GTK_NOTEBOOK(gtk_builder_get_object(window->builder, "sawCuePropsTabs"));

	// Set up signal handler for drag-drop in cue list
	g_signal_connect(window->sclw, "drag-data-received", G_CALLBACK(saw_file_dropped), (gpointer)window);
	g_signal_connect(window->sclw, "primary-selection-changed", G_CALLBACK(saw_cue_selected), (gpointer)window);
	g_signal_connect(window->sclw, "key-press-event", G_CALLBACK(saw_cue_list_key_event), (gpointer)window);
	g_signal_connect(window->sclw, "key-release-event", G_CALLBACK(saw_cue_list_key_event), (gpointer)window);

	// Set up a drop target to handle files being dropped
	gtk_drag_dest_set(GTK_WIDGET(window->sclw), GTK_DEST_DEFAULT_ALL, NULL, 0, GDK_ACTION_COPY);
	gtk_drag_dest_add_uri_targets(GTK_WIDGET(window->sclw));

	// Set up a timer to periodically refresh the UI
	window->timer_state = 0;
	gdk_threads_add_timeout(33, (GSourceFunc)saw_ui_timer, (gpointer)window);

	// Setup the default device
	saw_setup_default_device(window);

	// Set the focus to the cue list
	gtk_widget_grab_focus(GTK_WIDGET(window->sclw));
}

StackCue* stack_select_cue_dialog(StackAppWindow *window, StackCue *current, StackCue *hide)
{
	// Build the dialog
	GtkBuilder *builder = gtk_builder_new_from_resource("/org/stack/ui/SelectCue.ui");
	GtkDialog *dialog = GTK_DIALOG(gtk_builder_get_object(builder, "cueSelectDialog"));
	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window));

	// Set up response buttons
	gtk_dialog_add_buttons(dialog, "Clear Cue", 3, "Cancel", 2, "Select", 1, NULL);
	gtk_dialog_set_default_response(dialog, 1);

	// Get the treeview
	GtkTreeView *treeview = GTK_TREE_VIEW(gtk_builder_get_object(builder, "csdTreeView"));
	GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(treeview));

	// Lock the cue list
	stack_cue_list_lock(window->cue_list);

	// Get an iterator over the cue list
	void *citer = stack_cue_list_iter_front(window->cue_list);

	// Iterate over the cue list
	while (!stack_cue_list_iter_at_end(window->cue_list, citer))
	{
		// Get the cue
		StackCue *cue = stack_cue_list_iter_get(citer);

		if (cue != hide)
		{
			// Append a row to the dialog
			GtkTreeIter iter;
			gtk_list_store_append(store, &iter);

			// If we're the current cue, select it
			if (cue == current)
			{
				gtk_tree_selection_select_iter(gtk_tree_view_get_selection(treeview), &iter);
			}

			// Build cue number
			char cue_number[32];
			stack_cue_id_to_string(cue->id, cue_number, 32);

			// Update iterator
			gtk_list_store_set(store, &iter,
				0, cue_number,
				1, stack_cue_get_rendered_name(cue),
				2, (gpointer)cue, -1);
		}

		// Iterate
		citer = stack_cue_list_iter_next(citer);
	}

	// Free the iterator
	stack_cue_list_iter_free(citer);

	// Unlock the cue list
	stack_cue_list_unlock(window->cue_list);

	// Run the dialog
	gint response = gtk_dialog_run(dialog);

	// Return a cue based on the response
	StackCue *return_cue;
	switch (response)
	{
		case 1:	// Select
			// Get the selected rows
			GtkTreeModel *model;
			GList *list;

			list = gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(treeview), &model);

			// If there is a selection
			if (list != NULL)
			{
				// Choose the last element in the selection (in case we're multi-select)
				list = g_list_last(list);

				// Get the path to the selected row
				GtkTreePath *path = (GtkTreePath*)(list->data);

				// Get the data from the tree model for that row
				GtkTreeIter iter;
				gtk_tree_model_get_iter(model, &iter, path);

				// Get the cue
				gpointer selected_cue;
				gtk_tree_model_get(model, &iter, 3, &selected_cue, -1);

				// Tidy up
				g_list_free_full(list, (GDestroyNotify) gtk_tree_path_free);

				// Return the cue
				return_cue = STACK_CUE(selected_cue);
			}
			else
			{
				return_cue = NULL;
			}
			break;

		case 3:	// Clear Cue
			return_cue = NULL;
			break;

		case 2:	// Cancel
		default:
			return_cue = current;
			break;
	}

	// Destroy the dialog
	gtk_widget_destroy(GTK_WIDGET(dialog));

	// Free the builder
	g_object_unref(builder);

	return return_cue;
}

// Initialises the StackAppWindow class
static void stack_app_window_class_init(StackAppWindowClass *cls)
{
	// Define an "update-selected-cue" signal for StackAppWindow
	g_signal_new("update-selected-cue", stack_app_window_get_type(), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
	g_signal_new("update-cue", stack_app_window_get_type(), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
	g_signal_new("update-cue-properties", stack_app_window_get_type(), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
}

// Creates a new StackAppWindow
StackAppWindow* stack_app_window_new(StackApp *app)
{
	return (StackAppWindow*)g_object_new(stack_app_window_get_type(), "application", app, NULL);
}

// Opens a given file in the StackAppWindow
void stack_app_window_open(StackAppWindow *window, GFile *file)
{
	stack_log("stack_app_window_open()\n");

	// Get the URI of the File
	char *uri = g_file_get_uri(file);

	// Set up the loading dialog and data
	ShowLoadingData sld;
	sld.builder = gtk_builder_new_from_resource("/org/stack/ui/StackLoading.ui");
	sld.dialog = GTK_DIALOG(gtk_builder_get_object(sld.builder, "StackLoadingDialog"));
	sld.window = window;
	sld.uri = uri;
	sld.new_cue_list = NULL;
	sld.finished = false;
	sld.progress = 0;
	sld.message = NULL;
	gtk_window_set_transient_for(GTK_WINDOW(sld.dialog), GTK_WINDOW(window));
	gtk_window_set_default_size(GTK_WINDOW(sld.dialog), 350, 150);
	gtk_dialog_add_buttons(sld.dialog, "Cancel", 1, NULL);
	gtk_dialog_set_default_response(sld.dialog, 1);

	// Start the thread, dialog update timer, and show the dialog
	guint id = g_timeout_add(20, saw_open_file_timer, (gpointer)&sld);
	std::thread thread = std::thread(saw_open_file_thread, &sld);

	// This call blocks until the dialog goes away
	gint result = gtk_dialog_run(sld.dialog);

	// If we get here, either the timer killed the dialog, or the user
	// cancelled. In either case, wait for the thread to die
	thread.join();

	// If the result from the dialog was 1 (Cancel - see gtk_dialog_add_buttons
	// above), then we were cancelled
	if (result == 1)
	{
		// Stop the timer from doing anything else
		sld.finished = true;

		// Delete the cue list
		if (sld.new_cue_list)
		{
			stack_cue_list_destroy(sld.new_cue_list);

			// Set the pointer to NULL, so the if() below does nothing
			sld.new_cue_list = NULL;
		}
	}

	// Clear the loading dialog
	gtk_widget_destroy(GTK_WIDGET(sld.dialog));
	sld.dialog = NULL;
	g_object_unref(sld.builder);
	sld.builder = NULL;

	if (sld.new_cue_list != NULL)
	{
		// Set up state change notification
		sld.new_cue_list->state_change_func = saw_cue_state_changed;
		sld.new_cue_list->state_change_func_data = (void*)window;

		// Stop all the cues
		saw_cue_stop_all_clicked(NULL, (gpointer)window);

		// Tidy up the widgets from the now-stopped cuews
		saw_remove_inactive_cue_widgets(window);

		// Deselect any selected tab
		if (window->selected_cue != NULL)
		{
			stack_cue_unset_tabs(window->selected_cue, window->notebook);
			window->selected_cue = NULL;
		}

		// We don't need to worry about the UI timer, as that's running on the
		// same thread as the event loop that is handling this event handler

		// Destroy the old cue list
		stack_cue_list_destroy(window->cue_list);

		// Store the new cue list
		window->cue_list = sld.new_cue_list;
		stack_cue_list_widget_set_cue_list(window->sclw, window->cue_list);

		// Refresh the cue list
		//saw_refresh_list_store_from_list(window);
		char title_buffer[512];
		snprintf(title_buffer, 512, "%s - Stack", uri);
		gtk_window_set_title(GTK_WINDOW(window), title_buffer);

		// Setup the default device
		saw_setup_default_device(window);

		// Set the focus to the cue list and select the first cue so we're
		// ready for playback
		gtk_widget_grab_focus(GTK_WIDGET(window->sclw));
		saw_select_first_cue(window);
	}

	// Tidy up
	g_free(uri);
}

