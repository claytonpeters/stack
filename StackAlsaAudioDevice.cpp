// Includes:
#include "StackAlsaAudioDevice.h"
#include "StackLog.h"
#include <cstring>

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
	StackAlsaAudioDevice *alsa_device = STACK_ALSA_AUDIO_DEVICE(device);

	// Debug
	stack_log("stack_alsa_audio_device_destroy() called\n");

	if (alsa_device->thread_running)
	{
		alsa_device->thread_running = false;
		alsa_device->output_thread.join();
	}

	// Tidy up
	if (STACK_ALSA_AUDIO_DEVICE(device)->stream != NULL)
	{
		snd_pcm_close(STACK_ALSA_AUDIO_DEVICE(device)->stream);
	}

	if (device->device_name != NULL)
	{
		free(device->device_name);
	}

	// Call superclass destroy
	stack_audio_device_destroy_base(device);
}

static void stack_alsa_audio_device_output_thread(void *user_data)
{
	StackAlsaAudioDevice *device = STACK_ALSA_AUDIO_DEVICE(user_data);
	size_t channels = STACK_AUDIO_DEVICE(device)->channels;

	device->thread_running = true;

	while (device->thread_running)
	{
		snd_pcm_sframes_t writable = snd_pcm_avail_update(device->stream);
		if (writable < 0)
		{
			stack_log("stack_alsa_audio_device_callback: snd_pcm_avail_update failed with %s\n", snd_strerror(writable));
		}

		while (writable >= 256)
		{
			// Get a buffer of that size and read (up to) that many bytes
			size_t total_sample_count = writable * channels;
			float *buffer = new float[total_sample_count];
			size_t read = STACK_AUDIO_DEVICE(device)->request_audio(writable, buffer, STACK_AUDIO_DEVICE(device)->request_audio_user_data);

			if (read < writable)
			{
				total_sample_count = read * channels;
				stack_log("Buffer underflow: %lu < %lu!\n", read, writable);
			}

			if (device->format == SND_PCM_FORMAT_FLOAT_LE)
			{
				// Write out
				snd_pcm_writei(device->stream, buffer, read);
			}
			else if (device->format == SND_PCM_FORMAT_S16_LE)
			{
				// Convert to int16s
				int16_t *i16_buffer = new int16_t[total_sample_count];
				for (size_t i = 0; i < total_sample_count; i++)
				{
					i16_buffer[i] = (int16_t)(buffer[i] * 32767.0f);
				}

				// Write out
				snd_pcm_writei(device->stream, i16_buffer, read);

				// Tidy up
				delete [] i16_buffer;
			}

			// Tidy up
			delete [] buffer;

			// Determine if more data is required
			writable = snd_pcm_avail_update(device->stream);
		}

		// TODO: We can probably do something better than this
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

StackAudioDevice *stack_alsa_audio_device_create(const char *name, uint32_t channels, uint32_t sample_rate, stack_audio_device_audio_request_t request_audio, void *user_data)
{
	// Debug
	stack_log("stack_alsa_audio_device_create(\"%s\", %u, %u) called\n", name, channels, sample_rate);

	// Allocate the new device
	StackAlsaAudioDevice *device = new StackAlsaAudioDevice();
	device->stream = NULL;
	device->format = SND_PCM_FORMAT_FLOAT_LE;
	//if (snd_pcm_open(&device->stream, name, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) != 0)
	if (snd_pcm_open(&device->stream, name, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) != 0)
	{
		stack_log("stack_alsa_audio_device_create: snd_pcm_open() failed\n");
		return NULL;
	}

	// Get some initial hardware parameters
	snd_pcm_hw_params_t *hw_params = NULL;
	snd_pcm_hw_params_malloc(&hw_params);
	snd_pcm_hw_params_any(device->stream, hw_params);

	// Choose the correct sample rate
	if (snd_pcm_hw_params_set_rate(device->stream, hw_params, sample_rate, 0) < 0)
	{
		stack_log("stack_alsa_audio_device_create: snd_pcm_hw_params_set_rate() failed\n");
	}

	// Set the correct number of channels
	if (snd_pcm_hw_params_set_channels(device->stream, hw_params, channels) < 0)
	{
		stack_log("stack_alsa_audio_device_create: snd_pcm_hw_params_set_channels() failed\n");
	}

	// Set the access type
	if (snd_pcm_hw_params_set_access(device->stream, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
	{
		stack_log("stack_alsa_audio_device_create: snd_pcm_hw_params_set_access() failed\n");
	}

	// Set the buffer size
	if (snd_pcm_hw_params_set_buffer_size(device->stream, hw_params, 2048) < 0)
	{
		stack_log("stack_alsa_audio_device_create: snd_pcm_hw_params_set_buffer_size() failed\n");
	}

	// Set the format to 32-bit floating point, which is what Stack
	// uses internally
	if (snd_pcm_hw_params_set_format(device->stream, hw_params, SND_PCM_FORMAT_FLOAT_LE) < 0)
	{
		stack_log("stack_alsa_audio_device_create: snd_pcm_hw_params_set_format(float) failed - falling back to s16\n");
		device->format = SND_PCM_FORMAT_S16_LE;
		if (snd_pcm_hw_params_set_format(device->stream, hw_params, SND_PCM_FORMAT_S16_LE) < 0)
		{
			stack_log("stack_alsa_audio_device_create: snd_pcm_hw_params_set_format(s16) failed\n");
		}
	}

	// Apply the hardware parameters to the device
	int result = snd_pcm_hw_params(device->stream, hw_params);
	if (result < 0)
	{
		stack_log("stack_alsa_audio_device_create: snd_pcm_hw_params() failed: %d: %s\n", result, snd_strerror(result));
	}

	// Tidy up
	snd_pcm_hw_params_free(hw_params);

	// Get some initial software parameters
	snd_pcm_sw_params_t *sw_params = NULL;
	snd_pcm_sw_params_malloc(&sw_params);
	result = snd_pcm_sw_params_current(device->stream, sw_params);
	if (result < 0)
	{
		stack_log("stack_alsa_audio_device_create: snd_pcm_sw_params_current() failed: %d: %s\n", result, snd_strerror(result));
	}
	result = snd_pcm_sw_params_set_start_threshold(device->stream, sw_params, 512);
	if (result < 0)
	{
		stack_log("stack_alsa_audio_device_create: snd_pcm_sw_params_set_start_threshold() failed: %d: %s\n", result, snd_strerror(result));
	}

	// Apply the software parameters to the devices
	result = snd_pcm_sw_params(device->stream, sw_params);
	if (result < 0)
	{
		stack_log("stack_alsa_audio_device_create: snd_pcm_sw_params() failed: %d: %s\n", result, snd_strerror(result));
	}

	// Tidy up
	snd_pcm_sw_params_free(sw_params);

	// Set up superclass
	STACK_AUDIO_DEVICE(device)->_class_name = "StackAlsaAudioDevice";
	STACK_AUDIO_DEVICE(device)->channels = channels;
	STACK_AUDIO_DEVICE(device)->sample_rate = sample_rate;
	STACK_AUDIO_DEVICE(device)->request_audio = request_audio;
	STACK_AUDIO_DEVICE(device)->request_audio_user_data = user_data;
	STACK_AUDIO_DEVICE(device)->device_name = strdup(name);

	// Start the output thread
	device->output_thread = std::thread(stack_alsa_audio_device_output_thread, device);

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
	StackAudioDeviceClass* pulse_audio_device_class = new StackAudioDeviceClass{ "StackAlsaAudioDevice", "StackAlsaAudioDevice", stack_alsa_audio_device_list_outputs, stack_alsa_audio_device_free_outputs, stack_alsa_audio_device_create, stack_alsa_audio_device_destroy, stack_alsa_audio_device_get_friendly_name };
	stack_register_audio_device_class(pulse_audio_device_class);
}

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_alsa_audio_device_register();
	return true;
}

