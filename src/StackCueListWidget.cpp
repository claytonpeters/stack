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
	GtkIconTheme *theme = gtk_icon_theme_get_default();
	sclw->icon_play = gtk_icon_theme_load_icon(theme, "media-playback-start", 16, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
	sclw->icon_pause = gtk_icon_theme_load_icon(theme, "media-playback-pause", 16, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
	sclw->icon_error = gtk_icon_theme_load_icon(theme, "error", 16, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);

	// Drag drop
	sclw->dragging = 0;
	sclw->dragged_cue = STACK_CUE_UID_NONE;
	sclw->drop_index = -1;
	sclw->drag_start_x = -1;
	sclw->drag_start_y = -1;

	return widget;
}

// Queues a redraw of the widget. Designed to be called from
// gdk_threads_add_idle so as to be UI-thread-safe
static gboolean stack_cue_list_widget_idle_redraw(gpointer user_data)
{
	// This is here to prevent redraws whilst we're being destroyed
	if (GTK_IS_WIDGET(user_data))
	{
		gtk_widget_queue_draw(GTK_WIDGET(user_data));
	}

	return G_SOURCE_REMOVE;
}

void stack_cue_list_widget_set_cue_list(StackCueListWidget *sclw, StackCueList *cue_list)
{
	// Tidy up
	sclw->cue_list = cue_list;
	sclw->top_cue = STACK_CUE_UID_NONE;
	sclw->scroll_offset = 0;
	sclw->primary_selection = STACK_CUE_UID_NONE;

	// Update the top cue
	stack_cue_list_widget_recalculate_top_cue(sclw);

	// Redraw
	stack_cue_list_widget_update_list_cache(sclw, 0, 0);
	gdk_threads_add_idle(stack_cue_list_widget_idle_redraw, sclw);
}

void stack_cue_list_widget_recalculate_top_cue(StackCueListWidget *sclw)
{
	if (sclw->cue_list == NULL)
	{
		return;
	}

	// For now (with no expanded cues this is trivial)
	int32_t cue_index = sclw->scroll_offset / sclw->row_height;
	StackCue *cue = stack_cue_list_get_cue_by_index(sclw->cue_list, cue_index);

	if (cue == NULL)
	{
		sclw->top_cue = STACK_CUE_UID_NONE;
	}
	else
	{
		sclw->top_cue = cue->uid;
	}
}

int32_t stack_cue_list_widget_get_list_height(StackCueListWidget *sclw)
{
	if (sclw->cue_list == NULL)
	{
		return -1;
	}

	// For now (with no exapnded cues this is trivial)
	return stack_cue_list_count(sclw->cue_list) * sclw->row_height;
}

int32_t stack_cue_list_widget_get_cue_y(StackCueListWidget *sclw, cue_uid_t cue)
{
	if (sclw->cue_list == NULL)
	{
		return -1;
	}

	// Get the index of the cue in the cue list
	size_t index = -1;
	auto iter = stack_cue_list_iter_at(sclw->cue_list, cue, &index);
	stack_cue_list_iter_free(iter);

	if (index == -1)
	{
		return -1;
	}

	return index * sclw->row_height;
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
	void *iter = stack_cue_list_iter_at(sclw->cue_list, cue, NULL);
	if (stack_cue_list_iter_at_end(sclw->cue_list, iter))
	{
		stack_cue_list_iter_free(iter);
		return false;
	}
	stack_cue_list_iter_free(iter);

	// Calculate the Y location of the cue in the list
	int32_t cue_y = stack_cue_list_widget_get_cue_y(sclw, cue);

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
	// Determine the index
	int32_t cue_index = stack_cue_list_widget_get_index_at_point(sclw, x, y);
	if (cue_index == -1)
	{
		return STACK_CUE_UID_NONE;
	}

	// Grab the cue
	StackCue *cue = stack_cue_list_get_cue_by_index(sclw->cue_list, cue_index);
	if (cue == NULL)
	{
		return STACK_CUE_UID_NONE;
	}
	else
	{
		return cue->uid;
	}
}

static gboolean stack_cue_list_widget_idle_update_cue(gpointer user_data)
{
	SCLWUpdateCue *uc = (SCLWUpdateCue*)user_data;
	StackCue *cue = stack_cue_get_by_uid(uc->cue_uid);
	stack_cue_list_widget_update_row(uc->sclw, cue, NULL, -1, uc->fields);
	gtk_widget_queue_draw(GTK_WIDGET(uc->sclw));
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

static void stack_cue_list_widget_render_text(StackCueListWidget *sclw, cairo_t *cr, double x, double y, double width, double height, const char *text, bool align_center, bool bold)
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
	cairo_set_source_rgb(cr, RGBf(0xdd, 0xdd, 0xdd));
	cairo_move_to(cr, x, y + (height - pango_units_to_double(text_height)) / 2 - 1);
	pango_cairo_show_layout(cr, pl);

	// Tidy up
	g_object_unref(pl);
}

static void stack_cue_list_widget_render_header(StackCueListWidget *sclw, cairo_t *cr, double x, double width, const char *text, bool align_center)
{
	// Set up pattern for time background
	cairo_pattern_t *back = cairo_pattern_create_linear(0, 0, 0, sclw->header_height);
	cairo_pattern_add_color_stop_rgb(back, 0.0, RGBf(0x40, 0x40, 0x50));
	cairo_pattern_add_color_stop_rgb(back, 1.0, RGBf(0x38, 0x38, 0x48));

	// Draw background
	cairo_set_source(cr, back);
	cairo_rectangle(cr, x, 0, width, sclw->header_height);
	cairo_fill_preserve(cr);

	// Draw edge
	cairo_set_source_rgb(cr, RGBf(0x30, 0x30, 0x40));
	cairo_stroke(cr);

	// Draw top
	cairo_set_source_rgb(cr, RGBf(0x70, 0x70, 0x80));
	cairo_move_to(cr, x, 0);
	cairo_line_to(cr, x + width, 0);
	cairo_stroke(cr);

	// Tidy up
	cairo_pattern_destroy(back);

	// Render text
	stack_cue_list_widget_render_text(sclw, cr, x + (align_center ? 0 : 8), 0, width, sclw->header_height, text, align_center, true);
}

static void stack_cue_list_widget_render_time(StackCueListWidget *sclw, cairo_t *cr, double x, double y, double width, const char *text, double pct)
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
	stack_cue_list_widget_render_text(sclw, cr, x, y, width, sclw->row_height, text, true, false);
}

static void stack_cue_list_widget_get_geometry(StackCueListWidget *sclw, SCLWColumnGeometry *geom)
{
	// Get width
	guint width = gtk_widget_get_allocated_width(GTK_WIDGET(sclw));

	// Caluclate positions and some widths
	geom->cue_x = (double)sclw->row_height;  // First column is as wide as it is tall
	geom->name_x = geom->cue_x + (double)sclw->cue_width;
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

	// Get column geometry if not given
	SCLWColumnGeometry local_geom;
	if (geom == NULL)
	{
		geom = &local_geom;
		stack_cue_list_widget_get_geometry(sclw, geom);
	}

	if (row_y < 0)
	{
		row_y = stack_cue_list_widget_get_cue_y(sclw, cue->uid) - sclw->scroll_offset;
	}

	// Stop drawing if we're off the top or bottom of the view
	if (row_y + sclw->row_height < 0 || row_y >= (double)sclw->list_cache_height)
	{
		return;
	}

	// Fill the background
	GtkStyleContext *style_context = gtk_widget_get_style_context(GTK_WIDGET(sclw));

	// Highlight selected row
	auto flags_iter = sclw->cue_flags.find(cue->uid);
	bool selected = false;
	if (flags_iter != sclw->cue_flags.end())
	{
		selected = (flags_iter->second & SCLW_FLAG_SELECTED);
	}

	if (selected)
	{
		cairo_set_source_rgb(sclw->list_cr, RGBf(0x40, 0x60, 0x80));
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
		}
		else
		{
			cairo_set_source_rgb(sclw->list_cr, RGBf(0x40, 0x40, 0x50));
		}
	}

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
	}

	// Fill the rectangle
	cairo_rectangle(sclw->list_cr, rectangle_x, row_y, rectangle_width, sclw->row_height);
	cairo_clip(sclw->list_cr);
	cairo_paint(sclw->list_cr);

	if (cue->uid == sclw->primary_selection)
	{
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
		stack_cue_id_to_string(cue->id, buffer, 32);
		stack_cue_list_widget_render_text(sclw, sclw->list_cr, geom->cue_x, row_y, sclw->cue_width, sclw->row_height, buffer, true, false);
	}

	// Render the cue name
	if (fields == 0 || fields == 3)
	{
		stack_cue_list_widget_render_text(sclw, sclw->list_cr, geom->name_x, row_y, geom->name_width, sclw->row_height, stack_cue_get_rendered_name(cue), false, false);
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
			stack_format_time_as_string(cue_action_time - raction, action_buffer, 32);
			stack_format_time_as_string(cue_post_time - rpost, post_buffer, 32);

			// Calculate fractions
			if (cue_pre_time != 0)
			{
				pre_pct = double(rpre) / double(cue_pre_time);
			}
			if (cue_action_time != 0)
			{
				action_pct = double(raction) / double(cue_action_time);
			}
			if (cue_post_time != 0)
			{
				post_pct = double(rpost) / double(cue_post_time);
			}
		}

		stack_cue_list_widget_render_time(sclw, sclw->list_cr, geom->pre_x, row_y, sclw->pre_width, pre_buffer, pre_pct);
		stack_cue_list_widget_render_time(sclw, sclw->list_cr, geom->action_x, row_y, sclw->action_width, action_buffer, action_pct);
		stack_cue_list_widget_render_time(sclw, sclw->list_cr, geom->post_x, row_y, sclw->post_width, post_buffer, post_pct);
	}

	// Reset any clip region that we might have set
	cairo_reset_clip(sclw->list_cr);
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

	cairo_set_source_rgb(sclw->list_cr, RGBf(0x40, 0x40, 0x50));
	cairo_paint(sclw->list_cr);

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
	double pixel_offset = (double)-(sclw->scroll_offset);
	void *citer = stack_cue_list_iter_front(sclw->cue_list);

	// Iterate to the first cue we're meant to display
	while (!stack_cue_list_iter_at_end(sclw->cue_list, citer) && stack_cue_list_iter_get(citer)->uid != sclw->top_cue)
	{
		stack_cue_list_iter_next(citer);
		pixel_offset += (double)sclw->row_height;
	}

	// Iterate over the cue list
	while (!stack_cue_list_iter_at_end(sclw->cue_list, citer))
	{
		// Get the cue
		StackCue *cue = stack_cue_list_iter_get(citer);

		// Figure out the Y location
		double row_y = pixel_offset + (count * sclw->row_height);

		// Stop drawing if we're off the bottom of the view
		if (row_y >= height)
		{
			break;
		}

		// Update the row
		stack_cue_list_widget_update_row(sclw, cue, &geom, row_y, 0);

		// Iterate
		stack_cue_list_iter_next(citer);
		count++;
	}

	// Free the iterator
	stack_cue_list_iter_free(citer);

	// Unlock the cue list
	stack_cue_list_unlock(sclw->cue_list);
}

static gboolean stack_cue_list_widget_draw(GtkWidget *widget, cairo_t *cr)
{
	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(widget);

	// Get details
	guint width = gtk_widget_get_allocated_width(widget);
	guint height = gtk_widget_get_allocated_height(widget);

	// Set up for text
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
	cairo_set_font_size(cr, 14.0);
	cairo_set_line_width(cr, 1.0);

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
	stack_cue_list_widget_set_primary_selection(sclw, new_uid);
	stack_cue_list_widget_reset_selection(sclw);
}

void stack_cue_list_widget_set_primary_selection(StackCueListWidget *sclw, cue_uid_t new_uid)
{
	if (new_uid != sclw->primary_selection)
	{
		cue_uid_t old_uid = sclw->primary_selection;
		sclw->primary_selection = new_uid;
		//sclw->cue_flags[new_uid] = SCLW_FLAG_SELECTED;

		// Redraw both rows
		stack_cue_list_widget_update_cue(sclw, old_uid, 0);
		stack_cue_list_widget_update_cue(sclw, new_uid, 0);

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
	auto iter = sclw->cue_flags.find(new_uid);
	if (iter != sclw->cue_flags.end())
	{
		iter->second &= ~SCLW_FLAG_SELECTED;
	}
	else
	{
		sclw->cue_flags[new_uid] = 0;
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

void stack_cue_list_widget_toggle_selection(StackCueListWidget *sclw, cue_uid_t new_uid)
{
	auto iter = sclw->cue_flags.find(new_uid);
	if (iter != sclw->cue_flags.end())
	{
		if (iter->second & SCLW_FLAG_SELECTED)
		{
			iter->second &= ~SCLW_FLAG_SELECTED;
		}
		else
		{
			iter->second |= SCLW_FLAG_SELECTED;
		}
	}
	else
	{
		// If not found, it can't have been selected
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

bool stack_cue_list_widget_is_cue_selected(StackCueListWidget *sclw, cue_uid_t uid)
{
	auto iter = sclw->cue_flags.find(uid);
	if (iter != sclw->cue_flags.end())
	{
		return (iter->second & SCLW_FLAG_SELECTED);
	}

	// Not found, can't possibly be selected
	return false;
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
		if (new_index < 0)
		{
			new_index = 0;
		}
		else if (new_index > stack_cue_list_count(sclw->cue_list))
		{
			new_index = stack_cue_list_count(sclw->cue_list);
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
				void *iter_clicked = stack_cue_list_iter_at(sclw->cue_list, clicked_cue, &index_clicked);
				void *iter_current = stack_cue_list_iter_at(sclw->cue_list, sclw->primary_selection, &index_current);

				// Determine which iterator is earlier in the list (so we only
				// have to go in one direction)
				void *iter;
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
					if (stack_cue_list_iter_at_end(sclw->cue_list, iter))
					{
						done = true;
					}
					else
					{
						StackCue *iter_cue = stack_cue_list_iter_get(iter);
						stack_cue_list_widget_add_to_selection(sclw, iter_cue->uid);

						if (iter_cue->uid == end)
						{
							done = true;
						}

						// Iterate to next item
						stack_cue_list_iter_next(iter);
					}
				}

				// Tidy up
				stack_cue_list_iter_free(iter_current);
				stack_cue_list_iter_free(iter_clicked);
			}
			// For Ctrl+Click (but not Shift+Ctrl+Click) we add to selection
			else if (event->state & GDK_CONTROL_MASK)
			{
				stack_cue_list_widget_toggle_selection(sclw, clicked_cue);
			}

			// Set the new primary row
			stack_cue_list_widget_set_primary_selection(sclw, clicked_cue);

			// If neither Shift or Control were held, we should remove all other
			// selected tows
			if (!(event->state & GDK_CONTROL_MASK) && !(event->state & GDK_SHIFT_MASK))
			{
				stack_cue_list_widget_reset_selection(sclw);
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
				stack_log("stack_cue_list_widget_button(): Moving cue 0x%016llx to index %d\n", sclw->dragged_cue, sclw->drop_index);

				// Because index and index + 1 essentially represent the same location, if
				// the drop is after the original cue we need to subtract one to account for
				// the space left by the cue being moved
				int32_t new_index = sclw->drop_index;
				if (sclw->drop_index > sclw->drag_index)
				{
					new_index--;
				}
				stack_cue_list_move(sclw->cue_list, stack_cue_get_by_uid(sclw->dragged_cue), new_index);
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

static gboolean stack_cue_list_widget_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(widget);
	cue_uid_t new_cue_uid = sclw->primary_selection;
	bool select_new_cue = false;

	if (event->keyval == GDK_KEY_Up)
	{
		if (sclw->primary_selection == STACK_CUE_UID_NONE)
		{
			auto iter = stack_cue_list_iter_front(sclw->cue_list);
			if (!stack_cue_list_iter_at_end(sclw->cue_list, iter))
			{
				new_cue_uid = stack_cue_list_iter_get(iter)->uid;
			}
		}
		else
		{
			// Get an iterator at the current cue
			auto iter = stack_cue_list_iter_at(sclw->cue_list, sclw->primary_selection, NULL);

			// Move the iterator back one
			stack_cue_list_iter_prev(iter);

			// If we're not at the end of the cue list, set the new primary selection
			if (!stack_cue_list_iter_at_end(sclw->cue_list, iter))
			{
				new_cue_uid = stack_cue_list_iter_get(iter)->uid;
			}
		}
	}
	else if (event->keyval == GDK_KEY_Down)
	{
		if (sclw->primary_selection == STACK_CUE_UID_NONE)
		{
			auto iter = stack_cue_list_iter_front(sclw->cue_list);
			if (!stack_cue_list_iter_at_end(sclw->cue_list, iter))
			{
				new_cue_uid = stack_cue_list_iter_get(iter)->uid;
			}
		}
		else
		{
			// Get an iterator at the current cue
			auto iter = stack_cue_list_iter_at(sclw->cue_list, sclw->primary_selection, NULL);

			// Move the iterator on one more
			stack_cue_list_iter_next(iter);

			// If we're not at the end of the cue list, set the new primary selection
			if (!stack_cue_list_iter_at_end(sclw->cue_list, iter))
			{
				new_cue_uid = stack_cue_list_iter_get(iter)->uid;
			}
		}
	}

	// On either keypress select the new cue
	if (event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_Down)
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

		// Prevent default Gtk Widget handling of keypresses
		return true;
	}

	return false;
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

	// Setup signals for selection changes
	signal_primary_selection_changed = g_signal_new("primary-selection-changed", stack_cue_list_widget_get_type(), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT64);
	signal_selection_changed = g_signal_new("selection-changed", stack_cue_list_widget_get_type(), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}
