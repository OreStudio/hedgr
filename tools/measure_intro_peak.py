#!/usr/bin/env python3
"""Prints the centre, in seconds, of the loudest 50 ms window of each pulse.

Usage: measure_intro_peak.py assets/sounds/intro_swell.wav [PULSES PERIOD_S]
Pulse k is searched in [k * PERIOD_S, (k + 1) * PERIOD_S). The defaults are the
intro's two pulses 3.2 s apart. The first centre is what intro_peak_ns in
hedgr.ui/render/intro.hpp is set to and the gap to the next is
intro_pulse_period_ns. The windows' mean-square levels are printed in dBFS.
"""
import math
import struct
import sys
import wave

with wave.open(sys.argv[1]) as w:
    rate, n = w.getframerate(), w.getnframes()
    samples = struct.unpack("<%dh" % n, w.readframes(n))
pulses = int(sys.argv[2]) if len(sys.argv) > 2 else 2
period = float(sys.argv[3]) if len(sys.argv) > 3 else 3.2
window, hop = rate // 20, rate // 1000
cumulative = [0]
for s in samples:
    cumulative.append(cumulative[-1] + s * s)
energy = lambda i: cumulative[i + window] - cumulative[i]
for k in range(pulses):
    lo = int(k * period * rate)
    hi = min(int((k + 1) * period * rate), n - window)
    best = max(range(lo, hi, hop), key=energy)
    level = 10 * math.log10(energy(best) / window / 32768 ** 2)
    print("pulse %d: %.3f s, %.1f dBFS" % (k, (best + window / 2) / rate, level))
