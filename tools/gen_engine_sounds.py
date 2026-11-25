#!/usr/bin/env python3
"""Formula Legends engine sounds, made from scratch (no recordings, no sawtooth oscillators).

Each era's engine is built from what makes the sound in a real one:
  * combustion pulses: every firing is a short burst that rings the exhaust's resonances (damped
    sine waves), placed exactly on the firing order, with a little cycle-to-cycle variation
  * the engine orders: the firing frequency and its neighbours as plain sine waves
  * intake / mechanical noise: shaped noise
  * 2016 / 2025: the turbo whistle and the MGU-K's electric whine

Output: DATA/audio/f1_<era>.raw, 22050 Hz mono 16-bit, a 2.5 s seamless loop at the reference revs the
game assumes for every loop (72 % of the way from idle to the limit, src/sound.cpp). Everything is
built round the loop (frequencies are whole multiples of 0.4 Hz, the pulses circularly convolved),
so the loop has no seam.      python3 tools/gen_engine_sounds.py [wav preview dir]
"""
import os, sys
import numpy as np

SR = 22050
N = int(SR * 2.5)
T = N / SR
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
rng = np.random.default_rng(4)
t = np.arange(N) / SR


def snap(f):                       # to a whole number of cycles per loop
    return round(f * T) / T


def ref_rpm(idle, mx):
    return idle + (mx - idle) * 0.72


def sines(parts):
    """parts: (freq, amp, phase-wobble depth Hz, wobble rate Hz)"""
    out = np.zeros(N)
    for f, a, wd, wr in parts:
        f = snap(f)
        ph = 2 * np.pi * f * t
        if wd:
            wr = snap(wr)
            ph += (wd / wr) * np.sin(2 * np.pi * wr * t + rng.uniform(0, 6.28))
        out += a * np.sin(ph + rng.uniform(0, 6.28))
    return out


def burst_kernel(res, length=0.012):
    """one firing: the exhaust resonances (freq Hz, decay s, amp) ringing after the pulse"""
    k = np.zeros(int(length * SR))
    tk = np.arange(len(k)) / SR
    for f, tau, a in res:
        k += a * np.exp(-tk / tau) * np.sin(2 * np.pi * f * tk)
    k[:8] *= np.linspace(0, 1, 8)
    return k


def pulse_train(crank_hz, cyl, pattern, jitter, res):
    """impulses at the firing times (cyl firings per 2 crank revs), amplitudes from the bank
    pattern plus random cycle-to-cycle variation; circularly convolved with the burst"""
    crank = snap(crank_hz / 2) * 2          # a whole number of 4-stroke cycles in the loop
    cycles = int(round(crank / 2 * T))
    fires = cycles * cyl
    x = np.zeros(N)
    for i in range(fires):
        pos = i * N / fires
        a = pattern[i % len(pattern)] * (1 + jitter * rng.normal())
        p0 = int(pos); fr = pos - p0
        x[p0 % N] += a * (1 - fr); x[(p0 + 1) % N] += a * fr
    k = burst_kernel(res)
    K = np.fft.rfft(np.concatenate([k, np.zeros(N - len(k))]))
    return np.fft.irfft(np.fft.rfft(x) * K, N), crank


def noise(lo, hi, amp):
    """periodic band-limited noise"""
    S = np.zeros(N // 2 + 1, complex)
    f = np.fft.rfftfreq(N, 1 / SR)
    band = (f >= lo) & (f <= hi)
    S[band] = np.exp(1j * rng.uniform(0, 6.28, band.sum())) * np.hanning(band.sum() + 2)[1:-1]
    x = np.fft.irfft(S, N)
    return amp * x / (np.abs(x).max() + 1e-9)


def lowpass(x, fc, order=2):
    f = np.fft.rfftfreq(N, 1 / SR)
    H = 1 / (1 + (f / fc) ** (2 * order)) ** 0.5
    return np.fft.irfft(np.fft.rfft(x) * H, N)


def norm(x, peak=0.62):
    x = x - x.mean()
    return x / np.abs(x).max() * peak


ERAS = {}

# (the 1998 V10 -- no longer used by any car since the era became 1980 -- kept for reference)
# 1998 V10, 17,500 rpm: a hard, high scream. Five firings a crank turn, even spacing, bright exhaust
idle, mx = 4000, 17500
crank = ref_rpm(idle, mx) / 60
p, cr = pulse_train(crank, 10, [1.0, 0.95, 1.0, 0.93, 0.98], 0.06,
                    [(2350, 0.0011, 1.0), (3900, 0.0007, 0.7), (1450, 0.0016, 0.6), (5600, 0.0004, 0.35)])
fire = cr * 5
x = 0.55 * norm(p) + norm(sines([(fire, 1.0, 3, 6.4), (fire * 2, 0.55, 5, 6.4), (fire * 3, 0.3, 6, 4.0),
                                (fire * 0.5, 0.18, 0, 0), (fire * 1.5, 0.14, 0, 0)])) * 0.5
x += noise(3000, 8000, 0.10)
ERAS['90s'] = (norm(lowpass(x, 7500)), idle, mx)

# 2010 V8, 18,000 rpm: high and raspy with a rumble under it (flat-plane: the banks alternate,
# uneven pulses each half-turn, the half-order "rumble" the 2010-era game made famous)
idle, mx = 4500, 18000
crank = ref_rpm(idle, mx) / 60
p, cr = pulse_train(crank, 8, [1.0, 0.72, 0.92, 0.66], 0.10,
                    [(1900, 0.0014, 1.0), (3100, 0.0009, 0.8), (950, 0.0022, 0.5), (4700, 0.0005, 0.3)])
fire = cr * 4
x = 0.65 * norm(p) + 0.45 * norm(sines([(fire, 1.0, 4, 7.2), (fire * 2, 0.6, 6, 7.2), (fire * 3, 0.25, 0, 0),
                                       (fire * 0.5, 0.45, 2, 3.2), (fire * 0.25, 0.22, 0, 0), (fire * 0.75, 0.2, 0, 0)]))
x += noise(1500, 6500, 0.12)
ERAS['v8'] = (norm(lowpass(x, 6800)), idle, mx)

# 2016 V6 turbo hybrid, 15,000 rpm: muted -- the turbo eats the exhaust pulses: a soft, low,
# rounded note, a faint turbo whistle and the MGU-K underneath
idle, mx = 4000, 15000
crank = ref_rpm(idle, mx) / 60
p, cr = pulse_train(crank, 6, [1.0, 0.9, 0.95], 0.05,
                    [(900, 0.0024, 1.0), (1600, 0.0013, 0.45), (2600, 0.0008, 0.18)])
fire = cr * 3
x = 0.5 * norm(p) + 0.6 * norm(sines([(fire, 1.0, 2, 5.6), (fire * 2, 0.35, 2, 5.6), (fire * 0.5, 0.3, 0, 0), (fire * 1.5, 0.12, 0, 0)]))
x = lowpass(x, 2200, 2)
x += 0.06 * norm(sines([(4800, 1.0, 25, 0.8)]))                    # turbo whistle
x += 0.05 * norm(sines([(fire * 2.75, 1.0, 3, 1.2)]))              # MGU-K
x += noise(300, 2500, 0.07)
ERAS['hybrid'] = (norm(x), idle, mx)

# 2025 V6 turbo hybrid, 13,000 rpm (most of the noise is the electric side): the engine note under a
# clear electric hum -- the MGU-K whine (geared to the crank) with its harmonics, a slow
# shimmer as the inverter switches, and the turbo / MGU-H whistle on top
idle, mx = 4000, 13000
crank = ref_rpm(idle, mx) / 60
p, cr = pulse_train(crank, 6, [1.0, 0.92, 0.96], 0.05,
                    [(1050, 0.0020, 1.0), (1900, 0.0011, 0.5), (3000, 0.0007, 0.2)])
fire = cr * 3
eng = 0.5 * norm(p) + 0.55 * norm(sines([(fire, 1.0, 2, 5.0), (fire * 2, 0.4, 2, 5.0), (fire * 0.5, 0.25, 0, 0)]))
eng = lowpass(eng, 3000, 2)
whine = norm(sines([(fire * 3.2, 1.0, 6, 2.4), (fire * 6.4, 0.42, 10, 2.4), (fire * 1.6, 0.35, 3, 2.4), (fire * 9.6, 0.12, 0, 0)]))
shimmer = 1 + 0.18 * np.sin(2 * np.pi * snap(9.6) * t)                # the inverter's slow beat
x = 0.62 * norm(eng) + 0.42 * whine * shimmer + 0.08 * norm(sines([(6200, 1.0, 40, 0.8)]))
x += noise(400, 3200, 0.05)
ERAS['ge'] = (norm(x), idle, mx)

# 1980 Cosworth DFV-style V8, 11,000 rpm: a cross-plane crank -- each bank fires unevenly, so the note is
# a deep, hard-edged bark with a strong burble under it rather than a scream; open exhausts, lots of
# induction roar from the trumpets
idle, mx = 3200, 11000
crank = ref_rpm(idle, mx) / 60
p, cr = pulse_train(crank, 8, [1.0, 0.62, 0.88, 0.70, 0.95, 0.60, 0.84, 0.74], 0.12,
                    [(1150, 0.0020, 1.0), (2100, 0.0012, 0.75), (650, 0.0030, 0.55), (3300, 0.0007, 0.3)])
fire = cr * 4
x = 0.70 * norm(p) + 0.42 * norm(sines([(fire, 1.0, 3, 5.0), (fire * 2, 0.5, 4, 5.0), (fire * 0.5, 0.55, 2, 2.4),
                                       (fire * 0.25, 0.30, 0, 0), (fire * 1.5, 0.22, 0, 0), (fire * 3, 0.15, 0, 0)]))
x += noise(800, 4200, 0.14)                     # induction roar
x = norm(lowpass(x, 5200))
ERAS['80s'] = (norm(np.tanh(x * 2.2)), idle, mx)     # driven a little harder: as loud as the others

out = os.path.join(ROOT, 'DATA', 'audio')
for era, (x, idle, mx) in ERAS.items():
    s = np.clip(np.round(x * 32767), -32768, 32767).astype('<i2')
    if era == 'v8':
        continue        # the 2010 V8 is a recording now: tools/engine/v8_from_recording.py
    s.tofile(os.path.join(out, f'f1_{era}.raw'))
    print(f'f1_{era}.raw  idle {idle}  limit {mx}  ref {ref_rpm(idle, mx):.0f} rpm  rms {np.sqrt((x ** 2).mean()):.3f}')
    if len(sys.argv) > 1:
        import scipy.io.wavfile as w
        # a rev sweep preview: idle -> limit, the way the game pitches the loop
        sweep = []
        pos = 0.0
        for k in range(int(SR * 4)):
            rpm = idle + (mx - idle) * (k / (SR * 4))
            rate = min(1.6, max(0.3, rpm / ref_rpm(idle, mx)))
            sweep.append(s[int(pos) % N]); pos += rate
        w.write(os.path.join(sys.argv[1], f'f1_{era}_sweep.wav'), SR, np.array(sweep, '<i2'))
        w.write(os.path.join(sys.argv[1], f'f1_{era}_loop.wav'), SR, np.tile(s, 2))
