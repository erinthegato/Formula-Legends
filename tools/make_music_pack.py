"""The music pack: the race playlist, the attract and podium music, outside the VPK.

  python3 make_music_pack.py out.zip DATA/music

Extract the zip at the root of ux0: (VitaShell: open it, select all, copy, paste in ux0:). It puts the
music in ux0:data/FormulaLegends/DATA/music/, where the game looks for any data file the app itself
doesn't carry. Install it once: game updates (the VPK) then stay small, and the music stays where it is.
The intro and headquarters music stay in the VPK, so the game is never silent at the start.
"""
import os, sys, zipfile

IN_VPK = {'intro.emu', 'hq_1980.emu', 'hq_2010.emu', 'hq_2016.emu', 'hq_2025.emu'}

out, src = sys.argv[1], sys.argv[2]
n = size = 0
with zipfile.ZipFile(out + '.tmp', 'w') as z:
    for f in sorted(os.listdir(src)):
        if not f.endswith('.emu') or f in IN_VPK:
            continue
        info = zipfile.ZipInfo('data/FormulaLegends/DATA/music/' + f, date_time=(2026, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_STORED          # ADPCM: zip would save ~10 % and cost unzip time
        info.external_attr = 0o644 << 16
        b = open(os.path.join(src, f), 'rb').read()
        z.writestr(info, b); n += 1; size += len(b)
os.replace(out + '.tmp', out)
print(f'{out}: {n} tracks, {size / 1e6:.1f} MB')
