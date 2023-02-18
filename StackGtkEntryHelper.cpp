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