#ifndef _STACKAUDIODEVICE_H_INCLUDED
#define _STACKAUDIODEVICE_H_INCLUDED

// Includes:
#include "StackError.h"
#include <cstdint>
#include <cstdlib>

// Early typedef required by StackAudioDevice
typedef size_t(*stack_audio_device_audio_request_t)(size_t, float *, void *);

// Structure
struct StackAudioDevice
{
	// Class name
	const char *_class_name;

	// The number of channels present
	uint32_t channels;

	// The sample rate of the device
	uint32_t sample_rate;

	// Function pointer to routine that gives the device audio data
	stack_audio_device_audio_request_t request_audio;

	// Arbitrary user data passed to request_audio function
	void *request_audio_user_data;
};

// Structure: Descriptor of an audio device
struct StackAudioDeviceDesc
{
	uint16_t min_channels;
	uint16_t max_channels;
	char *name;
	char *desc;
	uint8_t num_rates;
	uint32_t *rates;
};

// Typedefs:
typedef size_t(*stack_audio_device_list_outputs_t)(StackAudioDeviceDesc **);
typedef void(*stack_audio_device_free_outputs_t)(StackAudioDeviceDesc **, size_t);
typedef StackAudioDevice*(*stack_audio_device_create_t)(const char *, uint32_t, uint32_t, stack_audio_device_audio_request_t, void *);
typedef void(*stack_audio_device_destroy_t)(StackAudioDevice *);
typedef const char *(*stack_audio_device_get_friendly_name_t)();

// Function pointers for each type
struct StackAudioDeviceClass
{
	const char *class_name;
	const char *super_class_name;
	stack_audio_device_list_outputs_t get_outputs_func;		// Static function
	stack_audio_device_free_outputs_t free_outputs_func;	// Static function
	stack_audio_device_create_t create_func;	// Static function
	stack_audio_device_destroy_t destroy_func;
	stack_audio_device_get_friendly_name_t get_friendly_name_func; // Static function
};

// Functions: Cue Type Registration
int stack_register_audio_device_class(StackAudioDeviceClass *adev_class);

// Functions: Base audio device functions that call the superclass
void stack_audio_device_initsystem();

// Functions: Base functions. These should not be called except from subclasses
// of StackAudioDevice
void stack_audio_device_destroy_base(StackAudioDevice *adev);

// Functions: Arbitrary audio device creation/deletion
StackAudioDevice *stack_audio_device_new(const char *type, const char *name, uint32_t channels, uint32_t sample_rate, stack_audio_device_audio_request_t request_audio, void *user_data);
void stack_audio_device_destroy(StackAudioDevice *adev);
const StackAudioDeviceClass *stack_audio_device_get_class(const char *name);

// Functions: Iterate over available providers
void *stack_audio_device_class_iter_front();
void *stack_audio_device_class_iter_next(void *iter);
const StackAudioDeviceClass *stack_audio_device_class_iter_get(void *iter);
void stack_audio_device_class_iter_free(void *iter);
bool stack_audio_device_class_iter_at_end(void *iter);

// Defines:
#define STACK_AUDIO_DEVICE(_d) ((StackAudioDevice*)(_d))

#endif

