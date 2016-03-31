#ifndef _STACKAUDIODEVICE_H_INCLUDED
#define _STACKAUDIODEVICE_H_INCLUDED

// Includes:
#include "StackError.h"
#include <cstdint>
#include <cstdlib>

// Structure
typedef struct StackAudioDevice
{
	// Class name
	const char *_class_name;

	// The number of channels present
	uint32_t channels;
	
	// The sample rate of the device
	uint32_t sample_rate;
} StackAudioDevice;

// Structure: Descriptor of an audio device
typedef struct StackAudioDeviceDesc
{
	uint32_t channels;
	char *name;
	char *desc;
} StackAudioDeviceDesc;

// Typedefs:
typedef size_t(*stack_audio_device_list_outputs_t)(StackAudioDeviceDesc **);
typedef void(*stack_audio_device_free_outputs_t)(StackAudioDeviceDesc **, size_t);
typedef StackAudioDevice*(*stack_audio_device_create_t)(const char *, uint32_t, uint32_t);
typedef void(*stack_audio_device_destroy_t)(StackAudioDevice *);
typedef void(*stack_audio_device_write_t)(StackAudioDevice *, const char *, size_t);

// Function pointers for each type
typedef struct StackAudioDeviceClass
{
	const char *class_name;
	const char *super_class_name;
	stack_audio_device_list_outputs_t get_outputs_func;		// Static function
	stack_audio_device_free_outputs_t free_outputs_func;	// Static function
	stack_audio_device_create_t create_func;	// Static function
	stack_audio_device_destroy_t destroy_func;
	stack_audio_device_write_t write_func;
} StackAudioDeviceClass;

// Functions: Cue Type Registration
int stack_register_audio_device_class(StackAudioDeviceClass *adev_class);

// Functions: Base audio device functions that call the superclass
void stack_audio_device_initsystem();
void stack_audio_device_write(StackAudioDevice *adev, const char *data, size_t bytes);

// Functions: Base functions. These should not be called except from subclasses
// of StackAudioDevice
void stack_audio_device_destroy_base(StackAudioDevice *adev);

// Functions: Arbitrary audio device creation/deletion
StackAudioDevice *stack_audio_device_new(const char *type, const char *name, uint32_t channels, uint32_t sample_rate);
void stack_audio_device_destroy(StackAudioDevice *adev);
const StackAudioDeviceClass *stack_audio_device_get_class(const char *name);

// Defines:
#define STACK_AUDIO_DEVICE(_d) ((StackAudioDevice*)(_d))

#endif

