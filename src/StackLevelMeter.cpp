// Includes:
#include "StackLevelMeter.h"
#include <cmath>

// Provides an implementation of stack_level_meter_get_type
G_DEFINE_TYPE(StackLevelMeter, stack_level_meter, GTK_TYPE_WIDGET)

GtkWidget *stack_level_meter_new(guint channels, float min, float max)
{
	// Validity checks
	if (channels == 0)
	{
		return NULL;
	}

	// Create the new object
	GtkWidget *widget = GTK_WIDGET(g_object_new(stack_level_meter_get_type(), NULL, NULL));
	StackLevelMeter *meter = STACK_LEVEL_METER(widget);

	// Set its parameters
	meter->min = min;
	meter->max = max;
	stack_level_meter_set_channels(meter, channels);

	return widget;
}

void stack_level_meter_set_channels(StackLevelMeter *meter, guint channels)
{
	// This is not allowed
	if (channels == 0)
	{
		return;
	}

	meter->channels = channels;
	delete [] meter->levels;
	delete [] meter->peaks;
	delete [] meter->clipped;
	meter->levels = new float[meter->channels];
	meter->peaks = new float[meter->channels];
	meter->clipped = new bool[meter->channels];

	// Set all the levels to min
	for (guint i = 0; i < channels; i++)
	{
		meter->levels[i] = meter->min;
		meter->peaks[i] = meter->min;
		meter->clipped[i] = false;
	}
}

void stack_level_meter_set_level(StackLevelMeter *meter, guint channel, float level)
{
	if (channel >= meter->channels)
	{
		return;
	}

	if (level != meter->levels[channel])
	{
		meter->levels[channel] = level;
		gtk_widget_queue_draw(GTK_WIDGET(meter));
	}
}

void stack_level_meter_set_peak(StackLevelMeter *meter, guint channel, float level)
{
	if (channel >= meter->channels)
	{
		return;
	}

	if (level != meter->peaks[channel])
	{
		meter->peaks[channel] = level;
		gtk_widget_queue_draw(GTK_WIDGET(meter));
	}
}

void stack_level_meter_set_clipped(StackLevelMeter *meter, guint channel, bool clipped)
{
	if (channel >= meter->channels)
	{
		return;
	}

	if (clipped != meter->clipped[channel])
	{
		meter->clipped[channel] = clipped;
		gtk_widget_queue_draw(GTK_WIDGET(meter));
	}
}

void stack_level_meter_set_level_and_peak(StackLevelMeter *meter, guint channel, float level, float peak)
{
	if (channel >= meter->channels)
	{
		return;
	}

	if (level != meter->levels[channel] || peak != meter->peaks[channel])
	{
		meter->levels[channel] = level;
		meter->peaks[channel] = peak;
		gtk_widget_queue_draw(GTK_WIDGET(meter));
	}
}

void stack_level_meter_reset(StackLevelMeter *meter)
{
	bool redraw = false;
	for (guint i = 0; i < meter->channels; i++)
	{
		if (meter->levels[i] != meter->min || meter->peaks[i] != meter->min)
		{
			meter->levels[i] = meter->min;
			meter->peaks[i] = meter->min;
			meter->clipped[i] = false;
			redraw = true;
		}
	}

	if (redraw)
	{
		gtk_widget_queue_draw(GTK_WIDGET(meter));
	}
}

static void stack_level_meter_destroy_patterns(StackLevelMeter *meter)
{
	if (meter->foreground_pattern)
	{
		cairo_pattern_destroy(meter->foreground_pattern);
		meter->foreground_pattern = NULL;
	}
	if (meter->background_pattern)
	{
		cairo_pattern_destroy(meter->background_pattern);
		meter->background_pattern = NULL;
	}
}

static gboolean stack_level_meter_draw(GtkWidget *widget, cairo_t *cr)
{
	StackLevelMeter *meter = STACK_LEVEL_METER(widget);

    // Get details
    GtkStyleContext *context = gtk_widget_get_style_context(widget);
    guint width = gtk_widget_get_allocated_width(widget);
    guint height = gtk_widget_get_allocated_height(widget);

    // Clear the background
    gtk_render_background(context, cr, 0, 0, width, height);

	if (meter->pattern_size != width)
	{
		stack_level_meter_destroy_patterns(meter);

		// Create a dark pattern background
		meter->background_pattern = cairo_pattern_create_linear(0.0, 0.0, (float)width, 0.0);
		cairo_pattern_add_color_stop_rgb(meter->background_pattern, 0.0, 0.0, 0.25, 0.0);
		cairo_pattern_add_color_stop_rgb(meter->background_pattern, 0.75, 0.25, 0.25, 0.0);
		cairo_pattern_add_color_stop_rgb(meter->background_pattern, 1.0, 0.25, 0.0, 0.0);

		// Create a pattern
		meter->foreground_pattern = cairo_pattern_create_linear(0.0, 0.0, (float)width, 0.0);
		cairo_pattern_add_color_stop_rgb(meter->foreground_pattern, 0.0, 0.0, 1.0, 0.0);
		cairo_pattern_add_color_stop_rgb(meter->foreground_pattern, 0.75, 1.0, 1.0, 0.0);
		cairo_pattern_add_color_stop_rgb(meter->foreground_pattern, 1.0, 1.0, 0.0, 0.0);

		// Store the size
		meter->pattern_size = width;
	}

    // Turn off antialiasing for speed
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

	const float bar_height = floorf((float)height / (float)meter->channels);
	const float float_width = (float)width - bar_height;  // Subtract bar height for square clipped indicator
	const float level_scalar = float_width / (meter->max - meter->min);

	for (guint i = 0; i < meter->channels; i++)
	{
		// Grab the level and put it in bounds
		float level = meter->levels[i];
		if (level < meter->min) { level = meter->min; }
		if (level > meter->max) { level = meter->max; }
		const float level_x = (level - meter->min) * level_scalar;

		// Draw the dark section (we minus one on x for rounding errors)
		cairo_set_source(cr, meter->background_pattern);
		cairo_rectangle(cr, level_x - 1, bar_height * (float)i, float_width - level_x, bar_height - 1.0);
		cairo_fill(cr);

		// Draw the level
		cairo_set_source(cr, meter->foreground_pattern);
		cairo_rectangle(cr, 0.0, bar_height * (float)i, level_x, bar_height - 1.0);
		cairo_fill(cr);

		// Draw the clipped indicator
		if (meter->clipped[i])
		{
			cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
		}
		else
		{
			cairo_set_source_rgb(cr, 0.2, 0.0, 0.0);
		}
		cairo_rectangle(cr, float_width + 1.0, bar_height * (float)i, float_width + bar_height, bar_height - 1.0);
		cairo_fill(cr);

		// Grab the peak and put it in bounds
		float peak = meter->peaks[i];
		if (peak < meter->min) { peak = meter->min; }
		if (peak > meter->max) { peak = meter->max; }
		float peak_x = (peak - meter->min) * level_scalar;

		// Draw the peak line
		cairo_set_source_rgb(cr, 1.0, 1.0, 0.0);
		cairo_move_to(cr, peak_x, bar_height * (float)i);
		cairo_rel_line_to(cr, 0, bar_height - 1);
		cairo_stroke(cr);
	}

	return false;
}

static void stack_level_meter_button(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	StackLevelMeter *meter = STACK_LEVEL_METER(widget);

	// Reset clipped indicator
	memset(meter->clipped, 0, meter->channels * sizeof(bool));

	// Redraw
	gtk_widget_queue_draw(widget);
}

static void stack_level_meter_init(StackLevelMeter *meter)
{
	gtk_widget_set_has_window(GTK_WIDGET(meter), true);
	meter->window = NULL;

	meter->foreground_pattern = NULL;
	meter->background_pattern = NULL;
	meter->pattern_size = -1;
	meter->min = -60.0;
	meter->max = 0.0;
	meter->channels = 1;
	meter->levels = NULL;
	meter->peaks = NULL;
	meter->clipped = NULL;

	g_signal_connect(meter, "button-press-event", G_CALLBACK(stack_level_meter_button), NULL);
}

static void stack_level_meter_realize(GtkWidget *widget)
{
	// Note that the Gtk+ docs say you should usually chain up here... but most
	// examples I've found don't, and I've yet to make anything work when I do

	StackLevelMeter *meter = STACK_LEVEL_METER(widget);

	GtkAllocation allocation;
	gtk_widget_get_allocation(widget, &allocation);

	GdkWindowAttr attr;
	attr.x = allocation.x;
	attr.y = allocation.y;
	attr.width = allocation.width;
	attr.height = allocation.height;
	attr.wclass = GDK_INPUT_OUTPUT;
	attr.window_type = GDK_WINDOW_CHILD;
	attr.event_mask = gtk_widget_get_events(widget) | GDK_BUTTON_PRESS_MASK;
	attr.visual = gtk_widget_get_visual(widget);

	GdkWindow *parent = gtk_widget_get_parent_window(widget);
	meter->window = gdk_window_new(parent, &attr, GDK_WA_WMCLASS | GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL);

	// Register our window with the widget
	gtk_widget_set_window(widget, meter->window);
	gtk_widget_register_window(widget, meter->window);
	gtk_widget_set_realized(widget, true);
}

static void stack_level_meter_unrealize(GtkWidget *widget)
{
	StackLevelMeter *meter = STACK_LEVEL_METER(widget);

	gtk_widget_set_realized(widget, false);
	gtk_widget_unregister_window(widget, meter->window);
	gtk_widget_set_window(widget, NULL);

	gdk_window_destroy(meter->window);
	meter->window = NULL;
}

static void stack_level_meter_map(GtkWidget *widget)
{
	// Chain up
	GTK_WIDGET_CLASS(stack_level_meter_parent_class)->map(widget);

	gdk_window_show(STACK_LEVEL_METER(widget)->window);
}

static void stack_level_meter_unmap(GtkWidget *widget)
{
	gdk_window_hide(STACK_LEVEL_METER(widget)->window);

	// Chain up
	GTK_WIDGET_CLASS(stack_level_meter_parent_class)->unmap(widget);
}

static void stack_level_meter_finalize(GObject *obj)
{
	StackLevelMeter *meter = STACK_LEVEL_METER(obj);
	if (meter->levels != NULL)
	{
		delete [] meter->levels;
	}
	if (meter->peaks != NULL)
	{
		delete [] meter->peaks;
	}
	if (meter->clipped != NULL)
	{
		delete [] meter->clipped;
	}
	stack_level_meter_destroy_patterns(meter);

	// Chain up
	G_OBJECT_CLASS(stack_level_meter_parent_class)->finalize(obj);
}

static void stack_level_meter_class_init(StackLevelMeterClass *cls)
{
	// Things we need to override at the class level
	GObjectClass *object_cls = G_OBJECT_CLASS(cls);
	object_cls->finalize = stack_level_meter_finalize;

	// Things we need to override at the widget level
	GtkWidgetClass *widget_cls = GTK_WIDGET_CLASS(cls);
	widget_cls->draw = stack_level_meter_draw;
	widget_cls->realize = stack_level_meter_realize;
	widget_cls->unrealize = stack_level_meter_unrealize;
	widget_cls->map = stack_level_meter_map;
	widget_cls->unmap = stack_level_meter_unmap;
}
