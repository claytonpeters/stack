// Includes:
#include "StackGtkEntryHelper.h"

void stack_limit_gtk_entry_int_callback(GtkEntryBuffer *buffer, guint position, gchar *chars, guint count, gpointer userdata)
{
	bool negatives = (bool)userdata;

	// Iterate through the string that was inserted and if anything is not a number,
	for (size_t i = 0; i < count; i++)
	{
		if (!((chars[i] >= '0' && chars[i] <= '9') || (negatives && chars[i] == '-')))
		{
			gtk_entry_buffer_delete_text(buffer, position, count);
			break;
		}
	}
}

void stack_limit_gtk_entry_int(GtkEntry *entry, bool negatives)
{
	g_signal_connect(gtk_entry_get_buffer(entry), "inserted-text", G_CALLBACK(stack_limit_gtk_entry_int_callback), (void*)negatives);
}

void stack_limit_gtk_entry_float_callback(GtkEntryBuffer *buffer, guint position, gchar *chars, guint count, gpointer userdata)
{
	bool negatives = (bool)userdata;

	// If the only text that was inserted was Inf, allow it (for infinite)
	if (count == 3 && (chars[1] == 'I' || chars[1] == 'i') && chars[1] == 'n' && chars[2] == 'f')
	{
		return;
	}

	// If the only text that was inserted was -Inf, allow it (for -infinite)
	if (count == 4 && chars[0] == '-' && (chars[1] == 'I' || chars[1] == 'i') && chars[2] == 'n' && chars[3] == 'f')
	{
		return;
	}

	// Iterate through the string that was inserted and if anything is not a number,
	for (size_t i = 0; i < count; i++)
	{
		if (!((chars[i] >= '0' && chars[i] <= '9') || chars[i] == '.' || (negatives && chars[i] == '-')))
		{
			gtk_entry_buffer_delete_text(buffer, position, count);
			break;
		}
	}
}

void stack_limit_gtk_entry_float(GtkEntry *entry, bool negatives)
{
	g_signal_connect(gtk_entry_get_buffer(entry), "inserted-text", G_CALLBACK(stack_limit_gtk_entry_float_callback), (void*)negatives);
}

void stack_limit_gtk_entry_time_callback(GtkEntryBuffer *buffer, guint position, gchar *chars, guint count, gpointer userdata)
{
	bool negatives = (bool)userdata;

	// Iterate through the string that was inserted and if anything is not a number,
	for (size_t i = 0; i < count; i++)
	{
		if (!((chars[i] >= '0' && chars[i] <= '9') || chars[i] == '.' || chars[i] == ':' || (negatives && chars[i] == '-')))
		{
			gtk_entry_buffer_delete_text(buffer, position, count);
			break;
		}
	}
}

void stack_limit_gtk_entry_time(GtkEntry *entry, bool negatives)
{
	g_signal_connect(gtk_entry_get_buffer(entry), "inserted-text", G_CALLBACK(stack_limit_gtk_entry_time_callback), (void*)negatives);
}

void stack_limit_gtk_entry_date_callback(GtkEntryBuffer *buffer, guint position, gchar *chars, guint count, gpointer userdata)
{
	// Iterate through the string that was inserted and if anything is not a
	// number, dash or slash (accept both for separators)
	for (size_t i = 0; i < count; i++)
	{
		if (!((chars[i] >= '0' && chars[i] <= '9') || chars[i] == '.' || chars[i] == '/' || chars[i] == '-'))
		{
			gtk_entry_buffer_delete_text(buffer, position, count);
			break;
		}
	}
}

void stack_limit_gtk_entry_date(GtkEntry *entry)
{
	g_signal_connect(gtk_entry_get_buffer(entry), "inserted-text", G_CALLBACK(stack_limit_gtk_entry_date_callback), NULL);
}
