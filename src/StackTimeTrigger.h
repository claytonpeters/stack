#ifndef STACKTIMETRIGGER_H_INCLUDED
#define STACKTIMETRIGGER_H_INCLUDED

// Includes:
#include "StackTrigger.h"

struct StackTimeTrigger
{
	// Superclass
	StackTrigger super;

	// The description for the trigger
	char *description;

	// The date of the trigger time (or 0 if today)
	uint16_t year;
	uint8_t month;
	uint8_t day;

	// The time of the trigger time
	uint8_t hour;
	uint8_t minute;
	uint8_t second;

	// How often to repeat (seconds)
	uint32_t repeat;

	// The last time the trigger was run
	uint64_t last_trigger_ts;

	// The next time the trigger should run
	uint64_t next_trigger_ts;

	// Configured flag (i.e. are the date/time valid)
	bool configured;

	// Buffer for our event text
	char event_text[128];
};

// Defines:
#define STACK_TIME_TRIGGER(_t) ((StackTimeTrigger*)(_t))

#endif
