#ifndef _STACKAPP_H_INCLUDED
#define _STACKAPP_H_INCLUDED

// Includes
#include <gtk/gtk.h>
#include <thread>
#include "StackCue.h"

struct StackApp
{
	GtkApplication parent;
};

struct StackAppClass
{
	GtkApplicationClass parent_class;
};

struct StackAppWindow
{
	// The parent class
	GtkApplicationWindow parent;
	
	// Our cue list
	StackCueList *cue_list;
	
	// Our builder that contains our controls
	GtkBuilder *builder;
	
	// Easy access to some of our controls
	GtkTreeView *treeview;
	GtkListStore *store;
	GtkNotebook *notebook;
	
	// The currently selected cue
	StackCue *selected_cue;
	
	// Whether to use our custom style
	bool use_custom_style;
	
	// Don't update selections for now (mostly to work round Gtk+ oddities)
	bool freeze_list_selections;
	
	// The thread which handles pulsing the cue list
	std::thread pulse_thread;
	bool kill_thread;
	
	// Timer state (0 = not started, 1 = running, 2 = stopping, 3 = stopped)
	int timer_state;
};

struct StackAppWindowClass
{
	GtkApplicationWindowClass parent_class;
};

StackApp *stack_app_new(void);
StackAppWindow* stack_app_window_new(StackApp *app);
void stack_app_window_open(StackAppWindow *window, GFile *file);
StackCue* stack_select_cue_dialog(StackAppWindow *window, StackCue *current);
void sss_show_dialog(StackAppWindow* window);

// TreeViewModel / ListStore column IDs
#define STACK_MODEL_CUEID             (0)
#define STACK_MODEL_NAME              (1)
#define STACK_MODEL_PREWAIT_PERCENT   (2)
#define STACK_MODEL_PREWAIT_TEXT      (3)
#define STACK_MODEL_ACTION_PERCENT    (4)
#define STACK_MODEL_ACTION_TEXT       (5)
#define STACK_MODEL_POSTWAIT_PERCENT  (6)
#define STACK_MODEL_POSTWAIT_TEXT     (7)
#define STACK_MODEL_STATE_IMAGE       (8)
#define STACK_MODEL_COLOR             (9)
#define STACK_MODEL_CUE_POINTER       (10)
#define STACK_MODEL_ERROR_MESSAGE     (11)

#endif


