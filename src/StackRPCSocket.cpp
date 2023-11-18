#if HAVE_LIBPROTOBUF_C == 1

// Includes:
#include "StackRPCSocket.h"
#include "StackRPC.pb-c.h"
#include "StackLog.h"
#include "StackCue.h"
#include <unistd.h>
#include <stdlib.h>
#include <cstdio>
#include <sys/socket.h>
#include <sys/un.h>

// Helper funciton that populates a stackrpc.V1.CueInfo protobuf object with the
// details about a given cue
// @param cue The StackCue object that is the source of the cue information
// @param cue_info The stackrpc.v1.CueInfo protobuf object to populate
static void stack_rpc_socket_fill_cue_info(StackCue *cue, StackRPC__V1__CueInfo *cue_info)
{
	cue_info->uid = cue->uid;
	cue_info->parent_uid = 0;
	cue_info->id = cue->id;

	// Populate cue state
	switch (cue->state)
	{
		case STACK_CUE_STATE_ERROR:
			cue_info->state = STACK_RPC__V1__CUE_STATE__Error;
			break;
		case STACK_CUE_STATE_STOPPED:
			cue_info->state = STACK_RPC__V1__CUE_STATE__Stopped;
			break;
		case STACK_CUE_STATE_PAUSED:
			cue_info->state = STACK_RPC__V1__CUE_STATE__Paused;
			break;
		case STACK_CUE_STATE_PREPARED:
			cue_info->state = STACK_RPC__V1__CUE_STATE__Prepared;
			break;
		case STACK_CUE_STATE_PLAYING_PRE:
			cue_info->state = STACK_RPC__V1__CUE_STATE__PlayingPre;
			break;
		case STACK_CUE_STATE_PLAYING_ACTION:
			cue_info->state = STACK_RPC__V1__CUE_STATE__PlayingAction;
			break;
		case STACK_CUE_STATE_PLAYING_POST:
			cue_info->state = STACK_RPC__V1__CUE_STATE__PlayingPost;
			break;
	}

	// Note that we strdup this because it's returned as a const char
	cue_info->name = strdup(stack_cue_get_rendered_name(cue));

	// Populate times
	stack_property_get_int64(stack_cue_get_property(cue, "pre_time"), STACK_PROPERTY_VERSION_DEFINED, &cue_info->prewait_time);
	stack_property_get_int64(stack_cue_get_property(cue, "action_time"), STACK_PROPERTY_VERSION_DEFINED, &cue_info->action_time);
	stack_property_get_int64(stack_cue_get_property(cue, "post_time"), STACK_PROPERTY_VERSION_DEFINED, &cue_info->postwait_time);

	// Populate trigger
	int32_t post_trigger = 0;
	stack_property_get_int32(stack_cue_get_property(cue, "post_trigger"), STACK_PROPERTY_VERSION_DEFINED, &post_trigger);
	switch (post_trigger)
	{
		case STACK_CUE_WAIT_TRIGGER_NONE:
			cue_info->post_trigger = STACK_RPC__V1__CUE_WAIT_TRIGGER__None;
			break;
		case STACK_CUE_WAIT_TRIGGER_IMMEDIATE:
			cue_info->post_trigger = STACK_RPC__V1__CUE_WAIT_TRIGGER__Immediate;
			break;
		case STACK_CUE_WAIT_TRIGGER_AFTERPRE:
			cue_info->post_trigger = STACK_RPC__V1__CUE_WAIT_TRIGGER__AfterPre;
			break;
		case STACK_CUE_WAIT_TRIGGER_AFTERACTION:
			cue_info->post_trigger = STACK_RPC__V1__CUE_WAIT_TRIGGER__AfterAction;
			break;
	}

	// Populate notes - note that we don't strdup this
	stack_property_get_string(stack_cue_get_property(cue, "notes"), STACK_PROPERTY_VERSION_DEFINED, &cue_info->notes);

	// Populate the colour as a 24-bit packed RGB integer, as protobuf doesn't
	// have an 8-bit unsigned integer type
	uint8_t r = 0, g = 0, b = 0;
	stack_property_get_uint8(stack_cue_get_property(cue, "r"), STACK_PROPERTY_VERSION_DEFINED, &r);
	stack_property_get_uint8(stack_cue_get_property(cue, "g"), STACK_PROPERTY_VERSION_DEFINED, &g);
	stack_property_get_uint8(stack_cue_get_property(cue, "b"), STACK_PROPERTY_VERSION_DEFINED, &b);
	cue_info->colour = (((int32_t)r) << 16) | (((int32_t)g) << 8) | ((int32_t)b);
}

// Sends a ControlResponse message back to a client
// @param client The StackRPCSocketClient object that contains the details for
// the client that we're responding to
// @param response The stackrpc.v1.ControlResponse message to pack and send
static void stack_rpc_socket_send_response(StackRPCSocketClient *client, StackRPC__V1__ControlResponse *response)
{
	size_t packed_size = protobuf_c_message_get_packed_size((const ProtobufCMessage*)response);
	if (packed_size > 0)
	{
		uint8_t *buffer = new uint8_t[packed_size];
		protobuf_c_message_pack((ProtobufCMessage*)response, buffer);
		ssize_t bytes_sent = send(client->client_socket, buffer, packed_size, 0);
		delete [] buffer;
	}
	else
	{
		stack_log("stack_rpc_socket_send_response(%lx): Failed to pack response: %d\n", client, packed_size);
	}
}

// Handles Get Show messages, sending the data back to the client
// @param client The StackRPCSocketClient object that is the client connection
// @param message The inbound stackrpc.v1.ControlRequest message
// @param response A ready-to-use stackrpc.v1.ControlResponse response message
static void stack_rpc_socket_handle_get_show(StackRPCSocketClient *client, StackRPC__V1__ControlRequest *message, StackRPC__V1__ControlResponse *response)
{
	stack_log("stack_rpc_socket_handle_get_show(%lx): Called\n", client);

	StackRPC__V1__GetShowResponse get_show_response;
	protobuf_c_message_init(&stack_rpc__v1__get_show_response__descriptor, &get_show_response);
	response->type_case = STACK_RPC__V1__CONTROL_RESPONSE__TYPE_GET_SHOW_RESPONSE;
	response->get_show_response = &get_show_response;

	get_show_response.name = client->rpc_socket->cue_list->show_name;
	get_show_response.designer = client->rpc_socket->cue_list->show_designer;
	get_show_response.revision = client->rpc_socket->cue_list->show_revision;
	get_show_response.filename = client->rpc_socket->cue_list->uri;

	// Send the response
	stack_rpc_socket_send_response(client, response);
}

// Handles List Cue messages, sending the list of cue UIDs to the client
// @param client The StackRPCSocketClient object that is the client connection
// @param message The inbound stackrpc.v1.ControlRequest message
// @param response A ready-to-use stackrpc.v1.ControlResponse response message
static void stack_rpc_socket_handle_list_cues(StackRPCSocketClient *client, StackRPC__V1__ControlRequest *message, StackRPC__V1__ControlResponse *response)
{
	stack_log("stack_rpc_socket_handle_list_cues(%lx): Called\n", client);

	StackRPC__V1__ListCuesResponse list_cues_response;
	protobuf_c_message_init(&stack_rpc__v1__list_cues_response__descriptor, &list_cues_response);
	response->type_case = STACK_RPC__V1__CONTROL_RESPONSE__TYPE_LIST_CUES_RESPONSE;
	response->list_cues_response = &list_cues_response;

	list_cues_response.n_cue_uid = stack_cue_list_count(client->rpc_socket->cue_list);
	list_cues_response.cue_uid = new int64_t[list_cues_response.n_cue_uid];

	stack_cue_list_lock(client->rpc_socket->cue_list);
	void *iter = stack_cue_list_iter_front(client->rpc_socket->cue_list);
	size_t index = 0;
	while (!stack_cue_list_iter_at_end(client->rpc_socket->cue_list, iter))
	{
		list_cues_response.cue_uid[index] = stack_cue_list_iter_get(iter)->uid;
		stack_cue_list_iter_next(iter);
		index++;
	}
	stack_cue_list_iter_free(iter);
	stack_cue_list_unlock(client->rpc_socket->cue_list);

	// Send the response
	stack_rpc_socket_send_response(client, response);

	// Tidy up
	delete [] list_cues_response.cue_uid;
}

// Handles Get Cue messages, sending the appropriate data to the client
// @param client The StackRPCSocketClient object that is the client connection
// @param message The inbound stackrpc.v1.ControlRequest message
// @param response A ready-to-use stackrpc.v1.ControlResponse response message
static void stack_rpc_socket_handle_get_cues(StackRPCSocketClient *client, StackRPC__V1__ControlRequest *message, StackRPC__V1__ControlResponse *response)
{
	stack_log("stack_rpc_socket_handle_get_cues(%lx): Called\n", client);

	StackRPC__V1__GetCuesResponse get_cues_response;
	protobuf_c_message_init(&stack_rpc__v1__get_cues_response__descriptor, &get_cues_response);
	response->type_case = STACK_RPC__V1__CONTROL_RESPONSE__TYPE_GET_CUES_RESPONSE;
	response->get_cues_response = &get_cues_response;

	// Determine how many cues we find to respond with
	size_t count = 0;
	for (size_t i = 0; i < message->get_cues_request->n_cue_uid; i++)
	{
		StackCue *cue = stack_cue_get_by_uid(message->get_cues_request->cue_uid[i]);
		if (cue != NULL)
		{
			count++;
		}
	}

	stack_log("stack_rpc_socket_handle_get_cues(%lx): Will respond with %d out of %d cues\n", client, count, message->get_cues_request->n_cue_uid);

	get_cues_response.n_cue_info = count;
	if (count > 0)
	{
		get_cues_response.cue_info = new StackRPC__V1__CueInfo*[count];
		memset(get_cues_response.cue_info, 0, sizeof(StackRPC__V1__CueInfo*) * count);

		for (size_t in = 0, out = 0; in < message->get_cues_request->n_cue_uid; in++)
		{
			StackCue *cue = stack_cue_get_by_uid(message->get_cues_request->cue_uid[in]);
			if (cue != NULL)
			{
				get_cues_response.cue_info[out] = new StackRPC__V1__CueInfo;
				protobuf_c_message_init(&stack_rpc__v1__cue_info__descriptor, get_cues_response.cue_info[out]);
				stack_rpc_socket_fill_cue_info(cue, get_cues_response.cue_info[out]);
				out++;
			}
		}
	}
	else
	{
		get_cues_response.cue_info = NULL;
	}

	// Send the response
	stack_rpc_socket_send_response(client, response);

	// Tidy up
	for (size_t i = 0; i < get_cues_response.n_cue_info; i++)
	{
		// From strdups in stack_rpc_socket_fill_cue_info
		free(get_cues_response.cue_info[i]->name);
		delete get_cues_response.cue_info[i];
	}
	delete [] get_cues_response.cue_info;
}

// Handles Cue Action messages, calling the appropriate cue list function
// @param client The StackRPCSocketClient object that is the client connection
// @param message The inbound stackrpc.v1.ControlRequest message
// @param response A ready-to-use stackrpc.v1.ControlResponse response message
static void stack_rpc_socket_handle_cue_action(StackRPCSocketClient *client, StackRPC__V1__ControlRequest *message, StackRPC__V1__ControlResponse *response)
{
	stack_log("stack_rpc_socket_handle_cue_action(%lx): Called\n", client);

	StackRPC__V1__CueActionResponse cue_action_response;

	protobuf_c_message_init(&stack_rpc__v1__cue_action_response__descriptor, &cue_action_response);

	response->type_case = STACK_RPC__V1__CONTROL_RESPONSE__TYPE_CUE_ACTION_RESPONSE;
	response->cue_action_response = &cue_action_response;

	// StopAll is a special case that doesn't require a valid cue UID
	if (message->cue_action_request->cue_action == STACK_RPC__V1__CUE_ACTION__StopAll)
	{
		stack_cue_list_stop_all(client->rpc_socket->cue_list);
	}
	else
	{
		// TODO: THIS SHOULD BE CUE_UID!!
		StackCue *cue = stack_cue_get_by_uid(message->cue_action_request->cue_id);
		if (cue != NULL)
		{
			switch (message->cue_action_request->cue_action)
			{
				case STACK_RPC__V1__CUE_ACTION__Play:
					stack_cue_play(cue);
					break;
				case STACK_RPC__V1__CUE_ACTION__Pause:
					stack_cue_pause(cue);
					break;
				case STACK_RPC__V1__CUE_ACTION__Stop:
					stack_cue_stop(cue);
					break;
				default:
					stack_log("stack_rpc_socket_handle_cue_action(%lx): Unknown action %d\n", client, message->cue_action_request->cue_action);
					break;
			}
		}
		else
		{
			stack_log("stack_rpc_socket_handle_cue_action(%lx): Request for unknown cue %lx\n", client, message->cue_action_request->cue_id);
		}
	}

	// Send the response
	stack_rpc_socket_send_response(client, response);
}

// Thread that waits for and handles messages from a client
// @param data The StackRCPSocketClient object that this thread is handling
// messages for
static void stack_rpc_socket_client_thread(void *data)
{
	StackRPCSocketClient *client = (StackRPCSocketClient*)data;
	stack_log("stack_rpc_socket_client_thread(%lx): Thread started\n", client);

	uint8_t buffer[128];
	while (client->rpc_socket->running)
	{
		memset(buffer, 0, sizeof(buffer));
		ssize_t bytes_read = recv(client->client_socket, buffer, 128, 0);
		if (bytes_read <= 0)
		{
			stack_log("stack_rpc_socket_client_thread(%lx): Socket closing\n", client);
			break;
		}

		StackRPC__V1__ControlRequest *message = (StackRPC__V1__ControlRequest*)protobuf_c_message_unpack(&stack_rpc__v1__control_request__descriptor, NULL, (size_t)bytes_read, buffer);
		if (message == NULL)
		{
			stack_log("stack_rpc_socket_client_thread(%lx): Failed to decode incoming message\n", client);
			break;
		}

		// Start building the response
		StackRPC__V1__ControlResponse response;
		protobuf_c_message_init(&stack_rpc__v1__control_response__descriptor, &response);
		response.response_to = message->message_id;

		switch (message->type_case)
		{
			case STACK_RPC__V1__CONTROL_REQUEST__TYPE__NOT_SET:
				stack_log("stack_rpc_socket_client_thread(%lx): Message had no 'type' set\n", client);
				break;
			case STACK_RPC__V1__CONTROL_REQUEST__TYPE_CUE_ACTION_REQUEST:
				stack_rpc_socket_handle_cue_action(client, message, &response);
				break;
			case STACK_RPC__V1__CONTROL_REQUEST__TYPE_LIST_CUES_REQUEST:
				stack_rpc_socket_handle_list_cues(client, message, &response);
				break;
			case STACK_RPC__V1__CONTROL_REQUEST__TYPE_GET_CUES_REQUEST:
				stack_rpc_socket_handle_get_cues(client, message, &response);
				break;
			case STACK_RPC__V1__CONTROL_REQUEST__TYPE_GET_SHOW_REQUEST:
				stack_rpc_socket_handle_get_show(client, message, &response);
				break;
			default:
				stack_log("stack_rpc_socket_client_thread(%lx): Message had invalid 'type'\n", client);
				break;
		}

		// Tidy up
		protobuf_c_message_free_unpacked((ProtobufCMessage*)message, NULL);
	}

	// Tidy up the socket
	shutdown(client->client_socket, SHUT_RDWR);
	close(client->client_socket);

	// We can't delete ourselves, so mark that we're terminated - the listen thread
	// can tidy up
	client->terminated = true;

	stack_log("stack_rpc_socket_client_thread(%lx): Thread exited\n", client);
}

// Thread that accepts new connections on the server socket
// @param data A pointer to the StackRPCSocket object that the thread is running
// for
static void stack_rpc_socket_listen_thread(void *data)
{
	StackRPCSocket *rpc_socket = (StackRPCSocket*)data;
	stack_log("stack_rpc_socket_listen_thread(): Thread started\n");

	while (rpc_socket->running)
	{
		int client_socket = accept(rpc_socket->handle, NULL, NULL);
		if (client_socket <= 0)
		{
			stack_log("stack_rpc_socket_listen_thread(): Failed to accept new RPC client: errno %d\n", errno);
			continue;
		}

		stack_log("stack_rpc_socket_listen_thread(): New connection\n");
		StackRPCSocketClient *client = new StackRPCSocketClient;
		client->rpc_socket = rpc_socket;
		client->client_socket = client_socket;
		client->terminated = false;
		client->thread = std::thread(stack_rpc_socket_client_thread, (void*)(int64_t)client);
		rpc_socket->clients.push_back(client);

		// Tidy up any terminated clients
		for (auto iter = rpc_socket->clients.begin(); iter != rpc_socket->clients.end(); iter++)
		{
			if ((*iter)->terminated)
			{
				stack_log("stack_rpc_socket_listen_thread(): Tidying up old client %lx\n", *iter);

				// Join the thread to ensure it's finished (as so we don't terminate() the thread
				// when calling delete)
				(*iter)->thread.join();
				delete *iter;
				rpc_socket->clients.erase(iter);
			}
		}
	}

	stack_log("stack_rpc_socket_listen_thread(): Thread exited\n");
}

// Creates a new StackRPCSocket object, and starts it listening, with a thread
// waiting to accept connections
// @param directory The directory to create the UNIX socket in. If NULL, the
// contents of the XDG_RUNTIME_DIR environment variable is used. If that is not
// set then the socket is created in /tmp
// @param filename The filename of the UNIX socket. If not given, a filename is
// generated using the PID of the process and the address of the cue list in the
// format stack_<pid>_<address>.sock
// @param cue_list The cue list to associte with the socket
StackRPCSocket *stack_rpc_socket_create(const char *directory, const char *filename, StackCueList *cue_list)
{
	// We must have a cue list
	if (cue_list == NULL)
	{
		stack_log("stack_rpc_socket_create(): No cue list passed\n");
		return NULL;
	}

	// If no directory is given, default to the XDG runtime directory path as
	// given by environment variable
	if (directory == NULL)
	{
		directory = getenv("XDG_RUNTIME_DIR");

		// If we've still not got a path, default to /tmp
		if (directory == NULL)
		{
			directory = "/tmp";
		}
	}

	StackRPCSocket *rpc_socket = new StackRPCSocket();

	if (filename != NULL)
	{
		snprintf(rpc_socket->path, PATH_MAX, "%s/%s", directory, filename);
	}
	else
	{
		snprintf(rpc_socket->path, PATH_MAX, "%s/stack_%u_%lx.sock", directory, getpid(), (uint64_t)cue_list);
	}

	// Create the socket
	stack_log("stack_rpc_socket_create(): Creating RPC socket at %s\n", rpc_socket->path);

	rpc_socket->handle = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (rpc_socket->handle <= 0)
	{
		stack_log("stack_rpc_socket_create(): Failed to create UNIX socket: errno %d\n", errno);
		delete rpc_socket;
		return NULL;
	}

	// Bind the socket to the path
	struct sockaddr_un sock_name;
	memset(&sock_name, 0, sizeof(sock_name));
	sock_name.sun_family = AF_UNIX;
	strncpy(sock_name.sun_path, rpc_socket->path, sizeof(sock_name.sun_path) -1 );
	int bind_result = bind(rpc_socket->handle, (const struct sockaddr*)&sock_name, sizeof(sock_name));
	if (bind_result == -1)
	{
		stack_log("stack_rpc_socket_create(): Failed to bind UNIX socket, errno %d\n", errno);
		close(rpc_socket->handle);
		delete rpc_socket;
		return NULL;
	}

	// Start the socket listening
	int listen_res = listen(rpc_socket->handle, 20);
	if (listen_res < 0)
	{
		stack_log("stack_rpc_socket_create(): Failed to listen on UNIX socket, errno %d\n", errno);
		close(rpc_socket->handle);
		delete rpc_socket;
		return NULL;
	}

	rpc_socket->running = true;
	stack_log("stack_rpc_socket_create(): Starting thread for 0x%lx\n", rpc_socket);
	rpc_socket->listen_thread = std::thread(stack_rpc_socket_listen_thread, (void*)rpc_socket);
	rpc_socket->clients = std::vector<StackRPCSocketClient*>();
	rpc_socket->cue_list = cue_list;

	return rpc_socket;
}

// Destroys a StackRPCSocket object, stopping the thread and closing the socket
// @param rpc_socket The socket
void stack_rpc_socket_destroy(StackRPCSocket *rpc_socket)
{
	stack_log("stack_rpc_socket_destroy(): Called for 0x%lx\n", rpc_socket);
	if (rpc_socket == NULL)
	{
		return;
	}

	// Tell the listen thread to stop accepting
	rpc_socket->running = false;

	// Close the listen socket (should also stop the thread)
	if (rpc_socket->handle)
	{
		stack_log("stack_rpc_socket_destroy(): Closing socket\n");
		shutdown(rpc_socket->handle, SHUT_RDWR);
		close(rpc_socket->handle);
	}

	// Tidy up the socket file
	stack_log("stack_rpc_socket_destroy(): Removing socket %s\n", rpc_socket->path);
	unlink(rpc_socket->path);

	// Join the listen thread for it to exit
	stack_log("stack_rpc_socket_destroy(): Waiting for thread to terminate\n");
	rpc_socket->listen_thread.join();

	// Delete ourselves
	delete rpc_socket;
}

#endif
