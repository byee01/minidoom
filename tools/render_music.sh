#!/bin/bash
# Render Freedoom's D_E1M1 (MIDI) to a mono AAC loop with FluidR3Mono (MIT).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
python3 -c "
import sys; sys.path.insert(0,'tools'); from wadlib import Wad
d=Wad.load('data/freedoom-0.13.0/freedoom1.wad').get('D_E1M1'); assert d[:4]==b'MThd'
open('data/music/e1m1.mid','wb').write(d)"
fluidsynth -ni -q -g 0.8 -r 44100 -F build/e1m1.wav data/soundfont/FluidR3Mono_GM.sf3 data/music/e1m1.mid
# trim trailing silence (reverb tail beyond the loop) and encode mono 64 kbps
ffmpeg -loglevel error -y -i build/e1m1.wav -af "silenceremove=stop_periods=1:stop_duration=1:stop_threshold=-60dB,aformat=channel_layouts=mono" -c:a libmp3lame -b:a 64k data/music/music_e1m1.mp3
ffprobe -v error -show_entries format=duration,size -of default=nw=1 data/music/music_e1m1.mp3
