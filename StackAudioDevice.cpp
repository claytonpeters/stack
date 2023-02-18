// Includes:
#include "StackAudioDevice.h"
#include "StackLog.h"
#include <cstring>
#include <map>
#include <string>
using namespace std;

// Map of classes
static map<string, const StackAudioDeviceClass*> adev_class_map;
typedef map<string, const StackAudioDeviceClass*>::iterator sadc_iter_t;

StackAudioDevice *stack_audio_device_create_base(const char *name, uint32_t channels, uint32_t sample_rate, stack_audio_device_audio_request_t request_audio, void *user_data)
{
	stack_log("stack_audio_device_create_base(): Objects of type StackAudioDevice cannot be created\n");
	return NULL;
}

void stack_audio_device_destroy_base(StackAudioDevice *adev)
{
	// Delete ourselves
	delete adev;
}

const char *stack_audio_device_get_friendly_name_base()
{
	return "Stack Null-Audio Provider";
}

// Registers base classes
void stack_audio_device_initsystem()
{
	StackAudioDeviceClass* audio_device_class = new StackAudioDeviceClass{ "StackAudioDevice", NULL, NULL, NULL, stack_audio_device_create_base, stack_audio_device_destroy_base, stack_audio_device_get_friendly_name_base };
	stack_register_audio_device_class(audio_device_class);
}

// Functions: Cue Type Registration
int stack_register_audio_device_class(StackAudioDeviceClass *adev_class)
{
	// Parameter error checking
	if (adev_class == NULL)
	{
		return STACKERR_PARAM_NULL;
	}

	// Debug
	stack_log("Registering audio device type '%s'\n", adev_class->class_name);

	// Validate name pointer
	if (adev_class->class_name == NULL)
	{
		stack_log("stack_register_audio_device_class(): Class name cannot be NULL\n");
		return STACKERR_CLASS_BADNAME;
	}

	// Ensure we don't already have a class of this type
	if (adev_class_map.find(string(adev_class->class_name)) != adev_class_map.end())
	{
		stack_log("stack_register_audio_device_class(): Class name already registered\n");
		return STACKERR_CLASS_DUPLICATE;
	}

	// Only the 'StackAudioDevice' class is allowed to not have a superclass
	if (adev_class->super_class_name == NULL && strcmp(adev_class->class_name, "StackAudioDevice") != 0)
	{
		stack_log("stack_register_audio_device_class(): Cue classes must have a superclass\n");
		return STACKERR_CLASS_NOSUPER;
	}

	// Validate name length
	if (strlen(adev_class->class_name) <= 0)
	{
		stack_log("stack_register_audio_device_class(): Class name cannot be empty\n");
		return STACKERR_CLASS_BADNAME;
	}

	// Validate create function pointer
	if (adev_class->create_func == NULL)
	{
		stack_log("stack_register_audio_device_class(): Class create_func cannot be NULL. An implementation must be provided\n");
	}

	// Validate destroy function pointer
	if (adev_class->destroy_func == NULL)
	{
		stack_log("stack_register_audio_device_class(): Class destroy_func cannot be NULL. An implementation must be provided\n");
		return STACKERR_CLASS_BADDESTROY;
	}

	// Store the class
	adev_class_map[string(adev_class->class_name)] = adev_class;

	return 0;
}

// Functions: Arbitrary audio device creation/deletion
StackAudioDevice *stack_audio_device_new(const char *type, const char *name, uint32_t channels, uint32_t sample_rate, stack_audio_device_audio_request_t request_audio, void *request_audio_user_data)
{
	// Debug
	stack_log("stack_audio_device_new(): Calling create for type '%s'\n", type);

	// Locate the class
	auto iter = adev_class_map.find(string(type));
	if (iter == adev_class_map.end())
	{
		stack_log("stack_audio_device_new(): Unknown class\n");
		return NULL;
	}

	// No need to iterate up through superclasses - we can't be NULL
	return iter->second->create_func(name, channels, sample_rate, request_audio, request_audio_user_data);
}

void stack_audio_device_destroy(StackAudioDevice *adev)
{
	// Debug
	stack_log("stack_audio_device_destroy(): Calling destroy for type '%s'\n", adev->_class_name);

	// Locate the class
	auto iter = adev_class_map.find(string(adev->_class_name));
	if (iter == adev_class_map.end())
	{
		stack_log("stack_audio_device_destroy(): Unknown class\n");
		return;
	}

	// No need to iterate up through superclasses - we can't be NULL
	iter->second->destroy_func(adev);
}

const StackAudioDeviceClass *stack_audio_device_get_class(const char *name)
{
	auto iter = adev_class_map.find(string(name));
	if (iter == adev_class_map.end())
	{
		return NULL;
	}

	return iter->second;
}

void *stack_audio_device_class_iter_front()
{
	sadc_iter_t* result = new sadc_iter_t;
	*result = adev_class_map.begin();
	return result;
}

void *stack_audio_device_class_iter_next(void *iter)
{
	return (void*)&(++(*(sadc_iter_t*)(iter)));
}

const StackAudioDeviceClass *stack_audio_device_class_iter_get(void *iter)
{
	return (*(sadc_iter_t*)(iter))->second;
}

void stack_audio_device_class_iter_free(void *iter)
{
	delete (sadc_iter_t*)iter;
}

bool stack_audio_device_class_iter_at_end(void *iter)
{
	return (*(sadc_iter_t*)(iter)) == adev_class_map.end();
}

