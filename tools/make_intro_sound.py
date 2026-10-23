#!/usr/bin/env python3
"""Builds assets/sounds/intro_swell.wav, the launch intro's sound.

Kenney's Sci-fi Sounds pack (CC0) has no swell or power-up, only engine loops
and impacts, so the rise is synthesised here: low-passed noise with a rising
cutoff under an exponential sine sweep, both growing to the impact. The
impact at the peak is Kenney's lowFrequency_explosion_000, decoded to a mono
44.1 kHz wav and passed as the second argument. Without it a soft sine thump
stands in, so the script still runs with nothing but the standard library.

Usage: make_intro_sound.py OUT.wav [lowFrequency_explosion_000.wav]
"""
import math
import random
import struct
import sys
import wave

RATE = 44100
HIT_S = 1.2           # the impact starts here and is loudest 0.2 s later, at 1.4 s
TOTAL_S = 3.2         # (intro_peak_ns and intro_total_ns in hedgr.ui/render/intro.hpp)
SWELL_TAIL_S = 0.25   # the swell rings briefly past the impact


def read_mono(path):
    with wave.open(path) as w:
        assert w.getnchannels() == 1 and w.getframerate() == RATE
        raw = w.readframes(w.getnframes())
    return [s / 32768.0 for s in struct.unpack("<%dh" % (len(raw) // 2), raw)]


def swell(n_rise, n_tail):
    rng = random.Random(7)
    out = []
    low = 0.0
    phase = 0.0
    for i in range(n_rise + n_tail):
        if i < n_rise:
            x = i / n_rise
            gain = x ** 2.2
        else:
            x = 1.0
            gain = math.exp(-(i - n_rise) / (SWELL_TAIL_S * RATE / 4.0))
        cutoff = 200.0 * (40.0 ** x)
        a = 1.0 - math.exp(-2.0 * math.pi * cutoff / RATE)
        low += a * (rng.uniform(-1.0, 1.0) - low)
        freq = 90.0 * (10.0 ** x)
        phase += 2.0 * math.pi * freq / RATE
        out.append(gain * (0.9 * low + 0.35 * math.sin(phase)))
    return out


def main():
    out_path = sys.argv[1]
    n_total = int(TOTAL_S * RATE)
    n_rise = int(HIT_S * RATE)
    mix = [0.0] * n_total
    for i, s in enumerate(swell(n_rise, int(SWELL_TAIL_S * RATE))):
        mix[i] += s * 0.55
    if len(sys.argv) > 2:
        hit = read_mono(sys.argv[2])
    else:
        hit = [math.sin(2 * math.pi * 55.0 * i / RATE) * math.exp(-i / (0.35 * RATE))
               for i in range(2 * RATE)]
    for i, s in enumerate(hit):
        if n_rise + i < n_total:
            mix[n_rise + i] += s * 0.9
    fade = int(0.05 * RATE)
    for i in range(fade):
        mix[n_total - 1 - i] *= i / fade
    top = max(abs(s) for s in mix)
    with wave.open(out_path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(
            struct.pack("<h", int(max(-1.0, min(1.0, s / top * 0.85)) * 32767))
            for s in mix))


main()
