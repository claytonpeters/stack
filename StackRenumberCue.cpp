// Includes:
#include "StackApp.h"
#include <cstring>

bool src_show_dialog(StackAppWindow *window)
{
	// Build the dialog
	GtkBuilder *builder = gtk_builder_new_from_file("StackRenumberCue.ui");
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

		// TODO: Reimplement once multiselection is working
		// Get the list of selected items
		/*GtkTreeModel *model = gtk_tree_view_get_model(window->treeview);
		GList *selected = gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(window->treeview), NULL);

		// Iterate over the selected items
		GList *ptr = selected;
		cue_id_t new_cue_id = start;
		while (ptr != NULL)
		{
			// Get the value of the cue pointer for the selected cue
			GtkTreeIter iter;
			GValue value = G_VALUE_INIT;
			GtkTreePath *path = (GtkTreePath*)ptr->data;
			gtk_tree_model_get_iter(model, &iter, path);
			gtk_tree_model_get_value(model, &iter, STACK_MODEL_CUE_POINTER, &value);

			// Set the new cue ID
			StackCue *cue = STACK_CUE(g_value_get_pointer(&value));
			stack_cue_set_id(cue, new_cue_id);

			// Iterate to next item in list
			ptr = ptr->next;
			new_cue_id += increment;
		}

		// Free the selection list
		g_list_free_full(selected, (GDestroyNotify)gtk_tree_path_free);*/
	}

	// Destroy the dialog
	gtk_widget_destroy(GTK_WIDGET(dialog));
	g_object_unref(builder);

	return result == 1;
}

