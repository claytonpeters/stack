// Includes:
#include "StackApp.h"
#include "StackLog.h"
#include "StackOSCCue.h"
#include "StackGtkHelper.h"
#include "StackJson.h"
#include <cstring>
#include <cstdlib>
#include <string>
#include <cmath>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

// Global: A single instance of our builder so we don't have to keep reloading
// it every time we change the selected cue
static GtkBuilder *soc_builder = NULL;

// Global: A single instace of our icon
static GdkPixbuf *icon = NULL;

static bool stack_osc_cue_update_error_state(StackOSCCue *cue)
{
	char *patch = NULL, *address = NULL, *types = NULL, *arguments = NULL;
	bool error = false;

	// Get the values from the properties
	stack_property_get_string(stack_cue_get_property(STACK_CUE(cue), "patch"), STACK_PROPERTY_VERSION_DEFINED, &patch);
	stack_property_get_string(stack_cue_get_property(STACK_CUE(cue), "address"), STACK_PROPERTY_VERSION_DEFINED, &address);
	stack_property_get_string(stack_cue_get_property(STACK_CUE(cue), "types"), STACK_PROPERTY_VERSION_DEFINED, &types);
	stack_property_get_string(stack_cue_get_property(STACK_CUE(cue), "arguments"), STACK_PROPERTY_VERSION_DEFINED, &arguments);

	// We must have a patch
	if (patch == NULL || strlen(patch) == 0)
	{
		error = true;
	}

	// The patch must exist
	if (stack_cue_list_get_network_patch(STACK_CUE(cue)->parent, patch) == NULL)
	{
		error = true;
	}

	// We must have an address starting with a slash
	if (address == NULL || strlen(address) == 0 || address[0] != '/')
	{
		error = true;
	}

	if (error)
	{
		stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_ERROR);
	}
	else
	{
		if (STACK_CUE(cue)->state == STACK_CUE_STATE_ERROR)
		{
			stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_STOPPED);
		}
	}

	return error;
}

static void stack_osc_cue_ccb_generic(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	// If a defined-version property has changed, we should notify the cue list
	// that we're now different
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackOSCCue* cue = STACK_OSC_CUE(user_data);

		// Notify cue list that we've changed
		stack_cue_list_changed(STACK_CUE(cue)->parent, STACK_CUE(cue), property);

		// Update our error state
		stack_osc_cue_update_error_state(cue);

		// Fire an updated-selected-cue signal to signal the UI to change (we might
		// have changed state)
		if (cue->osc_tab)
		{
			StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(GTK_WIDGET(cue->osc_tab));
			g_signal_emit_by_name((gpointer)window, "update-selected-cue");
		}
	}
}

/// Pause or resumes change callbacks on variables
static void stack_osc_cue_pause_change_callbacks(StackCue *cue, bool pause)
{
	stack_property_pause_change_callback(stack_cue_get_property(cue, "patch"), pause);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "address"), pause);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "types"), pause);
	stack_property_pause_change_callback(stack_cue_get_property(cue, "arguments"), pause);
}

////////////////////////////////////////////////////////////////////////////////
// CREATION AND DESTRUCTION

/// Creates a OSC cue
static StackCue* stack_osc_cue_create(StackCueList *cue_list)
{
	// Allocate the cue
	StackOSCCue* cue = new StackOSCCue();

	// Initialise the superclass
	stack_cue_init(&cue->super, cue_list);

	// Make this class a StackOSCCue
	cue->super._class_name = "StackOSCCue";

	// We start in error state until we have a target
	stack_cue_set_state(STACK_CUE(cue), STACK_CUE_STATE_ERROR);

	// Initialise our variables
	cue->osc_tab = NULL;
	cue->sock = 0;
	stack_cue_set_action_time(STACK_CUE(cue), 1);

	// Add our properties
	StackProperty *patch = stack_property_create("patch", STACK_PROPERTY_TYPE_STRING);
	stack_cue_add_property(STACK_CUE(cue), patch);
	stack_property_set_changed_callback(patch, stack_osc_cue_ccb_generic, (void*)cue);

	StackProperty *address = stack_property_create("address", STACK_PROPERTY_TYPE_STRING);
	stack_cue_add_property(STACK_CUE(cue), address);
	stack_property_set_changed_callback(address, stack_osc_cue_ccb_generic, (void*)cue);

	StackProperty *types = stack_property_create("types", STACK_PROPERTY_TYPE_STRING);
	stack_cue_add_property(STACK_CUE(cue), types);
	stack_property_set_changed_callback(types, stack_osc_cue_ccb_generic, (void*)cue);

	StackProperty *arguments = stack_property_create("arguments", STACK_PROPERTY_TYPE_STRING);
	stack_cue_add_property(STACK_CUE(cue), arguments);
	stack_property_set_changed_callback(arguments, stack_osc_cue_ccb_generic, (void*)cue);

	// Initialise superclass variables
	stack_cue_set_name(STACK_CUE(cue), "OSC Action");

	return STACK_CUE(cue);
}

/// Destroys a OSC cue
static void stack_osc_cue_destroy(StackCue *cue)
{
	// Tidy up the socket
	if (STACK_OSC_CUE(cue)->sock > 0)
	{
		close(STACK_OSC_CUE(cue)->sock);
	}

	// Call parent destructor
	stack_cue_destroy_base(cue);
}

////////////////////////////////////////////////////////////////////////////////
// PROPERTY SETTERS

////////////////////////////////////////////////////////////////////////////////
// UI CALLBACKS

static gboolean ocp_patch_changed(GtkWidget *widget, gpointer user_data)
{
	StackCue *cue = STACK_CUE(((StackAppWindow*)gtk_widget_get_toplevel(widget))->selected_cue);
	const gchar *value = gtk_entry_get_text(GTK_ENTRY(widget));
	stack_property_set_string(stack_cue_get_property(cue, "patch"), STACK_PROPERTY_VERSION_DEFINED, value);
	return false;
}

static gboolean ocp_address_changed(GtkWidget *widget, gpointer user_data)
{
	StackCue *cue = STACK_CUE(((StackAppWindow*)gtk_widget_get_toplevel(widget))->selected_cue);
	const gchar *value = gtk_entry_get_text(GTK_ENTRY(widget));
	stack_property_set_string(stack_cue_get_property(cue, "address"), STACK_PROPERTY_VERSION_DEFINED, value);
	return false;
}

static gboolean ocp_types_changed(GtkWidget *widget, gpointer user_data)
{
	StackCue *cue = STACK_CUE(((StackAppWindow*)gtk_widget_get_toplevel(widget))->selected_cue);
	const gchar *value = gtk_entry_get_text(GTK_ENTRY(widget));
	stack_property_set_string(stack_cue_get_property(cue, "types"), STACK_PROPERTY_VERSION_DEFINED, value);
	return false;
}

static gboolean ocp_arguments_changed(GtkWidget *widget, gpointer user_data)
{
	StackCue *cue = STACK_CUE(((StackAppWindow*)gtk_widget_get_toplevel(widget))->selected_cue);
	GtkTextIter start, end;
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
	gtk_text_buffer_get_start_iter(buffer, &start);
	gtk_text_buffer_get_end_iter(buffer, &end);
	stack_property_set_string(stack_cue_get_property(cue, "arguments"), STACK_PROPERTY_VERSION_DEFINED, gtk_text_buffer_get_text(buffer, &start, &end, false));
	return false;
}

/// Called when the patch settings button is clicked
static void ocp_patch_settings_clicked(GtkWidget *widget, gpointer user_data)
{
	StackAppWindow *window = (StackAppWindow*)gtk_widget_get_toplevel(widget);
	sss_show_dialog(window, window->cue_list, STACK_SETTINGS_TAB_NETWORK);
}

////////////////////////////////////////////////////////////////////////////////
// OSC OPERATIONS

static bool stack_osc_cue_establish_socket(StackOSCCue *cue)
{
	// Don't do anything if we've already got a socket
	if (cue->sock > 0)
	{
		return true;
	}

	// Create our UDP socket
	cue->sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (cue->sock <= 0)
	{
		stack_log("stack_osc_cue_establish_socket(): Failed to create socket (%d)\n", cue->sock);
		cue->sock = 0;
		return false;
	}

	return true;
}

static bool stack_osc_cue_send_osc_packet(StackOSCCue *cue)
{
	char *patch = NULL, *address = NULL, *types = NULL, *arguments = NULL;

	// Our command can never be larger than this otherwise it won't fit in to
	// a UDP packet
	char command_buffer[65535];

	// Get the values from the properties
	stack_property_get_string(stack_cue_get_property(STACK_CUE(cue), "patch"), STACK_PROPERTY_VERSION_DEFINED, &patch);
	stack_property_get_string(stack_cue_get_property(STACK_CUE(cue), "address"), STACK_PROPERTY_VERSION_DEFINED, &address);
	stack_property_get_string(stack_cue_get_property(STACK_CUE(cue), "types"), STACK_PROPERTY_VERSION_DEFINED, &types);
	stack_property_get_string(stack_cue_get_property(STACK_CUE(cue), "arguments"), STACK_PROPERTY_VERSION_DEFINED, &arguments);

	// Get the patch and check it exists
	StackNetworkPatch *network_patch = stack_cue_list_get_network_patch(STACK_CUE(cue)->parent, patch);
	if (network_patch == NULL)
	{
		return false;
	}

	// Copy in the address (plus a NUL)
	strncpy(command_buffer, address, 65535);

	// Start with the length of the address plus its NUL terminator
	size_t length = strlen(address) + 1;

	// Pad with NULs to 4-byte alignment as per OSC spec
	while (length % 4 != 0)
	{
		command_buffer[length] = '\0';
		length++;
	}

	// This pointer is the start of our type spec in the command buffer
	char *types_ptr = &command_buffer[length];
	types_ptr[0] = ',';
	length++;

	// Find the first token
	size_t arg_count = 0;
	char *type_reentrant_save = NULL;
	char *dup_types = strdup(types);
	char *type_token = strtok_r(dup_types, ",", &type_reentrant_save);

	// Iterate through all the tokens
	while (type_token != NULL)
	{
		arg_count++;
		if (strncmp(type_token, "int", 3) == 0)
		{
			types_ptr[arg_count] = 'i';
		}
		else if (strncmp(type_token, "float", 5) == 0)
		{
			types_ptr[arg_count] = 'f';
		}
		else if (strncmp(type_token, "string", 6) == 0)
		{
			types_ptr[arg_count] = 's';
		}

		type_token = strtok_r(NULL, ",", &type_reentrant_save);
	}
	free(dup_types);

	// Add on the null
	types_ptr[arg_count + 1] = '\0';
	length += arg_count + 1;

	// Pad with NULs to 4-byte alignment as per OSC spec
	while (length % 4 != 0)
	{
		command_buffer[length] = '\0';
		length++;
	}

	// Write in the arguments
	char *arg_reentrant_save = NULL;
	char *dup_args = strdup(arguments);
	char *arg_token = strtok_r(dup_args, "\n", &arg_reentrant_save);
	dup_types = strdup(types);
	type_token = strtok_r(dup_types, ",", &type_reentrant_save);

	// Iterate through all the tokens
	while (type_token != NULL && arg_token != NULL)
	{
		if (strncmp(type_token, "int", 3) == 0)
		{
			// Convert the value to big-endian byte order
			int32_t be_value = htonl(atoi(arg_token));
			memcpy(&command_buffer[length], &be_value, sizeof(int32_t));
			length += sizeof(int32_t);
		}
		else if (strncmp(type_token, "float", 5) == 0)
		{
			// Convert the float to big-endian byte order
			float value = atof(arg_token);
			int32_t be_value = htonl(*reinterpret_cast<int32_t*>(&value));

			memcpy(&command_buffer[length], &be_value, sizeof(int32_t));
			length += sizeof(float);
		}
		else if (strncmp(type_token, "string", 6) == 0)
		{
			char *next = strchr(arg_token, '\n');
			size_t arg_length = 0;
			if (next != NULL)
			{
				// Length is up until next newline
				memcpy(&command_buffer[length], arg_token, next - arg_token);
				length += next - arg_token;

				// Add on the NUL
				command_buffer[length] = '\0';
				length++;
			}
			else
			{
				// Length is remainder of arguments string
				strcpy(&command_buffer[length], arg_token);

				// Add on length including NUL
				length += strlen(arg_token) + 1;
			}

			// Pad with NULs to 4-byte alignment as per OSC spec
			while (length % 4 != 0)
			{
				command_buffer[length] = '\0';
				length++;
			}
		}

		// Find the next token
		type_token = strtok_r(NULL, ",", &type_reentrant_save);
		arg_token = strtok_r(NULL, "\n", &arg_reentrant_save);
	}
	free(dup_args);
	free(dup_types);

	stack_log("stack_osc_cue_send_osc_packet(): Send to %s:%d%s, packet length is %d, with %d arguments\n", network_patch->host, network_patch->port, address, length, arg_count);
#ifndef NDEBUG
	stack_log("stack_osc_cue_send_osc_packet(): Hex dump: ");
	for (size_t i = 0; i < length; i++)
	{
		fprintf(stderr, "%02x ", (unsigned char)command_buffer[i]);
	}
	fprintf(stderr, "\n");
#endif

	// Ensure the socket is ready
	if (!stack_osc_cue_establish_socket(cue))
	{
		return false;
	}

	// Get our destination address
	char port_buffer[16];
	snprintf(port_buffer, 16, "%u", network_patch->port);
	addrinfo *address_info;
	int addr_result = getaddrinfo(network_patch->host, port_buffer, NULL, &address_info);
	bool success = true;
	if (addr_result == 0)
	{
		// Send datagram
		int s = sendto(cue->sock, command_buffer, length, 0, address_info->ai_addr, address_info->ai_addrlen);
		freeaddrinfo(address_info);
		if (s <= 0)
		{
			stack_log("stack_osc_cue_send_osc_packet(): Failed to send datagram (%d)\n", s);
			success = false;
		}
	}
	else
	{
		stack_log("stack_osc_cue_send_osc_packet(): Failed to resolve address (%d)\n", addr_result);
		success = false;
	}

	return success;
}

////////////////////////////////////////////////////////////////////////////////
// BASE CUE OPERATIONS

/// Start the cue playing
static bool stack_osc_cue_play(StackCue *cue)
{
	// Call the superclass
	if (!stack_cue_play_base(cue))
	{
		return false;
	}

	// Double-check our error state and don't play if we're broken
	if (stack_osc_cue_update_error_state(STACK_OSC_CUE(cue)))
	{
		return false;
	}

	// Copy the variables to live
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "patch"));
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "address"));
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "types"));
	stack_property_copy_defined_to_live(stack_cue_get_property(cue, "arguments"));

	return true;
}

/// Update the cue based on time
static void stack_osc_cue_pulse(StackCue *cue, stack_time_t clocktime)
{
	bool this_action = false;
	// Get the cue state before the base class potentially updates it
	StackCueState pre_pulse_state = cue->state;

	// Call superclass
	stack_cue_pulse_base(cue, clocktime);

	// We have zero action time so we need to detect the transition from pre->something or playing->something
	if ((pre_pulse_state == STACK_CUE_STATE_PLAYING_PRE && cue->state != STACK_CUE_STATE_PLAYING_PRE) ||
		(pre_pulse_state == STACK_CUE_STATE_PLAYING_ACTION && cue->state != STACK_CUE_STATE_PLAYING_ACTION))
	{
		stack_osc_cue_send_osc_packet(STACK_OSC_CUE(cue));
	}
}

/// Sets up the tabs for the action cue
static void stack_osc_cue_set_tabs(StackCue *cue, GtkNotebook *notebook)
{
	StackOSCCue *acue = STACK_OSC_CUE(cue);

	// Create the tab
	GtkWidget *label = gtk_label_new("OSC");

	// Load the UI (if we haven't already)
	if (soc_builder == NULL)
	{
		soc_builder = gtk_builder_new_from_resource("/org/stack/ui/StackOSCCue.ui");

		// Set up callbacks
		gtk_builder_add_callback_symbol(soc_builder, "ocp_patch_changed", G_CALLBACK(ocp_patch_changed));
		gtk_builder_add_callback_symbol(soc_builder, "ocp_address_changed", G_CALLBACK(ocp_address_changed));
		gtk_builder_add_callback_symbol(soc_builder, "ocp_types_changed", G_CALLBACK(ocp_types_changed));
		gtk_builder_add_callback_symbol(soc_builder, "ocp_arguments_changed", G_CALLBACK(ocp_arguments_changed));
		gtk_builder_add_callback_symbol(soc_builder, "ocp_patch_settings_clicked", G_CALLBACK(ocp_patch_settings_clicked));

		// Connect the signals
		gtk_builder_connect_signals(soc_builder, NULL);
	}
	acue->osc_tab = GTK_WIDGET(gtk_builder_get_object(soc_builder, "ocpGrid"));

	// Pause change callbacks on the properties
	stack_osc_cue_pause_change_callbacks(cue, true);

	// Add an extra reference to the action tab - we're about to remove it's
	// parent and we don't want it to get garbage collected
	g_object_ref(acue->osc_tab);

	// The tab has a parent window in the UI file - unparent the tab container from it
	gtk_widget_unparent(acue->osc_tab);

	// Append the tab (and show it, because it starts off hidden...)
	gtk_notebook_append_page(notebook, acue->osc_tab, label);
	gtk_widget_show(acue->osc_tab);

	char *patch, *address, *types, *arguments;

	// Get the values from the properties
	stack_property_get_string(stack_cue_get_property(cue, "patch"), STACK_PROPERTY_VERSION_DEFINED, &patch);
	stack_property_get_string(stack_cue_get_property(cue, "address"), STACK_PROPERTY_VERSION_DEFINED, &address);
	stack_property_get_string(stack_cue_get_property(cue, "types"), STACK_PROPERTY_VERSION_DEFINED, &types);
	stack_property_get_string(stack_cue_get_property(cue, "arguments"), STACK_PROPERTY_VERSION_DEFINED, &arguments);

	// Set all the values
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(soc_builder, "ocpEntryPatch")), patch);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(soc_builder, "ocpEntryAddress")), address);
	gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(soc_builder, "ocpEntryTypes")), types);
	GtkTextView *tv_arguments = GTK_TEXT_VIEW(gtk_builder_get_object(soc_builder, "ocpTextViewArguments"));
	GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(tv_arguments);
	gtk_text_buffer_set_text(text_buffer, arguments, -1);

	// Resume change callbacks on the properties
	stack_osc_cue_pause_change_callbacks(cue, false);
}

/// Removes the properties tabs for a action cue
static void stack_osc_cue_unset_tabs(StackCue *cue, GtkNotebook *notebook)
{
	// Find our media page
	gint page = gtk_notebook_page_num(notebook, STACK_OSC_CUE(cue)->osc_tab);

	// If we've found the page, remove it
	if (page >= 0)
	{
		gtk_notebook_remove_page(notebook, page);
	}

	// Remove our reference to the action tab
	g_object_unref(STACK_OSC_CUE(cue)->osc_tab);

	// Be tidy
	STACK_OSC_CUE(cue)->osc_tab = NULL;
}

/// Saves the details of this cue as JSON
static char *stack_osc_cue_to_json(StackCue *cue)
{
	StackOSCCue *acue = STACK_OSC_CUE(cue);

	// Build JSON
	Json::Value cue_root;

	// Write out our properties
	stack_property_write_json(stack_cue_get_property(cue, "patch"), &cue_root);
	stack_property_write_json(stack_cue_get_property(cue, "address"), &cue_root);
	stack_property_write_json(stack_cue_get_property(cue, "types"), &cue_root);
	stack_property_write_json(stack_cue_get_property(cue, "arguments"), &cue_root);

	// Write out JSON string and return (to be free'd by
	// stack_osc_cue_free_json)
	Json::StreamWriterBuilder builder;
	return strdup(Json::writeString(builder, cue_root).c_str());
}

/// Frees JSON strings as returned by stack_osc_cue_to_json
static void stack_osc_cue_free_json(StackCue *cue, char *json_data)
{
	free(json_data);
}

/// Re-initialises this cue from JSON Data
void stack_osc_cue_from_json(StackCue *cue, const char *json_data)
{
	// Parse JSON data
	Json::Value cue_root;
	stack_json_read_string(json_data, &cue_root);

	// Get the data that's pertinent to us
	if (!cue_root.isMember("StackOSCCue"))
	{
		stack_log("stack_osc_cue_from_json(): Missing StackOSCCue class\n");
		return;
	}

	Json::Value& cue_data = cue_root["StackOSCCue"];

	// Read in our properties
	if (cue_data.isMember("patch"))
	{
		stack_property_set_string(stack_cue_get_property(cue, "patch"), STACK_PROPERTY_VERSION_DEFINED, cue_data["patch"].asString().c_str());
	}

	if (cue_data.isMember("address"))
	{
		stack_property_set_string(stack_cue_get_property(cue, "address"), STACK_PROPERTY_VERSION_DEFINED, cue_data["address"].asString().c_str());
	}

	if (cue_data.isMember("types"))
	{
		stack_property_set_string(stack_cue_get_property(cue, "types"), STACK_PROPERTY_VERSION_DEFINED, cue_data["types"].asString().c_str());
	}

	if (cue_data.isMember("arguments"))
	{
		stack_property_set_string(stack_cue_get_property(cue, "arguments"), STACK_PROPERTY_VERSION_DEFINED, cue_data["arguments"].asString().c_str());
	}

	stack_osc_cue_update_error_state(STACK_OSC_CUE(cue));
}

/// Gets the error message for the cue
bool stack_osc_cue_get_error(StackCue *cue, char *message, size_t size)
{
	char *patch = NULL, *address = NULL, *types = NULL, *arguments = NULL;
	bool error = false;

	// Get the values from the properties
	stack_property_get_string(stack_cue_get_property(STACK_CUE(cue), "patch"), STACK_PROPERTY_VERSION_DEFINED, &patch);
	stack_property_get_string(stack_cue_get_property(STACK_CUE(cue), "address"), STACK_PROPERTY_VERSION_DEFINED, &address);
	stack_property_get_string(stack_cue_get_property(STACK_CUE(cue), "types"), STACK_PROPERTY_VERSION_DEFINED, &types);
	stack_property_get_string(stack_cue_get_property(STACK_CUE(cue), "arguments"), STACK_PROPERTY_VERSION_DEFINED, &arguments);

	// We must have a patch
	if (patch == NULL || strlen(patch) == 0)
	{
		strncpy(message, "No network patch provided", size);
		return true;
	}

	// The patch must exist
	if (stack_cue_list_get_network_patch(STACK_CUE(cue)->parent, patch) == NULL)
	{
		strncpy(message, "The specified network patch does not exist", size);
		return true;
	}

	// We must have an address
	if (address == NULL || strlen(address) == 0)
	{
		strncpy(message, "No OSC address given", size);
		return true;
	}

	if (address[0] != '/')
	{
		strncpy(message, "OSC address must start with a slash", size);
		return true;
	}

	// Default condition: no error
	strncpy(message, "", size);
	return false;
}

const char *stack_osc_cue_get_field(StackCue *cue, const char *field)
{
	/*if (strcmp(field, "playback") == 0)
	{
		int16_t playback = 0;
		stack_property_get_int16(stack_cue_get_property(cue, "playback"), STACK_PROPERTY_VERSION_DEFINED, &playback);
		snprintf(STACK_OSC_CUE(cue)->playback_string, 8, "%d", playback);
		return STACK_OSC_CUE(cue)->playback_string;
	}
	else if (strcmp(field, "level") == 0)
	{
		int16_t level = 0;
		stack_property_get_int16(stack_cue_get_property(cue, "level"), STACK_PROPERTY_VERSION_DEFINED, &level);
		snprintf(STACK_OSC_CUE(cue)->level_string, 8, "%d", level);
		return STACK_OSC_CUE(cue)->level_string;
	}
	else if (strcmp(field, "jump_target") == 0)
	{
		char *jump_target = NULL;
		stack_property_get_string(stack_cue_get_property(cue, "jump_cue_id"), STACK_PROPERTY_VERSION_DEFINED, &jump_target);
		return jump_target;
	}*/

	return stack_cue_get_field_base(cue, field);
}

/// Returns the icon for a cue
/// @param cue The cue to get the icon of
GdkPixbuf *stack_osc_cue_get_icon(StackCue *cue)
{
	return icon;
}

////////////////////////////////////////////////////////////////////////////////
// CLASS REGISTRATION

// Registers StackOSCCue with the application
void stack_osc_cue_register()
{
	// Load the icons
	icon = gdk_pixbuf_new_from_resource("/org/stack/icons/stackosccue.png", NULL);

	// Register built in cue types
	StackCueClass* osc_cue_class = new StackCueClass{ "StackOSCCue", "StackCue", "OSC Cue", stack_osc_cue_create, stack_osc_cue_destroy, stack_osc_cue_play, NULL, NULL, stack_osc_cue_pulse, stack_osc_cue_set_tabs, stack_osc_cue_unset_tabs, stack_osc_cue_to_json, stack_osc_cue_free_json, stack_osc_cue_from_json, stack_osc_cue_get_error, NULL, NULL, stack_osc_cue_get_field, stack_osc_cue_get_icon, NULL, NULL };
	stack_register_cue_class(osc_cue_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_osc_cue_register();
	return true;
}
