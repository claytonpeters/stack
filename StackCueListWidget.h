#ifndef _STACKCUELISTWIDGET_H_INCLUDED
#define _STACKCUELISTWIDGET_H_INCLUDED

// Includes:
#include <gtk/gtk.h>
#include <map>
#include "StackCue.h"

// Defines:
#define SCLW_FIELD_ICON    ((uint32_t)1)
#define SCLW_FIELD_ID      ((uint32_t)2)
#define SCLW_FIELD_NAME    ((uint32_t)4)
#define SCLW_FIELD_PRE     ((uint32_t)8)
#define SCLW_FIELD_ACTION  ((uint32_t)16)
#define SCLW_FIELD_POST    ((uint32_t)32)

#define SCLW_FLAG_SELECTED ((uint32_t)1)
#define SCLW_FLAG_EXPANDED ((uint32_t)2)

typedef std::map<cue_uid_t, uint32_t> SCLWCueFlagsMap;

struct StackCueListWidget
{
	GtkWidget super;

	// Our window
	GdkWindow *window;

	// The size of our columns (note that name fills whatever is left)
	int32_t row_height;
	int32_t cue_width;
	int32_t pre_width;
	int32_t action_width;
	int32_t post_width;

	// The cue list to draw
	StackCueList *cue_list;

	// The vertical scroll offset in pixels
	int32_t scroll_offset;

	// The highlighted cue
	cue_uid_t primary_selection;

	// The cue that is currently appearing first in the list
	cue_uid_t top_cue;

	// Some additional data about the cue
	SCLWCueFlagsMap* cue_flags;
};

struct StackCueListWidgetClass
{
	GtkWidgetClass super;
};

// Define our macro for casting
#define STACK_CUE_LIST_WIDGET(obj)       G_TYPE_CHECK_INSTANCE_CAST(obj, stack_cue_list_widget_get_type(), StackCueListWidget)
#define STACK_CUE_LIST_WIDGET_CLASS(cls) G_TYPE_CHECK_CLASS_CAST(cls, stack_cue_list_widget_get_type(), StackCueListWidgetClass)
#define IS_STACK_CUE_LIST_WIDGET(obj)    G_TYPE_CHECK_INSTANCE_TYPE(obj, stack_cue_list_widget_get_type())

// Additional functions:
GType stack_cue_list_widget_get_type();
GtkWidget *stack_cue_list_widget_new();

// Functions:
void stack_cue_list_widget_set_cue_list(StackCueListWidget *sclw, StackCueList *cue_list);
void stack_cue_list_widget_select_single(StackCueListWidget *sclw, StackCue *cue);
StackCue *stack_cue_list_widget_cue_from_position(StackCueListWidget *sclw, int32_t x, int32_t y);
void stack_cue_list_widget_update_cue(StackCueListWidget *sclw, StackCue *cue, int32_t fields);
bool stack_cue_list_widget_expand_cue(StackCueListWidget *sclw, StackCue *cue, bool expand);

// Internal only:
void stack_cue_list_widget_recalculate_top_cue(StackCueListWidget *sclw);
// Set active cue
// Set scroll
// Keypress
// Mouse action

#endif
