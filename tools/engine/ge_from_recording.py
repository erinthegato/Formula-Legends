#!/usr/bin/env python3
"""2025 engines from a recording: F1_25_engine_sounds.mp3 holds ten 20-second stretches, one per team,
each with its own engine note. For every stretch: find the steadiest high-rev, full-throttle 1.6 s
(the pitch barely moving, loud), pitch it so it sits at the game's reference revs, crossfade it into a
loop, and write DATA/audio/f1_ge<k>.raw (16-bit mono 22050 Hz).

The teams take them in the order of the grid (data.cpp): stretch k -> the k-th 2025 team.
usage: ge_from_recording.py F1_25_engine_sounds.mp3 DATA/audio [preview_dir]
"""
import subprocess, sys, os
import numpy as np
from scipy.signal import resample_poly, butter, sosfiltfilt

SR = 48000
OUT = 22050
BLOCK = 20.0
WIN = 1.6
XF = 0.25
REF_RPM = 4000 + (13000 - 4000) * 0.72          # the loop stands for these revs (sound.cpp setVoice)


def crank_hz(seg):
    """crank frequency: the strongest peak between 140 and 260 Hz (the V6's half order)"""
    n = 1 << 16
    sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n))
    f = np.fft.rfftfreq(n, 1 / SR)
    m = (f > 140) & (f < 260)
    return f[m][np.argmax(sp[m])], sp[m].max() / (sp.mean() + 1e-9)


def best(block):
    best, score = None, -1e9
    step = int(0.1 * SR)
    w = int(WIN * SR)
    for a in range(0, len(block) - w, step):
        seg = block[a:a + w]
        rms = np.sqrt((seg ** 2).mean())
        parts = [crank_hz(seg[k:k + w // 4])[0] for k in range(0, w - w // 4 + 1, w // 4)]
        f0 = np.mean(parts); spread = np.std(parts) / f0
        s = np.log(rms + 1e-6) * 1.0 + f0 / 60.0 - spread * 60
        if s > score: score, best = s, (a, f0, spread, rms)
    return best


def main():
    src, out = sys.argv[1], sys.argv[2]
    prev = sys.argv[3] if len(sys.argv) > 3 else None
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", src, "-ac", "1", "-ar", str(SR), "-f", "s16le", "-"],
                         check=True, capture_output=True).stdout
    x = np.frombuffer(raw, np.int16).astype(np.float64) / 32768
    x = sosfiltfilt(butter(2, 40, "highpass", fs=SR, output="sos"), x)
    nb = int(len(x) // (BLOCK * SR))
    for k in range(nb):
        block = x[int(k * BLOCK * SR): int((k + 1) * BLOCK * SR)]
        a, f0, spread, rms = best(block)
        seg = block[a:a + int(WIN * SR)]
        rpm = f0 * 60
        if rpm > 13000: rpm /= 1.5      # it caught the 1.5th order (a V6's strong half orders): 2025 cars stay under ~12,500
        pitch = REF_RPM / rpm                          # > 1: raise it
        up, down = OUT * 100, int(round(SR * pitch * 100))
        g = np.gcd(up, down)
        y = resample_poly(seg, up // g, down // g)
        n = int(XF * OUT)
        body, tail = y[:-n].copy(), y[-n:]
        w = np.linspace(0, 1, n)
        body[:n] = body[:n] * w + tail * (1 - w)
        body -= body.mean()
        body *= 0.21 / np.sqrt((body ** 2).mean())       # as loud as the old 2025 loop
        body = np.tanh(body / 0.95) * 0.95
        s = np.clip(np.round(body * 32767), -32768, 32767).astype("<i2")
        s.tofile(os.path.join(out, f"f1_ge{k}.raw"))
        print(f"team {k}: {k * BLOCK + a / SR:6.1f} s  ~{rpm:5.0f} rpm (pitch x{pitch:.2f})  steadiness {spread * 100:.1f} %  loop {len(s) / OUT:.2f} s")
        if prev:
            import scipy.io.wavfile as wf
            wf.write(os.path.join(prev, f"ge{k}.wav"), OUT, np.tile(s, 2))


if __name__ == "__main__":
    main()
