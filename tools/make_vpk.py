"""Pack the Vita install files (a VPK is a zip) so they install quickly.

  python3 make_vpk.py out.vpk eboot.bin param.sfo SRC=DST [SRC=DST ...] [--skip GLOB ...]

Each file is zipped only when that makes it at least 10 % smaller. Most of the game's data is already
packed (zlib models and tracks, ADPCM sound), and zipping it again saves almost nothing while the Vita
still has to unzip every byte while installing; those files are stored as they are.
SRC may be a folder (everything under it goes in under DST). --skip leaves out matching paths
(DST-relative, fnmatch) unless they match a --keep, e.g. the music, which comes in its own pack (make_music_pack.py).
"""
import os, sys, zlib, zipfile, fnmatch


def entries(args):
    for a in args:
        src, dst = a.split('=', 1)
        if os.path.isdir(src):
            for dp, _, fs in os.walk(src):
                for f in sorted(fs):
                    p = os.path.join(dp, f)
                    yield p, (dst + '/' + os.path.relpath(p, src)).replace(os.sep, '/')
        else:
            yield src, dst


def main():
    argv = sys.argv[1:]
    skip, keep = [], []
    for flag, lst in (('--skip', skip), ('--keep', keep)):
        while flag in argv:
            i = argv.index(flag); lst.append(argv[i + 1]); del argv[i:i + 2]
    out, eboot, sfo, rest = argv[0], argv[1], argv[2], argv[3:]
    files = [(eboot, 'eboot.bin'), (sfo, 'sce_sys/param.sfo')] + list(entries(rest))
    raw = packed = stored = 0
    with zipfile.ZipFile(out + '.tmp', 'w') as z:
        for src, dst in files:
            if any(fnmatch.fnmatch(dst, g) for g in skip) and not any(fnmatch.fnmatch(dst, g) for g in keep):
                continue
            b = open(src, 'rb').read()
            small = len(zlib.compress(b, 6)) < len(b) * 0.9
            info = zipfile.ZipInfo(dst, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED if small else zipfile.ZIP_STORED
            info.external_attr = 0o644 << 16
            z.writestr(info, b, compresslevel=6 if small else None)
            raw += len(b); stored += 0 if small else 1; packed += 1
    os.replace(out + '.tmp', out)
    print(f'{out}: {packed} files, {raw / 1e6:.1f} MB of data ({stored} stored as they are), '
          f'{os.path.getsize(out) / 1e6:.1f} MB to install')


if __name__ == '__main__':
    main()
