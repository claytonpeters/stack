# Stack

Stack is an extensible sound cue playback system for theatre written in C++
using Gtk+, aiming to bring efficient, stable show control to Linux.

![Stack User Interface](https://thork.io/stack/ui-github.png)

## Currently Implemented

Cues:
* Audio Cues
  * Playback from Wave and MP3
  * Start/end trim points
  * Loops
  * Resampling when device/file sample rates differ
* Fade Cues
  * Choice of fade profiles
* Action Cues: Start/Stop/Pause other cues

Outputs:
* PulseAudio
* ALSA

PulseAudio is set as the default output device with a common 2-channel, 44.1kHz
sample rate initially. Output device, channels and sample rates can be chosen
in the settings. If you want to use ALSA on a system where PulseAudio currently
uses the same audio device, you may start Stack by running,
`pasuspender ./runstack` and then changing the device settings to ALSA.

Features:
* Basic cue stack
  * Pre-wait/post-wait times, auto-continue and auto-follow
  * Cue notes
  * Automatic cue name generation
  * Custom per-cue colours
* List of active cues with per-channel levels and times
* Remote control via a (currently undocumented) protobuf-based protocol
