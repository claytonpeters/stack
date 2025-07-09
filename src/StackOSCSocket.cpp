// Includes:
#define _IN_STACKOSCSOCKET_CPP
#include "StackOSCSocket.h"
#include "StackCue.h"
#include "StackLog.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

// Defines:
const char STACK_OSC_PARAM_TYPE_INT	= 'i';
const char STACK_OSC_PARAM_TYPE_FLOAT  = 'f';
const char STACK_OSC_PARAM_TYPE_STRING = 's';

StackOSCMessage *stack_osc_socket_parse_message(const char *buffer, size_t buffer_size)
{
	// Early error checking
	if (buffer == NULL || buffer_size == 0)
	{
		stack_log("stack_osc_socket_parse_message(): Invalid buffer\n");
		return NULL;
	}

#ifndef NDEBUG
	stack_log("stack_osc_socket_parse_message(): Hex dump: ");
	for (size_t i = 0; i < buffer_size; i++)
	{
	    fprintf(stderr, "%02x ", (unsigned char)buffer[i]);
	}
	fprintf(stderr, "\n");
#endif

	// First byte points to NUL-terminated address
	const char *address = &buffer[0];
	size_t address_size = strlen(address);

	// Figure out the index of where the types start accounting for the 4-byte padding
	size_t types_index = address_size + 1; // for the NUL
	if (types_index % 4 != 0)
	{
		types_index += 4 - (types_index % 4);
	}

	// Make sure we're not out of bounds
	if (types_index >= buffer_size)
	{
		// Technically, omitting the type string is invalid according to the OSC
		// spec, but the spec also says we should be robust to omitting the type
		// so we'll return a valid message here with no params
		stack_log("stack_osc_socket_parse_message(): Warning: non-conformant zero-param OSC message received\n");

		StackOSCMessage *message = new StackOSCMessage;
		message->address = strdup(address);
		message->param_count = 0;
		message->params = NULL;

		return message;
	}

	// Get a pointer to the NUL-terminated types
	const char *types = &buffer[types_index];
	size_t types_size = strlen(types);

	// Type string always starts with a comma
	if (types[0] != ',')
	{
		stack_log("stack_osc_socket_parse_message(): Invalid type string - no leading comma\n");
		return NULL;
	}

	// If we have a string length of one, which given the above check is just
	// a comma, then we have no params and can return early
	if (types_size == 1)
	{
		StackOSCMessage *message = new StackOSCMessage;
		message->address = strdup(address);
		message->param_count = 0;
		message->params = NULL;

		return message;
	}

	// Figure out the index of where the parameters start (relative to
	// the types), accounting for the four-byte padding
	size_t params_index = types_size + 1; // for the NUL
	if (params_index % 4 != 0)
	{
		params_index += 4 - (params_index % 4);
	}

	// Make sure we're not out of bounds
	if (params_index + types_index >= buffer_size)
	{
		stack_log("stack_osc_socket_parse_message(): Truncated message, params outside of message\n");
		return NULL;
	}

	// Create the initial message
	StackOSCMessage *message = new StackOSCMessage;
	message->address = strdup(address);
	message->param_count = types_size - 1; // because of the leading comma
	message->params = new StackOSCParam[message->param_count];
	memset(message->params, 0, sizeof(StackOSCParam) * message->param_count);

	// Iterate over the expected parameters
	const char *param_ptr = &types[params_index];
	for (size_t param = 0; param < message->param_count; param++)
	{
		message->params[param].type = types[param + 1]; // because of the leading comma
		switch (message->params[param].type)
		{
			case STACK_OSC_PARAM_TYPE_INT:
				// Int is in big-endian
				memcpy(&message->params[param].data.as_int, param_ptr, sizeof(int32_t));
				message->params[param].data.as_int = ntohl(message->params[param].data.as_int);
				param_ptr += sizeof(int32_t);
				break;
			case STACK_OSC_PARAM_TYPE_FLOAT:
				// Float is in big-endian
				int32_t temp;
				memcpy(&temp, param_ptr, sizeof(float));
				temp = ntohl(temp);
				memcpy(&message->params[param].data.as_float, &temp, sizeof(float));
				param_ptr += sizeof(float);
				break;
			case STACK_OSC_PARAM_TYPE_STRING:
				size_t string_length, ptr_offset;
				string_length = strlen(param_ptr);
				message->params[param].data.as_string = strdup(param_ptr);

				// Figure out how much to offset the pointer by, accounting for
				// the 4-byte alignment padding on the string
				ptr_offset = (string_length + 1);
				if (ptr_offset % 4 != 0)
				{
					ptr_offset += 4 - (ptr_offset % 4);
				}
				param_ptr += ptr_offset;
				break;
			default:
				stack_log("stack_osc_socket_parse_message(): Unsupported or unknown data type '%c'\n", message->params[param].type);
				stack_osc_message_free(message);
				return NULL;
		}
	}

	return message;
}

void stack_osc_socket_dispatch_cue_play(StackOSCSocket *osc_socket, StackOSCMessage *message)
{
	if (message->param_count != 1)
	{
		stack_log("stack_osc_socket_dispatch_cue_play(): Incorrect message parameter count %d, expected 1\n", message->param_count);
		return;
	}

	if (message->params[0].type != STACK_OSC_PARAM_TYPE_STRING)
	{
		stack_log("stack_osc_socket_dispatch_cue_play(): Incorrect type for message parameter 1: got %c, expected %c\n", message->params[0].type, STACK_OSC_PARAM_TYPE_STRING);
		return;
	}

	StackCue *cue = stack_cue_list_get_first_cue_with_id(osc_socket->cue_list, message->params[0].data.as_string);
	if (cue != NULL)
	{
		stack_cue_play(cue);
	}
}

void stack_osc_socket_dispatch_cue_pause(StackOSCSocket *osc_socket, StackOSCMessage *message)
{
	if (message->param_count != 1)
	{
		stack_log("stack_osc_socket_dispatch_cue_pause(): Incorrect message parameter count %d, expected 1\n", message->param_count);
		return;
	}

	if (message->params[0].type != STACK_OSC_PARAM_TYPE_STRING)
	{
		stack_log("stack_osc_socket_dispatch_cue_pause(): Incorrect type for message parameter 1: got %c, expected %c\n", message->params[0].type, STACK_OSC_PARAM_TYPE_STRING);
		return;
	}

	StackCue *cue = stack_cue_list_get_first_cue_with_id(osc_socket->cue_list, message->params[0].data.as_string);
	if (cue != NULL)
	{
		stack_cue_pause(cue);
	}
}

void stack_osc_socket_dispatch_cue_stop(StackOSCSocket *osc_socket, StackOSCMessage *message)
{
	if (message->param_count != 1)
	{
		stack_log("stack_osc_socket_dispatch_cue_stop(): Incorrect message parameter count %d, expected 1\n", message->param_count);
		return;
	}

	if (message->params[0].type != STACK_OSC_PARAM_TYPE_STRING)
	{
		stack_log("stack_osc_socket_dispatch_cue_stop(): Incorrect type for message parameter 1: got %c, expected %c\n", message->params[0].type, STACK_OSC_PARAM_TYPE_STRING);
		return;
	}

	StackCue *cue = stack_cue_list_get_first_cue_with_id(osc_socket->cue_list, message->params[0].data.as_string);
	if (cue != NULL)
	{
		stack_cue_stop(cue);
	}
}

void stack_osc_socket_dispatch_list_stopall(StackOSCSocket *osc_socket, StackOSCMessage *message)
{
	if (message->param_count != 0)
	{
		stack_log("stack_osc_socket_dispatch_list_stopall(): Incorrect message parameter count %d, expected 0\n", message->param_count);
		return;
	}

	stack_cue_list_stop_all(osc_socket->cue_list);
}

void stack_osc_socket_dispatch_list_go(StackOSCSocket *osc_socket, StackOSCMessage *message)
{
	if (message->param_count != 0)
	{
		stack_log("stack_osc_socket_dispatch_list_go(): Incorrect message parameter count %d, expected 0\n", message->param_count);
		return;
	}

	stack_cue_list_go(osc_socket->cue_list);
}

void stack_osc_socket_dispatch_list_next(StackOSCSocket *osc_socket, StackOSCMessage *message)
{
	if (message->param_count != 0)
	{
		stack_log("stack_osc_socket_dispatch_list_next(): Incorrect message parameter count %d, expected 0\n", message->param_count);
		return;
	}

	stack_cue_list_next_cue(osc_socket->cue_list);
}

void stack_osc_socket_dispatch_list_previous(StackOSCSocket *osc_socket, StackOSCMessage *message)
{
	if (message->param_count != 0)
	{
		stack_log("stack_osc_socket_dispatch_list_previous(): Incorrect message parameter count %d, expected 0\n", message->param_count);
		return;
	}

	stack_cue_list_prev_cue(osc_socket->cue_list);
}

void stack_osc_socket_dispatch_list_goto(StackOSCSocket *osc_socket, StackOSCMessage *message)
{
	if (message->param_count != 1)
	{
		stack_log("stack_osc_socket_dispatch_list_goto(): Incorrect message parameter count %d, expected 1\n", message->param_count);
		return;
	}

	StackCue *cue = stack_cue_list_get_first_cue_with_id(osc_socket->cue_list, message->params[0].data.as_string);
	if (cue != NULL)
	{
		stack_cue_list_goto(osc_socket->cue_list, cue->uid);
	}
	else
	{
		stack_log("stack_osc_socket_dispatch_list_goto(): Failed to find cue with ID '%s'\n", message->params[0].data.as_string);
	}
}

void stack_osc_socket_dispatch_message(StackOSCSocket *osc_socket, StackOSCMessage *message)
{
	StackCueList *cue_list = osc_socket->cue_list;

	// Ensure the message address is at least as long as our message prefix,
	// or it definitely doesn't match
	const size_t expected_prefix_length = strlen(osc_socket->address_prefix);
	if (strlen(message->address) < expected_prefix_length)
	{
		stack_log("stack_osc_socket_dispatch_message(): Unknown address '%s'\n", message->address);
		return;
	}

	// Validate the prefix
	if (memcmp(message->address, osc_socket->address_prefix, expected_prefix_length) != 0)
	{
		stack_log("stack_osc_socket_dispatch_message(): Unknown address '%s'\n", message->address);
		return;
	}

	// Grab a pointer to the unprefixed part of the message's address
	const char *unprefixed_address = &message->address[expected_prefix_length];

	if (strcmp(unprefixed_address, "cue/play") == 0)
	{
		stack_osc_socket_dispatch_cue_play(osc_socket, message);
	}
	else if (strcmp(unprefixed_address, "cue/pause") == 0)
	{
		stack_osc_socket_dispatch_cue_pause(osc_socket, message);
	}
	else if (strcmp(unprefixed_address, "cue/stop") == 0)
	{
		stack_osc_socket_dispatch_cue_stop(osc_socket, message);
	}
	else if (strcmp(unprefixed_address, "list/stopall") == 0)
	{
		stack_osc_socket_dispatch_list_stopall(osc_socket, message);
	}
	else if (strcmp(unprefixed_address, "list/go") == 0)
	{
		stack_osc_socket_dispatch_list_go(osc_socket, message);
	}
	else if (strcmp(unprefixed_address, "list/next") == 0)
	{
		stack_osc_socket_dispatch_list_next(osc_socket, message);
	}
	else if (strcmp(unprefixed_address, "list/previous") == 0)
	{
		stack_osc_socket_dispatch_list_previous(osc_socket, message);
	}
	else if (strcmp(unprefixed_address, "list/goto") == 0)
	{
		stack_osc_socket_dispatch_list_goto(osc_socket, message);
	}
	else
	{
		stack_log("stack_osc_socket_dispatch_message(): Unknown address '%s'\n", message->address);
	}
}

void stack_osc_message_free(StackOSCMessage *message)
{
	// Error checking
	if (message == NULL)
	{
		return;
	}

	if (message->address != NULL)
	{
		free(message->address);
	}

	if (message->params != NULL)
	{
		for (size_t i = 0; i < message->param_count; i++)
		{
			if (message->params[i].type == STACK_OSC_PARAM_TYPE_STRING)
			{
				free(message->params[i].data.as_string);
			}
		}

		delete [] message->params;
	}

	delete message;
}

bool stack_osc_socket_establish(StackOSCSocket *osc_socket)
{
	if (osc_socket->socket != 0)
	{
		return true;
	}

	// Create our UDP socket
	osc_socket->socket = socket(PF_INET, SOCK_DGRAM, 0);
	if (osc_socket->socket <= 0)
	{
		stack_log("stack_osc_socket_establish(0x%016llx): Failed to create socket (%d)\n", osc_socket, osc_socket->socket);
		osc_socket->socket = 0;
		return false;
	}

	// Get the address info of our bind address
	char port_buffer[16];
	snprintf(port_buffer, 16, "%u", osc_socket->bind_port);
	addrinfo *address_info;
	int addr_result = getaddrinfo(osc_socket->bind_addr, port_buffer, NULL, &address_info);
	if (addr_result != 0)
	{
		stack_log("stack_osc_socket_establish(0x%016llx): Failed to resolve bind address (%d)\n", osc_socket, addr_result);
		close(osc_socket->socket);
		osc_socket->socket = 0;
		return false;
	}

	// Bind the socket
	int bind_result = bind(osc_socket->socket, address_info->ai_addr, address_info->ai_addrlen);
	freeaddrinfo(address_info);
	if (bind_result != 0)
	{
		stack_log("stack_osc_socket_establish(0x%016llx): Failed to bind socket (%d, %d)\n", osc_socket, bind_result, errno);
		close(osc_socket->socket);
		osc_socket->socket = 0;
		return false;
	}

	stack_log("stack_osc_socket_establish(0x%016llx): Ready on %s:%u\n", osc_socket, osc_socket->bind_addr, osc_socket->bind_port);
	return true;
}

void stack_osc_socket_thread(void *user_data)
{
	StackOSCSocket *osc_socket = STACK_OSC_SOCKET(user_data);
	stack_log("stack_osc_socket_thread(0x%016llx): Started\n", osc_socket);

	// Loop whilst the thread is running
	while (osc_socket->running)
	{
		// Connect the socket
		if (!stack_osc_socket_establish(osc_socket))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			continue;
		}

		// Read a datagram
		char buffer[65536];
		ssize_t nread = recvfrom(osc_socket->socket, buffer, sizeof(buffer), 0, NULL, NULL);
		if (nread <= 0)
		{
			// On a read failure, close the socket and we'll re-establish next time
			if (osc_socket->socket != 0)
			{
				close(osc_socket->socket);
				osc_socket->socket = 0;
				continue;
			}
			else
			{
				// OSC socket is gone, stop processing
				break;
			}
		}

		stack_log("stack_osc_socket_thread(0x%016llx): Datagram rececived (%d bytes)\n", osc_socket, nread);
		StackOSCMessage *message = stack_osc_socket_parse_message(buffer, nread);
		if (message == NULL)
		{
			stack_log("stack_osc_socket_thread(0x%016llx): Invalid OSC message\n", osc_socket);
		}
		else
		{
			stack_log("stack_osc_socket_thread(0x%016llx): got OSC message at address %s with %d params\n", osc_socket, message->address, message->param_count);
			stack_osc_socket_dispatch_message(osc_socket, message);
			stack_osc_message_free(message);
		}
	}

	stack_log("stack_osc_socket_thread(0x%016llx): Terminated\n", osc_socket);
}

StackOSCSocket *stack_osc_socket_create(const char *bind_addr, uint16_t bind_port, const char *address_prefix, StackCueList *cue_list)
{
	if (bind_addr == NULL || strlen(bind_addr) == 0)
	{
		stack_log("stack_osc_socket_create(): Invalid bind address\n");
		return NULL;
	}

	if (bind_port == 0)
	{
		stack_log("stack_osc_socket_create(): Invalid bind port\n");
		return NULL;
	}

	if (!stack_osc_socket_is_address_prefix_valid(address_prefix))
	{
		stack_log("stack_osc_socket_create(): Invalid address prefix\n");
		return NULL;
	}

	if (cue_list == NULL)
	{
		stack_log("stack_osc_socket_create(): Invalid cue list\n");
		return NULL;
	}

	StackOSCSocket *osc_socket = new StackOSCSocket();
	osc_socket->bind_addr = strdup(bind_addr);
	osc_socket->bind_port = bind_port;
	osc_socket->socket = 0;
	osc_socket->address_prefix = strdup(address_prefix);
	osc_socket->cue_list = cue_list;
	osc_socket->running = true;
	osc_socket->thread = std::thread(stack_osc_socket_thread, (void*)osc_socket);

	return osc_socket;
}

void stack_osc_socket_destroy(StackOSCSocket *osc_socket)
{
	stack_log("stack_osc_socket_destroy(0x%016llx) called\n", osc_socket);
	// If we're running, set our flag and close the socket
	if (osc_socket->running)
	{
		stack_log("stack_osc_socket_destroy(0x%016llx): Telling thread to stop\n", osc_socket);
		osc_socket->running = false;
		if (osc_socket->socket != 0)
		{
			stack_log("stack_osc_socket_destroy(0x%016llx): Closing socket\n", osc_socket);
			shutdown(osc_socket->socket, SHUT_RDWR);
			close(osc_socket->socket);
			osc_socket->socket = 0;
		}

		// Wait for the thread to stop
		osc_socket->thread.join();
	}

	if (osc_socket->bind_addr)
	{
		free(osc_socket->bind_addr);
	}
	if (osc_socket->address_prefix)
	{
		free(osc_socket->address_prefix);
	}

	delete osc_socket;
}

const char *stack_osc_socket_get_bind_address(const StackOSCSocket *osc_socket)
{
	return osc_socket->bind_addr;
}

uint16_t stack_osc_socket_get_port(const StackOSCSocket *osc_socket)
{
	return osc_socket->bind_port;
}

const char *stack_osc_socket_get_address_prefix(const StackOSCSocket *osc_socket)
{
	return osc_socket->address_prefix;
}

bool stack_osc_socket_set_address_prefix(StackOSCSocket *osc_socket, const char *address_prefix)
{
	// Validate
	if (!stack_osc_socket_is_address_prefix_valid(address_prefix))
	{
		return false;
	}

	// Maintain a copy of the old address
	char *old_address_prefix = osc_socket->address_prefix;

	// Update the new address
	osc_socket->address_prefix = strdup(address_prefix);

	// Tidy up
	if (old_address_prefix)
	{
		free(old_address_prefix);
	}

	return true;
}

bool stack_osc_socket_is_address_prefix_valid(const char *address_prefix)
{
	size_t length = strlen(address_prefix);

	// Early checks
	if (length == 0 || address_prefix[0] != '/' || address_prefix[length - 1] != '/')
	{
		return false;
	}

	// Validate each character
	for (size_t i = 0; i < length; i++)
	{
		// Only allow characters in the ASCII printable range
		if ((unsigned char)address_prefix[i] < 32 || (unsigned char)address_prefix[i] > 127)
		{
			return false;
		}

		// All these printable characters are disallowed
		switch (address_prefix[i])
		{
			case ' ':
			case '#':
			case '*':
			case ',':
			case '?':
			case '[':
			case ']':
			case '{':
			case '}':
				return false;
				break;
		}
	}

	return true;
}
