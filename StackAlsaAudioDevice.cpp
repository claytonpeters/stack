// Includes:
#include "StackAlsaAudioDevice.h"
#include <cstring>

// Globals:
semaphore state_semaphore;

// Initialises AlsaAudio
bool stack_init_alsa_audio()
{
	return true;
}

size_t stack_alsa_audio_device_list_outputs(StackAudioDeviceDesc **outputs)
{
	static const int common_sample_rates[] = {8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000};
	static const size_t num_common_sample_rates = 11;

	// Initialise pulse audio
	if (!stack_init_alsa_audio())
	{
		// Failed to initialise, return NULL
		*outputs = NULL;
		return 0;
	}

	// Get some hints
	void **hints = NULL;
	int result = snd_device_name_hint(-1, "pcm", &hints);
	if (result != 0)
	{
		*outputs = NULL;
		return 0;
	}

	size_t alsa_device_count = 0, stack_device_count = 0;

	// Count how many devices we find
	for (size_t i = 0; hints[i] != NULL; i++)
	{
		alsa_device_count = i + 1;
	}

	// If there are no devices, return immediately
	if (alsa_device_count == 0)
	{
		snd_device_name_free_hint(hints);
		*outputs = NULL;
		return 0;
	}

	StackAudioDeviceDesc* devices = new StackAudioDeviceDesc[alsa_device_count];

	// Iterate over the devices and build information
	for (size_t alsa_device_idx = 0, stack_device_idx = 0; alsa_device_idx < alsa_device_count; alsa_device_idx++)
	{
		char *name = snd_device_name_get_hint(hints[alsa_device_idx], "NAME");
		char *desc = snd_device_name_get_hint(hints[alsa_device_idx], "DESC");

		// Open the device
		snd_pcm_t* pcm = NULL;
		if (snd_pcm_open(&pcm, name, SND_PCM_STREAM_PLAYBACK, 0) != 0)
		{
			free(name);
			free(desc);
			continue;
		}

		// Get parameters
		snd_pcm_hw_params_t* hw_params = NULL;
		snd_pcm_hw_params_malloc(&hw_params);
		snd_pcm_hw_params_any(pcm, hw_params);

		// Get minimum and maximum number of channels
		unsigned int min, max;
		snd_pcm_hw_params_get_channels_min(hw_params, &min);
		snd_pcm_hw_params_get_channels_max(hw_params, &max);

		// Limit the maximum to 32 channels, as some devices return "-1" channels
		if (max > 32)
		{
			max = 32;
		}

		// Store channel counts
		devices[stack_device_idx].min_channels = min;
		devices[stack_device_idx].max_channels = max;

		// Get minimum and maximum sample rates
		int dir;
		snd_pcm_hw_params_get_rate_min(hw_params, &min, &dir);
		snd_pcm_hw_params_get_rate_max(hw_params, &max, &dir);

		// Iterate over our common sample rates and see which ones are valid
		size_t sample_rate_count = 0;
		for (size_t common_rate_idx = 0; common_rate_idx < num_common_sample_rates; common_rate_idx++)
		{
			if (common_sample_rates[common_rate_idx] >= min && common_sample_rates[common_rate_idx] <= max)
			{
				if (snd_pcm_hw_params_test_rate(pcm, hw_params, common_sample_rates[common_rate_idx], 0) == 0)
				{
					sample_rate_count++;
				}
			}
		}

		// Store the sample rates
		devices[stack_device_idx].num_rates = sample_rate_count;
		if (sample_rate_count > 0)
		{
			devices[stack_device_idx].rates = new uint32_t[sample_rate_count];
			for (size_t rate_idx = 0, common_rate_idx = 0; common_rate_idx < num_common_sample_rates; common_rate_idx++)
			{
				if (common_sample_rates[common_rate_idx] >= min && common_sample_rates[common_rate_idx] <= max)
				{
					devices[stack_device_idx].rates[rate_idx] = common_sample_rates[common_rate_idx];
					rate_idx++;
				}
			}

		}
		else
		{
			devices[stack_device_idx].rates = NULL;
		}

		// Store the name and description
		devices[stack_device_idx].name = strdup(name);
		devices[stack_device_idx].desc = strdup(desc);

		// Tidy up
		free(name);
		free(desc);
		snd_pcm_hw_params_free(hw_params);
		snd_pcm_close(pcm);
		stack_device_count++;
		stack_device_idx++;
	}

	// Tidy up
	snd_device_name_free_hint(hints);

	// We can technically return an array that contains more elements than
	// we say (if a device fails to open, for example), but this is not a
	// problem, as we only allocate and indeed free the internals of the
	// number we say, but we tidy up the whole array
	*outputs = devices;
	return stack_device_count;
}

void stack_alsa_audio_device_free_outputs(StackAudioDeviceDesc **outputs, size_t count)
{
	for (size_t i = 0; i < count; i++)
	{
		free((*outputs)[i].name);
		free((*outputs)[i].desc);
		delete [] (*outputs)[i].rates;
	}

	delete [] *outputs;
}

void stack_alsa_audio_device_destroy(StackAudioDevice *device)
{
	// Debug
	fprintf(stderr, "stack_alsa_audio_device_destroy() called\n");

	// Tidy up
	if (STACK_ALSA_AUDIO_DEVICE(device)->stream != NULL)
	{
	}

	// Call superclass destroy
	stack_audio_device_destroy_base(device);
}

StackAudioDevice *stack_alsa_audio_device_create(const char *name, uint32_t channels, uint32_t sample_rate)
{
	// Debug
	fprintf(stderr, "stack_alsa_audio_device_create(\"%s\", %u, %u) called\n", name, channels, sample_rate);

	// Allocate the new device
	StackAlsaAudioDevice *device = new StackAlsaAudioDevice();
	device->stream = NULL;
	if (snd_pcm_open(&device->stream, name, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) != 0)
	{
		fprintf(stderr, "stack_alsa_audio_device_create: snd_pcm_open() failed\n");
		return NULL;
	}

	// Get some initial hardware parameters
	snd_pcm_hw_params_t *hw_params = NULL;
	snd_pcm_hw_params_malloc(&hw_params);
	snd_pcm_hw_params_any(device->stream, hw_params);

	// Choose the correct sample rate
	if (snd_pcm_hw_params_set_rate(device->stream, hw_params, sample_rate, 0) < 0)
	{
		fprintf(stderr, "stack_alsa_audio_device_create: snd_pcm_hw_params_set_rate() failed\n");
	}

	// Set the correct number of channels
	if (snd_pcm_hw_params_set_channels(device->stream, hw_params, channels) < 0)
	{
		fprintf(stderr, "stack_alsa_audio_device_create: snd_pcm_hw_params_set_channels() failed\n");
	}

	// Set the format to 32-bit floating point, which is what Stack
	// uses internally
	if (snd_pcm_hw_params_set_format(device->stream, hw_params, SND_PCM_FORMAT_FLOAT_LE) < 0)
	{
		fprintf(stderr, "stack_alsa_audio_device_create: snd_pcm_hw_params_set_format() failed\n");
	}

	// Apply the hardware parameters to the device
	snd_pcm_hw_params(device->stream, hw_params);

	// Set up superclass
	STACK_AUDIO_DEVICE(device)->_class_name = "StackAlsaAudioDevice";
	STACK_AUDIO_DEVICE(device)->channels = channels;
	STACK_AUDIO_DEVICE(device)->sample_rate = sample_rate;

	// Start the PCM stream
	if (snd_pcm_start(device->stream) < 0)
	{
		fprintf(stderr, "stack_alsa_audio_device_create: snd_pcm_start() failed\n");
	}

	// Return the newly created device
	return STACK_AUDIO_DEVICE(device);
}

void stack_alsa_audio_device_write(StackAudioDevice *device, const char *data, size_t bytes)
{
	snd_pcm_writei(STACK_ALSA_AUDIO_DEVICE(device)->stream, data, bytes / sizeof(float) / STACK_AUDIO_DEVICE(device)->channels);
}

const char *stack_alsa_audio_device_get_friendly_name()
{
	return "Stack ALSA Provider";
}

void stack_alsa_audio_device_register()
{
	// Register the Alsa audio class
	StackAudioDeviceClass* pulse_audio_device_class = new StackAudioDeviceClass{ "StackAlsaAudioDevice", "StackAlsaAudioDevice", stack_alsa_audio_device_list_outputs, stack_alsa_audio_device_free_outputs, stack_alsa_audio_device_create, stack_alsa_audio_device_destroy, stack_alsa_audio_device_write, stack_alsa_audio_device_get_friendly_name };
	stack_register_audio_device_class(pulse_audio_device_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_alsa_audio_device_register();
	return true;
}

