"""Formula Legends -- turn ElevenLabs engine recordings into the game's engine loops.

The takes were generated with ElevenLabs sound effects (eleven_text_to_sound_v3), one prompt per era:
  1998  naturally aspirated 3.0 litre V10 racing engine held at steady high revs, constant pitch,
        shrill screaming howl, close-mic exhaust, no gear changes, no doppler
  2010  naturally aspirated 2.4 litre V8 racing engine at a steady 18000 rpm, constant pitch,
        piercing high-pitched scream, close-mic exhaust, no gear changes, no doppler
  2016  1.6 litre turbocharged hybrid V6 racing engine held at steady high revs, constant pitch,
        deep muffled growl with turbo whistle, close-mic exhaust, no gear changes, no doppler
  2025  1.6 litre turbocharged hybrid V6 racing engine at steady high revs, constant pitch, raspy
        turbo growl with electric motor whine, close-mic exhaust, no gear changes, no doppler

    python3 tools/audio/make_engine_loops.py <folder of engine_<year>_<take>.mp3>

For each era it picks the take (and the 2.5 s stretch of it) whose tone and loudness hold steadiest,
makes it loop seamlessly (the end crossfades into the start), matches the old loop's loudness, and
writes DATA/audio/f1_<era>.raw: 22050 Hz mono 16-bit, 2.5 s. The game treats each loop as the
engine at 72 % of the way from idle to the rev limit and pitches it with the revs (src/sound.cpp).
Needs ffmpeg.
"""
import glob, os, subprocess, sys
import numpy as np

SR, L, X = 22050, int(2.5 * 22050), int(0.15 * 22050)       # loop length, crossfade
ERAS = {'1998': '90s', '2010': 'v8', '2016': 'hybrid', '2025': 'ge'}
HERE = os.path.dirname(os.path.abspath(__file__))
AUDIO = os.path.join(HERE, '..', '..', 'DATA', 'audio')


def decode(path):
    raw = subprocess.run(['ffmpeg', '-loglevel', 'error', '-i', path, '-ac', '1', '-ar', str(SR), '-f', 's16le', '-'],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, '<i2').astype(np.float32)


def steadiest(x):
    """(score, start sample) of the 2.5 s stretch whose brightness, loudness and note vary least"""
    fr = np.lib.stride_tricks.sliding_window_view(x, 1024)[::256] * np.hanning(1024)
    S = np.abs(np.fft.rfft(fr, axis=1)) + 1e-9
    f = np.fft.rfftfreq(1024, 1 / SR)
    cent = (S * f).sum(1) / S.sum(1)
    rms = np.sqrt((fr ** 2).mean(1))
    note = f[np.argmax(S[:, 4:200], axis=1) + 4]
    w = (L + X) // 256 + 1
    best = None
    for s in range(0, len(cent) - w, 4):
        c, r, p = cent[s:s + w], rms[s:s + w], note[s:s + w]
        if r.mean() < 1500:
            continue
        score = c.std() / c.mean() + 1.5 * r.std() / r.mean() + 0.5 * np.median(np.abs(np.diff(p))) / (np.median(p) + 1)
        if best is None or score < best[0]:
            best = (score, s * 256)
    return best


def make_loop(seg, target_rms):
    seg = seg - seg.mean()
    y = seg[:L].copy()
    w = np.sqrt(np.linspace(0, 1, X))                      # equal-power crossfade
    y[:X] = seg[L:L + X] * np.sqrt(1 - w ** 2) + seg[:X] * w   # the wrap carries on from the end
    y *= target_rms / np.sqrt((y ** 2).mean())
    peak = np.abs(y).max()
    if peak > 30000:                                        # soft-limit the loudest peaks
        y = 30000 * np.tanh(y / 30000)
    return np.clip(np.round(y), -32768, 32767).astype('<i2')


def main(folder):
    for year, era in ERAS.items():
        if not glob.glob(os.path.join(folder, 'engine_%s_*.mp3' % year)):
            continue                                        # no takes for this era: its loop stays
        out = os.path.join(AUDIO, 'f1_%s.raw' % era)
        old = np.fromfile(out, '<i2').astype(np.float32)
        target = np.sqrt((old ** 2).mean())
        picks = []
        for p in sorted(glob.glob(os.path.join(folder, 'engine_%s_*.mp3' % year))):
            x = decode(p)
            b = steadiest(x)
            if b:
                picks.append((b[0], b[1], p, x))
        if not picks:
            print(year, 'no usable take'); continue
        score, start, path, x = min(picks)
        y = make_loop(x[start:start + L + X], target)
        y.tofile(out)
        jump = abs(int(y[0]) - int(y[-1]))
        print('%s -> f1_%s.raw  from %s at %.1f s (steadiness %.3f), loop seam step %d' %
              (year, era, os.path.basename(path), start / SR, score, jump))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '.')
