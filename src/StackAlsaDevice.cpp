// These are defined in StackAlsaAudioDevice.cpp and StackAlsaMidiDevice.cpp
void stack_alsa_midi_device_register();
void stack_alsa_audio_device_register();

// The entry point for the plugin that Stack calls
extern "C" bool stack_init_plugin()
{
	stack_alsa_audio_device_register();
	stack_alsa_midi_device_register();
	return true;
}
