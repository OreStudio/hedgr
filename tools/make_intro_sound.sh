#!/usr/bin/env bash
# Builds a launch intro sound like assets/sounds/intro_swell.wav from Kenney's
# Sci-Fi Sounds (CC0): spaceEngineLow_000 swells in and dies away, and
# lowFrequency_explosion_000 lands on it at 2.8 s as the impact. The intro
# pulses twice, so the swell plays twice, one pulse period apart.
#
# Usage: make_intro_sound.sh SCIFI_AUDIO_DIR OUT.wav
#
# SCIFI_AUDIO_DIR holds the pack's files, .ogg or .wav. The graph, in order:
#   engine   mono 44.1 kHz, exponential fade in over 2.8 s, fade out over the
#            2.6 s that follow (to its end at 5.0 s)
#   impact   mono 44.1 kHz, delayed 2.8 s
#   swell    the two summed, padded with silence to 6 s, 1.2 s fade out from 4.8 s
#   copies   the swell twice at the same level; the second is delayed by the
#            pulse period (3.2 s), over the first's tail, so the loudest
#            moments are one period apart
#   mix      the two summed, padded to the end of the veil fade, 1.2 s fade
#            out ending there
#   loudnorm I=-18 LUFS, TP=-2 dB, then -6 dB of volume for headroom
# The committed intro_swell.wav is built by this recipe. loudnorm's output
# depends on the ffmpeg build, so a rebuild matches in shape and loudness, not
# byte for byte. The loudest 50 ms window of each swell is what intro_peak_ns
# and intro_pulse_period_ns in hedgr.ui/render/intro.hpp are set to. Change
# the sound and measure again with tools/measure_intro_peak.py before moving
# those constants.
set -euo pipefail
dir=${1:?usage: make_intro_sound.sh SCIFI_AUDIO_DIR OUT.wav}
out=${2:?usage: make_intro_sound.sh SCIFI_AUDIO_DIR OUT.wav}
period_ms=3200
total_s=8.623
fade_s=1.2
engine=$(ls "$dir"/spaceEngineLow_000.* | head -1)
impact=$(ls "$dir"/lowFrequency_explosion_000.* | head -1)
mono='aformat=sample_rates=44100:channel_layouts=mono'
ffmpeg -v error -y -i "$engine" -i "$impact" \
    -filter_complex "[0:a]$mono,afade=t=in:st=0:d=2.8:curve=exp,afade=t=out:st=2.4:d=2.6[engine];\
[1:a]$mono,adelay=2800[impact];\
[engine][impact]amix=inputs=2:normalize=0:duration=longest,apad=whole_dur=6,\
afade=t=out:st=4.8:d=1.2,asplit[first][again];\
[again]adelay=${period_ms}[second];\
[first][second]amix=inputs=2:normalize=0:duration=longest,apad=whole_dur=$total_s,\
afade=t=out:st=$(python3 -c "print($total_s-$fade_s)"):d=$fade_s,\
loudnorm=I=-18:TP=-2,volume=-6dB,$mono" \
    -t "$total_s" "$out"
