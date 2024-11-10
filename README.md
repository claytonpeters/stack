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
* Execute Cues: Run arbitrary programs/commands/scripts
* Group Cues: various modes when playing:
  * Trigger all child cues
  * Trigger random cchild cue
  * Trigger children in order as a playlist
  * Trigger children in random order as a shuffled playlist

Cue Triggers:
* Spacebar to play next cue in playlist
* Custom key triggers to play/pause/stop cues when a key is pressed/released
* Timed cue triggers to fire at a given date/time, optionally repeatng

Outputs:
* PulseAudio (also works with PipeWire via PipeWire PulseAudio plugin)
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
  * Cue script references
* List of active cues with per-channel levels and times
* Additional cue triggers
* Remote control via a (currently undocumented) protobuf-based protocol

## Building

Stack uses CMake as its build system. You will need at least version 3.12 of
CMake to build. You will need the following library dependencies (along with
their development packages) to compile and run Stack:

* cmake
* gcc/g++
* pkg-config
* gtk3
* glib2
* PulseAudio
* ALSA (optional, required for ALSA audio devices)
* jsoncpp
* libmad (optional, required for MP3 playback)
* libsoxr (optional, required for resampling)
* protobuf-c (optional, required for remote control)

For **Ubuntu 23.04 and newer**, this list of dependencies can be installed with:

```shell
sudo apt install cmake g++ pkg-config libgtk-3-0 libgtk-3-dev libglib2.0-dev \
  libpulse0 libpulse-dev libasound2t64 libasound2-dev libjsoncpp25 libjsoncpp-dev \
  libmad0 libmad0-dev libsoxr0 libsoxr-dev libprotobuf-c1 libprotobuf-c-dev
```

For **Ubuntu 22.04 and newer**, this list of dependencies can be installed with:

```shell
sudo apt install cmake g++ pkg-config libgtk-3-0 libgtk-3-dev libglib2.0-dev \
  libpulse0 libpulse-dev libasound2 libasound2-dev libjsoncpp25 libjsoncpp-dev \
  libmad0 libmad0-dev libsoxr0 libsoxr-dev libprotobuf-c1 libprotobuf-c-dev
```

For **Ubuntu 20.04**, the slight variation on the above is:

```shell
sudo apt install cmake g++ pkg-config libgtk-3-0 libgtk-3-dev libglib2.0-dev \
  libpulse0 libpulse-dev libasound2 libasound2-dev libjsoncpp1 libjsoncpp-dev \
  libmad0 libmad0-dev libsoxr0 libsoxr-dev libprotobuf-c1 libprotobuf-c-dev
```

For **Rocky Linux**, you will first need the EPEL repository configured,

```shell
# Rocky Linux 8
yum -y install https://dl.fedoraproject.org/pub/epel/epel-release-latest-8.noarch.rpm

# Rocky Linux 9
yum -y install https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm
````

The list of dependencies can be installed with:

```shell
yum -y install --enablerepo=devel cmake gcc-c++ pkg-config gtk3 gtk3-devel \
  glib2 glib2-devel pulseaudio-libs pulseaudio-libs-devel alsa-lib \
  alsa-lib-devel libmad libmad-devel soxr soxr-devel jsoncpp jsoncpp-devel \
  protobuf-c protobuf-c-devel
```

**Note that whilst Stack has been shown to compile on Ubuntu 20.04, Ubuntu
23.10, Ubuntu 24.04, Ubuntu 24.10 and Rocky Linux 8, and Rocky Linux 9, it has
not been actively tested on these distros!**

Compilation (once you have the correct dependencies installed), should be as
simple as:

```shell
cmake -DCMAKE_BUILD_TYPE=Release .
make
```

This will compile all the plugins and binaries. You can run Stack by running

```shell
./runstack
```
