// Includes:
#include "StackMidiEvent.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "StackLog.h"

StackMidiEvent *stack_midi_event_new_short(uint8_t event_type, uint8_t channel, uint8_t param1, uint8_t param2)
{
	StackMidiShortEvent *short_event = new StackMidiShortEvent;
	short_event->event_type = event_type;
	short_event->channel = channel;
	short_event->param1 = param1;
	short_event->param2 = param2;

	StackMidiEvent *event = new StackMidiEvent;
	event->is_long = 0;
	event->ref_count = 1;
	event->types.short_event = short_event;

	return event;
}

StackMidiEvent *stack_midi_event_new_long(uint8_t *data, size_t size)
{
	StackMidiLongEvent *long_event = new StackMidiLongEvent;
	long_event->size = size;
	if (size > 0 || data == NULL)
	{
		long_event->data = new uint8_t[size];
		memcpy(long_event->data, data, size);
	}
	else
	{
		long_event->data = NULL;
	}

	StackMidiEvent *event = new StackMidiEvent;
	event->is_long = 1;
	event->ref_count = 1;
	event->types.long_event = long_event;

	return event;
}

void stack_midi_event_free(StackMidiEvent *event)
{
	event->ref_count--;
	if (event->ref_count == 0)
	{
		// Tidy up event-specific data
		if (event->is_long)
		{
			if (event->types.long_event->data != NULL)
			{
				delete [] event->types.long_event->data;
			}
			delete event->types.long_event;
		}
		else
		{
			delete event->types.short_event;
		}

		// Tidy up the event
		delete event;
	}
}

// Functions: Helper utilities
void stack_midi_event_get_name_from_type(char *buffer, size_t buffer_size, uint8_t event_type)
{
	const char *event_name = NULL;

	switch (event_type)
	{
		case STACK_MIDI_EVENT_NOTE_OFF:
			event_name = "Note Off";
			break;
		case STACK_MIDI_EVENT_NOTE_ON:
			event_name = "Note On";
			break;
		case STACK_MIDI_EVENT_NOTE_AFTERTOUCH:
			event_name = "Note Aftertouch";
			break;
		case STACK_MIDI_EVENT_CONTROLLER:
			event_name = "Controller Change";
			break;
		case STACK_MIDI_EVENT_PROGRAM_CHANGE:
			event_name = "Program Change";
			break;
		case STACK_MIDI_EVENT_CHANNEL_AFTERTOUCH:
			event_name = "Channel Aftertouch";
			break;
		case STACK_MIDI_EVENT_PITCH_BEND:
			event_name = "Pitch Bend";
			break;
		case STACK_MIDI_EVENT_SYSEX:
			event_name = "System Exclusive";
			break;
		case STACK_MIDI_EVENT_TIMECODE:
			event_name = "Timecode";
			break;
		case STACK_MIDI_EVENT_SONG_POSITION:
			event_name = "Song Position";
			break;
		case STACK_MIDI_EVENT_SONG_SELECT:
			event_name = "Song Select";
			break;
		case STACK_MIDI_EVENT_TUNE_REQUEST:
			event_name = "Tune Request";
			break;
		case STACK_MIDI_EVENT_SYSEX_END:
			event_name = "System Exclusive End";
			break;
		case STACK_MIDI_EVENT_RT_CLOCK:
			event_name = "Real-time Clock";
			break;
		case STACK_MIDI_EVENT_RT_START:
			event_name = "Real-time Start Sequence";
			break;
		case STACK_MIDI_EVENT_RT_CONTINUE:
			event_name = "Real-time Continue Sequence";
			break;
		case STACK_MIDI_EVENT_RT_STOP:
			event_name = "Real-time Stop Sequence";
			break;
		case STACK_MIDI_EVENT_RT_ACTIVE_SENSE:
			event_name = "Real-time Active Sense";
			break;
		case STACK_MIDI_EVENT_RT_RESET:
			event_name = "Real-time Reset";
			break;

		case STACK_MIDI_EVENT_RESERVED1:
		case STACK_MIDI_EVENT_RESERVED2:
		case STACK_MIDI_EVENT_RESERVED3:
		case STACK_MIDI_EVENT_RESERVED4:
			event_name = "Reserved";
			break;

		default:
			event_name = "Unknown";
			break;
	}

	strncpy(buffer, event_name, buffer_size);
}

void stack_midi_event_get_name(char *buffer, size_t buffer_size, const StackMidiEvent *event)
{
	const char *event_name = NULL;

	if (event->is_long)
	{
		strncpy(buffer, "System Exclusive", buffer_size);
		return;
	}

	stack_midi_event_get_name_from_type(buffer, buffer_size, event->types.short_event->event_type);
}

int8_t stack_midi_event_note_name_to_value(const char *buffer)
{
	// Offset of the non-accidental notes from C
	static const int8_t base_note_offsets[] = {
		0,  // C
		2,  // D
		4,  // E
		5,  // F
		7,  // G
		9,  // A
		11  // B
	};

	int32_t note_offset = 0, octave = 0;
	const char *octave_ptr = NULL;

	// Map C-G, A-B to 0-7 (allowing lowercase or uppercase)
	if (buffer[0] >= 'A' && buffer[0] <= 'G')
	{
		note_offset = ((int8_t)buffer[0] - (int8_t)'C') % 7;
	}
	else if (buffer[0] >= 'a' && buffer[0] <= 'g')
	{
		note_offset = ((int8_t)buffer[0] - (int8_t)'c') % 7;
	}
	else
	{
		return -1;
	}

	// Map 0-7 to 0-11 to allow space for sharps/flats
	note_offset = base_note_offsets[note_offset];

	// String is too short
	if (buffer[1] == '\0')
	{
		return -1;
	}

	// Deal with sharps and flats
	if (buffer[1] == '#')
	{
		note_offset++;
		octave_ptr = &buffer[2];
	}
	else if (buffer[1] == 'b')
	{
		note_offset--;
		octave_ptr = &buffer[2];
	}
	else
	{
		octave_ptr = &buffer[1];
	}

	// Make sure we have a number where the octave number should be
	if ((*octave_ptr >= '0' && *octave_ptr <= '9') || *octave_ptr == '-')
	{
		octave = (int8_t)atoi(octave_ptr);
	}
	else
	{
		return -1;
	}

	// Middle C (C4) is 60, hence the add one on the octave
	int32_t result = ((octave + 1) * 12) + note_offset;

	// Make sure we return a valid MIDI note number
	if (result < 0 || result > 127)
	{
		return -1;
	}

	return result;
}

int stack_midi_event_get_note_name(char *buffer, size_t buffer_size, uint8_t note)
{
	static const char *notes[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

	// Middle C (C4) is 60, 12 notes per octave, (60[note] / 12[notes per octave]) - 1[offset] = 4[octave]
	return snprintf(buffer, buffer_size, "%s%d", notes[note % 12], (note / 12) - 1);
}

int stack_midi_event_describe(char *buffer, size_t buffer_size, const StackMidiEvent *event, bool any_channel, bool any_param1, bool any_param2)
{
	char event_name_buffer[64], channel_buffer[8], param1_buffer[8], param2_buffer[8];

	// Get the event name
	stack_midi_event_get_name(event_name_buffer, sizeof(event_name_buffer), event);

	// Long events are always SysEx with just the data size
	if (event->is_long)
	{
		return snprintf(buffer, buffer_size, "%s, %lu bytes", event_name_buffer, event->types.long_event->size);
	}

	// We use this a lot
	const StackMidiShortEvent *short_event = event->types.short_event;

	// Format the channel
	if (any_channel)
	{
		strncpy(channel_buffer, "Any", sizeof(channel_buffer));
	}
	else
	{
		snprintf(channel_buffer, sizeof(channel_buffer), "%u", short_event->channel);
	}

	// Format the parameters
	if (any_param1)
	{
		strncpy(param1_buffer, "Any", sizeof(param1_buffer));
	}
	else
	{
		snprintf(param1_buffer, sizeof(param1_buffer), "%u", short_event->param1);
	}
	if (any_param2)
	{
		strncpy(param2_buffer, "Any", sizeof(param2_buffer));
	}
	else
	{
		snprintf(param2_buffer, sizeof(param2_buffer), "%u", short_event->param2);
	}

	switch (short_event->event_type)
	{
		case STACK_MIDI_EVENT_NOTE_OFF:
		case STACK_MIDI_EVENT_NOTE_ON:
		case STACK_MIDI_EVENT_NOTE_AFTERTOUCH:
			if (!any_param1)
			{
				stack_midi_event_get_note_name(param1_buffer, sizeof(param1_buffer), short_event->param1);
			}
			return snprintf(buffer, buffer_size, "%s, Channel: %s, Note: %s, Velocity %s", event_name_buffer, channel_buffer, param1_buffer, param2_buffer);
		case STACK_MIDI_EVENT_CONTROLLER:
			return snprintf(buffer, buffer_size, "%s, Channel: %s, Controller: %s, Value: %s", event_name_buffer, channel_buffer, param1_buffer, param2_buffer);
		case STACK_MIDI_EVENT_PROGRAM_CHANGE:
			return snprintf(buffer, buffer_size, "%s, Channel: %s, Program: %s", event_name_buffer, channel_buffer, param1_buffer);
		case STACK_MIDI_EVENT_CHANNEL_AFTERTOUCH:
			return snprintf(buffer, buffer_size, "%s, Channel: %s", event_name_buffer, channel_buffer);
		case STACK_MIDI_EVENT_PITCH_BEND:
			// Pitch bend is a 14-bit value
			if (!any_param1)
			{
				snprintf(param1_buffer, sizeof(param1_buffer), "%u", ((uint16_t)short_event->param2 << 7) | (uint16_t)short_event->param1);
			}
			return snprintf(buffer, buffer_size, "%s, Channel: %s, Value: %s", event_name_buffer, channel_buffer, param1_buffer);
		case STACK_MIDI_EVENT_TIMECODE:
			return snprintf(buffer, buffer_size, "%s, Type: %s, Value: %s", event_name_buffer, param1_buffer, param2_buffer);
		case STACK_MIDI_EVENT_SONG_POSITION:
			return snprintf(buffer, buffer_size, "%s, Position: %s", event_name_buffer, param1_buffer);
		case STACK_MIDI_EVENT_SONG_SELECT:
			return snprintf(buffer, buffer_size, "%s, Song: %s", event_name_buffer, param1_buffer);

		// All unknown, reserved, or zero-parameter events
		default:
			return snprintf(buffer, buffer_size, "%s", event_name_buffer);
	}
}

// Based on the event type, determines if parameter one represents a note, and
// whether parameter two is used
bool stack_midi_event_get_descriptor(uint8_t event_type, bool *param1_is_note, bool *has_param2)
{
	bool result_param1_is_note = false, result_has_param2 = false;

	switch (event_type)
	{
		case STACK_MIDI_EVENT_NOTE_OFF:
		case STACK_MIDI_EVENT_NOTE_ON:
		case STACK_MIDI_EVENT_NOTE_AFTERTOUCH:
			result_has_param2 = true;
			result_param1_is_note = true;
		case STACK_MIDI_EVENT_CONTROLLER:
		case STACK_MIDI_EVENT_PITCH_BEND:
		case STACK_MIDI_EVENT_SONG_POSITION:
		case STACK_MIDI_EVENT_SONG_SELECT:
			result_has_param2 = true;
			break;
		// TODO: Finish this off - all of the STACK_MIDI_EVENT_RT_* messages
		default:
			return false;
			break;
	}

	if (param1_is_note != NULL)
	{
		*param1_is_note = result_param1_is_note;
	}
	if (has_param2 != NULL)
	{
		*has_param2 = result_has_param2;
	}

	return true;
}

int stack_midi_event_get_param_names(uint8_t event_type, char *param1_out, size_t param1_size, char *param2_out, size_t param2_size)
{
	const char *param1_name = NULL;
	const char *param2_name = NULL;
	int param_count = 0;

	switch (event_type)
	{
		case STACK_MIDI_EVENT_NOTE_OFF:
		case STACK_MIDI_EVENT_NOTE_ON:
		case STACK_MIDI_EVENT_NOTE_AFTERTOUCH:
			param1_name = "Note";
			param2_name = "Velocity";
			param_count = 2;
			break;
		case STACK_MIDI_EVENT_CONTROLLER:
			param1_name = "Controller";
			param2_name = "Value";
			param_count = 2;
			break;
		case STACK_MIDI_EVENT_PROGRAM_CHANGE:
			param1_name = "Program";
			param_count = 1;
			break;
		case STACK_MIDI_EVENT_CHANNEL_AFTERTOUCH:
			param1_name = "Velocity";
			param_count = 1;
			break;
		case STACK_MIDI_EVENT_PITCH_BEND:
			param1_name = "Value";
			param_count = 1;
			break;
		default:
			break;
	}

	if (param1_out != NULL)
	{
		strncpy(param1_out, param1_name, param1_size);
	}

	if (param2_name != NULL && param2_out != NULL)
	{
		strncpy(param2_out, param2_name, param2_size);
	}

	return param_count;
}
