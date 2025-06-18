// Includes:
#include "StackAlsaMidiDevice.h"
#include "StackLog.h"
#include <cstring>

// Upper 4 bits of an event code is the event type
#define EVENT_TYPE_FROM_EVENT_CODE(_code) ((_code) & 0xF0)

// Lower 4 bits of an event code is the channel (0-15 maps to channel 1-16)
#define CHANNEL_FROM_EVENT_CODE(_code) (((_code) & 0x0F) + 1)

// All MIDI messages start with an event code byte whose MSB is 1, which can be
// used as a sync point
#define EVENT_CODE_IS_EVENT(_code) (((_code) & 0x80) == 0x80)

// Turns an event type and channel in to an event code
#define MAKE_EVENT_CODE(_type, _channel) (((uint8_t)(_type) & 0xF0) | ((uint8_t)(_channel - 1) & 0x0F))

// Structure for iterating over ALSA MIDI devices
typedef struct AlsaDeviceIterator
{
	int card;
	int dev;
	int subdev;
	int subdevs;
	snd_ctl_t *ctl;
	snd_rawmidi_info_t* info;
} AlsaDeviceIterator;

// Initialises an ALSA MIDI Device iterator
void stack_alsa_midi_device_iterator_init(AlsaDeviceIterator *iter)
{
	iter->card = -1;
	iter->dev = -1;
	iter->subdev = -1;
	iter->subdevs = 0;
	iter->ctl = NULL;
	iter->info = NULL;
}

// Iterates to the next card on an ALSA Device Iterator. Called by the iterator
// code itself, shouldn't be called by users of the iterator
bool stack_alsa_midi_device_iterator_nextcard(AlsaDeviceIterator *iter)
{
	// Close the existing card if we have one
	if (iter->ctl != NULL)
	{
		snd_ctl_close(iter->ctl);
		iter->ctl = NULL;
	}

	// Iterate to the next card
	if (snd_card_next(&iter->card) < 0 || iter->card == -1)
	{
		return false;
	}

	// Open the card
	char card_device[32];
	snprintf(card_device, sizeof(card_device), "hw:%d", iter->card);
	int err = snd_ctl_open(&iter->ctl, card_device, 0);
	if (err < 0)
	{
		return false;
	}

	// Reset state
	iter->dev = -1;
	iter->subdev = -1;
	iter->subdevs = 0;

	return true;
}

// Iterates to the next device on an ALSA Device Iterator. Called by the iterator
// code itself, shouldn't be called by users of the iterator
bool stack_alsa_midi_device_iterator_nextdevice(AlsaDeviceIterator *iter)
{
	while (snd_ctl_rawmidi_next_device(iter->ctl, &iter->dev) >= 0)
	{
		// If we've reached the last device on the current card, move to the
		// next card
		if (iter->dev < 0)
		{
			// Tidy up
			if (iter->info != NULL)
			{
				snd_rawmidi_info_free(iter->info);
				iter->info = NULL;
			}

			if (!stack_alsa_midi_device_iterator_nextcard(iter))
			{
				return false;
			}

			// Retry the loop
			continue;
		}

		// Allocate the info structure if we don't have one
		if (iter->info == NULL)
		{
			snd_rawmidi_info_malloc(&iter->info);
		}

		// Get information about the device
		snd_rawmidi_info_set_device(iter->info, iter->dev);
		snd_rawmidi_info_set_subdevice(iter->info, 0);
		snd_rawmidi_info_set_stream(iter->info, SND_RAWMIDI_STREAM_INPUT);

		int err = 0;
		if ((err = snd_ctl_rawmidi_info(iter->ctl, iter->info)) < 0)
		{
			// Tidy up
			snd_rawmidi_info_free(iter->info);
			iter->info = NULL;

			if (err != -ENOENT)
			{
				return false;
			}

			continue;
		}

		// Get the number of subdevices
		iter->subdevs = snd_rawmidi_info_get_subdevices_count(iter->info);

		// Prepare nextsubdev to get the first subdevice
		iter->subdev = -1;

		return true;
	}

	return false;
}

// Iterates to the next subdevice on an ALSA Device Iterator. Called by the
// iterator code itself, shouldn't be called by users of the iterator
bool stack_alsa_midi_device_iterator_nextsubdev(AlsaDeviceIterator *iter)
{
	if (iter->subdev == -1)
	{
		iter->subdev = 0;
		return true;
	}

	// If we've reached the last subdevice, move to the next device
	if (iter->subdev == iter->subdevs - 1)
	{
		if (!stack_alsa_midi_device_iterator_nextdevice(iter))
		{
			return false;
		}
	}

	// Iterate to next subdevice
	iter->subdev++;

	return true;
}

// Iterate to the next card/device/subdevice on an ALSA MIDI Device Iterator
bool stack_alsa_midi_device_iterator_next(AlsaDeviceIterator *iter)
{
	// If we don't yet have a card, get one
	if (iter->card == -1)
	{
		if (!stack_alsa_midi_device_iterator_nextcard(iter))
		{
			return false;
		}
	}

	// If we don't yet have a device, get one
	if (iter->dev == -1)
	{
		if (!stack_alsa_midi_device_iterator_nextdevice(iter))
		{
			return false;
		}
	}

	// Get the next subdevice
	if (!stack_alsa_midi_device_iterator_nextsubdev(iter))
	{
		return false;
	}

	snd_rawmidi_info_set_subdevice(iter->info, iter->subdev);
	if (snd_ctl_rawmidi_info(iter->ctl, iter->info) < 0)
	{
		return false;
	}

	return true;
}

// Populate a StackMidiDeviceDesc structure with information about the current
// device in the iteratr
bool stack_alsa_midi_device_iterator_get(AlsaDeviceIterator *iter, StackMidiDeviceDesc *desc)
{
	const char *subdevice_name = snd_rawmidi_info_get_subdevice_name(iter->info);
	if (subdevice_name == NULL)
	{
		return false;
	}

	// If the subdevice has no name, use the parent device name
	if (strlen(subdevice_name) == 0)
	{
		subdevice_name = snd_rawmidi_info_get_name(iter->info);
	}

	// Figure out the length of the hardware device
	size_t hw_size = snprintf(NULL, 0, "hw:%d,%d,%d", iter->card, iter->dev, iter->subdev);
	if (hw_size < 0)
	{
		return false;
	}

	// Populate the device address
	char *hwdevice = (char*)malloc(hw_size + 1);
	snprintf(hwdevice, hw_size + 1, "hw:%d,%d,%d", iter->card, iter->dev, iter->subdev);

	// Populate the structure
	desc->name = hwdevice;
	desc->desc = strdup(subdevice_name);

	return true;
}

// Tidies up an ALSA MIDI Device Iterator
void stack_alsa_midi_device_iterator_free(AlsaDeviceIterator *iter)
{
	if (iter->ctl != NULL)
	{
		snd_ctl_close(iter->ctl);
		iter->ctl = NULL;
	}

	if (iter->info != NULL)
	{
		snd_rawmidi_info_free(iter->info);
		iter->info = NULL;
	}

	iter->card = -1;
	iter->dev = -1;
	iter->subdev = -1;
}

size_t stack_alsa_midi_device_list_outputs(StackMidiDeviceDesc **outputs)
{
	// Count the number of MIDI devices
	size_t device_count = 0;
	AlsaDeviceIterator iter;
	stack_alsa_midi_device_iterator_init(&iter);
	while (stack_alsa_midi_device_iterator_next(&iter))
	{
		device_count++;
	}
	stack_alsa_midi_device_iterator_free(&iter);
	stack_log("stack_alsa_midi_device_list_outputs(): Found %d devices\n", device_count);

	// Early exit if we found no devices
	if (device_count == 0)
	{
		*outputs = NULL;
		return 0;
	}

	// Populate the array - note that there's a small risk that the device count
	// will change between the above and here, which we mitigate with the memset
	// and index check
	size_t device_index = 0;
	StackMidiDeviceDesc *devices = new StackMidiDeviceDesc[device_count];
	memset(devices, 0, sizeof(StackMidiDeviceDesc) * device_count);
	stack_alsa_midi_device_iterator_init(&iter);
	while (stack_alsa_midi_device_iterator_next(&iter) && device_index < device_count)
	{
		stack_alsa_midi_device_iterator_get(&iter, &devices[device_index]);
		device_index++;
	}
	stack_alsa_midi_device_iterator_free(&iter);

	*outputs = devices;
	return device_count;
}

void stack_alsa_midi_device_free_outputs(StackMidiDeviceDesc **outputs, size_t count)
{
	for (size_t i = 0; i < count; i++)
	{
		free((*outputs)[i].name);
		free((*outputs)[i].desc);
	}

	delete [] *outputs;
}

static void stack_alsa_midi_device_close(StackAlsaMidiDevice *device)
{
	if (device->handle_in)
	{
		stack_log("stack_alsa_midi_device_close(): Closing MIDI In\n");
		snd_rawmidi_close(device->handle_in);
		device->handle_in = NULL;
	}
	if (device->handle_out)
	{
		stack_log("stack_alsa_midi_device_close(): Closing MIDI Out\n");
		snd_rawmidi_close(device->handle_out);
		device->handle_out = NULL;
	}

	// We're no longer ready
	STACK_MIDI_DEVICE(device)->ready = false;
}

void stack_alsa_midi_device_destroy(StackMidiDevice *device)
{
	StackAlsaMidiDevice *alsa_device = STACK_ALSA_MIDI_DEVICE(device);

	// Debug
	stack_log("stack_alsa_midi_device_destroy() called\n");

	// Wait for the thread to stop (which also closes the device)
	if (alsa_device->thread_running)
	{
		alsa_device->thread_running = false;
		alsa_device->read_thread.join();
	}

	if (device->descriptor.name != NULL)
	{
		free(device->descriptor.name);
	}
	if (device->descriptor.desc != NULL)
	{
		free(device->descriptor.desc);
	}

	// Call superclass destroy
	stack_midi_device_destroy_base(device);
}

static bool stack_alsa_midi_device_send_event(StackMidiDevice *device, StackMidiEvent *event)
{
	uint32_t event_size;
	ssize_t write_result;

	// Prevent multiple concurrent writes
	std::unique_lock<std::mutex> lock(STACK_ALSA_MIDI_DEVICE(device)->write_mutex);

	// We can't send if we're not ready
	if (!device->ready)
	{
		stack_log("stack_alsa_midi_device_send_event(): Device was not ready");
		return false;
	}

	if (!event->is_long)
	{
		uint8_t short_event_buffer[3];

		// Populate our buffer (note the we map the humanised 1-16 channel to 0-15)
		short_event_buffer[0] = MAKE_EVENT_CODE(event->types.short_event->event_type, event->types.short_event->channel - 1);
		short_event_buffer[1] = event->types.short_event->param1;
		short_event_buffer[2] = event->types.short_event->param2;

		// These four event types are only two bytes
		if (event->types.short_event->event_type != STACK_MIDI_EVENT_PROGRAM_CHANGE && event->types.short_event->event_type != STACK_MIDI_EVENT_CHANNEL_AFTERTOUCH &&
			event->types.short_event->event_type != STACK_MIDI_EVENT_TIMECODE && event->types.short_event->event_type != STACK_MIDI_EVENT_SONG_SELECT)
		{
			event_size = 3;
		}
		else
		{
			event_size = 2;
		}

		write_result = snd_rawmidi_write(STACK_ALSA_MIDI_DEVICE(device)->handle_out, short_event_buffer, event_size);
		if (write_result != event_size)
		{
			stack_log("stack_alsa_midi_device_send_event(): Failed to write short event: %d != %u\n", write_result, event_size);
			return false;
		}
	}
	else
	{
		uint8_t message_byte;

		// Size is the length of the data plus the start and end bytes
		event_size = event->types.long_event->size + 2;

		// Write the leading message start byte
		message_byte = STACK_MIDI_EVENT_SYSEX;
		write_result = snd_rawmidi_write(STACK_ALSA_MIDI_DEVICE(device)->handle_out, &message_byte, 1);
		if (write_result != 1)
		{
			stack_log("stack_alsa_midi_device_send_event(): Failed to write long event header: %d != 1\n", write_result);
			return false;
		}

		// Write the message itself
		write_result = snd_rawmidi_write(STACK_ALSA_MIDI_DEVICE(device)->handle_out, event->types.long_event->data, event->types.long_event->size);
		if (write_result != event->types.long_event->size)
		{
			stack_log("stack_alsa_midi_device_send_event(): Failed to write long event footer: %d != %u\n", write_result, event->types.long_event->size);
			return false;
		}

		// Write the trailing message end byte
		message_byte = STACK_MIDI_EVENT_SYSEX_END;
		write_result = snd_rawmidi_write(STACK_ALSA_MIDI_DEVICE(device)->handle_out, &message_byte, 1);
		if (write_result != 1)
		{
			stack_log("stack_alsa_midi_device_send_event(): Failed to write long event footer: %d != 1\n", write_result);
			return false;
		}
	}

	return true;
}

int16_t stack_alsa_midi_device_sync(StackAlsaMidiDevice *device)
{
	// If we're already sync'd, do nothing
	if (device->synced)
	{
		return 0;
	}

	uint8_t byte = 0;
	int read_result = 0;

	// Loop to sync
	do
	{
		// Read a byte
		read_result = snd_rawmidi_read(device->handle_in, &byte, 1);
		if (read_result <= 0)
		{
			stack_log("stack_alsa_midi_device_sync(): Failed to read from MIDI device: %d\n", read_result);
			return -1;
		}

		// If we have something that looks like an event
		if (EVENT_CODE_IS_EVENT(byte))
		{
			// Assume we're now sync'd and return the event code
			device->synced = true;
		}
	} while (!device->synced);

	return byte;
}

static bool stack_alsa_midi_device_read_sysex(StackAlsaMidiDevice *device)
{
	size_t event_size = 0;
	uint8_t event_data[32768];
	unsigned char read_byte = 0;
	bool logged_dropped = false;

	// A sysex event ends with a 0xF7 byte
	while (read_byte != STACK_MIDI_EVENT_SYSEX_END)
	{
		// Read the next byte
		int read_result = snd_rawmidi_read(device->handle_in, &read_byte, 1);
		if (read_result != 1)
		{
			stack_log("stack_alsa_midi_device_read_sysex(): Failed to read from MIDI device on sysex: %d\n", read_result);
			stack_alsa_midi_device_close(device);
			return false;
		}

		// If the MSB is high, it could be a terminating byte or an RT event, or a problem
		if (read_byte & 0x80 == 1)
		{
			// The SysEx event is being interrupted by an RT event
			if (read_byte >= STACK_MIDI_EVENT_RT_CLOCK)
			{
				// None of the RT events have parameters, so just dispatch a short event
				StackMidiEvent *event = stack_midi_event_new_short(read_byte, 0, 0, 0);
				stack_midi_device_dispatch_event(STACK_MIDI_DEVICE(device), event);
				stack_midi_event_free(event);
			}
			// Normal SysEx termination condition
			else if (read_byte == STACK_MIDI_EVENT_SYSEX_END)
			{
				break;
			}
			// Error condition
			else
			{
				stack_log("stack_alsa_midi_device_read_sysex(): Desync'd on invalid byte during SysEx event: %u\n", read_byte);
				device->synced = false;
				return false;
			}
		}
		// Normal 7-bit byte
		else
		{
			// Limit to the maximum size of our buffer
			if (event_size < sizeof(event_data))
			{
				// Store the byte
				event_data[event_size] == read_byte;
				event_size++;
			}
			else
			{
				if (!logged_dropped)
				{
					stack_log("stack_alsa_midi_device_read_sysex(): Event too long - truncated to 32kB\n");
					logged_dropped = true;
				}
			}
		}
	}

	// Dispatch the event
	StackMidiEvent *event = stack_midi_event_new_long(event_data, event_size);
	stack_midi_device_dispatch_event(STACK_MIDI_DEVICE(device), event);
	stack_midi_event_free(event);

	// Return that we dispatched an event
	return true;
}

static bool stack_alsa_midi_device_read_short_event(StackAlsaMidiDevice *device, uint8_t status_code, uint8_t first_byte)
{
	uint8_t params, current_param, param1 = 0, param2 = 0, channel = 0;

	// For events whose codes have the channel embedded in them
	uint8_t event_type = EVENT_TYPE_FROM_EVENT_CODE(status_code);

	// Determine how many parameters we have
	if (status_code <= STACK_MIDI_EVENT_SONG_SELECT)
	{
		// These four events only have a single parameter, but all other short
		// events have two parameters
		if (event_type != STACK_MIDI_EVENT_PROGRAM_CHANGE && event_type != STACK_MIDI_EVENT_CHANNEL_AFTERTOUCH &&
			event_type != STACK_MIDI_EVENT_TIMECODE && event_type != STACK_MIDI_EVENT_SONG_SELECT)
		{
			params = 2;
		}
		else
		{
			params = 1;
		}

		// Store the status code for possible re-use by a next event
		device->last_status_byte = status_code;

		// We're reading the first parameter
		current_param = 1;
	}
	else
	{
		// All the upper MIDI events have no parameters
		params = 0;
		current_param = 0;
		device->last_status_byte = 0;
	}

	// Read first param if we have one
	while (params > 0 && current_param == 1)
	{
		// We might have read the first byte in stack_alsa_midi_device_read_event
		if (first_byte == 0xFF)
		{
			int param1_read_result = snd_rawmidi_read(device->handle_in, &param1, 1);
			if (param1_read_result <= 0)
			{
				stack_log("stack_alsa_midi_device_read_short_event(): Failed to read from MIDI device on short event param 1: %d\n", param1_read_result);
				stack_alsa_midi_device_close(device);
				return false;
			}
		}
		else
		{
			param1 = first_byte;
		}

		// If the MSB is high, we're either being interrupted by a SysEx or something has gone wrong
		if (param1 && 0x80 == 1)
		{
			// If we're being interrupted by SysEx
			if (param1 == STACK_MIDI_EVENT_SYSEX)
			{
				// Process the event (and any possible RT events that occur during it)
				if (!stack_alsa_midi_device_read_sysex(device))
				{
					return false;
				}
			}
			else
			{
				// Desync
				device->synced = false;
				stack_log("stack_alsa_midi_device_read_short_event(): Desync'd on invalid byte during short event: %u\n", param1);
				return false;
			}
		}
		else
		{
			current_param++;
		}
	}

	// Read first param if we have one
	while (params > 1 && current_param == 2)
	{
		int param2_read_result = snd_rawmidi_read(device->handle_in, &param2, 1);
		if (param2_read_result <= 0)
		{
			stack_log("stack_alsa_midi_device_read_short_event(): Failed to read from MIDI device on short event param 2: %d\n", param2_read_result);
			stack_alsa_midi_device_close(device);
			return false;
		}

		// If the MSB is high, we're either being interrupted by a SysEx or something has gone wrong
		if (param2 && 0x80 == 1)
		{
			if (param2 == STACK_MIDI_EVENT_SYSEX)
			{
				if (!stack_alsa_midi_device_read_sysex(device))
				{
					return false;
				}
			}
			else
			{
				// Desync
				device->synced = false;
				stack_log("stack_alsa_midi_device_read_short_event(): Desync'd on invalid byte during short event: %u\n", param1);
				return NULL;
			}
		}
		else
		{
			current_param++;
		}
	}

	if (event_type >= STACK_MIDI_EVENT_SYSEX)
	{
		// For events that have no channel
		event_type = status_code;
	}
	else
	{
		// For events that do have a channel
		channel = CHANNEL_FROM_EVENT_CODE(status_code);
	}

	// Note that we map 0-15 of the reported channel to the humanised 1-16 in the event
	StackMidiEvent *event = stack_midi_event_new_short(event_type, channel + 1, param1, param2);
	stack_midi_device_dispatch_event(STACK_MIDI_DEVICE(device), event);
	stack_midi_event_free(event);

	return true;
}

static bool stack_alsa_midi_device_read_event(StackAlsaMidiDevice *device)
{
	// Ensure we're sync'd
	int16_t sync_result = stack_alsa_midi_device_sync(device);
	if (sync_result < 0)
	{
		stack_alsa_midi_device_close(device);
		stack_log("stack_alsa_midi_device_read_event(): Failed to sync\n");
		return NULL;
	}

	// Get a byte of data to work on
	uint8_t first_byte;
	if (sync_result == 0)
	{
		// Read a byte
		int read_result = snd_rawmidi_read(device->handle_in, &first_byte, 1);
		if (read_result <= 0)
		{
			stack_log("stack_alsa_midi_device_read_event(): Failed to read from MIDI device: %d\n", read_result);
			return NULL;
		}
	}
	else
	{
		// The sync call sync'd and the byte we read is our first byte
		first_byte = sync_result;
	}

	// If the byte looks like an event code
	if (EVENT_CODE_IS_EVENT(first_byte))
	{
		if (first_byte == STACK_MIDI_EVENT_SYSEX)
		{
			// If it's a SysEx event, read the whole event
			if (!stack_alsa_midi_device_read_sysex(device))
			{
				stack_log("stack_alsa_midi_device_read_event(): Failed to read sysex\n");
				return NULL;
			}
		}
		else
		{
			// IF it's not a SysEx event, read a short event
			if (!stack_alsa_midi_device_read_short_event(device, first_byte, 0xFF))
			{
				stack_log("stack_alsa_midi_device_read_event(): Failed to read short event\n");
				return NULL;
			}
		}
	}
	else
	{
		// Byte doesn't look like a new event, re-use old status byte if possible
		if (device->last_status_byte != 0)
		{
			if (!stack_alsa_midi_device_read_short_event(device, device->last_status_byte, first_byte))
			{
				stack_log("stack_alsa_midi_device_read_event(): Failed to short event (reused event code)\n");
				return NULL;
			}
		}
		else
		{
			// Something has gone awry, resync on next call
			device->synced = false;
			stack_log("stack_alsa_midi_device_read_event(): Lost sync\n");
			return NULL;
		}
	}

	return NULL;
}

static void stack_alsa_midi_device_read_thread(void *user_data)
{
	StackAlsaMidiDevice *device = STACK_ALSA_MIDI_DEVICE(user_data);
	device->thread_running = true;

	struct pollfd poll_fds;

	while (device->thread_running)
	{
		// Open the device if we haven't already
		if (!(STACK_MIDI_DEVICE(device)->ready))
		{
			stack_log("stack_alsa_midi_device_read_thread(): Opening device %s\n", STACK_MIDI_DEVICE(device)->descriptor.name);

			// Open the device. We do this in non-blocking mode otherwise attempting
			// to open a device that's in use causes this thread to block, which in
			// turn causes the destroy to block when terminating
			int result = snd_rawmidi_open(&device->handle_in, &device->handle_out, STACK_MIDI_DEVICE(device)->descriptor.name, SND_RAWMIDI_NONBLOCK);
			if (result > 0 || device->handle_in == NULL)
			{
				stack_log("stack_alsa_midi_device_read_thread(): Failed to open MIDI device: %d\n", result);
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				continue;
			}

			// Put us back into blocking mode (as we want reads to block)
			snd_rawmidi_nonblock(device->handle_in, 0);

			// Get the descriptors to poll
			int fd_count = snd_rawmidi_poll_descriptors(device->handle_in, &poll_fds, 1);
			if (fd_count == 0)
			{
				stack_log("stack_alsa_midi_device_read_thread(): Failed to get MIDI poll descriptors\n");
				stack_alsa_midi_device_close(device);
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				continue;
			}

			// Device opened - mark ourselves as ready
			STACK_MIDI_DEVICE(device)->ready = true;
			stack_log("stack_alsa_midi_device_read_thread(): Device ready\n");
		}

		// Wait for data (for up to 100msec)
		int poll_result = poll(&poll_fds, 1, 100);
		if (poll_result < 0)
		{
			stack_log("stack_alsa_midi_device_read_thread(): Poll failed: %d\n", poll_result);
			continue;
		}
		else if (poll_result == 0)
		{
			// Nothing ready (poll timeout)
			continue;
		}

		// Check if we've been asked to terminate (the device could now be closed if so)
		if (!device->thread_running)
		{
			return;
		}

		// Read and dispatch an event (or potentially more than one event if events are
		// interleaved)
		stack_alsa_midi_device_read_event(device);
	}

	// Close the devices as we're no longer using them
	stack_alsa_midi_device_close(device);

	stack_log("stack_alsa_midi_device_read_thread(): Exiting\n");
}

StackMidiDevice *stack_alsa_midi_device_new(const char *name, const char *desc)
{
	// Debug
	stack_log("stack_alsa_midi_device_new(\"%s\") called\n", name);

	// Allocate the new device
	StackAlsaMidiDevice *device = new StackAlsaMidiDevice();
	StackMidiDevice *mdev = STACK_MIDI_DEVICE(device);

	// Set up superclass
	stack_midi_device_init(mdev, name, desc);
	mdev->_class_name = "StackAlsaMidiDevice";

	// We're not ready until the thread says we are
	mdev->ready = false;

	// Initialise the device
	device->handle_in = NULL;
	device->handle_out = NULL;
	device->thread_running = false;
	device->last_status_byte = 0;
	device->synced = false;

	// Start the output thread
	device->read_thread = std::thread(stack_alsa_midi_device_read_thread, device);

	// Return the newly created device
	return mdev;
}

char *stack_alsa_midi_device_to_json(StackMidiDevice *mdev)
{
	// We have nothing
	return NULL;
}

void stack_alsa_midi_device_from_json(StackMidiDevice *mdev, const char *json_data)
{
	// Call the superclass
	stack_midi_device_from_json_base(mdev, json_data);

	// We have no parameters
}

void stack_alsa_midi_device_free_json(StackMidiDevice *mdev, char *json_data)
{
	free(json_data);
}

const char *stack_alsa_midi_device_get_friendly_name()
{
	return "Stack ALSA Provider";
}

void stack_alsa_midi_device_register()
{
	// Register the Alsa MIDI class
	StackMidiDeviceClass* pulse_midi_device_class = new StackMidiDeviceClass{ "StackAlsaMidiDevice", "StackMidiDevice", stack_alsa_midi_device_list_outputs, stack_alsa_midi_device_free_outputs, stack_alsa_midi_device_new, stack_alsa_midi_device_destroy, stack_alsa_midi_device_get_friendly_name, stack_alsa_midi_device_send_event, stack_alsa_midi_device_to_json, stack_alsa_midi_device_from_json, stack_alsa_midi_device_free_json };

	stack_register_midi_device_class(pulse_midi_device_class);
}
