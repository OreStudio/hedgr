#!/usr/bin/env python3
"""Prints the centre, in seconds, of the loudest 50 ms window of a mono wav.

Usage: measure_intro_peak.py assets/sounds/intro_swell.wav
The result is what intro_peak_ns in hedgr.ui/render/intro.hpp is set to.
"""
import struct
import sys
import wave

with wave.open(sys.argv[1]) as w:
    rate, n = w.getframerate(), w.getnframes()
    samples = struct.unpack("<%dh" % n, w.readframes(n))
window, hop = rate // 20, rate // 1000
best = max(range(0, n - window, hop),
           key=lambda i: sum(s * s for s in samples[i:i + window]))
print("%.3f" % ((best + window / 2) / rate))
