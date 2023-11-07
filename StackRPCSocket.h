#if HAVE_LIBPROTOBUF_C == 1
#ifndef STACKRPCSOCKET_H_INCLUDED
#define STACKRPCSOCKET_H_INCLUDED

// We have a potential circular dependency
struct StackCueList;
struct StackRPCSocket;

// Includes:
#include <thread>
#include <vector>
#include <limits.h>
#include "StackCue.h"

typedef struct StackRPCSocketClient
{
	// Pointer to the owning server socket
	StackRPCSocket *rpc_socket;

	// The handle to the client socket
	int client_socket;

	// The thread running the client
	std::thread thread;

	// Whether the client has finished
	bool terminated;
} StackRPCSocketClient;

typedef struct StackRPCSocket
{
	// The path on the filesystem to the UNIX socket
	char path[PATH_MAX];

	// The handle to the UNIX socket
	int handle;

	// Thread for the listening socket
	std::thread listen_thread;

	// Vector of threads for clients
	std::vector<StackRPCSocketClient*> clients;

	// The cue list we're remotely controlling
	StackCueList* cue_list;

	// Whether the listen thread is running
	bool running;
} StackRPCSocket;

// Includes:
StackRPCSocket *stack_rpc_socket_create(const char *directory, const char *filename, StackCueList *cue_list);
void stack_rpc_socket_destroy(StackRPCSocket *rpc_socket);

#endif
#endif
