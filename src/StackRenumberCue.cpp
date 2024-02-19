// Includes:
#include "StackApp.h"
#include <cstring>

bool src_show_dialog(StackAppWindow *window)
{
	// Build the dialog
	GtkBuilder *builder = gtk_builder_new_from_resource("/org/stack/ui/StackRenumberCue.ui");
	GtkDialog *dialog = GTK_DIALOG(gtk_builder_get_object(builder, "StackRenumberCueDialog"));
	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window));
	gtk_dialog_add_buttons(dialog, "OK", 1, "Cancel", 2, NULL);
	gtk_dialog_set_default_response(dialog, 1);

	// Connect the signals
	//gtk_builder_connect_signals(builder, (gpointer)&dialog_data);

	// Get the two entry boxes
	GtkEntry *start_entry = GTK_ENTRY(gtk_builder_get_object(builder, "srcStart"));
	GtkEntry *increment_entry = GTK_ENTRY(gtk_builder_get_object(builder, "srcIncrement"));

	// Fill in the dialog
	/*gtk_entry_set_text(start_entry, "");*/

	// This call blocks until the dialog goes away
	gint result = gtk_dialog_run(dialog);

	// If the result is OK (see gtk_dialog_add_buttons above)
	if (result == 1)
	{
		// Get the entered values
		cue_id_t start = stack_cue_string_to_id(gtk_entry_get_text(start_entry));
		cue_id_t increment = stack_cue_string_to_id(gtk_entry_get_text(increment_entry));

		// Start renumbering by iterating over the list
		cue_id_t new_cue_id = start;
		for (auto iter = window->cue_list->cues->recursive_begin(); iter != window->cue_list->cues->recursive_end(); ++iter)
		{
			StackCue *cue = *iter;
			
			// If the cue is selected
			if (stack_cue_list_widget_is_cue_selected(window->sclw, cue->uid))
			{
				// Set the new cue ID
				stack_cue_set_id(cue, new_cue_id);
				new_cue_id += increment;
			}
		}
	}

	// Destroy the dialog
	gtk_widget_destroy(GTK_WIDGET(dialog));
	g_object_unref(builder);

	return result == 1;
}
