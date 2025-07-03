// Includes:
#include "StackCueListWidget.h"
#include "StackLog.h"

// Provides an implementation of stack_cue_list_header_widget_get_type
G_DEFINE_TYPE(StackCueListHeaderWidget, stack_cue_list_header_widget, GTK_TYPE_WIDGET)

void stack_cue_list_widget_render_text(StackCueListWidget *sclw, cairo_t *cr, double x, double y, double width, double height, const char *text, bool align_center, bool bold, GtkStyleContext *style_context);

GtkWidget *stack_cue_list_header_widget_new(StackCueListWidget *sclw)
{
	// Create the new object
	GtkWidget *widget = GTK_WIDGET(g_object_new(stack_cue_list_header_widget_get_type(), NULL, NULL));
	StackCueListHeaderWidget *sclhw = STACK_CUE_LIST_HEADER_WIDGET(widget);
	gtk_widget_set_size_request(widget, 0, sclw->header_height);
	sclhw->sclw = sclw;

	return widget;
}

static void stack_cue_list_header_widget_render(StackCueListHeaderWidget *sclhw, cairo_t *cr, double x, double width, const char *text, bool align_center)
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
	gtk_render_background(sc, cr, x, 0, width, sclhw->sclw->header_height);
	gtk_render_frame(sc, cr, x, 0, width, sclhw->sclw->header_height);

	// Render text
	stack_cue_list_widget_render_text(sclhw->sclw, cr, x + (align_center ? 0 : 8), 0, width, sclhw->sclw->header_height, text, align_center, true, sc);

	// Tidy up
	gtk_widget_path_unref(path);
	g_object_unref(sc);
}

static void stack_cue_list_header_widget_update_cache(StackCueListHeaderWidget *sclhw, guint width)
{
	// Tidy up existing objects
	if (sclhw->header_surface != NULL)
	{
		cairo_surface_destroy(sclhw->header_surface);
	}
	if (sclhw->header_cr != NULL)
	{
		cairo_destroy(sclhw->header_cr);
	}

	// Create new Cairo objects
	sclhw->header_surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, sclhw->sclw->header_height);
	sclhw->header_cr = cairo_create(sclhw->header_surface);
	sclhw->header_cache_width = width;

	// Set up for text
	cairo_set_antialias(sclhw->header_cr, CAIRO_ANTIALIAS_DEFAULT);
	cairo_set_font_size(sclhw->header_cr, 14.0);
	cairo_set_line_width(sclhw->header_cr, 1.0);

	// Get some geometry
	SCLWColumnGeometry geom;
	stack_cue_list_widget_get_geometry(sclhw->sclw, &geom);

	// Render header
	stack_cue_list_header_widget_render(sclhw, sclhw->header_cr, 0, geom.cue_x, "", false);
	stack_cue_list_header_widget_render(sclhw, sclhw->header_cr, geom.cue_x, sclhw->sclw->cue_width, "Cue", true);
	if (sclhw->sclw->scriptref_width > 0)
	{
		stack_cue_list_header_widget_render(sclhw, sclhw->header_cr, geom.scriptref_x, sclhw->sclw->scriptref_width, "Script Ref.", true);
	}
	stack_cue_list_header_widget_render(sclhw, sclhw->header_cr, geom.name_x, geom.name_width, "Name", false);
	stack_cue_list_header_widget_render(sclhw, sclhw->header_cr, geom.pre_x, sclhw->sclw->pre_width, "Pre-wait", true);
	stack_cue_list_header_widget_render(sclhw, sclhw->header_cr, geom.action_x, sclhw->sclw->action_width, "Action", true);
	stack_cue_list_header_widget_render(sclhw, sclhw->header_cr, geom.post_x, sclhw->sclw->post_width, "Post-wait", true);
}

static gboolean stack_cue_list_header_widget_draw(GtkWidget *widget, cairo_t *cr)
{
	StackCueListHeaderWidget *sclhw = STACK_CUE_LIST_HEADER_WIDGET(widget);

	// Get details
	guint width = gtk_widget_get_allocated_width(widget);
	guint height = gtk_widget_get_allocated_height(widget);

	// Set up
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
	cairo_set_line_width(cr, 1.0);

	// Update header cache if necessary
	if (sclhw->header_surface == NULL || sclhw->header_cache_width != width)
	{
		stack_cue_list_header_widget_update_cache(sclhw, width);
	}

	// Render the header
	cairo_set_source_surface(cr, sclhw->header_surface, 0, 0);
	cairo_rectangle(cr, 0.0, 0.0, width, height);
	cairo_fill(cr);

	return false;
}

static void stack_cue_list_header_widget_init(StackCueListHeaderWidget *sclhw)
{
	gtk_widget_set_has_window(GTK_WIDGET(sclhw), true);
	sclhw->window = NULL;
}

static void stack_cue_list_header_widget_realize(GtkWidget *widget)
{
	// Note that the Gtk+ docs say you should usually chain up here... but most
	// examples I've found don't, and I've yet to make anything work when I do

	StackCueListHeaderWidget *sclhw = STACK_CUE_LIST_HEADER_WIDGET(widget);

	GtkAllocation allocation;
	gtk_widget_get_allocation(widget, &allocation);

	GdkWindowAttr attr;
	attr.x = allocation.x;
	attr.y = allocation.y;
	attr.width = allocation.width;
	attr.height = allocation.height;
	attr.wclass = GDK_INPUT_OUTPUT;
	attr.window_type = GDK_WINDOW_CHILD;
	attr.event_mask = gtk_widget_get_events(widget);
	attr.visual = gtk_widget_get_visual(widget);

	GdkWindow *parent = gtk_widget_get_parent_window(widget);
	sclhw->window = gdk_window_new(parent, &attr, GDK_WA_WMCLASS | GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL);

	// Register our window with the widget
	gtk_widget_set_window(widget, sclhw->window);
	gtk_widget_register_window(widget, sclhw->window);
	gtk_widget_set_realized(widget, true);
}

static void stack_cue_list_header_widget_unrealize(GtkWidget *widget)
{
	StackCueListHeaderWidget *sclhw = STACK_CUE_LIST_HEADER_WIDGET(widget);

	gtk_widget_set_realized(widget, false);
	gtk_widget_unregister_window(widget, sclhw->window);
	gtk_widget_set_window(widget, NULL);

	gdk_window_destroy(sclhw->window);
	sclhw->window = NULL;
}

static void stack_cue_list_header_widget_map(GtkWidget *widget)
{
	// Chain up
	GTK_WIDGET_CLASS(stack_cue_list_header_widget_parent_class)->map(widget);

	gdk_window_show(STACK_CUE_LIST_HEADER_WIDGET(widget)->window);
}

static void stack_cue_list_header_widget_unmap(GtkWidget *widget)
{
	gdk_window_hide(STACK_CUE_LIST_HEADER_WIDGET(widget)->window);

	// Chain up
	GTK_WIDGET_CLASS(stack_cue_list_header_widget_parent_class)->unmap(widget);
}

static void stack_cue_list_header_widget_finalize(GObject *obj)
{
	StackCueListHeaderWidget *sclhw = STACK_CUE_LIST_HEADER_WIDGET(obj);

	if (sclhw->header_surface != NULL)
	{
		cairo_surface_destroy(sclhw->header_surface);
	}
	if (sclhw->header_cr != NULL)
	{
		cairo_destroy(sclhw->header_cr);
	}

	// Chain up
	G_OBJECT_CLASS(stack_cue_list_header_widget_parent_class)->finalize(obj);
}

static void stack_cue_list_header_widget_class_init(StackCueListHeaderWidgetClass *cls)
{
	// Things we need to override at the class level
	GObjectClass *object_cls = G_OBJECT_CLASS(cls);
	object_cls->finalize = stack_cue_list_header_widget_finalize;

	// Things we need to override at the widget level
	GtkWidgetClass *widget_cls = GTK_WIDGET_CLASS(cls);
	widget_cls->draw = stack_cue_list_header_widget_draw;
	widget_cls->realize = stack_cue_list_header_widget_realize;
	widget_cls->unrealize = stack_cue_list_header_widget_unrealize;
	widget_cls->map = stack_cue_list_header_widget_map;
	widget_cls->unmap = stack_cue_list_header_widget_unmap;
}
