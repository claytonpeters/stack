#ifndef _STACKOSCSOCKET_H_INCLUDED
#define _STACKOSCSOCKET_H_INCLUDED

// Circular dependency here:
struct StackCueList;

// Includes:
#include <thread>

// These are defined in StackOSCSocket.cpp
#ifndef _IN_STACKOSCSOCKET_CPP
extern const char STACK_OSC_PARAM_TYPE_INT;
extern const char STACK_OSC_PARAM_TYPE_FLOAT;
extern const char STACK_OSC_PARAM_TYPE_STRING;
#endif

// Defines
#define STACK_OSC_SOCKET(_p) ((StackOSCSocket*)(_p))

struct StackOSCParam
{
	union
	{
		int32_t as_int;
		float as_float;
		char *as_string;
	} data;
	char type;
};

struct StackOSCMessage
{
	char *address;
	size_t param_count;
	StackOSCParam* params;
};

struct StackOSCSocket
{
	// The address we're binding on
	char *bind_addr;

	// The port we're binding on
	uint16_t bind_port;

	// The datagram socket
	int socket;

	// The thread listening for soket
	std::thread thread;

	// Whether the thread is running
	bool running;

	// The OSC address prefix (usually just /)
	char *address_prefix;

	// The cue list we're operating on
	StackCueList *cue_list;
};

// Functions: Creation and destruction
StackOSCSocket *stack_osc_socket_create(const char *bind_addr, uint16_t bind_port, const char *address_prefix, StackCueList *cue_list);
void stack_osc_socket_destroy(StackOSCSocket *osc_socket);

// Functions: Getters and setters
const char *stack_osc_socket_get_bind_address(const StackOSCSocket *osc_socket);
uint16_t stack_osc_socket_get_port(const StackOSCSocket *osc_Socket);
const char *stack_osc_socket_get_address_prefix(const StackOSCSocket *osc_socket);
bool stack_osc_socket_set_address_prefix(StackOSCSocket *osc_socket, const char *address_prefix);

// Functions: helpers
bool stack_osc_socket_is_address_prefix_valid(const char *address_prefix);
void stack_osc_message_free(StackOSCMessage *message);

#endif
