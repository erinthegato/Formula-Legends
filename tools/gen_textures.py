#!/usr/bin/env python3
"""Code-made Vita textures (HD/): ground surfaces and a double-resolution font.

  python3 tools/gen_textures.py [preview_dir]

ground  HD/tex/asphalt.tex  512  the whole width of the road (u across, 14 m along v): fine aggregate,
                                 a darker rubbered band each side of the middle, sealant lines,
                                 patches, oil drops and light skid marks
        HD/tex/grass.tex    512  10 m tile: blades over soil, two-tone mowing stripes
        HD/tex/gravel.tex   512  10 m tile: pebbles of mixed size with shadows, stone colours
        HD/tex/kerb.tex     256  u across the kerb, 6 m along v: red / white halves, rumble ridges,
                                 worn paint and dirt along the outer edge
font    HD/font.bin  'FNT2': the same glyph layout as DATA/font.bin (so text is the same size on
                     screen) drawn at twice the resolution (texScale 2 in the header).

Every texture tiles in both directions (periodic noise), so mipmaps and wrap-around have no seams.
Same file formats as tools/build_assets.py (8-bit palette, PSP swizzle); the Vita turns them into
16-bit textures with mipmaps when it loads them.
"""
import os, sys, struct
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from build_models import swizzle, quantise          # noqa: E402

OUT = os.path.join(ROOT, 'HD')
TTF = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'


def noise(n, scale, seed, octaves=4, persist=0.55):
    """periodic fractal noise in 0..1 (filtered white noise in the frequency domain, so it tiles)"""
    rng = np.random.RandomState(seed)
    out = np.zeros((n, n)); amp = 1.0; tot = 0
    fy = np.fft.fftfreq(n)[:, None]; fx = np.fft.fftfreq(n)[None, :]
    f = np.sqrt(fx * fx + fy * fy)
    for o in range(octaves):
        w = rng.randn(n, n)
        cut = scale * (2 ** o) / n
        filt = np.exp(-(f / max(cut, 1e-6)) ** 2)
        layer = np.real(np.fft.ifft2(np.fft.fft2(w) * filt))
        layer = (layer - layer.mean()) / (layer.std() + 1e-9)
        out += layer * amp; tot += amp; amp *= persist
    out /= tot
    return np.clip(out * 0.25 + 0.5, 0, 1)


def to_img(a):
    return Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8))


def wrap_draw(d, n, fn, x, y, r):
    """draw a shape and its copies across the edges so the tile wraps"""
    for dx in (-n, 0, n):
        for dy in (-n, 0, n):
            if -r * 2 < x + dx < n + r * 2 and -r * 2 < y + dy < n + r * 2:
                fn(d, x + dx, y + dy)


def asphalt(n=512):
    rng = np.random.RandomState(1)
    base = 0.24 + 0.05 * (noise(n, 6, 2) - 0.5) + 0.06 * (noise(n, 60, 3, 3) - 0.5)
    grit = rng.rand(n, n)
    base += np.where(grit > 0.985, 0.18 * rng.rand(n, n), 0) - np.where(grit < 0.02, 0.05, 0)
    u = (np.arange(n) + 0.5) / n
    # two rubbered bands where most of the tyres run (u across the road), softly edged
    rub = np.exp(-((u - 0.36) / 0.09) ** 2) + np.exp(-((u - 0.64) / 0.09) ** 2)
    base -= 0.045 * rub[None, :] * (0.7 + 0.6 * noise(n, 4, 4))
    rgb = np.stack([base * 0.97, base * 0.98, base * 1.04], -1)
    img = to_img(rgb)
    d = ImageDraw.Draw(img)
    # sealant lines (dark, thin, wandering), a couple of patches and oil drops
    for k in range(5):
        x = rng.randint(0, n); y = rng.randint(0, n); pts = []
        for s in range(14):
            pts.append((x, y)); x += rng.randint(-6, 7); y += rng.randint(8, 22)
        for dx in (-n, 0, n):
            for dy in (-n, 0, n):
                d.line([(px + dx, py + dy) for px, py in pts], fill=(30, 30, 33), width=2)
    for k in range(2):
        x0 = rng.randint(0, n); y0 = rng.randint(0, n); w = rng.randint(40, 90); h = rng.randint(60, 140)
        wrap_draw(d, n, lambda dd, x, y: dd.rectangle((x, y, x + w, y + h), fill=(52, 53, 57), outline=(34, 34, 37)), x0, y0, max(w, h))
    for k in range(9):
        x = rng.randint(0, n); y = rng.randint(0, n); r = rng.randint(2, 5)
        wrap_draw(d, n, lambda dd, xx, yy: dd.ellipse((xx - r, yy - r * 1.4, xx + r, yy + r * 1.4), fill=(38, 38, 41)), x, y, r * 2)
    # faint skid marks along the road
    for k in range(4):
        x = rng.randint(n // 5, 4 * n // 5); y = rng.randint(0, n); L = rng.randint(80, 220)
        for t in range(L):
            xx = x + int(6 * np.sin(t / 40.0)); yy = (y + t) % n
            a = 0.85 + 0.1 * np.sin(t / 9.0)
            for w_ in (-4, 4):
                px = img.getpixel(((xx + w_) % n, yy))
                img.putpixel(((xx + w_) % n, yy), tuple(int(c * a) for c in px))
    return img


def grass(n=512):
    rng = np.random.RandomState(5)
    v = noise(n, 5, 6) * 0.5 + noise(n, 40, 7, 3) * 0.5
    soil = np.stack([0.20 + 0.1 * v, 0.30 + 0.12 * v, 0.08 + 0.05 * v], -1)
    img = to_img(soil)
    d = ImageDraw.Draw(img)
    for k in range(9000):
        x = rng.rand() * n; y = rng.rand() * n; L = 3 + rng.rand() * 7; ang = (rng.rand() - 0.5) * 0.9
        g = 0.55 + rng.rand() * 0.6
        col = (int(60 * g), int(125 * g), int(28 * g)) if rng.rand() > 0.08 else (int(120 * g), int(130 * g), int(50 * g))
        x2 = x + np.sin(ang) * L; y2 = y - np.cos(ang) * L
        for dx in (-n, 0, n):
            for dy in (-n, 0, n):
                if -12 < x + dx < n + 12 and -12 < y + dy < n + 12:
                    d.line((x + dx, y + dy, x2 + dx, y2 + dy), fill=col, width=1)
    a = np.asarray(img, np.float32) / 255
    # mowing stripes: two 5 m bands per 10 m tile (v direction), plus a soft brightness wobble
    yy = (np.arange(n) + 0.5) / n
    stripe = 1.0 + 0.07 * np.sign(np.sin(yy * 2 * np.pi))[:, None, None]
    a = a * stripe * (0.92 + 0.16 * noise(n, 3, 8)[..., None])
    return to_img(a)


def gravel(n=512):
    rng = np.random.RandomState(9)
    base = 0.62 + 0.08 * (noise(n, 8, 10) - 0.5)
    img = to_img(np.stack([base * 1.0, base * 0.92, base * 0.80], -1))
    d = ImageDraw.Draw(img)
    stones = []
    for k in range(5200):
        r = rng.choice([1.5, 2.2, 3.0, 4.2, 5.5], p=[0.35, 0.3, 0.2, 0.1, 0.05])
        stones.append((rng.rand() * n, rng.rand() * n, r, rng.rand()))
    stones.sort(key=lambda s: s[2])
    for x, y, r, t in stones:
        tone = 0.62 + t * 0.38
        col = (int(225 * tone), int(208 * tone), int(182 * tone)) if t > 0.15 else (int(150 * tone), int(140 * tone), int(130 * tone))
        sh = (int(120 * tone), int(108 * tone), int(92 * tone))
        hi = tuple(min(255, int(c * 1.18)) for c in col)
        def stone(dd, xx, yy):
            dd.ellipse((xx - r + 1, yy - r * 0.8 + 1.2, xx + r + 1, yy + r * 0.8 + 1.2), fill=sh)
            dd.ellipse((xx - r, yy - r * 0.8, xx + r, yy + r * 0.8), fill=col)
            dd.ellipse((xx - r * 0.5, yy - r * 0.6, xx + r * 0.1, yy - r * 0.1), fill=hi)
        wrap_draw(d, n, stone, x, y, r)
    return img


def kerb(n=256):
    rng = np.random.RandomState(13)
    img = Image.new('RGB', (n, n))
    d = ImageDraw.Draw(img)
    red = np.array([0.80, 0.10, 0.09]); white = np.array([0.93, 0.93, 0.91])
    a = np.zeros((n, n, 3))
    vv = (np.arange(n) + 0.5) / n
    uu = (np.arange(n) + 0.5) / n
    half = (vv < 0.5)[:, None, None]
    a[:] = np.where(half, red, white)
    # rumble ridges across the kerb (shading along v), brighter crests
    ridge = 0.86 + 0.18 * np.clip(np.sin(vv * 2 * np.pi * 12), -0.2, 1)[:, None, None]
    a *= ridge
    # worn paint showing concrete, grime towards the outer edge (u = 1 is the grass side)
    wear = noise(n, 10, 14, 3)
    conc = np.array([0.55, 0.55, 0.53])
    mask = (wear > 0.80)[..., None]
    a = np.where(mask, a * 0.55 + conc * 0.45, a)
    grime = np.clip((uu - 0.75) / 0.25, 0, 1)[None, :, None] * (0.35 + 0.3 * noise(n, 6, 15)[..., None])
    a = a * (1 - grime) + np.array([0.28, 0.24, 0.19]) * grime
    # dark rubber streaks on the inner edge
    rub = np.clip((0.25 - uu) / 0.25, 0, 1)[None, :, None] * 0.35 * noise(n, 5, 16)[..., None]
    a = a * (1 - rub)
    return to_img(a)


def write_tex(img, path):
    size = img.size[0]
    pal, pix = quantise(img.convert('RGB'))
    with open(path, 'wb') as f:
        f.write(b'TEX1'); f.write(struct.pack('<III', size, 0, 0))
        f.write(struct.pack('<256I', *pal))
        f.write(swizzle(pix.reshape(-1), size, size).tobytes())


def font2x(dst):
    """The same layout as build_assets.build_font (13 px DejaVu Bold, 256x128) -- the glyph table stays
    in those units -- but the atlas is drawn at 26 px into 512x256 at twice each glyph's position."""
    size = 13
    f1 = ImageFont.truetype(TTF, size); f2 = ImageFont.truetype(TTF, size * 2)
    W, H = 256, 128
    atlas = Image.new('L', (W * 2, H * 2), 0)
    d = ImageDraw.Draw(atlas)
    x, y, rowh = 1, 1, 0
    glyphs = []
    asc, desc = f1.getmetrics()
    for c in range(32, 127):
        ch = chr(c)
        bb = f1.getbbox(ch)
        w = max(1, bb[2] - bb[0]); h = asc + desc
        adv = int(round(f1.getlength(ch)))
        if x + w + 2 > W:
            x = 1; y += rowh + 2; rowh = 0
        bb2 = f2.getbbox(ch)
        d.text((x * 2 - bb2[0], y * 2), ch, font=f2, fill=255)
        glyphs.append((x, y, w, h, adv, bb[0]))
        x += w + 2; rowh = max(rowh, h)
    a = np.asarray(atlas, np.uint8)
    pal = [(i << 24) | 0xFFFFFF for i in range(256)]
    with open(dst, 'wb') as fo:
        fo.write(b'FNT2'); fo.write(struct.pack('<III', W * 2, H * 2, (asc + desc) | (2 << 16)))   # high 16 bits: texScale
        fo.write(struct.pack('<256I', *pal))
        fo.write(swizzle(a.reshape(-1), W * 2, H * 2).tobytes())
        for g in glyphs:
            fo.write(struct.pack('<6h', *g))
    return atlas


def main():
    prev = sys.argv[1] if len(sys.argv) > 1 else None
    os.makedirs(os.path.join(OUT, 'tex'), exist_ok=True)
    for name, fn in (('asphalt', asphalt), ('grass', grass), ('gravel', gravel), ('kerb', kerb)):
        img = fn()
        write_tex(img, os.path.join(OUT, 'tex', name + '.tex'))
        print('texture', name, img.size)
        if prev:
            img.save(os.path.join(prev, f'tex_{name}.png'))
    at = font2x(os.path.join(OUT, 'font.bin'))
    print('font 2x', at.size)
    if prev:
        at.save(os.path.join(prev, 'font2x.png'))


if __name__ == '__main__':
    main()
