#ifndef _STACKGTKENTRYHELPER_H_INCLUDED
#define _STACKGTKENTRYHELPER_H_INCLUDED

// Includes:
#include <gtk/gtk.h>
#include <cstdint>

void stack_limit_gtk_entry_int(GtkEntry *entry, bool negatives);
void stack_limit_gtk_entry_float(GtkEntry *entry, bool negatives);
void stack_limit_gtk_entry_time(GtkEntry *entry, bool negatives);
void stack_limit_gtk_entry_date(GtkEntry *entry);
void stack_gtk_color_chooser_get_rgb(GtkColorChooser *chooser, uint8_t *r, uint8_t *g, uint8_t *b);

#endif
