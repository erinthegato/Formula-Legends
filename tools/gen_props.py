#!/usr/bin/env python3
"""Code-made trackside props for the PS Vita (HD/models): the four new buildings (race control,
advertising footbridge, TV camera tower, team hospitality), after the concept pictures made in Unity
(Assets/PSP3DExport/Generated/ref_*.png), plus the grandstand, trees, tyre stacks, billboard, marshal
post, fuel rig and the pit crew poses.

  python3 tools/gen_props.py [preview_dir] [names...]

Every model is built from boxes, cylinders, tubes and quads with its own painted texture atlas.
Axes: +X towards the track (the front), +Y left, +Z up, metres, ground at z = 0.
"""
import os, sys, struct, math
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, 'HD', 'models')
sys.path.insert(0, HERE)
from gen_cars import quantise, swizzle   # noqa: E402  (same palette / swizzle as the cars)

KEY = np.array([0.45, 0.35, 0.82]); KEY /= np.linalg.norm(KEY)


def font(px, bold=True):
    for f in ('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', '/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf'):
        if os.path.exists(f):
            return ImageFont.truetype(f, max(6, int(px)))
    return ImageFont.load_default()


# ------------------------------------------------------------------ texture atlas
class Atlas:
    """A square texture cut into named rectangles; painters draw into them."""

    def __init__(self, S=512, cells=8):
        self.S = S; self.n = cells
        self.img = Image.new('RGB', (S, S), (128, 128, 128))
        self.d = ImageDraw.Draw(self.img)
        self.rects = {}
        self.next = 0

    def add(self, name, w=1, h=1):
        """reserve a w x h block of cells (row by row)"""
        n = self.n
        while True:
            x, y = self.next % n, self.next // n
            if x + w <= n and y + h <= n and all((x + i, y + j) not in getattr(self, '_used', set()) for i in range(w) for j in range(h)):
                break
            self.next += 1
            if self.next >= n * n:
                raise SystemExit('atlas full')
        used = getattr(self, '_used', set())
        for i in range(w):
            for j in range(h):
                used.add((x + i, y + j))
        self._used = used
        c = self.S / n
        px = (x * c + 1, y * c + 1, (x + w) * c - 1, (y + h) * c - 1)       # pixels (inset by one)
        self.rects[name] = (px[0] / self.S, px[1] / self.S, px[2] / self.S, px[3] / self.S)
        return px

    def solid(self, name, col, w=1, h=1, noise=0.0, seed=1):
        px = self.add(name, w, h)
        self.d.rectangle(px, fill=tuple(int(c * 255) for c in col))
        if noise > 0:
            rng = np.random.RandomState(seed)
            x0, y0, x1, y1 = [int(v) for v in px]
            a = np.asarray(self.img).copy()
            blk = a[y0:y1, x0:x1].astype(np.float32)
            blk *= 1 + noise * (rng.rand(*blk.shape[:2], 1) - 0.5)
            a[y0:y1, x0:x1] = np.clip(blk, 0, 255).astype(np.uint8)
            self.img = Image.fromarray(a); self.d = ImageDraw.Draw(self.img)
        return px

    def rect(self, name):
        return self.rects[name]


def C(c):
    return tuple(int(max(0, min(1, x)) * 255) for x in c)


# ------------------------------------------------------------------ geometry kit
class Kit:
    def __init__(self):
        self.tris = []          # (P 3x3, N 3x3, UV 3x2, shade)

    def _add(self, P, UV, shade, N=None):
        P = np.array(P, float); UV = np.array(UV, float)
        fn = np.cross(P[1] - P[0], P[2] - P[0]); a = np.linalg.norm(fn)
        if a < 1e-10:
            return
        fn /= a
        self.tris.append((P, np.repeat(fn[None], 3, 0) if N is None else np.array(N, float), UV, shade))

    def quad(self, a, b, c, d, r, shade=1.0, both=False):
        """a, b along the bottom (left -> right), c, d along the top (right -> left); r = (u0, v0, u1, v1)"""
        u0, v0, u1, v1 = r
        uv = [(u0, v1), (u1, v1), (u1, v0), (u0, v0)]
        self._add([a, b, c], [uv[0], uv[1], uv[2]], shade)
        self._add([a, c, d], [uv[0], uv[2], uv[3]], shade)
        if both:
            self._add([c, b, a], [uv[2], uv[1], uv[0]], shade)
            self._add([d, c, a], [uv[3], uv[2], uv[0]], shade)

    def box(self, lo, hi, r, top=None, front=None, shade=1.0, bottom=False):
        (x0, y0, z0), (x1, y1, z1) = lo, hi
        top = top or r; front = front or r
        self.quad((x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1), front, shade)         # +X
        self.quad((x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1), r, shade)             # -X
        self.quad((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), r, shade)             # -Y
        self.quad((x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1), r, shade)             # +Y
        self.quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), top, shade)           # top
        if bottom:
            self.quad((x0, y1, z0), (x1, y1, z0), (x1, y0, z0), (x0, y0, z0), r, shade * 0.6)

    def cyl(self, a, b, r, sides, rr, caps=True, shade=1.0, r2=None):
        a = np.array(a, float); b = np.array(b, float); r2 = r if r2 is None else r2
        t = b - a; t /= np.linalg.norm(t)
        ref = np.array([0, 0, 1.0]) if abs(t[2]) < 0.9 else np.array([1.0, 0, 0])
        u = np.cross(t, ref); u /= np.linalg.norm(u); w = np.cross(t, u)
        u0, v0, u1, v1 = rr
        for k in range(sides):
            c0 = math.cos(2 * math.pi * k / sides); s0 = math.sin(2 * math.pi * k / sides)
            c1 = math.cos(2 * math.pi * (k + 1) / sides); s1 = math.sin(2 * math.pi * (k + 1) / sides)
            pa = a + r * (c0 * u + s0 * w); pb = a + r * (c1 * u + s1 * w)
            pc = b + r2 * (c1 * u + s1 * w); pd = b + r2 * (c0 * u + s0 * w)
            ua = u0 + (u1 - u0) * k / sides; ub = u0 + (u1 - u0) * (k + 1) / sides
            na = c0 * u + s0 * w; nb = c1 * u + s1 * w
            self._add([pa, pb, pc], [(ua, v1), (ub, v1), (ub, v0)], shade, [na, nb, nb])
            self._add([pa, pc, pd], [(ua, v1), (ub, v0), (ua, v0)], shade, [na, nb, na])
            if caps:
                cu, cv = (u0 + u1) / 2, (v0 + v1) / 2
                self._add([b, pd, pc], [(cu, cv), (cu, v0), (u1, cv)], shade)
                if r > 0:
                    self._add([a, pb, pa], [(cu, cv), (u1, cv), (cu, v0)], shade * 0.7)

    def tube(self, pts, r, sides, rr, shade=1.0):
        for a, b in zip(pts, pts[1:]):
            self.cyl(a, b, r, sides, rr, caps=False, shade=shade)

    def bar(self, a, b, w, rr, shade=1.0):
        """a square bar (cheap tube): 4 sides"""
        self.cyl(a, b, w, 4, rr, caps=False, shade=shade)

    def arrays(self):
        m = len(self.tris)
        P = np.zeros((m, 3, 3)); UV = np.zeros((m, 3, 2)); LT = np.zeros((m, 3))
        for i, (p, n, uv, shade) in enumerate(self.tris):
            P[i] = p; UV[i] = uv
            light = 0.50 + 0.30 * (n[:, 2] * 0.5 + 0.5) + 0.30 * np.clip(n @ KEY, 0, 1)
            ao = 0.72 + 0.28 * np.clip(p[:, 2] / 0.6, 0, 1)
            LT[i] = np.clip(light * ao * shade, 0, 1)
        return P, np.clip(UV, 0.001, 0.999), LT


def write_emm(path, lods, emx=None, shared_bbox=None):
    """lods: list of (Kit, Image or None, texture size). Positions stored -1..1 of the bounding box."""
    P0 = np.concatenate([k.arrays()[0].reshape(-1, 3) for k, _, _ in lods]) if shared_bbox is None else shared_bbox
    mn, mx = P0.min(0), P0.max(0)
    centre = (mn + mx) / 2; scale = np.maximum((mx - mn) / 2, 1e-4)
    with open(path, 'wb') as f:
        f.write(b'EMM1'); f.write(struct.pack('<I', len(lods)))
        f.write(struct.pack('<3f', *scale)); f.write(struct.pack('<3f', *centre))
        for kit, tex, ts in lods:
            P, UV, LT = kit.arrays()
            vc = len(P) * 3
            f.write(struct.pack('<IIIf', vc, ts if tex is not None else 0, 0, 0.0))
            if tex is not None:
                t = tex.resize((ts, ts), Image.LANCZOS) if tex.size[0] != ts else tex
                pal, pix = quantise(t)
                f.write(struct.pack('<256I', *pal)); f.write(swizzle(pix.reshape(-1), ts, ts).tobytes())
            q = np.clip(np.round((P.reshape(-1, 3) - centre) / scale * 32767), -32767, 32767).astype(np.int16)
            uv16 = np.clip(np.round(UV.reshape(-1, 2) * 32767), 0, 65535).astype(np.uint16)
            g = np.clip(np.round(LT.reshape(-1) * 255), 0, 255).astype(np.uint32)
            rec = np.zeros(vc, dtype=[('u', '<u2'), ('v', '<u2'), ('c', '<u4'), ('x', '<i2'), ('y', '<i2'), ('z', '<i2'), ('pad', '<i2')])
            rec['u'] = uv16[:, 0]; rec['v'] = uv16[:, 1]; rec['c'] = 0xFF000000 | (g << 16) | (g << 8) | g
            rec['x'] = q[:, 0]; rec['y'] = q[:, 1]; rec['z'] = q[:, 2]
            f.write(rec.tobytes())
        if emx is not None:
            f.write(b'EMX1'); f.write(struct.pack('<4fI', *emx))
    return [len(k.tris) for k, _, _ in lods], [round(float(x), 2) for x in (mx - mn)]


# ------------------------------------------------------------------ shared paint helpers
def paint_windows(a, name, cols, rows, frame, glass, w=1, h=1, mullion=0.12, light=None):
    x0, y0, x1, y1 = a.add(name, w, h)
    a.d.rectangle((x0, y0, x1, y1), fill=C(frame))
    cw = (x1 - x0) / cols; rh = (y1 - y0) / rows
    for i in range(cols):
        for j in range(rows):
            gx0 = x0 + i * cw + cw * mullion; gx1 = x0 + (i + 1) * cw - cw * mullion
            gy0 = y0 + j * rh + rh * mullion; gy1 = y0 + (j + 1) * rh - rh * mullion
            top = np.array(glass) * 1.25; bot = np.array(glass) * 0.8
            for k in range(int(gy1 - gy0)):
                t = k / max(1, gy1 - gy0)
                a.d.line((gx0, gy0 + k, gx1, gy0 + k), fill=C(top * (1 - t) + bot * t))
            a.d.line((gx0 + (gx1 - gx0) * 0.15, gy1 - 2, gx0 + (gx1 - gx0) * 0.55, gy0 + 2), fill=C(np.array(glass) * 1.6), width=2)   # reflection streak


def panel_lines(a, name, base, line, n, w=1, h=1, horizontal=True, noise=0.04, seed=3):
    px = a.solid(name, base, w, h, noise=noise, seed=seed)
    x0, y0, x1, y1 = px
    for k in range(1, n):
        if horizontal:
            y = y0 + (y1 - y0) * k / n; a.d.line((x0, y, x1, y), fill=C(line), width=1)
        else:
            x = x0 + (x1 - x0) * k / n; a.d.line((x, y0, x, y1), fill=C(line), width=1)


def text_panel(a, name, text, bg, fg, w=2, h=1, size=0.6, border=None):
    x0, y0, x1, y1 = a.add(name, w, h)
    a.d.rectangle((x0, y0, x1, y1), fill=C(bg))
    if border:
        a.d.rectangle((x0, y0, x1, y1), outline=C(border), width=3)
    f = font((y1 - y0) * size)
    tw = a.d.textlength(text, font=f)
    if tw > (x1 - x0) * 0.92:
        f = font((y1 - y0) * size * (x1 - x0) * 0.92 / tw); tw = a.d.textlength(text, font=f)
    bb = a.d.textbbox((0, 0), text, font=f)
    a.d.text(((x0 + x1) / 2 - tw / 2, (y0 + y1) / 2 - (bb[3] + bb[1]) / 2), text, fill=C(fg), font=f)


# ------------------------------------------------------------------ 1. race control tower (ref_race_control_b.png)
def race_control(lod):
    a = Atlas(512, 8)
    a.solid('concrete', (0.84, 0.85, 0.86), noise=0.06)
    panel_lines(a, 'concrete_panels', (0.82, 0.83, 0.84), (0.62, 0.63, 0.65), 4, 2, 2, horizontal=False)
    a.solid('dark', (0.30, 0.31, 0.33), noise=0.05)
    paint_windows(a, 'win3', 3, 1, (0.30, 0.31, 0.33), (0.38, 0.48, 0.56), 2, 1)
    paint_windows(a, 'glass', 4, 1, (0.20, 0.21, 0.23), (0.42, 0.55, 0.64), 2, 1, mullion=0.05)
    a.solid('roof', (0.62, 0.63, 0.64), noise=0.05)
    a.solid('rail', (0.75, 0.76, 0.78))
    a.solid('red', (1.0, 0.15, 0.10))
    # timing board: RACE CONTROL, a big clock, three lap times (fictional), blue LED text on black
    x0, y0, x1, y1 = a.add('board', 3, 4)
    a.d.rectangle((x0, y0, x1, y1), fill=(10, 12, 16))
    led = (120, 200, 255); white = (225, 230, 240)
    rows = [('RACE CONTROL', 0.16, led), ('15:42:03', 0.24, white), ('1.  01:24.55', 0.14, white), ('2.  01:24.89', 0.14, white), ('3.  01:25.12', 0.14, white)]
    y = y0 + 6
    for text, frac, col in rows:
        hgt = (y1 - y0) * frac
        f = font(hgt * 0.8)
        a.d.rectangle((x0 + 4, y, x1 - 4, y + hgt), outline=(40, 44, 54), width=2)
        tw = a.d.textlength(text, font=f)
        a.d.text(((x0 + x1) / 2 - tw / 2, y + hgt * 0.08), text, fill=col, font=f)
        y += hgt + 6
    k = Kit()
    W = 10.0; D = 10.0
    R = a.rect
    # three storey base: concrete with window bands, dark plinth
    k.box((-D / 2, -W / 2, 0), (D / 2, W / 2, 2.4), R('dark'))
    for fl in range(3):
        z0 = 2.4 + fl * 3.0
        k.box((-D / 2, -W / 2, z0), (D / 2, W / 2, z0 + 0.8), R('concrete'))
        for (lo, hi, rr) in (((-D / 2, -W / 2, z0 + 0.8), (D / 2, W / 2, z0 + 3.0), R('win3')),):
            k.box(lo, hi, rr, top=R('roof'))
    k.box((-D / 2 - 0.05, -W / 2 - 0.05, 11.4), (D / 2 + 0.05, W / 2 + 0.05, 11.7), R('concrete'))
    # timing board on the track side (+X), framed in concrete
    k.box((D / 2, -2.4, 2.6), (D / 2 + 0.35, 2.4, 10.8), R('concrete'))
    k.quad((D / 2 + 0.36, -2.1, 3.0), (D / 2 + 0.36, 2.1, 3.0), (D / 2 + 0.36, 2.1, 10.4), (D / 2 + 0.36, -2.1, 10.4), R('board'), shade=1.25)
    # control room: glass walls leaning outwards, roof slab overhanging, balcony with a rail
    z0, z1 = 11.7, 14.6
    lean = 0.9
    for side in range(4):
        ang = side * math.pi / 2
        c, s_ = math.cos(ang), math.sin(ang)
        def P(u, v, z, out):
            x = (D / 2 - 0.8 + out) ; y = u
            return (x * c - y * s_, x * s_ + y * c, z)
        for t in np.linspace(-1, 1, 3)[:-1]:
            u0 = t * (W / 2 - 0.8); u1 = (t + 1.0) * (W / 2 - 0.8)
            k.quad(P(u0, 0, z0, 0), P(u1, 0, z0, 0), P(u1, 0, z1, lean), P(u0, 0, z1, lean), R('glass'), shade=1.1)
    k.box((-D / 2 - 0.3, -W / 2 - 0.3, z1), (D / 2 + 0.6, W / 2 + 0.3, z1 + 0.45), R('roof'))
    if lod == 0:
        for side in (-1, 1):
            k.bar((D / 2 + 0.3, side * (W / 2 + 0.2), 12.6), (-D / 2 - 0.2, side * (W / 2 + 0.2), 12.6), 0.03, R('rail'))
        k.bar((D / 2 + 0.3, -W / 2 - 0.2, 12.6), (D / 2 + 0.3, W / 2 + 0.2, 12.6), 0.03, R('rail'))
        for t in np.linspace(-W / 2 - 0.2, W / 2 + 0.2, 9):
            k.bar((D / 2 + 0.3, t, 11.7), (D / 2 + 0.3, t, 12.6), 0.02, R('rail'))
        # stair core on the back, antenna mast with dishes and a red light
        k.box((-D / 2 - 1.4, 1.5, 0), (-D / 2, 4.5, 15.6), R('concrete_panels'), top=R('roof'))
        k.tube([(-2.0, 2.0, 15.05), (-2.0, 2.0, 21.0)], 0.08, 6, R('rail'))
        for zz in (16.0, 18.0, 20.0):
            k.bar((-2.4, 2.0, zz), (-1.6, 2.0, zz), 0.03, R('rail'))
        k.cyl((-1.5, 2.0, 17.0), (-1.38, 2.0, 17.0), 0.45, 10, R('rail'), r2=0.05)
        k.cyl((-2.0, 1.4, 18.5), (-2.0, 1.28, 18.5), 0.35, 10, R('rail'), r2=0.05)
        k.cyl((-2.0, 2.0, 21.0), (-2.0, 2.0, 21.3), 0.12, 8, R('red'), shade=1.6)
    return k, a.img


# ------------------------------------------------------------------ 2. advertising footbridge (ref_ad_bridge.png)
BANNERS = [('AURORA RACING', (0.10, 0.22, 0.55), (1, 1, 1)), ('NEXUS TYRES', (0.85, 0.12, 0.14), (1, 1, 1)),
           ('VELOCITY', (0.98, 0.78, 0.10), (0.10, 0.15, 0.40)), ('ORBIT', (0.10, 0.55, 0.30), (1, 1, 1))]


def ad_bridge(lod):
    a = Atlas(512, 8)
    a.solid('steel', (0.70, 0.72, 0.74), noise=0.05)
    a.solid('dark', (0.25, 0.26, 0.28))
    a.solid('deck', (0.45, 0.46, 0.48), noise=0.08)
    for i, (t, bg, fg) in enumerate(BANNERS):
        text_panel(a, f'ban{i}', t, bg, fg, 4, 1, size=0.55, border=(0.9, 0.9, 0.9))
    k = Kit(); R = a.rect
    L = 30.0; half = L / 2
    zd = 5.6; zt = 8.4; dw = 1.2          # deck height, top of the truss, half the deck width
    # two lattice towers at the ends (4 legs, braced)
    for sx in (-1, 1):
        cx = sx * (half - 1.0)
        legs = [(cx + dx, dy, 0) for dx in (-0.8, 0.8) for dy in (-dw, dw)]
        for (x, y, _) in legs:
            k.bar((x, y, 0), (x, y, zt), 0.09, R('steel'))
        for z in np.arange(1.2, zd, 1.4) if lod == 0 else [zd - 0.3]:
            for (x0, y0, _), (x1, y1, _) in ((legs[0], legs[1]), (legs[2], legs[3]), (legs[0], legs[2]), (legs[1], legs[3])):
                k.bar((x0, y0, z), (x1, y1, z), 0.05, R('steel'))
                if lod == 0:
                    k.bar((x0, y0, z), (x1, y1, z + 1.4), 0.035, R('steel'))
        if lod == 0:
            # zig-zag stairs outside each tower
            ox = cx + sx * 1.6
            for f, (z0, z1) in enumerate(((0, 1.9), (1.9, 3.8), (3.8, zd))):
                y0, y1 = (-dw, dw) if f % 2 == 0 else (dw, -dw)
                for st in range(9):
                    t0 = st / 9; zz = z0 + (z1 - z0) * t0; yy = y0 + (y1 - y0) * t0
                    k.box((ox - 0.45, yy - 0.12, zz), (ox + 0.45, yy + 0.12, zz + 0.05), R('dark'))
                k.bar((ox + sx * 0.45, y0, z0 + 0.9), (ox + sx * 0.45, y1, z1 + 0.9), 0.025, R('steel'))
                k.box((ox - 0.5, y1 - 0.5 * np.sign(y1 - y0) - 0.3, z1 - 0.05), (ox + 0.5, y1 + 0.3, z1), R('dark'))
    # deck and the truss box with the banners on both long sides (facing the cars, +Y and -Y)
    k.box((-half, -dw, zd - 0.25), (half, dw, zd), R('deck'))
    for sy in (-1, 1):
        y = sy * (dw + 0.02)
        k.bar((-half, sy * dw, zd), (half, sy * dw, zd), 0.08, R('steel'))
        k.bar((-half, sy * dw, zt), (half, sy * dw, zt), 0.08, R('steel'))
        n = len(BANNERS)
        seg = (L - 1.0) / n
        for i in range(n):
            x0 = -half + 0.5 + i * seg; x1 = x0 + seg - 0.15
            # text reads left to right for a driver looking at this side
            if sy > 0:
                k.quad((x1, y, zd + 0.15), (x0, y, zd + 0.15), (x0, y, zt - 0.15), (x1, y, zt - 0.15), R(f'ban{(i + 1) % n}'), shade=1.25)
            else:
                k.quad((x0, y, zd + 0.15), (x1, y, zd + 0.15), (x1, y, zt - 0.15), (x0, y, zt - 0.15), R(f'ban{i}'), shade=1.25)
    k.box((-half, -dw, zt), (half, dw, zt + 0.08), R('steel'))
    if lod == 0:
        for x in np.linspace(-half, half, 11):
            for sy in (-1, 1):
                k.bar((x, sy * dw, zd), (x, sy * dw, zt), 0.04, R('steel'))
    return k, a.img


# ------------------------------------------------------------------ 3. TV camera tower (ref_tv_tower.png)
def person_simple(k, R, at, facing, suit, skin, dark, crouch=0.0):
    """a seated / standing figure made of boxes (cameraman)"""
    x, y, z = at
    c, s = math.cos(facing), math.sin(facing)
    def T(px, py, pz):
        return (x + px * c - py * s, y + px * s + py * c, z + pz)
    def box(lo, hi, r):
        pts = [T(*lo), T(*hi)]
        k.box((min(pts[0][0], pts[1][0]), min(pts[0][1], pts[1][1]), lo[2] + z), (max(pts[0][0], pts[1][0]), max(pts[0][1], pts[1][1]), hi[2] + z), r)
    for sy in (-0.1, 0.1):
        box((-0.08, sy - 0.07, 0), (0.08, sy + 0.07, 0.85 - crouch), R(dark))
    box((-0.13, -0.2, 0.85 - crouch), (0.13, 0.2, 1.45 - crouch), R(suit))
    box((-0.1, -0.1, 1.45 - crouch), (0.12, 0.1, 1.72 - crouch), R(skin))
    box((0.0, -0.26, 1.15 - crouch), (0.35, -0.18, 1.35 - crouch), R(suit))
    box((0.0, 0.18, 1.15 - crouch), (0.35, 0.26, 1.35 - crouch), R(suit))


def tv_tower(lod):
    a = Atlas(256, 8)
    a.solid('steel', (0.74, 0.76, 0.78), noise=0.05)
    a.solid('deck', (0.40, 0.42, 0.44), noise=0.06)
    a.solid('tarp', (0.22, 0.24, 0.26))
    a.solid('camera', (0.12, 0.12, 0.13))
    a.solid('lens', (0.05, 0.06, 0.08))
    a.solid('suit', (0.20, 0.28, 0.20))
    a.solid('skin', (0.80, 0.62, 0.50))
    a.solid('jeans', (0.18, 0.22, 0.35))
    a.solid('logo', (0.95, 0.45, 0.05))
    k = Kit(); R = a.rect
    w = 0.8; H = 5.8
    legs = [(dx, dy) for dx in (-w, w) for dy in (-w, w)]
    for (x, y) in legs:
        k.bar((x, y, 0), (x, y, H + 1.1), 0.05, R('steel'))
    levels = np.arange(0.0, H + 0.01, 1.45)
    for i, z in enumerate(levels):
        ring = [(-w, -w), (w, -w), (w, w), (-w, w), (-w, -w)]
        for p0, p1 in zip(ring, ring[1:]):
            k.bar((p0[0], p0[1], z), (p1[0], p1[1], z), 0.03, R('steel'))
            if lod == 0 and z < H:
                (k.bar((p0[0], p0[1], z), (p1[0], p1[1], z + 1.45), 0.022, R('steel')) if i % 2 == 0 else
                 k.bar((p1[0], p1[1], z), (p0[0], p0[1], z + 1.45), 0.022, R('steel')))
    # ladder on the back
    if lod == 0:
        for sy in (-0.22, 0.22):
            k.bar((-w - 0.15, sy, 0), (-w - 0.15, sy, H + 1.0), 0.02, R('steel'))
        for z in np.arange(0.3, H, 0.3):
            k.bar((-w - 0.15, -0.22, z), (-w - 0.15, 0.22, z), 0.012, R('steel'))
    # platform, rail, canopy
    k.box((-w - 0.2, -w - 0.2, H), (w + 0.2, w + 0.2, H + 0.08), R('deck'))
    for z in (H + 0.55, H + 1.05):
        ring = [(-w - 0.2, -w - 0.2), (w + 0.2, -w - 0.2), (w + 0.2, w + 0.2), (-w - 0.2, w + 0.2), (-w - 0.2, -w - 0.2)]
        for p0, p1 in zip(ring, ring[1:]):
            k.bar((p0[0], p0[1], z), (p1[0], p1[1], z), 0.02, R('steel'))
    for (x, y) in legs:
        k.bar((x, y, H + 1.1), (x, y, H + 2.3), 0.025, R('steel'))
    k.box((-w - 0.4, -w - 0.4, H + 2.3), (w + 0.4, w + 0.4, H + 2.36), R('tarp'))
    k.box((-w - 0.4, -w - 0.4, H + 2.1), (w + 0.4, w + 0.4, H + 2.3), R('tarp'), front=R('logo'))
    # camera on a tripod, cameraman behind it
    k.bar((0.25, 0, H + 0.08), (0.25, 0, H + 1.25), 0.03, R('camera'))
    for ang in (0, 2.1, 4.2):
        k.bar((0.25 + 0.3 * math.cos(ang), 0.3 * math.sin(ang), H + 0.08), (0.25, 0, H + 0.9), 0.015, R('camera'))
    k.box((0.0, -0.13, H + 1.25), (0.65, 0.13, H + 1.55), R('camera'))
    k.cyl((0.65, 0, H + 1.40), (1.05, 0, H + 1.40), 0.09, 10, R('lens'), r2=0.11)
    person_simple(k, R, (-0.35, 0.0, H + 0.08), 0.0, 'suit', 'skin', 'jeans')
    return k, a.img


# ------------------------------------------------------------------ 4. team hospitality (ref_hospitality.png)
def hospitality(lod):
    a = Atlas(512, 8)
    panel_lines(a, 'cladding', (0.24, 0.25, 0.27), (0.15, 0.15, 0.17), 6, 2, 2)
    x0, y0, x1, y1 = a.add('stripe', 2, 1)
    a.d.rectangle((x0, y0, x1, y1), fill=C((0.24, 0.25, 0.27)))
    a.d.rectangle((x0, (y0 + y1) / 2 - 3, x1, (y0 + y1) / 2 + 3), fill=C((1.0, 0.45, 0.05)))
    paint_windows(a, 'front_glass', 5, 2, (0.18, 0.19, 0.20), (0.35, 0.47, 0.56), 4, 2, mullion=0.04)
    paint_windows(a, 'side_glass', 2, 1, (0.18, 0.19, 0.20), (0.35, 0.47, 0.56), 2, 1, mullion=0.06)
    a.solid('roof', (0.55, 0.56, 0.58), noise=0.05)
    a.solid('rail', (0.80, 0.82, 0.85))
    a.solid('canvas', (0.98, 0.93, 0.72))
    a.solid('pole', (0.60, 0.60, 0.62))
    text_panel(a, 'sign', 'TEAM HOSPITALITY', (0.96, 0.96, 0.96), (0.15, 0.16, 0.18), 4, 1, size=0.45)
    k = Kit(); R = a.rect
    D, W, H = 11.0, 15.0, 7.2
    k.box((-D / 2, -W / 2, 0), (D / 2, W / 2, H), R('cladding'), top=R('roof'), front=R('cladding'))
    # big glass front (+X, two storeys), side windows, orange stripes along the slab lines
    k.quad((D / 2 + 0.02, -W / 2 + 1.0, 0.2), (D / 2 + 0.02, W / 2 - 1.0, 0.2), (D / 2 + 0.02, W / 2 - 1.0, H - 0.6), (D / 2 + 0.02, -W / 2 + 1.0, H - 0.6), R('front_glass'), shade=1.15)
    for sy in (-1, 1):
        for (z0, z1) in ((0.8, 3.0), (4.2, 6.4)):
            if sy > 0:
                k.quad((D / 2 - 1.0, sy * (W / 2 + 0.02), z0), (-D / 2 + 1.0, sy * (W / 2 + 0.02), z0), (-D / 2 + 1.0, sy * (W / 2 + 0.02), z1), (D / 2 - 1.0, sy * (W / 2 + 0.02), z1), R('side_glass'), shade=1.1)
            else:
                k.quad((-D / 2 + 1.0, sy * (W / 2 + 0.02), z0), (D / 2 - 1.0, sy * (W / 2 + 0.02), z0), (D / 2 - 1.0, sy * (W / 2 + 0.02), z1), (-D / 2 + 1.0, sy * (W / 2 + 0.02), z1), R('side_glass'), shade=1.1)
    for z in (3.55, H - 0.1):
        k.box((-D / 2 - 0.05, -W / 2 - 0.05, z - 0.25), (D / 2 + 0.06, W / 2 + 0.05, z + 0.05), R('stripe'), top=R('roof'))
    k.quad((D / 2 + 0.05, -3.0, 3.7), (D / 2 + 0.05, 3.0, 3.7), (D / 2 + 0.05, 3.0, 4.4), (D / 2 + 0.05, -3.0, 4.4), R('sign'), shade=1.2)
    # roof terrace: glass rail, parasols
    if lod == 0:
        for sy in (-1, 1):
            k.bar((D / 2 - 0.3, sy * (W / 2 - 0.3), H + 1.0), (-D / 2 + 0.3, sy * (W / 2 - 0.3), H + 1.0), 0.03, R('rail'))
        k.bar((D / 2 - 0.3, -W / 2 + 0.3, H + 1.0), (D / 2 - 0.3, W / 2 - 0.3, H + 1.0), 0.03, R('rail'))
        for t in np.linspace(-W / 2 + 0.3, W / 2 - 0.3, 11):
            k.bar((D / 2 - 0.3, t, H), (D / 2 - 0.3, t, H + 1.0), 0.02, R('rail'))
        for (px, py) in ((2.5, -4.5), (2.5, 0.0), (2.5, 4.5), (-2.0, -2.5), (-2.0, 2.5)):
            k.bar((px, py, H), (px, py, H + 2.4), 0.04, R('pole'))
            k.cyl((px, py, H + 2.0), (px, py, H + 2.7), 1.6, 10, R('canvas'), r2=0.05, caps=False)
            k.cyl((px, py, H + 2.7), (px, py, H + 2.0), 0.05, 10, R('canvas'), r2=1.6, caps=False, shade=0.6)
    else:
        for (px, py) in ((2.5, -4.5), (2.5, 4.5)):
            k.cyl((px, py, H + 2.0), (px, py, H + 2.7), 1.6, 6, R('canvas'), r2=0.05, caps=False)
    return k, a.img


# ------------------------------------------------------------------ pit crew (after ref_pit_crew.png)
# One jointed figure; every pose has the same parts in the same order, so the game can blend two poses
# vertex by vertex (Renderer::drawPerson). All 16 poses share one bounding box (same scale / offset).
CREW_BOX = np.array([[-1.2, -1.2, 0.0], [1.2, 1.2, 2.4]])


def crew_atlas():
    # 256 px (was 128): room for seams, a team badge and a helmet stripe
    a = Atlas(256, 8)
    navy = (0.10, 0.14, 0.32); orange = (1.0, 0.45, 0.05)
    x0, y0, x1, y1 = a.solid('suit', navy, noise=0.06)
    seam = C(np.array(navy) * 0.6)
    for yy in range(int(y0) + 6, int(y1), 9):                      # quilted fire-suit stitching
        a.d.line((x0, yy, x1, yy), fill=seam, width=1)
    a.d.rectangle((x0 + 9, y0 + 9, x0 + 19, y0 + 16), fill=C((0.95, 0.95, 0.93)))   # small team badge
    a.d.rectangle((x0 + 11, y0 + 11, x0 + 17, y0 + 14), fill=C(orange))
    x0, y0, x1, y1 = a.add('suit_stripe')
    a.d.rectangle((x0, y0, x1, y1), fill=C(navy))
    m = (x0 + x1) / 2
    a.d.rectangle((m - 4, y0, m + 4, y1), fill=C(orange))
    a.d.line((m - 6, y0, m - 6, y1), fill=C((0.92, 0.92, 0.90)), width=1)
    a.d.line((m + 6, y0, m + 6, y1), fill=C((0.92, 0.92, 0.90)), width=1)
    x0, y0, x1, y1 = a.solid('helmet', (0.12, 0.20, 0.55), noise=0.03)
    a.d.rectangle((x0, (y0 + y1) / 2 - 3, x1, (y0 + y1) / 2 + 3), fill=C(orange))
    a.d.line((x0, y0 + 3, x1, y0 + 3), fill=C((0.35, 0.45, 0.80)), width=2)       # shine
    x0, y0, x1, y1 = a.solid('visor', (0.05, 0.06, 0.08))
    a.d.line((x0 + 4, y0 + 6, x1 - 8, y0 + 4), fill=C((0.30, 0.34, 0.42)), width=2)  # reflection
    x0, y0, x1, y1 = a.solid('glove', (0.06, 0.06, 0.07), noise=0.08)
    a.d.rectangle((x0, y0, x1, y0 + 5), fill=C(orange))                          # cuff
    x0, y0, x1, y1 = a.solid('boot', (0.08, 0.08, 0.09), noise=0.08)
    a.d.rectangle((x0, y1 - 5, x1, y1), fill=C((0.20, 0.20, 0.22)))              # sole
    a.solid('gun', (0.35, 0.36, 0.38), noise=0.08)
    a.solid('white', (0.92, 0.92, 0.90), noise=0.03)
    return a


def rot(axis, ang):
    c, s = math.cos(ang), math.sin(ang)
    if axis == 'x': return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
    if axis == 'y': return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def figure(pose, R, gun=False):
    """pose: dict of joint angles (radians). The figure faces +X, feet on z = 0 (pose['drop'] lowers the hips)."""
    k = Kit()
    g = pose.get
    hip_z = 0.95 - g('drop', 0.0)
    pelvis = np.array([0.0, 0.0, hip_z])
    Rt = rot('y', g('lean', 0.0)) @ rot('z', g('twist', 0.0))           # torso: lean forward (+) and twist
    def seg(a, b, ra, rb, sides, rr):
        k.cyl(a, b, ra, sides, rr, caps=True, r2=rb)
    def boxat(c, half, Rm, rr):
        # an oriented box: 8 corners through the rotation, faces as quads
        hx, hy, hz = half
        P = {}
        for i, sx in enumerate((-1, 1)):
            for j, sy in enumerate((-1, 1)):
                for l, sz in enumerate((-1, 1)):
                    P[(i, j, l)] = c + Rm @ np.array([sx * hx, sy * hy, sz * hz])
        q = lambda a_, b_, c_, d_: k.quad(P[a_], P[b_], P[c_], P[d_], rr)
        q((1, 0, 0), (1, 1, 0), (1, 1, 1), (1, 0, 1)); q((0, 1, 0), (0, 0, 0), (0, 0, 1), (0, 1, 1))
        q((0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1)); q((1, 1, 0), (0, 1, 0), (0, 1, 1), (1, 1, 1))
        q((0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)); q((0, 1, 0), (1, 1, 0), (1, 0, 0), (0, 0, 0))
    # legs: hip -> knee -> ankle, boots
    for side, sy in (('l', 1), ('r', -1)):
        hip = pelvis + np.array([0, sy * 0.11, -0.02])
        Rh = rot('y', -g('hip_' + side, 0.0)) @ rot('x', sy * g('splay', 0.0))
        knee = hip + Rh @ np.array([0, 0, -0.45])
        Rk = Rh @ rot('y', g('knee_' + side, 0.0))
        ankle = knee + Rk @ np.array([0, 0, -0.44])
        seg(hip, knee, 0.085, 0.07, 8, R('suit'))
        seg(knee - Rh @ np.array([0, 0, 0.02]), knee + Rk @ np.array([0, 0, -0.06]), 0.075, 0.068, 8, R('suit'))   # knee pad
        seg(knee, ankle, 0.065, 0.05, 8, R('suit_stripe'))
        boxat(ankle + np.array([0.06, 0, -0.035]), (0.13, 0.055, 0.045), rot('z', 0), R('boot'))
    # torso, neck, helmet
    chest = pelvis + Rt @ np.array([0, 0, 0.55])
    seg(pelvis - Rt @ np.array([0, 0, 0.04]), chest, 0.15, 0.18, 10, R('suit'))
    seg(pelvis - Rt @ np.array([0, 0, 0.06]), pelvis + Rt @ np.array([0, 0, 0.02]), 0.165, 0.165, 10, R('glove'))   # belt
    seg(chest + Rt @ np.array([0, -0.24, -0.04]), chest + Rt @ np.array([0, 0.24, -0.04]), 0.075, 0.075, 8, R('suit'))   # shoulders
    neck = chest + Rt @ np.array([0, 0, 0.08])
    head = neck + Rt @ rot('y', g('nod', 0.0)) @ np.array([0.01, 0, 0.14])
    seg(chest, neck, 0.06, 0.06, 6, R('suit'))
    Rhd = Rt @ rot('y', g('nod', 0.0))
    seg(head - Rhd @ np.array([0, 0, 0.13]), head + Rhd @ np.array([0, 0, 0.08]), 0.125, 0.112, 12, R('helmet'))
    seg(head + Rhd @ np.array([0, 0, 0.08]), head + Rhd @ np.array([0, 0, 0.125]), 0.112, 0.05, 12, R('helmet'))   # rounded crown
    boxat(head + Rhd @ np.array([0.10, 0, 0.0]), (0.035, 0.09, 0.045), Rhd, R('visor'))
    boxat(head + Rhd @ np.array([0.095, 0, -0.085]), (0.04, 0.075, 0.03), Rhd, R('helmet'))   # chin bar
    # arms: shoulder -> elbow -> wrist, gloves
    for side, sy in (('l', 1), ('r', -1)):
        sh = chest + Rt @ np.array([0, sy * 0.21, -0.03])
        Rs = Rt @ rot('y', -g('sh_' + side, 0.0)) @ rot('x', sy * g('abd_' + side, 0.15))
        elbow = sh + Rs @ np.array([0, 0, -0.30])
        Re = Rs @ rot('y', -g('el_' + side, 0.0))
        wrist = elbow + Re @ np.array([0, 0, -0.28])
        seg(sh, elbow, 0.06, 0.05, 8, R('suit'))
        seg(elbow, wrist, 0.048, 0.04, 8, R('suit_stripe'))
        boxat(wrist + Re @ np.array([0, 0, -0.05]), (0.04, 0.035, 0.05), Re, R('glove'))
    if gun:
        # the wheel gun held in both hands in front of the chest
        gc = chest + Rt @ rot('y', g('gun_pitch', 0.9)) @ np.array([0.40, 0, -0.10])
        Rg = Rt @ rot('y', g('gun_pitch', 0.9))
        boxat(gc, (0.10, 0.05, 0.07), Rg, R('gun'))
        k.cyl(gc + Rg @ np.array([0.10, 0, 0]), gc + Rg @ np.array([0.26, 0, 0]), 0.035, 6, R('gun'))
    return k


def crew_poses():
    """4 motions x 4 poses (the game plays them back and forth)"""
    P = {}
    # idle: standing, weight shifting, arms loose
    P['idle'] = [dict(sh_l=0.10 + 0.05 * f, sh_r=0.05 - 0.03 * f, el_l=0.25, el_r=0.30, hip_l=0.03 * f, knee_l=0.06 * f, twist=0.03 * (f - 1.5), nod=0.05)
                 for f in range(4)]
    # wheel gun: crouched at the wheel, gun forward, small jolts
    P['wheelgun'] = [dict(drop=0.38, lean=0.55, hip_l=1.35, hip_r=1.20, knee_l=-2.0, knee_r=-1.75, splay=0.25,
                          sh_l=1.05 + 0.05 * (f % 2), sh_r=1.10 + 0.05 * ((f + 1) % 2), el_l=0.9, el_r=0.85, abd_l=0.25, abd_r=0.25,
                          gun_pitch=0.75 + 0.04 * (f % 2), nod=-0.25) for f in range(4)]
    # push: leaning hard into the car, arms straight forward, striding
    P['push'] = [dict(lean=0.75, drop=0.06, hip_l=0.5 + 0.35 * math.sin(f * math.pi / 3), hip_r=0.5 - 0.35 * math.sin(f * math.pi / 3),
                      knee_l=-0.4 - 0.2 * (f % 2), knee_r=-0.4 - 0.2 * ((f + 1) % 2), sh_l=1.75, sh_r=1.75, el_l=0.1, el_r=0.1, nod=-0.6)
                 for f in range(4)]
    # celebrate: arms up, jumping
    P['celebrate'] = [dict(drop=-0.08 * (f in (1, 2)), sh_l=2.9 - 0.25 * (f % 2), sh_r=2.7 + 0.25 * (f % 2), el_l=0.2, el_r=0.4,
                           abd_l=0.35, abd_r=0.35, hip_l=0.15 * (f % 2), knee_l=-0.3 * (f % 2), nod=0.15) for f in range(4)]
    return P


# the game turns each motion by Renderer::drawPerson FACE[] (idle pi, wheel gun pi/2, push 0, celebrate pi),
# so the poses are stored turned back by that much: they then face +X like every caller expects
FACE = {'idle': math.pi, 'wheelgun': math.pi / 2, 'push': 0.0, 'celebrate': math.pi}


def turned(k, ang):
    if ang == 0:
        return k
    Rz = rot('z', -ang)
    out = Kit()
    for P_, N_, UV_, sh in k.tris:
        out.tris.append(((Rz @ P_.T).T, (Rz @ N_.T).T, UV_, sh))
    return out


def build_crew(prev):
    a = crew_atlas()
    poses = crew_poses()
    first = True
    for anim, frames in poses.items():
        for f, pose in enumerate(frames):
            k = turned(figure(pose, a.rect, gun=(anim == 'wheelgun')), FACE[anim])
            n, size = write_emm(os.path.join(OUT, f'crew_{anim}_{f}.emm'), [(k, a.img if first else None, 256)], shared_bbox=CREW_BOX)
            if first:
                print(f'crew poses  triangles {n[0]} (idle) ...')
            first = False
    n, _ = write_emm(os.path.join(OUT, 'pit_crew.emm'), [(turned(figure(poses['idle'][0], a.rect), 0), a.img, 256)], shared_bbox=CREW_BOX)
    if prev:
        a.img.save(os.path.join(prev, 'atlas_crew.png'))


# ------------------------------------------------------------------ grandstand, trees, tyres, billboard, marshal post, fuel rig
def grandstand(lod):
    a = Atlas(512, 8)
    x0, y0, x1, y1 = a.add('crowd', 4, 4)
    rng = np.random.RandomState(7)
    a.d.rectangle((x0, y0, x1, y1), fill=(70, 72, 78))
    cols = [(230, 60, 50), (240, 240, 235), (40, 90, 200), (250, 200, 40), (30, 30, 35), (60, 160, 80), (230, 120, 40)]
    for row in range(10):
        yy = y0 + (y1 - y0) * (row + 0.5) / 10
        for i in range(40):
            if rng.rand() < 0.82:
                xx = x0 + (x1 - x0) * (i + 0.5 + (rng.rand() - 0.5) * 0.3) / 40
                c = cols[rng.randint(len(cols))]
                a.d.ellipse((xx - 3, yy - 6, xx + 3, yy + 1), fill=c)
                a.d.ellipse((xx - 2, yy - 10, xx + 2, yy - 6), fill=(225, 180, 150))
    a.solid('concrete', (0.78, 0.79, 0.80), noise=0.06)
    a.solid('roof', (0.86, 0.87, 0.88))
    a.solid('steel', (0.55, 0.57, 0.60))
    a.solid('back', (0.30, 0.33, 0.40), noise=0.04)
    text_panel(a, 'fascia', 'ENDURANCE MOTOR RACING', (0.10, 0.12, 0.25), (1.0, 0.55, 0.10), 4, 1, size=0.5)
    k = Kit(); R = a.rect
    L, D = 30.0, 9.0
    tiers = 6 if lod == 0 else 1
    # stepped seating (+X faces the track): each step's riser carries a strip of the crowd picture
    for t in range(tiers):
        f0, f1 = t / tiers, (t + 1) / tiers
        x0_, x1_ = D / 2 - f0 * D * 0.8, D / 2 - f1 * D * 0.8
        z0, z1 = 0.8 + f0 * 6.0, 0.8 + f1 * 6.0
        u0, v0, u1, v1 = R('crowd')
        rr = (u0, v0 + (v1 - v0) * (1 - f1), u1, v0 + (v1 - v0) * (1 - f0))
        if lod == 0:     # real steps: a riser with the crowd, then a concrete tread
            xm = x0_ + (x1_ - x0_) * 0.25
            k.quad((x0_, -L / 2, z0), (x0_, L / 2, z0), (xm, L / 2, z1), (xm, -L / 2, z1), rr, shade=1.05)
            k.quad((xm, -L / 2, z1), (xm, L / 2, z1), (x1_, L / 2, z1), (x1_, -L / 2, z1), R('concrete'), shade=1.1)
        else:
            k.quad((x0_, -L / 2, z0), (x0_, L / 2, z0), (x1_, L / 2, z1), (x1_, -L / 2, z1), rr, shade=1.05)
    if lod == 0:
        # two stair aisles cutting up through the seats, and a railing along the front
        for ya in (-L / 6, L / 6):
            for t in range(tiers):
                f0, f1 = t / tiers, (t + 1) / tiers
                xa, xb = D / 2 - f0 * D * 0.8, D / 2 - f1 * D * 0.8
                k.box((xb, ya - 0.6, 0.8 + f0 * 6.0), (xa + 0.02, ya + 0.6, 0.8 + f1 * 6.0 + 0.02), R('concrete'))
        k.bar((D / 2 - 0.1, -L / 2, 1.8), (D / 2 - 0.1, L / 2, 1.8), 0.05, R('steel'))
        for y in np.linspace(-L / 2 + 0.5, L / 2 - 0.5, 9):
            k.bar((D / 2 - 0.1, y, 0.8), (D / 2 - 0.1, y, 1.8), 0.04, R('steel'))
    k.box((D / 2 - 0.3, -L / 2, 0), (D / 2, L / 2, 0.8), R('concrete'))
    k.box((-D / 2 + 0.6, -L / 2, 0), (-D / 2 + 1.2, L / 2, 7.8), R('back'))
    for sy in (-1, 1):
        k.quad((D / 2, sy * L / 2, 0), (-D / 2 + 1.2, sy * L / 2, 0), (-D / 2 + 1.2, sy * L / 2, 7.8), (D / 2 - 0.2, sy * L / 2, 0.8), R('concrete'), both=True)
    # roof on pillars with a sponsor fascia along its front edge
    k.box((-D / 2 + 0.6, -L / 2 - 0.5, 9.2), (D / 2 + 1.2, L / 2 + 0.5, 9.45), R('roof'))
    k.quad((D / 2 + 1.21, -L / 2 + 0.5, 8.85), (D / 2 + 1.21, L / 2 - 0.5, 8.85), (D / 2 + 1.21, L / 2 - 0.5, 9.45), (D / 2 + 1.21, -L / 2 + 0.5, 9.45), R('fascia'), shade=1.2)
    for y in (np.linspace(-L / 2 + 1, L / 2 - 1, 5) if lod == 0 else (-L / 2 + 1, L / 2 - 1)):
        k.bar((-D / 2 + 0.9, y, 0), (-D / 2 + 0.9, y, 9.2), 0.15, R('steel'))
        if lod == 0:
            k.bar((-D / 2 + 0.9, y, 9.1), (D / 2 + 1.0, y, 9.2), 0.10, R('steel'))
    return k, a.img


def leaves(a, name, base, seed):
    px = a.solid(name, base, 2, 2)
    rng = np.random.RandomState(seed)
    x0, y0, x1, y1 = [int(v) for v in px]
    k = a.S / 128
    # dark gaps first, then lit leaf clumps on top: reads as depth from a distance
    for _ in range(int(120 * k * k)):
        x = rng.randint(x0, x1); y = rng.randint(y0, y1); r = rng.randint(2, 4) * k
        a.d.ellipse((x - r, y - r, x + r, y + r), fill=C(np.array(base) * (0.45 + rng.rand() * 0.2)))
    for _ in range(int(260 * k * k)):
        x = rng.randint(x0, x1); y = rng.randint(y0, y1); r = rng.randint(2, 5) * (0.6 + 0.4 * k)
        f = 0.75 + rng.rand() * 0.6
        a.d.ellipse((x - r, y - r * 0.8, x + r, y + r * 0.8), fill=C(np.array(base) * f))


def tree_pine(lod):
    a = Atlas(256, 4)
    leaves(a, 'needles', (0.13, 0.30, 0.16), 11)
    a.solid('bark', (0.30, 0.22, 0.15), noise=0.15)
    k = Kit(); R = a.rect
    sides = 8 if lod == 0 else 5
    k.cyl((0, 0, 0), (0, 0, 3.0), 0.22, 5, R('bark'), caps=False, r2=0.16)
    layers = [(1.8, 5.2, 2.7), (4.0, 7.6, 2.3), (6.2, 10.0, 1.9), (8.4, 12.2, 1.4), (10.6, 14.0, 0.9)] if lod == 0 else [(2.0, 8.0, 2.6), (6.0, 14.0, 1.8)]
    for z0, z1, r in layers:
        k.cyl((0, 0, z0), (0, 0, z1), r, sides, R('needles'), caps=lod == 0, r2=0.04)
    return k, a.img


def tree_round(lod):
    a = Atlas(256, 4)
    leaves(a, 'leaves', (0.22, 0.42, 0.15), 12)
    a.solid('bark', (0.33, 0.25, 0.18), noise=0.15)
    k = Kit(); R = a.rect
    k.cyl((0, 0, 0), (0, 0, 3.4), 0.25, 5, R('bark'), caps=False, r2=0.18)
    def blob(c, r, rings, sides):
        cx, cy, cz = c
        pts = []
        for i in range(rings + 1):
            th = math.pi * i / rings
            pts.append([(cx + r[0] * math.sin(th) * math.cos(2 * math.pi * j / sides), cy + r[1] * math.sin(th) * math.sin(2 * math.pi * j / sides), cz - r[2] * math.cos(th)) for j in range(sides + 1)])
        u0, v0, u1, v1 = R('leaves')
        for i in range(rings):
            for j in range(sides):
                p00, p01, p10, p11 = pts[i][j], pts[i][j + 1], pts[i + 1][j], pts[i + 1][j + 1]
                k.quad(p00, p01, p11, p10, (u0 + (u1 - u0) * j / sides, v0 + (v1 - v0) * i / rings, u0 + (u1 - u0) * (j + 1) / sides, v0 + (v1 - v0) * (i + 1) / rings))
    if lod == 0:
        blob((0, 0, 5.6), (2.7, 2.7, 2.4), 4, 8)
        blob((0.9, 0.6, 7.2), (1.8, 1.8, 1.6), 3, 7)
        blob((-1.0, -0.7, 6.6), (1.6, 1.5, 1.4), 3, 6)
    else:
        blob((0, 0, 6.0), (2.8, 2.8, 2.8), 3, 5)
    return k, a.img


def tyre_stack(lod):
    a = Atlas(128, 4)
    a.solid('rubber', (0.08, 0.08, 0.09), noise=0.1)
    a.solid('band', (0.92, 0.92, 0.90))
    a.solid('band_red', (0.85, 0.10, 0.08))
    k = Kit(); R = a.rect
    sides = 10 if lod == 0 else 6
    n = 5 if lod == 0 else 2
    h = 1.2 / n
    for i in range(n):
        k.cyl((0, 0, i * h), (0, 0, (i + 1) * h - 0.01), 0.33, sides, R('rubber'), caps=(i == n - 1))
    if lod == 0:
        k.cyl((0, 0, 0.55), (0, 0, 0.75), 0.335, sides, R('band_red' if True else 'band'), caps=False)
    return k, a.img


def billboard(lod):
    a = Atlas(512, 8)
    text_panel(a, 'ad1', 'NEXUS TYRES', (0.85, 0.12, 0.14), (1, 1, 1), 8, 2, size=0.5, border=(1, 1, 1))
    text_panel(a, 'ad2', 'AURORA RACING', (0.10, 0.22, 0.55), (1, 1, 1), 8, 2, size=0.5, border=(1, 1, 1))
    a.solid('steel', (0.55, 0.57, 0.60))
    a.solid('back', (0.25, 0.26, 0.28))
    k = Kit(); R = a.rect
    L = 8.0
    for x in (-L / 2 + 0.6, L / 2 - 0.6):
        k.bar((x, -0.1, 0), (x, -0.1, 3.2), 0.08, R('steel'))
    # +Y faces the track (the model's X runs along the track); the back carries the other advert
    k.quad((L / 2, 0.0, 1.0), (-L / 2, 0.0, 1.0), (-L / 2, 0.0, 3.2), (L / 2, 0.0, 3.2), R('ad1'), shade=1.15)
    k.quad((-L / 2, -0.2, 1.0), (L / 2, -0.2, 1.0), (L / 2, -0.2, 3.2), (-L / 2, -0.2, 3.2), R('ad2'), shade=1.15)
    k.box((-L / 2, -0.2, 3.2), (L / 2, 0.0, 3.3), R('back'))
    k.box((-L / 2, -0.2, 0.95), (L / 2, 0.0, 1.0), R('back'))
    return k, a.img


def marshal_post(lod):
    a = Atlas(256, 8)
    a.solid('wall', (0.92, 0.92, 0.90), noise=0.05)
    a.solid('roof', (0.30, 0.32, 0.35))
    paint_windows(a, 'win', 2, 1, (0.92, 0.92, 0.90), (0.35, 0.45, 0.55))
    a.solid('orange', (1.0, 0.45, 0.05))
    a.solid('steel', (0.60, 0.62, 0.65))
    a.solid('flagy', (1.0, 0.85, 0.1))
    a.solid('skin', (0.80, 0.62, 0.50))
    a.solid('dark', (0.15, 0.15, 0.17))
    text_panel(a, 'num', 'POST 7', (1, 1, 1), (0.1, 0.1, 0.12), 2, 1, size=0.6)
    k = Kit(); R = a.rect
    k.box((-0.9, -1.0, 0), (0.9, 1.0, 2.3), R('wall'), front=R('win'))
    k.box((-1.1, -1.2, 2.3), (1.2, 1.2, 2.45), R('roof'))
    k.quad((0.91, -0.6, 1.9), (0.91, 0.6, 1.9), (0.91, 0.6, 2.25), (0.91, -0.6, 2.25), R('num'), shade=1.2)
    if lod == 0:
        k.bar((1.3, 0.9, 0), (1.3, 0.9, 3.4), 0.03, R('steel'))
        k.quad((1.3, 0.9, 2.8), (1.3, 1.6, 2.8), (1.3, 1.6, 3.3), (1.3, 0.9, 3.3), R('flagy'), both=True)
        person_simple(k, R, (1.6, -0.6, 0.0), 0.0, 'orange', 'skin', 'dark')
    return k, a.img


def fuel_rig(lod):
    a = Atlas(128, 4)
    a.solid('body', (0.80, 0.10, 0.08), noise=0.05)
    a.solid('steel', (0.65, 0.67, 0.70))
    a.solid('hose', (0.08, 0.08, 0.09))
    a.solid('white', (0.95, 0.95, 0.93))
    k = Kit(); R = a.rect
    k.box((-0.25, -0.25, 0), (0.25, 0.25, 0.2), R('steel'))
    k.cyl((0, 0, 0.2), (0, 0, 1.8), 0.18, 10, R('body'))
    k.cyl((0, 0, 1.0), (0, 0, 1.12), 0.185, 10, R('white'), caps=False)
    k.bar((0, 0, 1.8), (0, 0, 2.2), 0.03, R('steel'))
    k.tube([(0, 0, 2.2), (0.3, 0, 2.1), (0.6, 0, 1.5), (0.7, 0.1, 0.9)], 0.035, 6, R('hose'))
    k.box((0.62, 0.02, 0.75), (0.80, 0.18, 0.95), R('steel'))
    return k, a.img


# (tree_pine, tree_round, tyre_stack, marshal_post, fuel_rig and tv_tower are made in Blender now:
#  tools/blender/trackside.py -> tools/gen_trackside.py; their old code versions are kept above for reference)
OTHER = {'grandstand': (grandstand, (512, 128)), 'billboard': (billboard, (512, 128))}


def EXTRA(only, prev):
    for name, (fn, sizes) in OTHER.items():
        if only and name not in only:
            continue
        k0, img = fn(0)
        lods = [(k0, img, sizes[0])]
        if sizes[1]:
            k1, _ = fn(1); lods.append((k1, img, sizes[1]))
        n, size = write_emm(os.path.join(OUT, name + '.emm'), lods)
        print(f'{name:14s} triangles {n}  size {size} m')
        if prev:
            img.save(os.path.join(prev, f'atlas_{name}.png'))
    if not only or 'crew' in only:
        build_crew(prev)


BUILDERS = {'race_control': (race_control, (512, 128)), 'ad_bridge': (ad_bridge, (512, 128)),
            'hospitality': (hospitality, (512, 128))}


def main():
    args = [x for x in sys.argv[1:]]
    prev = args.pop(0) if args and os.path.isdir(args[0]) else None
    only = set(args)
    os.makedirs(OUT, exist_ok=True)
    for name, (fn, sizes) in BUILDERS.items():
        if only and name not in only:
            continue
        k0, img = fn(0); k1, _ = fn(1)
        n, size = write_emm(os.path.join(OUT, name + '.emm'), [(k0, img, sizes[0]), (k1, img, sizes[1])])
        print(f'{name:14s} triangles {n}  size {size} m')
        if prev:
            img.save(os.path.join(prev, f'atlas_{name}.png'))
    if 'EXTRA' in globals():
        EXTRA(only, prev)


if __name__ == '__main__':
    main()
