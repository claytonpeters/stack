#ifndef _STACKMIDIDEVICE_H_INCLUDED
#define _STACKMIDIDEVICE_H_INCLUDED

// Includes:
#include "StackError.h"
#include "StackMidiEvent.h"
#include <map>
#include <string>
#include <queue>
#include <cstdint>
#include "semaphore.h"

// Structure: Descriptor of a MIDI device
struct StackMidiDeviceDesc
{
	char *name;
	char *desc;
};


struct StackMidiDevice;

// State of our receiver
#define STACK_MIDI_EVENT_RECEIVER_STATE_IDLE              0
#define STACK_MIDI_EVENT_RECEIVER_STATE_WAITING           1
#define STACK_MIDI_EVENT_RECEIVER_STATE_EXITING           2

// Structure: Defines an interface to something that receives MIDI events
struct StackMidiEventReceiver
{
	// The MIDI device associated with the reader
	StackMidiDevice *device;

	// Synchronisation
	semaphore sync;
	std::mutex mutex;
	int8_t state;

	// The queue of events
	std::queue<StackMidiEvent*> events;
};

// Structure:
struct StackMidiDevice
{
	// Class name
	const char *_class_name;

	// The device that's been opened
	StackMidiDeviceDesc descriptor;

	// Whether the device is ready for use
	bool ready;

	// The list of receivers
	std::vector<StackMidiEventReceiver*> receivers;

	// Mutex protecting receivers
	std::mutex receiver_mutex;
};

// Typedefs:
typedef size_t(*stack_midi_device_list_outputs_t)(StackMidiDeviceDesc **);
typedef void(*stack_midi_device_free_outputs_t)(StackMidiDeviceDesc **, size_t);
typedef StackMidiDevice*(*stack_midi_device_create_t)(const char *, const char *);
typedef void(*stack_midi_device_destroy_t)(StackMidiDevice *);
typedef const char *(*stack_midi_device_get_friendly_name_t)();
typedef bool(*stack_midi_device_send_event_t)(StackMidiDevice *, StackMidiEvent *);
typedef char*(*stack_midi_device_to_json_t)(StackMidiDevice *);
typedef void(*stack_midi_device_from_json_t)(StackMidiDevice *, const char *);
typedef void(*stack_midi_device_free_json_t)(StackMidiDevice *, char *);

struct StackMidiDeviceClass
{
	const char *class_name;
	const char *super_class_name;

	stack_midi_device_list_outputs_t get_outputs_func;		// Static function
	stack_midi_device_free_outputs_t free_outputs_func;	// Static function
	stack_midi_device_create_t create_func;	// Static function
	stack_midi_device_destroy_t destroy_func;
	stack_midi_device_get_friendly_name_t get_friendly_name_func; // Static function
	stack_midi_device_send_event_t send_event_func;
	stack_midi_device_to_json_t to_json_func;
	stack_midi_device_from_json_t from_json_func;
	stack_midi_device_free_json_t free_json_func;
};

// Typedefs:
typedef std::map<std::string, const StackMidiDeviceClass*> StackMidiDeviceClassMap;

// Functions: Class Registration
int stack_register_midi_device_class(StackMidiDeviceClass *mdev_class);

// Functions: Base MIDI device functions that call the superclass
void stack_midi_device_initsystem();

// Functions: Superclass initialiser - should be called my constructors
void stack_midi_device_init(StackMidiDevice *mdev, const char *name, const char *desc);

// Functions: Base functions. These should not be called except from subclasses
// of StackMidiDevice
void stack_midi_device_destroy_base(StackMidiDevice *mdev);
void stack_midi_device_from_json_base(StackMidiDevice *mdev, const char *json_data);

// I/O
char *stack_midi_device_to_json(StackMidiDevice *mdev);
void stack_midi_device_from_json(StackMidiDevice *mdev, const char *json_data);
void stack_midi_device_free_json(StackMidiDevice *mdev, char *json_data);

// Functions: Arbitrary MIDI device creation/deletion
StackMidiDevice *stack_midi_device_new(const char *type, const char *name, const char *desc);
void stack_midi_device_destroy(StackMidiDevice *mdev);
const StackMidiDeviceClass *stack_midi_device_get_class(const char *name);
// Functions: MIDI Out
bool stack_midi_device_send_event(StackMidiDevice *mdev, StackMidiEvent *event);

// Functions: Receiver interface
StackMidiEventReceiver *stack_midi_device_add_receiver(StackMidiDevice *mev);
bool stack_midi_device_remove_receiver(StackMidiDevice *mdev, StackMidiEventReceiver *receiver);
void stack_midi_device_dispatch_event(StackMidiDevice *mdev, StackMidiEvent *event);
bool stack_midi_device_get_event(StackMidiEventReceiver *receiver, StackMidiEvent **event);

// Functions: Get map of classes
const StackMidiDeviceClassMap *stack_midi_device_class_get_map();

// Defines:
#define STACK_MIDI_DEVICE(_d) ((StackMidiDevice*)(_d))

#endif
