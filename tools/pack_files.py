#!/usr/bin/env python3
"""Pack game files on the memory card: 'FLZ1', u32 unpacked size, then a zlib stream. sys::loadFile
unpacks them as it reads them, so nothing else changes (memory use is the same). Only files that
shrink by a fifth or more are packed; packed ones are skipped, so it can run again after a tool
rewrites a model.

usage: pack_files.py file [...]      (e.g. tools/pack_files.py HD/models/* DATA/models/* DATA/tracks/*)
"""
import struct, sys, zlib

saved = 0
for path in sys.argv[1:]:
    try:
        data = open(path, "rb").read()
    except IsADirectoryError:
        continue
    if data[:4] == b"FLZ1" or len(data) < 4096:
        continue
    z = zlib.compress(data, 9)
    if len(z) + 8 > len(data) * 0.8:
        continue
    open(path, "wb").write(b"FLZ1" + struct.pack("<I", len(data)) + z)
    saved += len(data) - len(z) - 8
print(f"packed: {saved / 1e6:.1f} MB saved")
