# stack
[Work in Progress] An extensible sound cue playback system written in Gtk+

## Currently Implemented

Cues:
* Audio Cues: playback from Wave and MP3
* Fade Cues
* Action Cues: Start/Stop/Pause other cues

Outputs:
* PulseAudio
* ALSA

PulseAudio is currently hardcoded as the output device with it fixed as a two-
channel output at 44.1 kHz. This is customisable in code, but not in the UI at
present.

Features:
* Basic cue stack, with pre-wait/post-wait, auto-continue and auto-follow
* List of active cues with per-channel levels and times
