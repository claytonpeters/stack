// Includes:
#include "StackAudioFile.h"
#include "StackCueListWidget.h"
#include "StackLog.h"
#include <cmath>

// Provides an implementation of stack_cue_list_widget_get_type
G_DEFINE_TYPE(StackCueListWidget, stack_cue_list_widget, GTK_TYPE_WIDGET)

GtkWidget *stack_cue_list_widget_new()
{
	// Create the new object
	GtkWidget *widget = GTK_WIDGET(g_object_new(stack_cue_list_widget_get_type(), NULL, NULL));
	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(widget);

	sclw->row_height = 26;
	sclw->cue_width = 60;
	sclw->pre_width = 85;
	sclw->action_width = 85;
	sclw->post_width = 85;

	sclw->cue_flags = NULL;
	stack_cue_list_widget_set_cue_list(sclw, NULL);

	return widget;
}

void stack_cue_list_widget_set_cue_list(StackCueListWidget *sclw, StackCueList *cue_list)
{
	// Tidy up
	if (sclw->cue_flags != NULL)
	{
		delete sclw->cue_flags;
	}

	sclw->cue_list = cue_list;
	sclw->top_cue = STACK_CUE_UID_NONE;
	sclw->scroll_offset = 0;
	sclw->primary_selection = STACK_CUE_UID_NONE;
	sclw->cue_flags = new SCLWCueFlagsMap;
}

void stack_cue_list_widget_recalculate_top_cue(StackCueListWidget *sclw)
{
	// For now (with no expanded cues this is trivial
	int32_t cue_index = sclw->scroll_offset / sclw->row_height;
}

// Queues a redraw of the widget. Designed to be called from
// gdk_threads_add_idle so as to be UI-thread-safe
static gboolean stack_cue_list_widget_idle_redraw(gpointer user_data)
{
	gtk_widget_queue_draw(GTK_WIDGET(user_data));
	return G_SOURCE_REMOVE;
}

static void stack_cue_list_widget_render_text(StackCueListWidget *sclw, cairo_t *cr, double x, double y, double width, const char *text, bool align_center)
{
	// Get text size
	cairo_text_extents_t text_size;
	cairo_text_extents(cr, text, &text_size);

	// Figure out the X location
	double text_x;
	if (align_center)
	{
		text_x = x + (width - text_size.width) / 2;
	}
	else
	{
		// Introduce a bit of padding
		text_x = x + 4;
	}

	// Render text
	cairo_move_to(cr, text_x, y + (sclw->row_height - text_size.y_bearing) / 2);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_show_text(cr, text);
}

static void stack_cue_list_widget_render_header(StackCueListWidget *sclw, cairo_t *cr, double x, double width, const char *text, bool align_center)
{
	// Set up pattern for time background
	cairo_pattern_t *back = cairo_pattern_create_linear(0, 0, 0, sclw->row_height);
	cairo_pattern_add_color_stop_rgb(back, 0.0, 0.21875, 0.21875, 0.28125);
	cairo_pattern_add_color_stop_rgb(back, 1.0, 0.1875, 0.1875, 0.25);

	// Draw background
	cairo_set_source(cr, back);
	cairo_rectangle(cr, x, 0, width, sclw->row_height);
	cairo_fill_preserve(cr);

	// Draw edge
	cairo_set_source_rgb(cr, 0.1875, 0.1875, 0.25);
	cairo_stroke(cr);

	// Tidy up
	cairo_pattern_destroy(back);

	// Render text
	stack_cue_list_widget_render_text(sclw, cr, x, 0, width, text, align_center);
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
		cairo_rectangle(cr, x + 1, y + 1, (width * pct) - 2, sclw->row_height - 2);
		cairo_fill_preserve(cr);

		// Draw edge
		cairo_set_source_rgb(cr, 0.1875, 0.1875, 0.25);
		cairo_stroke(cr);

		// Tidy up
		cairo_pattern_destroy(back);
	}

	// Render text on top
	stack_cue_list_widget_render_text(sclw, cr, x, y, width, text, true);
}

//static void draw_text
static gboolean stack_cue_list_widget_draw(GtkWidget *widget, cairo_t *cr)
{
	char buffer[32];

	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(widget);

	// Get details
	guint width = gtk_widget_get_allocated_width(widget);
	guint height = gtk_widget_get_allocated_height(widget);

	// Fill the background
	gtk_render_background(gtk_widget_get_style_context(widget), cr, 0, 0, width, height);

	// Set up for text
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
	cairo_set_font_size(cr, 14.0);
	cairo_set_line_width(cr, 1.0);

	double cue_x = (double)sclw->row_height;  // First column is as wide as it is tall
	double name_x = cue_x + (double)sclw->cue_width;
	double post_x = (double)width - (double)sclw->post_width;
	double action_x = post_x - (double)sclw->action_width;
	double pre_x = action_x - (double)sclw->pre_width;
	double name_width = pre_x - name_x;

	// Render header
	stack_cue_list_widget_render_header(sclw, cr, cue_x, sclw->cue_width, "Cue", true);
	stack_cue_list_widget_render_header(sclw, cr, name_x, name_width, "Name", false);
	stack_cue_list_widget_render_header(sclw, cr, pre_x, sclw->pre_width, "Pre-wait", true);
	stack_cue_list_widget_render_header(sclw, cr, action_x, sclw->action_width, "Action", true);
	stack_cue_list_widget_render_header(sclw, cr, post_x, sclw->post_width, "Post-wait", true);

	// If we don't have a cue list then return
	if (sclw->cue_list == NULL)
	{
		return false;
	}

	// Lock the cue list
	stack_cue_list_lock(sclw->cue_list);

	// Get an iterator over the cue list
	size_t count = 0;
	void *citer = stack_cue_list_iter_front(sclw->cue_list);

	// Iterate over the cue list
	while (!stack_cue_list_iter_at_end(sclw->cue_list, citer))
	{
		// Get the cue
		StackCue *cue = stack_cue_list_iter_get(citer);

		// Figure out the Y location
		double row_y = (count + 1) * sclw->row_height;

		// Render the cue number
		stack_cue_id_to_string(cue->id, buffer, 32);
		stack_cue_list_widget_render_text(sclw, cr, cue_x, row_y, sclw->cue_width, buffer, true);

		// Render the cue name
		stack_cue_list_widget_render_text(sclw, cr, name_x, row_y, name_width, stack_cue_get_rendered_name(cue), false);

		// Render the cue times
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

		action_pct = 0.5;
		stack_cue_list_widget_render_time(sclw, cr, pre_x, row_y, sclw->pre_width, pre_buffer, pre_pct);
		stack_cue_list_widget_render_time(sclw, cr, action_x, row_y, sclw->action_width, action_buffer, action_pct);
		stack_cue_list_widget_render_time(sclw, cr, post_x, row_y, sclw->post_width, post_buffer, post_pct);

		// Iterate
		stack_cue_list_iter_next(citer);
		count++;
	}

	// Free the iterator
	stack_cue_list_iter_free(citer);

	// Unlock the cue list
	stack_cue_list_unlock(sclw->cue_list);

	return false;
}

static void stack_cue_list_widget_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(widget);
}

static void stack_cue_list_widget_button(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(widget);
}

static void stack_cue_list_widget_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(widget);
}

static void stack_cue_list_widget_init(StackCueListWidget *sclw)
{
	gtk_widget_set_has_window(GTK_WIDGET(sclw), true);
	sclw->window = NULL;

	g_signal_connect(sclw, "button-press-event", G_CALLBACK(stack_cue_list_widget_button), NULL);
	g_signal_connect(sclw, "button-release-event", G_CALLBACK(stack_cue_list_widget_button), NULL);
	g_signal_connect(sclw, "motion-notify-event", G_CALLBACK(stack_cue_list_widget_motion), NULL);
	g_signal_connect(sclw, "scroll-event", G_CALLBACK(stack_cue_list_widget_scroll), NULL);
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
	attr.event_mask = gtk_widget_get_events(widget) | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK | GDK_POINTER_MOTION_MASK;
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
}
