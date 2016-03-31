// Includes:
#include "StackApp.h"
#include "StackFadeCue.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>

// Creates a fade cue
static StackCue* stack_fade_cue_create(StackCueList *cue_list)
{
	// Debug
	fprintf(stderr, "stack_fade_cue_create() called\n");
	
	// Allocate the cue
	StackFadeCue* cue = new StackFadeCue();
	
	// Initialise the superclass
	stack_cue_init(&cue->super, cue_list);

	// Make this class a StackAudioCue
	cue->super._class_name = "StackFadeCue";
	
	// We start in error state until we have a target
	stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_ERROR);
	
	// Initialise our variables
	cue->target = STACK_CUE_UID_NONE;
	cue->target_volume = -INFINITY;
	cue->builder = NULL;
	cue->fade_tab = NULL;

	return STACK_CUE(cue);
}

// Destroys a fade cue
static void stack_fade_cue_destroy(StackCue *cue)
{
	// Debug
	fprintf(stderr, "stack_fade_cue_destroy() called\n");

	// TODO: Our tidyup here
	
	// TODO: What do we do with builder and media_tab?
	
	// Call parent destructor
	stack_cue_destroy_base(cue);
}

static void fcp_cue_changed(GtkButton *widget, gpointer user_data)
{
	// Get the cue
	StackFadeCue *cue = STACK_FADE_CUE(user_data);
	
	// Get the parent window
	StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->fade_tab));

	// Call the dialog
	StackCue *new_cue = stack_select_cue_dialog(window, stack_cue_get_by_uid(cue->target));
	
	// Store the cue in the target and update the state
	if (new_cue == NULL)
	{
		// Update the UI
		gtk_button_set_label(widget, "Select Cue...");

		cue->target = STACK_CUE_UID_NONE;
		stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_ERROR);
	}
	else
	{
		cue->target = new_cue->uid;
		stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_STOPPED);

		// Build cue number	
		char cue_number[32];
		stack_cue_id_to_string(STACK_CUE(new_cue)->id, cue_number, 32);

		// Build the string
		std::string button_text;
		button_text = std::string(cue_number) + ": " + std::string(STACK_CUE(new_cue)->name);
		
		// Update the UI
		gtk_button_set_label(widget, button_text.c_str());
	}
		
	// Fire an updated-selected-cue signal to signal the UI to change (we might
	// have changed state)
	g_signal_emit_by_name((gpointer)window, "update-selected-cue");
}

static void fcp_volume_changed(GtkRange *range, gpointer user_data)
{
	StackFadeCue *cue = STACK_FADE_CUE(user_data);
	
	// Get the volume
	double vol_db = gtk_range_get_value(range);
	
	char buffer[32];
	
	if (vol_db < -49.99)
	{
		cue->target_volume = -INFINITY;
		snprintf(buffer, 32, "-Inf dB");
	}
	else
	{
		cue->target_volume = vol_db;
		snprintf(buffer, 32, "%.2f dB", vol_db);
	}
	
	// Get the volume label and update it's value
	gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(cue->builder, "fcpVolumeValueLabel")), buffer);
}

static gboolean fcp_fade_time_changed(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	StackFadeCue *cue = STACK_FADE_CUE(user_data);
	
	// Set the time
	stack_cue_set_action_time(STACK_CUE(cue), stack_time_string_to_ns(gtk_entry_get_text(GTK_ENTRY(widget))));

	// Update the UI
	char buffer[32];
	stack_format_time_as_string(STACK_CUE(cue)->action_time, buffer, 32);
	gtk_entry_set_text(GTK_ENTRY(widget), buffer);

	// Fire an updated-selected-cue signal to signal the UI to change
	g_signal_emit_by_name((gpointer)gtk_widget_get_toplevel(widget), "update-selected-cue");
	
	return false;
}

static void fcp_fade_type_changed(GtkRadioButton *widget, gpointer user_data)
{
	StackFadeCue *cue = STACK_FADE_CUE(user_data);
	
	// Get pointers to the four radio button options
	GtkRadioButton* r1 = GTK_RADIO_BUTTON(gtk_builder_get_object(cue->builder, "fcpFadeTypeLinear"));
	GtkRadioButton* r2 = GTK_RADIO_BUTTON(gtk_builder_get_object(cue->builder, "fcpFadeTypeQuadratic"));
	GtkRadioButton* r3 = GTK_RADIO_BUTTON(gtk_builder_get_object(cue->builder, "fcpFadeTypeExponential"));
	GtkRadioButton* r4 = GTK_RADIO_BUTTON(gtk_builder_get_object(cue->builder, "fcpFadeTypeInverseExponential"));
	
	// Determine which one is toggled on
	if (widget == r1 && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r1))) { cue->fade_profile = STACK_FADE_PROFILE_LINEAR; }
	if (widget == r2 && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r2))) { cue->fade_profile = STACK_FADE_PROFILE_QUAD; }
	if (widget == r3 && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r3))) { cue->fade_profile = STACK_FADE_PROFILE_EXP; }
	if (widget == r4 && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r4))) { cue->fade_profile = STACK_FADE_PROFILE_INVEXP; }
	
	// No need to update the UI on this one (currently)
}
	
static void stack_fade_cue_set_tabs(StackCue *cue, GtkNotebook *notebook)
{
	StackFadeCue *fcue = STACK_FADE_CUE(cue);
	
	// Debug
	fprintf(stderr, "stack_fade_cue_set_tabs() called\n");

	// Create the tab
	GtkWidget *label = gtk_label_new("Fade");
	
	// Load the UI
	GtkBuilder *builder = gtk_builder_new_from_file("StackFadeCue.ui");
	fcue->builder = builder;
	fcue->fade_tab = GTK_WIDGET(gtk_builder_get_object(builder, "fcpGrid"));

	// Set up callbacks
	gtk_builder_add_callback_symbol(builder, "fcp_cue_changed", G_CALLBACK(fcp_cue_changed));
	gtk_builder_add_callback_symbol(builder, "fcp_fade_time_changed", G_CALLBACK(fcp_fade_time_changed));
	gtk_builder_add_callback_symbol(builder, "fcp_volume_changed", G_CALLBACK(fcp_volume_changed));
	gtk_builder_add_callback_symbol(builder, "fcp_fade_type_changed", G_CALLBACK(fcp_fade_type_changed));
	
	// Connect the signals
	gtk_builder_connect_signals(builder, (gpointer)cue);

	// Add an extra reference to the fade tab - we're about to remove it's
	// parent and we don't want it to get garbage collected
	g_object_ref(fcue->fade_tab);

	// The tab has a parent window in the UI file - unparent the tab container from it
	gtk_widget_unparent(fcue->fade_tab);
	
	// Append the tab (and show it, because it starts off hidden...)
	gtk_notebook_append_page(notebook, fcue->fade_tab, label);
	gtk_widget_show(fcue->fade_tab);
	
	// Set the values: cue
	StackCue *target_cue = stack_cue_get_by_uid(fcue->target);
	if (target_cue)
	{
		char cue_number[32];
		stack_cue_id_to_string(target_cue->id, cue_number, 32);

		// Build the string
		std::string button_text;
		button_text = std::string(cue_number) + ": " + std::string(target_cue->name);
	
		// Set the button text
		gtk_button_set_label(GTK_BUTTON(gtk_builder_get_object(builder, "fcpCue")), button_text.c_str());
	}
	
	// Set the values: fade time
	char buffer[32];	// Warning: used multiple times!
	stack_format_time_as_string(STACK_CUE(fcue)->action_time, buffer, 32);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(builder, "fcpFadeTime")), buffer);

	// Update fade type option
	switch (fcue->fade_profile)
	{
		case STACK_FADE_PROFILE_LINEAR:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "fcpFadeTypeLinear")), true);
			break;

		case STACK_FADE_PROFILE_QUAD:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "fcpFadeTypeQuadratic")), true);
			break;

		case STACK_FADE_PROFILE_EXP:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "fcpFadeTypeExponential")), true);
			break;

		case STACK_FADE_PROFILE_INVEXP:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "fcpFadeTypeInverseExponential")), true);
			break;
	}
	
	// Set the values: volume
	if (fcue->target_volume < -49.99)
	{
		snprintf(buffer, 32, "-Inf dB");
	}
	else
	{
		snprintf(buffer, 32, "%.2f dB", fcue->target_volume);
	}
	gtk_range_set_value(GTK_RANGE(gtk_builder_get_object(builder, "fcpVolume")), fcue->target_volume);
	gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(builder, "fcpVolumeValueLabel")), buffer);
}

// Removes the properties tabs for a fade cue
static void stack_fade_cue_unset_tabs(StackCue *cue, GtkNotebook *notebook)
{
	// Debug
	fprintf(stderr, "stack_fade_cue_unset_tabs() called\n");
	
	// Find our media page
	gint page = gtk_notebook_page_num(notebook, STACK_FADE_CUE(cue)->fade_tab);
	
	// If we've found the page, remove it
	if (page >= 0)
	{
		gtk_notebook_remove_page(notebook, page);
	}
	
	// We don't need the top level window, so destroy it (GtkBuilder doesn't
	// destroy top-level windows itself)
	gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(STACK_FADE_CUE(cue)->builder, "window1")));

	// Destroy the builder
	g_object_unref(STACK_FADE_CUE(cue)->builder);

	// Be tidy	
	STACK_FADE_CUE(cue)->builder = NULL;
	STACK_FADE_CUE(cue)->fade_tab = NULL;
}

// Registers StackFadeCue with the application
void stack_fade_cue_register()
{
	// Register built in cue types
	StackCueClass* fade_cue_class = new StackCueClass{ "StackFadeCue", "StackCue", stack_fade_cue_create, stack_fade_cue_destroy, NULL, NULL, NULL, NULL, stack_fade_cue_set_tabs, stack_fade_cue_unset_tabs };
	stack_register_cue_class(fade_cue_class);
}

