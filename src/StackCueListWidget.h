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

struct StackCueListHeaderWidget;

struct StackCueListContentWidget
{
	GtkWidget super;

	// Our window
	GdkWindow *window;

	// The size of our columns (note that name fills whatever is left)
	int32_t row_height;
	int32_t header_height;
	int32_t cue_width;
	int32_t scriptref_width;
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
	bool top_cue_is_placeholder;

	// Some additional data about the cue
	SCLWCueFlagsMap cue_flags;

	// Cache some icons
	GdkPixbuf *icon_play;
	GdkPixbuf *icon_pause;
	GdkPixbuf *icon_error;
	GdkPixbuf *icon_open;
	GdkPixbuf *icon_closed;

	// Cairo objects for cached items
	cairo_t *list_cr;
	cairo_surface_t *list_surface;
	int32_t list_cache_width;
	int32_t list_cache_height;
	int32_t rendered_scroll_offset;

	// Drag/drop
	int32_t dragging;
	int32_t drag_index;
	int32_t drop_index;
	double drag_start_x;
	double drag_start_y;

	// Redraw pending optimisation
	bool redraw_pending;
};

struct StackCueListContentWidgetClass
{
	GtkWidgetClass super;
};

struct StackCueListHeaderWidget
{
	GtkWidget super;
	GdkWindow *window;
	StackCueListContentWidget *sclw;

	// Cairo objects for cached items
	cairo_t *header_cr;
	cairo_surface_t *header_surface;
	int32_t header_cache_width;
};

struct StackCueListHeaderWidgetClass
{
	GtkWidgetClass super;
};

struct StackCueListWidget
{
	GtkBox super;

	StackCueListHeaderWidget *header;
	GtkScrolledWindow *scrolled;
	StackCueListContentWidget *content;
};

struct StackCueListWidgetClass
{
	GtkBoxClass super;
};

// Relevant geometry for columns
struct SCLWColumnGeometry
{
	double cue_x;
	double scriptref_x;
	double name_x;
	double name_width;
	double post_x;
	double action_x;
	double pre_x;
};

// Define our macro for casting
#define STACK_CUE_LIST_WIDGET(obj)               G_TYPE_CHECK_INSTANCE_CAST(obj, stack_cue_list_widget_get_type(), StackCueListWidget)
#define STACK_CUE_LIST_WIDGET_CLASS(cls)         G_TYPE_CHECK_CLASS_CAST(cls, stack_cue_list_widget_get_type(), StackCueListWidgetClass)
#define IS_STACK_CUE_LIST_WIDGET(obj)            G_TYPE_CHECK_INSTANCE_TYPE(obj, stack_cue_list_widget_get_type())

#define STACK_CUE_LIST_CONTENT_WIDGET(obj)       G_TYPE_CHECK_INSTANCE_CAST(obj, stack_cue_list_content_widget_get_type(), StackCueListContentWidget)
#define STACK_CUE_LIST_CONTENT_WIDGET_CLASS(cls) G_TYPE_CHECK_CLASS_CAST(cls, stack_cue_list_content_widget_get_type(), StackCueListContentWidgetClass)
#define IS_STACK_CUE_LIST_CONTENT_WIDGET(obj)    G_TYPE_CHECK_INSTANCE_TYPE(obj, stack_cue_list_content_widget_get_type())

#define STACK_CUE_LIST_HEADER_WIDGET(obj)        G_TYPE_CHECK_INSTANCE_CAST(obj, stack_cue_list_header_widget_get_type(), StackCueListHeaderWidget)
#define STACK_CUE_LIST_HEADER_WIDGET_CLASS(cls)  G_TYPE_CHECK_CLASS_CAST(cls, stack_cue_list_header_widget_get_type(), StackCueListWHeaderidgetClass)
#define IS_STACK_CUE_LIST_HEADER_WIDGET(obj)     G_TYPE_CHECK_INSTANCE_TYPE(obj, stack_cue_list_header_widget_get_type())

// Additional functions:
GType stack_cue_list_widget_get_type();
GtkWidget *stack_cue_list_widget_new();

GType stack_cue_list_content_widget_get_type();
GtkWidget *stack_cue_list_content_widget_new();

GType stack_cue_list_header_widget_get_type();
GtkWidget *stack_cue_list_header_widget_new(StackCueListContentWidget *sclw);

// Functions:
void stack_cue_list_widget_toggle_scriptref_column(StackCueListWidget *sclw);
void stack_cue_list_content_widget_set_cue_list(StackCueListContentWidget *sclw, StackCueList *cue_list);
void stack_cue_list_content_widget_select_single_cue(StackCueListContentWidget *sclw, cue_uid_t new_uid);
void stack_cue_list_content_widget_add_to_selection(StackCueListContentWidget *sclw, cue_uid_t new_uid);
void stack_cue_list_content_widget_set_primary_selection(StackCueListContentWidget *sclw, cue_uid_t new_uid);
StackCue *stack_cue_list_content_widget_cue_from_position(StackCueListContentWidget *sclw, int32_t x, int32_t y);
void stack_cue_list_content_widget_update_cue(StackCueListContentWidget *sclw, cue_uid_t cue, int32_t fields);
void stack_cue_list_content_widget_list_modified(StackCueListContentWidget *sclw);
bool stack_cue_list_content_widget_is_cue_selected(StackCueListContentWidget *sclw, cue_uid_t uid);
bool stack_cue_list_content_widget_is_cue_expanded(StackCueListContentWidget *sclw, cue_uid_t uid);
void stack_cue_list_content_widget_toggle_selection(StackCueListContentWidget *sclw, cue_uid_t new_uid);
void stack_cue_list_content_widget_toggle_expansion(StackCueListContentWidget *sclw, cue_uid_t new_uid);
gboolean stack_cue_list_content_widget_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
void stack_cue_list_content_widget_get_geometry(StackCueListContentWidget *sclw, SCLWColumnGeometry *geom);

// Internal only:
void stack_cue_list_content_widget_recalculate_top_cue(StackCueListContentWidget *sclw);
void stack_cue_list_content_widget_update_list_cache(StackCueListContentWidget *sclw, guint width, guint height);
void stack_cue_list_header_widget_update_cache(StackCueListHeaderWidget *sclhw, guint width);

#endif
