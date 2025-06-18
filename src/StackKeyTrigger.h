#ifndef STACKKEYTRIGGER_H_INCLUDED
#define STACKKEYTRIGGER_H_INCLUDED

// Includes:
#include "StackTrigger.h"

struct StackKeyTrigger
{
	// Superclass
	StackTrigger super;

	guint event_type;
	guint keyval;
	gulong handler_id;
	StackCueListWidget *sclw;

	char *description;

	// Whether we've currently got the seat grabbed
	bool grabbed;
};

// Defines:
#define STACK_KEY_TRIGGER(_t) ((StackKeyTrigger*)(_t))

#endif
