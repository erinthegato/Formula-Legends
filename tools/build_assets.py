"""Build the small assets: engine loops, font, ground textures, PSP icon.

  python3 tools/build_assets.py <work dir> <data out dir> <repo root>

engines : the Unity project's 10 engine recordings -> 22050 Hz mono 16-bit loops (.raw) with a
          crossfaded seam, so the PSP can pitch them up and down with the revs.
font    : DejaVu Sans Bold (Bitstream Vera licence) -> 8-bit alpha atlas + glyph table (font.bin).
ground  : Unity-AI tiling textures (asphalt, grass, gravel, kerb) -> 64x64 / 128x128 CLUT8 (.tex).
icon    : ICON0.PNG 144x80 for the PSP menu, from the game's app icon.
"""
import os, sys, struct, subprocess
import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_models import swizzle, quantise

ENGINES = ['engine_flat6', 'engine_gt_v6_turbo', 'engine_gt_v8_turbo', 'engine_hyper_v6', 'engine_hyper_v8_na',
           'engine_hyper_v8_turbo', 'engine_i6_turbo', 'engine_v8_flatplane', 'engine_v8_na', 'engine_v12']
RATE = 22050
LOOP_SEC = 2.5
FADE_SEC = 0.25


def build_engine(src, dst):
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', src, '-ac', '1', '-ar', str(RATE), '-f', 's16le', '-'],
                         capture_output=True, check=True).stdout
    x = np.frombuffer(raw, np.int16).astype(np.float32)
    # take a steady middle stretch and crossfade its tail into its head so it loops without a click
    n = int(LOOP_SEC * RATE); f = int(FADE_SEC * RATE)
    start = (len(x) - n - f) // 2
    seg = x[start:start + n + f].copy()
    ramp = np.linspace(0, 1, f)
    seg[:f] = seg[:f] * ramp + seg[n:n + f] * (1 - ramp)
    seg = seg[:n]
    peak = np.abs(seg).max()
    seg *= min(1.0, 0.9 * 32767 / max(peak, 1))
    seg.astype(np.int16).tofile(dst)


def write_tex(img, size, path):
    img = img.convert('RGB').resize((size, size), Image.LANCZOS)
    pal, pix = quantise(img)
    with open(path, 'wb') as f:
        f.write(b'TEX1'); f.write(struct.pack('<III', size, 0, 0))   # 16-byte header (PSP alignment)
        f.write(struct.pack('<256I', *pal))
        f.write(swizzle(pix.reshape(-1), size, size).tobytes())


def build_font(dst, ttf):
    """ASCII 32..126 in a 256x128 atlas; glyph table: x, y, w, h, advance, yoffset."""
    size = 13
    font = ImageFont.truetype(ttf, size)
    W, H = 256, 128
    atlas = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(atlas)
    x, y, rowh = 1, 1, 0
    glyphs = []
    asc, desc = font.getmetrics()
    for c in range(32, 127):
        ch = chr(c)
        bb = font.getbbox(ch)
        w = max(1, bb[2] - bb[0]); h = asc + desc
        adv = int(round(font.getlength(ch)))
        if x + w + 2 > W:
            x = 1; y += rowh + 2; rowh = 0
        d.text((x - bb[0], y), ch, font=font, fill=255)
        glyphs.append((x, y, w, h, adv, bb[0]))
        x += w + 2; rowh = max(rowh, h)
    a = np.asarray(atlas, np.uint8)
    # palette: white with alpha ramp (CLUT8, index = alpha)
    pal = [(i << 24) | 0xFFFFFF for i in range(256)]
    with open(dst, 'wb') as f:
        f.write(b'FNT1'); f.write(struct.pack('<III', W, H, asc + desc))
        f.write(struct.pack('<256I', *pal))
        f.write(swizzle(a.reshape(-1), W, H).tobytes())
        for g in glyphs:
            f.write(struct.pack('<6h', *g))


def main():
    work, out, repo = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(os.path.join(out, 'audio'), exist_ok=True)
    os.makedirs(os.path.join(out, 'tex'), exist_ok=True)
    for e in ENGINES:
        src = os.path.join(work, 'src_audio', e + '.wav')
        if os.path.exists(src):
            build_engine(src, os.path.join(out, 'audio', e + '.raw'))
            print('engine', e)
    ttf = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
    if os.path.exists(ttf):
        build_font(os.path.join(out, 'font.bin'), ttf)
        print('font')
    for name, size in [('asphalt', 128), ('grass', 64), ('gravel', 64), ('kerb', 64)]:
        src = os.path.join(work, 'src_tex', name + '.png')
        if os.path.exists(src):
            write_tex(Image.open(src), size, os.path.join(out, 'tex', name + '.tex'))
            print('texture', name)
    icon = os.path.join(work, 'AppIcon.png')
    if os.path.exists(icon):
        im = Image.open(icon).convert('RGB')
        w, h = im.size
        crop = im.crop((0, int(h * 0.21), w, int(h * 0.21) + int(w * 80 / 144)))
        crop.resize((144, 80), Image.LANCZOS).save(os.path.join(repo, 'ICON0.PNG'))
        print('icon')


if __name__ == '__main__':
    main()
