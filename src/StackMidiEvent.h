#ifndef _STACKMIDIEVENT_H_INCLUDED
#define _STACKMIDIEVENT_H_INCLUDED

// Includes:
#include <cstdint>
#include <unistd.h>

// Useful defines:
#define STACK_MIDI_EVENT_NOTE_OFF           (0x80)
#define STACK_MIDI_EVENT_NOTE_ON            (0x90)
#define STACK_MIDI_EVENT_NOTE_AFTERTOUCH    (0xA0)
#define STACK_MIDI_EVENT_CONTROLLER         (0xB0)
#define STACK_MIDI_EVENT_PROGRAM_CHANGE     (0xC0)
#define STACK_MIDI_EVENT_CHANNEL_AFTERTOUCH (0xD0)
#define STACK_MIDI_EVENT_PITCH_BEND         (0xE0)
#define STACK_MIDI_EVENT_SYSEX              (0xF0)
#define STACK_MIDI_EVENT_TIMECODE           (0xF1)
#define STACK_MIDI_EVENT_SONG_POSITION      (0xF2)
#define STACK_MIDI_EVENT_SONG_SELECT        (0xF3)
#define STACK_MIDI_EVENT_RESERVED1          (0xF4)
#define STACK_MIDI_EVENT_RESERVED2          (0xF5)
#define STACK_MIDI_EVENT_TUNE_REQUEST       (0xF6)
#define STACK_MIDI_EVENT_SYSEX_END          (0xF7)
#define STACK_MIDI_EVENT_RT_CLOCK           (0xF8)
#define STACK_MIDI_EVENT_RESERVED3          (0xF9)
#define STACK_MIDI_EVENT_RT_START           (0xFA)
#define STACK_MIDI_EVENT_RT_CONTINUE        (0xFB)
#define STACK_MIDI_EVENT_RT_STOP            (0xFC)
#define STACK_MIDI_EVENT_RESERVED4          (0xFD)
#define STACK_MIDI_EVENT_RT_ACTIVE_SENSE    (0xFE)
#define STACK_MIDI_EVENT_RT_RESET           (0xFF)

// Structure: A "short" MIDI event - one of the defined above
struct StackMidiShortEvent
{
	// Event type
	uint8_t event_type;

	// Event channel
	uint8_t channel;

	// First 8-bit parameter, event-specific
	uint8_t param1;

	// Second 8-bit parameter, event-specific
	uint8_t param2;
};

// Structure: A "long" MIDI event (sysex)
struct StackMidiLongEvent
{
	size_t size;
	uint8_t* data;
};

// Structure: An individual event
struct StackMidiEvent
{
	// MSB is whether the event is short or long
	uint16_t is_long:1;

	// Remaining bits are the ref count
	uint16_t ref_count:15;

	// The two event types
	union
	{
		StackMidiShortEvent* short_event;
		StackMidiLongEvent* long_event;
	} types;
};

// Functions:
StackMidiEvent *stack_midi_event_new_short(uint8_t event_type, uint8_t channel, uint8_t param1, uint8_t param2);
StackMidiEvent *stack_midi_event_new_long(uint8_t *data, size_t size)
	__attribute__((access (read_only, 1, 2)));
void stack_midi_event_free(StackMidiEvent *event);

// Functions: Helper utilities
int stack_midi_event_get_note_name(char *buffer, size_t buffer_size, uint8_t note)
	__attribute__((access (write_only, 1, 2)));
int8_t stack_midi_event_note_name_to_value(const char *buffer);
void stack_midi_event_get_name_from_type(char *buffer, size_t buffer_size, uint8_t event_type)
	__attribute__((access (write_only, 1, 2)));
void stack_midi_event_get_name(char *buffer, size_t buffer_size, const StackMidiEvent *event)
	__attribute__((access (write_only, 1, 2)));
int stack_midi_event_describe(char *buffer, size_t buffer_size, const StackMidiEvent *event, bool any_channel, bool any_param1, bool any_param2)
	__attribute__((access (write_only, 1, 2)));
bool stack_midi_event_get_descriptor(uint8_t event_type, bool *param1_is_note, bool *has_param2);
int stack_midi_event_get_param_names(uint8_t event_type, char *param1, size_t param1_size, char *param2, size_t param2_size)
	__attribute__((access (write_only, 2, 3))) __attribute__((access (write_only, 4, 5)));

#endif
