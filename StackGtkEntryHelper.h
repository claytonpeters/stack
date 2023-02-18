#ifndef _STACKGTKENTRYHELPER_H_INCLUDED
#define _STACKGTKENTRYHELPER_H_INCLUDED

// Includes:
#include <gtk/gtk.h>

void stack_limit_gtk_entry_int(GtkEntry *entry, bool negatives);
void stack_limit_gtk_entry_float(GtkEntry *entry, bool negatives);
void stack_limit_gtk_entry_time(GtkEntry *entry, bool negatives);

#endif
