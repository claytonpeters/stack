// Includes:
#include "StackAudioFile.h"
#include "StackCueListWidget.h"
#include "StackLog.h"
#include <cmath>
#include <glib-2.0/gobject/gsignal.h>

#define RGBf(r, g, b) (float)(r) / 255.0f, (float)(g) / 255.0f, (float)(b) / 255.0f

// Provides an implementation of stack_cue_list_widget_get_type
G_DEFINE_TYPE(StackCueListWidget, stack_cue_list_widget, GTK_TYPE_WIDGET)

// Custom signals
static guint signal_selection_changed = 0;
static guint signal_primary_selection_changed = 0;

// Relevant geometry for columns
typedef struct SCLWColumnGeometry
{
	double cue_x;
	double scriptref_x;
	double name_x;
	double name_width;
	double post_x;
	double action_x;
	double pre_x;
} SCLWColumnGeometry;

typedef struct SCLWUpdateCue
{
	StackCueListWidget *sclw;
	cue_uid_t cue_uid;
	int32_t fields;
} SCLWUpdateCue;

// Pre-defs:
static void stack_cue_list_widget_update_list_cache(StackCueListWidget *sclw, guint width, guint height);
static void stack_cue_list_widget_update_row(StackCueListWidget *sclw, StackCue *cue, SCLWColumnGeometry *geom, double row_y, const int32_t fields);

void stack_cue_list_widget_reload_icons(StackCueListWidget *sclw)
{
	guint main_height = (sclw->row_height * 2) / 3;
	guint arrow_height = (sclw->row_height * 4) / 10;

	// Release old icons
	if (sclw->icon_play != NULL) { g_object_unref(sclw->icon_play); }
	if (sclw->icon_pause != NULL) { g_object_unref(sclw->icon_pause); }
	if (sclw->icon_error != NULL) { g_object_unref(sclw->icon_error); }
	if (sclw->icon_open != NULL) { g_object_unref(sclw->icon_open); }
	if (sclw->icon_closed != NULL) { g_object_unref(sclw->icon_closed); }

	// Load some icons
	GtkIconTheme *theme = gtk_icon_theme_get_default();
	sclw->icon_play = gtk_icon_theme_load_icon(theme, "media-playback-start", main_height, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
	sclw->icon_pause = gtk_icon_theme_load_icon(theme, "media-playback-pause", main_height, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
	sclw->icon_error = gtk_icon_theme_load_icon(theme, "error", main_height, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
	sclw->icon_open = gdk_pixbuf_new_from_resource_at_scale("/org/stack/icons/cuelist-open.png", arrow_height, arrow_height, false, NULL);
	sclw->icon_closed = gdk_pixbuf_new_from_resource_at_scale("/org/stack/icons/cuelist-closed.png", arrow_height, arrow_height, false, NULL);
}

GtkWidget *stack_cue_list_widget_new()
{
	// Create the new object
	GtkWidget *widget = GTK_WIDGET(g_object_new(stack_cue_list_widget_get_type(), NULL, NULL));
	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(widget);

	sclw->row_height = 24;
	sclw->header_height = 27;
	sclw->cue_width = 60;
	sclw->pre_width = 85;
	sclw->action_width = 85;
	sclw->post_width = 85;
	sclw->scriptref_width = 0;

	sclw->cue_flags = SCLWCueFlagsMap();
	stack_cue_list_widget_set_cue_list(sclw, NULL);

	sclw->header_cr = NULL;
	sclw->header_surface = NULL;
	sclw->list_cr = NULL;
	sclw->list_surface = NULL;
	sclw->header_cache_width = -1;
	sclw->list_cache_width = -1;
	sclw->list_cache_height = -1;

	// Load some icons
	sclw->icon_play = NULL;
	sclw->icon_pause = NULL;
	sclw->icon_error = NULL;
	sclw->icon_open = NULL;
	sclw->icon_closed = NULL;
	stack_cue_list_widget_reload_icons(sclw);

	// Drag drop
	sclw->dragging = 0;
	sclw->dragged_cue = STACK_CUE_UID_NONE;
	sclw->drop_index = -1;
	sclw->drag_start_x = -1;
	sclw->drag_start_y = -1;

	// Optimisation
	sclw->redraw_pending = false;

	return widget;
}

// Queues a redraw of the widget. Designed to be called from
// gdk_threads_add_idle so as to be UI-thread-safe
static gboolean stack_cue_list_widget_idle_redraw(gpointer user_data)
{
	// This is here to prevent redraws whilst we're being destroyed
	if (GTK_IS_WIDGET(user_data))
	{
		if (!STACK_CUE_LIST_WIDGET(user_data)->redraw_pending)
		{
			gtk_widget_queue_draw(GTK_WIDGET(user_data));
			STACK_CUE_LIST_WIDGET(user_data)->redraw_pending = true;
		}
	}

	return G_SOURCE_REMOVE;
}

void stack_cue_list_widget_set_cue_list(StackCueListWidget *sclw, StackCueList *cue_list)
{
	// Tidy up
	sclw->cue_list = cue_list;
	sclw->top_cue = STACK_CUE_UID_NONE;
	sclw->top_cue_is_placeholder = false;
	sclw->scroll_offset = 0;
	sclw->primary_selection = STACK_CUE_UID_NONE;

	// Update the top cue
	stack_cue_list_widget_recalculate_top_cue(sclw);

	// Redraw
	stack_cue_list_widget_update_list_cache(sclw, 0, 0);
	gdk_threads_add_idle(stack_cue_list_widget_idle_redraw, sclw);
}

void stack_cue_list_widget_set_row_height(StackCueListWidget *sclw, guint row_height)
{
	// Update the stored row height
	sclw->row_height = row_height;
	sclw->header_height = row_height + 4;

	// Reload the icons to rescale them
	stack_cue_list_widget_reload_icons(sclw);

	// Update the top cue
	stack_cue_list_widget_recalculate_top_cue(sclw);

	// Redraw the entire list
	stack_cue_list_widget_update_list_cache(sclw, 0, 0);
	gdk_threads_add_idle(stack_cue_list_widget_idle_redraw, sclw);
}

/// @brief Takes an index within the list and returns the cue at that index, or NULL if there is not a cue
/// @param sclw The cue list widget
/// @param search_index The index
/// @param placeholder_for If the entry at the index is a placeholder, return the cue it's a placeholder for
/// @return A cue if a cue was found or NULL if there was an error or if the entry at the index was a placeholder
StackCue *stack_cue_list_get_cue_at_index(StackCueListWidget *sclw, uint32_t search_index, StackCue **placeholder_for)
{
	if (sclw->cue_list == NULL)
	{
		if (placeholder_for != NULL)
		{
			*placeholder_for = NULL;
		}
		return NULL;
	}

	if (stack_cue_list_count(sclw->cue_list) == 0)
	{
		if (placeholder_for != NULL)
		{
			*placeholder_for = NULL;
		}
		return NULL;
	}

	int32_t current_cue_index = 0;

	for (auto citer = sclw->cue_list->cues->recursive_begin(); citer != sclw->cue_list->cues->recursive_end();)
	{
		StackCue *cue = *citer;

		// If we're in a child cue and the parent isn't expanded, skip it
		if (citer.is_child() && !stack_cue_list_widget_is_cue_expanded(sclw, cue->parent_cue->uid))
		{
			citer.leave_child(true);
			continue;
		}

		if (current_cue_index == search_index)
		{
			return cue;
		}

		// Check if this is an empty, expanded cue that can have children and handle the placeholder special case
		if (cue->can_have_children && stack_cue_list_widget_is_cue_expanded(sclw, cue->uid) && stack_cue_get_children(cue)->size() == 0)
		{
			++current_cue_index;
			if (current_cue_index == search_index)
			{
				if (placeholder_for != NULL)
				{
					*placeholder_for = cue;
				}
				return NULL;
			}
		}

		++citer;
		++current_cue_index;
	}

	return NULL;
}

void stack_cue_list_widget_recalculate_top_cue(StackCueListWidget *sclw)
{
	StackCue *placeholder = NULL;
	StackCue *cue = stack_cue_list_get_cue_at_index(sclw, sclw->scroll_offset / sclw->row_height, &placeholder);
	if (cue == NULL)
	{
		if (placeholder == NULL)
		{
			sclw->top_cue = STACK_CUE_UID_NONE;
			sclw->top_cue_is_placeholder = false;
		}
		else
		{
			sclw->top_cue = placeholder->uid;
			sclw->top_cue_is_placeholder = true;
		}
		return;
	}

	sclw->top_cue_is_placeholder = false;
	sclw->top_cue = cue->uid;

	return;
}

/// @brief Returns the total number of items in the queue list that would be
/// visible if the widget was infinitely long
/// @param sclw The cue list widget
/// @return The number of cues
int32_t stack_cue_list_widget_get_visible_count(StackCueListWidget *sclw)
{
	if (sclw->cue_list == NULL)
	{
		return -1;
	}

	size_t count = stack_cue_list_count(sclw->cue_list);

	// We don't use the recursive iterator here as this is easier
	for (auto cue : *sclw->cue_list->cues)
	{
		if (cue->can_have_children)
		{
			StackCueStdList *children = stack_cue_get_children(cue);

			// Determine if the queue is expanded
			if (stack_cue_list_widget_is_cue_expanded(sclw, cue->uid) && children != NULL)
			{
				if (children->size() == 0)
				{
					// Cue is expanded but empty, count the placeholder
					count++;
				}
				else
				{
					// Cue is expanded, add on the count of its subcues
					count += children->size();
				}
			}
		}
	}

	return count;
}

/// @brief Returns the height in pixels required to draw every visible cue in hte list
/// @param sclw The cue list widget
/// @return The height in pixels
int32_t stack_cue_list_widget_get_list_height(StackCueListWidget *sclw)
{
	return stack_cue_list_widget_get_visible_count(sclw) * sclw->row_height;
}

int32_t stack_cue_list_widget_get_cue_y(StackCueListWidget *sclw, cue_uid_t cue_uid)
{
	if (sclw->cue_list == NULL)
	{
		return -1;
	}

	size_t index = 0;

	for (auto citer = sclw->cue_list->cues->recursive_begin(); citer != sclw->cue_list->cues->recursive_end(); )
	{
		StackCue *cue = *citer;

		if (cue->uid == cue_uid)
		{
			return index * sclw->row_height;
		}

		if (citer.is_child())
		{
			if (!stack_cue_list_widget_is_cue_expanded(sclw, cue->parent_cue->uid))
			{
				// Increment to next main list cue
				citer.leave_child(true);
				continue;
			}
		}
		else
		{
			// Leave a placeholder space for empty, expanded cues that can have children
			if (cue->can_have_children && stack_cue_list_widget_is_cue_expanded(sclw, cue->uid) && stack_cue_get_children(cue)->size() == 0)
			{
				index++;
			}
		}

		// Increment to the next cue
		index++;
		++citer;
	}

	return -1;
}

bool stack_cue_list_widget_ensure_cue_visible(StackCueListWidget *sclw, cue_uid_t cue)
{
	bool redraw = false;

	if (sclw->cue_list == NULL || cue == STACK_CUE_UID_NONE)
	{
		return false;
	}

	// Calculate the Y location of the cue in the list
	int32_t cue_y = stack_cue_list_widget_get_cue_y(sclw, cue);
	if (cue_y == -1)
	{
		// Cue wasn't visible in the list (probably a child cue inside a closed parent)
		return false;
	}

	// Get the height of the list area
	guint height = gtk_widget_get_allocated_height(GTK_WIDGET(sclw)) - sclw->header_height;

	// If the top of the cue is off the top
	if (cue_y - sclw->scroll_offset < 0)
	{
		sclw->scroll_offset = cue_y;
		redraw = true;
	}
	// If the bottom of the cue if off the bottom
	else if (cue_y + sclw->row_height - sclw->scroll_offset > height)
	{
		sclw->scroll_offset = cue_y - height + sclw->row_height;
		redraw = true;
	}

	if (redraw)
	{
		// We've scrolled so we need to recalculate which cue is at the top of the list
		stack_cue_list_widget_recalculate_top_cue(sclw);

		// Redraw the entire list
		stack_cue_list_widget_update_list_cache(sclw, 0, 0);
		gdk_threads_add_idle(stack_cue_list_widget_idle_redraw, sclw);
	}

	return redraw;
}

bool stack_cue_list_widget_is_cue_visible(StackCueListWidget *sclw, cue_uid_t cue)
{
	if (sclw->cue_list == NULL || cue == STACK_CUE_UID_NONE)
	{
		return false;
	}

	// Ensure the cue is in the cue list (at early cue creation time, there's
	// a possibility it isn't!)
	bool found = false;
	for (auto citer = sclw->cue_list->cues->recursive_begin(); citer != sclw->cue_list->cues->recursive_end(); citer++)
	{
		if ((*citer)->uid == cue)
		{
			found = true;
			break;
		}
	}
	if (!found)
	{
		return false;
	}

	// Calculate the Y location of the cue in the list
	int32_t cue_y = stack_cue_list_widget_get_cue_y(sclw, cue);
	if (cue_y == -1)
	{
		// Can't be visible if we don't have a Y component
		return false;
	}

	// Get the height of the list area
	guint height = gtk_widget_get_allocated_height(GTK_WIDGET(sclw)) - sclw->header_height;

	// If the bottom of the cue is off the top, or the top of the cue is off the bottom
	if ((cue_y + sclw->row_height - sclw->scroll_offset < 0) || (cue_y - sclw->scroll_offset > (int32_t)height))
	{
		return false;
	}

	return true;
}

int32_t stack_cue_list_widget_get_index_at_point(StackCueListWidget *sclw, int32_t x, int32_t y)
{
	// If we don't have a cue list, or the point is in the header
	if (sclw->cue_list == NULL || y < sclw->header_height)
	{
		return -1;
	}

	// For now (with no expanded cues this is trivial)
	return (y - sclw->header_height + sclw->scroll_offset) / sclw->row_height;
}

cue_uid_t stack_cue_list_widget_get_cue_at_point(StackCueListWidget *sclw, int32_t x, int32_t y)
{
	int32_t row_y = -sclw->scroll_offset + sclw->header_height;

	for (auto citer = sclw->cue_list->cues->recursive_begin(); citer != sclw->cue_list->cues->recursive_end(); )
	{
		StackCue *cue = *citer;

		// Skip past child cues of unexpanded parents
		if (citer.is_child())
		{
			if (!stack_cue_list_widget_is_cue_expanded(sclw, cue->parent_cue->uid))
			{
				// Increment to next main list cue
				citer.leave_child(true);
				continue;
			}
		}

		if (y >= row_y && y < row_y + sclw->row_height)
		{
			return cue->uid;
		}

		// Leave space for placeholders
		if (cue->can_have_children && stack_cue_list_widget_is_cue_expanded(sclw, cue->uid) && stack_cue_get_children(cue)->size() == 0)
		{
			row_y += sclw->row_height;
		}

		// Increment to the next cue
		row_y += sclw->row_height;
		++citer;
	}

	// Didn't find anything
	return STACK_CUE_UID_NONE;
}

static gboolean stack_cue_list_widget_idle_update_cue(gpointer user_data)
{
	SCLWUpdateCue *uc = (SCLWUpdateCue*)user_data;
	StackCue *cue = stack_cue_get_by_uid(uc->cue_uid);
	stack_cue_list_widget_update_row(uc->sclw, cue, NULL, -1, uc->fields);
	if (!uc->sclw->redraw_pending)
	{
		gtk_widget_queue_draw(GTK_WIDGET(uc->sclw));
		uc->sclw->redraw_pending = true;
	}
	delete uc;

	return G_SOURCE_REMOVE;
}

void stack_cue_list_widget_update_cue(StackCueListWidget *sclw, cue_uid_t cue_uid, int32_t fields)
{
	if (stack_cue_list_widget_is_cue_visible(sclw, cue_uid))
	{
		// This must be done on the UI thread or we get... issues
		SCLWUpdateCue *uc = new SCLWUpdateCue;
		uc->sclw = sclw;
		uc->cue_uid = cue_uid;
		uc->fields = fields;
		gdk_threads_add_idle(stack_cue_list_widget_idle_update_cue, uc);
	}
}

static void stack_cue_list_widget_render_text(StackCueListWidget *sclw, cairo_t *cr, double x, double y, double width, double height, const char *text, bool align_center, bool bold, GtkStyleContext *style_context)
{
	PangoContext *pc = gtk_widget_get_pango_context(GTK_WIDGET(sclw));
	PangoFontDescription *fd = pango_context_get_font_description(pc);
	pango_font_description_set_weight(fd, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
	PangoLayout *pl = gtk_widget_create_pango_layout(GTK_WIDGET(sclw), text);
	pango_layout_set_width(pl, pango_units_from_double(width));
	pango_layout_set_alignment(pl, align_center ? PANGO_ALIGN_CENTER : PANGO_ALIGN_LEFT);
	pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_END);

	// Get the height of the text so we can vertically center
	int text_height;
	pango_layout_get_size(pl, NULL, &text_height);

	// Render text
	gtk_render_layout(style_context, cr, x, y + (height - pango_units_to_double(text_height)) / 2 - 1, pl);

	// Tidy up
	g_object_unref(pl);
}

static void stack_cue_list_widget_render_header(StackCueListWidget *sclw, cairo_t *cr, double x, double width, const char *text, bool align_center)
{
	GtkWidgetPath *path = gtk_widget_path_new();
	gtk_widget_path_append_type(path, G_TYPE_NONE);
	gtk_widget_path_iter_set_object_name(path, -1, "treeview");
	gtk_widget_path_iter_add_class(path, -1, "view");
	gtk_widget_path_append_type(path, G_TYPE_NONE);
	gtk_widget_path_iter_set_object_name(path, -1, "header");
	gtk_widget_path_append_type(path, G_TYPE_NONE);
	gtk_widget_path_iter_set_object_name(path, -1, "button");
	gtk_widget_path_iter_set_state(path, -1, GTK_STATE_FLAG_NORMAL);
	GtkStyleContext *sc = gtk_style_context_new();
	gtk_style_context_set_path(sc, path);
	gtk_style_context_set_parent(sc, NULL);
	gtk_style_context_set_state(sc, gtk_widget_path_iter_get_state(path, -1));

	// Draw background
	gtk_render_background(sc, cr, x, 0, width, sclw->header_height);
	gtk_render_frame(sc, cr, x, 0, width, sclw->header_height);

	// Render text
	stack_cue_list_widget_render_text(sclw, cr, x + (align_center ? 0 : 8), 0, width, sclw->header_height, text, align_center, true, sc);

	// Tidy up
	gtk_widget_path_unref(path);
	g_object_unref(sc);
}

static void stack_cue_list_widget_render_time(StackCueListWidget *sclw, cairo_t *cr, double x, double y, double width, const char *text, double pct, GtkStyleContext *style_context)
{
	if (pct > 0.0)
	{
		// Set up pattern for time background
		cairo_pattern_t *back = cairo_pattern_create_linear(0, y, 0, y + sclw->row_height);
		cairo_pattern_add_color_stop_rgb(back, 0.0, 0.0, 0.5, 0.0);
		cairo_pattern_add_color_stop_rgb(back, 1.0, 0.0, 0.375, 0.0);

		// Draw background
		cairo_set_source(cr, back);
		cairo_rectangle(cr, x + 2, y + 2, (width - 4) * pct, sclw->row_height - 4);
		cairo_fill_preserve(cr);

		// Draw edge
		cairo_set_source_rgb(cr, 0.1875, 0.1875, 0.25);
		cairo_stroke(cr);

		// Tidy up
		cairo_pattern_destroy(back);
	}

	// Render text on top
	stack_cue_list_widget_render_text(sclw, cr, x, y, width, sclw->row_height, text, true, false, style_context);
}

static void stack_cue_list_widget_get_geometry(StackCueListWidget *sclw, SCLWColumnGeometry *geom)
{
	// Get width
	guint width = gtk_widget_get_allocated_width(GTK_WIDGET(sclw));

	// Caluclate positions and some widths
	geom->cue_x = (double)sclw->row_height;  // First column is as wide as it is tall
	geom->scriptref_x = geom->cue_x + (double)sclw->cue_width;
	geom->name_x = geom->scriptref_x + (double)sclw->scriptref_width;
	geom->post_x = (double)width - (double)sclw->post_width;
	geom->action_x = geom->post_x - (double)sclw->action_width;
	geom->pre_x = geom->action_x - (double)sclw->pre_width;
	geom->name_width = geom->pre_x - geom->name_x;
}

static void stack_cue_list_widget_update_header_cache(StackCueListWidget *sclw, guint width)
{
	// Tidy up existing objects
	if (sclw->header_surface != NULL)
	{
		cairo_surface_destroy(sclw->header_surface);
	}
	if (sclw->header_cr != NULL)
	{
		cairo_destroy(sclw->header_cr);
	}

	// Create new Cairo objects
	sclw->header_surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, sclw->header_height);
	sclw->header_cr = cairo_create(sclw->header_surface);
	sclw->header_cache_width = width;

	// Set up for text
	cairo_set_antialias(sclw->header_cr, CAIRO_ANTIALIAS_DEFAULT);
	cairo_set_font_size(sclw->header_cr, 14.0);
	cairo_set_line_width(sclw->header_cr, 1.0);

	// Get some geometry
	SCLWColumnGeometry geom;
	stack_cue_list_widget_get_geometry(sclw, &geom);

	// Render header
	stack_cue_list_widget_render_header(sclw, sclw->header_cr, 0, geom.cue_x, "", false);
	stack_cue_list_widget_render_header(sclw, sclw->header_cr, geom.cue_x, sclw->cue_width, "Cue", true);
	if (sclw->scriptref_width > 0)
	{
		stack_cue_list_widget_render_header(sclw, sclw->header_cr, geom.scriptref_x, sclw->scriptref_width, "Script Ref.", true);
	}
	stack_cue_list_widget_render_header(sclw, sclw->header_cr, geom.name_x, geom.name_width, "Name", false);
	stack_cue_list_widget_render_header(sclw, sclw->header_cr, geom.pre_x, sclw->pre_width, "Pre-wait", true);
	stack_cue_list_widget_render_header(sclw, sclw->header_cr, geom.action_x, sclw->action_width, "Action", true);
	stack_cue_list_widget_render_header(sclw, sclw->header_cr, geom.post_x, sclw->post_width, "Post-wait", true);
}

void stack_cue_list_widget_list_modified(StackCueListWidget *sclw)
{
	// Check that the currently selected cue is still valid
	StackCue *cue = stack_cue_get_by_uid(sclw->primary_selection);
	if (cue == NULL)
	{
		sclw->primary_selection = STACK_CUE_UID_NONE;
	}

	// Re-calculate which cue is at the top as it could have changed
	stack_cue_list_widget_recalculate_top_cue(sclw);

	// Force a full redraw
	stack_cue_list_widget_update_list_cache(sclw, 0, 0);
	gdk_threads_add_idle(stack_cue_list_widget_idle_redraw, sclw);
}

static void stack_cue_list_widget_render_placeholder(StackCueListWidget *sclw, SCLWColumnGeometry *geom, double row_y)
{
	if (sclw == NULL)
	{
		return;
	}

	if (sclw->list_surface == NULL || sclw->list_cr == NULL)
	{
		return;
	}

	// Get column geometry if not given
	SCLWColumnGeometry local_geom;
	if (geom == NULL)
	{
		geom = &local_geom;
		stack_cue_list_widget_get_geometry(sclw, geom);
	}

	// Set the clip region to our rectangle
	cairo_rectangle(sclw->list_cr, 0, row_y, (double)sclw->list_cache_width, sclw->row_height);
	cairo_clip(sclw->list_cr);

	// Fill the background
	GtkStyleContext *style_context = gtk_style_context_new();
	GtkWidgetPath *path = gtk_widget_path_new();
	gtk_widget_path_append_type(path, G_TYPE_NONE);
	gtk_widget_path_iter_set_object_name(path, -1, "treeview");
	gtk_widget_path_iter_add_class(path, -1, "view");
	gtk_widget_path_iter_set_state(path, -1, GTK_STATE_FLAG_NORMAL);
	gtk_style_context_set_state(style_context, gtk_widget_path_iter_get_state(path, -1));
	gtk_style_context_set_path(style_context, path);
	gtk_render_background(style_context, sclw->list_cr, 0, row_y, sclw->list_cache_width, sclw->row_height);

	// Render the placeholder text
	stack_cue_list_widget_render_text(sclw, sclw->list_cr, geom->name_x + (sclw->row_height * 2 - 6), row_y, geom->name_width - (sclw->row_height * 2 - 6), sclw->row_height, "Drag-and-drop cues into this cue", false, true, style_context);

	// Reset any clip region that we might have set
	cairo_reset_clip(sclw->list_cr);

	// Tidy up
	gtk_widget_path_unref(path);
	g_object_unref(style_context);
}

static void stack_cue_list_widget_update_row(StackCueListWidget *sclw, StackCue *cue, SCLWColumnGeometry *geom, double row_y, const int32_t fields)
{
	char buffer[32];

	if (sclw == NULL || cue == NULL)
	{
		return;
	}

	// If we don't currently have a cache, just render the entire thing
	if (sclw->list_surface == NULL || sclw->list_cr == NULL)
	{
		stack_cue_list_widget_update_list_cache(sclw, 0, 0);
		return;
	}

	// We shouldn't be drawing cues that are children of unexpanded cues
	if (cue->parent_cue != NULL && !stack_cue_list_widget_is_cue_expanded(sclw, cue->parent_cue->uid))
	{
		return;
	}

	if (row_y < 0)
	{
		int32_t calc_row_y = stack_cue_list_widget_get_cue_y(sclw, cue->uid);
		if (calc_row_y == -1)
		{
			return;
		}
		row_y = calc_row_y - sclw->scroll_offset;
	}

	// Stop drawing if we're off the top or bottom of the view
	if (row_y + sclw->row_height < 0 || row_y >= (double)sclw->list_cache_height)
	{
		return;
	}

	// Preparre to fill the background
	GtkStyleContext *style_context = gtk_style_context_new();
	GtkWidgetPath *path = gtk_widget_path_new();
	gtk_widget_path_append_type(path, G_TYPE_NONE);
	gtk_widget_path_iter_set_object_name(path, -1, "treeview");
	gtk_widget_path_iter_add_class(path, -1, "view");

	// Get column geometry if not given
	SCLWColumnGeometry local_geom;
	if (geom == NULL)
	{
		geom = &local_geom;
		stack_cue_list_widget_get_geometry(sclw, geom);
	}

	// Determine field geometry
	double rectangle_x = 0.0, rectangle_width = (double)sclw->list_cache_width;
	switch (fields)
	{
		case 1:
			rectangle_width = local_geom.cue_x;
			break;
		case 2:
			rectangle_x = local_geom.cue_x;
			rectangle_width = local_geom.name_x - local_geom.cue_x;
			break;
		case 3:
			rectangle_x = local_geom.name_x;
			rectangle_width = local_geom.name_width;
			break;
		case 4:
			rectangle_x = local_geom.pre_x;
			rectangle_width = sclw->list_cache_width - local_geom.pre_x;
			break;
		case 5:
			rectangle_x = local_geom.scriptref_x;
			rectangle_width = sclw->scriptref_width;
			break;
	}

	// Set the clip region to our rectangle
	cairo_rectangle(sclw->list_cr, rectangle_x, row_y, rectangle_width, sclw->row_height);
	cairo_clip(sclw->list_cr);

	// Highlight selected row
	auto flags_iter = sclw->cue_flags.find(cue->uid);
	bool selected = false, expanded = false;
	if (flags_iter != sclw->cue_flags.end())
	{
		selected = (flags_iter->second & SCLW_FLAG_SELECTED);
		expanded = (flags_iter->second & SCLW_FLAG_EXPANDED);
	}

	if (selected)
	{
		gtk_widget_path_iter_set_state(path, -1, GTK_STATE_FLAG_SELECTED);
		gtk_style_context_set_state(style_context, gtk_widget_path_iter_get_state(path, -1));
		gtk_style_context_set_path(style_context, path);
		gtk_render_background(style_context, sclw->list_cr, rectangle_x, row_y, rectangle_width, sclw->row_height);
	}
	else
	{
		// Get background colour
		uint8_t r = 0xdd, g = 0xdd, b = 0xdd;
		stack_property_get_uint8(stack_cue_get_property(cue, "r"), STACK_PROPERTY_VERSION_DEFINED, &r);
		stack_property_get_uint8(stack_cue_get_property(cue, "g"), STACK_PROPERTY_VERSION_DEFINED, &g);
		stack_property_get_uint8(stack_cue_get_property(cue, "b"), STACK_PROPERTY_VERSION_DEFINED, &b);

		// If all zero, don't render a background
		if (r + g + b > 0)
		{
			cairo_set_source_rgb(sclw->list_cr, RGBf(r, g, b));
			cairo_paint(sclw->list_cr);
		}
		else
		{
			gtk_widget_path_iter_set_state(path, -1, GTK_STATE_FLAG_NORMAL);
			gtk_style_context_set_state(style_context, gtk_widget_path_iter_get_state(path, -1));
			gtk_style_context_set_path(style_context, path);
			gtk_render_background(style_context, sclw->list_cr, rectangle_x, row_y, rectangle_width, sclw->row_height);
		}
	}

	if (cue->uid == sclw->primary_selection)
	{
		gtk_widget_path_iter_set_state(path, -1, GTK_STATE_FLAG_FOCUSED);
		gtk_style_context_set_state(style_context, gtk_widget_path_iter_get_state(path, -1));
		gtk_style_context_set_path(style_context, path);
		gtk_render_focus(style_context, sclw->list_cr, 0, row_y, sclw->list_cache_width, sclw->row_height);
	}

	if (fields == 0 || fields == 1)
	{
		// Decide on an icon to draw
		GdkPixbuf *pixbuf;
		bool free_pixbuf = false;
		switch (cue->state)
		{
			case STACK_CUE_STATE_ERROR:
				pixbuf = sclw->icon_error;
				break;
			case STACK_CUE_STATE_PAUSED:
				pixbuf = sclw->icon_pause;
				break;
			case STACK_CUE_STATE_PLAYING_PRE:
			case STACK_CUE_STATE_PLAYING_ACTION:
			case STACK_CUE_STATE_PLAYING_POST:
				pixbuf = sclw->icon_play;
				break;
			default:
				pixbuf = NULL;
				if (stack_cue_get_icon(cue) != NULL)
				{
					pixbuf = gdk_pixbuf_scale_simple(stack_cue_get_icon(cue), sclw->row_height - 8, sclw->row_height - 8, GDK_INTERP_BILINEAR);
					free_pixbuf = true;
				}
		}

		// Draw an icon if we have one
		if (pixbuf != NULL)
		{
			gtk_render_icon(style_context, sclw->list_cr, pixbuf, 4, row_y + (sclw->row_height - 16) / 2);
		}

		if (free_pixbuf)
		{
			g_object_unref(pixbuf);
		}
	}


	// Render the cue number
	if (fields == 0 || fields == 2)
	{
		// Cue ID 0 is a placeholder for "unset"
		if (cue->id != 0)
		{
			stack_cue_id_to_string(cue->id, buffer, 32);
			stack_cue_list_widget_render_text(sclw, sclw->list_cr, geom->cue_x, row_y, sclw->cue_width, sclw->row_height, buffer, true, false, style_context);
		}
	}

	if ((fields == 0 || fields == 5) && sclw->scriptref_width > 0)
	{
		char *scriptref_text = NULL;
		stack_property_get_string(stack_cue_get_property(cue, "script_ref"), STACK_PROPERTY_VERSION_DEFINED, &scriptref_text);
		if (scriptref_text != NULL)
		{
			stack_cue_list_widget_render_text(sclw, sclw->list_cr, geom->scriptref_x, row_y, sclw->scriptref_width, sclw->row_height, scriptref_text, true, false, style_context);
		}
	}

	// Render the cue name
	if (fields == 0 || fields == 3)
	{
		int text_x_offset = 0;
		if (cue->can_have_children)
		{
			text_x_offset = sclw->row_height;
			gtk_render_icon(style_context, sclw->list_cr, expanded ? sclw->icon_open : sclw->icon_closed, geom->name_x + (sclw->row_height - 9) / 2, row_y + (sclw->row_height - 9) / 2);
		}
		else if (cue->parent_cue != NULL)
		{
			text_x_offset = sclw->row_height * 2 - 6;
		}
		stack_cue_list_widget_render_text(sclw, sclw->list_cr, geom->name_x + text_x_offset, row_y, geom->name_width - text_x_offset, sclw->row_height, stack_cue_get_rendered_name(cue), false, false, style_context);
	}

	// Render the cue times
	if (fields == 0 || fields == 4)
	{
		char pre_buffer[32], action_buffer[32], post_buffer[32], col_buffer[8];
		double pre_pct = 0.0, action_pct = 0.0, post_pct = 0.0;
		stack_time_t cue_pre_time = 0, cue_action_time = 0, cue_post_time = 0;

		// Format time strings
		if (cue->state == STACK_CUE_STATE_ERROR || cue->state == STACK_CUE_STATE_PREPARED || cue->state == STACK_CUE_STATE_STOPPED)
		{
			// Get the _defined_ version of these properties
			stack_property_get_int64(stack_cue_get_property(cue, "pre_time"), STACK_PROPERTY_VERSION_DEFINED, &cue_pre_time);
			stack_property_get_int64(stack_cue_get_property(cue, "action_time"), STACK_PROPERTY_VERSION_DEFINED, &cue_action_time);
			stack_property_get_int64(stack_cue_get_property(cue, "post_time"), STACK_PROPERTY_VERSION_DEFINED, &cue_post_time);

			// If cue is stopped, display their total times
			stack_format_time_as_string(cue_pre_time, pre_buffer, 32);
			stack_format_time_as_string(cue_action_time, action_buffer, 32);
			stack_format_time_as_string(cue_post_time, post_buffer, 32);
		}
		else
		{
			// Get the _live_ version of these properties
			stack_property_get_int64(stack_cue_get_property(cue, "pre_time"), STACK_PROPERTY_VERSION_LIVE, &cue_pre_time);
			stack_property_get_int64(stack_cue_get_property(cue, "action_time"), STACK_PROPERTY_VERSION_LIVE, &cue_action_time);
			stack_property_get_int64(stack_cue_get_property(cue, "post_time"), STACK_PROPERTY_VERSION_LIVE, &cue_post_time);

			// If cue is running or paused, display the time left
			stack_time_t rpre, raction, rpost;
			stack_cue_get_running_times(cue, stack_get_clock_time(), &rpre, &raction, &rpost, NULL, NULL, NULL);

			// Format the times
			stack_format_time_as_string(cue_pre_time - rpre, pre_buffer, 32);
			if (cue_action_time < 0)
			{
				// Cue never stops
				stack_format_time_as_string(raction, action_buffer, 32);
			}
			else
			{
				stack_format_time_as_string(cue_action_time - raction, action_buffer, 32);
			}
			stack_format_time_as_string(cue_post_time - rpost, post_buffer, 32);

			// Calculate fractions
			if (cue_pre_time > 0)
			{
				pre_pct = double(rpre) / double(cue_pre_time);
			}
			if (cue_action_time > 0)
			{
				action_pct = double(raction) / double(cue_action_time);
			}
			if (cue_post_time > 0)
			{
				post_pct = double(rpost) / double(cue_post_time);
			}
		}

		stack_cue_list_widget_render_time(sclw, sclw->list_cr, geom->pre_x, row_y, sclw->pre_width, pre_buffer, pre_pct, style_context);
		stack_cue_list_widget_render_time(sclw, sclw->list_cr, geom->action_x, row_y, sclw->action_width, action_buffer, action_pct, style_context);
		stack_cue_list_widget_render_time(sclw, sclw->list_cr, geom->post_x, row_y, sclw->post_width, post_buffer, post_pct, style_context);
	}

	// Reset any clip region that we might have set
	cairo_reset_clip(sclw->list_cr);

	// Tidy up
	gtk_widget_path_unref(path);
	g_object_unref(style_context);
}

static void stack_cue_list_widget_update_list_cache(StackCueListWidget *sclw, guint width, guint height)
{
	// Tidy up existing objects
	if (sclw->list_surface != NULL)
	{
		cairo_surface_destroy(sclw->list_surface);
	}
	if (sclw->list_cr != NULL)
	{
		cairo_destroy(sclw->list_cr);
	}

	// Get size if not given
	if (width == 0)
	{
		width = gtk_widget_get_allocated_width(GTK_WIDGET(sclw));
	}
	if (height == 0)
	{
		height = gtk_widget_get_allocated_height(GTK_WIDGET(sclw)) - sclw->header_height;
	}

	// Create new Cairo objects
	sclw->list_surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
	sclw->list_cr = cairo_create(sclw->list_surface);
	sclw->list_cache_width = width;
	sclw->list_cache_height = height;

	// Set up for text
	cairo_set_antialias(sclw->list_cr, CAIRO_ANTIALIAS_DEFAULT);
	cairo_set_font_size(sclw->list_cr, 14.0);
	cairo_set_line_width(sclw->list_cr, 1.0);

	// Get some geometry
	SCLWColumnGeometry geom;
	stack_cue_list_widget_get_geometry(sclw, &geom);

	// Fill the background
	GtkStyleContext *style_context = gtk_style_context_new();
	GtkWidgetPath *path = gtk_widget_path_new();
	gtk_widget_path_append_type(path, G_TYPE_NONE);
	gtk_widget_path_iter_set_object_name(path, -1, "treeview");
	gtk_widget_path_iter_add_class(path, -1, "view");
	gtk_widget_path_iter_set_state(path, -1, GTK_STATE_FLAG_NORMAL);
	gtk_style_context_set_state(style_context, gtk_widget_path_iter_get_state(path, -1));
	gtk_style_context_set_path(style_context, path);
	gtk_render_background(style_context, sclw->list_cr, 0, 0, width, height);
	gtk_widget_path_unref(path);
	g_object_unref(style_context);

	// If we don't have a cue list then return
	if (sclw->cue_list == NULL)
	{
		return;
	}

	// Lock the cue list
	stack_cue_list_lock(sclw->cue_list);

	// If we don't have a top cue currently, work it out
	if (sclw->top_cue == STACK_CUE_UID_NONE)
	{
		stack_cue_list_widget_recalculate_top_cue(sclw);
	}

	// Get an iterator over the cue list
	size_t count = 0;
	double row_y = (double)-(sclw->scroll_offset);
	bool found_first = false;

	for (auto citer = sclw->cue_list->cues->recursive_begin(); citer != sclw->cue_list->cues->recursive_end(); )
	{
		StackCue *cue = *citer;

		if (citer.is_child())
		{
			if (!stack_cue_list_widget_is_cue_expanded(sclw, cue->parent_cue->uid))
			{
				// Increment to next main list cue
				citer.leave_child(true);
				continue;
			}
		}

		if (!found_first && cue->uid == sclw->top_cue)
		{
			found_first = true;
		}

		if (found_first)
		{
			stack_cue_list_widget_update_row(sclw, cue, &geom, row_y, 0);
			if (cue->can_have_children && stack_cue_list_widget_is_cue_expanded(sclw, cue->uid) && stack_cue_get_children(cue)->size() == 0)
			{
				// Draw the placeholder and increment Y to account for its space
				row_y += (double)sclw->row_height;
				stack_cue_list_widget_render_placeholder(sclw, &geom, row_y);
			}
		}

		if (!found_first && cue->can_have_children && stack_cue_list_widget_is_cue_expanded(sclw, cue->uid) && stack_cue_get_children(cue)->size() == 0)
		{
			// Leave a gap for placeholder
			row_y += (double)sclw->row_height;
		}

		// Increment to the next cue
		++citer;
		row_y += (double)sclw->row_height;
	}

	// Unlock the cue list
	stack_cue_list_unlock(sclw->cue_list);
}

static gboolean stack_cue_list_widget_draw(GtkWidget *widget, cairo_t *cr)
{
	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(widget);
	sclw->redraw_pending = false;

	// Get details
	guint width = gtk_widget_get_allocated_width(widget);
	guint height = gtk_widget_get_allocated_height(widget);

	// Set up
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
	cairo_set_line_width(cr, 1.0);

	// TODO: Check font height and reset row_height
	PangoContext *pc = gtk_widget_get_pango_context(GTK_WIDGET(sclw));
	PangoFontDescription *fd = pango_context_get_font_description(pc);
	gint text_size = pango_font_description_get_size(fd);

	if (sclw->row_height != text_size / PANGO_SCALE + 13)
	{
		stack_log("Changing row height from %d to %d\n", sclw->row_height, (text_size / PANGO_SCALE) + 13);
		stack_cue_list_widget_set_row_height(sclw, (text_size / PANGO_SCALE) + 13);
	}

	// Update header cache if necessary
	if (sclw->header_surface == NULL || sclw->header_cache_width != width)
	{
		stack_cue_list_widget_update_header_cache(sclw, width);
	}

	// Render the header
	cairo_set_source_surface(cr, sclw->header_surface, 0, 0);
	cairo_rectangle(cr, 0.0, 0.0, width, sclw->header_height);
	cairo_fill(cr);

	// Update list cache if necessary
	if (sclw->list_surface == NULL || sclw->list_cache_width != width || sclw->list_cache_height != height - sclw->header_height)
	{
		stack_cue_list_widget_update_list_cache(sclw, width, height - sclw->header_height);
	}

	// Render the list
	cairo_set_source_surface(cr, sclw->list_surface, 0, sclw->header_height);
	cairo_rectangle(cr, 0.0, sclw->header_height, width, height - sclw->header_height);
	cairo_fill(cr);

	// Draw drag overlay if necessary
	if (sclw->dragging == 2)
	{
		double drop_y = (sclw->drop_index * sclw->row_height) - sclw->scroll_offset + sclw->header_height;

		cairo_set_source_rgb(cr, 0.0, 1.0, 0.0);
		cairo_move_to(cr, 0.0, drop_y);
		cairo_line_to(cr, width, drop_y);
		cairo_set_line_width(cr, 3.0);
		cairo_stroke(cr);
		cairo_set_line_width(cr, 1.0);
	}

	return false;
}

static void stack_cue_list_widget_reset_selection(StackCueListWidget *sclw)
{
	bool changed = false;

	// Clear all selection flags, except the primary
	for (auto iter : sclw->cue_flags)
	{
		if (iter.second & SCLW_FLAG_SELECTED && iter.first != sclw->primary_selection)
		{
			changed = true;
			sclw->cue_flags[iter.first] &= ~SCLW_FLAG_SELECTED;

			// Redraw
			stack_cue_list_widget_update_cue(sclw, iter.first, 0);
		}
	}

	// Select the primary
	if (!(sclw->cue_flags[sclw->primary_selection] & SCLW_FLAG_SELECTED))
	{
		sclw->cue_flags[sclw->primary_selection] |= SCLW_FLAG_SELECTED;
		stack_cue_list_widget_update_cue(sclw, sclw->primary_selection, 0);
	}

	// Signal that one or more selected items have changed
	if (changed)
	{
		g_signal_emit(G_OBJECT(sclw), signal_selection_changed, 0);
	}
}

void stack_cue_list_widget_select_single_cue(StackCueListWidget *sclw, cue_uid_t new_uid)
{
	if (stack_cue_get_by_uid(new_uid) == NULL)
	{
		stack_log("stack_cue_list_widget_select_single_cue(): Cue %016llx does not exist\n", new_uid);
		new_uid = STACK_CUE_UID_NONE;
	}

	stack_cue_list_widget_set_primary_selection(sclw, new_uid);
	stack_cue_list_widget_reset_selection(sclw);
}

void stack_cue_list_widget_set_primary_selection(StackCueListWidget *sclw, cue_uid_t new_uid)
{
	if (new_uid != sclw->primary_selection)
	{
		cue_uid_t old_uid = sclw->primary_selection;
		sclw->primary_selection = new_uid;
		StackCue *new_cue = stack_cue_get_by_uid(new_uid);

		// If the new cue is a child and the parent is not expanded, we should expand
		if (new_cue != NULL && new_cue->parent_cue != NULL && !stack_cue_list_widget_is_cue_expanded(sclw, new_cue->parent_cue->uid))
		{
			// Expand the parent
			stack_cue_list_widget_toggle_expansion(sclw, new_cue->parent_cue->uid);
		}
		else
		{
			// Redraw both rows
			stack_cue_list_widget_update_cue(sclw, old_uid, 0);
			stack_cue_list_widget_update_cue(sclw, new_uid, 0);
		}

		g_signal_emit(G_OBJECT(sclw), signal_primary_selection_changed, 0, new_uid);

		// Redraw
		if (!stack_cue_list_widget_ensure_cue_visible(sclw, new_uid))
		{
			gdk_threads_add_idle(stack_cue_list_widget_idle_redraw, sclw);
		}
	}
}

void stack_cue_list_widget_add_to_selection(StackCueListWidget *sclw, cue_uid_t new_uid)
{
	auto iter = sclw->cue_flags.find(new_uid);
	if (iter != sclw->cue_flags.end())
	{
		iter->second |= SCLW_FLAG_SELECTED;
	}
	else
	{
		sclw->cue_flags[new_uid] = SCLW_FLAG_SELECTED;
	}

	stack_cue_list_widget_update_cue(sclw, new_uid, 0);

	// Signal that one or more selected items have changed
	g_signal_emit(G_OBJECT(sclw), signal_selection_changed, 0);

	// Redraw
	if (!stack_cue_list_widget_ensure_cue_visible(sclw, new_uid))
	{
		gdk_threads_add_idle(stack_cue_list_widget_idle_redraw, sclw);
	}
}

void stack_cue_list_widget_remove_from_selection(StackCueListWidget *sclw, cue_uid_t new_uid)
{
	if (stack_cue_list_widget_is_cue_selected(sclw, new_uid))
	{
		stack_cue_list_widget_toggle_selection(sclw, new_uid);
		stack_cue_list_widget_update_cue(sclw, new_uid, 0);

		// Signal that one or more selected items have changed
		g_signal_emit(G_OBJECT(sclw), signal_selection_changed, 0);

		// Redraw
		if (!stack_cue_list_widget_ensure_cue_visible(sclw, new_uid))
		{
			gdk_threads_add_idle(stack_cue_list_widget_idle_redraw, sclw);
		}
	}
}

void stack_cue_list_widget_toggle_flag(StackCueListWidget *sclw, cue_uid_t new_uid, uint32_t flag)
{
	auto iter = sclw->cue_flags.find(new_uid);
	if (iter != sclw->cue_flags.end())
	{
		if (iter->second & flag)
		{
			iter->second &= ~flag;
		}
		else
		{
			iter->second |= flag;
		}
	}
	else
	{
		// If not found, it can't have has flag
		sclw->cue_flags[new_uid] = flag;
	}
}

void stack_cue_list_widget_toggle_selection(StackCueListWidget *sclw, cue_uid_t new_uid)
{
	stack_cue_list_widget_toggle_flag(sclw, new_uid, SCLW_FLAG_SELECTED);
	stack_cue_list_widget_update_cue(sclw, new_uid, 0);

	// Signal that one or more selected items have changed
	g_signal_emit(G_OBJECT(sclw), signal_selection_changed, 0);

	// Redraw
	if (!stack_cue_list_widget_ensure_cue_visible(sclw, new_uid))
	{
		gdk_threads_add_idle(stack_cue_list_widget_idle_redraw, sclw);
	}
}

void stack_cue_list_widget_toggle_expansion(StackCueListWidget *sclw, cue_uid_t new_uid)
{
	stack_cue_list_widget_toggle_flag(sclw, new_uid, SCLW_FLAG_EXPANDED);

	// Redraw the entire list
	stack_cue_list_widget_update_list_cache(sclw, 0, 0);
	gdk_threads_add_idle(stack_cue_list_widget_idle_redraw, sclw);
}

void stack_cue_list_widget_toggle_scriptref_column(StackCueListWidget *sclw)
{
	if (sclw->scriptref_width == 0)
	{
		sclw->scriptref_width = 90;
	}
	else
	{
		sclw->scriptref_width = 0;
	}

	// Redraw the entire list
	guint width = gtk_widget_get_allocated_width(GTK_WIDGET(sclw));
	stack_cue_list_widget_update_header_cache(sclw, width);
	stack_cue_list_widget_update_list_cache(sclw, 0, 0);
	gdk_threads_add_idle(stack_cue_list_widget_idle_redraw, sclw);
}

bool stack_cue_list_widget_has_flag(StackCueListWidget *sclw, cue_uid_t uid, uint32_t flag)
{
	auto iter = sclw->cue_flags.find(uid);
	if (iter != sclw->cue_flags.end())
	{
		return (iter->second & flag);
	}

	// Not found, can't possibly have flag
	return false;
}

bool stack_cue_list_widget_is_cue_expanded(StackCueListWidget *sclw, cue_uid_t uid)
{
	return stack_cue_list_widget_has_flag(sclw, uid, SCLW_FLAG_EXPANDED);
}

bool stack_cue_list_widget_is_cue_selected(StackCueListWidget *sclw, cue_uid_t uid)
{
	return stack_cue_list_widget_has_flag(sclw, uid, SCLW_FLAG_SELECTED);
}

static void stack_cue_list_widget_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(widget);

	if (sclw->dragging == 1 && sclw->primary_selection != STACK_CUE_UID_NONE)
	{
		// If a button is pressed and we've moved more than a few pixel in
		// either direction than start dragging
		if (fabs(event->x - sclw->drag_start_x) > 5 || fabs(event->y - sclw->drag_start_y) > 5)
		{
			sclw->dragging = 2;
			sclw->dragged_cue = sclw->primary_selection;
		}
	}

	// If we're mid-drag (or possibly just started) then update the current index
	if (sclw->dragging == 2)
	{
		// Get the drop index - capped between 0 and the length of the cue list
		// so that we can drop _after_ the final cue. Note that we add half the
		// row height so we get the index nearest the line between two cues
		int32_t new_index = stack_cue_list_widget_get_index_at_point(sclw, (int32_t)event->x, (sclw->row_height / 2) + (int32_t)event->y);
		int32_t visible_count = stack_cue_list_widget_get_visible_count(sclw);
		if (new_index < 0)
		{
			new_index = 0;
		}
		else if (new_index > visible_count)
		{
			new_index = visible_count;
		}

		if (sclw->drop_index != new_index)
		{
			sclw->drop_index = new_index;

			// Redraw to put in overlay (we draw on top of the cache)
			gdk_threads_add_idle(stack_cue_list_widget_idle_redraw, sclw);
		}
	}
}

static void stack_cue_list_widget_button(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(widget);

	// Button clicked
	if (event->type == GDK_BUTTON_PRESS)
	{
		if (!gtk_widget_has_focus(widget))
		{
			gtk_widget_grab_focus(widget);
			stack_cue_list_widget_update_list_cache(sclw, 0, 0);
			gdk_threads_add_idle(stack_cue_list_widget_idle_redraw, sclw);
		}

		cue_uid_t clicked_cue = stack_cue_list_widget_get_cue_at_point(sclw, event->x, event->y);
		if (clicked_cue != STACK_CUE_UID_NONE)
		{
			// Update cue and redraw

			// For Shift+Click we select a range
			if (event->state & GDK_SHIFT_MASK)
			{
				// If Control was not pressed, we should clear any current selection
				if (!(event->state & GDK_CONTROL_MASK))
				{
					stack_cue_list_widget_reset_selection(sclw);
				}

				// Get iterators to both the current primary selection and what
				// was clicked
				size_t index_clicked, index_current;
				auto iter_clicked = stack_cue_list_recursive_iter_at(sclw->cue_list, clicked_cue, &index_clicked);
				auto iter_current = stack_cue_list_recursive_iter_at(sclw->cue_list, sclw->primary_selection, &index_current);

				// Determine which iterator is earlier in the list (so we only
				// have to go in one direction)
				StackCueStdList::recursive_iterator iter = sclw->cue_list->cues->recursive_begin();
				cue_uid_t end;
				if (index_clicked > index_current)
				{
					iter = iter_current;
					end = clicked_cue;
				}
				else
				{
					iter = iter_clicked;
					end = sclw->primary_selection;
				}

				bool done = false;
				while (!done)
				{
					if (iter == sclw->cue_list->cues->recursive_end())
					{
						done = true;
					}
					else
					{
						StackCue *iter_cue = *iter;
						if (!iter.is_child() || (iter.is_child() & stack_cue_list_widget_is_cue_expanded(sclw, iter_cue->parent_cue->uid)))
						{
							stack_cue_list_widget_add_to_selection(sclw, iter_cue->uid);
						}

						if (iter_cue->uid == end)
						{
							done = true;
						}

						// Iterate to next item
						++iter;
					}
				}
			}
			// For Ctrl+Click (but not Shift+Ctrl+Click) we add to selection
			else if (event->state & GDK_CONTROL_MASK)
			{
				stack_cue_list_widget_toggle_selection(sclw, clicked_cue);
			}

			// Set the new primary row
			stack_cue_list_widget_set_primary_selection(sclw, clicked_cue);

			// If neither Shift or Control were held, we should remove all other
			// selected rows
			if (!(event->state & GDK_CONTROL_MASK) && !(event->state & GDK_SHIFT_MASK))
			{
				stack_cue_list_widget_reset_selection(sclw);
			}

			// If the clicked cue can have children and we are inside the expand/contract icon, then toggle
			StackCue *clicked_cue_object = stack_cue_get_by_uid(clicked_cue);
			SCLWColumnGeometry geom;
			stack_cue_list_widget_get_geometry(sclw, &geom);
			if (clicked_cue_object->can_have_children && event->x > geom.name_x && event->x < geom.name_x + sclw->row_height)
			{
				stack_cue_list_widget_toggle_expansion(sclw, clicked_cue);
			}
		}

		// Set drag start position, just in case
		sclw->dragging = 1;
		sclw->drag_start_x = event->x;
		sclw->drag_start_y = event->y;
		sclw->drag_index = stack_cue_list_widget_get_index_at_point(sclw, event->x, event->y);
	}
	else if (event->type == GDK_BUTTON_RELEASE)
	{
		if (sclw->dragging == 2)
		{
			// Determine if the cue has moved (from our perspective both the
			// original drag index and one past that represent the same location
			// due to the "drop line" being either side of that cue)
			bool moved = false;
			if (sclw->drop_index != sclw->drag_index && sclw->drop_index != sclw->drag_index + 1)
			{
				moved = true;
			}

			// Move the cue (if it's in a different location)
			if (moved)
			{
				// Because index and index + 1 essentially represent the same location, if
				// the drop is after the original cue we need to subtract one to account for
				// the space left by the cue being moved
				int32_t new_index = sclw->drop_index;
				bool before = true;
				if (sclw->drop_index > sclw->drag_index)
				{
					new_index--;
					before = false;
				}

				bool dest_is_child = false;
				StackCue *placeholder_for = NULL;
				StackCue *dest_cue = stack_cue_list_get_cue_at_index(sclw, new_index, &placeholder_for);
				if (dest_cue == NULL)
				{
					if (placeholder_for != NULL)
					{
						dest_cue = placeholder_for;
						before = false;
					}
					else
					{
						stack_log("stack_cue_list_widget_button(): No cue at index %d\n", new_index);
						sclw->dragged_cue = STACK_CUE_UID_NONE;
						sclw->dragging = 0;
						return;
					}
				}

				// Determine if we're dropping the cue inside another cue to make it a child
				// Two conditions: dropping on an existing child or dropping on the root of an
				// expanded cue
				if (dest_cue->parent_cue != NULL || (!before && stack_cue_list_widget_is_cue_expanded(sclw, dest_cue->uid)))
				{
					dest_is_child = true;
				}

				stack_log("stack_cue_list_widget_button(): Moving cue 0x%016llx %s cue 0x%016llx (%d) - child: %s\n", sclw->dragged_cue, before ? "before" : "after", dest_cue->uid, dest_cue->id, dest_is_child ? "true" : "false");

				if (stack_cue_get_by_uid(sclw->dragged_cue)->can_have_children && dest_is_child)
				{
					GtkWidget *message_dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Invalid destination");
					gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(message_dialog), "Cues with children can't be nested inside each other.");
					gtk_window_set_title(GTK_WINDOW(message_dialog), "Error");
					gtk_dialog_run(GTK_DIALOG(message_dialog));
					gtk_widget_destroy(message_dialog);
				}
				else
				{
					stack_cue_list_lock(sclw->cue_list);
					stack_cue_list_move(sclw->cue_list, stack_cue_get_by_uid(sclw->dragged_cue), dest_cue, before, dest_is_child);
					stack_cue_list_unlock(sclw->cue_list);
				}
			}

			// Clear the dragging state
			sclw->dragged_cue = STACK_CUE_UID_NONE;
			sclw->dragging = 0;

			// Redraw to remove overlay (we draw on top of the cache) and to
			// move any cue that was moved
			if (moved)
			{
				stack_cue_list_widget_recalculate_top_cue(sclw);
				stack_cue_list_widget_update_list_cache(sclw, 0, 0);
			}
			gdk_threads_add_idle(stack_cue_list_widget_idle_redraw, sclw);
		}

		// Stop any dragging
		sclw->dragging = 0;
	}
}

static void stack_cue_list_widget_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(widget);
	int32_t new_scroll_offset = sclw->scroll_offset;

	if (event->direction == GDK_SCROLL_UP)
	{
		new_scroll_offset = sclw->scroll_offset - 5;
		if (new_scroll_offset < 0)
		{
			new_scroll_offset = 0;
		}
	}
	else if (event->direction == GDK_SCROLL_DOWN)
	{
		const int32_t full_list_height = stack_cue_list_widget_get_list_height(sclw);
		guint list_display_height = gtk_widget_get_allocated_height(widget) - sclw->header_height;

		// Only scroll if the list is longer than the widget is tall
		if (full_list_height - sclw->scroll_offset > list_display_height)
		{
			new_scroll_offset = sclw->scroll_offset + 5;
		}
	}

	// Only redraw if we've changed
	if (new_scroll_offset != sclw->scroll_offset)
	{
		sclw->scroll_offset = new_scroll_offset;

		// Recalculate what's on display and redraw
		stack_cue_list_widget_recalculate_top_cue(sclw);
		stack_cue_list_widget_update_list_cache(sclw, 0, 0);
		gdk_threads_add_idle(stack_cue_list_widget_idle_redraw, sclw);
	}
}

gboolean stack_cue_list_widget_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(widget);
	cue_uid_t new_cue_uid = sclw->primary_selection;
	bool select_new_cue = false, key_handled = true;

	if (event->keyval == GDK_KEY_Up)
	{
		if (sclw->primary_selection == STACK_CUE_UID_NONE)
		{
			if (sclw->cue_list->cues->size() != 0)
			{
				select_new_cue = true;
				new_cue_uid = (*sclw->cue_list->cues->begin())->uid;
			}
		}
		else
		{
			// Get an iterator at the current cue
			auto iter = stack_cue_list_recursive_iter_at(sclw->cue_list, sclw->primary_selection, NULL);

			// Move the iterator back one
			--iter;

			// If we're not at the end of the cue list, set the new primary selection
			if (iter != sclw->cue_list->cues->recursive_end())
			{
				// If we ended up in an unexpanded child, leave it
				if (iter.is_child() && !stack_cue_list_widget_is_cue_expanded(sclw, (*iter)->parent_cue->uid))
				{
					iter.leave_child(false);
				}
			}

			if (iter != sclw->cue_list->cues->recursive_end())
			{
				select_new_cue = true;
				new_cue_uid = (*iter)->uid;
			}
		}
	}
	else if (event->keyval == GDK_KEY_Down)
	{
		if (sclw->primary_selection == STACK_CUE_UID_NONE)
		{
			if (sclw->cue_list->cues->size() != 0)
			{
				select_new_cue = true;
				new_cue_uid = (*sclw->cue_list->cues->begin())->uid;
			}
		}
		else
		{
			// Get an iterator at the current cue
			auto iter = stack_cue_list_recursive_iter_at(sclw->cue_list, sclw->primary_selection, NULL);

			// Move the iterator on one more
			++iter;

			// If we're not at the end of the cue list, set the new primary selection
			if (iter != sclw->cue_list->cues->recursive_end())
			{
				// If we ended up in an unexpanded child, leave it
				if (iter.is_child() && !stack_cue_list_widget_is_cue_expanded(sclw, (*iter)->parent_cue->uid))
				{
					iter.leave_child(true);
				}
			}

			// If we're not at the end of the cue list, set the new primary selection
			if (iter != sclw->cue_list->cues->recursive_end())
			{
				select_new_cue = true;
				new_cue_uid = (*iter)->uid;
			}
		}
	}
	else if (event->keyval == GDK_KEY_Left)
	{
		if (sclw->primary_selection != STACK_CUE_UID_NONE)
		{
			if (stack_cue_list_widget_is_cue_expanded(sclw, sclw->primary_selection))
			{
				stack_cue_list_widget_toggle_expansion(sclw, sclw->primary_selection);
			}
		}
	}
	else if (event->keyval == GDK_KEY_Right)
	{
		if (sclw->primary_selection != STACK_CUE_UID_NONE)
		{
			if (!stack_cue_list_widget_is_cue_expanded(sclw, sclw->primary_selection))
			{
				stack_cue_list_widget_toggle_expansion(sclw, sclw->primary_selection);
			}
		}
	}
	else if (event->keyval == GDK_KEY_Home || event->keyval == GDK_KEY_KP_Home)
	{
		auto iter = sclw->cue_list->cues->begin();
		if (iter != sclw->cue_list->cues->end())
		{
			select_new_cue = true;
			new_cue_uid = (*iter)->uid;
		}
	}
	else if (event->keyval == GDK_KEY_End || event->keyval == GDK_KEY_KP_End)
	{
		auto iter = --sclw->cue_list->cues->recursive_end();
		if (*iter != NULL)
		{
			// If we ended up in an unexpanded child, leave it
			if (iter.is_child() && !stack_cue_list_widget_is_cue_expanded(sclw, (*iter)->parent_cue->uid))
			{
				iter.leave_child(false);
			}

			select_new_cue = true;
			new_cue_uid = (*iter)->uid;
		}
	}
	else if (event->keyval == GDK_KEY_Page_Up || event->keyval == GDK_KEY_KP_Page_Up)
	{
		if (sclw->primary_selection == STACK_CUE_UID_NONE)
		{
			auto iter = sclw->cue_list->cues->begin();
			if (iter != sclw->cue_list->cues->end())
			{
				select_new_cue = true;
				new_cue_uid = (*iter)->uid;
			}
		}
		else
		{
			// Get an iterator at the current cue
			auto iter = stack_cue_list_recursive_iter_at(sclw->cue_list, sclw->primary_selection, NULL);

			const guint list_display_height = gtk_widget_get_allocated_height(widget) - sclw->header_height;

			size_t count = list_display_height / sclw->row_height;

			// Move the iterator backward
			for (size_t i = 0; i < count; i++)
			{
				// If we reach past the front, move to the front
				if (iter == sclw->cue_list->cues->recursive_end() || *iter == NULL)
				{
					iter = sclw->cue_list->cues->recursive_begin();
					break;
				}

				// Skip children of unexpanded parents
				if (iter.is_child() && !stack_cue_list_widget_is_cue_expanded(sclw, (*iter)->parent_cue->uid))
				{
					iter.leave_child(false);
					i--;
				}
				else
				{
					--iter;
				}
			}

			// If we're not off the front of the cue list...
			if (iter != sclw->cue_list->cues->recursive_end())
			{
				// We might now be in a child cue of an unexpanded parent, so
				// leave it in a forward direction
				if (iter.is_child() && !stack_cue_list_widget_is_cue_expanded(sclw, (*iter)->parent_cue->uid))
				{
					iter.leave_child(false);
				}
			}

			// If we're (now) past the front of the cue list, just select the
			// front one
			if (iter == sclw->cue_list->cues->recursive_end())
			{
				iter = sclw->cue_list->cues->recursive_begin();
			}

			// Set the primary selection
			select_new_cue = true;
			new_cue_uid = (*iter)->uid;
		}
	}
	else if (event->keyval == GDK_KEY_Page_Down || event->keyval == GDK_KEY_KP_Page_Down)
	{
		if (sclw->primary_selection == STACK_CUE_UID_NONE)
		{
			auto iter = sclw->cue_list->cues->begin();
			if (iter != sclw->cue_list->cues->end())
			{
				select_new_cue = true;
				new_cue_uid = (*iter)->uid;
			}
		}
		else
		{
			// Get an iterator at the current cue
			auto iter = stack_cue_list_recursive_iter_at(sclw->cue_list, sclw->primary_selection, NULL);

			const guint list_display_height = gtk_widget_get_allocated_height(widget) - sclw->header_height;

			size_t count = list_display_height / sclw->row_height;

			// Move the iterator forward
			for (size_t i = 0; i < count; i++)
			{
				// If we reach the end, move back one
				if (iter == sclw->cue_list->cues->recursive_end() || *iter == NULL)
				{
					iter--;
					break;
				}

				// Skip children of unexpanded parents
				if (iter.is_child() && !stack_cue_list_widget_is_cue_expanded(sclw, (*iter)->parent_cue->uid))
				{
					iter.leave_child(true);
					i--;
				}
				else
				{
					++iter;
				}
			}

			// If we're not off the end of the cue list...
			if (iter != sclw->cue_list->cues->recursive_end())
			{
				// We might now be in a child cue of an unexpanded parent, so
				// leave it in a forward direction
				if (iter.is_child() && !stack_cue_list_widget_is_cue_expanded(sclw, (*iter)->parent_cue->uid))
				{
					iter.leave_child(true);
				}
			}

			// If we're (now) at the end of the cue list, go back one (or maybe
			// more if we're at a child cue), until we're at a visible cue
			if (iter == sclw->cue_list->cues->recursive_end())
			{
				iter--;
				if (iter.is_child() && !stack_cue_list_widget_is_cue_expanded(sclw, (*iter)->parent_cue->uid))
				{
					iter.leave_child(false);
				}
			}

			// Set the primary selection
			select_new_cue = true;
			new_cue_uid = (*iter)->uid;
		}
	}
	else
	{
		// Pass up the chain
		key_handled = false;
	}

	// On either keypress select the new cue
	if (select_new_cue)
	{
		if (event->state & GDK_SHIFT_MASK)
		{
			stack_cue_list_widget_add_to_selection(sclw, new_cue_uid);
			stack_cue_list_widget_set_primary_selection(sclw, new_cue_uid);
		}
		else
		{
			stack_cue_list_widget_select_single_cue(sclw, new_cue_uid);
		}
	}

	// Prevent default Gtk Widget handling of keypresses when necessary
	return key_handled;
}

static gboolean stack_cue_list_widget_focus(GtkWidget *widget, GdkEventFocus *event)
{
	// Redraw
	stack_cue_list_widget_update_list_cache(STACK_CUE_LIST_WIDGET(widget), 0, 0);
	gdk_threads_add_idle(stack_cue_list_widget_idle_redraw, STACK_CUE_LIST_WIDGET(widget));

	return true;
}

static void stack_cue_list_widget_init(StackCueListWidget *sclw)
{
	gtk_widget_set_has_window(GTK_WIDGET(sclw), true);
	sclw->window = NULL;

	// Make our widget focusable
	gtk_widget_set_can_focus(GTK_WIDGET(sclw), true);

	g_signal_connect(sclw, "button-press-event", G_CALLBACK(stack_cue_list_widget_button), NULL);
	g_signal_connect(sclw, "button-release-event", G_CALLBACK(stack_cue_list_widget_button), NULL);
	g_signal_connect(sclw, "motion-notify-event", G_CALLBACK(stack_cue_list_widget_motion), NULL);
	g_signal_connect(sclw, "scroll-event", G_CALLBACK(stack_cue_list_widget_scroll), NULL);
	g_signal_connect(sclw, "key-press-event", G_CALLBACK(stack_cue_list_widget_key_press), NULL);
	g_signal_connect(sclw, "focus-in-event", G_CALLBACK(stack_cue_list_widget_focus), NULL);
	g_signal_connect(sclw, "focus-out-event", G_CALLBACK(stack_cue_list_widget_focus), NULL);
}

static void stack_cue_list_widget_realize(GtkWidget *widget)
{
	// Note that the Gtk+ docs say you should usually chain up here... but most
	// examples I've found don't, and I've yet to make anything work when I do

	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(widget);

	GtkAllocation allocation;
	gtk_widget_get_allocation(widget, &allocation);

	GdkWindowAttr attr;
	attr.x = allocation.x;
	attr.y = allocation.y;
	attr.width = allocation.width;
	attr.height = allocation.height;
	attr.wclass = GDK_INPUT_OUTPUT;
	attr.window_type = GDK_WINDOW_CHILD;
	attr.event_mask = gtk_widget_get_events(widget) | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK | GDK_POINTER_MOTION_MASK | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK | GDK_FOCUS_CHANGE_MASK;
	attr.visual = gtk_widget_get_visual(widget);

	GdkWindow *parent = gtk_widget_get_parent_window(widget);
	sclw->window = gdk_window_new(parent, &attr, GDK_WA_WMCLASS | GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL);

	// Register our window with the widget
	gtk_widget_set_window(widget, sclw->window);
	gtk_widget_register_window(widget, sclw->window);
	gtk_widget_set_realized(widget, true);

	// Enable tooltips
	gtk_widget_set_has_tooltip(widget, true);
}

static void stack_cue_list_widget_unrealize(GtkWidget *widget)
{
	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(widget);

	gtk_widget_set_realized(widget, false);
	gtk_widget_unregister_window(widget, sclw->window);
	gtk_widget_set_window(widget, NULL);

	gdk_window_destroy(sclw->window);
	sclw->window = NULL;
}

static void stack_cue_list_widget_map(GtkWidget *widget)
{
	// Chain up
	GTK_WIDGET_CLASS(stack_cue_list_widget_parent_class)->map(widget);

	gdk_window_show(STACK_CUE_LIST_WIDGET(widget)->window);

	stack_cue_list_widget_ensure_cue_visible(STACK_CUE_LIST_WIDGET(widget), STACK_CUE_LIST_WIDGET(widget)->primary_selection);
}

static void stack_cue_list_widget_unmap(GtkWidget *widget)
{
	gdk_window_hide(STACK_CUE_LIST_WIDGET(widget)->window);

	// Chain up
	GTK_WIDGET_CLASS(stack_cue_list_widget_parent_class)->unmap(widget);
}

static void stack_cue_list_widget_finalize(GObject *obj)
{
	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(obj);

	// Tidy up
	g_object_unref(sclw->icon_play);
	g_object_unref(sclw->icon_pause);
	g_object_unref(sclw->icon_error);
	g_object_unref(sclw->icon_open);
	g_object_unref(sclw->icon_closed);

	if (sclw->list_surface != NULL)
	{
		cairo_surface_destroy(sclw->list_surface);
	}
	if (sclw->list_cr != NULL)
	{
		cairo_destroy(sclw->list_cr);
	}

	// Chain up
	G_OBJECT_CLASS(stack_cue_list_widget_parent_class)->finalize(obj);
}

static gboolean stack_cue_list_widget_query_tooltip(GtkWidget* widget, gint x, gint y, gboolean keyboard_tooltip, GtkTooltip* tooltip)
{
	if (keyboard_tooltip)
	{
		return false;
	}

	cue_uid_t cue_uid = stack_cue_list_widget_get_cue_at_point(STACK_CUE_LIST_WIDGET(widget), x, y);
	if (cue_uid == STACK_CUE_UID_NONE)
	{
		return false;
	}

	StackCue *cue = stack_cue_get_by_uid(cue_uid);
	if (cue == NULL)
	{
		return false;
	}

	char error[512];
	if (!stack_cue_get_error(cue, error, 512))
	{
		return false;
	}

	gtk_tooltip_set_text(tooltip, error);

	return true;
}

static void stack_cue_list_widget_class_init(StackCueListWidgetClass *cls)
{
	// Things we need to override at the class level
	GObjectClass *object_cls = G_OBJECT_CLASS(cls);
	object_cls->finalize = stack_cue_list_widget_finalize;

	// Things we need to override at the widget level
	GtkWidgetClass *widget_cls = GTK_WIDGET_CLASS(cls);
	widget_cls->draw = stack_cue_list_widget_draw;
	widget_cls->realize = stack_cue_list_widget_realize;
	widget_cls->unrealize = stack_cue_list_widget_unrealize;
	widget_cls->map = stack_cue_list_widget_map;
	widget_cls->unmap = stack_cue_list_widget_unmap;
	widget_cls->query_tooltip = stack_cue_list_widget_query_tooltip;

	// Setup signals for selection changes
	signal_primary_selection_changed = g_signal_new("primary-selection-changed", stack_cue_list_widget_get_type(), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT64);
	signal_selection_changed = g_signal_new("selection-changed", stack_cue_list_widget_get_type(), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}
