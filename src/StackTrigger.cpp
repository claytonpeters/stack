// Includes:
#include "StackTrigger.h"
#include "StackLog.h"
#include "StackJson.h"
#include <map>
#include <cstring>
#include <time.h>
#include <cmath>
using namespace std;

// Map of classes
static StackTriggerClassMap trigger_class_map;

void stack_trigger_init(StackTrigger *trigger, StackCue *cue)
{
	trigger->cue = cue;
	trigger->action = STACK_TRIGGER_ACTION_PLAY;
	stack_log("stack_trigger_init() called - 0x%016lx initialised for cue UID %016lx\n", trigger, cue->uid);
}

// Registers a new trigger type, and stores the functions to create and destroy it
int stack_register_trigger_class(const StackTriggerClass *trigger_class)
{
	// Parameter error checking
	if (trigger_class == NULL)
	{
		return STACKERR_PARAM_NULL;
	}

	// Debug
	stack_log("Registering trigger type '%s'\n", trigger_class->class_name);

	// Validate name pointer
	if (trigger_class->class_name == NULL)
	{
		stack_log("stack_register_trigger_class(): Class name cannot be NULL\n");
		return STACKERR_CLASS_BADNAME;
	}

	// Ensure we don't already have a class of this type
	if (trigger_class_map.find(string(trigger_class->class_name)) != trigger_class_map.end())
	{
		stack_log("stack_register_trigger_class(): Class name already registered\n");
		return STACKERR_CLASS_DUPLICATE;
	}

	// Only the 'StackTrigger' class is allowed to not have a superclass
	if (trigger_class->super_class_name == NULL && strcmp(trigger_class->class_name, "StackTrigger") != 0)
	{
		stack_log("stack_register_trigger_class(): Trigger classes must have a superclass\n");
		return STACKERR_CLASS_NOSUPER;
	}

	// Validate name length
	if (strlen(trigger_class->class_name) <= 0)
	{
		stack_log("stack_register_trigger_class(): Class name cannot be empty\n");
		return STACKERR_CLASS_BADNAME;
	}

	// Validate create function pointer
	if (trigger_class->create_func == NULL)
	{
		stack_log("stack_register_trigger_class(): Class create_func cannot be NULL. An implementation must be provided\n");
		return STACKERR_CLASS_BADCREATE;
	}

	// Validate destroy function pointer
	if (trigger_class->destroy_func == NULL)
	{
		stack_log("stack_register_trigger_class(): Class destroy_func cannot be NULL. An implementation must be provided\n");
		return STACKERR_CLASS_BADDESTROY;
	}

	// Store the class
	trigger_class_map[string(trigger_class->class_name)] = trigger_class;

	return 0;
}

// Constructs a trigger of the given type
// @param type The type of the trigger
// @param cue The cue it's going to become part of
StackTrigger* stack_trigger_new(const char *type, StackCue *cue)
{
	// Locate the class
	auto iter = trigger_class_map.find(string(type));
	if (iter == trigger_class_map.end())
	{
		stack_log("stack_trigger_new(): Unknown class\n");
		return NULL;
	}

	// No need to iterate up through superclasses - we can't be NULL
	return iter->second->create_func(cue);
}

// Destroys a trigger. This calls the destroy() method for the type of the trigger that
// is given in the parameter
// @param trigger The trigger to destroy.
void stack_trigger_destroy(StackTrigger *trigger)
{
	if (trigger == NULL)
	{
		stack_log("stack_trigger_destroy(): Attempted to destroy NULL!\n");
		return;
	}

	stack_log("stack_trigger_destroy(0x%016lx): Destroying trigger for cue UID %016lx\n", trigger, trigger->cue->uid);

	// Locate the class
	auto iter = trigger_class_map.find(string(trigger->_class_name));
	if (iter == trigger_class_map.end())
	{
		stack_log("stack_trigger_destroy(): Unknown class\n");
		return;
	}

	// No need to iterate up through superclasses - we can't be NULL
	iter->second->destroy_func(trigger);
}

const char *stack_trigger_get_name(StackTrigger *trigger)
{
	// Get the class name
	const char *class_name = trigger->_class_name;

	// Look for a function. Iterate through superclasses if we don't have one
	while (class_name != NULL && trigger_class_map[class_name]->get_name_func == NULL)
	{
		class_name = trigger_class_map[class_name]->super_class_name;
	}

	// Call the function
	return trigger_class_map[string(class_name)]->get_name_func(trigger);
}

const char *stack_trigger_get_event_text(StackTrigger *trigger)
{
	// Get the class name
	const char *class_name = trigger->_class_name;

	// Look for a function. Iterate through superclasses if we don't have one
	while (class_name != NULL && trigger_class_map[class_name]->get_event_text_func == NULL)
	{
		class_name = trigger_class_map[class_name]->super_class_name;
	}

	// Call the function
	return trigger_class_map[string(class_name)]->get_event_text_func(trigger);
}

const char *stack_trigger_get_description(StackTrigger *trigger)
{
	// Get the class name
	const char *class_name = trigger->_class_name;

	// Look for a function. Iterate through superclasses if we don't have one
	while (class_name != NULL && trigger_class_map[class_name]->get_description_func == NULL)
	{
		class_name = trigger_class_map[class_name]->super_class_name;
	}

	// Call the function
	return trigger_class_map[string(class_name)]->get_description_func(trigger);
}

StackTriggerAction stack_trigger_get_action(StackTrigger *trigger)
{
	// Get the class name
	const char *class_name = trigger->_class_name;

	// Look for a function. Iterate through superclasses if we don't have one
	while (class_name != NULL && trigger_class_map[class_name]->get_action_func == NULL)
	{
		class_name = trigger_class_map[class_name]->super_class_name;
	}

	// Call the function
	return trigger_class_map[string(class_name)]->get_action_func(trigger);
}

char *stack_trigger_to_json(StackTrigger *trigger)
{
	// Get the class name
	const char *class_name = trigger->_class_name;

	// Start a JSON response value
	Json::Value trigger_root;
	trigger_root["class"] = trigger->_class_name;

	// Iterate up through all the classes
	while (class_name != NULL)
	{
		// Start a node
		trigger_root[class_name] = Json::Value();

		if (trigger_class_map[class_name]->to_json_func)
		{
			if (trigger_class_map[class_name]->free_json_func)
			{
				char *json_data = trigger_class_map[class_name]->to_json_func(trigger);
				stack_json_read_string(json_data, &trigger_root[class_name]);
			}
			else
			{
				stack_log("stack_trigger_to_json(): Warning: Class '%s' has no free_json_func - skipping\n", class_name);
			}
		}
		else
		{
			stack_log("stack_trigger_to_json(): Warning: Class '%s' has no to_json_func - skipping\n", class_name);
		}

		// Iterate up to superclass
		class_name = trigger_class_map[class_name]->super_class_name;
	}

	// Generate JSON string and return it (to be free'd by stack_trigger_free_json)
	Json::StreamWriterBuilder builder;
	return strdup(Json::writeString(builder, trigger_root).c_str());
}

void stack_trigger_free_json(StackTrigger *trigger, char *json_data)
{
	// Get the class name
	const char *class_name = trigger->_class_name;

	// Look for a free_json function. Iterate through superclasses if we don't have one
	while (class_name != NULL && trigger_class_map[class_name]->free_json_func == NULL)
	{
		class_name = trigger_class_map[class_name]->super_class_name;
	}

	// Call the function
	trigger_class_map[string(class_name)]->free_json_func(trigger, json_data);
}

// Generates the trigger from JSON data
void stack_trigger_from_json(StackTrigger *trigger, const char *json_data)
{
	if (trigger == NULL)
	{
		stack_log("stack_trigger_from_json(): NULL trigger passed\n");
		return;
	}

	// Get the class name
	const char *class_name = trigger->_class_name;

	// Look for a from_json function. Iterate through superclasses if we don't have one
	while (class_name != NULL && trigger_class_map[class_name]->from_json_func == NULL)
	{
		class_name = trigger_class_map[class_name]->super_class_name;
	}

	// Call the function
	trigger_class_map[string(class_name)]->from_json_func(trigger, json_data);
}

bool stack_trigger_show_config_ui(StackTrigger *trigger, GtkWidget *parent, bool new_trigger)
{
	// Get the class name
	const char *class_name = trigger->_class_name;

	// Look for a function. Iterate through superclasses if we don't have one
	while (class_name != NULL && trigger_class_map[class_name]->show_config_ui_func == NULL)
	{
		class_name = trigger_class_map[class_name]->super_class_name;
	}

	// Call the function
	return trigger_class_map[string(class_name)]->show_config_ui_func(trigger, parent, new_trigger);
}

char *stack_trigger_config_to_json(const char *class_name)
{
	// Start a JSON response value
	char *json_data = NULL;

	// Iterate up through all the classes
	while (class_name != NULL)
	{
		if (trigger_class_map[class_name]->config_to_json_func)
		{
			if (trigger_class_map[class_name]->config_free_json_func)
			{
				return trigger_class_map[class_name]->config_to_json_func();
			}
			else
			{
				stack_log("stack_trigger_to_json(): Warning: Class '%s' has no config_free_json_func - skipping\n", class_name);
			}
		}
		else
		{
			stack_log("stack_trigger_to_json(): Warning: Class '%s' has no config_to_json_func - skipping\n", class_name);
		}

		// Iterate up to superclass
		class_name = trigger_class_map[class_name]->super_class_name;
	}

	return NULL;
}

void stack_trigger_config_free_json(const char *class_name, char *json_data)
{
	// Look for a free_json function. Iterate through superclasses if we don't have one
	while (class_name != NULL && trigger_class_map[class_name]->config_free_json_func == NULL)
	{
		class_name = trigger_class_map[class_name]->super_class_name;
	}

	// Call the function
	trigger_class_map[string(class_name)]->config_free_json_func(json_data);
}

// Generates the global trigger config from JSON data
void stack_trigger_config_from_json(const char *class_name, const char *json_data)
{
	// Look for a from_json function. Iterate through superclasses if we don't have one
	while (class_name != NULL && trigger_class_map[class_name]->config_from_json_func == NULL)
	{
		class_name = trigger_class_map[class_name]->super_class_name;
	}

	// Call the function
	trigger_class_map[string(class_name)]->config_from_json_func(json_data);
}

StackTrigger* stack_trigger_create_base(StackCue *cue)
{
	// Can't create one of these
	return NULL;
}

void stack_trigger_destroy_base(StackTrigger *trigger)
{
}

const char* stack_trigger_get_name_base(StackTrigger *trigger)
{
	return "";
}

const char* stack_trigger_get_event_text_base(StackTrigger *trigger)
{
	return "";
}

const char* stack_trigger_get_description_base(StackTrigger *trigger)
{
	return "";
}

StackTriggerAction stack_trigger_get_action_base(StackTrigger *trigger)
{
	return trigger->action;
}

char* stack_trigger_to_json_base(StackTrigger *trigger)
{
	Json::Value trigger_root;

	trigger_root["action"] = (Json::UInt64)trigger->action;

	Json::StreamWriterBuilder builder;
	return strdup(Json::writeString(builder, trigger_root).c_str());
}

void stack_trigger_free_json_base(StackTrigger *trigger, char *json_data)
{
	free(json_data);
}

void stack_trigger_from_json_base(StackTrigger *trigger, const char *json_data)
{
	Json::Value trigger_root;

	// Parse JSON data
	stack_json_read_string(json_data, &trigger_root);

	// Get the data that's pertinent to us
	Json::Value& stack_trigger_data = trigger_root["StackTrigger"];

	trigger->action = (StackTriggerAction)stack_trigger_data["action"].asUInt64();
}

bool stack_trigger_show_config_ui_base(StackTrigger *trigger, GtkWidget *parent, bool new_trigger)
{
	if (!new_trigger)
	{
		GtkWidget *message_dialog = NULL;
		message_dialog = gtk_message_dialog_new(GTK_WINDOW(parent), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "No configuration");
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(message_dialog), "This trigger has no configuration options.");
		gtk_window_set_title(GTK_WINDOW(message_dialog), "Stack");
		gtk_dialog_run(GTK_DIALOG(message_dialog));
		gtk_widget_destroy(message_dialog);
	}

	// Everything is always okay here
	return true;
}

char* stack_trigger_config_to_json_base()
{
	return NULL;
}

void stack_trigger_config_free_json_base(char *json_data)
{
}

void stack_trigger_config_from_json_base(const char *json_data)
{
}

// Performs the trigger action on the target cue
void stack_trigger_do_action(StackTrigger *trigger)
{
	// Run the correct action
	stack_cue_list_lock(trigger->cue->parent);
	switch (stack_trigger_get_action(trigger))
	{
		case STACK_TRIGGER_ACTION_STOP:
			stack_cue_stop(trigger->cue);
			break;
		case STACK_TRIGGER_ACTION_PAUSE:
			stack_cue_pause(trigger->cue);
			break;
		case STACK_TRIGGER_ACTION_PLAY:
			stack_cue_play(trigger->cue);
			break;
	}
	stack_cue_list_unlock(trigger->cue->parent);
}

// Initialise the StackTrigger system
void stack_trigger_initsystem()
{
	// Register base trigger type
	StackTriggerClass* stack_trigger_class = new StackTriggerClass{ "StackTrigger", NULL, "No-op abstract", stack_trigger_create_base, stack_trigger_destroy_base, stack_trigger_get_name_base, stack_trigger_get_event_text_base, stack_trigger_get_description_base, stack_trigger_get_action_base, stack_trigger_to_json_base, stack_trigger_free_json_base, stack_trigger_from_json_base, stack_trigger_show_config_ui_base, stack_trigger_config_to_json_base, stack_trigger_config_free_json_base, stack_trigger_config_from_json_base };
	stack_register_trigger_class(stack_trigger_class);
}

const StackTriggerClass *stack_trigger_get_class(const char *name)
{
	auto iter = trigger_class_map.find(string(name));
	if (iter == trigger_class_map.end())
	{
		return NULL;
	}

	return iter->second;
}

const StackTriggerClassMap *stack_trigger_class_map_get()
{
	return &trigger_class_map;
}
