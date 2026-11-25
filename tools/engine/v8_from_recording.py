#!/usr/bin/env python3
"""2010 V8 engine loop from a recording (eng_350_all.wav: run-ups at several loads).

Takes the steady full-throttle stretch at the top of a run-up, raises the pitch so it sits where the
game expects the engine at its reference revs (0.72 of the way from idle to the limit: the game
pitches the loop with the revs from there), and crossfades the ends so it loops without a click.

usage: v8_from_recording.py eng_350_all.wav DATA/audio/f1_v8.raw [preview.wav]
"""
import subprocess, sys
import numpy as np
from scipy.signal import resample_poly

SRC_SR = 38606
OUT_SR = 22050
START, LENGTH = 44.35, 1.75     # seconds: the loudest run-up, held at the top
PITCH = 1.8                     # 4-litre road V8 at ~6,500 rpm -> a 2.4-litre F1 V8 at ~14,000 rpm
XFADE = 0.25                    # seconds of crossfade at the loop point


def main():
    src, out = sys.argv[1], sys.argv[2]
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", src, "-f", "s16le", "-ac", "1", "-ar", str(SRC_SR), "-"],
                         check=True, capture_output=True).stdout
    x = np.frombuffer(raw, np.int16).astype(np.float64) / 32768
    seg = x[int(START * SRC_SR): int((START + LENGTH) * SRC_SR)]
    # pitch up by playing it faster: SRC_SR * PITCH -> OUT_SR
    up, down = OUT_SR * 100, int(round(SRC_SR * PITCH * 100))
    g = np.gcd(up, down)
    y = resample_poly(seg, up // g, down // g)
    n = int(XFADE * OUT_SR)
    body, tail = y[:-n], y[-n:]
    w = np.linspace(0, 1, n)
    body[:n] = body[:n] * w + tail * (1 - w)            # the end flows into the start
    body -= body.mean()
    body *= 0.25 / np.sqrt((body ** 2).mean())        # as loud as the other engine loops (rms ~0.25)
    body = np.tanh(body / 0.95) * 0.95                  # (round off the odd peak)
    s = np.clip(np.round(body * 32767), -32768, 32767).astype("<i2")
    s.tofile(out)
    print(f"{out}: {len(s) / OUT_SR:.2f} s loop, rms {np.sqrt((body ** 2).mean()):.3f}")
    if len(sys.argv) > 3:
        import scipy.io.wavfile as wf
        wf.write(sys.argv[3], OUT_SR, np.tile(s, 3))


if __name__ == "__main__":
    main()
