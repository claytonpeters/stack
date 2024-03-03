// Includes:
#include "StackCue.h"
#include "StackGroupCue.h"
#include "StackLog.h"
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
	static bool seeded = false;
	if (!seeded)
	{
		seeded = true;
		srand(time(NULL));
	}

	cue_uid_t uid = STACK_CUE_UID_NONE;

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

static void stack_cue_ccb(StackProperty *property, StackPropertyVersion version, void *user_data)
{
	if (version == STACK_PROPERTY_VERSION_DEFINED)
	{
		StackCue *cue = STACK_CUE(user_data);
		stack_cue_list_changed(cue->parent, cue, property);
	}
}

// Initialises a base cue object
// @param cue The cue to initialise
void stack_cue_init(StackCue *cue, StackCueList *cue_list)
{
	cue->_class_name = "StackCue";
	cue->parent = cue_list;
	cue->parent_cue = NULL;
	cue->can_have_children = false;
	cue->id = stack_cue_list_get_next_cue_number(cue_list);
	cue->uid = stack_cue_generate_uid();
	cue->rendered_name = strdup("");
	cue->state = STACK_CUE_STATE_STOPPED;
	cue->start_time = 0;
	cue->pause_time = 0;
	cue->paused_time = 0;
	cue->pause_paused_time = 0;
	cue->properties = new StackPropertyMap();
	cue->triggers = new StackTriggerVector();

	// Store the UID in our map
	cue_uid_map[cue->uid] = cue;

	// Add our properties
	StackProperty *name = stack_property_create("name", STACK_PROPERTY_TYPE_STRING);
	stack_cue_add_property(cue, name);
	stack_property_set_changed_callback(name, stack_cue_ccb, cue);

	StackProperty *notes = stack_property_create("notes", STACK_PROPERTY_TYPE_STRING);
	stack_cue_add_property(cue, notes);
	stack_property_set_changed_callback(notes, stack_cue_ccb, cue);

	StackProperty *r = stack_property_create("r", STACK_PROPERTY_TYPE_UINT8);
	stack_cue_add_property(cue, r);
	stack_property_set_uint8(stack_cue_get_property(cue, "r"), STACK_PROPERTY_VERSION_DEFINED, 0);
	stack_property_set_changed_callback(r, stack_cue_ccb, cue);

	StackProperty *g = stack_property_create("g", STACK_PROPERTY_TYPE_UINT8);
	stack_cue_add_property(cue, g);
	stack_property_set_uint8(stack_cue_get_property(cue, "g"), STACK_PROPERTY_VERSION_DEFINED, 0);
	stack_property_set_changed_callback(g, stack_cue_ccb, cue);

	StackProperty *b = stack_property_create("b", STACK_PROPERTY_TYPE_UINT8);
	stack_cue_add_property(cue, b);
	stack_property_set_uint8(stack_cue_get_property(cue, "b"), STACK_PROPERTY_VERSION_DEFINED, 0);
	stack_property_set_changed_callback(b, stack_cue_ccb, cue);

	StackProperty *pre_time = stack_property_create("pre_time", STACK_PROPERTY_TYPE_INT64);
	stack_cue_add_property(cue, pre_time);
	stack_property_set_changed_callback(pre_time, stack_cue_ccb, cue);

	StackProperty *action_time = stack_property_create("action_time", STACK_PROPERTY_TYPE_INT64);
	stack_cue_add_property(cue, action_time);
	stack_property_set_changed_callback(action_time, stack_cue_ccb, cue);

	StackProperty *post_time = stack_property_create("post_time", STACK_PROPERTY_TYPE_INT64);
	stack_cue_add_property(cue, post_time);
	stack_property_set_changed_callback(post_time, stack_cue_ccb, cue);

	StackProperty *post_trigger = stack_property_create("post_trigger", STACK_PROPERTY_TYPE_INT32);
	stack_cue_add_property(cue, post_trigger);
	stack_property_set_changed_callback(post_trigger, stack_cue_ccb, cue);

	stack_log("stack_cue_init() called - initialised cue UID %016lx\n", cue->uid);
}

void stack_cue_add_property(StackCue *cue, StackProperty *property)
{
	(*cue->properties)[string(property->name)] = property;
}

void stack_cue_remove_property(StackCue *cue, const char *property)
{
	cue->properties->erase(string(property));
}

StackProperty *stack_cue_get_property(StackCue *cue, const char *property)
{
	// Find the property
	auto property_iter = cue->properties->find(property);
	if (property_iter == cue->properties->end())
	{
		return NULL;
	}

	// Get a pointer to the base property
	return property_iter->second;
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
	// Get the _live_ version of these properties
	stack_time_t cue_pre_time = 0, cue_action_time = 0, cue_post_time = 0;
	stack_property_get_int64(stack_cue_get_property(cue, "pre_time"), STACK_PROPERTY_VERSION_LIVE, &cue_pre_time);
	stack_property_get_int64(stack_cue_get_property(cue, "action_time"), STACK_PROPERTY_VERSION_LIVE, &cue_action_time);
	stack_property_get_int64(stack_cue_get_property(cue, "post_time"), STACK_PROPERTY_VERSION_LIVE, &cue_post_time);

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
		if (cue_elapsed < cue_pre_time)
		{
			// The pre-wait time is as simple as how long the cue has been running
			*pre = cue_elapsed;
		}
		else
		{
			*pre = cue_pre_time;
		}
	}

	// Return action time if we want it
	if (action != NULL)
	{
		// If we've not got passed the pre-wait time yet, then action time is 0
		if (cue_elapsed < cue_pre_time)
		{
			*action = 0;
		}
		else if (cue_elapsed < cue_pre_time + cue_action_time || cue_action_time < 0)
		{
			// Calculate the action time. This is how long the cue has been runnng
			// less the time we were in pre-wait
			*action = cue_elapsed - cue_pre_time;
		}
		else
		{
			// The action time has elapsed
			*action = cue_action_time;
		}
	}

	// Return post-wait time if we want it
	if (post != NULL)
	{
		int32_t cue_post_trigger = STACK_CUE_WAIT_TRIGGER_NONE;
		stack_property_get_int32(stack_cue_get_property(cue, "post_trigger"), STACK_PROPERTY_VERSION_LIVE, &cue_post_trigger);

		// Post wait time depends on the post-wait trigger
		switch (cue_post_trigger)
		{
			// No post-wait trigger
			case STACK_CUE_WAIT_TRIGGER_NONE:
				*post = 0;
				break;

			// If triggering immediately, it's the same as pre-time
			case STACK_CUE_WAIT_TRIGGER_IMMEDIATE:
				if (cue_elapsed < cue_post_time)
				{
					*post = cue_elapsed;
				}
				else
				{
					*post = cue_post_time;
				}
				break;

			// If triggering after pre, we need to check if pre has completed
			case STACK_CUE_WAIT_TRIGGER_AFTERPRE:
				if (cue_elapsed < cue_pre_time && !cue->post_has_run)
				{
					*post = 0;
				}
				else if (cue_elapsed < cue_pre_time + cue_post_time && !cue->post_has_run)
				{
					*post = cue_elapsed - cue_pre_time;
				}
				else
				{
					*post = cue_post_time;
				}
				break;

			// If triggering after the action, we need to check if action is completed
			case STACK_CUE_WAIT_TRIGGER_AFTERACTION:
				if (cue_action_time < 0 || cue_elapsed < cue_pre_time + cue_action_time)
				{
					*post = 0;
				}
				else if (cue_elapsed < cue_pre_time + cue_action_time + cue_post_time)
				{
					*post = cue_elapsed - cue_pre_time - cue_action_time;
				}
				else
				{
					*post = cue_post_time;
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
	stack_log("Registering cue type '%s'\n", cue_class->class_name);

	// Validate name pointer
	if (cue_class->class_name == NULL)
	{
		stack_log("stack_register_cue_class(): Class name cannot be NULL\n");
		return STACKERR_CLASS_BADNAME;
	}

	// Ensure we don't already have a class of this type
	if (cue_class_map.find(string(cue_class->class_name)) != cue_class_map.end())
	{
		stack_log("stack_register_cue_class(): Class name already registered\n");
		return STACKERR_CLASS_DUPLICATE;
	}

	// Only the 'StackCue' class is allowed to not have a superclass
	if (cue_class->super_class_name == NULL && strcmp(cue_class->class_name, "StackCue") != 0)
	{
		stack_log("stack_register_cue_class(): Cue classes must have a superclass\n");
		return STACKERR_CLASS_NOSUPER;
	}

	// Validate name length
	if (strlen(cue_class->class_name) <= 0)
	{
		stack_log("stack_register_cue_class(): Class name cannot be empty\n");
		return STACKERR_CLASS_BADNAME;
	}

	// Validate create function pointer
	if (cue_class->create_func == NULL)
	{
		stack_log("stack_register_cue_class(): Class create_func cannot be NULL. An implementation must be provided\n");
		return STACKERR_CLASS_BADCREATE;
	}

	// Validate destroy function pointer
	if (cue_class->destroy_func == NULL)
	{
		stack_log("stack_register_cue_class(): Class destroy_func cannot be NULL. An implementation must be provided\n");
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
		stack_log("stack_cue_new(): Unknown class\n");
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
		stack_log("stack_cue_destroy(): Attempted to destroy NULL!\n");
		return;
	}

	stack_log("stack_cue_destroy(0x%p): Destroying cue %lx\n", (void*)cue, cue->uid);

	// Locate the class
	auto iter = cue_class_map.find(string(cue->_class_name));
	if (iter == cue_class_map.end())
	{
		stack_log("stack_cue_destroy(): Unknown class\n");
		return;
	}

	// Save the UID - we need to remove it from the map once the cue is deleted
	cue_uid_t uid = cue->uid;

	// Tidy up properties
	for (auto iter = cue->properties->begin(); iter != cue->properties->end(); iter++)
	{
		stack_property_destroy(iter->second);
	}
	delete cue->properties;

	// Tidy up triggers
	for (auto iter : *cue->triggers)
	{
		stack_trigger_destroy(iter);
	}
	delete cue->triggers;

	// No need to iterate up through superclasses - we can't be NULL
	iter->second->destroy_func(cue);

	// Remove the cue from our UID map
	auto uid_iter = cue_uid_map.find(uid);
	if (uid_iter == cue_uid_map.end())
	{
		stack_log("stack_cue_destroy(): Assertion warning: Cue UID %lx not in map!\n", uid);
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

// Gets the error message
void stack_cue_get_error(StackCue *cue, char *message, size_t size)
{
	// Get the class name
	const char *class_name = cue->_class_name;

	// Look for an error function. Iterate through superclasses if we don't have one
	while (class_name != NULL && cue_class_map[class_name]->get_error_func == NULL)
	{
		class_name = cue_class_map[class_name]->super_class_name;
	}

	// Call the function
	cue_class_map[string(class_name)]->get_error_func(cue, message, size);
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
				stack_log("stack_cue_to_json(): Warning: Class '%s' has no free_json_func - skipping\n", class_name);
			}
		}
		else
		{
			stack_log("stack_cue_to_json(): Warning: Class '%s' has no to_json_func - skipping\n", class_name);
		}

		// Iterate up to superclass
		class_name = cue_class_map[class_name]->super_class_name;
	}

	// Generate JSON string and return it (to be free'd by stack_cue_free_json)
	Json::FastWriter writer;
	return strdup(writer.write(cue_root).c_str());
}

void stack_cue_free_json(StackCue *cue, char *json_data)
{
	// Get the class name
	const char *class_name = cue->_class_name;

	// Look for a free_json function. Iterate through superclasses if we don't have one
	while (class_name != NULL && cue_class_map[class_name]->free_json_func == NULL)
	{
		class_name = cue_class_map[class_name]->super_class_name;
	}

	// Call the function
	return cue_class_map[string(class_name)]->free_json_func(cue, json_data);
}

// Generates the cue from JSON data
void stack_cue_from_json(StackCue *cue, const char *json_data)
{
	if (cue == NULL)
	{
		stack_log("stack_cue_from_json(): NULL cue passed\n");
		return;
	}

	// Get the class name
	const char *class_name = cue->_class_name;

	// Look for a from_json function. Iterate through superclasses if we don't have one
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

// Get the active channels for a cue
size_t stack_cue_get_active_channels(StackCue *cue, bool *active)
{
	// Get the class name
	const char *class_name = cue->_class_name;

	// Look for a get_active_channels function. Iterate through superclasses if we don't have one
	while (class_name != NULL && cue_class_map[class_name]->get_active_channels_func == NULL)
	{
		class_name = cue_class_map[class_name]->super_class_name;
	}

	// Call the function
	return cue_class_map[string(class_name)]->get_active_channels_func(cue, active);
}

const char *stack_cue_get_rendered_name(StackCue *cue)
{
	std::string result;
	char *name = NULL;
	stack_property_get_string(stack_cue_get_property(cue, "name"), STACK_PROPERTY_VERSION_DEFINED, &name);
	const size_t name_len = strlen(name);
	size_t search_start = 0;
	size_t search_end = 0;
	bool in_var = false;

	while (1)
	{
		// If we hit the end of the string, just copy what we had left and
		// stop iterating, regardless of whether we're parsing a variable name
		// or not
		if (name[search_end] == '\0')
		{
			result = result + std::string(&name[search_start], search_end - search_start);
			break;
		}

		if (!in_var)
		{
			// If we hit a dollar sign, we might have hit a variable
			if (name[search_end] == '$')
			{
				// If we're at the end of the string though, just copy the
				// dollar and stop iterating
				if (search_end == name_len - 1)
				{
					result = result + std::string(&name[search_start], search_end - search_start + 1);
					break;
				}

				// If the next char is a brace, then copy what we found so far,
				// and mark that we're in a variable
				if (name[search_end + 1] == '{')
				{
					result = result + std::string(&name[search_start], search_end - search_start);
					in_var = true;
					search_start = search_end;
					search_end++;
				}
			}
		}
		// If we're currently parsing a variable name
		else
		{
			if (name[search_end] == '}')
			{
				std::string variable_name = std::string(&name[search_start + 2], search_end - search_start - 2);
				result = result + std::string(stack_cue_get_field(cue, variable_name.c_str()));

				// We're no longer in a variable
				in_var = false;
				search_start = search_end + 1;
			}
		}

		search_end++;
	}

	// Cache the result and return it
	free(cue->rendered_name);
	cue->rendered_name = strdup(result.c_str());
	return cue->rendered_name;
}

// Gets more audio data from cue
size_t stack_cue_get_audio(StackCue *cue, float *buffer, size_t samples)
{
	// Get the class name
	const char *class_name = cue->_class_name;

	// Look for a get_audio function. Iterate through superclasses if we don't have one
	while (class_name != NULL && cue_class_map[class_name]->get_audio_func == NULL)
	{
		class_name = cue_class_map[class_name]->super_class_name;
	}

	// Call the function
	return cue_class_map[string(class_name)]->get_audio_func(cue, buffer, samples);
}

// Gets a value of a field for a cue
const char *stack_cue_get_field(StackCue *cue, const char *field)
{
	// Get the class name
	const char *class_name = cue->_class_name;

	// Look for a get_field function. Iterate through superclasses if we don't have one
	while (class_name != NULL && cue_class_map[class_name]->get_field_func == NULL)
	{
		class_name = cue_class_map[class_name]->super_class_name;
	}

	// Call the function
	return cue_class_map[string(class_name)]->get_field_func(cue, field);
}

// Gets the icon for the cue
GdkPixbuf *stack_cue_get_icon(StackCue *cue)
{
	// Get the class name
	const char *class_name = cue->_class_name;

	// Look for a get_field function. Iterate through superclasses if we don't have one
	while (class_name != NULL && cue_class_map[class_name]->get_icon_func == NULL)
	{
		class_name = cue_class_map[class_name]->super_class_name;
	}

	// Call the function
	return cue_class_map[string(class_name)]->get_icon_func(cue);
}

// Gets the children for the cue
StackCueStdList *stack_cue_get_children(StackCue *cue)
{
	// Get the class name
	const char *class_name = cue->_class_name;

	// Look for a get_children function. Iterate through superclasses if we don't have one
	while (class_name != NULL && cue_class_map[class_name]->get_children_func == NULL)
	{
		class_name = cue_class_map[class_name]->super_class_name;
	}

	// Call the function
	return cue_class_map[string(class_name)]->get_children_func(cue);
}

// Gets the next cue
StackCue *stack_cue_get_next_cue(StackCue *cue)
{
	// Get the class name
	const char *class_name = cue->_class_name;

	// Look for a get_children function. Iterate through superclasses if we don't have one
	while (class_name != NULL && cue_class_map[class_name]->get_next_cue_func == NULL)
	{
		class_name = cue_class_map[class_name]->super_class_name;
	}

	// Call the function
	return cue_class_map[string(class_name)]->get_next_cue_func(cue);
}

// Add a trigger to the list of triggers
void stack_cue_add_trigger(StackCue *cue, StackTrigger *trigger)
{
	cue->triggers->push_back(trigger);
}

// Remove a trigger from the list of triggers and destroys it
void stack_cue_remove_trigger(StackCue *cue, StackTrigger *trigger)
{
	for (auto iter = cue->triggers->begin(); iter != cue->triggers->end(); iter++)
	{
		if (*iter == trigger)
		{
			cue->triggers->erase(iter);
			stack_trigger_destroy(trigger);
			break;
		}
	}
}

// Removes all trigger from the list of triggers and destroys them
void stack_cue_clear_triggers(StackCue *cue)
{
	// Destroy all the triggers
	for (auto iter : *cue->triggers)
	{
		stack_trigger_destroy(iter);
	}

	// Clear the vector
	cue->triggers->clear();
}

// Initialise the StackCue system
void stack_cue_initsystem()
{
	// Register base cue type
	StackCueClass* stack_cue_class = new StackCueClass{ "StackCue", NULL, stack_cue_create_base, stack_cue_destroy_base, stack_cue_play_base, stack_cue_pause_base, stack_cue_stop_base, stack_cue_pulse_base, stack_cue_set_tabs_base, stack_cue_unset_tabs_base, stack_cue_to_json_base, stack_cue_free_json_base, stack_cue_from_json_void, stack_cue_get_error_base, stack_cue_get_active_channels_base, stack_cue_get_audio_base, stack_cue_get_field_base, stack_cue_get_icon_base, stack_cue_get_children_base, stack_cue_get_next_cue_base };
	stack_register_cue_class(stack_cue_class);

	// Group cues are built-in, not plugins
	stack_group_cue_register();
}
