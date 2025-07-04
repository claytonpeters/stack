#ifndef _STACKAPP_H_INCLUDED
#define _STACKAPP_H_INCLUDED

// Includes
#include <gtk/gtk.h>
#include <map>
#include <string>
#include "StackCue.h"
#include "StackLevelMeter.h"
#include "StackCueListWidget.h"

// For opening on a specific tab on the Show Settings dialog
#define STACK_SETTINGS_TAB_DEFAULT -1
#define STACK_SETTINGS_TAB_SHOW    0
#define STACK_SETTINGS_TAB_AUDIO   1
#define STACK_SETTINGS_TAB_MIDI    2

struct StackApp
{
	GtkApplication parent;
};

struct StackAppClass
{
	GtkApplicationClass parent_class;
};

struct StackActiveCueWidget
{
	cue_uid_t cue_uid;
	StackCueList *cue_list;
	GtkBox *vbox;
	GtkLabel *name;
	GtkLabel *time;
	StackLevelMeter *meter;
};

typedef std::map<cue_uid_t, StackActiveCueWidget*> stack_cue_widget_map_t;

struct StackAppWindow
{
	// The parent class
	GtkApplicationWindow parent;

	// The icon for the window
	GdkPixbuf *icon;

	// Our cue list
	StackCueList *cue_list;

	// A cue list that's currently loading
	StackCueList *loading_cue_list;

	// Our cue list widget
	StackCueListWidget *sclw;

	// Our builder that contains our controls
	GtkBuilder *builder;

	// Easy access to some of our controls
	GtkNotebook *notebook;

	// The currently selected cue
	StackCue *selected_cue;

	// Whether to use our custom style
	bool use_custom_style;

	// Timer state (0 = not started, 1 = running, 2 = stopping, 3 = stopped)
	int timer_state;

	// Master out widget
	StackLevelMeter *master_out_meter;

	// Map for active cue widgets
	stack_cue_widget_map_t active_cue_widgets;

	// GtkTargetEntry for our clipboard data
	GtkTargetEntry *clipboard_target;
};

struct StackAppWindowClass
{
	GtkApplicationWindowClass parent_class;
};

StackApp *stack_app_new(void);
StackAppWindow* stack_app_window_new(StackApp *app);
void stack_app_window_open(StackAppWindow *window, GFile *file);
StackCue* stack_select_cue_dialog(StackAppWindow *window, StackCue *current, StackCue *hide);
size_t saw_get_audio_from_cuelist(size_t samples, float *buffer, void *user_data)
	__attribute__((access (write_only, 2, 1)));
void sss_show_dialog(StackAppWindow* window, StackCueList *cue_list, int tab = STACK_SETTINGS_TAB_DEFAULT);
bool src_show_dialog(StackAppWindow* window);
StackAppWindow *saw_get_window_for_cue_list(StackCueList *cue_list);
StackAppWindow *saw_get_window_for_cue(StackCue *cue);

#endif
