#!/usr/bin/env python3
"""2025 engine from a set of engine loops (on-throttle and off-throttle / overrun).

  on_high_rpm_loop.wav     full throttle (crank ~165 Hz: ~9,900 rpm)   -> DATA/audio/f1_ge.raw
  decel_high_rpm_loop.wav  off throttle  (crank ~98 Hz: ~5,850 rpm)    -> DATA/audio/f1_ge_off.raw

Both are pitched to the game's reference revs (0.72 of the way from idle to the limit: 10,480 rpm
for the 2025 cars) so the game plays them at the same rate and blends them with the throttle
(sound.cpp: a car whose sound has a "<name>_off" loop gets the two layers).
usage: ge_from_loops.py <loops dir> DATA/audio [preview.wav]
"""
import os, sys
import numpy as np
from fractions import Fraction
import scipy.io.wavfile as wf
from scipy.signal import resample_poly

OUT = 22050
REF_RPM = 4000 + (13000 - 4000) * 0.72
LAYERS = [("on_high_rpm_loop.wav", 165.0, "f1_ge.raw", 0.21),
          ("decel_high_rpm_loop.wav", 97.6, "f1_ge_off.raw", 0.15)]
XF = 0.04


def load(path):
    sr, x = wf.read(path)
    x = x.astype(np.float64)
    if x.ndim > 1: x = x.mean(axis=1)
    return sr, x / 32768


def main():
    src, out = sys.argv[1], sys.argv[2]
    made = []
    for name, crank, outname, level in LAYERS:
        sr, x = load(os.path.join(src, name))
        pitch = (REF_RPM / 60) / crank
        fr = Fraction(OUT / (sr * pitch)).limit_denominator(400)      # (small factors: resample_poly's filter length)
        y = resample_poly(np.concatenate([x, x[:int(sr * 0.2)]]), fr.numerator, fr.denominator)   # a little past the end for the seam
        n_loop = int(round(len(x) / (sr * pitch) * OUT))
        n = int(XF * OUT)
        body = y[:n_loop].copy()
        w = np.linspace(0, 1, n)
        body[:n] = body[:n] * w + y[n_loop:n_loop + n] * (1 - w)    # the end runs on into the start
        body -= body.mean()
        body *= level / np.sqrt((body ** 2).mean())
        body = np.tanh(body / 0.95) * 0.95
        s = np.clip(np.round(body * 32767), -32768, 32767).astype("<i2")
        s.tofile(os.path.join(out, outname))
        made.append(s)
        print(f"{outname}: {name}  ~{crank * 60:.0f} rpm -> x{pitch:.2f}  {len(s) / OUT:.2f} s loop")
    if len(sys.argv) > 3:
        on, off = made
        prev = np.concatenate([np.tile(on, 2), np.tile(off, 2)])
        wf.write(sys.argv[3], OUT, prev)


if __name__ == "__main__":
    main()
