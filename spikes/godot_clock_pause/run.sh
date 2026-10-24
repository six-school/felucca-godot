#!/bin/sh
# Godot 4.7.2, headless, dummy audio driver (mixes on its own thread under the driver lock, like the real drivers).
#   T1: an AudioEffectInstance with _process_silence() == true runs once per mix step on a muted, empty bus, paused too
#   T2: 8 players separated by 4000 plain nodes each; 300 tree pause/unpause cycles; do their mixed positions agree?
#   spikes/godot_clock_pause/run.sh /path/to/Godot_v4.7.2-stable_linux.x86_64
set -e
G=${1:?path to the Godot 4.7.2 binary}
D=$(cd "$(dirname "$0")" && pwd)
"$G" --headless --audio-driver Dummy --path "$D" --import > /dev/null 2>&1 || true
"$G" --headless --audio-driver Dummy --path "$D" 2>&1 | grep '^T[12]'
