// Includes:
#include "StackCueListWidget.h"
#include "StackLog.h"

// Provides an implementation of stack_cue_list_widget_get_type
G_DEFINE_TYPE(StackCueListWidget, stack_cue_list_widget, GTK_TYPE_BOX)

static gboolean stack_cue_list_widget_scrolled(GtkRange *widget, gpointer user_data)
{
	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(user_data);
	gtk_widget_queue_draw(GTK_WIDGET(sclw->content));
	return false;
}

// Queues a redraw of the child widget. Designed to be called from
// gdk_threads_add_idle so as to be UI-thread-safe
static gboolean stack_cue_list_widget_idle_redraw(gpointer user_data)
{
	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(user_data);

	// This is here to prevent redraws whilst we're being destroyed
	if (GTK_IS_WIDGET(sclw))
	{
		if (!sclw->content->redraw_pending)
		{
			gtk_widget_queue_draw(GTK_WIDGET(sclw->content));
			gtk_widget_queue_draw(GTK_WIDGET(sclw->header));
			sclw->content->redraw_pending = true;
		}
	}

	return G_SOURCE_REMOVE;
}

void stack_cue_list_widget_toggle_scriptref_column(StackCueListWidget *sclw)
{
	if (sclw->content->scriptref_width == 0)
	{
		sclw->content->scriptref_width = 90;
	}
	else
	{
		sclw->content->scriptref_width = 0;
	}

	// Redraw everything
	stack_cue_list_content_widget_update_list_cache(sclw->content, 0, 0);
	stack_cue_list_header_widget_update_cache(sclw->header, 0);
	gdk_threads_add_idle(stack_cue_list_widget_idle_redraw, sclw);
}

GtkWidget *stack_cue_list_widget_new()
{
	// Create the new object, setting the orientation of the superclass box
	// to vertical
	GtkWidget *widget = GTK_WIDGET(g_object_new(stack_cue_list_widget_get_type(), "orientation", GTK_ORIENTATION_VERTICAL, NULL, NULL));
	StackCueListWidget *sclw = STACK_CUE_LIST_WIDGET(widget);

	// Create our children
	sclw->content = STACK_CUE_LIST_CONTENT_WIDGET(stack_cue_list_content_widget_new());
	sclw->header = STACK_CUE_LIST_HEADER_WIDGET(stack_cue_list_header_widget_new(sclw->content));
	sclw->scrolled = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));

	// Add them all
	gtk_box_pack_start(GTK_BOX(sclw), GTK_WIDGET(sclw->header), false, false, 0);
	gtk_box_pack_start(GTK_BOX(sclw), GTK_WIDGET(sclw->scrolled), true, true, 0);
	gtk_container_add(GTK_CONTAINER(sclw->scrolled), GTK_WIDGET(sclw->content));

	// Disable "scroll to child"
	GtkAdjustment *adjustment = gtk_adjustment_new(0, 0, 0, 0, 0, 0);
	GtkViewport *viewport = GTK_VIEWPORT(gtk_bin_get_child(GTK_BIN(sclw->scrolled)));
	gtk_container_set_focus_hadjustment(GTK_CONTAINER(viewport), adjustment);
	gtk_container_set_focus_vadjustment(GTK_CONTAINER(viewport), adjustment);
	g_object_unref(adjustment);

	// Make everything visible
	gtk_widget_set_visible(GTK_WIDGET(sclw->header), true);
	gtk_widget_set_visible(GTK_WIDGET(sclw->scrolled), true);
	gtk_widget_set_visible(GTK_WIDGET(sclw->content), true);

	g_signal_connect(gtk_scrolled_window_get_vscrollbar(GTK_SCROLLED_WINDOW(sclw->scrolled)), "value-changed", G_CALLBACK(stack_cue_list_widget_scrolled), (gpointer)sclw);
	return widget;
}

static void stack_cue_list_widget_init(StackCueListWidget *sclhw)
{
	gtk_widget_set_has_window(GTK_WIDGET(sclhw), false);
}

static void stack_cue_list_widget_finalize(GObject *obj)
{
	StackCueListWidget *sclhw = STACK_CUE_LIST_WIDGET(obj);

	// Child widgets should get destroyed automatically by Gtk

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
}
