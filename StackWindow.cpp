// Includes:
#include "StackApp.h"
#include "StackAudioCue.h"
#include "StackFadeCue.h"
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

// Callback for cue pulsing timer
static void stack_pulse_thread(StackAppWindow* window)
{
	// Set thread priority
	struct sched_param param = { 5 };
	if (pthread_setschedparam(window->pulse_thread.native_handle(), SCHED_RR, &param) != 0)
	{
		fprintf(stderr, "stack_pulse_thread(): Failed to set pulse thread priority.\n");
	}
	
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

	// Update iterator
	gtk_list_store_set(store, iter, 
		STACK_MODEL_CUEID,            cue_number,
		STACK_MODEL_NAME,             cue->name,
		STACK_MODEL_PREWAIT_PERCENT,  pre_pct,
		STACK_MODEL_PREWAIT_TEXT,     pre_buffer,
		STACK_MODEL_ACTION_PERCENT,   action_pct,
		STACK_MODEL_ACTION_TEXT,      action_buffer,
		STACK_MODEL_POSTWAIT_PERCENT, post_pct, 
		STACK_MODEL_POSTWAIT_TEXT,    post_buffer,
		STACK_MODEL_STATE_IMAGE,      icon,
		STACK_MODEL_COLOR,            col_buffer,
		STACK_MODEL_CUE_POINTER,      (gpointer)cue, -1);
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

	// Unlock the cue list
	stack_cue_list_unlock(window->cue_list);
	
	// Free the iterator
	stack_cue_list_iter_free(citer);
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

// Updates the cue properties window with information from the given 'cue'
static void saw_update_cue_properties(StackAppWindow *window, StackCue *cue)
{
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
	
	// Put on the correct tabs
	stack_cue_set_tabs(window->selected_cue, window->notebook);
}

// Selects the last cue on the list
static void saw_select_last_cue(StackAppWindow *window)
{
	GtkTreeIter iter_last;
	
	// Get the number of entries in the model
	gint entries = gtk_tree_model_iter_n_children(gtk_tree_view_get_model(window->treeview), NULL);

	// Select the last row in the model
	gtk_tree_model_iter_nth_child(gtk_tree_view_get_model(window->treeview), &iter_last, NULL, entries - 1);

	// Select that row	
	gtk_tree_selection_select_iter(gtk_tree_view_get_selection(window->treeview), &iter_last);
}

// Menu callback
static void saw_file_save_as_clicked(void* widget, gpointer user_data)
{
	fprintf(stderr, "File -> Save As clicked\n");

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
	fprintf(stderr, "File -> Save clicked\n");
	
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
	fprintf(stderr, "File -> Open clicked\n");

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
	
	// Run an Open dialog
	GtkWidget *dialog = gtk_file_chooser_dialog_new("Open Show", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
	gint response = gtk_dialog_run(GTK_DIALOG(dialog));

	// If the user chose to Open...
	if (response == GTK_RESPONSE_ACCEPT)
	{
		// Get the chosen URI
		gchar *uri = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(dialog));

		// Open the file
		StackCueList *new_cue_list = stack_cue_list_new_from_file(uri);

		if (new_cue_list != NULL)
		{
			// Kill the cue list pulsing thread
			window->kill_thread = true;
			window->pulse_thread.join();
	
			// We don't need to worry about the UI timer, as that's running on the same
			// thread as the event loop that is handling this event handler
	
			// Destroy the old cue list
			stack_cue_list_destroy(window->cue_list);
			
			// Store the new cue list
			window->cue_list = new_cue_list;
			
			// Refresh the cue list
			saw_refresh_list_store_from_list(window);
	
			// DEBUG: Open a PulseAudio device
			const StackAudioDeviceClass *sadc = stack_audio_device_get_class("StackPulseAudioDevice");
			if (sadc)
			{
				StackAudioDeviceDesc *devices;
				size_t num_outputs = sadc->get_outputs_func(&devices);

				if (devices != NULL && num_outputs > 0)
				{
					for (size_t i = 0; i < num_outputs; i++)
					{
						fprintf(stderr, "------------------------------------------------------------\n");
						fprintf(stderr, "Index: %lu\n", i);
						fprintf(stderr, "Name: %s\n", devices[i].name);
						fprintf(stderr, "Description: %s\n", devices[i].desc);
						fprintf(stderr, "Channels: %d\n", devices[i].channels);
					}
					fprintf(stderr, "------------------------------------------------------------\n");

					// Create a PulseAudio device for the first output
					StackAudioDevice *device = stack_audio_device_new("StackPulseAudioDevice", devices[0].name, devices[0].channels, 44100);
		
					// Store the audio device in the cue list
					window->cue_list->audio_device = device;
				}
		
				// Free the list of devices
				sadc->free_outputs_func(&devices, num_outputs);
			}

			// Start the cue list pulsing thread
			window->kill_thread = false;
			window->pulse_thread = std::thread(stack_pulse_thread, window);
		}
		
		// Tidy up
		g_free(uri);
	}

	gtk_widget_destroy(dialog);
}

// Menu/toolbar callback
static void saw_file_new_clicked(void* widget, gpointer user_data)
{
	fprintf(stderr, "File -> New clicked\n");

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
	
	// Kill the cue list pulsing thread
	window->kill_thread = true;
	window->pulse_thread.join();
	
	// We don't need to worry about the UI timer, as that's running on the same
	// thread as the event loop that is handling this event handler
	
	// Destroy the old cue list
	stack_cue_list_destroy(window->cue_list);

	// Initialise a new cue list, defaulting to two channels
	window->cue_list = stack_cue_list_new(2);
	
	// Refresh the cue list
	saw_refresh_list_store_from_list(window);
	
	// DEBUG: Open a PulseAudio device
	const StackAudioDeviceClass *sadc = stack_audio_device_get_class("StackPulseAudioDevice");
	if (sadc)
	{
		StackAudioDeviceDesc *devices;
		size_t num_outputs = sadc->get_outputs_func(&devices);

		if (devices != NULL && num_outputs > 0)
		{
			for (size_t i = 0; i < num_outputs; i++)
			{
				fprintf(stderr, "------------------------------------------------------------\n");
				fprintf(stderr, "Index: %lu\n", i);
				fprintf(stderr, "Name: %s\n", devices[i].name);
				fprintf(stderr, "Description: %s\n", devices[i].desc);
				fprintf(stderr, "Channels: %d\n", devices[i].channels);
			}
			fprintf(stderr, "------------------------------------------------------------\n");

			// Create a PulseAudio device for the first output
			StackAudioDevice *device = stack_audio_device_new("StackPulseAudioDevice", devices[0].name, devices[0].channels, 44100);
		
			// Store the audio device in the cue list
			window->cue_list->audio_device = device;
		}
		
		// Free the list of devices
		sadc->free_outputs_func(&devices, num_outputs);
	}

	// Start the cue list pulsing thread
	window->kill_thread = false;
	window->pulse_thread = std::thread(stack_pulse_thread, window);
}

// Menu callback
static void saw_file_quit_clicked(void* item, gpointer user_data)
{
	fprintf(stderr, "File -> Quit clicked\n");
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
	fprintf(stderr, "Edit -> Delete clicked\n");
}

// Menu callback
static void saw_cue_add_group_clicked(void* widget, gpointer user_data)
{
	fprintf(stderr, "Cue -> Add Group clicked\n");
}

// Menu callback
static void saw_cue_add_audio_clicked(void* widget, gpointer user_data)
{
	fprintf(stderr, "Cue -> Add Audio clicked\n");
	
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);
	
	// Lock the cue list
	stack_cue_list_lock(window->cue_list);

	// Create the new cue
	StackAudioCue* new_cue = STACK_AUDIO_CUE(stack_cue_new("StackAudioCue", window->cue_list));
	
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
	fprintf(stderr, "Cue -> Add Fade clicked\n");
	
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);
	
	// Lock the cue list
	stack_cue_list_lock(window->cue_list);

	// Create the new cue
	StackFadeCue* new_cue = STACK_FADE_CUE(stack_cue_new("StackFadeCue", window->cue_list));
	
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
static void saw_help_about_clicked(void* widget, gpointer user_data)
{
	// Build an about dialog
	GtkAboutDialog *about = GTK_ABOUT_DIALOG(gtk_about_dialog_new());
	gtk_about_dialog_set_program_name(about, "Stack");
	gtk_about_dialog_set_version(about, "Version 0.1.20160402-3");
	gtk_about_dialog_set_copyright(about, "Copyright (c) 2016 Clayton Peters");
	gtk_about_dialog_set_comments(about, "A GTK+ based sound cueing application for theatre");
	gtk_about_dialog_set_website(about, "https://github.com/claytonpeters/stack");
	
	// Show the dialog
	gtk_dialog_run(GTK_DIALOG(about));
	
	// Destroy the dialog on response
	gtk_widget_destroy(GTK_WIDGET(about));
}

// Menu/toolbar callback
static void saw_cue_play_clicked(void* widget, gpointer user_data)
{
	fprintf(stderr, "Play cue clicked\n");
	if (STACK_APP_WINDOW(user_data)->selected_cue != NULL)
	{
		stack_cue_list_lock(STACK_APP_WINDOW(user_data)->cue_list);
		stack_cue_play(STACK_APP_WINDOW(user_data)->selected_cue);
		stack_cue_list_unlock(STACK_APP_WINDOW(user_data)->cue_list);
		saw_update_list_store_from_cue(STACK_APP_WINDOW(user_data)->store, STACK_APP_WINDOW(user_data)->selected_cue);
	}
}

// Menu/toolbar callback
static void saw_cue_stop_clicked(void* widget, gpointer user_data)
{
	fprintf(stderr, "Stop cue clicked\n");
	if (STACK_APP_WINDOW(user_data)->selected_cue != NULL)
	{
		stack_cue_list_lock(STACK_APP_WINDOW(user_data)->cue_list);
		stack_cue_stop(STACK_APP_WINDOW(user_data)->selected_cue);
		stack_cue_list_unlock(STACK_APP_WINDOW(user_data)->cue_list);
		saw_update_list_store_from_cue(STACK_APP_WINDOW(user_data)->store, STACK_APP_WINDOW(user_data)->selected_cue);
	}
}

// Menu/toolbar callback
static void saw_cue_stop_all_clicked(void* widget, gpointer user_data)
{
	fprintf(stderr, "Stop all cues clicked\n");

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

		if (cue->state >= STACK_CUE_STATE_PLAYING_PRE && cue->state <= STACK_CUE_STATE_PLAYING_POST)
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

// Callback for UI timer
static gboolean saw_ui_timer(gpointer user_data)
{
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

		if (cue->state >= STACK_CUE_STATE_PLAYING_PRE && cue->state <= STACK_CUE_STATE_PLAYING_POST)
		{
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
	fprintf(stderr, "saw_destroy()\n");
	
	// Get the window
	StackAppWindow *window = STACK_APP_WINDOW(user_data);
	
	// Kill the pulse thread and wait for it to exit
	window->kill_thread = true;
	window->pulse_thread.join();
	
	// Clear the list store
	saw_clear_list_store(window);
	
	// Lock the cue list
	stack_cue_list_lock(window->cue_list);
	
	// Get an iterator over the cue list
	void *citer = stack_cue_list_iter_front(window->cue_list);

	// Iterate over the cue list
	while (!stack_cue_list_iter_at_end(window->cue_list, citer))
	{
		// Get the cue
		StackCue *cue = stack_cue_list_iter_get(citer);

		// Destroy the cue
		stack_cue_destroy(cue);
		
		// Iterate
		citer = stack_cue_list_iter_next(citer);
	}
	
	// Free the iterator
	stack_cue_list_iter_free(citer);

	// Unlock the cue list
	stack_cue_list_unlock(window->cue_list);
	
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

// Initialises the window
static void stack_app_window_init(StackAppWindow *window)
{
	// Object set up:
	window->selected_cue = NULL;
	window->use_custom_style = true;
	
	// Set up window signal handlers
	g_signal_connect(window, "destroy", G_CALLBACK(saw_destroy), (gpointer)window);
	g_signal_connect(window, "update-selected-cue", G_CALLBACK(saw_update_selected_cue), (gpointer)window);
	
	// Initialise this windows cue stack, defaulting to two channels
	window->cue_list = stack_cue_list_new(2);
	
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
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_add_group_clicked", G_CALLBACK(saw_cue_add_group_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_add_audio_clicked", G_CALLBACK(saw_cue_add_audio_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_add_fade_clicked", G_CALLBACK(saw_cue_add_fade_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_help_about_clicked", G_CALLBACK(saw_help_about_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_play_clicked", G_CALLBACK(saw_cue_play_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_stop_clicked", G_CALLBACK(saw_cue_stop_clicked));
	gtk_builder_add_callback_symbol(window->builder, "saw_cue_stop_all_clicked", G_CALLBACK(saw_cue_stop_all_clicked));
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

	// Get the StackAppWindow's child
	GObject* contents = gtk_builder_get_object(window->builder, "StackAppWindowContents");

	// Reparent so that this StackAppWindow instance has the 
	gtk_container_remove(GTK_CONTAINER(wintpl), GTK_WIDGET(contents));
	gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(contents));

	// Set up the window
	gtk_window_set_title(GTK_WINDOW(window), "Stack");
	gtk_window_set_default_size(GTK_WINDOW(window), 700, 500);

	// Set up accelerators
	GtkAccelGroup* ag = GTK_ACCEL_GROUP(gtk_builder_get_object(window->builder, "sawAccelGroup"));
	gtk_window_add_accel_group(GTK_WINDOW(window), ag);

	// Add on the non-stock accelerators that Glade doesn't want to make work for some reason
	//gtk_accel_group_connect(ag, ' ', (GdkModifierType)0, GTK_ACCEL_VISIBLE, g_cclosure_new(saw_cue_play_clicked, 0, 0));
	//gtk_accel_group_connect(ag, 'G', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE, g_cclosure_new(saw_cue_add_group_clicked, 0, 0));
	//gtk_accel_group_connect(ag, '1', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE, g_cclosure_new(saw_cue_add_audio_clicked, 0, 0));
	//gtk_accel_group_connect(ag, GDK_KEY_Escape, (GdkModifierType)0, GTK_ACCEL_VISIBLE, g_cclosure_new(saw_cue_stop_all_clicked, 0, 0));
	
	// Store some things in our class for easiness
	window->treeview = GTK_TREE_VIEW(gtk_builder_get_object(window->builder, "sawCuesTreeView"));
	window->store = GTK_LIST_STORE(gtk_tree_view_get_model(window->treeview));
	window->notebook = GTK_NOTEBOOK(gtk_builder_get_object(window->builder, "sawCuePropsTabs"));
	
	// Set up a timer to periodically refresh the UI
	gdk_threads_add_timeout(100, (GSourceFunc)saw_ui_timer, (gpointer)window);

	// Start the cue list pulsing thread
	window->kill_thread = false;
	window->pulse_thread = std::thread(stack_pulse_thread, window);
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
			1, cue->name,
			2, col_buffer,
			3, (gpointer)cue, -1);
			
		// Iterate
		citer = stack_cue_list_iter_next(citer);
	}
	
	// Free the iterator
	stack_cue_list_iter_free(citer);
	
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
}

// Creates a new StackAppWindow
StackAppWindow* stack_app_window_new(StackApp *app)
{
	return (StackAppWindow*)g_object_new(stack_app_window_get_type(), "application", app, NULL);
}

// Opens a StackAppWindow with a given file
void stack_app_window_open(StackAppWindow *window, GFile *file)
{
}

