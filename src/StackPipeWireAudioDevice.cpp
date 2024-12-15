// Includes:
#include "StackPipeWireAudioDevice.h"
#include "StackLog.h"
#include <cstring>
#include <spa/param/audio/format-utils.h>
#include <vector>

typedef struct StackPipeWireConnection
{
	union
	{
		pw_main_loop *main;
		pw_thread_loop *threaded;
	} loop;
	pw_context *context;
	pw_core *core;
	bool loop_is_threaded;
} StackPipeWireConnection;

typedef struct StackPipeWireEnumerationData
{
	StackPipeWireConnection* connection;
	std::vector<StackAudioDeviceDesc> outputs;
} StackPipeWireEnumerationData;

// Globals:
int32_t open_streams = 0;
bool initialised = false;
StackPipeWireConnection *audio_connection = NULL;

// For device enumeration, kill the main loop once we recieve a done signal from
// the core sync
static void stack_pipewire_audio_device_core_done(void *user_data, uint32_t id, int seq)
{
	StackPipeWireEnumerationData *data = (StackPipeWireEnumerationData*)user_data;
	pw_main_loop_quit(data->connection->loop.main);
}

// For device enumeration, receive object information from the PipeWire registry
static void stack_pipewire_audio_device_registry_callback_global(void *user_data, uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props)
{
	// We're only interested in nodes (as that what a stream connects to)
	if (strcmp(type, PW_TYPE_INTERFACE_Node) != 0)
	{
		return;
	}

	// We're only interested in audio sinks
	const char *media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
	if (media_class == NULL || strcmp(media_class, "Audio/Sink") != 0)
	{
		return;
	}

	// Get the details we need about the device
	const char *node_name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
	const char *node_desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);

	// Create a device description and store in our array
	// Note that the strdup here is called by free_outputs later
	StackAudioDeviceDesc output;// = new StackAudioDeviceDesc;
	output.min_channels = 1;
	output.max_channels = 32;
	output.name = strdup(node_name);
	output.desc = strdup(node_desc);
	output.num_rates = 0;
	output.rates = NULL;

	StackPipeWireEnumerationData *data = (StackPipeWireEnumerationData*)user_data;
	data->outputs.push_back(output);
}

// Disconects from Pipewire
void stack_pipewire_audio_device_disconnect(StackPipeWireConnection *connection)
{
	if (connection->loop_is_threaded && connection->loop.threaded != NULL)
	{
		pw_thread_loop_lock(connection->loop.threaded);
	}

	if (connection->core != NULL)
	{
		pw_core_disconnect(connection->core);
	}

	if (connection->context != NULL)
	{
		pw_context_destroy(connection->context);
	}

	if (connection->loop_is_threaded)
	{
		if (connection->loop.threaded != NULL)
		{
			pw_thread_loop_unlock(connection->loop.threaded);

			// Quit the main loop, wait for the thread to finish, and the tidy it up
			pw_thread_loop_stop(connection->loop.threaded);
			pw_thread_loop_destroy(connection->loop.threaded);
		}
	}
	else
	{
		pw_main_loop_destroy(connection->loop.main);
	}

	delete connection;
}

// Connects to PipeWire, and sets up the main loop
StackPipeWireConnection* stack_pipewire_audio_device_connect(bool threaded)
{
	stack_log("stack_pipewire_audio_device_connect(): Connecting to PipeWire...\n");

	// Create a new connection object
	StackPipeWireConnection *connection = new StackPipeWireConnection;
	if (connection == NULL)
	{
		return NULL;
	}
	memset(connection, 0, sizeof(StackPipeWireConnection));

	// Create the main loop
	pw_loop *loop = NULL;
	connection->loop_is_threaded = threaded;
	if (threaded)
	{
		connection->loop.threaded = pw_thread_loop_new("Stack", NULL);
		if (connection->loop.threaded == NULL)
		{
			stack_log("stack_pipewire_audio_device_connect(): Failed to initalise PipeWire main loop\n");
			stack_pipewire_audio_device_disconnect(connection);
			return NULL;
		}
		loop = pw_thread_loop_get_loop(connection->loop.threaded);
	}
	else
	{
		connection->loop.main = pw_main_loop_new(NULL);
		if (connection->loop.main == NULL)
		{
			stack_log("stack_pipewire_audio_device_connect(): Failed to initalise PipeWire main loop\n");
			stack_pipewire_audio_device_disconnect(connection);
			return NULL;
		}
		loop = pw_main_loop_get_loop(connection->loop.main);
	}

	// Create a context
	connection->context = pw_context_new(loop, NULL, 0);
	if (connection->context == NULL)
	{
		stack_log("stack_pipewire_audio_device_connect(): Failed to create context\n");
		stack_pipewire_audio_device_disconnect(connection);
		return NULL;
	}

	// Connect the context
	connection->core = pw_context_connect(connection->context, NULL, 0);
	if (connection->core == NULL)
	{
		stack_log("stack_pipewire_audio_devicec_connect(): Failed to connect context\n");
		stack_pipewire_audio_device_disconnect(connection);
		return NULL;
	}

	return connection;
}

void stack_deinit_pipewire_audio()
{
	// Tidy up PipeWire
	if (initialised)
	{
		stack_log("stack_deinit_pipewire_audio(): Deinitialising PipeWire...\n");
		pw_deinit();
		initialised = false;
	}
}

// Initialises PipeWireAudio
void stack_init_pipewire_audio()
{
	if (initialised)
	{
		return;
	}

	// Debug
	stack_log("stack_init_pipewire_audio(): Initialising PipeWire...\n");

	// Initialise PipeWire
	pw_init(NULL, NULL);
	initialised = true;
}

size_t stack_pipewire_audio_device_list_outputs(StackAudioDeviceDesc **outputs)
{
	// Ensure PipeWire is initialised
	stack_init_pipewire_audio();

	StackPipeWireEnumerationData data;
	data.connection = stack_pipewire_audio_device_connect(false);
	if (data.connection == NULL)
	{
		*outputs = NULL;
		return 0;
	}

	// Get the registry
	pw_registry *registry = pw_core_get_registry(data.connection->core, PW_VERSION_REGISTRY, 0);
	if (registry == NULL)
	{
		stack_log("stack_pipewire_audio_device_list_outputs(): Failed to get registry proxy\n");
		stack_pipewire_audio_device_disconnect(data.connection);
		*outputs = NULL;
		return 0;
	}

	// Set up our callbacks for the registry
	static const struct pw_registry_events registry_events = {
		PW_VERSION_REGISTRY_EVENTS,
		.global = stack_pipewire_audio_device_registry_callback_global,
	};
	spa_hook registry_listener;
	spa_zero(registry_listener);
	pw_registry_add_listener(registry, &registry_listener, &registry_events, &data);

	// Set up our callbacks for the core
	static const struct pw_core_events core_events = {
		PW_VERSION_CORE_EVENTS,
		.done = stack_pipewire_audio_device_core_done,
	};
	struct spa_hook core_listener;
	pw_core_add_listener(data.connection->core, &core_listener, &core_events, &data);

	// Run a sync'd iteratopn of the loop, waiting for the registry events to finish
	pw_core_sync(data.connection->core, PW_ID_CORE, 0);
	pw_main_loop_run(data.connection->loop.main);

	// Disconnect the audio device
	stack_pipewire_audio_device_disconnect(data.connection);
	pw_proxy_destroy((struct pw_proxy*)registry);

	// Deinitialise PipeWire if this call was the only thing using it
	if (open_streams == 0)
	{
		stack_deinit_pipewire_audio();
	}

	// If we haven't found any outputs, return nothng
	size_t output_count = data.outputs.size() + 1;

	// Duplicate the outputs into the array, but set the first entry to "system default"
	StackAudioDeviceDesc *result = new StackAudioDeviceDesc[output_count + 1];
	result[0].min_channels = 1;
	result[0].max_channels = 32;
	result[0].name = strdup("");
	result[0].desc = strdup("System Default");
	result[0].num_rates = 0;
	result[0].rates = NULL;
	memcpy(&result[1], &data.outputs[0], output_count * sizeof(StackAudioDeviceDesc));

	// Return the result
	*outputs = result;
	return output_count;
}

void stack_pipewire_audio_device_free_outputs(StackAudioDeviceDesc **outputs, size_t count)
{
	for (size_t i = 0; i < count; i++)
	{
		free((*outputs)[i].name);
		free((*outputs)[i].desc);
		if ((*outputs)[i].rates != NULL)
		{
			delete [] (*outputs)[i].rates;
		}
	}
	delete [] *outputs;
}

void stack_pipewire_audio_device_destroy(StackAudioDevice *device)
{
	StackPipeWireAudioDevice *pdev = STACK_PIPEWIRE_AUDIO_DEVICE(device);

	// Debug
	stack_log("stack_pipewire_audio_device_destroy() called\n");

	// Stop our stream
	if (pdev->stream != NULL)
	{
		pw_thread_loop_lock(audio_connection->loop.threaded);
		pw_stream_destroy(pdev->stream);
		pdev->stream = NULL;
		pw_thread_loop_unlock(audio_connection->loop.threaded);
	}

	// Decrement the open streams and see if we need to tidy up now
	open_streams--;
	if (open_streams == 0)
	{
		stack_pipewire_audio_device_disconnect(audio_connection);
		audio_connection = NULL;
		stack_deinit_pipewire_audio();
	}

	// Call superclass destroy
	stack_audio_device_destroy_base(device);
}

static void stack_pipewire_audio_device_process_callback(void *user_data)
{
	StackPipeWireAudioDevice *device = STACK_PIPEWIRE_AUDIO_DEVICE(user_data);
	size_t channels = STACK_AUDIO_DEVICE(device)->channels;
	size_t stride = sizeof(float) * channels;

	pw_thread_loop_lock(audio_connection->loop.threaded);
	pw_buffer *pwb = pw_stream_dequeue_buffer(device->stream);
	pw_thread_loop_unlock(audio_connection->loop.threaded);
	if (pwb == NULL)
	{
		stack_log("stack_pipeire_audio_device_process_callback(): failed to get PipeWire buffer\n");
		return;
	}

	// Get a pointer to the buffer
	float *buffer = (float*)pwb->buffer->datas[0].data;
	if (buffer == NULL)
	{
		stack_log("stack_pipeire_audio_device_process_callback(): didn't get a valid buffer from PipeWire\n");
		return;
	}

	// Figure out how much audio to get from the cue list
	size_t writable_frames = 0;
	if (pwb->requested > 0)
	{
		writable_frames = (size_t)pwb->requested;
	}
	else
	{
		writable_frames = pwb->buffer->datas[0].maxsize / stride;
	}

	// Currently we're hardcded to float32
	size_t read = STACK_AUDIO_DEVICE(device)->request_audio(writable_frames, buffer, STACK_AUDIO_DEVICE(device)->request_audio_user_data);

	// Warn if we didn't get enough
	if (read < writable_frames)
	{
		stack_log("Buffer underflow: %lu < %lu\n", read, writable_frames);
	}

	// Update the buffer with the details of the data
	pwb->buffer->datas[0].chunk->offset = 0;
	pwb->buffer->datas[0].chunk->stride = stride;
	pwb->buffer->datas[0].chunk->size = read * stride;

	// Queue the buffer back in PipeWire
	pw_thread_loop_lock(audio_connection->loop.threaded);
	pw_stream_queue_buffer(device->stream, pwb);
	pw_thread_loop_unlock(audio_connection->loop.threaded);
}

StackAudioDevice *stack_pipewire_audio_device_create(const char *name, uint32_t channels, uint32_t sample_rate, stack_audio_device_audio_request_t request_audio, void *user_data)
{
	stack_log("stack_pipewire_audio_device_create(\"%s\", %u, %u, ...) called\n", name, channels, sample_rate);

	// Allocate the new device
	StackPipeWireAudioDevice *device = new StackPipeWireAudioDevice();
	device->stream = NULL;

	// Set up superclass
	STACK_AUDIO_DEVICE(device)->_class_name = "StackPipeWireAudioDevice";
	STACK_AUDIO_DEVICE(device)->channels = channels;
	STACK_AUDIO_DEVICE(device)->sample_rate = sample_rate;
	STACK_AUDIO_DEVICE(device)->request_audio = request_audio;
	STACK_AUDIO_DEVICE(device)->request_audio_user_data = user_data;

	// Ensure PipeWire is initialised
	stack_init_pipewire_audio();

	// Create the audio_connection if it doesn't exist
	if (audio_connection == NULL)
	{
		audio_connection = stack_pipewire_audio_device_connect(true);
		if (audio_connection == NULL)
		{
			return NULL;
		}

		// Kick off the thread loop
		pw_thread_loop_start(audio_connection->loop.threaded);
	}

	// Set te properties for the stream
	pw_properties *props = pw_properties_new(
		PW_KEY_MEDIA_TYPE, "Audio",
		PW_KEY_MEDIA_CATEGORY, "Playback",
		PW_KEY_MEDIA_ROLE, "Music",
		NULL);

	// If we've been given a specific device name, add it
	if (name == NULL || strlen(name) == 0)
	{
		stack_log("stack_pipewire_audio_device_create(): Using default sink\n");
	}
	else
	{
		stack_log("stack_pipewire_audio_device_create(): Using device %s\n", name);
		pw_properties_set(props, PW_KEY_TARGET_OBJECT, name);
	}

	static const pw_stream_events stream_events = {
		PW_VERSION_STREAM_EVENTS,
		.process = stack_pipewire_audio_device_process_callback,
	};

	// Create the stream
	pw_thread_loop_lock(audio_connection->loop.threaded);
	device->stream = pw_stream_new_simple(pw_thread_loop_get_loop(audio_connection->loop.threaded), "Stack", props, &stream_events, (void*)device);
	pw_thread_loop_unlock(audio_connection->loop.threaded);
	if (device->stream == NULL)
	{
		stack_log("stack_pipewire_audio_device_create(): Failed to create PipeWire stream\n");
		delete device;
		return NULL;
	}

	// Increment the count of open streams
	open_streams++;

	// Prepare the sample format
	uint8_t spa_buffer[1024];
	const struct spa_pod *spa_params[1];
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(spa_buffer, sizeof(spa_buffer));
	struct spa_audio_info_raw audio_info = SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_F32, .rate = sample_rate, .channels = channels);
	spa_params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &audio_info);

	// Connect the stream
	stack_log("stack_pipewire_audio_device_create(): Connecting playback stream...\n");
	pw_thread_loop_lock(audio_connection->loop.threaded);
	int connect_result = pw_stream_connect(device->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY, (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS /*| PW_STREAM_FLAG_RT_PROCESS*/), spa_params, 1);
	pw_thread_loop_unlock(audio_connection->loop.threaded);
	if (connect_result < 0)
	{
		stack_log("stack_pipewire_audio_device_create(): Failed to connect stream: %d\n", connect_result);
	}

	// Wait for the stream to become ready (max of a second)
	const char *stream_error = NULL;
	int16_t timer = 0;
	pw_stream_state state = pw_stream_get_state(device->stream, &stream_error);
	while (state != PW_STREAM_STATE_STREAMING && timer < 20)
	{
		stack_log("stack_pipewire_audio_device_create(): Stream is not yet ready: %d\n", state);
		if (state == PW_STREAM_STATE_ERROR && stream_error != NULL)
		{
			stack_log("stack_pipewire_audio_device_create(): Stream reports error: %s\n", stream_error);
		}

		// Wait and increment timer
		usleep(50000);
		timer++;

		// Get new state
		state = pw_stream_get_state(device->stream, &stream_error);
	}

	if (state != PW_STREAM_STATE_STREAMING)
	{
		stack_log("stack_pipewire_audio_device_create(): Warning: Stream is still not ready\n");
	}
	else
	{
		stack_log("stack_pipewire_audio_device_create(): Stream connected\n");
	}

	// Return the newly created device
	return STACK_AUDIO_DEVICE(device);
}

const char *stack_pipewire_audio_device_get_friendly_name()
{
	return "Stack PipeWire Provider";
}

void stack_pipewire_audio_device_register()
{
	// Register the PipeWire audio class
	StackAudioDeviceClass* pipewire_audio_device_class = new StackAudioDeviceClass{ "StackPipeWireAudioDevice", "StackPipeWireAudioDevice", stack_pipewire_audio_device_list_outputs, stack_pipewire_audio_device_free_outputs, stack_pipewire_audio_device_create, stack_pipewire_audio_device_destroy, stack_pipewire_audio_device_get_friendly_name };
	stack_register_audio_device_class(pipewire_audio_device_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_pipewire_audio_device_register();
	return true;
}

