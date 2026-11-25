#!/usr/bin/env python3
"""Procedural cars for Endurance Motor Racing 3D.

Builds every car model from code (no source meshes):
  * Hypercars: low, sleek, enclosed single-seat prototypes with a bubble canopy, high wheel
    arches, an engine-cover fin and a full-width rear wing.
  * LMGT3s: blockier (but rounded) two-seat coupes with a separate cabin, splitter, mirrors and
    a swan-neck rear wing.
  * The cockpit interiors (roll cages, dash, right-hand-drive GT3 with a stripped passenger side),
    steering wheels and the road wheel.

Output: DATA/models/car_<id>.emm, cockpit_hypercar.emm, cockpit_gt3.emm, steering_hyper.emm,
steering_gt3.emm, race_wheel.emm.   Usage: python3 tools/gen_cars.py [preview_dir]

Axes: +X forward, +Y left, +Z up, metres, origin on the ground under the middle of the car.
"""
import os, re, struct, sys, zlib
import numpy as np
from PIL import Image, ImageDraw
from scipy.interpolate import PchipInterpolator

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'DATA', 'models')
# --hd: the PS Vita's code-made high-detail set (HD/models): a denser level of detail on top, 512 px
# liveries with panel lines, lights and (fictional) sponsor decals, and each car's proportions taken
# from its Unity AI reference model (tools/ref_shapes.json, made by tools/measure_refs.py)
HD = '--hd' in sys.argv
if HD:
    OUT = os.path.join(ROOT, 'HD', 'models')
    sys.argv.remove('--hd')

# texture layout (fractions of the square texture): side view, top view, colour swatches
SIDE_V = (0.0, 0.44)
TOP_V = (0.44, 0.88)
SW_V = (0.89, 1.0)
ZMAX = 1.40          # side view covers z 0..ZMAX
YMAX = 1.06          # top view covers y -YMAX..YMAX
SW = dict(glass=0, white=1, red=2, carbon=3, cage=4, second=5, panel=6, paint=7,
          orange=8, green=9, blue=10, yellow=11, screen=12, grey=13, rubber=14, caliper=15)   # 8..15: HD only
HD_SWATCH = dict(orange=(1.0, 0.42, 0.05), green=(0.15, 0.85, 0.30), blue=(0.15, 0.40, 0.95), yellow=(0.98, 0.80, 0.10),
                 screen=(0.03, 0.05, 0.09), grey=(0.30, 0.31, 0.33), rubber=(0.07, 0.07, 0.075), caliper=(0.85, 0.08, 0.06))
SW_DIV = 8          # swatch cells across the texture (HD: 16, the right half holds the decal slots)


def sstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def densify(xs, k):
    """insert k-1 evenly spaced points between neighbours of a list of stations"""
    out = []
    for a, b in zip(xs, xs[1:]):
        out += [a + (b - a) * i / k for i in range(k)]
    return out + [xs[-1]]


def curve(pts):
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    o = np.argsort(xs)
    f = PchipInterpolator(np.array(xs)[o], np.array(ys)[o], extrapolate=True)
    lo, hi = min(xs), max(xs)
    return lambda x: float(f(min(max(x, lo), hi)))


class Mesh:
    """Triangle soup with per-triangle group ('body' or 'glass') and UVs in texture space."""

    def __init__(self, length):
        self.tris = []      # (p 3x3, n 3x3, uv 3x2 or None, group)
        self.L = length

    def part(self, V, F, uv='proj', group='body', flat=False, glass_faces=None, shade=1.0):
        V = np.asarray(V, float); F = np.asarray(F, int)
        if len(F) == 0:
            return
        c = V.mean(0)
        P = V[F]                                    # m x 3 x 3
        fn = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
        flip = (fn * (P.mean(1) - c)).sum(1) < 0
        fn[flip] *= -1
        area = np.linalg.norm(fn, axis=1)
        keep = area > 1e-9
        vn = np.zeros_like(V)
        for k in range(3):
            np.add.at(vn, F[:, k], fn)
        vn /= np.maximum(np.linalg.norm(vn, axis=1, keepdims=True), 1e-9)
        fnu = fn / np.maximum(area[:, None], 1e-12)
        for i in range(len(F)):
            if not keep[i]:
                continue
            n = np.repeat(fnu[i][None], 3, 0) if flat else vn[F[i]]
            g = group
            u = uv
            if glass_faces is not None and i in glass_faces:
                g, u = 'glass', 'glass'
            self.tris.append((P[i], n, u, g, fnu[i], shade))

    # ---- primitives
    def box(self, lo, hi, uv='carbon', group='body', shade=1.0, taper=None):
        lo = np.array(lo, float); hi = np.array(hi, float)
        V = np.array([[x, y, z] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])])
        if taper is not None:
            V = taper(V)
        F = [[0, 1, 3], [0, 3, 2], [4, 6, 7], [4, 7, 5], [0, 4, 5], [0, 5, 1], [2, 3, 7], [2, 7, 6], [0, 2, 6], [0, 6, 4], [1, 5, 7], [1, 7, 3]]
        self.part(V, F, uv, group, flat=True, shade=shade)

    def decal(self, corners, uvs, shade=1.0):
        """a quad with its own texture coordinates (a sticker: the decal slots in the swatch row)"""
        P = np.array(corners, float); U = np.array(uvs, float)
        fn = np.cross(P[1] - P[0], P[2] - P[0]); fn /= max(np.linalg.norm(fn), 1e-9)
        for t in ((0, 1, 2), (0, 2, 3)):
            self.tris.append((P[list(t)], np.repeat(fn[None], 3, 0), U[list(t)], 'body', fn, shade))

    def quad(self, a, b, c, d, uv='carbon', group='body', shade=1.0):
        self.part(np.array([a, b, c, d], float), [[0, 1, 2], [0, 2, 3]], uv, group, flat=True, shade=shade)

    def tube(self, pts, r, sides=4, uv='cage', shade=1.0):
        """A bar through the given points (square or hexagonal section)."""
        pts = [np.array(p, float) for p in pts]
        rings = []
        for i, p in enumerate(pts):
            a = pts[max(i - 1, 0)]; b = pts[min(i + 1, len(pts) - 1)]
            t = b - a; t /= np.linalg.norm(t)
            ref = np.array([0, 0, 1.0]) if abs(t[2]) < 0.9 else np.array([1.0, 0, 0])
            u = np.cross(t, ref); u /= np.linalg.norm(u)
            w = np.cross(t, u)
            rings.append([p + r * (np.cos(k * 2 * np.pi / sides + 0.785) * u + np.sin(k * 2 * np.pi / sides + 0.785) * w) for k in range(sides)])
        V = np.array([q for ring in rings for q in ring])
        F = []
        for i in range(len(pts) - 1):
            for k in range(sides):
                a = i * sides + k; b = i * sides + (k + 1) % sides
                F += [[a, b, b + sides], [a, b + sides, a + sides]]
        # the outward test in part() uses the part centre, which fails for bent tubes: light them flat-ish
        self.part(V, F, uv, 'body', flat=True, shade=shade)

    def sheet(self, rings, uv='carbon', shade=1.0, flat=False):
        """an open surface through rows of points (rows not joined end to start)"""
        n = len(rings[0])
        V = np.array([q for r in rings for q in r], float)
        F = []
        for i in range(len(rings) - 1):
            for k in range(n - 1):
                a = i * n + k
                F += [[a, a + 1, a + 1 + n], [a, a + 1 + n, a + n]]
        self.part(V, F, uv, 'body', flat=flat, shade=shade)

    def cyl(self, a, b, r, sides=10, uv='grey', shade=1.0, caps=True):
        """a closed cylinder from a to b"""
        a = np.array(a, float); b = np.array(b, float)
        t = b - a; t /= np.linalg.norm(t)
        ref = np.array([0, 0, 1.0]) if abs(t[2]) < 0.9 else np.array([1.0, 0, 0])
        u = np.cross(t, ref); u /= np.linalg.norm(u); w = np.cross(t, u)
        ring = lambda c: [c + r * (np.cos(k * 2 * np.pi / sides) * u + np.sin(k * 2 * np.pi / sides) * w) for k in range(sides)]
        self.loft([np.array(ring(a)), np.array(ring(b))], uv=uv, cap_front=caps, cap_back=caps)
        if shade != 1.0:
            for i in range(1, 2 * sides + (2 * (sides - 2) if caps else 0) + 1):
                p_, n_, u_, g_, f_, _ = self.tris[-i]; self.tris[-i] = (p_, n_, u_, g_, f_, shade)

    def loft(self, rings, uv='proj', group='body', cap_front=True, cap_back=True, glass=None):
        """rings: list of (n x 3) closed rings, same n. glass(i, k) -> True for glass quads."""
        n = len(rings[0])
        V = np.array([p for r in rings for p in r])
        F = []; gl = set()
        for i in range(len(rings) - 1):
            for k in range(n):
                a = i * n + k; b = i * n + (k + 1) % n
                if glass is not None and glass(i, k):
                    gl.add(len(F)); gl.add(len(F) + 1)
                F += [[a, b, b + n], [a, b + n, a + n]]
        for end, on in ((0, cap_front), (len(rings) - 1, cap_back)):
            if on:
                base = end * n
                for k in range(1, n - 1):
                    F.append([base, base + k, base + k + 1])
        self.part(V, F, uv, group, glass_faces=gl)

    # ---- output
    def arrays(self):
        """-> positions (m,3,3), uv (m,3,2), light (m,3), glass mask (m)"""
        m = len(self.tris)
        P = np.zeros((m, 3, 3)); UV = np.zeros((m, 3, 2)); LT = np.zeros((m, 3)); G = np.zeros(m, bool)
        key = np.array([0.45, 0.35, 0.82]); key /= np.linalg.norm(key)
        for i, (p, n, uv, g, fn, shade) in enumerate(self.tris):
            P[i] = p
            G[i] = g == 'glass'
            if isinstance(uv, str) and uv == 'proj':
                if abs(fn[2]) > 0.62:
                    UV[i, :, 0] = 0.5 - p[:, 0] / (self.L + 0.2)
                    UV[i, :, 1] = TOP_V[0] + (0.5 - p[:, 1] / (2 * YMAX)) * (TOP_V[1] - TOP_V[0])
                else:
                    UV[i, :, 0] = 0.5 - p[:, 0] / (self.L + 0.2)
                    UV[i, :, 1] = SIDE_V[0] + (1 - np.clip(p[:, 2], 0, ZMAX) / ZMAX) * (SIDE_V[1] - SIDE_V[0])
            else:
                if isinstance(uv, np.ndarray):
                    UV[i] = uv
                    LT[i] = np.clip((0.55 + 0.3 * np.clip(n @ key, 0, 1) + 0.2 * (n[:, 2] * 0.5 + 0.5)) * shade, 0, 1)
                    continue
                k = SW[uv]
                UV[i, :, 0] = (k + 0.5) / SW_DIV
                UV[i, :, 1] = (SW_V[0] + SW_V[1]) / 2
            light = 0.50 + 0.30 * (n[:, 2] * 0.5 + 0.5) + 0.30 * np.clip(n @ key, 0, 1)
            ao = 0.62 + 0.38 * np.clip(p[:, 2] / 0.28, 0, 1)        # darker towards the ground
            if g == 'glass':
                light = 0.55 + 0.6 * np.clip(n[:, 2], 0, 1) + 0.25 * np.clip(n @ key, 0, 1)
                ao = 1
            LT[i] = np.clip(light * ao * shade, 0, 1.0)
        UV = np.clip(UV, 0.002, 0.998)
        return P, UV, LT, G


def swizzle(pix, w, h):
    out = np.zeros(w * h, np.uint8)
    src = pix.reshape(h, w)
    o = 0
    for by in range(h // 8):
        for bx in range(w // 16):
            out[o:o + 128] = src[by * 8:by * 8 + 8, bx * 16:bx * 16 + 16].reshape(-1)
            o += 128
    return out


def quantise(img):
    q = img.convert('RGB').quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:768]
    pal += [0] * (768 - len(pal))
    return [0xFF000000 | (pal[i * 3 + 2] << 16) | (pal[i * 3 + 1] << 8) | pal[i * 3] for i in range(256)], np.asarray(q, np.uint8)


def write_emm(path, lods, meta=(0.0, 0.0, 0.0), emx=None, share_tex=False):
    """lods: list of (Mesh, texture image, texture size). Glass triangles go last in each LOD.
    share_tex: levels 1+ painted from the same image as level 0 store no texture and use level 0's
    (the Vita's mipmaps shrink it in the distance) -- saves memory with no visible change."""
    P0 = lods[0][0].arrays()[0].reshape(-1, 3)
    mn, mx = P0.min(0), P0.max(0)
    centre = (mn + mx) / 2
    scale = np.maximum((mx - mn) / 2, 1e-4)
    with open(path, 'wb') as f:
        f.write(b'EMM1'); f.write(struct.pack('<I', len(lods)))
        f.write(struct.pack('<3f', *scale)); f.write(struct.pack('<3f', *centre))
        for li, (mesh, tex, ts) in enumerate(lods):
            P, UV, LT, G = mesh.arrays()
            order = np.argsort(G, kind='stable')
            P, UV, LT, G = P[order], UV[order], LT[order], G[order]
            vc = len(P) * 3
            shared = share_tex and li > 0 and tex is lods[0][1]
            # header (16 bytes): vertex count, texture size (0: level 0's), glass vertex count, one extra
            # number (LOD 0: cabin position x, LOD 1: number panel y, LOD 2: number panel x)
            f.write(struct.pack('<IIIf', vc, 0 if shared else ts, int(G.sum()) * 3, meta[li] if li < 3 else 0.0))
            if not shared:
                t = tex.resize((ts, ts), Image.LANCZOS) if tex.size[0] != ts else tex
                pal, pix = quantise(t)
                f.write(struct.pack('<256I', *pal))
                f.write(swizzle(pix.reshape(-1), ts, ts).tobytes())
            q = np.clip(np.round((P.reshape(-1, 3) - centre) / scale * 32767), -32767, 32767).astype(np.int16)
            uv16 = np.clip(np.round(UV.reshape(-1, 2) * 32767), 0, 65535).astype(np.uint16)
            g = np.clip(np.round(LT.reshape(-1) * 255), 0, 255).astype(np.uint32)
            rec = np.zeros(vc, dtype=[('u', '<u2'), ('v', '<u2'), ('c', '<u4'), ('x', '<i2'), ('y', '<i2'), ('z', '<i2'), ('pad', '<i2')])
            rec['u'] = uv16[:, 0]; rec['v'] = uv16[:, 1]; rec['c'] = 0xFF000000 | (g << 16) | (g << 8) | g
            rec['x'] = q[:, 0]; rec['y'] = q[:, 1]; rec['z'] = q[:, 2]
            f.write(rec.tobytes())
        if emx is not None:      # 'EMX1' axleF, axleR, wheelY, wheelR, flags (bit 1: the Vita's new instrument layout)
            f.write(b'EMX1'); f.write(struct.pack('<4fI', *emx))
    return [len(l[0].tris) for l in lods]


# ------------------------------------------------------------------ liveries
def rgb(c):
    return tuple(int(max(0, min(1, x)) * 255) for x in c)


def mix(a, b, t):
    return tuple(a[i] * (1 - t) + b[i] * t for i in range(3))


class Paint:
    """Draws the livery texture: a side view, a top view and the swatch row."""

    def __init__(self, L, S=256):
        self.S = S; self.L = L
        self.img = Image.new('RGB', (S, S), (0, 0, 0))
        self.d = ImageDraw.Draw(self.img)

    def sx(self, x):
        return (0.5 - x / (self.L + 0.2)) * self.S

    def sz(self, z):
        return (SIDE_V[0] + (1 - z / ZMAX) * (SIDE_V[1] - SIDE_V[0])) * self.S

    def ty(self, y):
        return (TOP_V[0] + (0.5 - y / (2 * YMAX)) * (TOP_V[1] - TOP_V[0])) * self.S

    def side_poly(self, pts, col):
        self.d.polygon([(self.sx(x), self.sz(z)) for x, z in pts], fill=rgb(col))

    def top_poly(self, pts, col, mirror=True):
        self.d.polygon([(self.sx(x), self.ty(y)) for x, y in pts], fill=rgb(col))
        if mirror:
            self.d.polygon([(self.sx(x), self.ty(-y)) for x, y in pts], fill=rgb(col))

    def side_disc(self, x, z, r, col):
        kx = self.S / (self.L + 0.2); kz = (SIDE_V[1] - SIDE_V[0]) * self.S / ZMAX
        self.d.ellipse([self.sx(x) - r * kx, self.sz(z) - r * kz, self.sx(x) + r * kx, self.sz(z) + r * kz], fill=rgb(col))

    def base(self, col):
        S = self.S
        self.d.rectangle([0, 0, S, TOP_V[1] * S + 2], fill=rgb(col))

    def swatches(self, cols):
        S = self.S
        for name, k in SW.items():
            if k * SW_DIV // 16 >= SW_DIV and name not in cols:
                continue
            col = cols.get(name, HD_SWATCH.get(name, (0.5, 0.5, 0.5)))
            self.d.rectangle([k * S / SW_DIV, TOP_V[1] * S + 2, (k + 1) * S / SW_DIV, S], fill=rgb(col))


STYLES = ['stripe', 'twotone', 'slash', 'band', 'split', 'arrow']


def livery(p, cdef, spec, hyper):
    L = spec['L']; xf = L / 2; xr = -L / 2
    A = cdef['colour']; B = spec['second']; C = spec['accent']
    p.base(A)
    st = spec['style']
    if st == 'stripe':        # twin stripes over the top, a low side stripe
        p.top_poly([(xf, 0.10), (xf, 0.26), (xr, 0.26), (xr, 0.10)], B)
        p.side_poly([(xf, 0.30), (xf, 0.40), (xr, 0.52), (xr, 0.42)], B)
    elif st == 'twotone':     # darker lower body, accent pinstripe
        p.side_poly([(xf, 0), (xf, 0.36), (xr, 0.50), (xr, 0)], B)
        p.side_poly([(xf, 0.36), (xf, 0.40), (xr, 0.54), (xr, 0.50)], C)
        p.top_poly([(xf, 0.62), (xf, YMAX), (xr, YMAX), (xr, 0.70)], B)
    elif st == 'slash':       # diagonal slashes behind the front wheels
        for k in range(3):
            x0 = 0.9 - k * 0.55
            p.side_poly([(x0, 0), (x0 + 0.25, 0), (x0 - 0.55, ZMAX), (x0 - 0.80, ZMAX)], B if k != 1 else C)
            p.top_poly([(x0 - 0.5, 0.45), (x0 - 0.25, 0.45), (x0 - 0.75, YMAX), (x0 - 1.0, YMAX)], B if k != 1 else C)
    elif st == 'band':        # a wide band round the nose and one round the tail
        p.side_poly([(xf, 0), (xf, ZMAX), (xf - 0.75, ZMAX), (xf - 1.05, 0)], B)
        p.top_poly([(xf, 0), (xf, YMAX), (xf - 0.75, YMAX), (xf - 0.75, 0)], B)
        p.side_poly([(xr, 0), (xr, ZMAX), (xr + 0.6, ZMAX), (xr + 0.9, 0)], C)
        p.top_poly([(xr, 0), (xr, YMAX), (xr + 0.6, YMAX), (xr + 0.6, 0)], C)
    elif st == 'split':       # front half one colour, rear the other, with a wedge between
        p.side_poly([(-0.2, 0), (0.5, ZMAX), (xr, ZMAX), (xr, 0)], B)
        p.top_poly([(0.1, 0), (0.6, YMAX), (xr, YMAX), (xr, 0)], B)
        p.side_poly([(-0.2, 0), (0.5, ZMAX), (0.62, ZMAX), (-0.08, 0)], C)
    elif st == 'arrow':       # a pointed centre panel running back from the nose
        p.top_poly([(xf, 0), (xf - 0.4, 0.42), (xr, 0.30), (xr, 0)], B)
        p.side_poly([(xf, 0.18), (xf - 0.5, 0.34), (xr, 0.44), (xr, 0.30)], C)
    carbon = (0.07, 0.07, 0.08)
    # sills and wheel arches
    p.side_poly([(xf, 0), (xf, 0.11), (xr, 0.11), (xr, 0)], carbon)
    for ax in spec['axles']:
        p.side_disc(ax, 0.355, 0.43, (0.03, 0.03, 0.035))
    # class number panel (the digits are drawn by the game) with three position lights
    panel = (0.85, 0.07, 0.07) if hyper else (0.95, 0.45, 0.05)
    px, pz = spec['panel']
    p.side_poly([(px - 0.26, pz - 0.17), (px + 0.26, pz - 0.17), (px + 0.26, pz + 0.17), (px - 0.26, pz + 0.17)], panel)
    for k in range(3):
        p.side_disc(px + 0.36 + k * 0.09, pz + 0.12, 0.03, (0.2, 1.0, 0.3))
    tx = spec['top_panel']
    p.top_poly([(tx - 0.22, -0.20), (tx + 0.22, -0.20), (tx + 0.22, 0.20), (tx - 0.22, 0.20)], panel, mirror=False)
    if hyper:
        # louvres over the front wheels, cooling exits beside the cockpit
        fa = spec['axles'][0]
        for k in range(4):
            x0 = fa + 0.28 - k * 0.16
            p.top_poly([(x0, 0.66), (x0 - 0.07, 0.66), (x0 - 0.07, 0.95), (x0, 0.95)], carbon)
        p.top_poly([(-0.9, 0.52), (-1.5, 0.52), (-1.5, 0.80), (-0.9, 0.72)], carbon)
        p.side_poly([(0.75, 0.16), (0.2, 0.16), (0.0, 0.40), (0.6, 0.34)], carbon)     # side intake
    else:
        # bonnet vents, door shut lines, grille, side exhaust exit
        bx = spec['bonnet_x']
        for s in (1,):
            p.top_poly([(bx + 0.30, 0.22), (bx - 0.25, 0.22), (bx - 0.25, 0.52), (bx + 0.30, 0.46)], carbon)
        dx = spec['cab']
        for xx in (dx + 0.55, dx - 0.55):
            p.side_poly([(xx, 0.14), (xx + 0.02, 0.14), (xx + 0.02, 0.88), (xx, 0.88)], mix(A, (0, 0, 0), 0.6))
        p.side_poly([(xf, 0.14), (xf, 0.42), (xf - 0.10, 0.42), (xf - 0.10, 0.14)], carbon)
        p.side_poly([(xr, 0.14), (xr, 0.36), (xr + 0.10, 0.36), (xr + 0.10, 0.14)], carbon)
    p.swatches(dict(glass=(0.08, 0.11, 0.16), white=(1.0, 0.98, 0.9), red=(0.95, 0.06, 0.05), carbon=carbon,
                    cage=(0.42, 0.44, 0.48), second=B, panel=panel, paint=A))


# ------------------------------------------------------------------ HD livery details (Vita)
# fictional sponsors (the same names as the advertising banners on the footbridge)
SPONSORS = ['AURORA', 'NEXUS', 'VELOCITY', 'CODE 4', 'NOVA FUEL', 'KESTREL', 'ORBIT', 'SUMMIT', 'HELIX', 'VANTA']


def _font(px):
    from PIL import ImageFont
    for f in ('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', '/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf'):
        if os.path.exists(f):
            return ImageFont.truetype(f, max(6, int(px)))
    return ImageFont.load_default()


def hd_details(p, c, spec, hyper):
    """Panel lines, lights and sponsor decals painted over the livery (512 px textures only)."""
    L = spec['L']; xf = L / 2; xr = -L / 2
    A = c['colour']; dark = mix(A, (0, 0, 0), 0.55)
    d = p.d
    def line_side(x0, z0, x1, z1, col, w=2):
        d.line([(p.sx(x0), p.sz(z0)), (p.sx(x1), p.sz(z1))], fill=rgb(col), width=max(1, round(w * p.S / 512)))
    def line_top(x0, y0, x1, y1, col, w=2, mirror=True):
        w = max(1, round(w * p.S / 512))
        d.line([(p.sx(x0), p.ty(y0)), (p.sx(x1), p.ty(y1))], fill=rgb(col), width=w)
        if mirror:
            d.line([(p.sx(x0), p.ty(-y0)), (p.sx(x1), p.ty(-y1))], fill=rgb(col), width=w)
    fa, ra = spec['axles']
    # shut lines: bonnet / front clam, door, engine cover
    if hyper:
        line_top(fa - 0.55, 0.0, fa - 0.55, 0.98, dark); line_top(ra + 0.75, 0.0, ra + 0.75, 0.98, dark)
        line_side(0.95, 0.12, 0.95, 0.60, dark); line_side(-0.55, 0.12, -0.55, 0.62, dark)
        line_side(0.95, 0.60, -0.55, 0.62, dark)
    else:
        cab = spec['cab']
        line_top(fa - 0.48, 0.0, fa - 0.48, 0.92, dark); line_top(cab - 1.10, 0.0, cab - 1.10, 0.92, dark)
        line_side(cab + 0.90, 0.20, cab + 0.86, 0.86, dark); line_side(cab - 0.70, 0.20, cab - 0.72, 0.86, dark)
    # lights on the side view: headlight graphic at the front, LED tail strip at the back
    line_side(xf - 0.05, (spec['nose'] if not hyper else 0.42) + 0.02, xf - 0.40, (spec['nose'] if not hyper else 0.46) + 0.08, (1.0, 0.98, 0.85), 4)
    line_side(xr + 0.03, 0.55 if hyper else spec['tail'] - 0.12, xr + 0.30, 0.56 if hyper else spec['tail'] - 0.10, (1.0, 0.10, 0.08), 4)
    if c['id'].startswith('career'):
        return
    # sponsor stickers: two slots in the right half of the swatch row (white / dark), stuck on as
    # separate quads by decal_quads() so the lettering reads the right way on both sides
    rng = np.random.RandomState(zlib.crc32(c['id'].encode()) & 0xffff)
    names = [SPONSORS[i] for i in rng.permutation(len(SPONSORS))[:2]]
    S = p.S
    white = (0.97, 0.97, 0.95); ink = (0.06, 0.06, 0.08)
    for k, (bg, fg) in enumerate(((white, ink), (ink, white))):
        x0, x1 = S * 0.5 + 2, S - 2
        y0 = TOP_V[1] * S + 3 + k * (S - TOP_V[1] * S - 4) / 2; y1 = y0 + (S - TOP_V[1] * S - 4) / 2 - 2
        d.rectangle([x0, y0, x1, y1], fill=rgb(bg))
        f = _font((y1 - y0) * 0.78)
        tw = d.textlength(names[k], font=f)
        d.text(((x0 + x1) / 2 - tw / 2, y0 + (y1 - y0) * 0.05), names[k], fill=rgb(fg), font=f)


def decal_quads(m, c, spec, W):
    """Stick the two sponsor decals on both sides: one on the rear fender, one on the door / front fender."""
    if c['id'].startswith('career'):
        return
    hyper = c['hyper']; fa, ra = spec['axles']
    v0 = TOP_V[1] + 3 / 512; vh = (1 - TOP_V[1] - 4 / 512) / 2
    slots = [(0.5 + 2 / 512, 1 - 2 / 512, v0, v0 + vh - 2 / 512), (0.5 + 2 / 512, 1 - 2 / 512, v0 + vh, v0 + 2 * vh - 2 / 512)]
    # rear sticker: on the panel between the back of the rear wheel arch and the tail, sized to keep
    # the lettering's proportions (about 6 : 1)
    L = spec['L']; xr = -L / 2
    x0 = ra - (0.43 if hyper else 0.45); x1 = xr + (0.10 if hyper else 0.14)
    hgt = (x0 - x1) / 6.0; zc = 0.42 if hyper else 0.58
    rear = (x0, x1, zc - hgt / 2, zc + hgt / 2, 0)
    if hyper:
        places = [rear, (-0.05, -0.60, 0.33, 0.43, 1)]
    else:
        places = [rear, (fa - 0.40, fa - 0.95, 0.47, 0.57, 1)]
    for x0, x1, z0, z1, k in places:
        u0, u1, va, vb = slots[k]
        N = 4
        xs = [x0 + (x1 - x0) * i / N for i in range(N + 1)]
        for side in (1, -1):
            for i in range(N):
                xa, xb = xs[i], xs[i + 1]
                ya, yb = side * (W(xa) + 0.006), side * (W(xb) + 0.006)
                # left side (side 1): the text starts at the front; right side: at the back
                ta, tb = i / N, (i + 1) / N
                if side < 0:
                    ta, tb = 1 - ta, 1 - tb
                ua, ub = u0 + (u1 - u0) * ta, u0 + (u1 - u0) * tb
                m.decal([(xa, ya, z0), (xb, yb, z0), (xb, yb, z1), (xa, ya, z1)],
                        [(ua, vb), (ub, vb), (ub, va), (ua, va)], shade=1.0)


# ------------------------------------------------------------------ hypercar
HY_AXLES = (1.495, -1.655)
HY_EYE = (-0.20, 0.0, 0.80)


def hyper_fns(spec):
    L = spec['L']; xf = L / 2; xr = -L / 2
    fa, ra = HY_AXLES
    nose = spec['nose']            # nose tip height
    fh = spec['fender']            # front fender peak
    rh = spec['rear_fender']
    waist = spec['waist']
    W = curve([(xf, 0.60), (xf - 0.14, 0.88), (fa + 0.30, 1.0), (fa - 0.45, 1.0), (0.45, waist), (-0.5, waist + 0.02),
               (ra + 0.62, 0.95), (ra + 0.25, 1.0), (ra - 0.40, 1.0), (xr, 0.95)])
    deck = curve([(xf, nose), (xf - 0.30, nose + 0.10), (fa, 0.42), (0.95, 0.50), (0.0, 0.56), (ra, 0.62), (xr, 0.58)])
    fend = curve([(xf, nose + 0.02), (xf - 0.25, nose + 0.20), (fa + 0.36, fh - 0.07), (fa, fh), (fa - 0.50, fh - 0.09),
                  (fa - 0.95, 0.50), (0.0, 0.48), (ra + 0.85, 0.55), (ra + 0.32, rh - 0.05), (ra, rh), (xr, rh - 0.12)])

    def top(x, y):
        w = W(x)
        return deck(x) + (fend(x) - deck(x)) * float(sstep(0.44 * w, 0.74 * w, abs(y)))
    return W, deck, fend, top


def hyper_canopy(spec):
    cw = curve([(1.02, 0.02), (0.75, 0.34), (0.25, 0.49), (-0.45, 0.47), (-1.10, 0.34), (-2.0, 0.14)])
    ct = curve([(1.02, 0.47), (0.70, 0.78), (0.25, 0.99), (-0.15, 1.03), (-0.70, 0.98), (-1.3, 0.84), (-2.0, 0.66)])
    return cw, ct


def build_hyper(spec, lod, interior_cut=False):
    L = spec['L']; xf = L / 2; xr = -L / 2
    fa, ra = HY_AXLES
    W, deck, fend, top = hyper_fns(spec)
    m = Mesh(L)
    zf = 0.05
    yfr = [[0, .22, .42, .56, .72, .86, .96], [0, .30, .56, .78, .95], [0, .5, .85]][max(lod, 0)]
    xs = [[xf, xf - 0.07, xf - 0.22, xf - 0.48, fa + 0.36, fa, fa - 0.45, fa - 0.90, 0.45, 0.0, -0.5, ra + 0.85, ra + 0.35, ra, ra - 0.40, xr + 0.08, xr],
          [xf, xf - 0.18, fa + 0.36, fa, fa - 0.60, 0.3, -0.5, ra + 0.5, ra, xr],
          [xf, fa + 0.2, -0.2, ra, xr]][max(lod, 0)]
    if lod < 0:      # HD: twice the stations along the car and across (the Vita draws ~20 of these at once)
        xs = densify(xs, 2)
        yfr = densify(yfr, 2) + [0.985]
    rings = []
    for x in xs:
        w = W(x)
        zl = zf + (0.10 if x > xf - 0.2 else 0.0)        # the nose underside lifts a little
        half = [(f * w, top(x, f * w)) for f in yfr]
        zt = top(x, w)
        half += [(w, zt - 0.06), (w, max(0.30, zl + 0.05)), (w - 0.03, zl + 0.02), (0.6 * w, zl), (0, zl)]
        ring = [(x, y, z) for y, z in half] + [(x, -y, z) for y, z in half[-2:0:-1]]
        rings.append(np.array(ring))
    m.loft(rings)
    # canopy: a teardrop bubble; glass from the windscreen back to behind the driver's head
    cw, ct = hyper_canopy(spec)
    cxs = [[1.02, 0.86, 0.62, 0.30, -0.05, -0.38, -0.80, -1.25, -1.65, -2.0], [1.02, 0.70, 0.25, -0.38, -1.1, -2.0], [1.02, 0.3, -0.38, -2.0]][max(lod, 0)]
    angs = [[0, 28, 52, 72, 90], [0, 40, 70, 90], [0, 55, 90]][max(lod, 0)]
    if lod < 0:
        cxs = densify(cxs, 2); angs = [0, 15, 30, 45, 60, 75, 90]
    crings = []
    for x in cxs:
        w = cw(x); zt = ct(x); zb = min(top(x, w), deck(x)) - 0.04
        half = [(w * np.cos(np.radians(a)), zb + (zt - zb) * np.sin(np.radians(a))) for a in angs]
        ring = [(x, y, z) for y, z in half] + [(x, -y, z) for y, z in half[-2::-1]]
        crings.append(np.array(ring))
    nglass = sum(1 for x in cxs if x > -0.40)
    m.loft(crings, cap_front=False, cap_back=False, glass=lambda i, k: i < nglass)
    # roof intake and engine-cover fin
    if lod < 2:
        m.box((-0.95, -0.13, 0.95), (-0.42, 0.13, 1.10), uv='carbon')
    fin_top = spec['fin']
    if fin_top > 0:
        x0, x1 = -0.95, xr + 0.30
        m.part(np.array([[x0, 0.012, ct(x0) - 0.05], [x1, 0.012, deck(x1)], [x1, 0.012, fin_top], [x0 - 0.1, 0.012, 1.08],
                         [x0, -0.012, ct(x0) - 0.05], [x1, -0.012, deck(x1)], [x1, -0.012, fin_top], [x0 - 0.1, -0.012, 1.08]]),
               [[0, 1, 2], [0, 2, 3], [4, 6, 5], [4, 7, 6]], uv='second', flat=True)
    # rear wing with endplates
    wz = spec['wing_z']; ws = spec['wing_span']
    m.box((xr + 0.00, -ws, wz), (xr + 0.34, ws, wz + 0.035), uv='carbon')
    if lod < 2:
        for s in (-1, 1):
            m.box((xr - 0.02, s * ws - 0.012, rhz(spec) - 0.20), (xr + 0.40, s * ws + 0.012, wz + 0.10), uv='second')
        if fin_top <= 0:
            for s in (-0.35, 0.35):
                m.box((xr + 0.10, s - 0.015, deck(xr + 0.2)), (xr + 0.24, s + 0.015, wz), uv='carbon')
    if lod <= 0:
        # splitter, lights, mirrors
        m.box((xf - 0.30, -0.80, 0.04), (xf + 0.04, 0.80, 0.07), uv='carbon')
        for s in (-1, 1):
            xl = fa + 0.50; yl = s * 0.80
            zl = top(xl, 0.8)
            m.quad((xl + 0.20, yl - 0.07, zl - 0.14), (xl + 0.20, yl + 0.07, zl - 0.14), (xl - 0.05, yl + 0.07, zl + 0.012), (xl - 0.05, yl - 0.07, zl + 0.012), uv='white', shade=1.3)
            if lod == 0:     # the HD car gets its own mirrors (hd_hyper_parts)
                m.box((0.80, s * 0.86 - 0.06, fend(0.9) + 0.10), (0.92, s * 0.86 + 0.06, fend(0.9) + 0.17), uv='carbon')
            m.box((0.84, s * 0.86 - 0.012, fend(0.9) - 0.02), (0.88, s * 0.86 + 0.012, fend(0.9) + 0.10), uv='carbon')
            m.quad((xr - 0.005, s * 0.95, 0.40), (xr - 0.005, s * 0.89, 0.40), (xr - 0.005, s * 0.89, rhz(spec) - 0.16), (xr - 0.005, s * 0.95, rhz(spec) - 0.16), uv='red', shade=1.3)
        m.quad((xr - 0.005, -0.85, 0.50), (xr - 0.005, 0.85, 0.50), (xr - 0.005, 0.85, 0.54), (xr - 0.005, -0.85, 0.54), uv='red', shade=1.3)
        m.quad((xr - 0.004, -0.80, 0.08), (xr - 0.004, 0.80, 0.08), (xr - 0.004, 0.80, 0.34), (xr - 0.004, -0.80, 0.34), uv='carbon')
    if lod < 0:
        hd_hyper_parts(m, spec, top, deck, fend, ct)
    return m


def hd_hyper_parts(m, spec, top, deck, fend, ct):
    """Vita detail: dive planes, a two-element wing with a gurney, diffuser strakes, splitter fences,
    headlight pods, mirror stalks, roof antenna, tow strap, rain light."""
    L = spec['L']; xf = L / 2; xr = -L / 2
    fa, ra = HY_AXLES
    for s in (-1, 1):
        for k, dz in enumerate((0.0, 0.07)):          # dive planes on the nose corners
            x0 = xf - 0.10 - k * 0.04; y0 = s * 0.86
            z0 = top(x0, 0.86) - 0.14 + dz
            m.part(np.array([[x0 + 0.02, y0, z0], [x0 - 0.20, y0, z0 + 0.05], [x0 - 0.20, y0 + s * 0.10, z0 + 0.05], [x0 + 0.02, y0 + s * 0.06, z0]]),
                   [[0, 1, 2], [0, 2, 3]], uv='carbon', flat=True)
        m.box((xf - 0.32, s * 0.80 - 0.008, 0.04), (xf + 0.03, s * 0.80 + 0.008, 0.14), uv='carbon')     # splitter fences
        for k in range(4):                                                                               # diffuser strakes
            y = s * (0.15 + k * 0.17)
            m.box((xr - 0.02, y - 0.006, 0.07), (xr + 0.55, y + 0.006, 0.30 - k * 0.03), uv='carbon')
        # headlight pod: a dark lens and a bright LED strip above it
        xl = fa + 0.42; zl = top(xl, 0.70)
        m.quad((xl + 0.22, s * 0.62, zl - 0.07), (xl + 0.22, s * 0.84, zl - 0.10), (xl - 0.02, s * 0.86, zl + 0.015), (xl - 0.02, s * 0.62, zl + 0.02), uv='glass', shade=1.4)
        m.quad((xl + 0.20, s * 0.64, zl - 0.055), (xl + 0.20, s * 0.82, zl - 0.085), (xl + 0.17, s * 0.82, zl - 0.07), (xl + 0.17, s * 0.64, zl - 0.04), uv='white', shade=1.5)
    ws = spec['wing_span']; wz = spec['wing_z']
    m.box((xr + 0.04, -ws, wz + 0.055), (xr + 0.26, ws, wz + 0.075), uv='carbon')         # second element
    m.box((xr - 0.005, -ws, wz + 0.035), (xr + 0.01, ws, wz + 0.065), uv='second')        # gurney flap
    m.box((-0.40, -0.004, ct(-0.40) - 0.02), (-0.30, 0.004, ct(-0.40) + 0.13), uv='carbon')   # antenna
    m.box((xr - 0.03, -0.06, 0.20), (xr - 0.005, 0.06, 0.27), uv='red', shade=1.5)        # rain light
    m.box((xf + 0.0, 0.25, 0.10), (xf + 0.04, 0.30, 0.13), uv='red', shade=1.2)           # tow strap
    W = hyper_fns(spec)[0]
    for s_ in (-1, 1):
        # louvres cut into the top of each front fender (hot air from the brakes)
        for k in range(6):
            x0 = fa + 0.22 - k * 0.075
            y0 = s_ * 0.80; zt = fend(x0) + 0.004
            m.box((x0 - 0.035, y0 - 0.11, zt - 0.006), (x0, y0 + 0.11, zt + 0.014), uv='carbon')
        # louvres over the rear wheels
        for k in range(4):
            x0 = ra + 0.18 - k * 0.075
            m.box((x0 - 0.035, s_ * 0.82 - 0.10, fend(x0) - 0.002), (x0, s_ * 0.82 + 0.10, fend(x0) + 0.016), uv='carbon')
        # streamlined mirror housing on a thin blade
        mz = fend(0.9) + 0.13
        def tear(V, c=np.array([0.0, s_ * 0.88, mz + 0.005])):
            V = V.copy(); f = np.where(V[:, 0] > 0.85, 0.5, 1.0)[:, None]     # nose of the housing narrower
            V[:, 1:] = c[1:] + (V[:, 1:] - c[1:]) * f
            return V
        m.box((0.74, s_ * 0.88 - 0.075, mz - 0.035), (0.96, s_ * 0.88 + 0.075, mz + 0.045), uv='paint', taper=tear)
        m.quad((0.745, s_ * 0.88 - 0.065, mz - 0.03), (0.745, s_ * 0.88 + 0.065, mz - 0.03),
               (0.745, s_ * 0.88 + 0.065, mz + 0.04), (0.745, s_ * 0.88 - 0.065, mz + 0.04), uv='glass', shade=1.6)
        # red class position light on the flank behind the cockpit (Hypercar class)
        xp = -0.95; yp = s_ * (W(xp) + 0.008)
        m.quad((xp, yp, 0.50), (xp - 0.22, yp, 0.50), (xp - 0.22, yp, 0.56), (xp, yp, 0.56), uv='red', shade=1.6)
        # floor edge / skid plank lip along the sides
        m.box((ra + 0.42, s_ * (W(0.0) - 0.04) - 0.03, 0.035), (fa - 0.45, s_ * (W(0.0) - 0.04) + 0.03, 0.065), uv='carbon')
        # brake cooling inlets either side of the nose
        xn = xf - 0.16; zn = top(xn, 0.45)
        m.quad((xn + 0.03, s_ * 0.30, zn - 0.115), (xn + 0.03, s_ * 0.55, zn - 0.115), (xn + 0.03, s_ * 0.55, zn - 0.05), (xn + 0.03, s_ * 0.30, zn - 0.05), uv='cage', shade=0.35)
        # refuelling / air-jack nozzle on the roof side
    m.cyl((-0.70, 0.0, ct(-0.70) - 0.01), (-0.70, 0.0, ct(-0.70) + 0.03), 0.04, 8, uv='cage', shade=0.8)
    m.cyl((xr + 0.22, 0.0, 0.05), (xr + 0.22, 0.0, 0.10), 0.03, 6, uv='cage', shade=0.7)   # air-jack


def rhz(spec):
    return spec['rear_fender']


def build_hyper_interior():
    """Single central seat under the bubble: carbon tub, dash, roll cage. Car coordinates."""
    m = Mesh(5.0)
    cw, ct = hyper_canopy({})
    tub = 0.44
    # tub: floor, sides, front bulkhead, rear bulkhead (everything below the canopy's base)
    m.quad((1.05, -tub, 0.20), (1.05, tub, 0.20), (-0.75, tub, 0.20), (-0.75, -tub, 0.20), uv='carbon', shade=0.55)
    for s in (-1, 1):
        m.quad((1.05, s * tub, 0.20), (-0.75, s * tub, 0.20), (-0.75, s * tub, 0.60), (1.05, s * tub, 0.52), uv='carbon', shade=0.75)
        m.quad((1.05, s * tub, 0.52), (-0.75, s * tub, 0.60), (-0.75, s * (tub + 0.10), 0.63), (1.05, s * (tub + 0.05), 0.55), uv='carbon', shade=0.95)
    m.quad((-0.62, -tub, 0.20), (-0.62, tub, 0.20), (-0.62, tub, 1.00), (-0.62, -tub, 1.00), uv='carbon', shade=0.6)
    # dash cowl across the front with a hood over the centre display
    m.box((0.50, -tub, 0.20), (1.05, tub, 0.585), uv='carbon', shade=0.8)
    m.box((0.46, -0.25, 0.585), (0.62, 0.25, 0.72), uv='carbon', shade=0.65)                      # race-control message board
    for s in (-1, 1):
        m.box((0.40, s * 0.31 - 0.125, 0.47), (0.52, s * 0.31 + 0.125, 0.635), uv='carbon', shade=0.6)   # side display housings
    # seat shell either side of the driver, headrest behind
    for s in (-1, 1):
        m.box((-0.62, s * 0.27 - 0.03, 0.25), (0.05, s * 0.27 + 0.03, 0.62), uv='carbon', shade=0.7)
        m.box((-0.62, s * 0.19 - 0.05, 0.62), (-0.42, s * 0.19 + 0.05, 0.92), uv='second', shade=0.5)
    # roll cage: main hoop behind the head, front hoop round the windscreen, roof bars, side bars
    r = 0.022
    def hoop(x, shrink):
        w = cw(x) - shrink; zt = ct(x) - shrink
        return [(x, w * np.cos(np.radians(a)), 0.56 + (zt - 0.56) * np.sin(np.radians(a))) for a in (0, 35, 62, 90, 118, 145, 180)]
    main = hoop(-0.50, 0.035); front = hoop(0.42, 0.03)
    m.tube(main, r, uv='cage'); m.tube(front, r * 0.8, uv='cage')
    for k in (1, 5):
        m.tube([main[k], front[k]], r * 0.8, uv='cage')
    for s in (-1, 1):
        m.tube([(-0.50, s * 0.43, 0.56), (0.0, s * 0.455, 0.66), (0.42, s * 0.43, 0.58)], r * 0.8, uv='cage')
        m.tube([(0.42, s * 0.40, 0.58), (0.98, s * 0.10, 0.50)], r * 0.7, uv='cage')
    m.tube([(-0.50, -0.38, 0.60), (-0.50, 0.38, 0.95)], r * 0.8, uv='cage')
    return m


# ------------------------------------------------------------------ LMGT3
GT_EYE_REL = (-0.28, -0.38, 1.05)     # relative to the cabin reference point; driver on the right


def gt_axles(L):
    return (0.29 * L, -0.29 * L)


def gt_fns(spec):
    L = spec['L']; xf = L / 2; xr = -L / 2
    fa, ra = gt_axles(L)
    cab = spec['cab']
    nose = spec['nose']; belt = 0.88; tail = spec['tail']
    flare = lambda x: 0.07 * (np.exp(-((x - fa) / 0.36) ** 2) + np.exp(-((x - ra) / 0.38) ** 2))
    Wb = curve([(xf, 0.74), (xf - 0.12, 0.90), (xf - 0.45, 0.93), (xr + 0.35, 0.93), (xr, 0.88)])
    W = lambda x: Wb(x) + float(flare(x))
    top = curve([(xf, nose), (xf - 0.10, nose + 0.05), (fa, spec['bonnet']), (cab + 0.80, belt - 0.02), (cab + 0.4, belt), (cab - 1.0, belt + 0.01),
                 (cab - 1.75, tail + 0.02), (xr + 0.12, tail), (xr, tail - 0.05)])
    return W, top


def build_gt(spec, lod):
    L = spec['L']; xf = L / 2; xr = -L / 2
    fa, ra = gt_axles(L)
    cab = spec['cab']; belt = 0.88
    W, top = gt_fns(spec)
    m = Mesh(L)
    zf = 0.085
    yfr = [[0, .35, .65, .86, .96], [0, .5, .86, .96], [0, .6, .95]][max(lod, 0)]
    xs = [[xf, xf - 0.08, xf - 0.30, fa + 0.38, fa, fa - 0.42, cab + 0.80, cab + 0.25, cab - 0.45, cab - 1.05, ra + 0.40, ra, ra - 0.38, xr + 0.10, xr],
          [xf, xf - 0.15, fa + 0.2, fa - 0.4, cab + 0.80, cab - 0.3, ra + 0.4, ra - 0.3, xr],
          [xf, fa, cab + 0.8, cab - 1.0, xr]][max(lod, 0)]
    if lod < 0:
        xs = densify(xs, 2)
        yfr = densify(yfr, 2) + [0.99]
    rings = []
    for x in xs:
        w = W(x); zt = top(x)
        front = x > xf - 0.05; back = x < xr + 0.05
        zl = zf + (0.03 if front or back else 0)
        half = [(f * w, zt - 0.035 * f * f) for f in yfr]
        half += [(w, zt - 0.10), (w, 0.42), (w - 0.035, zl + 0.03), (0.7 * w, zl), (0, zl)]
        rings.append(np.array([(x, y, z) for y, z in half] + [(x, -y, z) for y, z in half[-2:0:-1]]))
    m.loft(rings)
    # greenhouse: windscreen, side windows and rear window are glass; roof and pillars are bodywork
    roof = 1.265; yb = 0.80; yr = 0.61
    rear = spec['rear_glass']
    gx = [cab + 0.82, cab + 0.08, cab - 0.52, cab - 0.66, cab - 1.02, cab - rear]
    grings = []
    for i, x in enumerate(gx):
        if i in (0, 5):
            b = top(x) - 0.02
            half = [(yb - (0.06 if i == 0 else 0.10), b), (yb - 0.12, b + 0.012), (0.34, b + 0.018), (0, b + 0.022)]
        else:
            half = [(yb, belt - 0.02), (yr, roof - 0.02), (0.33, roof + 0.012), (0, roof + 0.025)]
        grings.append(np.array([(x, y, z) for y, z in half] + [(x, -y, z) for y, z in half[-2::-1]]))
    # ring segments: 0 = left side, 1,2 = left roof, 3,4 = right roof, 5 = right side, 6 = underside (closing)
    def is_glass(i, k):
        side = k in (0, 5); roofk = k in (1, 2, 3, 4)
        if i == 0: return side or roofk          # windscreen and front quarter lights
        if i == 1: return side                   # door windows
        if i == 3: return side                   # rear quarter windows
        if i == 4: return roofk                  # rear window
        return False
    m.loft(grings, cap_front=False, cap_back=False, glass=is_glass)
    if lod <= 0:
        # A-pillars (bodywork over the glass edge) so the frame stays when the glass is removed
        for s in (-1, 1):
            m.tube([(gx[0], s * (yb - 0.05), top(gx[0])), (gx[1], s * yr, roof - 0.02)], 0.035, uv='paint')
    # rear wing on swan-neck mounts
    wz = spec['wing_z']; ws = spec['wing_span']; wx = xr + spec['wing_x']
    m.box((wx, -ws, wz), (wx + 0.36, ws, wz + 0.04), uv='carbon')
    if lod < 2:
        for s in (-1, 1):
            m.box((wx - 0.03, s * ws - 0.012, wz - 0.10), (wx + 0.40, s * ws + 0.012, wz + 0.12), uv='second')
            m.box((wx + 0.12, s * 0.42 - 0.015, top(wx + 0.3) - 0.05), (wx + 0.30, s * 0.42 + 0.015, wz + 0.01), uv='carbon')
    if lod <= 0:
        m.box((xf - 0.25, -0.90, 0.05), (xf + 0.07, 0.90, 0.085), uv='carbon')                 # splitter
        m.box((xr - 0.03, -0.75, 0.07), (xr + 0.30, 0.75, 0.10), uv='carbon')                 # diffuser
        zn = spec['nose']
        m.quad((xf + 0.004, -0.50, 0.16), (xf + 0.004, 0.50, 0.16), (xf + 0.004, 0.50, zn - 0.12), (xf + 0.004, -0.50, zn - 0.12), uv='carbon')   # grille
        for s in (-1, 1):
            xl = xf - 0.20; w = W(xl)
            m.quad((xl + 0.14, s * (w - 0.30), top(xl + 0.14) + 0.006), (xl + 0.10, s * (w - 0.06), top(xl + 0.1) - 0.02), (xl - 0.18, s * (w - 0.03), top(xl - 0.18) - 0.02), (xl - 0.12, s * (w - 0.28), top(xl - 0.12) + 0.006), uv='white', shade=1.3)
            m.quad((xr - 0.004, s * 0.84, spec['tail'] - 0.20), (xr - 0.004, s * 0.45, spec['tail'] - 0.20), (xr - 0.004, s * 0.45, spec['tail'] - 0.11), (xr - 0.004, s * 0.84, spec['tail'] - 0.11), uv='red', shade=1.3)
            if lod == 0:
                m.box((cab + 0.50, s * 0.95 - 0.08, 0.93), (cab + 0.60, s * 0.95 + 0.03, 1.01), uv='paint')    # mirrors
    if lod < 0:
        hd_gt_parts(m, spec, W, top)
    return m


def hd_gt_parts(m, spec, W, top):
    """Vita detail: canards, side skirts, diffuser fins, wing second element + gurney, exhausts,
    roof scoop and antenna, bonnet louvres, rear tow strap."""
    L = spec['L']; xf = L / 2; xr = -L / 2
    fa, ra = gt_axles(L)
    cab = spec['cab']
    for s in (-1, 1):
        for k in range(2):                                     # canards on the front corners
            x0 = xf - 0.12 - k * 0.05; z0 = 0.26 + k * 0.10
            y0 = s * (W(x0) - 0.06)
            m.part(np.array([[x0 + 0.03, y0, z0], [x0 - 0.22, y0, z0 + 0.06], [x0 - 0.22, y0 + s * 0.11, z0 + 0.06], [x0 + 0.03, y0 + s * 0.07, z0]]),
                   [[0, 1, 2], [0, 2, 3]], uv='carbon', flat=True)
        m.box((ra + 0.42, s * (W(0.0) - 0.02) - 0.03, 0.07), (fa - 0.42, s * (W(0.0) - 0.02) + 0.03, 0.15), uv='carbon')   # side skirt
        for k in range(3):                                     # diffuser fins
            y = s * (0.18 + k * 0.20)
            m.box((xr - 0.03, y - 0.006, 0.07), (xr + 0.42, y + 0.006, 0.26 - k * 0.03), uv='carbon')
        m.tube([(xr + 0.10, s * 0.62, 0.16), (xr - 0.04, s * 0.62, 0.16)], 0.04, sides=8, uv='cage', shade=0.6)    # exhausts
    wz = spec['wing_z']; ws = spec['wing_span']; wx = xr + spec['wing_x']
    m.box((wx + 0.05, -ws, wz + 0.06), (wx + 0.26, ws, wz + 0.08), uv='carbon')
    m.box((wx - 0.005, -ws, wz + 0.04), (wx + 0.01, ws, wz + 0.07), uv='second')
    m.box((cab - 0.05, -0.14, 1.27), (cab + 0.25, 0.14, 1.32), uv='carbon')                 # roof scoop
    m.box((cab - 0.50, -0.004, 1.27), (cab - 0.42, 0.004, 1.40), uv='carbon')               # antenna
    bx = spec['bonnet_x']
    for k in range(5):                                                                      # bonnet louvres
        x0 = bx + 0.25 - k * 0.10
        for s in (-1, 1):
            m.box((x0 - 0.03, s * 0.30 - 0.12, top(x0) - 0.005), (x0, s * 0.30 + 0.12, top(x0) + 0.012), uv='carbon')
    m.box((xr - 0.03, -0.30, 0.12), (xr, -0.25, 0.15), uv='red', shade=1.2)               # tow strap
    roof = 1.265
    for s_ in (-1, 1):
        # louvres over the front wheels
        for k in range(5):
            x0 = fa + 0.20 - k * 0.08; y0 = s_ * (W(x0) - 0.22)
            m.box((x0 - 0.035, y0 - 0.12, top(x0) - 0.02), (x0, y0 + 0.12, top(x0) + 0.008), uv='carbon')
        # headlight: dark lens with a bright daytime-running strip
        xl = xf - 0.20; w = W(xl)
        m.quad((xl + 0.12, s_ * (w - 0.27), top(xl + 0.12) + 0.012), (xl + 0.09, s_ * (w - 0.07), top(xl + 0.09) - 0.012),
               (xl + 0.06, s_ * (w - 0.06), top(xl + 0.06) - 0.008), (xl + 0.09, s_ * (w - 0.26), top(xl + 0.09) + 0.016), uv='white', shade=1.7)
        # brake ducts in the bumper
        m.quad((xf + 0.006, s_ * 0.58, 0.14), (xf + 0.006, s_ * 0.78, 0.14), (xf + 0.006, s_ * 0.76, 0.24), (xf + 0.006, s_ * 0.60, 0.24), uv='cage', shade=0.35)
        # mirror on a stalk: housing + glass facing back
        mx = cab + 0.55; my = s_ * 1.00
        m.box((mx - 0.08, my - 0.065, 0.93), (mx + 0.08, my + 0.065, 1.01), uv='paint')
        m.quad((mx - 0.081, my - 0.06, 0.935), (mx - 0.081, my + 0.04, 0.935), (mx - 0.081, my + 0.04, 1.005), (mx - 0.081, my - 0.06, 1.005), uv='glass', shade=1.6)
        m.box((mx - 0.02, s_ * 0.84, 0.90), (mx + 0.02, my, 0.93), uv='carbon')
        # LED tail strip across the back
        m.quad((xr - 0.005, s_ * 0.45, spec['tail'] - 0.17), (xr - 0.005, s_ * 0.02, spec['tail'] - 0.17),
               (xr - 0.005, s_ * 0.02, spec['tail'] - 0.15), (xr - 0.005, s_ * 0.45, spec['tail'] - 0.15), uv='red', shade=1.5)
        # bonnet pins
        m.cyl((fa + 0.45, s_ * 0.55, top(fa + 0.45)), (fa + 0.45, s_ * 0.55, top(fa + 0.45) + 0.015), 0.02, 6, uv='white')
        # rear-quarter fuel filler
        xq = cab - 0.85
        m.cyl((xq, s_ * 0.66, roof - 0.12), (xq, s_ * 0.71, roof - 0.12), 0.045, 8, uv='cage', shade=0.8)
    # windscreen wiper: arm from the scuttle across the screen
    x0 = cab + 0.78
    m.tube([(x0, -0.55, top(x0) + 0.02), (cab + 0.62, -0.12, top(cab + 0.62) + 0.10), (cab + 0.48, 0.30, 1.05)], 0.012, uv='cage', shade=0.5)
    m.cyl((xr + 0.20, 0.0, 0.07), (xr + 0.20, 0.0, 0.12), 0.03, 6, uv='cage', shade=0.7)   # air-jack


def build_gt_interior():
    """Two-seat cabin, driver on the right. The passenger side is stripped to the cage and an
    instrument pod. Coordinates relative to the cabin reference point (x) and the car centre."""
    m = Mesh(4.7)
    iw = 0.76; belt = 0.87; fl = 0.22
    # tub: floor, door panels, rear bulkhead, front bulkhead
    m.quad((0.95, -iw, fl), (0.95, iw, fl), (-1.05, iw, fl), (-1.05, -iw, fl), uv='cage', shade=0.45)
    for s in (-1, 1):
        m.quad((0.95, s * iw, fl), (-1.05, s * iw, fl), (-1.05, s * iw, belt), (0.95, s * iw, belt), uv='carbon', shade=0.8)
        m.quad((0.95, s * iw, belt), (-1.05, s * iw, belt), (-1.05, s * (iw + 0.06), belt + 0.01), (0.95, s * (iw + 0.06), belt + 0.01), uv='carbon', shade=1.0)
    m.quad((-1.0, -iw, fl), (-1.0, iw, fl), (-1.0, iw, belt + 0.02), (-1.0, -iw, belt + 0.02), uv='carbon', shade=0.6)
    # dash: full width, sloping up to the windscreen; binnacle hood in front of the driver (right)
    m.box((0.42, -iw, fl), (0.95, iw, 0.80), uv='carbon', shade=0.7)
    m.part(np.array([[0.42, -iw, 0.80], [0.42, iw, 0.80], [0.86, iw, 0.885], [0.86, -iw, 0.885]]), [[0, 1, 2], [0, 2, 3]], uv='carbon', flat=True, shade=0.95)
    m.box((0.36, -0.57, 0.80), (0.56, -0.19, 0.935), uv='carbon', shade=0.6)        # driver's binnacle
    m.box((0.30, -0.13, 0.50), (0.50, 0.13, 0.86), uv='carbon', shade=0.75)         # centre console with screen
    m.box((-0.55, -0.10, fl), (0.30, 0.10, 0.42), uv='carbon', shade=0.6)           # tunnel
    # driver's seat (right): bolsters and head restraint wings
    for s in (-0.16, 0.16):
        m.box((-0.62, -0.38 + s - 0.035, 0.30), (0.02, -0.38 + s + 0.035, 0.70), uv='carbon', shade=0.7)
        m.box((-0.62, -0.38 + s * 1.15 - 0.03, 0.86), (-0.40, -0.38 + s * 1.15 + 0.03, 1.14), uv='second', shade=0.5)
    m.box((-0.66, -0.56, 0.30), (-0.56, -0.20, 1.14), uv='carbon', shade=0.55)
    # roll cage
    r = 0.022; cg = 'cage'
    roof = 1.215; yr = 0.565
    A = lambda s: [(0.74, s * 0.70, 0.89), (0.10, s * yr, roof), (-0.58, s * yr, roof), (-0.60, s * 0.70, fl)]
    for s in (-1, 1):
        m.tube(A(s), r, uv=cg)
        m.tube([(0.40, s * 0.72, 0.36), (-0.58, s * 0.72, 0.80)], r * 0.9, uv=cg)      # door cross
        m.tube([(0.40, s * 0.72, 0.80), (-0.58, s * 0.72, 0.36)], r * 0.9, uv=cg)
    m.tube([(-0.58, -yr, roof), (-0.58, yr, roof)], r, uv=cg)                           # main hoop top
    m.tube([(-0.22, -yr, roof), (-0.22, yr, roof)], r * 0.9, uv=cg)                     # roof cross bar
    m.tube([(-0.58, -yr, roof), (0.10, yr, roof)], r * 0.8, uv=cg)                      # roof diagonal
    m.tube([(-0.60, -0.70, fl), (-0.59, 0.66, roof - 0.04)], r * 0.9, uv=cg)            # main hoop diagonal
    m.tube([(-0.59, -0.68, 0.74), (-0.59, 0.68, 0.74)], r * 0.9, uv=cg)                 # harness bar
    # passenger side (left): no seat -- floor bars and the instrument pod on a cage bar
    m.tube([(0.40, 0.10, 0.30), (-0.58, 0.70, 0.30)], r * 0.8, uv=cg)
    m.tube([(0.40, 0.70, 0.30), (-0.58, 0.10, 0.30)], r * 0.8, uv=cg)
    m.tube([(0.70, 0.08, 0.86), (0.70, 0.70, 0.86)], r * 0.8, uv=cg)
    m.box((0.70, 0.06, 0.875), (0.80, 0.66, 1.035), uv='carbon', shade=0.6)             # instrument pod on the dash top
    return m


def build_steering(hyper):
    """Steering wheel in the YZ plane, facing -X (towards the driver), centre at the origin."""
    m = Mesh(1.0)
    if hyper:
        pts = [(0.14, 0.10), (0.15, 0.0), (0.14, -0.08), (0.08, -0.10), (-0.08, -0.10), (-0.14, -0.08), (-0.15, 0.0), (-0.14, 0.10)]
        for a, b in zip(pts, pts[1:]):
            m.tube([(0, a[0], a[1]), (0, b[0], b[1])], 0.017, sides=5, uv='carbon', shade=0.9)
        m.box((-0.012, -0.135, -0.07), (0.022, 0.135, 0.085), uv='carbon', shade=0.75)
    else:
        n = 12
        for k in range(n):
            a0 = k * 2 * np.pi / n; a1 = (k + 1) * 2 * np.pi / n
            if 4.2 < (a0 + a1) / 2 < 5.2:       # flat bottom
                continue
            m.tube([(0, 0.165 * np.cos(a0), 0.165 * np.sin(a0)), (0, 0.165 * np.cos(a1), 0.165 * np.sin(a1))], 0.017, sides=5, uv='carbon', shade=0.9)
        m.tube([(0, 0.165 * np.cos(4.19), 0.165 * np.sin(4.19)), (0, 0.165 * np.cos(5.24), 0.165 * np.sin(5.24))], 0.017, sides=5, uv='carbon', shade=0.9)
        m.box((-0.012, -0.155, -0.035), (0.02, 0.155, 0.04), uv='carbon', shade=0.75)       # spokes
        m.box((-0.012, -0.03, -0.14), (0.02, 0.03, -0.03), uv='carbon', shade=0.75)
        m.box((-0.014, -0.075, -0.05), (0.024, 0.075, 0.06), uv='carbon', shade=0.7)       # hub with display
    for s in (-1, 1):
        m.box((-0.01, s * 0.11 - 0.012, 0.045), (0.024, s * 0.11 + 0.012, 0.062), uv='red' if s < 0 else 'white', shade=0.9)   # buttons
    m.tube([(0.02, 0, 0), (0.30, 0, -0.03)], 0.025, sides=5, uv='cage', shade=0.6)        # column
    return m


def build_wheel(sides):
    """Road wheel: axle along Y, outer face at +Y."""
    m = Mesh(1.0)
    R = 0.355; hw = 0.155; rr = 0.25
    ang = [k * 2 * np.pi / sides for k in range(sides)]
    c = [(np.cos(a), np.sin(a)) for a in ang]
    for k in range(sides):
        a = c[k]; b = c[(k + 1) % sides]
        m.quad((R * a[0], -hw, R * a[1]), (R * b[0], -hw, R * b[1]), (R * b[0], hw, R * b[1]), (R * a[0], hw, R * a[1]), uv='carbon', shade=0.75)           # tread
        m.quad((R * a[0], hw, R * a[1]), (R * b[0], hw, R * b[1]), (rr * b[0], hw, rr * b[1]), (rr * a[0], hw, rr * a[1]), uv='carbon', shade=1.0)        # sidewall
        m.part(np.array([(rr * a[0], hw - 0.03, rr * a[1]), (rr * b[0], hw - 0.03, rr * b[1]), (0, hw - 0.05, 0)]), [[0, 1, 2]], uv='white' if k % 2 else 'cage', flat=True, shade=0.9 if k % 2 else 0.7)   # rim / spokes
        m.part(np.array([(R * a[0], -hw, R * a[1]), (R * b[0], -hw, R * b[1]), (0, -hw, 0)]), [[0, 1, 2]], uv='carbon', flat=True, shade=0.4)            # inner face
    return m


def swatch_tex(second=(0.8, 0.1, 0.1)):
    p = Paint(5.0, 64)
    p.base((0.1, 0.1, 0.1))
    p.swatches(dict(glass=(0.08, 0.11, 0.16), white=(0.92, 0.92, 0.88), red=(0.85, 0.08, 0.06), carbon=(0.10, 0.10, 0.115),
                    cage=(0.50, 0.52, 0.56), second=second, panel=(0.9, 0.1, 0.1), paint=(0.6, 0.6, 0.6)))
    return p.img


# ------------------------------------------------------------------ car list
def read_cars():
    src = open(os.path.join(ROOT, 'src', 'data.cpp')).read()
    cars = []
    for mm in re.finditer(r'\{"(\w+)",\s*"[^"]*",\s*"[^"]*",\s*"[^"]*",\s*\{\d+, \d+\},\s*(CLASS_\w+),.*?([\d.]+)f, C\(([\d.]+)f, ([\d.]+)f, ([\d.]+)f\)', src):
        cars.append(dict(id=mm.group(1), hyper=mm.group(2) == 'CLASS_HYPER', L=float(mm.group(3)), colour=(float(mm.group(4)), float(mm.group(5)), float(mm.group(6)))))
    return cars


# per-car character: (livery style, second colour, accent) and shape choices
LOOK = {
    'ardenza499':       ('arrow',   (0.98, 0.85, 0.10), (0.05, 0.05, 0.05)),
    'kaizen10':         ('band',    (0.90, 0.10, 0.10), (0.95, 0.95, 0.95)),
    'regalvr8':         ('twotone', (0.08, 0.10, 0.30), (0.95, 0.95, 0.95)),
    'isarmh8':          ('slash',   (0.10, 0.25, 0.75), (0.85, 0.10, 0.12)),
    'origingx01':       ('stripe',  (0.95, 0.55, 0.10), (0.95, 0.55, 0.10)),
    'montagne26':       ('split',   (0.95, 0.95, 0.97), (0.90, 0.12, 0.12)),
    'lionne9x9':        ('slash',   (0.75, 0.95, 0.10), (0.50, 0.50, 0.55)),
    'ashcombevalkyr':   ('stripe',  (0.80, 0.95, 0.20), (0.80, 0.95, 0.20)),
    'ardenza296':       ('stripe',  (0.98, 0.98, 0.98), (0.05, 0.40, 0.10)),
    'zuffen911':        ('twotone', (0.10, 0.10, 0.12), (0.90, 0.10, 0.50)),
    'isarm4':           ('slash',   (0.20, 0.55, 0.95), (0.90, 0.12, 0.12)),
    'sterneamg':        ('band',    (0.08, 0.08, 0.10), (0.10, 0.80, 0.75)),
    'ashcombevanguard': ('arrow',   (0.80, 0.95, 0.20), (0.05, 0.05, 0.05)),
    'kentuckyz6':       ('split',   (0.08, 0.08, 0.10), (0.95, 0.95, 0.95)),
    'woking720':        ('twotone', (0.10, 0.25, 0.60), (0.95, 0.95, 0.95)),
    'dearbornstallion': ('stripe',  (0.10, 0.20, 0.65), (0.85, 0.10, 0.10)),
    'kotobukircg':      ('slash',   (0.85, 0.10, 0.12), (0.08, 0.08, 0.10)),
    # career mode team cars: the livery is repainted in the game (see write_livery_set)
    'careerhyper':      ('stripe',  (0.08, 0.08, 0.10), (0.95, 0.95, 0.97)),
    'careergt3':        ('stripe',  (0.08, 0.08, 0.10), (0.95, 0.95, 0.97)),
}
# GT3 layouts: where the cabin sits (front-, mid- or rear-engined proportions)
GT_LAYOUT = {'ardenza296': 'mid', 'woking720': 'mid', 'zuffen911': 'rear', 'kentuckyz6': 'mid'}


def car_spec(c):
    rng = np.random.RandomState(zlib.crc32(c['id'].encode()) & 0x7fffffff)
    style, second, accent = LOOK[c['id']]
    s = dict(L=c['L'], style=style, second=second, accent=accent)
    if c['hyper']:
        s.update(nose=0.20 + rng.rand() * 0.06, fender=0.70 + rng.rand() * 0.08, rear_fender=0.76 + rng.rand() * 0.08,
                 waist=0.80 + rng.rand() * 0.10, fin=(1.02 + rng.rand() * 0.06) if rng.rand() < 0.8 else 0,
                 wing_z=0.96 + rng.rand() * 0.08, wing_span=0.86 + rng.rand() * 0.10,
                 axles=HY_AXLES, panel=(0.55, 0.36), top_panel=1.60)
    else:
        lay = GT_LAYOUT.get(c['id'], 'front')
        cab = dict(front=-0.30, mid=0.12, rear=-0.12)[lay] + (rng.rand() - 0.5) * 0.08
        s.update(cab=cab, nose=dict(front=0.62, mid=0.50, rear=0.54)[lay] + rng.rand() * 0.05,
                 bonnet=dict(front=0.80, mid=0.66, rear=0.70)[lay] + rng.rand() * 0.03,
                 tail=dict(front=0.90, mid=0.88, rear=0.80)[lay] + rng.rand() * 0.04,
                 rear_glass=dict(front=1.62, mid=1.50, rear=1.95)[lay],
                 wing_z=1.24 + rng.rand() * 0.08, wing_span=0.84 + rng.rand() * 0.10, wing_x=rng.rand() * 0.10,
                 axles=gt_axles(c['L']), panel=(cab - 0.02, 0.56), top_panel=cab - 0.25, bonnet_x=gt_axles(c['L'])[0] - 0.05)
    if HD:
        ref_shape(s, c)
    return s


REF = None


def ref_shape(s, c):
    """Proportions from the car's Unity AI reference model, kept inside the ranges the generator's
    shapes are made for (the AI models' wings and antennas would otherwise skew the heights)."""
    global REF
    if REF is None:
        fn = os.path.join(ROOT, 'tools', 'ref_shapes.json')
        REF = __import__('json').load(open(fn)) if os.path.exists(fn) else {}
    r = REF.get(c['id'])
    if not r:
        return
    cl = lambda v, a, b: float(min(max(v, a), b))
    if c['hyper']:
        s['nose'] = cl(0.20 + (r['nose'] - 0.27) * 0.30, 0.20, 0.27)
        s['fender'] = cl(r['front_fender'] * 0.92, 0.70, 0.79)
        s['rear_fender'] = cl(r['rear_fender'] * 0.92, 0.76, 0.86)
        s['wing_z'] = cl(r['wing'] * 0.82, 0.95, 1.06)
        s['fin'] = cl(r['fin'] * 0.84, 1.00, 1.09) if r['fin'] > r['roof'] - 0.12 else 0
    else:
        s['cab'] = cl(r['roof_x'] + 0.29, -0.40, 0.16)
        s['top_panel'] = s['cab'] - 0.25; s['panel'] = (s['cab'] - 0.02, 0.56)
        s['nose'] = cl(r['nose'] * 0.95, 0.48, 0.68)
        s['bonnet'] = cl(r['front_fender'] * 0.95, 0.64, 0.84)
        s['wing_z'] = cl(r['wing'] * 0.97, 1.20, 1.38)


# ------------------------------------------------------------------ career liveries
# The career car's livery is painted once per style with "key" colours standing for the player's
# primary / second / accent paint, then stored as palette indices ("roles"). The game builds the palette
# from the colours chosen in the livery editor, so any style in any colours costs no extra drawing.
KEY_A, KEY_B, KEY_C = (1.0, 0.0, 1.0), (0.0, 1.0, 1.0), (0.5, 0.0, 1.0)
LVY_LODS = (128, 64, 32)


def write_livery_set(path, c, spec, sizes=None):
    sizes = sizes or LVY_LODS
    keys = {rgb(KEY_A): 1, rgb(KEY_B): 2, rgb(KEY_C): 3, rgb(mix(KEY_A, (0, 0, 0), 0.6)): 4}   # 4: primary, darkened (door lines)
    roles = {}                                    # rgb -> role index
    images = []
    for style in STYLES:
        for ts in sizes:
            p = Paint(c['L'], S=ts)
            livery(p, dict(c, colour=KEY_A), dict(spec, style=style, second=KEY_B, accent=KEY_C), c['hyper'])
            a = np.asarray(p.img, np.uint8).reshape(-1, 3)
            idx = np.zeros(len(a), np.uint8)
            cols, inv = np.unique(a, axis=0, return_inverse=True)
            for k, col in enumerate(tuple(int(x) for x in cc) for cc in cols):
                if col not in roles: roles[col] = len(roles)
                idx[inv.reshape(-1) == k] = roles[col]
            images.append(swizzle(idx, ts, ts))
    assert len(roles) <= 32, len(roles)
    with open(path, 'wb') as f:
        f.write(b'LVY1'); f.write(struct.pack('<III', len(STYLES), len(sizes), len(roles)))
        f.write(struct.pack('<4I', *(list(sizes) + [0])))
        tab = [(0, 0)] * 32
        for col, r in roles.items():
            kind = keys.get(col, 0)
            tab[r] = (kind, 0xFF000000 | (col[2] << 16) | (col[1] << 8) | col[0])
        for kind, abgr in tab: f.write(struct.pack('<II', kind, abgr))
        for im in images: f.write(im.tobytes())
    return len(roles)


def main():
    global SW_DIV
    if HD:
        SW_DIV = 16
    os.makedirs(OUT, exist_ok=True)
    for c in read_cars():
        spec = car_spec(c)
        p = Paint(c['L'], S=1024 if HD else 256)   # HD: drawn at 2x, shrunk to 512 = smooth edges
        livery(p, c, spec, c['hyper'])
        build = build_hyper if c['hyper'] else build_gt
        if HD:
            hd_details(p, c, spec, c['hyper'])
            top_mesh = build(spec, -1)
            decal_quads(top_mesh, c, spec, hyper_fns(spec)[0] if c['hyper'] else gt_fns(spec)[0])
            mid_mesh = build(spec, 0)
            decal_quads(mid_mesh, c, spec, hyper_fns(spec)[0] if c['hyper'] else gt_fns(spec)[0])
            # far cars (> 75 m) get the plainest body: most of the field is far away most of the time
            lods = [(top_mesh, p.img, 512), (mid_mesh, p.img, 256), (build(spec, 2), p.img, 128)]
        else:
            lods = [(build(spec, 0), p.img, 128), (build(spec, 1), p.img, 64), (build(spec, 2), p.img, 32)]
        Wf = hyper_fns(spec)[0] if c['hyper'] else gt_fns(spec)[0]
        px = spec['panel'][0]
        meta = (0.0 if c['hyper'] else spec['cab'], min(Wf(px - 0.2), Wf(px + 0.2)) + 0.012, px)
        n = write_emm(os.path.join(OUT, f"car_{c['id']}.emm"), lods, meta=meta)
        print(f"{c['id']:18s} {'HY' if c['hyper'] else 'GT'}  triangles {n}")
        if c['id'].startswith('career'):
            print('   livery set', write_livery_set(os.path.join(OUT, f"livery_{c['id']}.lvy"), c, spec, (512, 256, 128) if HD else None), 'colour roles')
        if len(sys.argv) > 1:
            p.img.save(os.path.join(sys.argv[1], f"livery_{c['id']}.png"))
    if HD:
        main_hd_extras()
        return
    t = swatch_tex()
    print('cockpit_hypercar', write_emm(os.path.join(OUT, 'cockpit_hypercar.emm'), [(build_hyper_interior(), t, 64)]))
    print('cockpit_gt3', write_emm(os.path.join(OUT, 'cockpit_gt3.emm'), [(build_gt_interior(), t, 64)]))
    print('steering_hyper', write_emm(os.path.join(OUT, 'steering_hyper.emm'), [(build_steering(True), t, 64)]))
    print('steering_gt3', write_emm(os.path.join(OUT, 'steering_gt3.emm'), [(build_steering(False), t, 64)]))
    print('race_wheel', write_emm(os.path.join(OUT, 'race_wheel.emm'), [(build_wheel(14), t, 64), (build_wheel(10), t, 32), (build_wheel(6), t, 32)]))


# ------------------------------------------------------------------ HD interiors (Vita), after the Unity AI reference pictures
# Cabin coordinates (x from the cabin point, metres). They are laid out around the Vita's instruments
# (src/render.cpp drawNewInstruments): hypercar wheel at (0.12, 0, 0.625), dash display at (0.43, 0, 0.655);
# GT3 cluster at (0.45, -0.38, 0.935), centre screen at (0.40, 0.02, 0.985).

def hd_hyper_interior():
    """Carbon tub with a sculpted dash cowl, orange trim lines, a moulded seat and the canopy frame
    (the halo arch) -- after ref_cockpit_hypercar.png."""
    m = Mesh(5.0)
    cw, ct = hyper_canopy({})
    # tub: an open U section from behind the seat to the nose, sills rolling over at the top
    rings = []
    for x in np.linspace(-0.75, 1.05, 10):
        sill = 0.60 + 0.04 * float(sstep(0.2, 1.0, x))
        prof = [(-0.56, sill - 0.04), (-0.52, sill + 0.02), (-0.46, sill + 0.01), (-0.44, sill - 0.10), (-0.42, 0.30),
                (-0.36, 0.21), (-0.20, 0.20), (0.20, 0.20), (0.36, 0.21), (0.42, 0.30), (0.44, sill - 0.10), (0.46, sill + 0.01),
                (0.52, sill + 0.02), (0.56, sill - 0.04)]
        rings.append([(x, y, z) for y, z in prof])
    m.sheet(rings, uv='carbon', shade=0.75)
    # orange trim along both sill tops and round the foot well
    for s in (-1, 1):
        m.quad((-0.70, s * 0.462, 0.605), (1.0, s * 0.462, 0.645), (1.0, s * 0.475, 0.648), (-0.70, s * 0.475, 0.608), uv='orange', shade=1.5)
        m.quad((-0.40, s * 0.30, 0.205), (0.20, s * 0.30, 0.205), (0.20, s * 0.32, 0.205), (-0.40, s * 0.32, 0.205), uv='orange', shade=1.4)
    m.quad((0.20, -0.32, 0.206), (0.22, -0.32, 0.206), (0.22, 0.32, 0.206), (0.20, 0.32, 0.206), uv='orange', shade=1.4)
    # dash cowl: rises from the sills to a hump over the instruments; its face towards the driver is at x 0.24
    cow = []
    for x in np.linspace(0.24, 1.05, 6):
        prof = []
        for y in np.linspace(-0.46, 0.46, 13):
            hump = 0.035 * np.exp(-(y / 0.22) ** 2)
            prof.append((x, y, 0.63 + hump + 0.03 * float(sstep(0.24, 0.7, x)) - 0.05 * (abs(y) / 0.46) ** 2))
        cow.append(prof)
    m.sheet(cow, uv='grey', shade=0.85)
    face = [[(0.24, y, 0.30) for y in np.linspace(-0.46, 0.46, 13)], cow[0]]
    m.sheet(face, uv='carbon', shade=0.6)
    m.quad((0.235, -0.30, 0.585), (0.235, 0.30, 0.585), (0.235, 0.30, 0.597), (0.235, -0.30, 0.597), uv='orange', shade=1.6)   # trim line under the hump
    # housing behind the dash display (the game draws the display on its face)
    m.box((0.445, -0.225, 0.600), (0.56, 0.225, 0.715), uv='carbon', shade=0.55)
    # side panels with switches and two small vents each
    for s in (-1, 1):
        m.box((0.24, s * 0.30 - 0.07, 0.40), (0.30, s * 0.30 + 0.07, 0.55), uv='carbon', shade=0.65)
        for k in range(3):
            m.box((0.232, s * (0.26 + k * 0.035) - 0.010, 0.47), (0.24, s * (0.26 + k * 0.035) + 0.010, 0.49), uv=('green', 'red', 'blue')[k], shade=1.5)
        m.cyl((0.24, s * 0.38, 0.60), (0.225, s * 0.38, 0.60), 0.03, sides=12, uv='rubber', shade=0.7)
    # moulded seat: back, cushion, side bolsters with orange edges, head wings
    m.quad((-0.64, -0.20, 0.26), (-0.64, 0.20, 0.26), (-0.50, 0.20, 0.90), (-0.50, -0.20, 0.90), uv='grey', shade=0.7)
    m.quad((-0.62, -0.20, 0.27), (-0.62, 0.20, 0.27), (0.05, 0.20, 0.33), (0.05, -0.20, 0.33), uv='grey', shade=0.75)
    for s in (-1, 1):
        m.box((-0.64, s * 0.24 - 0.04, 0.27), (0.05, s * 0.24 + 0.04, 0.48), uv='carbon', shade=0.7)
        m.box((-0.62, s * 0.24 - 0.045, 0.48), (0.05, s * 0.24 + 0.045, 0.495), uv='orange', shade=1.3)
        m.box((-0.62, s * 0.19 - 0.05, 0.66), (-0.42, s * 0.19 + 0.05, 0.96), uv='carbon', shade=0.6)
    # canopy frame: an arch over each side of the canopy and the halo spine over the driver's head
    r = 0.030
    for s in (-1, 1):
        pts = [(x, s * (cw(x) - 0.03), 0.60 + (ct(x) - 0.60) * 0.15) for x in np.linspace(1.0, 0.60, 4)]
        pts += [(x, s * (cw(x) - 0.04), ct(x) - 0.05) for x in np.linspace(0.40, -0.55, 6)]
        m.tube(pts, r, sides=8, uv='grey', shade=0.9)
    spine = [(x, 0.0, ct(x) - 0.04) for x in np.linspace(0.40, -0.55, 6)]       # over the head only: the windscreen stays clear
    m.tube(spine, r * 1.25, sides=8, uv='grey', shade=0.95)
    for x in (0.35, -0.50):
        m.tube([(x, -(cw(x) - 0.04), ct(x) - 0.05), (x, 0.0, ct(x) - 0.04), (x, cw(x) - 0.04, ct(x) - 0.05)], r * 0.9, sides=8, uv='grey', shade=0.85)
    # pedals in the foot well
    for k, y in enumerate((-0.12, 0.0, 0.12)):
        m.box((0.95, y - 0.035, 0.24), (0.98, y + 0.035, 0.36), uv='grey' if k else 'carbon', shade=0.8)
    return m


def hd_gt_interior():
    """Two-seat tub, driver on the right: carbon door cards, padded cage with a window net, dash with
    the cluster binnacle and a centre screen on a mount, switch panel -- after ref_cockpit_gt3.png."""
    m = Mesh(4.7)
    iw = 0.76; belt = 0.87; fl = 0.22
    m.quad((0.95, -iw, fl), (0.95, iw, fl), (-1.05, iw, fl), (-1.05, -iw, fl), uv='rubber', shade=0.6)
    for s in (-1, 1):
        m.quad((0.95, s * iw, fl), (-1.05, s * iw, fl), (-1.05, s * iw, belt), (0.95, s * iw, belt), uv='carbon', shade=0.8)
        m.quad((0.95, s * iw, belt), (-1.05, s * iw, belt), (-1.05, s * (iw + 0.06), belt + 0.01), (0.95, s * (iw + 0.06), belt + 0.01), uv='grey', shade=1.0)
        m.box((-0.40, s * (iw - 0.02) - 0.01, 0.40), (0.30, s * (iw - 0.02) + 0.01, 0.70), uv='carbon', shade=0.65)    # door card
        m.box((-0.20, s * (iw - 0.035) - 0.008, 0.55), (0.05, s * (iw - 0.035) + 0.008, 0.58), uv='blue', shade=1.3)  # door pull
    m.quad((-1.0, -iw, fl), (-1.0, iw, fl), (-1.0, iw, belt + 0.02), (-1.0, -iw, belt + 0.02), uv='carbon', shade=0.6)
    # dash: carbon face, top sloping up to the windscreen
    m.box((0.42, -iw, fl), (0.95, iw, 0.80), uv='carbon', shade=0.7)
    m.part(np.array([[0.42, -iw, 0.80], [0.42, iw, 0.80], [0.86, iw, 0.885], [0.86, -iw, 0.885]]), [[0, 1, 2], [0, 2, 3]], uv='grey', flat=True, shade=0.9)
    # cluster binnacle in front of the driver (the cluster is drawn on its face at x 0.45)
    m.box((0.495, -0.56, 0.84), (0.66, -0.20, 1.005), uv='carbon', shade=0.6)
    m.part(np.array([[0.47, -0.58, 1.02], [0.47, -0.18, 1.02], [0.68, -0.18, 1.005], [0.68, -0.58, 1.005]]), [[0, 1, 2], [0, 2, 3]], uv='carbon', flat=True, shade=0.8)
    # centre screen mount (the screen sits at (0.40, 0.02, 0.985))
    m.box((0.44, -0.03, 0.80), (0.50, 0.07, 0.93), uv='grey', shade=0.7)
    # centre console: switch panel with rows of coloured buttons, tunnel, gear paddle box
    m.box((0.30, -0.13, 0.50), (0.42, 0.13, 0.80), uv='carbon', shade=0.7)
    cols = ('green', 'red', 'blue', 'yellow', 'orange', 'white')
    for row in range(3):
        for k in range(5):
            m.box((0.295, -0.10 + k * 0.05 - 0.015, 0.58 + row * 0.06), (0.305, -0.10 + k * 0.05 + 0.015, 0.60 + row * 0.06), uv=cols[(row + k) % 6], shade=1.4)
    m.box((-0.55, -0.10, fl), (0.30, 0.10, 0.42), uv='carbon', shade=0.6)
    m.cyl((-0.05, 0.0, 0.42), (-0.05, 0.0, 0.50), 0.03, sides=10, uv='red', shade=1.2)        # fire extinguisher button
    # driver's seat: shell with bolsters and head wings, harness
    m.quad((-0.66, -0.56, 0.30), (-0.66, -0.20, 0.30), (-0.56, -0.20, 1.14), (-0.56, -0.56, 1.14), uv='grey', shade=0.65)
    m.quad((-0.62, -0.56, 0.31), (-0.62, -0.20, 0.31), (0.02, -0.20, 0.36), (0.02, -0.56, 0.36), uv='grey', shade=0.7)
    for s in (-0.16, 0.16):
        m.box((-0.62, -0.38 + s - 0.035, 0.30), (0.02, -0.38 + s + 0.035, 0.70), uv='carbon', shade=0.7)
        m.box((-0.62, -0.38 + s * 1.15 - 0.03, 0.86), (-0.40, -0.38 + s * 1.15 + 0.03, 1.14), uv='carbon', shade=0.5)
    for y in (-0.46, -0.30):
        m.quad((-0.55, y - 0.025, 1.0), (-0.55, y + 0.025, 1.0), (-0.30, y + 0.025, 0.55), (-0.30, y - 0.025, 0.55), uv='blue', shade=1.0)
    # roll cage: rounder, thicker tubes, padding (orange) where a helmet could touch
    r = 0.026; cg = 'grey'
    roof = 1.215; yr = 0.565
    A = lambda s: [(0.74, s * 0.70, 0.89), (0.10, s * yr, roof), (-0.58, s * yr, roof), (-0.60, s * 0.70, fl)]
    for s in (-1, 1):
        m.tube(A(s), r, sides=8, uv=cg)
        m.tube([(0.40, s * 0.72, 0.36), (-0.58, s * 0.72, 0.80)], r * 0.9, sides=8, uv=cg)
        m.tube([(0.40, s * 0.72, 0.80), (-0.58, s * 0.72, 0.36)], r * 0.9, sides=8, uv=cg)
    m.tube([(-0.58, -yr, roof), (-0.58, yr, roof)], r, sides=8, uv=cg)
    m.tube([(-0.22, -yr, roof), (-0.22, yr, roof)], r * 0.9, sides=8, uv=cg)
    m.tube([(-0.58, -yr, roof), (0.10, yr, roof)], r * 0.8, sides=8, uv=cg)
    m.tube([(-0.60, -0.70, fl), (-0.59, 0.66, roof - 0.04)], r * 0.9, sides=8, uv=cg)
    m.tube([(-0.59, -0.68, 0.74), (-0.59, 0.68, 0.74)], r * 0.9, sides=8, uv=cg)
    m.tube([(-0.40, -yr + 0.01, roof - 0.01), (0.0, -yr + 0.02, roof - 0.04)], r * 1.6, sides=8, uv='orange', shade=0.9)   # padding
    # window net on the driver's door (a grid of straps between the cage bars)
    for k in range(6):
        z = 0.90 + k * 0.05
        m.tube([(-0.45, -0.715, z), (0.20, -0.70, z + 0.02)], 0.004, sides=4, uv='rubber', shade=0.8)
    for k in range(7):
        x = -0.45 + k * 0.11
        m.tube([(x, -0.715, 0.90), (x + 0.02, -0.705, 1.17)], 0.004, sides=4, uv='rubber', shade=0.8)
    # passenger side: floor bars and the old instrument pod's bar (no seat)
    m.tube([(0.40, 0.10, 0.30), (-0.58, 0.70, 0.30)], r * 0.8, sides=8, uv=cg)
    m.tube([(0.40, 0.70, 0.30), (-0.58, 0.10, 0.30)], r * 0.8, sides=8, uv=cg)
    return m


def hd_steering(hyper):
    """Carbon yoke / wheel with coloured buttons, two rotary knobs, grips and paddles -- after
    ref_steering_wheel.png. Face towards -X; the game draws the screen at x -0.012."""
    m = Mesh(1.0)
    if hyper:
        outline = [(0.10, 0.09), (0.16, 0.08), (0.175, 0.02), (0.168, -0.05), (0.13, -0.095), (0.06, -0.10),
                   (-0.06, -0.10), (-0.13, -0.095), (-0.168, -0.05), (-0.175, 0.02), (-0.16, 0.08), (-0.10, 0.09)]
        for a, b in zip(outline, outline[1:]):
            m.tube([(0.004, a[0], a[1]), (0.004, b[0], b[1])], 0.017, sides=8, uv='carbon', shade=0.95)
        for s in (-1, 1):                                    # rubber grips
            m.tube([(0.002, s * 0.168, 0.06), (0.002, s * 0.174, 0.0), (0.002, s * 0.166, -0.05)], 0.024, sides=8, uv='rubber', shade=0.9)
        m.box((-0.006, -0.12, -0.085), (0.025, 0.12, 0.075), uv='carbon', shade=0.8)          # plate
    else:
        n = 24
        for k in range(n):
            a0 = k * 2 * np.pi / n; a1 = (k + 1) * 2 * np.pi / n
            if 4.25 < (a0 + a1) / 2 < 5.17:
                continue
            m.tube([(0.004, 0.165 * np.cos(a0), 0.165 * np.sin(a0)), (0.004, 0.165 * np.cos(a1), 0.165 * np.sin(a1))], 0.018, sides=8,
                   uv='rubber' if 0.4 < (a0 + a1) / 2 < 2.7 or 3.6 < (a0 + a1) / 2 < 4.2 else 'carbon', shade=0.95)
        m.tube([(0.004, 0.165 * np.cos(4.19), 0.165 * np.sin(4.19)), (0.004, 0.165 * np.cos(5.24), 0.165 * np.sin(5.24))], 0.018, sides=8, uv='carbon')
        m.box((-0.006, -0.155, -0.035), (0.02, 0.155, 0.045), uv='carbon', shade=0.75)
        m.box((-0.006, -0.03, -0.14), (0.02, 0.03, -0.03), uv='carbon', shade=0.75)
        m.box((-0.008, -0.075, -0.055), (0.024, 0.075, 0.06), uv='carbon', shade=0.7)
    # buttons beside the screen and two rotary knobs under it
    W = 0.10 if hyper else 0.085
    cols = ('green', 'red', 'blue', 'white')
    for s in (-1, 1):
        for k in range(4):
            y = s * (W + 0.008 + (k % 2) * 0.026); z = 0.035 - (k // 2) * 0.03
            m.box((-0.012, y - 0.009, z - 0.009), (0.0, y + 0.009, z + 0.009), uv=cols[(k + (s > 0) * 2) % 4], shade=1.4)
        m.cyl((-0.004, s * 0.045, -0.065), (-0.022, s * 0.045, -0.065), 0.014, sides=10, uv='red' if s < 0 else 'yellow', shade=1.3)
        m.box((0.03, s * 0.06 - 0.02, -0.02), (0.04, s * 0.06 + 0.02, 0.09), uv='grey', shade=0.9)       # paddles behind
    m.tube([(0.03, 0, 0), (0.30, 0, -0.03)], 0.025, sides=8, uv='grey', shade=0.6)
    return m


def hd_wheel(spokes, sides, pairs=False):
    """Road wheel: tyre with rounded shoulders, dark 10-spoke rim, centre lock nut and a red caliper
    behind the spokes -- after ref_race_wheel.png. Axle along Y, outer face at +Y."""
    m = Mesh(1.0)
    R = 0.355; hw = 0.155; rr = 0.235
    prof = [(-hw, rr + 0.02), (-hw, R - 0.03), (-hw + 0.03, R), (hw - 0.03, R), (hw, R - 0.03), (hw, rr + 0.02)]
    rings = []
    for k in range(sides + 1):
        a = k * 2 * np.pi / sides
        rings.append([(r_ * np.cos(a), y, r_ * np.sin(a)) for y, r_ in prof])
    m.sheet(rings, uv='rubber', shade=0.9)
    ang = [k * 2 * np.pi / sides for k in range(sides + 1)]
    lip = [[(rr * np.cos(a), hw - 0.012, rr * np.sin(a)), ((rr + 0.022) * np.cos(a), hw + 0.002, (rr + 0.022) * np.sin(a))] for a in ang]
    m.sheet([[lip[k][0], lip[k][1]] for k in range(sides + 1)], uv='grey', shade=1.1)
    barrel = [[(rr * np.cos(a), y, rr * np.sin(a)) for y in (hw - 0.012, -hw)] for a in ang]
    m.sheet(barrel, uv='rubber', shade=0.5)
    for k in range(spokes):                                   # spokes (pairs, like the reference)
        a = k * 2 * np.pi / spokes
        for da in ((-0.06, 0.06) if pairs else (0.0,)):
            c, s_ = np.cos(a + da), np.sin(a + da)
            m.tube([(0.045 * c, hw - 0.03, 0.045 * s_), (rr * c, hw - 0.012, rr * s_)], 0.010, sides=5, uv='grey', shade=0.95)
    m.cyl((0, hw - 0.06, 0), (0, hw - 0.005, 0), 0.05, sides=12, uv='grey', shade=1.1)          # hub
    m.cyl((0, hw - 0.005, 0), (0, hw + 0.012, 0), 0.028, sides=6, uv='red', shade=1.2)          # centre-lock nut
    m.cyl((0, -0.02, 0), (0, 0.0, 0), 0.20, sides=sides, uv='cage', shade=0.7)                  # brake disc
    m.box((0.12, -0.035, -0.07), (0.205, 0.035, 0.07), uv='caliper', shade=1.2)                 # caliper (in front, up)
    return m


def main_hd_extras():
    S = 256
    p = Paint(5.0, S)
    p.base((0.1, 0.1, 0.1))
    p.swatches(dict(glass=(0.08, 0.11, 0.16), white=(0.92, 0.92, 0.88), red=(0.85, 0.08, 0.06), carbon=(0.09, 0.09, 0.10),
                    cage=(0.50, 0.52, 0.56), second=(0.8, 0.1, 0.1), panel=(0.9, 0.1, 0.1), paint=(0.6, 0.6, 0.6), **HD_SWATCH))
    t = p.img
    NEW_DASH = (0.0, 0.0, 0.0, 0.0, 2)        # EMX1 flags bit 1: use the new instruments with these
    print('cockpit_hypercar', write_emm(os.path.join(OUT, 'cockpit_hypercar.emm'), [(hd_hyper_interior(), t, S)], emx=NEW_DASH))
    print('cockpit_gt3', write_emm(os.path.join(OUT, 'cockpit_gt3.emm'), [(hd_gt_interior(), t, S)], emx=NEW_DASH))
    print('steering_hyper', write_emm(os.path.join(OUT, 'steering_hyper.emm'), [(hd_steering(True), t, S)], emx=NEW_DASH))
    print('steering_gt3', write_emm(os.path.join(OUT, 'steering_gt3.emm'), [(hd_steering(False), t, S)], emx=NEW_DASH))
    # used within 12 m / 28 m / 160 m of the camera, four per car: kept light
    print('race_wheel', write_emm(os.path.join(OUT, 'race_wheel.emm'), [(hd_wheel(10, 16), t, S), (build_wheel(12), t, 128), (build_wheel(6), t, 64)]))


if __name__ == '__main__':
    main()
