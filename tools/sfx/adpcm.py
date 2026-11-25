#!/usr/bin/env python3
"""Compact sound effects: 'ADP1' files (IMA ADPCM, 4 bits a sample, mono) that the game keeps packed in
memory and decodes as it plays them (sound.cpp). A quarter of the size of 16-bit samples.

  ADP1 file: 'ADP1', u32 sample rate (22050 or 11025), u32 samples, then the codes, two a byte, low
  nibble first; the decoder starts from predictor 0, step index 0.

usage: adpcm.py [--ulaw] file.raw [...]   16-bit mono 22050 Hz .raw files -> ADP1 (or ULW1), in place
       (or import: encode / ulaw(samples_int16, rate) -> bytes)
"""
import struct, sys
import numpy as np

STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
        107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
        4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
        22385, 24623, 27086, 29794, 32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


def encode(x, rate):
    x = [int(v) for v in np.asarray(x, np.int16)]
    pred, index = 0, 0
    codes = bytearray((len(x) + 1) // 2)
    for i, s in enumerate(x):
        step = STEP[index]; diff = s - pred; code = 0
        if diff < 0: code = 8; diff = -diff
        vd = step >> 3
        if diff >= step: code |= 4; diff -= step; vd += step
        step >>= 1
        if diff >= step: code |= 2; diff -= step; vd += step
        step >>= 1
        if diff >= step: code |= 1; vd += step
        pred += -vd if code & 8 else vd
        pred = 32767 if pred > 32767 else (-32768 if pred < -32768 else pred)
        index += IDX[code]; index = 0 if index < 0 else (88 if index > 88 else index)
        codes[i >> 1] |= code << ((i & 1) * 4)
    return b"ADP1" + struct.pack("<II", rate, len(x)) + bytes(codes)




def ulaw(x, rate):
    """'ULW1' file: 8-bit mu-law (G.711), for voice and sharp sounds that ADPCM would make hiss:
    'ULW1', u32 rate, u32 samples, one byte a sample"""
    x = np.asarray(x, np.int32)
    BIAS, CLIP = 0x84, 32635
    sign = np.where(x < 0, 0x80, 0)
    m = np.minimum(np.abs(x), CLIP) + BIAS
    exp = np.zeros_like(m)
    for e in range(7, 0, -1):
        exp = np.where((exp == 0) & (m >= (0x80 << e)), e, exp)
    mant = (m >> (exp + 3)) & 0x0F
    code = (~(sign | (exp << 4) | mant)) & 0xFF
    return b"ULW1" + struct.pack("<II", rate, len(x)) + code.astype(np.uint8).tobytes()


if __name__ == "__main__":
    mu = "--ulaw" in sys.argv
    for path in [a for a in sys.argv[1:] if a != "--ulaw"]:
        data = open(path, "rb").read()
        if data[:4] in (b"ADP1", b"ULW1"):
            continue
        out = (ulaw if mu else encode)(np.frombuffer(data, "<i2"), 22050)
        open(path, "wb").write(out)
        print(f"{path}: {len(data)} -> {len(out)} bytes")
