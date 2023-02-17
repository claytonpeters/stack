// Includes:
#include "StackApp.h"
#include "StackAudioDevice.h"
#include <cstring>
#include <cstdlib>
#include <cmath>

// GTK stuff
G_DEFINE_TYPE(StackAppWindow, stack_app_window, GTK_TYPE_APPLICATION_WINDOW);
#define STACK_APP_WINDOW(_w) ((StackAppWindow*)(_w))

// Structure used to search for a cue within the model
typedef struct ModelFindCue
{
	StackCue *cue;
	GtkTreePath *path;
} ModelFindCue;

// Structure used to contain show opening data
typedef struct ShowLoadingData
{
	StackAppWindow* window;
	char* uri;
	GtkDialog* dialog;
	GtkBuilder* builder;
	StackCueList* new_cue_list;
	const char *message;
	double progress;
	bool finished;
} ShowLoadingData;

// Pre-define some function definitions:
static void saw_cue_stop_all_clicked(void* widget, gpointer user_data);
static void saw_remove_inactive_cue_widgets(StackAppWindow *window);

static void set_stack_pulse_thread_priority(StackAppWindow* window)
{
	// Set thread priority
	struct sched_param param = { 5 };
	if (pthread_setschedparam(window->pulse_thread.native_handle(), SCHED_RR, &param) != 0)
	{
		fprintf(stderr, "stack_pulse_thread(): Failed to set pulse thread priority.\n");
	}
}

// Callback for cue pulsing timer
static void stack_pulse_thread(StackAppWindow* window)
{
	// Loop until we're being destroyed
	while (!window->kill_thread)
	{
		// Lock the cue list
		stack_cue_list_lock(window->cue_list);

		// Send pulses to all the active cues
		stack_cue_list_pulse(window->cue_list);

		// Unlock the cue list
		stack_cue_list_unlock(window->cue_list);

		// Sleep for a millisecond
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	return;
}

// Callback when loading a show to update our loading dialog
static void saw_open_file_callback(StackCueList *cue_list, double progress, const char *message, void *data)
{
	// Get our show loading data
	ShowLoadingData *sld = (ShowLoadingData*)data;

	// Update our show loading data
	sld->message = message;
	sld->progress = progress;

	fprintf(stderr, "saw_open_file_callback: %s (%.2f%%)\n", message, progress * 100.0);
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

// gtk_tree_model_foreach callback for searching for a cue. Pass a ModelFindCue* as the user_data
static gboolean saw_model_foreach_find_cue(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	// Get the search data
	ModelFindCue *mfc = (ModelFindCue*)data;

	// Get the cue from the model
	StackCue *cue;
	gtk_tree_model_get(model, iter, STACK_MODEL_CUE_POINTER, &cue, -1);

	// If we've found the cue, return true
	if (cue == mfc->cue)
	{
		mfc->path = gtk_tree_path_copy(path);
		return true;
	}

	return false;
}

// Updates the list store of cues ('store'), updating the row pointed to by
// 'iter' with information from 'cue'
static void saw_update_list_store_from_cue(GtkListStore *store, GtkTreeIter *iter, StackCue *cue)
{
	char pre_buffer[32], action_buffer[32], post_buffer[32], col_buffer[8];
	double pre_pct = 0.0, action_pct = 0.0, post_pct = 0.0;

	// Format time strings
	if (cue->state == STACK_CUE_STATE_ERROR || cue->state == STACK_CUE_STATE_PREPARED || cue->state == STACK_CUE_STATE_STOPPED)
	{
		// If cue is stopped, display their total times
		stack_format_time_as_string(cue->pre_time, pre_buffer, 32);
		stack_format_time_as_string(cue->action_time, action_buffer, 32);
		stack_format_time_as_string(cue->post_time, post_buffer, 32);
	}
	else
	{
		// If cue is running or paused, display the time left
		stack_time_t rpre, raction, rpost;
		stack_cue_get_running_times(cue, stack_get_clock_time(), &rpre, &raction, &rpost, NULL, NULL, NULL);

		// Format the times
		stack_format_time_as_string(cue->pre_time - rpre, pre_buffer, 32);
		stack_format_time_as_string(cue->action_time - raction, action_buffer, 32);
		stack_format_time_as_string(cue->post_time - rpost, post_buffer, 32);

		// Calculate fractions
		if (cue->pre_time != 0)
		{
			pre_pct = 100.0 * double(rpre) / double(cue->pre_time);
		}
		if (cue->action_time != 0)
		{
			action_pct = 100.0 * double(raction) / double(cue->action_time);
		}
		if (cue->post_time != 0)
		{
			post_pct = 100.0 * double(rpost) / double(cue->post_time);
		}
	}

	// Format color
	snprintf(col_buffer, 8, "#%02x%02x%02x", cue->r, cue->g, cue->b);

	// Decide on icon depending on state
	const char* icon = "";
	switch (cue->state)
	{
		case STACK_CUE_STATE_ERROR:
			icon = "process-stop";
			break;

		case STACK_CUE_STATE_PAUSED:
			icon = "media-playback-pause";
			break;

		case STACK_CUE_STATE_PREPARED:
			icon = "media-seek-forward";
			break;

		case STACK_CUE_STATE_PLAYING_PRE:
		case STACK_CUE_STATE_PLAYING_ACTION:
		case STACK_CUE_STATE_PLAYING_POST:
			icon = "media-playback-start";
			break;
	}

	// Build cue number
	char cue_number[32];
	stack_cue_id_to_string(cue->id, cue_number, 32);

	// Get error message
	char error_message[512];
	char *message = NULL;
	if (cue->state == STACK_CUE_STATE_ERROR)
	{
		stack_cue_get_error(cue, error_message, 512);
		message = error_message;
	}

	// Update iterator
	gtk_list_store_set(store, iter,
		STACK_MODEL_CUEID,            cue_number,
		STACK_MODEL_NAME,             stack_cue_get_rendered_name(cue),
		STACK_MODEL_PREWAIT_PERCENT,  pre_pct,
		STACK_MODEL_PREWAIT_TEXT,     pre_buffer,
		STACK_MODEL_ACTION_PERCENT,   action_pct,
		STACK_MODEL_ACTION_TEXT,      action_buffer,
		STACK_MODEL_POSTWAIT_PERCENT, post_pct,
		STACK_MODEL_POSTWAIT_TEXT,    post_buffer,
		STACK_MODEL_STATE_IMAGE,      icon,
		STACK_MODEL_COLOR,            col_buffer,
		STACK_MODEL_CUE_POINTER,      (gpointer)cue,
		STACK_MODEL_ERROR_MESSAGE,    message, -1);
}

// Updates the list store of cues ('store'), searching the store for the given
// 'cue' and then updating it's information
static void saw_update_list_store_from_cue(GtkListStore *store, StackCue *cue)
{
	// Search for the cue
	ModelFindCue mfc = {cue, NULL};
	gtk_tree_model_foreach(GTK_TREE_MODEL(store), saw_model_foreach_find_cue, (gpointer)&mfc);

	// If we've found the cue...
	if (mfc.path != NULL)
	{
		// Get a treeiter to the row
		GtkTreeIter iter;
		gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, mfc.path);

		// Update the row
		saw_update_list_store_from_cue(store, &iter, cue);

		// Tidy up
		gtk_tree_path_free(mfc.path);
	}
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
		saw_update_list_store_from_cue(window->store, cue);
	}
}

typedef struct StackCueListUpdateData {
	StackAppWindow *window;
	StackCue *cue;
} StackCueListUpdateData;

static gboolean saw_update_cue_main_thread(gpointer user_data)
{
	// Update the list data
	StackCueListUpdateData* data = (StackCueListUpdateData*)user_data;
	saw_update_list_store_from_cue(data->window->store, data->cue);

	// Make the widget redraw
	gtk_widget_queue_draw(GTK_WIDGET(data->window->treeview));

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

	// Unselect the items on the TreeView (this is to stop the crap
	// implementation of gtk_list_store_clear from iteratively selecting each
	// item in the list as it deletes them)
	gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(window->treeview));
	window->freeze_list_selections = true;

	// Clear the list store
	gtk_list_store_clear(window->store);

	// Allow selection changes again
	window->freeze_list_selections = false;
}

// Clears the entire list store and repopulates it with the information
// contained within the 'window's cue_list
static void saw_refresh_list_store_from_list(StackAppWindow *window)
{
	// Clear the list store
	saw_clear_list_store(window);

	// An iterator for each row
	GtkTreeIter iter;

	// Lock the cue list
	stack_cue_list_lock(window->cue_list);

	// Get an iterator over the cue list
	void *citer = stack_cue_list_iter_front(window->cue_list);

	// Iterate over the cue list
	while (!stack_cue_list_iter_at_end(window->cue_list, citer))
	{
		// Get the cue
		StackCue *cue = stack_cue_list_iter_get(citer);

		// Append a row to the list store and get an iterator
		gtk_list_store_append(window->store, &iter);

		// Update the row
		saw_update_list_store_from_cue(window->store, &iter, STACK_CUE(cue));

		// Iterate
		citer = stack_cue_list_iter_next(citer);
	}

	// Unlock the cue cue_list
	stack_cue_list_unlock(window->cue_list);

	// Free the iterator
	stack_cue_list_iter_free(citer);
}

static void saw_cue_state_changed(StackCueList *cue_list, StackCue *cue, void *user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Update the list store. We emit a signal here rather than calling
	// saw_update_cue to prevent the draw happening on the wrong thread when
	// called from the pulse thread
	g_signal_emit_by_name(window, "update-cue", cue);
}

// Updates a pre/post wait time on the properties panel
static void saw_ucp_wait(StackAppWindow *window, StackCue *cue, bool pre)
{
	char waitTime[64];
	stack_time_t ctime = pre ? cue->pre_time : cue->post_time;

	// Update cue post-wait time (rounding nanoseconds to seconds with three decimal places)
	stack_format_time_as_string(ctime, waitTime, 64);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(window->builder, pre ? "sawPreWait" : "sawPostWait")), waitTime);
}

// Updates the cue properties window with information from the given 'cue'.
// Can also be called by the signal 'update-cue-properties'
static void saw_update_cue_properties(gpointer user_data, StackCue *cue)
{
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Update cue number
	char cue_number[32];
	stack_cue_id_to_string(cue->id, cue_number, 32);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(window->builder, "sawCueNumber")), cue_number);

	// Update cue color
	GdkRGBA cueColor = {(double)cue->r / 255.0, (double)cue->g / 255.0, (double)cue->b / 255.0, 1.0};
	gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(gtk_builder_get_object(window->builder, "sawCueColor")), &cueColor);

	// Update cue name
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(window->builder, "sawCueName")), cue->name);

	// Update cue notes
	GtkTextView *textview = GTK_TEXT_VIEW(gtk_builder_get_object(window->builder, "sawCueNotes"));
	GtkTextBuffer* buffer = gtk_text_view_get_buffer(textview);
	gtk_text_buffer_set_text(buffer, cue->notes, -1);

	// Update cue pre-wait and post-wait times
	saw_ucp_wait(window, cue, true);
	saw_ucp_wait(window, cue, false);

	// Update post-wait trigger option (and enable/disable post-wait time as necessary)
	switch (cue->post_trigger)
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
}

// Selects the last cue on the list
static void saw_select_last_cue(StackAppWindow *window)
{
	GtkTreeIter iter;

	// Get the number of entries in the model
	gint entries = gtk_tree_model_iter_n_children(gtk_tree_view_get_model(window->treeview), NULL);

	// Get an iterator and path to the last row in the model
	gtk_tree_model_iter_nth_child(gtk_tree_view_get_model(window->treeview), &iter, NULL, entries - 1);
	GtkTreePath *path = gtk_tree_model_get_path(gtk_tree_view_get_model(window->treeview), &iter);	// Allocates!

	// Select that row
	gtk_tree_selection_select_iter(gtk_tree_view_get_selection(window->treeview), &iter);
	gtk_tree_view_set_cursor(window->treeview, path, NULL, false);

	// Tidy up
	gtk_tree_path_free(path);	// From gtk_tree_model_get_path
}

// Selects the next cue in the list
static void saw_select_next_cue(StackAppWindow *window, bool skip_automatic = false)
{
	GList *list;
	GtkTreeModel *model;

	// Get the selected cue
	list = gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(window->treeview), &model);

	// If there is a selection
	if (list != NULL)
	{
		// Choose the last element in the selection (in case we're multi-select)
		list = g_list_last(list);

		// Get the path to the selected row
		GtkTreePath *path = (GtkTreePath*)(list->data);

		// Get an iterator to that node
		GtkTreeIter iter;
		gtk_tree_model_get_iter(model, &iter, path);

		// Get the current cue (before moving)
		StackCue *old_cue = NULL;
		gtk_tree_model_get(model, &iter, STACK_MODEL_CUE_POINTER, &old_cue, -1);

		// Move the iterator forward one
		if (gtk_tree_model_iter_next(model, &iter))
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
					// If the cue we are moving from from would trigger the next cue
					if (old_cue->post_trigger != STACK_CUE_WAIT_TRIGGER_NONE)
					{
						// We need to keep searching forward. Get the "next" cue, so
						// the loop can check to see if we need to skip again on the
						// next ieration
						gtk_tree_model_get(model, &iter, STACK_MODEL_CUE_POINTER, &old_cue, -1);

						// Move the iterator forward
						if (!gtk_tree_model_iter_next(model, &iter))
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
					// Select the row that contains the next cue
					path = gtk_tree_model_get_path(model, &iter);
					if (path != NULL)
					{
						gtk_tree_selection_select_iter(gtk_tree_view_get_selection(window->treeview), &iter);
						gtk_tree_view_set_cursor(window->treeview, path, NULL, false);
						gtk_tree_path_free(path);	// From gtk_tree_model_get_path
					}
				}
			}
			else
			{
				// Get a path to the iterator (this allocates a GtkTreePath!)
				path = gtk_tree_model_get_path(model, &iter);

				if (path != NULL)
				{
					// Select that row
					gtk_tree_selection_select_iter(gtk_tree_view_get_selection(window->treeview), &iter);
					gtk_tree_view_set_cursor(window->treeview, path, NULL, false);

					// Tidy up
					gtk_tree_path_free(path);	// From gtk_tree_model_get_path
				}
			}
		}

		// Tidy up
		g_list_free_full(list, (GDestroyNotify) gtk_tree_path_free);
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

	// Start the cue list pulsing thread
	window->kill_thread = false;
	window->pulse_thread = std::thread(stack_pulse_thread, window);
	set_stack_pulse_thread_priority(window);
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

		// Tidy up
		g_free(uri);
	}

	gtk_widget_destroy(dialog);
}

// Menu/toolbar callback
static void saw_file_save_clicked(void* widget, gpointer user_data)
{
	// If the current cue list has no cue list
	if (STACK_APP_WINDOW(user_data)->cue_list->uri == NULL)
	{
		// ...call Save As instead
		saw_file_save_as_clicked(widget, user_data);
	}
	else
	{
		/// ...otherwise overwrite
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

	// Kill the cue list pulsing thread
	window->kill_thread = true;
	window->pulse_thread.join();

	// We don't need to worry about the UI timer, as that's running on the same
	// thread as the event loop that is handling this event handler

	// Destroy the old cue list
	stack_cue_list_destroy(window->cue_list);

	// Initialise a new cue list, defaulting to two channels
	window->cue_list = stack_cue_list_new(2);
	window->cue_list->state_change_func = saw_cue_state_changed;
	window->cue_list->state_change_func_data = (void*)window;

	// Refresh the cue list
	saw_refresh_list_store_from_list(window);
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
	fprintf(stderr, "Edit -> Cut clicked\n");
}

// Menu callback
static void saw_edit_copy_clicked(void* widget, gpointer user_data)
{
	fprintf(stderr, "Edit -> Copy clicked\n");
}

// Menu callback
static void saw_edit_paste_clicked(void* widget, gpointer user_data)
{
	fprintf(stderr, "Edit -> Paste clicked\n");
}

// Menu callback
static void saw_edit_delete_clicked(void* widget, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Store a pointer to the cue we're deleting (as the selection will change
	// during this function)
	StackCue *cue_to_delete = window->selected_cue;

	// If a cue has been selected
	if (cue_to_delete != NULL)
	{
		// Deselect the cue
		stack_cue_unset_tabs(cue_to_delete, window->notebook);
		window->selected_cue = NULL;

		GtkTreeModel *model;
		GList *list;

		// Get the selected cue
		list = gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(window->treeview), &model);

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

			// Remove the cue from the cue list
			stack_cue_list_lock(window->cue_list);
			stack_cue_list_remove(window->cue_list, cue_to_delete);
			stack_cue_list_unlock(window->cue_list);

			// Destroy the cue
			stack_cue_destroy(cue_to_delete);

			// Remove the cue from the cue store
			gtk_list_store_remove(GTK_LIST_STORE(model), &iter);

			// Tidy up
			g_list_free_full(list, (GDestroyNotify) gtk_tree_path_free);
		}
	}
}

// Edit -> Show Settings
static void saw_edit_show_settings_clicked(void* widget, gpointer user_data)
{
	sss_show_dialog(STACK_APP_WINDOW(user_data));
}

// Menu callback
static void saw_cue_add_group_clicked(void* widget, gpointer user_data)
{
	fprintf(stderr, "Cue -> Add Group clicked\n");
}

// Menu callback
static void saw_cue_add_audio_clicked(void* widget, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Lock the cue list
	stack_cue_list_lock(window->cue_list);

	// Create the new cue
	StackCue* new_cue = STACK_CUE(stack_cue_new("StackAudioCue", window->cue_list));
	if (new_cue == NULL)
	{
		stack_cue_list_unlock(window->cue_list);
		return;
	}

	// Add the list to our cue stack
	stack_cue_list_append(window->cue_list, STACK_CUE(new_cue));

	// Unlock the cue list
	stack_cue_list_unlock(window->cue_list);

	// Append a row to the list store and get an iterator
	GtkTreeIter iter;
	gtk_list_store_append(window->store, &iter);

	// Update that last row in the list store with the basics of the cue
	saw_update_list_store_from_cue(window->store, &iter, STACK_CUE(new_cue));

	// Select the new cue
	saw_select_last_cue(window);
}

// Menu callback
static void saw_cue_add_fade_clicked(void* widget, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Lock the cue list
	stack_cue_list_lock(window->cue_list);

	// Create the new cue
	StackCue* new_cue = STACK_CUE(stack_cue_new("StackFadeCue", window->cue_list));
	if (new_cue == NULL)
	{
		stack_cue_list_unlock(window->cue_list);
		return;
	}

	// Add the list to our cue stack
	stack_cue_list_append(window->cue_list, STACK_CUE(new_cue));

	// Unlock the cue list
	stack_cue_list_unlock(window->cue_list);

	// Append a row to the list store and get an iterator
	GtkTreeIter iter;
	gtk_list_store_append(window->store, &iter);

	// Update that last row in the list store with the basics of the cue
	saw_update_list_store_from_cue(window->store, &iter, STACK_CUE(new_cue));

	// Select the new cue
	saw_select_last_cue(window);
}

// Menu callback
static void saw_cue_add_action_clicked(void* widget, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Lock the cue list
	stack_cue_list_lock(window->cue_list);

	// Create the new cue
	StackCue* new_cue = STACK_CUE(stack_cue_new("StackActionCue", window->cue_list));
	if (new_cue == NULL)
	{
		stack_cue_list_unlock(window->cue_list);
		return;
	}

	// Add the list to our cue stack
	stack_cue_list_append(window->cue_list, STACK_CUE(new_cue));

	// Unlock the cue list
	stack_cue_list_unlock(window->cue_list);

	// Append a row to the list store and get an iterator
	GtkTreeIter iter;
	gtk_list_store_append(window->store, &iter);

	// Update that last row in the list store with the basics of the cue
	saw_update_list_store_from_cue(window->store, &iter, STACK_CUE(new_cue));

	// Select the new cue
	saw_select_last_cue(window);
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
			saw_update_list_store_from_cue(window->store, cue);

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
	gtk_about_dialog_set_version(about, "Version 0.1.20230211-1");
	gtk_about_dialog_set_copyright(about, "Copyright (c) 2023 Clayton Peters");
	gtk_about_dialog_set_comments(about, "A GTK+ based sound cueing application for theatre");
	gtk_about_dialog_set_website(about, "https://github.com/claytonpeters/stack");
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
		saw_update_list_store_from_cue(window->store, window->selected_cue);
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
		saw_update_list_store_from_cue(window->store, window->selected_cue);
	}
}

// Menu/toolbar callback
static void saw_cue_stop_all_clicked(void* widget, gpointer user_data)
{
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Lock the cue list
	stack_cue_list_lock(window->cue_list);

	// Get an iterator over the cue list
	void *citer = stack_cue_list_iter_front(window->cue_list);

	// Iterate over the cue list
	while (!stack_cue_list_iter_at_end(window->cue_list, citer))
	{
		// Get the cue
		StackCue *cue = stack_cue_list_iter_get(citer);

		if ((cue->state >= STACK_CUE_STATE_PLAYING_PRE && cue->state <= STACK_CUE_STATE_PLAYING_POST) || cue->state == STACK_CUE_STATE_PAUSED)
		{
			// Stop the cue
			stack_cue_stop(cue);

			// Update the row
			saw_update_list_store_from_cue(window->store, cue);
		}

		// Iterate
		citer = stack_cue_list_iter_next(citer);
	}

	// Unlock the cue list
	stack_cue_list_unlock(window->cue_list);

	// Free the iterator
	stack_cue_list_iter_free(citer);
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

gboolean stack_level_meter_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	// Get details
	GtkStyleContext *context = gtk_widget_get_style_context(widget);
	guint width = gtk_widget_get_allocated_width(widget);
	guint height = gtk_widget_get_allocated_height(widget);
	StackActiveCueWidget *cue_widget = (StackActiveCueWidget*)g_object_get_data(G_OBJECT(widget), "cue-widget");
	stack_time_t current_time = stack_get_clock_time();

	// Clear the background
	gtk_render_background(context, cr, 0, 0, width, height);

	// Create a dark gradient background
	cairo_pattern_t *dark_gradient = cairo_pattern_create_linear(0.0, 0.0, (float)width, 0.0);
	cairo_pattern_add_color_stop_rgb(dark_gradient, 0.0, 0.0, 0.25, 0.0);
	cairo_pattern_add_color_stop_rgb(dark_gradient, 0.75, 0.25, 0.25, 0.0);
	cairo_pattern_add_color_stop_rgb(dark_gradient, 1.0, 0.25, 0.0, 0.0);

	// Turn off antialiasing for speed
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

	// Create a gradient
	cairo_pattern_t *gradient = cairo_pattern_create_linear(0.0, 0.0, (float)width, 0.0);
	cairo_pattern_add_color_stop_rgb(gradient, 0.0, 0.0, 1.0, 0.0);
	cairo_pattern_add_color_stop_rgb(gradient, 0.75, 1.0, 1.0, 0.0);
	cairo_pattern_add_color_stop_rgb(gradient, 1.0, 1.0, 0.0, 0.0);

	StackChannelRMSData *rms = stack_cue_list_get_rms_data(cue_widget->cue_list, cue_widget->cue_uid);
	if (rms != NULL)
	{
		// Setup the colouring of the rectangles

		// Render the rectangles
		StackCue *cue = stack_cue_list_get_cue_by_uid(cue_widget->cue_list, cue_widget->cue_uid);

		size_t channel_count = stack_cue_get_active_channels(cue, NULL);
		float bar_height = floorf((float)height / (float)channel_count);
		for (size_t i = 0; i < channel_count; i++)
		{
			// Grab the level and put it in bounds
			const float min = -90.0;
			const stack_time_t peak_hold_time = 2 * NANOSECS_PER_SEC;
			float level = rms[i].current_level;
			if (level < min) { level = min; }
			if (level > 0.0) { level = 0.0; }
			const float level_x = (float)width * ((level - min) / -min);

			// Draw the dark section (we minus one on x for rounding errors)
			cairo_set_source(cr, dark_gradient);
			cairo_rectangle(cr, level_x - 1, bar_height * (float)i, width - level_x, bar_height - 1.0);
			cairo_fill(cr);

			// Draw the level
			cairo_set_source(cr, gradient);
			cairo_rectangle(cr, 0.0, bar_height * (float)i, level_x, bar_height - 1.0);
			cairo_fill(cr);

			// Grab the peak and put it in bounds
			// TODO: This should probably be in StackCueList instead
			if (current_time - rms[i].peak_time > peak_hold_time)
			{
				rms[i].peak_level -= 1.0;
			}
			float peak = rms[i].peak_level;
			if (peak < min) { peak = min; }
			if (peak > 0.0) { peak = 0.0; }
			float peak_x = (float)width * ((peak - min) / -min);

			// Draw the peak line
			cairo_set_source_rgb(cr, 1.0, 1.0, 0.0);
			cairo_move_to(cr, peak_x, bar_height * (float)i);
			cairo_rel_line_to(cr, 0, bar_height - 1);
			cairo_stroke(cr);
		}
	}

	// Tidy up
	cairo_pattern_destroy(dark_gradient);
	cairo_pattern_destroy(gradient);

	return FALSE;
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
		cue_widget->levels = GTK_DRAWING_AREA(gtk_drawing_area_new());
		gtk_widget_set_visible(GTK_WIDGET(cue_widget->levels), true);
		GValue height_request = G_VALUE_INIT;
		g_value_init(&height_request, G_TYPE_INT);
		g_value_set_int(&height_request, 20);
		g_object_set_property(G_OBJECT(cue_widget->levels), "height-request", &height_request);
		g_object_set_data(G_OBJECT(cue_widget->levels), "cue-widget", (gpointer)cue_widget);
		g_signal_connect(G_OBJECT(cue_widget->levels), "draw", G_CALLBACK(stack_level_meter_draw), NULL);

		// Get the UI item to add the cue to
		GtkBox *active_cues = GTK_BOX(gtk_builder_get_object(window->builder, "sawActiveCuesBox"));

		// Pack everything
		gtk_box_pack_start(cue_widget->vbox, GTK_WIDGET(cue_widget->name), false, false, 0);
		gtk_box_pack_start(cue_widget->vbox, GTK_WIDGET(cue_widget->time), false, false, 0);
		gtk_box_pack_start(cue_widget->vbox, GTK_WIDGET(cue_widget->levels), false, false, 2);
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
	stack_cue_get_running_times(cue, stack_get_clock_time(), &rpre, &raction, &rpost, NULL, NULL, NULL);

	// Update the name of the cue. We compare the strings first to avoid a more
	// expensive redraw in Gtk if it hasn't changed
	if (strcmp(gtk_label_get_text(cue_widget->name), stack_cue_get_rendered_name(cue)) != 0)
	{
		gtk_label_set_text(cue_widget->name, stack_cue_get_rendered_name(cue));
	}

	// Format the times
	stack_time_t first_time = 0, second_time = 0;
	char time_text[64] = {0};
	if (cue->state == STACK_CUE_STATE_PLAYING_PRE)
	{
		strncat(time_text, "Pre: ", 63);
		first_time = rpre;
		second_time = cue->pre_time;
	}
	else if (cue->state == STACK_CUE_STATE_PLAYING_ACTION)
	{
		first_time = raction;
		second_time = cue->action_time;
	}
	else if (cue->state == STACK_CUE_STATE_PLAYING_POST)
	{
		strncat(time_text, "Post: ", 63);
		first_time = rpost;
		second_time = cue->post_time;
	}
	stack_format_time_as_string(first_time, &time_text[strlen(time_text)], 64 - strlen(time_text));
	strncat(time_text, " / ", 63);
	stack_format_time_as_string(second_time, &time_text[strlen(time_text)], 64 - strlen(time_text));

	// Update the contents
	gtk_label_set_text(cue_widget->time, time_text);
	gtk_widget_queue_draw(GTK_WIDGET(cue_widget->levels));
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
			// Update the row
			saw_update_list_store_from_cue(window->store, cue);

			// Update active cue panel
			saw_add_or_update_active_cue_widget(window, cue);
		}

		// Iterate
		citer = stack_cue_list_iter_next(citer);
	}

	// Remove any inactive cues
	saw_remove_inactive_cue_widgets(window);

	// Unlock the cue list
	stack_cue_list_unlock(window->cue_list);

	// Free the iterator
	stack_cue_list_iter_free(citer);

	return true;
}

// Callback for when the selected cue changes
static void saw_cue_selected(GtkTreeSelection *selection, gpointer user_data)
{
	// Extract the window from the parameters
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// If we're currently allowing selection changes
	if (!window->freeze_list_selections)
	{
		// Get the selected rows
		GtkTreeModel *model = NULL;
		GList *list = gtk_tree_selection_get_selected_rows(selection, &model);

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
			gpointer cue;
			gtk_tree_model_get(model, &iter, STACK_MODEL_CUE_POINTER, &cue, -1);

			// Keep track of what notebook page we were on
			gint page = gtk_notebook_get_current_page(window->notebook);

			// Remove tabs from previously selected cue
			if (window->selected_cue != NULL)
			{
				stack_cue_unset_tabs(window->selected_cue, window->notebook);
			}

			// Update the window
			window->selected_cue = STACK_CUE(cue);
			saw_update_cue_properties(window, STACK_CUE(cue));

			// Put on the correct tabs
			stack_cue_set_tabs(window->selected_cue, window->notebook);

			// Try and put us back on the same notebook page
			gtk_notebook_set_current_page(window->notebook, page);

			// Tidy up
			g_list_free_full(list, (GDestroyNotify)gtk_tree_path_free);
		}
		else
		{
			window->selected_cue = NULL;
		}
	}
}

// Callback for when the window is destroyed
static void saw_destroy(GtkWidget* widget, gpointer user_data)
{
	fprintf(stderr, "saw_destroy() called\n");

	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	// Kill the pulse thread and wait for it to exit
	window->kill_thread = true;
	window->pulse_thread.join();

	// Clear the list store
	saw_clear_list_store(window);

	// Stop the timer
	window->timer_state = 2;

	// Busy wait whilst we wait for the timer to stop (this probably should be
	// done with a semaphore...)
	while (window->timer_state == 3) {}

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
	saw_update_list_store_from_cue(window->store, window->selected_cue);

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
	saw_update_list_store_from_cue(window->store, window->selected_cue);
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
	saw_update_list_store_from_cue(window->store, window->selected_cue);

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
	saw_update_list_store_from_cue(window->store, window->selected_cue);

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
	saw_update_list_store_from_cue(window->store, window->selected_cue);

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
	saw_update_list_store_from_cue(window->store, window->selected_cue);

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
	StackCueWaitTrigger trigger;
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
static gboolean saw_treeview_key_event(GtkWidget *widget, GdkEvent *event, gpointer user_data)
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

/*gboolean saw_treeview_query_tooltip(GtkWidget *widget, gint x, gint y, gboolean keyboard_mode, GtkTooltip *tooltip, gpointer user_data)
{
	fprintf(stderr, ".");

	GtkTreePath *path = NULL;
	GtkTreeViewColumn *column = NULL;
	gint cell_x = 0, cell_y = 0;
	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), x, y, &path, &column, &cell_x, &cell_y))
	{
		fprintf(stderr, "!");
		if (strcmp(gtk_tree_view_column_get_title(column), "") == 0)
		{
			fprintf(stderr, "-");
			gtk_tooltip_set_text(tooltip, "Hello");
			return true;
		}
	}

	return false;
}*/

// Callback for a dragged row
void saw_row_dragged(GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *data, guint info, guint time, gpointer user_data)
{
	StackAppWindow *window = STACK_APP_WINDOW(user_data);

	GtkTreePath *dest_path = NULL;
	GtkTreeViewDropPosition pos;

	// This seems to be the condition of whether a drop has actually happened...
	if (x != 0 && y != 0)
	{
		// Get the index of the drop
		size_t new_index = 0;

		// Check to see if there is a row at (x,y) where we are being dropped
		if (gtk_tree_view_get_dest_row_at_pos(window->treeview, x, y, &dest_path, &pos))
		{
			// We have a row, figure out where we're going
			new_index = gtk_tree_path_get_indices(dest_path)[0];
			if (pos == GTK_TREE_VIEW_DROP_AFTER)
			{
				new_index++;
			}
		}
		else
		{
			// We have no row, assume end of list
			new_index = stack_cue_list_count(window->cue_list);
		}

		GtkTreeModel *model;
		GList *list;

		// Get the selected cue
		list = gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(window->treeview), &model);

		// If there is a selection
		if (list != NULL)
		{
			list = g_list_first(list);

			// Get the path to the selected row
			GtkTreePath *path = (GtkTreePath*)(list->data);

			// Get the data from the tree model for that row
			GtkTreeIter iter;
			gtk_tree_model_get_iter(model, &iter, path);

			// Get the cue
			gpointer cue;
			gtk_tree_model_get(model, &iter, STACK_MODEL_CUE_POINTER, &cue, -1);

			fprintf(stderr, "saw_row_dragged(): Moving cue %d (uid: 0x%016lx) to index %lu\n", STACK_CUE(cue)->id, STACK_CUE(cue)->uid, new_index);

			// Remove the cue from the cue list and move it to it's new location
			stack_cue_list_lock(window->cue_list);
			stack_cue_list_move(window->cue_list, STACK_CUE(cue), new_index);
			stack_cue_list_unlock(window->cue_list);

			// Tidy up
			g_list_free_full(list, (GDestroyNotify)gtk_tree_path_free);
		}
	}
}

// Initialises the window
static void stack_app_window_init(StackAppWindow *window)
{
	fprintf(stderr, "stack_app_window_init()\n");

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
		GFile* file = g_file_new_for_path("stack.css");
		gtk_css_provider_load_from_file(cssp, file, NULL);

		// Get the screen
		GdkScreen *screen = gdk_screen_get_default();

		// Change the context
		gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(cssp), GTK_STYLE_PROVIDER_PRIORITY_USER);

		// Tidy up
		g_object_unref(cssp);
		g_object_unref(file);
	}

	// Read the builder file
	window->builder = gtk_builder_new_from_file("window.ui");

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

	// Set up the callbacks - other
	gtk_builder_add_callback_symbol(window->builder, "saw_treeview_key_event", G_CALLBACK(saw_treeview_key_event));
	//gtk_builder_add_callback_symbol(window->builder, "saw_treeview_query_tooltip", G_CALLBACK(saw_treeview_query_tooltip));

	// Connect the signals
	gtk_builder_connect_signals(window->builder, (gpointer)window);

	// Get the StackAppWindow
	GObject* wintpl = gtk_builder_get_object(window->builder, "StackAppWindow");

	// Get the StackAppWindow's child
	GObject* contents = gtk_builder_get_object(window->builder, "StackAppWindowContents");

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

	// Store some things in our class for easiness
	window->treeview = GTK_TREE_VIEW(gtk_builder_get_object(window->builder, "sawCuesTreeView"));
	window->store = GTK_LIST_STORE(gtk_tree_view_get_model(window->treeview));
	window->notebook = GTK_NOTEBOOK(gtk_builder_get_object(window->builder, "sawCuePropsTabs"));

	// Set up signal handler for drag-drop in cue list
	g_signal_connect(window->treeview, "drag-data-received", G_CALLBACK(saw_row_dragged), (gpointer)window);

	// Set up a timer to periodically refresh the UI
	window->timer_state = 0;
	gdk_threads_add_timeout(51, (GSourceFunc)saw_ui_timer, (gpointer)window);

	// Setup the default device
	saw_setup_default_device(window);
}

StackCue* stack_select_cue_dialog(StackAppWindow *window, StackCue *current)
{
	// Build the dialog
	GtkBuilder *builder = gtk_builder_new_from_file("SelectCue.ui");
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

		// Format color
		char col_buffer[8];
		snprintf(col_buffer, 8, "#%02x%02x%02x", cue->r, cue->g, cue->b);

		// Update iterator
		gtk_list_store_set(store, &iter,
			0, cue_number,
			1, stack_cue_get_rendered_name(cue),
			2, col_buffer,
			3, (gpointer)cue, -1);

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
	fprintf(stderr, "stack_app_window_open()\n");

	// Get the URI of the File
	char *uri = g_file_get_uri(file);

	// Set up the loading dialog and data
	ShowLoadingData sld;
	sld.builder = gtk_builder_new_from_file("StackLoading.ui");
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

		// Kill the cue list pulsing thread
		window->kill_thread = true;
		window->pulse_thread.join();

		// We don't need to worry about the UI timer, as that's running on the same
		// thread as the event loop that is handling this event handler

		// Destroy the old cue list
		stack_cue_list_destroy(window->cue_list);

		// Store the new cue list
		window->cue_list = sld.new_cue_list;

		// Refresh the cue list
		saw_refresh_list_store_from_list(window);
		char title_buffer[512];
		snprintf(title_buffer, 512, "%s - Stack", uri);
		gtk_window_set_title(GTK_WINDOW(window), title_buffer);

		// Setup the default device
		saw_setup_default_device(window);
	}

	// Tidy up
	g_free(uri);
}

