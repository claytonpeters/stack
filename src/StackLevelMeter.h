#ifndef _STACKLEVELMETER_H_INCLUDED
#define _STACKLEVELMETER_H_INCLUDED

// Includes:
#include <gtk/gtk.h>

struct StackLevelMeter
{
	GtkWidget super;

	// The pattern to use for the current level
	cairo_pattern_t *foreground_pattern;

	// The pattern to use for the background
	cairo_pattern_t *background_pattern;

	// The size of the pattern when we last created it
	guint pattern_size;

	// The value that represents the lowest level
	float min;

	// The value that represents the maximum level
	float max;

	// The current levels
	float *levels;

	// The current peak levels
	float *peaks;

	// The current clip status
	bool *clipped;

	// The number of channels
	guint channels;

	// Our window
	GdkWindow *window;
};

struct StackLevelMeterClass
{
	GtkWidgetClass super;
};

// Define our macro for casting
#define STACK_LEVEL_METER(obj)       G_TYPE_CHECK_INSTANCE_CAST(obj, stack_level_meter_get_type(), StackLevelMeter)
#define STACK_LEVEL_METER_CLASS(cls) G_TYPE_CHECK_CLASS_CAST(cls, stack_level_meter_get_type(), StackLevelMeterClass)
#define IS_STACK_LEVEL_METER(obj)    G_TYPE_CHECK_INSTANCE_TYPE(obj, stack_level_meter_get_type())

// Additional functions:
GType stack_level_meter_get_type();
GtkWidget *stack_level_meter_new(guint channels, float min, float max);
void stack_level_meter_set_channels(StackLevelMeter *meter, guint channels);
void stack_level_meter_set_level(StackLevelMeter *meter, guint channel, float level);
void stack_level_meter_set_peak(StackLevelMeter *meter, guint channel, float peak);
void stack_level_meter_set_clipped(StackLevelMeter *meter, guint channel, bool clipped);
void stack_level_meter_set_level_and_peak(StackLevelMeter *meter, guint channel, float level, float peak);
void stack_level_meter_reset(StackLevelMeter *meter);

#endif
