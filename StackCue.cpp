// Includes:
#include "StackCue.h"
#include <map>
#include <cstring>
#include <time.h>
#include <cmath>
#include <json/json.h>
using namespace std;

// Map of classes
static map<string, const StackCueClass*> cue_class_map;

// Map of UIDs
static map<cue_uid_t, StackCue*> cue_uid_map;

// Generates a UID
static cue_uid_t stack_cue_generate_uid()
{
	cue_uid_t uid = 0;
	
	// Repeatedly generate UUIDs until we find a free one
	do
	{
#if RAND_MAX < 255
#    error RAND_MAX too small, must be at least 2^8 - 1 (8-bit) for UUID generation
#elif RAND_MAX < 65535
#    warning RAND_MAX is less than 2^16 - 1 (16-bit), falling back to 8-bit UUID generation method
		for (size_t i = 0; i < 8; i++)
		{
			uid = (uid << 8) | (cue_uid_t)(rand() % 0xff);
		}
#else
	for (size_t i = 0; i < 4; i++)
	{
		uid = (uid << 16) | (cue_uid_t)(rand() % 0xffff);
	}
#endif
	} while (cue_uid_map.find(uid) != cue_uid_map.end() && uid != STACK_CUE_UID_NONE);
	
	return uid;
}

// Initialises a base cue object
// @param cue The cue to initialise
void stack_cue_init(StackCue *cue, StackCueList *cue_list)
{
	cue->_class_name = "StackCue";
	cue->parent = cue_list;
	cue->can_have_children = false;
	cue->id = stack_cue_list_get_next_cue_number(cue_list);
	cue->uid = stack_cue_generate_uid();
	cue->name = strdup("");
	cue->notes = strdup("");
	cue->pre_time = 0;
	cue->action_time = 0;
	cue->post_time = 0;
	cue->state = STACK_CUE_STATE_STOPPED;
	cue->r = 210;
	cue->g = 210;
	cue->b = 210;
	cue->start_time = 0;
	cue->pause_time = 0;
	cue->paused_time = 0;
	cue->pause_paused_time = 0;
	
	// Store the UID in our map
	cue_uid_map[cue->uid] = cue;

	fprintf(stderr, "stack_cue_init() called - initialised cue UID %016lx\n", cue->uid);
}

// Finds the cue by the given UID and returns it
// @param uid The UID of the cue to look for
// @returns A pointer to the cue or NULL if not found
StackCue *stack_cue_get_by_uid(cue_uid_t uid)
{
	auto iter = cue_uid_map.find(uid);
	if (iter == cue_uid_map.end())
	{
		return NULL;
	}
	
	return iter->second;
}

// Calculates how long a cue has been running
void stack_cue_get_running_times(StackCue *cue, stack_time_t clocktime, stack_time_t *pre, stack_time_t *action, stack_time_t *post, stack_time_t *paused, stack_time_t *real, stack_time_t *total)
{
	// If we're paused, re-calculate the paused time
	if (cue->state == STACK_CUE_STATE_PAUSED)
	{
		// Calculate how long we've been paused on this instance of us being
		// paused
		stack_time_t this_pause_time = clocktime - cue->pause_time;
		
		// Update the total cue paused time
		cue->paused_time = cue->pause_paused_time + this_pause_time;
	}
	
	// Calculate how long the cue has been running both in clock time (time
	// since it was activated) and cue time (clock time minus how long the cue
	// has been paused for)
	stack_time_t clock_elapsed = (clocktime - cue->start_time);
	stack_time_t cue_elapsed = clock_elapsed - cue->paused_time;

	// Return the calculated paused_time if we want it
	if (paused != NULL)
	{
		*paused = cue->paused_time;
	}

	// Return the calculated clock_elapsed time if we want it
	if (real != NULL)
	{
		*real = clock_elapsed;
	}
	
	// Return the total (cue elapsed) time if we want it
	if (total != NULL)
	{
		*total = cue_elapsed;
	}
	
	// Return pre-wait time if we want it
	if (pre != NULL)
	{
		// If less than the pre-wait time has elapsed
		if (cue_elapsed < cue->pre_time)
		{
			// The pre-wait time is as simple as how long the cue has been running
			*pre = cue_elapsed;
		}
		else
		{
			*pre = cue->pre_time;
		}
	}
	
	// Return action time if we want it
	if (action != NULL)
	{
		// If we've not got passed the pre-wait time yet, then action time is 0
		if (cue_elapsed < cue->pre_time)
		{
			*action = 0;
		}
		else if (cue_elapsed < cue->pre_time + cue->action_time)
		{
			// Calculate the action time. This is how long the cue has been runnng	
			// less the time we were in pre-wait
			*action = cue_elapsed - cue->pre_time;
		}
		else
		{
			// The action time has elapsed
			*action = cue->action_time;
		}
	}

	// Return post-wait time if we want it
	if (post != NULL)
	{
		// Post wait time depends on the post-wait trigger
		switch (cue->post_trigger)
		{
			// No post-wait trigger
			case STACK_CUE_WAIT_TRIGGER_NONE:
				*post = 0;
				break;
			
			// If triggering immediately, it's the same as pre-time
			case STACK_CUE_WAIT_TRIGGER_IMMEDIATE:
				if (cue_elapsed < cue->post_time)
				{
					*post = cue_elapsed;
				}
				else
				{
					*post = cue->post_time;
				}
				break;
			
			// If triggering after pre, we need to check if pre has completed
			case STACK_CUE_WAIT_TRIGGER_AFTERPRE:
				if (cue_elapsed < cue->pre_time)
				{
					*post = 0;
				}
				else if (cue_elapsed < cue->pre_time + cue->post_time)
				{
					*post = cue_elapsed - cue->pre_time;
				}
				else
				{
					*post = cue->post_time;
				}
				break;
			
			// If triggering after the action, we need to check if action is completed
			case STACK_CUE_WAIT_TRIGGER_AFTERACTION:
				if (cue_elapsed < cue->pre_time + cue->action_time)
				{
					*post = 0;
				}
				else if (cue_elapsed < cue->pre_time + cue->action_time + cue->post_time)
				{
					*post = cue_elapsed - cue->pre_time - cue->action_time;
				}
				else
				{
					*post = cue->post_time;
				}
				break;
		}
	}
}

// Registers a new cue type, and stores the functions to create and destroy it
int stack_register_cue_class(const StackCueClass *cue_class)
{
	// Parameter error checking
	if (cue_class == NULL)
	{
		return STACKERR_PARAM_NULL;
	}
	
	// Debug
	fprintf(stderr, "Registering cue type '%s'\n", cue_class->class_name);
	
	// Validate name pointer
	if (cue_class->class_name == NULL)
	{
		fprintf(stderr, "stack_register_cue_class(): Class name cannot be NULL\n");
		return STACKERR_CLASS_BADNAME;
	}
	
	// Ensure we don't already have a class of this type
	if (cue_class_map.find(string(cue_class->class_name)) != cue_class_map.end())
	{
		fprintf(stderr, "stack_register_cue_class(): Class name already registered\n");
		return STACKERR_CLASS_DUPLICATE;
	}
	
	// Only the 'StackCue' class is allowed to not have a superclass
	if (cue_class->super_class_name == NULL && strcmp(cue_class->class_name, "StackCue") != 0)
	{
		fprintf(stderr, "stack_register_cue_class(): Cue classes must have a superclass\n");
		return STACKERR_CLASS_NOSUPER;
	}
	
	// Validate name length
	if (strlen(cue_class->class_name) <= 0)
	{
		fprintf(stderr, "stack_register_cue_class(): Class name cannot be empty\n");
		return STACKERR_CLASS_BADNAME;
	}
	
	// Validate create function pointer
	if (cue_class->create_func == NULL)
	{
		fprintf(stderr, "stack_register_cue_class(): Class create_func cannot be NULL. An implementation must be provided\n");
		return STACKERR_CLASS_BADCREATE;
	}
	
	// Validate destroy function pointer
	if (cue_class->destroy_func == NULL)
	{
		fprintf(stderr, "stack_register_cue_class(): Class destroy_func cannot be NULL. An implementation must be provided\n");
		return STACKERR_CLASS_BADDESTROY;
	}
	
	// Store the class
	cue_class_map[string(cue_class->class_name)] = cue_class;
	
	return 0;
}

// Constructs a cue of the given type
// @param type The type of the cue
// @param cue_list The cue list it's going to become part of
StackCue* stack_cue_new(const char *type, StackCueList *cue_list)
{
	// Locate the class
	auto iter = cue_class_map.find(string(type));
	if (iter == cue_class_map.end())
	{
		fprintf(stderr, "stack_cue_new(): Unknown class\n");
		return NULL;
	}
	
	// No need to iterate up through superclasses - we can't be NULL
	return iter->second->create_func(cue_list);
}
	
// Destroys a cue. This calls the destroy() method for the type of the cue that
// is given in the parameter
// @param cue The cue to destroy. 
void stack_cue_destroy(StackCue *cue)
{
	if (cue == NULL)
	{
		fprintf(stderr, "stack_cue_destroy(): Attempted to destroy NULL!\n");
		return;
	}

	// Locate the class
	auto iter = cue_class_map.find(string(cue->_class_name));
	if (iter == cue_class_map.end())
	{
		fprintf(stderr, "stack_cue_destroy(): Unknown class\n");
		return;
	}

	// No need to iterate up through superclasses - we can't be NULL
	iter->second->destroy_func(cue);
	
	// Remove the cue from our UID map
	auto uid_iter = cue_uid_map.find(cue->uid);
	if (uid_iter == cue_uid_map.end())
	{
		fprintf(stderr, "stack_cue_destroy(): Assertion warning: Cue UID not in map!\n");
		return;
	}
	else
	{
		cue_uid_map.erase(uid_iter);
	}
}

// Starts cue playback
bool stack_cue_play(StackCue *cue)
{
	// Get the class name
	const char *class_name = cue->_class_name;
	
	// Look for a playback function. Iterate through superclasses if we don't have one
	while (class_name != NULL && cue_class_map[class_name]->play_func == NULL)
	{
		class_name = cue_class_map[class_name]->super_class_name;
	}
	
	// Call the function
	return cue_class_map[string(class_name)]->play_func(cue);
}

// Pauses cue playback
void stack_cue_pause(StackCue *cue)
{
	// Get the class name
	const char *class_name = cue->_class_name;
	
	// Look for a pause function. Iterate through superclasses if we don't have one
	while (class_name != NULL && cue_class_map[class_name]->pause_func == NULL)
	{
		class_name = cue_class_map[class_name]->super_class_name;
	}
	
	// Call the function
	cue_class_map[string(class_name)]->pause_func(cue);
}

// Stops cue playback
void stack_cue_stop(StackCue *cue)
{
	// Get the class name
	const char *class_name = cue->_class_name;
	
	// Look for a stop function. Iterate through superclasses if we don't have one
	while (class_name != NULL && cue_class_map[class_name]->stop_func == NULL)
	{
		class_name = cue_class_map[class_name]->super_class_name;
	}
	
	// Call the function
	cue_class_map[string(class_name)]->stop_func(cue);
}

// Heartbeat callback for an active cue
void stack_cue_pulse(StackCue *cue, stack_time_t clock)
{
	// Get the class name
	const char *class_name = cue->_class_name;
	
	// Look for a pulse function. Iterate through superclasses if we don't have one
	while (class_name != NULL && cue_class_map[class_name]->pulse_func == NULL)
	{
		class_name = cue_class_map[class_name]->super_class_name;
	}
	
	// Call the function
	cue_class_map[string(class_name)]->pulse_func(cue, clock);
}

// Sets up the properties tabs on the given notebook
void stack_cue_set_tabs(StackCue *cue, GtkNotebook *notebook)
{
	// Get the class name
	const char *class_name = cue->_class_name;
	
	// Look for a set tabs function. Iterate through superclasses if we don't have one
	while (class_name != NULL && cue_class_map[class_name]->set_tabs_func == NULL)
	{
		class_name = cue_class_map[class_name]->super_class_name;
	}
	
	// Call the function
	cue_class_map[string(class_name)]->set_tabs_func(cue, notebook);
}

// Removes the properties tabs on the given notebook
void stack_cue_unset_tabs(StackCue *cue, GtkNotebook *notebook)
{
	// Get the class name
	const char *class_name = cue->_class_name;
	
	// Look for an unset tabs function. Iterate through superclasses if we don't have one
	while (class_name != NULL && cue_class_map[class_name]->unset_tabs_func == NULL)
	{
		class_name = cue_class_map[class_name]->super_class_name;
	}
	
	// Call the function
	cue_class_map[string(class_name)]->unset_tabs_func(cue, notebook);
}

char *stack_cue_to_json(StackCue *cue)
{
	// Get the class name
	const char *class_name = cue->_class_name;
	
	// Start a JSON response value
	Json::Value cue_root;
	cue_root["class"] = cue->_class_name;

	// Iterate up through all the classes
	while (class_name != NULL)
	{
		// Start a node
		cue_root[class_name] = Json::Value();
		
		if (cue_class_map[class_name]->to_json_func)
		{
			if (cue_class_map[class_name]->free_json_func)
			{
				Json::Reader reader;
				char *json_data = cue_class_map[class_name]->to_json_func(cue);
				reader.parse(json_data, cue_root[class_name]);
			}
			else
			{
				fprintf(stderr, "stack_cue_to_json(): Warning: Class '%s' has no free_json_func - skipping\n", class_name);
			}
		}
		else
		{
			fprintf(stderr, "stack_cue_to_json(): Warning: Class '%s' has no to_json_func - skipping\n", class_name);
		}
		
		// Iterate up to superclass
		class_name = cue_class_map[class_name]->super_class_name;
	}
	
	// Generate JSON string and return it (to be free'd by stack_cue_free_json)
	Json::FastWriter writer;
	return strdup(writer.write(cue_root).c_str());
}

void stack_cue_free_json(char *json_data)
{
	free(json_data);
}

// Generates the cue from JSON data
void stack_cue_from_json(StackCue *cue, const char *json_data)
{
	if (cue == NULL)
	{
		fprintf(stderr, "stack_cue_from_json(): NULL cue passed\n");
		return;
	}
	
	// Get the class name
	const char *class_name = cue->_class_name;
	
	// Look for a from_json tabs function. Iterate through superclasses if we don't have one
	while (class_name != NULL && cue_class_map[class_name]->from_json_func == NULL)
	{
		class_name = cue_class_map[class_name]->super_class_name;
	}
	
	// Call the function
	cue_class_map[string(class_name)]->from_json_func(cue, json_data);
}

static void stack_cue_from_json_void(StackCue *cue, const char *json_data)
{
	// Does nothing in the base implementation
}

// Initialise the StackCue system
void stack_cue_initsystem()
{
	// Register base cue type
	StackCueClass* stack_cue_class = new StackCueClass{ "StackCue", NULL, stack_cue_create_base, stack_cue_destroy_base, stack_cue_play_base, stack_cue_pause_base, stack_cue_stop_base, stack_cue_pulse_base, stack_cue_set_tabs_base, stack_cue_unset_tabs_base, stack_cue_to_json_base, stack_cue_free_json_base, stack_cue_from_json_void };
	stack_register_cue_class(stack_cue_class);
}

