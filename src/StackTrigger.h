#ifndef STACKTRIGGER_H_INCLUDED
#define STACKTRIGGER_H_INCLUDED

// Things defined in this file:
struct StackTrigger;
struct StackTriggerClass;

// Includes:
#include <map>
#include <string>
#include "StackCue.h"

typedef enum StackTriggerAction
{
	STACK_TRIGGER_ACTION_STOP = 0,
	STACK_TRIGGER_ACTION_PAUSE = 1,
	STACK_TRIGGER_ACTION_PLAY = 2,
} StackTriggerAction;

typedef struct StackTrigger
{
	const char *_class_name;

	// The cue to interact with when the trigger is activated
	StackCue *cue;

	// What to do with the cue (Play/Pause/Stop)
	StackTriggerAction action;

} StackTrigger;

// Function typedefs:
typedef StackTrigger*(*stack_trigger_create_t)(StackCue*);
typedef void(*stack_trigger_destroy_t)(StackTrigger*);
typedef const char*(*stack_trigger_get_name_t)(StackTrigger*);
typedef const char*(*stack_trigger_get_event_text_t)(StackTrigger*);
typedef const char*(*stack_trigger_get_description_t)(StackTrigger*);
typedef StackTriggerAction(*stack_trigger_get_action_t)(StackTrigger*);
typedef char*(*stack_trigger_to_json_t)(StackTrigger*);
typedef void(*stack_trigger_free_json_t)(StackTrigger*, char*);
typedef void(*stack_trigger_from_json_t)(StackTrigger*, const char*);
typedef bool(*stack_trigger_show_config_ui_t)(StackTrigger*, GtkWidget*, bool);

// Defines information about a class
typedef struct StackTriggerClass
{
	const char *class_name;
	const char *super_class_name;
	stack_trigger_create_t create_func;
	stack_trigger_destroy_t destroy_func;
	stack_trigger_get_name_t get_name_func;
	stack_trigger_get_event_text_t get_event_text_func;
	stack_trigger_get_description_t get_description_func;
	stack_trigger_get_action_t get_action_func;
	stack_trigger_to_json_t to_json_func;
	stack_trigger_free_json_t free_json_func;
	stack_trigger_from_json_t from_json_func;
	stack_trigger_show_config_ui_t show_config_ui_func;
} StackTriggerClass;

// Typedefs:
typedef std::map<std::string, const StackTriggerClass*> StackTriggerClassMap;

// Functions: Trigger type registration
int stack_register_trigger_class(const StackTriggerClass *trigger_class);

// Functions: Arbitrary trigger creation/deletion
StackTrigger *stack_trigger_new(const char *type, StackCue *cue);
void stack_trigger_destroy(StackTrigger *trigger);

// Functions
void stack_trigger_initsystem();

// Functions:
void stack_trigger_init(StackTrigger *trigger, StackCue *cue);
StackTrigger* stack_trigger_create(StackCue *cue);
void stack_trigger_destroy(StackTrigger *trigger);
const char* stack_trigger_get_name(StackTrigger *trigger);
const char* stack_trigger_get_event_text(StackTrigger *trigger);
const char* stack_trigger_get_description(StackTrigger *trigger);
StackTriggerAction stack_trigger_get_action(StackTrigger *trigger);
char* stack_trigger_to_json(StackTrigger *trigger);
void stack_trigger_free_json(StackTrigger *trigger, char *json_data);
void stack_trigger_from_json(StackTrigger *trigger, const char *json_data);
bool stack_trigger_show_config_ui(StackTrigger *trigger, GtkWidget *parent, bool new_trigger);

// Base trigger functions. These should not be called directly except from
// within subclasses of StackTrigger
StackTrigger* stack_trigger_create_base(StackCue *cue);
void stack_trigger_destroy_base(StackTrigger *trigger);
const char* stack_trigger_get_name_base(StackTrigger *trigger);
const char* stack_trigger_get_event_text_base(StackTrigger *trigger);
const char* stack_trigger_get_description_base(StackTrigger *trigger);
StackTriggerAction stack_trigger_get_action_base(StackTrigger *trigger);
char* stack_trigger_to_json_base(StackTrigger *trigger);
void stack_trigger_free_json_base(StackTrigger *trigger, char *json_data);
void stack_trigger_from_json_base(StackTrigger *trigger, const char *json_data);
bool stack_trigger_show_config_ui_base(StackTrigger *trigger, GtkWidget *parent, bool new_trigger);

// Class functions
const StackTriggerClass *stack_trigger_get_class(const char *name);
const StackTriggerClassMap *stack_trigger_class_map_get();

// Defines:
#define STACK_TRIGGER(_t) ((StackTrigger*)(_t))

#endif
