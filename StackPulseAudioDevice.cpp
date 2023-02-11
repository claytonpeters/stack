// Includes:
#include "StackPulseAudioDevice.h"
#include <cstring>

// Globals:
pa_threaded_mainloop *mainloop = NULL;
pa_mainloop_api *mainloop_api = NULL;
pa_context *context = NULL;
semaphore state_semaphore;
char *default_sink_name = NULL;

// Device enumeration structures:
typedef struct PulseAudioSinkCountData
{
	semaphore sink_semaphore;
	size_t count;
} PulseAudioSinkCountData;

typedef struct PulseAudioSinkEnumData
{
	semaphore sink_semaphore;
	size_t count;
	StackAudioDeviceDesc* devices;
} PulseAudioSinkEnumData;

// PULSEAUDIO CALLBACK: Called by PulseAudio on state change
void stack_pulse_audio_notify_callback(pa_context* context, void* userdata)
{
	pa_context_state_t state = pa_context_get_state(context);

	switch (state)
	{
		case PA_CONTEXT_CONNECTING:
			fprintf(stderr, "stack_pulse_audio_notify_callback(): Connecting to PulseAudio...\n");
			break;
		case PA_CONTEXT_AUTHORIZING:
			fprintf(stderr, "stack_pulse_audio_notify_callback(): Authorising...\n");
			break;
		case PA_CONTEXT_SETTING_NAME:
			fprintf(stderr, "stack_pulse_audio_notify_callback(): Setting name...\n");
			break;
		case PA_CONTEXT_READY:
			fprintf(stderr, "stack_pulse_audio_notify_callback(): PulseAudio context ready\n");
			if (userdata != NULL) { ((semaphore*)userdata)->notify(); }
			break;
		case PA_CONTEXT_FAILED:
			fprintf(stderr, "stack_pulse_audio_notify_callback(): PulseAudio connection failed\n");
			if (userdata != NULL) { ((semaphore*)userdata)->notify(); }
			break;
		case PA_CONTEXT_TERMINATED:
			fprintf(stderr, "stack_pulse_audio_notify_callback(): PulseAudio connection terminated\n");
			if (userdata != NULL) { ((semaphore*)userdata)->notify(); }
			break;
		default:
			fprintf(stderr, "stack_pulse_audio_notify_callback(): Unknown state!\n");
			if (userdata != NULL) { ((semaphore*)userdata)->notify(); }
			break;
	}
}

// PULSEAUDIO CALLBACK: Free a buffer passed to PulseAudio
void stack_pulse_audio_device_free_audio_buffer(void *p)
{
	delete [] (float*)p;
}

void stack_pulse_audio_device_write(StackAudioDevice *device, const char *data, size_t bytes, bool lock = true, bool free_callback = false)
{
	if (lock)
	{
		pa_threaded_mainloop_lock(mainloop);
	}
	pa_stream_write(STACK_PULSE_AUDIO_DEVICE(device)->stream, data, bytes, free_callback ? stack_pulse_audio_device_free_audio_buffer : NULL, 0, PA_SEEK_RELATIVE);
	if (lock)
	{
		pa_threaded_mainloop_unlock(mainloop);
	}
}

// PULSEAUDIO CALLBACK: Called by PulseAudio on state change
void stack_pulse_audio_stream_underflow_callback(pa_context* context, void* userdata)
{
	// Determine how many bytes PulseAudio wants
	size_t writable = pa_stream_writable_size(STACK_PULSE_AUDIO_DEVICE(userdata)->stream);
	const size_t channels = STACK_AUDIO_DEVICE(userdata)->channels;

	// Whilst it wants more
	while (writable != (size_t)-1 && writable > 0)
	{
		// Determine how many samples per channel (rather than bytes) PulseAudio wants
		size_t writable_samples = writable / sizeof(float) / channels;

		// Get a buffer of that size and read (up to) that many bytes
		float *buffer = new float[writable_samples * channels];
		size_t read = STACK_AUDIO_DEVICE(userdata)->request_audio(writable_samples, buffer, STACK_AUDIO_DEVICE(userdata)->request_audio_user_data);

		if (read < writable_samples)
		{
			fprintf(stderr, "Buffer underflow: %lu < %lu!\n", read, writable_samples);
		}

		// Write the data to PulseAudio
		stack_pulse_audio_device_write(&STACK_PULSE_AUDIO_DEVICE(userdata)->super, (char*)buffer, read * channels * sizeof(float), false, true);

		// Determine if PulseAudio still wants more
		writable = pa_stream_writable_size(STACK_PULSE_AUDIO_DEVICE(userdata)->stream);

		// Note: we don't free "buffer" here, as PulseAudio callback will free for us
	}

}

// PULSEAUDIO CALLBACK: Called by PulseAudio when counting sinks
void stack_pulse_audio_sink_info_count_callback(pa_context* context, pa_sink_info* sinkinfo, int eol, void* userdata)
{
	if (userdata != NULL)
	{
		if (sinkinfo == NULL && eol != 0)
		{
			((PulseAudioSinkCountData*)userdata)->sink_semaphore.notify();
			return;
		}

		((PulseAudioSinkCountData*)userdata)->count++;
	}
}

// PULSEAUDIO CALLBACK: Called by PulseAudio when collecting sink info
void stack_pulse_audio_sink_info_callback(pa_context* context, pa_sink_info* sinkinfo, int eol, void* userdata)
{
	// For easiness:
	PulseAudioSinkEnumData* pased = (PulseAudioSinkEnumData*)userdata;

	if (pased != NULL)
	{
		if (sinkinfo == NULL && eol != 0)
		{
			pased->sink_semaphore.notify();
			return;
		}

		if (sinkinfo)
		{
			// Store the information
			pased->devices[pased->count].name = strdup(sinkinfo->name);
			pased->devices[pased->count].desc = strdup(sinkinfo->description);
			pased->devices[pased->count].min_channels = 1;
			pased->devices[pased->count].max_channels = sinkinfo->channel_map.channels;
			pased->devices[pased->count].num_rates = 1;	// I *think* PulseAudio only provides one rate per device
			pased->devices[pased->count].rates = new uint32_t[1];
			pased->devices[pased->count].rates[0] = sinkinfo->sample_spec.rate;

			// Increment the count
			pased->count++;
		}
	}

	return;
}

// PULSEAUDIO CALLBACK: Stream state change
void stack_pulse_audio_stream_notify_callback(pa_stream* stream, void *userdata)
{
	// Get the device
	StackPulseAudioDevice *device = STACK_PULSE_AUDIO_DEVICE(userdata);

	// We don't do a great deal if we haven't got a device
	if (device == NULL)
	{
		return;
	}

	// Get the state
	pa_stream_state state = pa_stream_get_state(stream);

	switch (state)
	{
		case PA_STREAM_UNCONNECTED:
			fprintf(stderr, "stack_pulse_audio_stream_notify_callback(): Stream unconnected\n");
			break;
		case PA_STREAM_CREATING:
			fprintf(stderr, "stack_pulse_audio_stream_notify_callback(): Creating stream\n");
			break;
		case PA_STREAM_READY:
			fprintf(stderr, "stack_pulse_audio_stream_notify_callback(): Stream ready\n");
			device->sync_semaphore.notify();
			break;
		case PA_STREAM_FAILED:
			fprintf(stderr, "stack_pulse_audio_stream_notify_callback(): Stream failed\n");
			device->sync_semaphore.notify();
			break;
		case PA_STREAM_TERMINATED:
			fprintf(stderr, "stack_pulse_audio_stream_notify_callback(): Stream terminated\n");
			device->sync_semaphore.notify();
			break;
		default:
			fprintf(stderr, "stack_pulse_audio_stream_notify_callback(): Stream unknown state: %d!\n", state);
			device->sync_semaphore.notify();
			break;
	}
}

// PULSEAUDIO CALLBACK: Server info callback
void stack_pulse_audio_server_info_callback(pa_context *context, const pa_server_info *info, void *userdata)
{
	// Get the device
	StackPulseAudioDevice *device = STACK_PULSE_AUDIO_DEVICE(userdata);

	// We don't do a great deal if we haven't got a device
	if (device == NULL)
	{
		return;
	}

	// Get the default sink name if we don't already have it
	if (default_sink_name == NULL)
	{
		// Store this in our global
		default_sink_name = strdup(info->default_sink_name);
	}

	// Notify the creation to continue
	device->sync_semaphore.notify();
}

// Initialises PulseAudio
bool stack_init_pulse_audio()
{
	// Don't initialise if we're already initialised
	if (mainloop != NULL)
	{
		return true;
	}

	// Debug
	fprintf(stderr, "stack_init_pulse_audio(): Initialising PulseAudio...\n");

	// Initialise the main loop and the context
	mainloop = pa_threaded_mainloop_new();
	if (mainloop == NULL)
	{
		return false;
	}
	mainloop_api = pa_threaded_mainloop_get_api(mainloop);
	if  (mainloop_api == NULL)
	{
		pa_threaded_mainloop_free(mainloop);
		return false;
	}
	context = pa_context_new(mainloop_api, "Stack");
	if (context == NULL)
	{
		pa_threaded_mainloop_free(mainloop);
		return false;
	}

	// Connect and start the main loop
	pa_context_set_state_callback(context, (pa_context_notify_cb_t)stack_pulse_audio_notify_callback, &state_semaphore);
	pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL);
	pa_threaded_mainloop_start(mainloop);

	// Wait for connection
	state_semaphore.wait();

	// Get the state of the context
	pa_threaded_mainloop_lock(mainloop);
	pa_context_state_t state = pa_context_get_state(context);
	pa_threaded_mainloop_unlock(mainloop);

	// Exit if we're in an unknown state
	if (state != PA_CONTEXT_READY)
	{
		fprintf(stderr, "stack_init_pulse_audio(): Failed to connect");
		pa_threaded_mainloop_stop(mainloop);
		pa_threaded_mainloop_free(mainloop);
		mainloop = NULL;
		return false;
	}

	return true;
}

size_t stack_pulse_audio_device_list_outputs(StackAudioDeviceDesc **outputs)
{
	// Initialise pulse audio
	if (!stack_init_pulse_audio())
	{
		// Failed to initialise, return NULL
		*outputs = NULL;
		return 0;
	}

	// Setup sink enumeration data
	PulseAudioSinkCountData sink_count_data;
	PulseAudioSinkEnumData sink_data;

	// Count the sinks
	sink_count_data.count = 0;
	pa_operation* o = pa_context_get_sink_info_list(context, (pa_sink_info_cb_t)stack_pulse_audio_sink_info_count_callback, &sink_count_data);
	if (o == NULL)
	{
		*outputs = NULL;
		return 0;
	}
	pa_operation_unref(o);

	// Wait for count of sinks
	sink_count_data.sink_semaphore.wait();

	// If we found some sinks
	if (sink_count_data.count > 0)
	{
		// Setup sink enumeration data
		sink_data.count = 0;
		sink_data.devices = new StackAudioDeviceDesc[sink_count_data.count];

		// Enumerate the sinks
		pa_operation* o = pa_context_get_sink_info_list(context, (pa_sink_info_cb_t)stack_pulse_audio_sink_info_callback, &sink_data);
		pa_operation_unref(o);

		// Wait for enumeration of sinks
		sink_data.sink_semaphore.wait();

		*outputs = sink_data.devices;
		return sink_data.count;
	}

	// We found no sinks, return NULL
	*outputs = NULL;
	return 0;
}

void stack_pulse_audio_device_free_outputs(StackAudioDeviceDesc **outputs, size_t count)
{
	for (size_t i = 0; i < count; i++)
	{
		free((*outputs)[i].name);
		free((*outputs)[i].desc);
		delete [] (*outputs)[i].rates;
	}

	delete [] *outputs;
}

void stack_pulse_audio_device_destroy(StackAudioDevice *device)
{
	// Debug
	fprintf(stderr, "stack_pulse_audio_device_destroy() called\n");

	// Tidy up
	if (STACK_PULSE_AUDIO_DEVICE(device)->stream != NULL)
	{
		// Cork immediately and remove callbacks to prevent any further calls to
		// the callbacks
		pa_threaded_mainloop_lock(mainloop);
		pa_operation *o = pa_stream_cork(STACK_PULSE_AUDIO_DEVICE(device)->stream, 1, NULL, NULL);
		pa_threaded_mainloop_unlock(mainloop);

		// Wait for the stream to cork
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			usleep(10);
		}

		// Remove the callbacks
		pa_threaded_mainloop_lock(mainloop);
		pa_stream_set_state_callback(STACK_PULSE_AUDIO_DEVICE(device)->stream, NULL, NULL);
		pa_stream_set_underflow_callback(STACK_PULSE_AUDIO_DEVICE(device)->stream, NULL, NULL);
		pa_threaded_mainloop_unlock(mainloop);

		// Disconnect from the stream
		pa_stream_disconnect(STACK_PULSE_AUDIO_DEVICE(device)->stream);

		// Stop the mainloop to ensure no more events are in progress
		pa_threaded_mainloop_stop(mainloop);

		// Mainloop might still be using the stream
		pa_stream_unref(STACK_PULSE_AUDIO_DEVICE(device)->stream);

		// Disconnect our context
		pa_context_disconnect(context);

		// Wait for the disconnect
		state_semaphore.wait();

		// Tidy up
		pa_context_unref(context);
		context = NULL;

		// Free the main loop
		pa_threaded_mainloop_free(mainloop);
		mainloop = NULL;
	}

	// Call superclass destroy
	stack_audio_device_destroy_base(device);
}

StackAudioDevice *stack_pulse_audio_device_create(const char *name, uint32_t channels, uint32_t sample_rate, stack_audio_device_audio_request_t request_audio, void *user_data)
{
	// Debug
	fprintf(stderr, "stack_pulse_audio_device_create(\"%s\", %u, %u, ...) called\n", name, channels, sample_rate);

	// Allocate the new device
	StackPulseAudioDevice *device = new StackPulseAudioDevice();
	device->stream = NULL;

	// If we've not been given a device name...
	if (name == NULL)
	{
		// Default to two channels
		channels = 2;

		if (default_sink_name == NULL)
		{
			pa_context_get_server_info(context, (pa_server_info_cb_t)stack_pulse_audio_server_info_callback, device);
			device->sync_semaphore.wait();
		}

		fprintf(stderr, "stack_pulse_audio_device_create(): Using default sink, %s\n", default_sink_name);
	}

	// Set up superclass
	STACK_AUDIO_DEVICE(device)->_class_name = "StackPulseAudioDevice";
	STACK_AUDIO_DEVICE(device)->channels = channels;
	STACK_AUDIO_DEVICE(device)->sample_rate = sample_rate;
	STACK_AUDIO_DEVICE(device)->request_audio = request_audio;
	STACK_AUDIO_DEVICE(device)->request_audio_user_data = user_data;

	// Create a PulseAudio sample spec
	pa_sample_spec samplespec;
	samplespec.channels = channels;
	samplespec.format = PA_SAMPLE_FLOAT32;
	samplespec.rate = sample_rate;

	// Create a new stream (on the application-global context)
	pa_proplist* proplist = pa_proplist_new();
	device->stream = pa_stream_new_with_proplist(context, "PulseAudio Stream", &samplespec, NULL, proplist);
	pa_proplist_free(proplist);

	// Set up callbacks
	pa_stream_set_state_callback(device->stream, (pa_stream_notify_cb_t)stack_pulse_audio_stream_notify_callback, device);
	pa_stream_set_underflow_callback(device->stream, (pa_stream_notify_cb_t)stack_pulse_audio_stream_underflow_callback, device);

	// Connect a playback stream
	pa_buffer_attr attr;
	attr.maxlength = (uint32_t)-1;
	attr.tlength = 512 * sizeof(float) * channels;
	attr.prebuf = (uint32_t)-1;
	attr.minreq = (uint32_t)-1;
	fprintf(stderr, "stack_pulse_audio_device_create(): Connecting playback stream...\n");
	pa_stream_connect_playback(device->stream, name != NULL ? name : default_sink_name, &attr, (pa_stream_flags_t)(PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_INTERPOLATE_TIMING), NULL, NULL);

	// Wait for connection
	device->sync_semaphore.wait();
	fprintf(stderr, "stack_pulse_audio_device_create(): Stream connected\n");

	// Get the state
	pa_threaded_mainloop_lock(mainloop);
	pa_stream_state stream_state = pa_stream_get_state(device->stream);
	pa_threaded_mainloop_unlock(mainloop);

	// Exit if we're in an invalid state
	if (stream_state != PA_STREAM_READY)
	{
		fprintf(stderr, "stack_pulse_audio_device_create(): Failed to connect stream\n");

		// Tidy up:
		stack_pulse_audio_device_destroy(STACK_AUDIO_DEVICE(device));

		return NULL;
	}

	// Call the underflow callback now to trigger writing some audio
	stack_pulse_audio_stream_underflow_callback(context, device);

	// Return the newly created device
	return STACK_AUDIO_DEVICE(device);
}

const char *stack_pulse_audio_device_get_friendly_name()
{
	return "Stack PulseAudio Provider";
}

void stack_pulse_audio_device_register()
{
	// Register the Pulse audio class
	StackAudioDeviceClass* pulse_audio_device_class = new StackAudioDeviceClass{ "StackPulseAudioDevice", "StackPulseAudioDevice", stack_pulse_audio_device_list_outputs, stack_pulse_audio_device_free_outputs, stack_pulse_audio_device_create, stack_pulse_audio_device_destroy, stack_pulse_audio_device_get_friendly_name };
	stack_register_audio_device_class(pulse_audio_device_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_pulse_audio_device_register();
	return true;
}

