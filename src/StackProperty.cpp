// Includes:
#include "StackProperty.h"
#include "StackLog.h"
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>

#define CASE_PROPERTY_INTS(size) \
	case STACK_PROPERTY_TYPE_INT##size: \
		StackPropertyInt##size *p_int##size; \
		p_int##size = new StackPropertyInt##size; \
		p_int##size->defined = 0; \
		p_int##size->live = 0; \
		p_int##size->target = 0; \
		p_int##size->validator = NULL; \
		p = (StackProperty*)p_int##size; \
		break; \
	case STACK_PROPERTY_TYPE_UINT##size: \
		StackPropertyUInt##size *p_uint##size; \
		p_uint##size = new StackPropertyUInt##size; \
		p_uint##size->defined = 0; \
		p_uint##size->live = 0; \
		p_uint##size->target = 0; \
		p_uint##size->validator = NULL; \
		p = (StackProperty*)p_uint##size; \
		break;

StackProperty *stack_property_create(const char *property, StackPropertyType type)
{
	StackProperty *p = NULL;
	switch (type)
	{
		case STACK_PROPERTY_TYPE_BOOL:
			StackPropertyBool *p_bool;
			p_bool = new StackPropertyBool;
			p_bool->defined = false;
			p_bool->live = false;
			p_bool->target = false;
			p_bool->validator = NULL;
			p = (StackProperty*)p_bool;
			break;
		CASE_PROPERTY_INTS(8)
		CASE_PROPERTY_INTS(16)
		CASE_PROPERTY_INTS(32)
		CASE_PROPERTY_INTS(64)
		case STACK_PROPERTY_TYPE_DOUBLE:
			StackPropertyDouble *p_double;
			p_double = new StackPropertyDouble;
			p_double->defined = 0.0;
			p_double->live = 0.0;
			p_double->target = 0.0;
			p_double->validator = NULL;
			p = (StackProperty*)p_double;
			break;
		case STACK_PROPERTY_TYPE_STRING:
			StackPropertyString *p_string;
			p_string = new StackPropertyString;
			p_string->defined = strdup("");
			p_string->live = strdup("");
			p_string->target = strdup("");
			p_string->validator = NULL;
			p = (StackProperty*)p_string;
			break;
		default:
			return NULL;
	}

	// Set up the property
	p->type = type;
	p->name = strdup(property);
	p->changed_callback = NULL;
	p->changed_callback_user_data = NULL;
	p->change_callbacks_paused = 0;
	p->nullable = 0;
	p->version_defined_null = 0;
	p->version_live_null = 0;
	p->version_target_null = 0;

	return p;
}

void stack_property_destroy(StackProperty *property)
{
	if (property == NULL)
	{
		return;
	}

	// Base tidy up
	free(property->name);

	// Typed tidy up
	switch (property->type)
	{
		case STACK_PROPERTY_TYPE_NONE:
			break;
		case STACK_PROPERTY_TYPE_BOOL:
			delete (StackPropertyBool*)property;
			break;
		case STACK_PROPERTY_TYPE_INT8:
			delete (StackPropertyInt8*)property;
			break;
		case STACK_PROPERTY_TYPE_UINT8:
			delete (StackPropertyUInt8*)property;
			break;
		case STACK_PROPERTY_TYPE_INT16:
			delete (StackPropertyInt16*)property;
			break;
		case STACK_PROPERTY_TYPE_UINT16:
			delete (StackPropertyUInt16*)property;
			break;
		case STACK_PROPERTY_TYPE_INT32:
			delete (StackPropertyInt32*)property;
			break;
		case STACK_PROPERTY_TYPE_UINT32:
			delete (StackPropertyUInt32*)property;
			break;
		case STACK_PROPERTY_TYPE_INT64:
			delete (StackPropertyInt64*)property;
			break;
		case STACK_PROPERTY_TYPE_UINT64:
			delete (StackPropertyUInt64*)property;
			break;
		case STACK_PROPERTY_TYPE_DOUBLE:
			delete (StackPropertyDouble*)property;
			break;
		case STACK_PROPERTY_TYPE_STRING:
			StackPropertyString *p_string = (StackPropertyString*)property;
			free(p_string->defined);
			free(p_string->live);
			free(p_string->target);
			delete p_string;
			break;
	}
}

// Returns the name of the property
// @returns The name of the property
const char *stack_property_get_name(const StackProperty *property)
{
	if (property == NULL)
	{
		return NULL;
	}

	return property->name;
}

// Returns whether or not a property is nullable
// @param property The property to return the nullable attribute off
// @returns A boolean indicating if the property is nullable
bool stack_property_get_nullable(const StackProperty *property)
{
	if (property == NULL)
	{
		return false;
	}

	return property->nullable;
}

// Sets whether or not a property is nullable
// @param property The property to set the nullable attribute off
// @param nullable A boolean indicating if the property should be nullable
void stack_property_set_nullable(StackProperty *property, bool nullable)
{
	if (property == NULL)
	{
		return;
	}

	property->nullable = nullable;
}

void stack_property_set_changed_callback(StackProperty *property, stack_property_changed_t callback, void *user_data)
{
	if (property == NULL)
	{
		return;
	}

	property->changed_callback = callback;
	property->changed_callback_user_data = user_data;
}

bool stack_property_pause_change_callback(StackProperty *property, bool paused)
{
	if (property == NULL)
	{
		return false;
	}

	bool previous = property->change_callbacks_paused;
	property->change_callbacks_paused = paused;
	return previous;
}

void stack_property_set_validator(StackProperty *property, stack_property_validator_t validator, void *user_data)
{
	if (property == NULL)
	{
		return;
	}

	// Typed tidy up
	switch (property->type)
	{
		case STACK_PROPERTY_TYPE_NONE:
			break;
		case STACK_PROPERTY_TYPE_BOOL:
			((StackPropertyBool*)property)->validator = (stack_property_validator_bool_t)validator;
			((StackPropertyBool*)property)->validator_user_data = user_data;
			break;
		case STACK_PROPERTY_TYPE_INT8:
			((StackPropertyInt8*)property)->validator = (stack_property_validator_int8_t)validator;
			((StackPropertyInt8*)property)->validator_user_data = user_data;
			break;
		case STACK_PROPERTY_TYPE_UINT8:
			((StackPropertyUInt8*)property)->validator = (stack_property_validator_uint8_t)validator;
			((StackPropertyUInt8*)property)->validator_user_data = user_data;
			break;
		case STACK_PROPERTY_TYPE_INT16:
			((StackPropertyInt16*)property)->validator = (stack_property_validator_int16_t)validator;
			((StackPropertyInt16*)property)->validator_user_data = user_data;
			break;
		case STACK_PROPERTY_TYPE_UINT16:
			((StackPropertyUInt16*)property)->validator = (stack_property_validator_uint16_t)validator;
			((StackPropertyUInt16*)property)->validator_user_data = user_data;
			break;
		case STACK_PROPERTY_TYPE_INT32:
			((StackPropertyInt32*)property)->validator = (stack_property_validator_int32_t)validator;
			((StackPropertyInt32*)property)->validator_user_data = user_data;
			break;
		case STACK_PROPERTY_TYPE_UINT32:
			((StackPropertyUInt32*)property)->validator = (stack_property_validator_uint32_t)validator;
			((StackPropertyUInt32*)property)->validator_user_data = user_data;
			break;
		case STACK_PROPERTY_TYPE_INT64:
			((StackPropertyInt64*)property)->validator = (stack_property_validator_int64_t)validator;
			((StackPropertyInt64*)property)->validator_user_data = user_data;
			break;
		case STACK_PROPERTY_TYPE_UINT64:
			((StackPropertyUInt64*)property)->validator = (stack_property_validator_uint64_t)validator;
			((StackPropertyUInt64*)property)->validator_user_data = user_data;
			break;
		case STACK_PROPERTY_TYPE_DOUBLE:
			((StackPropertyDouble*)property)->validator = (stack_property_validator_double_t)validator;
			((StackPropertyDouble*)property)->validator_user_data = user_data;
			break;
		case STACK_PROPERTY_TYPE_STRING:
			((StackPropertyString*)property)->validator = (stack_property_validator_string_t)validator;
			((StackPropertyString*)property)->validator_user_data = user_data;
			break;
	}
}

// Helper define that creates the getter for a fundamental type property
#define DEFINE_STACK_PROPERTY_GETTER(type_suffix, type_name, type_enum, property_type_name) \
	bool stack_property_get_##type_suffix(const StackProperty *property, StackPropertyVersion version, type_name *value) \
	{ \
		if (property == NULL) \
		{ \
			stack_log("stack_property_get_" #type_suffix "(): NULL property given\n"); \
			return false; \
		} \
		if (property->type != type_enum) \
		{ \
			stack_log("stack_property_get_" #type_suffix "(): Supplied property was not of type " #type_enum "\n"); \
			return false; \
		} \
		if (value == NULL) \
		{ \
			stack_log("stack_property_get_" #type_suffix "(): NULL destination given\n"); \
			return false; \
		} \
		property_type_name *p_cast = (property_type_name*)property; \
		switch (version) \
		{ \
			case STACK_PROPERTY_VERSION_DEFINED: \
				*value = p_cast->defined; \
				break; \
			case STACK_PROPERTY_VERSION_LIVE: \
				*value = p_cast->live; \
				break; \
			case STACK_PROPERTY_VERSION_TARGET: \
				*value = p_cast->target; \
				break; \
			default: \
				return false; \
		} \
		return true; \
	}

// Helper define that creates the getter for a fundamental type property
#define DEFINE_STACK_PROPERTY_SETTER(type_suffix, type_name, type_enum, property_type_name) \
	bool stack_property_set_##type_suffix(StackProperty *property, StackPropertyVersion version, type_name value) \
	{ \
		if (property == NULL) \
		{ \
			stack_log("stack_property_set_" #type_suffix "(): NULL property given\n"); \
			return false; \
		} \
		if (property->type != type_enum) \
		{ \
			stack_log("stack_property_set_" #type_suffix "(): Supplied property was not of type " #type_enum "\n"); \
			return false; \
		} \
		property_type_name *p_cast = (property_type_name*)property; \
		type_name *target = NULL; \
		switch (version) \
		{ \
			case STACK_PROPERTY_VERSION_DEFINED: \
				target = &p_cast->defined; \
				break; \
			case STACK_PROPERTY_VERSION_LIVE: \
				target = &p_cast->live; \
				break; \
			case STACK_PROPERTY_VERSION_TARGET: \
				target = &p_cast->target; \
				break; \
			default: \
				return false; \
		} \
		type_name last_value = *target; \
		if (last_value != value) \
		{ \
			*target = (p_cast->validator == NULL ? value : p_cast->validator(p_cast, version, value, p_cast->validator_user_data)); \
			if (!property->change_callbacks_paused && property->changed_callback != NULL) \
			{ \
				property->changed_callback(property, version, property->changed_callback_user_data); \
			} \
		} \
		return true; \
	}

// Define our fundamental getters
DEFINE_STACK_PROPERTY_GETTER(bool, bool, STACK_PROPERTY_TYPE_BOOL, StackPropertyBool)
DEFINE_STACK_PROPERTY_GETTER(int8, int8_t, STACK_PROPERTY_TYPE_INT8, StackPropertyInt8)
DEFINE_STACK_PROPERTY_GETTER(uint8, uint8_t, STACK_PROPERTY_TYPE_UINT8, StackPropertyUInt8)
DEFINE_STACK_PROPERTY_GETTER(int16, int16_t, STACK_PROPERTY_TYPE_INT16, StackPropertyInt16)
DEFINE_STACK_PROPERTY_GETTER(uint16, uint16_t, STACK_PROPERTY_TYPE_UINT16, StackPropertyUInt16)
DEFINE_STACK_PROPERTY_GETTER(int32, int32_t, STACK_PROPERTY_TYPE_INT32, StackPropertyInt32)
DEFINE_STACK_PROPERTY_GETTER(uint32, uint32_t, STACK_PROPERTY_TYPE_UINT32, StackPropertyUInt32)
DEFINE_STACK_PROPERTY_GETTER(int64, int64_t, STACK_PROPERTY_TYPE_INT64, StackPropertyInt64)
DEFINE_STACK_PROPERTY_GETTER(uint64, uint64_t, STACK_PROPERTY_TYPE_UINT64, StackPropertyUInt64)
DEFINE_STACK_PROPERTY_GETTER(double, double, STACK_PROPERTY_TYPE_DOUBLE, StackPropertyDouble)
DEFINE_STACK_PROPERTY_GETTER(string, char*, STACK_PROPERTY_TYPE_STRING, StackPropertyString)

// Define our fundamental setters
DEFINE_STACK_PROPERTY_SETTER(bool, bool, STACK_PROPERTY_TYPE_BOOL, StackPropertyBool)
DEFINE_STACK_PROPERTY_SETTER(int8, int8_t, STACK_PROPERTY_TYPE_INT8, StackPropertyInt8)
DEFINE_STACK_PROPERTY_SETTER(uint8, uint8_t, STACK_PROPERTY_TYPE_UINT8, StackPropertyUInt8)
DEFINE_STACK_PROPERTY_SETTER(int16, int16_t, STACK_PROPERTY_TYPE_INT16, StackPropertyInt16)
DEFINE_STACK_PROPERTY_SETTER(uint16, uint16_t, STACK_PROPERTY_TYPE_UINT16, StackPropertyUInt16)
DEFINE_STACK_PROPERTY_SETTER(int32, int32_t, STACK_PROPERTY_TYPE_INT32, StackPropertyInt32)
DEFINE_STACK_PROPERTY_SETTER(uint32, uint32_t, STACK_PROPERTY_TYPE_UINT32, StackPropertyUInt32)
DEFINE_STACK_PROPERTY_SETTER(int64, int64_t, STACK_PROPERTY_TYPE_INT64, StackPropertyInt64)
DEFINE_STACK_PROPERTY_SETTER(uint64, uint64_t, STACK_PROPERTY_TYPE_UINT64, StackPropertyUInt64)
DEFINE_STACK_PROPERTY_SETTER(double, double, STACK_PROPERTY_TYPE_DOUBLE, StackPropertyDouble)

// We need a special case for set string to deal with allocations
bool stack_property_set_string(StackProperty *property, StackPropertyVersion version, const char *value)
{
	// Validate inputs
	if (property == NULL)
	{
		stack_log("stack_property_set_string(): NULL property given\n");
		return false;
	}
	if (property->type != STACK_PROPERTY_TYPE_STRING)
	{
		stack_log("stack_property_set_string(): Supplied property was not of type STACK_PROPERTY_TYPE_STRING\n");
		return false;
	}

	// Get a pointer to the typed property
	StackPropertyString *p_cast = (StackPropertyString*)property;

	// Determine which version to change
	char **target = NULL;
	switch (version)
	{
		case STACK_PROPERTY_VERSION_DEFINED:
			target = &p_cast->defined;
			break;
		case STACK_PROPERTY_VERSION_LIVE:
			target = &p_cast->live;
			break;
		case STACK_PROPERTY_VERSION_TARGET:
			target = &p_cast->target;
			break;
		default:
			return false;
	}

	if (strcmp(*target, value) != 0)
	{
		// Check the validator
		char *validated_value = NULL;
		if (p_cast->validator != NULL)
		{
			validated_value = p_cast->validator(p_cast, version, value, p_cast->validator_user_data);
		}

		// Tidy up existing value
		free(*target);

		// If the validator returned a different pointer
		if (p_cast->validator != NULL && validated_value != value)
		{
			*target = strdup(validated_value);

			// This feels... wrong
			free(validated_value);
		}
		// No change, just copy
		else
		{
			*target = strdup(value);
		}

		// Notify
		if (!property->change_callbacks_paused && property->changed_callback != NULL)
		{
			property->changed_callback(property, version, property->changed_callback_user_data);
		}
	}

	return true;
}

void stack_property_write_json(const StackProperty *property, Json::Value* json_root)
{
	if (property == NULL)
	{
		return;
	}

	if (stack_property_get_null(property, STACK_PROPERTY_VERSION_DEFINED))
	{
		(*json_root)[property->name] = Json::Value::nullSingleton();
		return;
	}

	switch (property->type)
	{
		case STACK_PROPERTY_TYPE_NONE:
			break;
		case STACK_PROPERTY_TYPE_BOOL:
			(*json_root)[property->name] = ((StackPropertyBool*)property)->defined;
			break;
		case STACK_PROPERTY_TYPE_INT8:
			(*json_root)[property->name] = ((StackPropertyInt8*)property)->defined;
			break;
		case STACK_PROPERTY_TYPE_UINT8:
			(*json_root)[property->name] = ((StackPropertyUInt8*)property)->defined;
			break;
		case STACK_PROPERTY_TYPE_INT16:
			(*json_root)[property->name] = ((StackPropertyInt16*)property)->defined;
			break;
		case STACK_PROPERTY_TYPE_UINT16:
			(*json_root)[property->name] = ((StackPropertyUInt16*)property)->defined;
			break;
		case STACK_PROPERTY_TYPE_INT32:
			(*json_root)[property->name] = ((StackPropertyInt32*)property)->defined;
			break;
		case STACK_PROPERTY_TYPE_UINT32:
			(*json_root)[property->name] = ((StackPropertyUInt32*)property)->defined;
			break;
		case STACK_PROPERTY_TYPE_INT64:
			(*json_root)[property->name] = (Json::Int64)((StackPropertyInt64*)property)->defined;
			break;
		case STACK_PROPERTY_TYPE_UINT64:
			(*json_root)[property->name] = (Json::UInt64)((StackPropertyUInt64*)property)->defined;
			break;
		case STACK_PROPERTY_TYPE_DOUBLE:
			if (std::isfinite(((StackPropertyDouble*)property)->defined))
			{
				(*json_root)[property->name] = ((StackPropertyDouble*)property)->defined;
			}
			else
			{
				(*json_root)[property->name] = "-Infinite";
			}
			break;
		case STACK_PROPERTY_TYPE_STRING:
			(*json_root)[property->name] = ((StackPropertyString*)property)->defined;
			break;
	}
}

#define CASE_PROPERTY_DEFLIVE(type_suffix, type_name, type_enum, property_type_name) \
	case type_enum: \
		property_type_name *p_##type_suffix; \
		type_name v_##type_suffix; \
		p_##type_suffix = (property_type_name*)property; \
		stack_property_get_##type_suffix(property, STACK_PROPERTY_VERSION_DEFINED, &v_##type_suffix); \
		stack_property_set_##type_suffix(property, STACK_PROPERTY_VERSION_LIVE, v_##type_suffix); \
		break;

void stack_property_copy_defined_to_live(StackProperty *property)
{
	if (property == NULL)
	{
		return;
	}

	switch (property->type)
	{
		case STACK_PROPERTY_TYPE_NONE:
			break;
		CASE_PROPERTY_DEFLIVE(bool, bool, STACK_PROPERTY_TYPE_BOOL, StackPropertyBool)
		CASE_PROPERTY_DEFLIVE(int8, int8_t, STACK_PROPERTY_TYPE_INT8, StackPropertyInt8)
		CASE_PROPERTY_DEFLIVE(uint8, uint8_t, STACK_PROPERTY_TYPE_UINT8, StackPropertyUInt8)
		CASE_PROPERTY_DEFLIVE(int16, int16_t, STACK_PROPERTY_TYPE_INT16, StackPropertyInt16)
		CASE_PROPERTY_DEFLIVE(uint16, uint16_t, STACK_PROPERTY_TYPE_UINT16, StackPropertyUInt16)
		CASE_PROPERTY_DEFLIVE(int32, int32_t, STACK_PROPERTY_TYPE_INT32, StackPropertyInt32)
		CASE_PROPERTY_DEFLIVE(uint32, uint32_t, STACK_PROPERTY_TYPE_UINT32, StackPropertyUInt32)
		CASE_PROPERTY_DEFLIVE(int64, int64_t, STACK_PROPERTY_TYPE_INT64, StackPropertyInt64)
		CASE_PROPERTY_DEFLIVE(uint64, uint64_t, STACK_PROPERTY_TYPE_UINT64, StackPropertyUInt64)
		CASE_PROPERTY_DEFLIVE(double, double, STACK_PROPERTY_TYPE_DOUBLE, StackPropertyDouble)
		CASE_PROPERTY_DEFLIVE(string, char *, STACK_PROPERTY_TYPE_STRING, StackPropertyString)
	}

	// Copy whether the value is null
	property->version_live_null = property->version_defined_null;
}

// Gets whether a property value is null
// @param property The property to get the value of
// @param version The version of the property to get the value of
// @returns Whether or not the given version of the property is null (or always
// false if the property is not nullable)
bool stack_property_get_null(const StackProperty *property, StackPropertyVersion version)
{
	if (property == NULL || !property->nullable)
	{
		return false;
	}

	switch (version)
	{
		case STACK_PROPERTY_VERSION_DEFINED:
			return property->version_defined_null;
		case STACK_PROPERTY_VERSION_LIVE:
			return property->version_live_null;
		case STACK_PROPERTY_VERSION_TARGET:
			return property->version_target_null;
		default:
			return false;
	}
}

// Sets whether a property value is null
// @param property The property to get the value of
// @param version The version of the property to get the value of
// @param is_null Whether to set the value to null or not. If setting a value to
// not null, you should ensure that the property then has a value
// @returns True if the nullable field was set, or false otherwise (including if
// the property is not nullable)
bool stack_property_set_null(StackProperty *property, StackPropertyVersion version, bool is_null)
{
	if (property == NULL)
	{
		return false;
	}

	if (!property->nullable)
	{
		stack_log("stack_property_set_null called on non-nullable property '%s'\n", property->name);
		return false;
	}

	switch (version)
	{
		case STACK_PROPERTY_VERSION_DEFINED:
			property->version_defined_null = is_null;
			break;
		case STACK_PROPERTY_VERSION_LIVE:
			property->version_live_null = is_null;
			break;
		case STACK_PROPERTY_VERSION_TARGET:
			property->version_target_null = is_null;
			break;
		default:
			return false;
	}

	return true;
}
