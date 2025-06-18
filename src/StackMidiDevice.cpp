// Includes:
#include "StackMidiDevice.h"
#include "StackLog.h"
#include "StackJson.h"
#include <cstring>
using namespace std;

// Map of classes
static StackMidiDeviceClassMap mdev_class_map;

StackMidiDevice *stack_midi_device_create_base(const char *name, const char *desc)
{
	stack_log("stack_midi_device_create_base(): Objects of type StackMidiDevice cannot be created\n");
	return NULL;
}

void stack_midi_device_destroy_base(StackMidiDevice *mdev)
{
	// Remove all the receivers
	while (mdev->receivers.size() > 0)
	{
		stack_midi_device_remove_receiver(mdev, *mdev->receivers.begin());
	}

	// Delete ourselves
	delete mdev;
}

const char *stack_midi_device_get_friendly_name_base()
{
	return "Stack Null-Midi Provider";
}

bool stack_midi_device_send_event_base(StackMidiDevice *mdev, StackMidiEvent *event)
{
	return false;
}

void stack_midi_device_init(StackMidiDevice *mdev, const char *name, const char *desc)
{
	mdev->ready = false;
	mdev->descriptor.name = strdup(name);
	mdev->descriptor.desc = strdup(desc);
}

// Functions: Cue Type Registration
int stack_register_midi_device_class(StackMidiDeviceClass *mdev_class)
{
	// Parameter error checking
	if (mdev_class == NULL)
	{
		return STACKERR_PARAM_NULL;
	}

	// Debug
	stack_log("Registering MIDI device type '%s'\n", mdev_class->class_name);

	// Validate name pointer
	if (mdev_class->class_name == NULL)
	{
		stack_log("stack_register_midi_device_class(): Class name cannot be NULL\n");
		return STACKERR_CLASS_BADNAME;
	}

	// Ensure we don't already have a class of this type
	if (mdev_class_map.find(string(mdev_class->class_name)) != mdev_class_map.end())
	{
		stack_log("stack_register_midi_device_class(): Class name already registered\n");
		return STACKERR_CLASS_DUPLICATE;
	}

	// Only the 'StackMidiDevice' class is allowed to not have a superclass
	if (mdev_class->super_class_name == NULL && strcmp(mdev_class->class_name, "StackMidiDevice") != 0)
	{
		stack_log("stack_register_midi_device_class(): Cue classes must have a superclass\n");
		return STACKERR_CLASS_NOSUPER;
	}

	// Validate name length
	if (strlen(mdev_class->class_name) <= 0)
	{
		stack_log("stack_register_midi_device_class(): Class name cannot be empty\n");
		return STACKERR_CLASS_BADNAME;
	}

	// Validate create function pointer
	if (mdev_class->create_func == NULL)
	{
		stack_log("stack_register_midi_device_class(): Class create_func cannot be NULL. An implementation must be provided\n");
		return STACKERR_CLASS_BADCREATE;
	}

	// Validate destroy function pointer
	if (mdev_class->destroy_func == NULL)
	{
		stack_log("stack_register_midi_device_class(): Class destroy_func cannot be NULL. An implementation must be provided\n");
		return STACKERR_CLASS_BADDESTROY;
	}

	// Store the class
	mdev_class_map[string(mdev_class->class_name)] = mdev_class;

	return 0;
}

StackMidiEventReceiver *stack_midi_device_add_receiver(StackMidiDevice *mdev)
{
	// Lock our mutex (will auto unlock on function exit)
	std::unique_lock<std::mutex> lock(mdev->receiver_mutex);

	StackMidiEventReceiver *new_receiver = new StackMidiEventReceiver;
	new_receiver->device = mdev;

	new_receiver->state = STACK_MIDI_EVENT_RECEIVER_STATE_IDLE;

	// Add the receiver to the list
	mdev->receivers.push_back(new_receiver);

	return new_receiver;
}

bool stack_midi_device_remove_receiver(StackMidiDevice *mdev, StackMidiEventReceiver *receiver)
{
	// Lock our mutex (will auto unlock on function exit)
	std::unique_lock<std::mutex> lock(mdev->receiver_mutex);

	for (auto iter = mdev->receivers.begin(); iter != mdev->receivers.end(); ++iter)
	{
		if (*iter == receiver)
		{
			mdev->receivers.erase(iter);

			// Make sure we're the only ones modifying the event queue
			receiver->mutex.lock();

			// Dequeue all events from a receiver
			while (receiver->events.size() > 0)
			{
				stack_midi_event_free(receiver->events.front());
				receiver->events.pop();
			}

			// Notify the receiver so that any read they have exits
			receiver->state = STACK_MIDI_EVENT_RECEIVER_STATE_EXITING;
			receiver->mutex.unlock();
			receiver->sync.notify();

			// Busy loop to wait for exit
			while (receiver->state != STACK_MIDI_EVENT_RECEIVER_STATE_IDLE)
			{
			}

			// Tidy up
			delete receiver;
			return true;
		}
	}

	return false;
}

void stack_midi_device_dispatch_event(StackMidiDevice *mdev, StackMidiEvent *event)
{
	// Lock our mutex (will auto unlock on function exit)
	std::unique_lock<std::mutex> lock(mdev->receiver_mutex);

	for (auto receiver : mdev->receivers)
	{
		// Only push to receivers who aren't exiting
		if (receiver->state != STACK_MIDI_EVENT_RECEIVER_STATE_EXITING)
		{
			event->ref_count++;
			receiver->events.push(event);
			receiver->sync.notify();
		}
	}
}

bool stack_midi_device_get_event(StackMidiEventReceiver *receiver, StackMidiEvent **event)
{
	receiver->mutex.lock();
	if (receiver->state == STACK_MIDI_EVENT_RECEIVER_STATE_IDLE)
	{
		receiver->state = STACK_MIDI_EVENT_RECEIVER_STATE_WAITING;
	}
	else
	{
		receiver->mutex.unlock();
		return false;
	}

	// Wait for the next event
	receiver->mutex.unlock();
	receiver->sync.wait();
	std::unique_lock<std::mutex> lock(receiver->mutex);

	// See if we're being asked to delete
	if (receiver->state == STACK_MIDI_EVENT_RECEIVER_STATE_EXITING)
	{
		receiver->state = STACK_MIDI_EVENT_RECEIVER_STATE_IDLE;
		return false;
	}

	// If there are no events in the queue (which could happen if the semaphore
	// was notified for exit, also return false)
	if (receiver->events.size() == 0)
	{
		receiver->state = STACK_MIDI_EVENT_RECEIVER_STATE_IDLE;
		return false;
	}

	// Return the event
	StackMidiEvent *next_event = receiver->events.front();
	receiver->events.pop();
	*event = next_event;

#ifndef NDEBUG
	// In debug, print out what MIDI we receive
	if (!next_event->is_long)
	{
		stack_log("MIDI IN: Type: %d, Channel: %d, Param1: %d, Param2: %d\n", next_event->types.short_event->event_type, next_event->types.short_event->channel, next_event->types.short_event->param1, next_event->types.short_event->param2);

		char buffer[128];
		stack_midi_event_describe(buffer, 128, next_event, false, false, false);
		stack_log(" - Description: %s\n", buffer);
	}
	else
	{
		stack_log("MIDI IN: Type: %d, Size: %d\n", 0xF0, next_event->types.long_event->size);
	}
#endif

	// Reset the state as we're no longer waiting
	receiver->state = STACK_MIDI_EVENT_RECEIVER_STATE_IDLE;

	return true;
}

// Functions: Arbitrary MIDI device creation/deletion
StackMidiDevice *stack_midi_device_new(const char *type, const char *name, const char *desc)
{
	// Debug
	stack_log("stack_midi_device_new(): Calling create for type '%s'\n", type);

	// Locate the class
	auto iter = mdev_class_map.find(string(type));
	if (iter == mdev_class_map.end())
	{
		stack_log("stack_midi_device_new(): Unknown class\n");
		return NULL;
	}

	// No need to iterate up through superclasses - we can't be NULL
	return iter->second->create_func(name, desc);
}

void stack_midi_device_destroy(StackMidiDevice *mdev)
{
	// Debug
	stack_log("stack_midi_device_destroy(): Calling destroy for type '%s'\n", mdev->_class_name);

	// Locate the class
	auto iter = mdev_class_map.find(string(mdev->_class_name));
	if (iter == mdev_class_map.end())
	{
		stack_log("stack_midi_device_destroy(): Unknown class\n");
		return;
	}

	// No need to iterate up through superclasses - we can't be NULL
	iter->second->destroy_func(mdev);
}

char *stack_midi_device_to_json(StackMidiDevice *mdev)
{
	// Get the class name
	const char *class_name = mdev->_class_name;

	// Start a JSON response value
	Json::Value mdev_root;
	mdev_root["class"] = mdev->_class_name;

	// Look for a to_json function. Iterate through superclasses if we don't have one
	while (class_name != NULL)
	{
		// Start a node
		mdev_root[class_name] = Json::Value();

		if (mdev_class_map[class_name]->to_json_func)
		{
			if (mdev_class_map[class_name]->free_json_func)
			{
				char *json_data = mdev_class_map[class_name]->to_json_func(mdev);
				if (json_data != NULL)
				{
					stack_json_read_string(json_data, &mdev_root[class_name]);
				}
			}
			else
			{
				stack_log("stack_midi_device_to_json(): Warning: Class '%s' has no free_json_func - skipping\n", class_name);
			}
		}
		else
		{
				stack_log("stack_midi_device_to_json(): Warning: Class '%s' has no to_json_func - skipping\n", class_name);
		}

		// Iterate up to superclass
		class_name = mdev_class_map[class_name]->super_class_name;
	}

	// Generate JSON string and return it (to be free'd by stack_midi_device_free_json)
	Json::StreamWriterBuilder builder;
	return strdup(Json::writeString(builder, mdev_root).c_str());
}

void stack_midi_device_from_json(StackMidiDevice *mdev, const char *json_data)
{
	const char *class_name = mdev->_class_name;

	// Look for a from_json function. Iterate through superclasses if we don't have one
	while (class_name != NULL && mdev_class_map[class_name]->from_json_func == NULL)
	{
		class_name = mdev_class_map[class_name]->super_class_name;
	}

	// Call the function
	mdev_class_map[string(class_name)]->from_json_func(mdev, json_data);
}

void stack_midi_device_free_json(StackMidiDevice *mdev, char *json_data)
{
	const char *class_name = mdev->_class_name;

	// Look for a free_json function. Iterate through superclasses if we don't have one
	while (class_name != NULL && mdev_class_map[class_name]->free_json_func == NULL)
	{
		class_name = mdev_class_map[class_name]->super_class_name;
	}

	// Call the function
	mdev_class_map[string(class_name)]->free_json_func(mdev, json_data);
}

char *stack_midi_device_to_json_base(StackMidiDevice *mdev)
{
	Json::Value mdev_root;

	mdev_root["name"] = mdev->descriptor.name;
	mdev_root["desc"] = mdev->descriptor.desc;

	// Write out the JSON string and return it (to be free'd by
	// stack_midi_device_free_json_base)
	Json::StreamWriterBuilder builder;
	return strdup(Json::writeString(builder, mdev_root).c_str());
}

void stack_midi_device_from_json_base(StackMidiDevice *mdev, const char *json_data)
{
	Json::Value mdev_root;

	// Parse JSON data
	stack_json_read_string(json_data, &mdev_root);

	// Get the data that's pertinent to us
	Json::Value &stack_midi_device_data = mdev_root["StackMidiDevice"];

	// Copy the data to the device
	if (mdev->descriptor.name != NULL)
	{
		free(mdev->descriptor.name);
	}
	mdev->descriptor.name = strdup(stack_midi_device_data["name"].asString().c_str());
	if (mdev->descriptor.desc != NULL)
	{
		free(mdev->descriptor.desc);
	}
	mdev->descriptor.desc = strdup(stack_midi_device_data["desc"].asString().c_str());

	// Something tells me this will be non-trivial
}

void stack_midi_device_free_json_base(StackMidiDevice *mdev, char *json_data)
{
	free(json_data);
}

bool stack_midi_device_send_event(StackMidiDevice *mdev, StackMidiEvent *event)
{
	// Get the class name
	const char *class_name = mdev->_class_name;

	// Look for a playback function. Iterate through superclasses if we don't have one
	while (class_name != NULL && mdev_class_map[class_name]->send_event_func == NULL)
	{
		class_name = mdev_class_map[class_name]->super_class_name;
	}

	// Call the function
	return mdev_class_map[string(class_name)]->send_event_func(mdev, event);
}

const StackMidiDeviceClass *stack_midi_device_get_class(const char *name)
{
	auto iter = mdev_class_map.find(string(name));
	if (iter == mdev_class_map.end())
	{
		return NULL;
	}

	return iter->second;
}

const StackMidiDeviceClassMap *stack_midi_device_class_get_map()
{
	return &mdev_class_map;
}

// Registers base classes
void stack_midi_device_initsystem()
{
	StackMidiDeviceClass* midi_device_class = new StackMidiDeviceClass{ "StackMidiDevice", NULL, NULL, NULL, stack_midi_device_create_base, stack_midi_device_destroy_base, stack_midi_device_get_friendly_name_base, stack_midi_device_send_event_base, stack_midi_device_to_json_base, stack_midi_device_from_json_base, stack_midi_device_free_json_base };
	stack_register_midi_device_class(midi_device_class);
}
