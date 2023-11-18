#ifndef _STACKPROPERTY_H_INCLUDED
#define _STACKPROPERTY_H_INCLUDED

// Includes:
#include <cstdint>
#include <json/json.h>

// Property types:
enum StackPropertyType
{
	STACK_PROPERTY_TYPE_NONE = 0,
	STACK_PROPERTY_TYPE_BOOL,
	STACK_PROPERTY_TYPE_INT8,
	STACK_PROPERTY_TYPE_UINT8,
	STACK_PROPERTY_TYPE_INT16,
	STACK_PROPERTY_TYPE_UINT16,
	STACK_PROPERTY_TYPE_INT32,
	STACK_PROPERTY_TYPE_UINT32,
	STACK_PROPERTY_TYPE_INT64,
	STACK_PROPERTY_TYPE_UINT64,
	STACK_PROPERTY_TYPE_DOUBLE,
	STACK_PROPERTY_TYPE_STRING,
};

// The three versions of the property value:
enum StackPropertyVersion
{
	// The version defined by the user (i.e. what they originally wanted)
	STACK_PROPERTY_VERSION_DEFINED = 0,

	// The version in use now (copied from defined at some point, and possibly then updated by others)
	STACK_PROPERTY_VERSION_LIVE,

	// The version we should use the next time (the owner will eventually set live to this)
	STACK_PROPERTY_VERSION_TARGET,
};

// Pre-define this:
struct StackProperty;

// Typedefs:
typedef void(*stack_property_changed_t)(StackProperty *, StackPropertyVersion, void *);
typedef void* stack_property_validator_t;

// Base Property
struct StackProperty
{
	// The type of the values of the property
	StackPropertyType type;

	// The name of the property
	char *name;

	// A callback to call when the value of the property changes
	stack_property_changed_t changed_callback;

	// User data to pass ot the callback
	void *changed_callback_user_data;

	// Whether change callbacks are paused
	bool change_callbacks_paused;
};

// Definitions: Helper to define a typed property. Each property contains:
//  - super: The StackProperty superclass
//  - defined: The user-defined value of the property
//  - live: The value of the property that is currently in use
//  - target: The value of the property that we're going to use next
//  - validator: A function to call before changing to validate the value
//  - validator_user_data: User data to pass to the callback
#define STACK_PROPERTY_DEFINE(property_type_name, type_name, type_suffix) \
	struct property_type_name; \
	typedef type_name(*stack_property_validator_##type_suffix##_t)(property_type_name *, StackPropertyVersion, const type_name, void *); \
	struct property_type_name \
	{ \
		StackProperty super; \
		type_name defined; \
		type_name live; \
		type_name target; \
		stack_property_validator_##type_suffix##_t validator; \
		void *validator_user_data; \
	};

// Definitons: Helper to define getter and setter functions
#define STACK_PROPERTY_DEFINE_ACCESSORS(type_suffix, type_name) \
	bool stack_property_get_##type_suffix(StackProperty *property, StackPropertyVersion version, type_name *value); \
	bool stack_property_set_##type_suffix(StackProperty *property, StackPropertyVersion version, const type_name value);

// Typed properties:
STACK_PROPERTY_DEFINE(StackPropertyBool, bool, bool)
STACK_PROPERTY_DEFINE(StackPropertyInt8, int8_t, int8)
STACK_PROPERTY_DEFINE(StackPropertyUInt8, uint8_t, uint8)
STACK_PROPERTY_DEFINE(StackPropertyInt16, int16_t, int16)
STACK_PROPERTY_DEFINE(StackPropertyUInt16, uint16_t, uint16)
STACK_PROPERTY_DEFINE(StackPropertyInt32, int32_t, int32)
STACK_PROPERTY_DEFINE(StackPropertyUInt32, uint32_t, uint32)
STACK_PROPERTY_DEFINE(StackPropertyInt64, int64_t, int64)
STACK_PROPERTY_DEFINE(StackPropertyUInt64, uint64_t, uint64)
STACK_PROPERTY_DEFINE(StackPropertyDouble, double, double)
STACK_PROPERTY_DEFINE(StackPropertyString, char *, string)

// Accessors for typed properties:
STACK_PROPERTY_DEFINE_ACCESSORS(bool, bool)
STACK_PROPERTY_DEFINE_ACCESSORS(int8, int8_t)
STACK_PROPERTY_DEFINE_ACCESSORS(uint8, uint8_t)
STACK_PROPERTY_DEFINE_ACCESSORS(int16, int16_t)
STACK_PROPERTY_DEFINE_ACCESSORS(uint16, uint16_t)
STACK_PROPERTY_DEFINE_ACCESSORS(int32, int32_t)
STACK_PROPERTY_DEFINE_ACCESSORS(uint32, uint32_t)
STACK_PROPERTY_DEFINE_ACCESSORS(int64, int64_t)
STACK_PROPERTY_DEFINE_ACCESSORS(uint64, uint64_t)
STACK_PROPERTY_DEFINE_ACCESSORS(double, double)
STACK_PROPERTY_DEFINE_ACCESSORS(string, char*)

// For each type we define:
// stack_property_get_<type>
// @param property The StackProperty object to get the value of
// @param version The version of the property to get the value of
// @param value A pointer of the correct type to receive the value
// @returns A boolean indicating if the call was successful
//
// stack_property_get_<type>
// @param property The StackProperty object to set the value of
// @param version The version of the property to set the value of
// @param value The new value of the property
// @returns A boolean indicating if the call was successful

// Definitions: casting
#define STACK_PROPERTY_BOOL(p) ((p) != NULL && (p)->type == STACK_PROPERTY_TYPE_BOOL ? (StackPropertyBool*)(p) : NULL)
#define STACK_PROPERTY_INT8(p) ((p) != NULL && (p)->type == STACK_PROPERTY_TYPE_INT8 ? (StackPropertyInt8*)(p) : NULL)
#define STACK_PROPERTY_UINT8(p) ((p) != NULL && (p)->type == STACK_PROPERTY_TYPE_UINT8 ? (StackPropertyUInt8*)(p) : NULL)
#define STACK_PROPERTY_INT16(p) ((p) != NULL && (p)->type == STACK_PROPERTY_TYPE_INT16 ? (StackPropertyInt16*)(p) : NULL)
#define STACK_PROPERTY_UINT16(p) ((p) != NULL && (p)->type == STACK_PROPERTY_TYPE_UINT16 ? (StackPropertyUInt16*)(p) : NULL)
#define STACK_PROPERTY_INT32(p) ((p) != NULL && (p)->type == STACK_PROPERTY_TYPE_INT32 ? (StackPropertyInt32*)(p) : NULL)
#define STACK_PROPERTY_UINT32(p) ((p) != NULL && (p)->type == STACK_PROPERTY_TYPE_UINT32 ? (StackPropertyUInt32*)(p) : NULL)
#define STACK_PROPERTY_INT64(p) ((p) != NULL && (p)->type == STACK_PROPERTY_TYPE_INT64 ? (StackPropertyInt64*)(p) : NULL)
#define STACK_PROPERTY_UINT64(p) ((p) != NULL && (p)->type == STACK_PROPERTY_TYPE_UINT64 ? (StackPropertyUInt64*)(p) : NULL)
#define STACK_PROPERTY_DOUBLE(p) ((p) != NULL && (p)->type == STACK_PROPERTY_TYPE_DOUBLE ? (StackPropertyDouble*)(p) : NULL)
#define STACK_PROPERTY_STRING(p) ((p) != NULL && (p)->type == STACK_PROPERTY_TYPE_STRING ? (StackPropertyString*)(p) : NULL)

// Creates a new StackProperty object:
// @param name The name of the property
// @param type The type of the property
// @returns A pointer to a new StackProperty-subclassed object, cast back to StackProperty
StackProperty *stack_property_create(const char *name, StackPropertyType type);

// Destroys a StackProperty object, tidying up any associated data in any sub-classes
// @param property The property to destroy
void stack_property_destroy(StackProperty *property);

// Sets the callback to call when a property's value changes
// @param property The property whose callback should be changed
// @param callback The function to call
// @param user_data User data to pass to the callback function
void stack_property_set_changed_callback(StackProperty *property, stack_property_changed_t callback, void *user_data);

// Pauses or unpauses change callbacks
// @param property The property whose callbacks are to be paused/unpaused
// @param pauses Whether to pause (true) or unpause (false) callbacks
// @param returns The previous value
bool stack_property_pause_change_callback(StackProperty *property, bool paused);

// Sets the callback to call to validate a new value for the property
// @param property The property whose callback should be changed
// @param callback The function to call
// @param user_data User data to pass to the callback function
void stack_property_set_validator(StackProperty *property, stack_property_validator_t validator, void *user_data);

// Writes the property out to JSON
// @param property The property to add to the JSON structure
// @param json_root The JSON structure to add the property to
void stack_property_write_json(StackProperty *property, Json::Value* json_root);

// Copy the defined version of the property to live
// @param property The property to change
void stack_property_copy_defined_to_live(StackProperty *property);

#endif
