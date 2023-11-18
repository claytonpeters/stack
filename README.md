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

## Building

Stack uses CMake as its build system. You will need at least version 3.12 of
CMake to build. You will need the following library dependencies (along with
their development package) to compile and run Stack:

* pkg-config
* GTK3
* PulseAudio
* ALSA
* jsoncpp
* libmad (optional, required for MP3 playback)
* libsoxr
* protobuf-c (optional, required for remote control)

For Ubuntu, this list of dependencies can be installed with:

```shell
sudo apt install pkg-config libgtk-3-0 libgtk-3-dev libglib2.0-dev libpulse0
  libpulse-dev libasound2 libasound2-dev libjsoncpp25 \ libjsoncpp-dev libmad0
  libmad0-dev libsoxr0 libsoxr-dev libprotobuf-c1 libprotobuf-c-dev
```

Compilation (once you have the correct dependencies installed), should be as
simple as:

```shell
cmake .
make
```

This will compile all the plugins and binaries. You can run Stack by running

```shell
./runstack
```
