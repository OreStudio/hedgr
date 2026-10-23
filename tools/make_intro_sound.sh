#!/usr/bin/env bash
# Builds a launch intro sound like assets/sounds/intro_swell.wav from Kenney's
# Sci-Fi Sounds (CC0): spaceEngineLow_000 swells in and dies away, and
# lowFrequency_explosion_000 lands on it at 2.8 s as the impact.
#
# Usage: make_intro_sound.sh SCIFI_AUDIO_DIR OUT.wav
#
# SCIFI_AUDIO_DIR holds the pack's .ogg files. The graph, in order:
#   engine   mono 44.1 kHz, exponential fade in over 2.8 s, fade out over the
#            2.6 s that follow (to its end at 5.0 s)
#   impact   mono 44.1 kHz, delayed 2.8 s
#   mix      the two summed, padded with silence to 6 s
#   loudnorm I=-18 LUFS, TP=-2 dB, then -6 dB of volume for headroom
#   afade    1.2 s fade out from 4.8 s
# The committed intro_swell.wav is the render the operator chose by ear. This
# recipe matches it in shape and loudness, not byte for byte, because
# loudnorm's output depends on the ffmpeg build. The committed file's loudest
# 50 ms window is centred at 3.025 s, which is intro_peak_ns in
# hedgr.ui/render/intro.hpp. Change the sound and measure again with
# tools/measure_intro_peak.py before moving that constant.
set -euo pipefail
dir=${1:?usage: make_intro_sound.sh SCIFI_AUDIO_DIR OUT.wav}
out=${2:?usage: make_intro_sound.sh SCIFI_AUDIO_DIR OUT.wav}
mono='aformat=sample_rates=44100:channel_layouts=mono'
ffmpeg -v error -y \
    -i "$dir/spaceEngineLow_000.ogg" -i "$dir/lowFrequency_explosion_000.ogg" \
    -filter_complex "[0:a]$mono,afade=t=in:st=0:d=2.8:curve=exp,afade=t=out:st=2.4:d=2.6[engine];\
[1:a]$mono,adelay=2800[impact];\
[engine][impact]amix=inputs=2:normalize=0:duration=longest,apad=whole_dur=6,\
loudnorm=I=-18:TP=-2,volume=-6dB,afade=t=out:st=4.8:d=1.2,$mono" \
    -t 6 "$out"
