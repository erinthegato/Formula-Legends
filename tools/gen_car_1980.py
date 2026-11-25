"""Formula Legends -- the 1980 ground-effect "wing car", built in code (no Blender needed).

    python3 tools/gen_car_1980.py            # writes tools/blender/f1_1980_car.json

The same kind of low-poly, flat-shaded body as the Blender cars (tools/blender/f1_eras.py), in the same
JSON format, so tools/gen_f1.py turns it into the ten 1980 cars (car_m98_*.emm: the era's internal id
is still "90s") with their liveries. Three levels of detail. +X forward, +Y left, +Z up, metres.

1980 rules (FIA): 3.0 litres without a compressor or 1.5 litres with one, 575 kg. The shape follows the
cars of that year (Williams FW07B: 2692 mm wheelbase, 1738 / 1600 mm track, Goodyear 25 in fronts and
wide rears on 13 in rims): a narrow aluminium monocoque with the driver well forward, a wedge nose with
small winglets, full-length venturi sidepods sealed to the track by sliding skirts, a low engine cover
over an exposed Cosworth-style V8 and its gearbox, a tall roll hoop, and a wide rear wing hung behind
the gearbox. The wing's top flap is left out: the game draws it (flap_90s.emm).

Materials are the game's swatches: 'livery' (the team livery, projected), 'second' (the team's second
colour), and '<swatch>_<shade %>' for darker or lighter flat colours.
"""
import json, math, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'blender', 'f1_1980_car.json')

CAR = dict(xf=1.32, xr=-1.37, R=0.335, Rf=0.30, yf=0.869, yr=0.80, wf=0.28, wr=0.42, rim=0.17)


class Builder:
    """collects triangles; every face is turned to point away from a reference point (the middle of
    the part, or of the section it belongs to), so closed shapes face outwards"""
    def __init__(self, lod):
        self.lod = lod
        self.tris = []          # (3x3 points, material, smooth group or None)
        self.group = None       # set to a name: the faces added share smoothed normals

    @staticmethod
    def mat(m):
        if isinstance(m, tuple):
            sw, sh = m
            return sw if sh == 100 else '%s_%d' % (sw, sh)
        return m

    def face(self, pts, mat, ref, inward=False):
        pts = [np.array(p, float) for p in pts]
        c = sum(pts) / len(pts)
        n = np.cross(pts[1] - pts[0], pts[2] - pts[0])
        if len(pts) > 3:
            n = n + np.cross(pts[2] - pts[0], pts[3] - pts[0])
        out = c - np.array(ref, float)
        if (np.dot(n, out) < 0) != inward:
            pts = pts[::-1]
        m = self.mat(mat)
        for k in range(1, len(pts) - 1):
            self.tris.append((np.array([pts[0], pts[k], pts[k + 1]]), m, self.group))

    def mesh(self, V, F, mat, ref=None):
        V = [np.array(v, float) for v in V]
        ref = sum(V) / len(V) if ref is None else ref
        for f in F:
            self.face([V[i] for i in f], mat, ref)

    # ---- the same shapes as the Blender builder
    def loft(self, secs, mat, yc=0.0, n=8, e=0.6):
        """rounded sections (x, half width, bottom, top) along X, capped at both ends"""
        rings = []
        for (x, w, z0, z1) in secs:
            zc = (z0 + z1) / 2; h = (z1 - z0) / 2
            r = []
            for i in range(n):
                a = 2 * math.pi * (i + 0.5) / n
                ca, sa = math.cos(a), math.sin(a)
                r.append((x, yc + w * math.copysign(abs(ca) ** e, ca), zc + h * math.copysign(abs(sa) ** e, sa)))
            rings.append((r, (x, yc, zc)))
        for k in range(len(rings) - 1):
            (A, ca), (B, cb) = rings[k], rings[k + 1]
            ref = (np.array(ca) + np.array(cb)) / 2
            for i in range(n):
                j = (i + 1) % n
                self.face([A[i], A[j], B[j], B[i]], mat, ref)
        c0, c1 = np.array(rings[0][1]), np.array(rings[1][1])
        cl, cp = np.array(rings[-1][1]), np.array(rings[-2][1])
        self.face(rings[0][0], mat, c0 + (c1 - c0) * 0.5)          # each end faces away from the rest
        self.face(rings[-1][0], mat, cl + (cp - cl) * 0.5)

    def prism(self, pts, depth, yc, mat):
        """an (x, z) outline pushed out sideways"""
        n = len(pts)
        V = [(x, yc - depth / 2, z) for (x, z) in pts] + [(x, yc + depth / 2, z) for (x, z) in pts]
        F = [tuple(range(n)), tuple(range(n, 2 * n))] + [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
        self.mesh(V, F, mat)

    def box(self, lo, hi, mat):
        V = [(x, y, z) for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
        F = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
        self.mesh(V, F, mat)

    def rod(self, a, b, r, mat, sides=4):
        a = np.array(a, float); b = np.array(b, float)
        t = (b - a) / np.linalg.norm(b - a)
        ref = np.array([0, 0, 1.0]) if abs(t[2]) < 0.9 else np.array([1.0, 0, 0])
        u = np.cross(t, ref); u /= np.linalg.norm(u); w = np.cross(t, u)
        ring = lambda p: [p + r * (math.cos(2 * math.pi * k / sides + math.pi / 4) * u + math.sin(2 * math.pi * k / sides + math.pi / 4) * w) for k in range(sides)]
        A, B = ring(a), ring(b)
        mid = (a + b) / 2
        for k in range(sides):
            j = (k + 1) % sides
            self.face([A[k], A[j], B[j], B[k]], mat, mid)
        self.face(A, mat, mid); self.face(B, mat, mid)

    def disc(self, pts, centre, mat, below):
        """a flat polygon facing up (or down when below)"""
        ref = np.array(centre, float) + np.array((0, 0, 1.0 if below else -1.0))
        self.face(pts, mat, ref)

    def json(self):
        """triangles, normals (flat, or averaged over a smooth group), material indices"""
        mats = []
        keep = []
        for tri, m, g in self.tris:
            n = np.cross(tri[1] - tri[0], tri[2] - tri[0])
            a = np.linalg.norm(n)
            if a < 1e-10:
                continue
            keep.append((tri, m, g, n / a, a))
        acc = {}
        for tri, m, g, n, a in keep:
            if g is None:
                continue
            for p in tri:
                k = (g, tuple(np.round(p, 4)))
                acc[k] = acc.get(k, 0) + n * a
        P, N, M, G = [], [], [], []
        for tri, m, g, n, a in keep:
            if m not in mats:
                mats.append(m)
            P += [round(float(c), 4) for c in tri.reshape(-1)]
            for p in tri:
                v = n if g is None else acc[(g, tuple(np.round(p, 4)))]
                v = v / max(np.linalg.norm(v), 1e-9)
                N += [round(float(c), 3) for c in v]
            M.append(mats.index(m)); G.append(0)
        return mats, dict(tris=len(M), p=P, n=N, m=M, g=G)


# ---------------------------------------------------------------------------------------------- the car
def ellipse_pts(cx, a, hw, z, k):
    return [(cx + a * math.copysign(abs(math.cos(t)) ** 0.6, math.cos(t)),
             hw * math.copysign(abs(math.sin(t)) ** 0.6, math.sin(t)), z) for t in (2 * math.pi * i / k for i in range(k))]


def monocoque(B, n):
    """the nose and the tub, open over the cockpit: the outer skin, the rim round the opening, the
    inside walls and a dark floor (the driver and the steering wheel sit in it)"""
    nose = [(2.10, 0.06, 0.12, 0.20), (1.90, 0.10, 0.10, 0.28), (1.60, 0.15, 0.09, 0.38), (1.30, 0.20, 0.08, 0.48),
            (1.05, 0.24, 0.08, 0.57)]
    B.loft(nose, 'livery', n=n)
    B.loft([(0.18, 0.28, 0.08, 0.64), (-0.15, 0.27, 0.08, 0.66)], 'livery', n=n)
    # the cockpit section, x 1.05 -> 0.18: a U open at the top
    xs = [1.05, 0.80, 0.50, 0.18] if B.lod == 0 else [1.05, 0.60, 0.18]
    hw = lambda x: 0.24 + (0.28 - 0.24) * (1.05 - x) / 0.87
    top = lambda x: 0.57 + (0.64 - 0.57) * (1.05 - x) / 0.87
    m = 5 if B.lod == 0 else 3                      # points round each half of the bottom
    def outer(x):
        w, z1 = hw(x), top(x)
        pts = [(x, w, z1), (x, w, 0.30)]
        for i in range(1, m):
            a = math.pi / 2 * i / m
            pts.append((x, w * math.cos(a), 0.30 - 0.22 * math.sin(a)))
        pts.append((x, 0.0, 0.08))
        for i in range(m - 1, 0, -1):
            a = math.pi / 2 * i / m
            pts.append((x, -w * math.cos(a), 0.30 - 0.22 * math.sin(a)))
        pts += [(x, -w, 0.30), (x, -w, z1)]
        return pts
    def inner(x):
        w = hw(x) - 0.045
        if x > 0.95: w *= 0.55                      # the opening narrows to a rounded front
        z1 = top(x)
        return [(x, w, z1), (x, w, 0.26), (x, -w, 0.26), (x, -w, z1)]
    for a, b in zip(xs, xs[1:]):
        A, Bo = outer(a), outer(b)
        ref = np.array(((a + b) / 2, 0.0, 0.32))
        for i in range(len(A) - 1):
            B.face([A[i], A[i + 1], Bo[i + 1], Bo[i]], 'livery', ref)
        Ai, Bi = inner(a), inner(b)
        for i in range(3):              # the inside walls and floor, facing in
            B.face([Ai[i], Ai[i + 1], Bi[i + 1], Bi[i]], ('carbon', 35), np.array(((a + b) / 2, 0.0, 0.40)), inward=True)
        for s, o, ii in ((1, 0, 0), (-1, len(A) - 1, 3)):    # the rim: outer top edge to inner top edge
            B.face([A[o], Bo[o], Bi[ii], Ai[ii]], 'livery', np.array(((a + b) / 2, s * hw(a), 0.0)))
    # the front and back walls of the opening (dark) are the ends of the nose and tub lofts


def car_1980(B):
    lod = B.lod; n = {0: 8, 1: 6, 2: 4}[lod]
    monocoque(B, n)
    # the fuel tank's top and a low engine cover over the front of the engine (no airbox)
    B.loft([(0.18, 0.20, 0.55, 0.70), (-0.15, 0.22, 0.45, 0.72), (-0.55, 0.20, 0.30, 0.62),
            (-0.95, 0.15, 0.22, 0.47), (-1.10, 0.11, 0.20, 0.40)], 'livery', n=n)
    # the exposed V8 and the gearbox behind it
    B.box((-1.30, -0.17, 0.12), (-0.60, 0.17, 0.40), ('grey', 70))
    B.box((-1.85, -0.13, 0.10), (-1.30, 0.13, 0.34), ('grey', 55))
    if lod < 2:
        for s_ in (1, -1):
            B.box((-1.25, s_ * 0.06 - 0.05, 0.40), (-0.80, s_ * 0.06 + 0.05, 0.47), ('red', 80))
        B.box((-1.15, -0.09, 0.47), (-0.85, 0.09, 0.52), ('grey', 110))
        B.box((-1.95, -0.10, 0.12), (-1.85, 0.10, 0.30), ('grey', 80))
    if lod == 0:
        for s_ in (1, -1):        # the exhausts sweep out of the vee and back over the gearbox
            for k in range(2):
                y = s_ * (0.10 + 0.04 * k)
                B.rod((-0.85 - 0.12 * k, y, 0.40), (-1.70, s_ * 0.18, 0.40 + 0.02 * k), 0.022, ('grey', 120))
    for s in (1, -1):
        # venturi sidepods from behind the front wheels to the rear wheels, flat-topped, the radiator
        # intake at the front; skirts along the bottom of their outer walls seal them to the track
        B.loft([(1.02, 0.16, 0.06, 0.30), (0.85, 0.20, 0.04, 0.40), (0.00, 0.20, 0.04, 0.40),
                (-0.60, 0.19, 0.04, 0.37), (-0.95, 0.15, 0.04, 0.30)], 'livery', yc=s * 0.465, n=n, e=0.25)
        B.prism([(1.00, 0.0), (-0.95, 0.0), (-0.95, 0.10), (1.00, 0.10)], 0.012, s * 0.672, ('carbon', 60))
        if lod < 2:
            B.box((1.02, s * 0.465 - 0.13, 0.10), (1.045, s * 0.465 + 0.13, 0.33), ('carbon', 30))
        # winglets on the sides of the nose, small endplates
        B.prism([(2.06, 0.13), (1.95, 0.17), (1.80, 0.18), (1.74, 0.16), (1.95, 0.12)], 0.48, s * 0.34, 'second')
        B.prism([(2.08, 0.08), (2.08, 0.22), (1.72, 0.22), (1.72, 0.08)], 0.012, s * 0.585, 'livery')
        # the rear wing's big endplates, from the gearbox up past the wing
        B.prism([(-1.62, 0.42), (-1.62, 0.98), (-2.22, 0.98), (-2.22, 0.42)], 0.015, s * 0.55, 'livery')
        if lod < 2:      # twin struts from the gearbox to the wing
            B.prism([(-1.62, 0.30), (-1.72, 0.82), (-1.82, 0.82), (-1.78, 0.30)], 0.02, s * 0.12, 'carbon')
    # the rear wing's main plane (the top flap is the game's flap_90s model)
    B.prism([(-1.66, 0.82), (-1.72, 0.85), (-1.90, 0.86), (-2.02, 0.84), (-1.85, 0.81)], 1.085, 0, 'livery')
    B.box((-1.00, -0.28, 0.035), (1.30, 0.28, 0.08), 'carbon')
    # the roll hoop behind the driver's head, braced back to the engine
    if lod < 2:
        for s_ in (1, -1):
            B.rod((0.05, s_ * 0.14, 0.62), (0.03, s_ * 0.10, 0.98), 0.022, ('grey', 100))
            B.rod((0.03, s_ * 0.10, 0.96), (-0.45, s_ * 0.10, 0.60), 0.016, ('grey', 100))
        B.rod((0.03, -0.10, 0.98), (0.03, 0.10, 0.98), 0.022, ('grey', 100))
    else:
        B.box((0.0, -0.10, 0.62), (0.06, 0.10, 0.98), ('grey', 100))
    # mirrors on stalks beside the cockpit
    if lod < 2:
        for s in (1, -1):
            B.box((0.78, s * 0.40 - 0.06, 0.635), (0.82, s * 0.40 + 0.06, 0.685), 'livery')
            B.rod((0.83, s * 0.24, 0.55), (0.80, s * 0.38, 0.65), 0.012, 'carbon')
    # suspension: wishbones out to the uprights
    if lod == 0:
        c = CAR; r = 0.012
        for s in (1, -1):
            for dx in (0.15, -0.15):
                B.rod((c['xf'] + dx, s * 0.14, c['Rf'] + 0.15), (c['xf'], s * (c['yf'] - 0.12), c['Rf'] + 0.10), r, 'carbon')
                B.rod((c['xf'] + dx * 1.3, s * 0.14, c['Rf'] - 0.05), (c['xf'], s * (c['yf'] - 0.12), c['Rf'] - 0.10), r, 'carbon')
                B.rod((c['xr'] + dx, s * 0.14, c['R'] + 0.10), (c['xr'], s * (c['yr'] - 0.19), c['R'] + 0.10), r, 'carbon')
                B.rod((c['xr'] + dx * 1.3, s * 0.14, c['R'] - 0.14), (c['xr'], s * (c['yr'] - 0.19), c['R'] - 0.14), r, 'carbon')


def main():
    mats = []; lods = []
    for lod in (0, 1, 2):
        B = Builder(lod)
        car_1980(B)
        m, d = B.json()
        # one material list for all three levels
        remap = []
        for k in m:
            if k not in mats:
                mats.append(k)
            remap.append(mats.index(k))
        d['m'] = [remap[i] for i in d['m']]
        lods.append(d)
    with open(OUT, 'w') as f:
        json.dump(dict(format='FL-blender-1', car='1980', axes='+X forward, +Y left, +Z up, metres',
                       materials=mats, palette={}, lods=lods), f, separators=(',', ':'))
    print('1980 car: triangles', [l['tris'] for l in lods], '->', OUT)


if __name__ == '__main__':
    main()
