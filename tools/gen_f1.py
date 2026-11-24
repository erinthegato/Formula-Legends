#!/usr/bin/env python3
"""Procedural Formula 1 cars for Formula Legends -- four generations, all built from code.

  * 1990s (1998): narrow 1.8 m cars, raised nose with the front wing hung under it, tall airbox,
    high cockpit sides, boxy sidepods, a tall two-plane rear wing, grooved tyres on 13-inch rims
  * 2010 (V8): long high nose, wide front wing with cascades, a big shark-fin engine cover running
    into a narrow tall rear wing, tight "coke bottle" sidepods
  * 2016 (hybrid): long low "finger" nose stepping down from the chassis, slim sidepods, a small fin,
    narrow rear wing with the DRS flap, oval airbox with the hybrid-era camera pods
  * 2025 (ground effect): 2.0 m wide, the halo, a low wide nose and a front wing that sweeps up at the
    tips, big undercut sidepods over a wide floor, beam wing, rounded rear-wing tips, 18-inch wheels
    with covers

Also: the steering wheels and road wheels of each era, and each era's DRS flap (drawn by the game
on its own, so it can open). Liveries are painted on the side / top projection like EMR's cars.

Output (PS Vita set, HD/models): car_<id>.emm, wheel_<era>.emm, steering_<era>.emm, flap_<era>.emm,
livery_career_<era>.lvy.     Usage: python3 tools/gen_f1.py [preview_dir]

Axes: +X forward, +Y left, +Z up, metres, origin on the ground under the middle of the car.
"""
import os, re, sys, zlib, json, struct
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_cars as G
from gen_cars import Mesh, Paint, write_emm, rgb, mix, swizzle, SW, TOP_V, SIDE_V, ZMAX, YMAX

ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, 'HD', 'models')
G.SW_DIV = 16

ERAS = ['90s', 'v8', 'hybrid', 'ge']

# ---------------------------------------------------------------------------------------------- era shapes
# x stations are metres from the middle of the car. wheel_y = the wheel centre's distance from the middle.
E = {
    '90s': dict(L=4.45, xf=1.52, xr=-1.43, R=0.330, wf=0.285, wr=0.365, wheel_y=0.73, wheel_yr=0.71,
                nose_tip=(2.22, 0.36), nose_hw=0.07, chassis_hw=0.29, chassis_top=0.64, nose_low=0.20,
                cockpit=(0.42, -0.38), rim=0.74, head=(-0.20, 0.86), airbox=(-0.48, 1.00, 0.16), cover_end=(-1.62, 0.44),
                pod=(0.28, -1.05, 0.70, 0.56), pod_end_y=0.42, floor_hw=0.70,
                fw=(1.92, 2.28, 0.82, 0.085, 'flat'), fw_endplate=0.20,
                rw=(-1.80, -2.15, 0.50, 0.80, 0.18), rw2=True, beam=False, fin=0.0, halo=False, high_nose=True, finger=False,
                drs=False, groove=True, cover=False, mirrors=(0.30, 0.42, 0.72)),
    # 2010: the shape of the Blender car (tools/blender/f1_v8_car.py, the first-generation car); these
    # numbers place its wheels, DRS flap and livery lines
    'v8':  dict(L=4.95, xf=1.60, xr=-1.60, R=0.330, wf=0.305, wr=0.38, wheel_y=0.725, wheel_yr=0.71,
                nose_tip=(2.55, 0.38), nose_hw=0.08, chassis_hw=0.30, chassis_top=0.68, nose_low=0.26,
                cockpit=(0.78, 0.08), rim=0.69, head=(0.20, 0.72), airbox=(-0.20, 0.95, 0.16), cover_end=(-2.00, 0.40),
                pod=(0.55, -1.50, 0.62, 0.55), pod_end_y=0.43, floor_hw=0.68,
                fw=(2.20, 2.68, 0.87, 0.075, 'cascade'), fw_endplate=0.22,
                rw=(-1.85, -2.18, 0.375, 0.80, 0.20), rw2=False, beam=False, fin=0.86, halo=False, high_nose=True, finger=False,
                drs=True, groove=False, cover=False, mirrors=(0.72, 0.36, 0.73)),
    'hybrid': dict(L=5.25, xf=1.82, xr=-1.65, R=0.330, wf=0.245, wr=0.325, wheel_y=0.78, wheel_yr=0.76,
                nose_tip=(2.72, 0.24), nose_hw=0.07, chassis_hw=0.27, chassis_top=0.64, nose_low=0.16,
                cockpit=(0.45, -0.42), rim=0.70, head=(-0.24, 0.85), airbox=(-0.54, 0.98, 0.15), cover_end=(-1.95, 0.40),
                pod=(0.25, -1.20, 0.62, 0.52), pod_end_y=0.30, floor_hw=0.70,
                fw=(2.32, 2.76, 0.90, 0.070, 'cascade'), fw_endplate=0.23,
                rw=(-2.08, -2.38, 0.375, 0.80, 0.21), rw2=False, beam=True, fin=0.70, halo=False, high_nose=False, finger=True,
                drs=True, groove=False, cover=False, mirrors=(0.15, 0.52, 0.68)),
    'ge':  dict(L=5.62, xf=1.84, xr=-1.76, R=0.360, wf=0.305, wr=0.405, wheel_y=0.80, wheel_yr=0.79,
                nose_tip=(2.90, 0.22), nose_hw=0.16, chassis_hw=0.28, chassis_top=0.66, nose_low=0.18,
                cockpit=(0.45, -0.42), rim=0.70, head=(-0.25, 0.85), airbox=(-0.56, 0.98, 0.16), cover_end=(-2.05, 0.42),
                pod=(0.28, -1.30, 0.78, 0.56), pod_end_y=0.32, floor_hw=0.82,
                fw=(2.45, 2.92, 1.00, 0.065, 'arch'), fw_endplate=0.24,
                rw=(-2.22, -2.55, 0.52, 0.82, 0.24), rw2=False, beam=True, fin=0.0, halo=True, high_nose=False, finger=False,
                drs=True, groove=False, cover=True, mirrors=(0.05, 0.56, 0.72)),
}


# Eras whose body is made in Blender (tools/blender/*.py builds and exports it): the three LODs
# replace the code-made body, and each team's livery is painted on them the same way.
BLENDER = {'v8': os.path.join(HERE, 'blender', 'f1_v8_car.json')}


def blender_lods(era):
    """the Blender model's LODs as Meshes, or None. Materials are the swatch names: 'livery' is the
    projected team livery, '<swatch>_<shade %>' a darker / lighter swatch."""
    path = BLENDER.get(era)
    if not path or not os.path.exists(path):
        return None
    d = json.load(open(path))
    mats = []
    for name in d['materials']:
        sw, _, shade = name.partition('_')
        mats.append(('proj' if sw == 'livery' else sw, int(shade) / 100.0 if shade else 1.0))
    out = []
    for lod in d['lods']:
        m = Mesh(E[era]['L'] + 1.2)
        P = np.array(lod['p'], float).reshape(-1, 3, 3)
        N = np.array(lod['n'], float).reshape(-1, 3, 3)
        for i in range(len(P)):
            fn = np.cross(P[i, 1] - P[i, 0], P[i, 2] - P[i, 0])
            a = np.linalg.norm(fn)
            if a < 1e-10:
                continue
            uv, shade = mats[lod['m'][i]]
            m.tris.append((P[i], N[i], uv, 'glass' if lod['g'][i] else 'body', fn / a, shade))
        out.append(m)
    return out


def sstep(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3 - 2 * t)


def lerp(a, b, t):
    return a + (b - a) * t


def ring(x, hw, zb, zt, n=12, p=3.0, flat_bottom=True):
    """a rounded-box cross-section at station x: half width hw, from zb to zt"""
    pts = []
    zc = (zb + zt) / 2; hh = (zt - zb) / 2
    for k in range(n):
        a = 2 * np.pi * k / n
        c, s = np.cos(a), np.sin(a)
        y = hw * np.sign(c) * abs(c) ** (2 / p)
        z = zc + hh * np.sign(s) * abs(s) ** (2 / p)
        if flat_bottom and s < 0:
            z = zc + hh * np.sign(s) * abs(s) ** (2 / (p * 2.2))
        pts.append((x, y, z))
    return np.array(pts)


# ---------------------------------------------------------------------------------------------- body
def chassis_profile(s, x):
    """half width, bottom, top of the monocoque / nose / engine cover at station x"""
    xt, zt_tip = s['nose_tip']
    cf, cb = s['cockpit']
    top = s['chassis_top']
    hw0 = s['chassis_hw']
    if x > cf:                               # the nose: from the cockpit front out to the tip
        t = (x - cf) / (xt - cf)             # 0 at the cockpit, 1 at the tip
        if s['finger']:                      # 2016: chassis to ~60 %, then a step down to a thin finger
            hw = lerp(hw0, s['nose_hw'] + 0.06, sstep(0, 0.6, t)) - 0.06 * sstep(0.55, 0.75, t)
            ztop = lerp(top, 0.56, sstep(0, 0.55, t))
            ztop = lerp(ztop, zt_tip + 0.05, sstep(0.55, 0.78, t))
            zb = lerp(0.22, s['nose_low'], sstep(0.4, 0.75, t))
        elif s['high_nose']:                 # raised nose: high underside, a gentle droop to the tip
            hw = lerp(hw0, s['nose_hw'], sstep(0, 1.0, t) ** 0.8)
            ztop = lerp(top, zt_tip + 0.07, sstep(0, 1, t) ** 1.1)
            zb = lerp(0.24, zt_tip - 0.06, sstep(0, 1, t) ** 0.7)
        else:                                # 2025: low, wide, short nose
            hw = lerp(hw0, s['nose_hw'], sstep(0, 1.0, t))
            ztop = lerp(top, zt_tip + 0.06, sstep(0, 1, t) ** 0.9)
            zb = lerp(0.20, zt_tip - 0.06, sstep(0.2, 1, t))
        return max(hw, 0.04), zb, max(ztop, zb + 0.05)
    if x > cb:                               # cockpit: the sides rise to the rim
        return hw0, 0.08, s['rim']
    # behind the driver: engine cover down to the gearbox
    xe, ze = s['cover_end']
    t = (cb - x) / (cb - xe)
    hw = lerp(hw0, 0.13, sstep(0, 1, t))
    ztop = lerp(s['rim'] - 0.04, ze, sstep(0.05, 1, t))
    zb = lerp(0.08, 0.16, sstep(0.6, 1, t))
    return hw, zb, ztop


def build_body(s, lod):
    """lod -1: the near Vita model, 0: middle, 2: far"""
    L = s['L']
    m = Mesh(L + 1.2)
    xt = s['nose_tip'][0]; xe = s['cover_end'][0]
    nst = {-1: 34, 0: 15, 2: 9}[lod]
    nr = {-1: 16, 0: 10, 2: 8}[lod]
    xs = list(np.linspace(xt, xe, nst))
    rings = []
    for x in xs:
        hw, zb, zt = chassis_profile(s, x)
        rings.append(ring(x, hw, zb, zt, nr, p=3.2 if x < s['cockpit'][0] else 2.6))
    m.loft(rings)
    # airbox over the driver's head (behind the roll hoop), falling into the engine cover
    xa, za, ha = s['airbox']; cb = s['cockpit'][1]
    axs = np.linspace(cb + 0.06, xa - 0.85, 7 if lod < 2 else 4)
    ar = []
    for x in axs:
        t = (cb + 0.06 - x) / (cb + 0.06 - (xa - 0.85))
        top = lerp(za, s['rim'] - 0.02, sstep(0.25, 1.0, t))
        hw = lerp(ha, 0.10, sstep(0.2, 1, t))
        ar.append(ring(x, hw, s['rim'] - 0.10, top, 10 if lod < 2 else 6, p=2.4))
    m.loft(ar)
    if lod < 2:
        # intake mouth (dark) and, after 2014, the oval inlet
        m.quad((cb + 0.065, -ha * 0.7, za - 0.17), (cb + 0.065, ha * 0.7, za - 0.17), (cb + 0.065, ha * 0.7, za - 0.03), (cb + 0.065, -ha * 0.7, za - 0.03), uv='carbon', shade=0.35)
    # shark fin (2010, small in 2016): a thin sheet from the airbox to the rear wing
    if s['fin'] > 0 and lod < 2:
        x0 = xa - 0.3; x1 = s['rw'][0] + 0.05
        zf = s['fin']
        for side in (0.008, -0.008):
            m.quad((x0, side, za - 0.05), (x1, side, zf), (x1, side, chassis_profile(s, x1)[2]), (x0 + 0.2, side, s['rim']), uv='paint')
    # sidepods (left and right): rounded boxes with an undercut, narrowing to the gearbox
    pf, pb, py, pz = s['pod']
    for side in (1, -1):
        prs = []
        nps = {-1: 12, 0: 8, 2: 4}[lod]
        for x in np.linspace(pf, pb, nps):
            t = (pf - x) / (pf - pb)
            outer = lerp(py, s['pod_end_y'], sstep(0.35, 1.0, t))
            inner = 0.18
            top = lerp(pz, 0.30, sstep(0.3, 1, t)) if s['cover'] else lerp(pz, 0.34, sstep(0.45, 1, t))
            bot = 0.10 + (0.12 * (1 - sstep(0.0, 0.6, t)) if s['cover'] else 0.0)    # the 2022+ undercut
            nose_in = sstep(0, 0.10, t)                                         # the inlet is a little smaller
            top = lerp(top - 0.10, top, nose_in); outer_i = lerp(outer - 0.06, outer, nose_in)
            hw = (outer_i - inner) / 2; yc = (outer_i + inner) / 2
            r = ring(x, hw, bot, top, 10 if lod < 2 else 6, p=2.8)
            r[:, 1] = side * (r[:, 1] + yc)
            prs.append(r)
        m.loft(prs)
        if lod < 2:      # inlet mouth
            hw0 = (py - 0.06 - 0.18) / 2
            yc0 = (py - 0.06 + 0.18) / 2
            m.quad((pf + 0.005, side * (yc0 - hw0 * 0.8), (0.10 + (0.12 if s['cover'] else 0)) + 0.04),
                   (pf + 0.005, side * (yc0 + hw0 * 0.8), (0.10 + (0.12 if s['cover'] else 0)) + 0.04),
                   (pf + 0.005, side * (yc0 + hw0 * 0.8), pz - 0.14), (pf + 0.005, side * (yc0 - hw0 * 0.8), pz - 0.14), uv='carbon', shade=0.3)
    # floor / plank
    fh = s['floor_hw']
    xf0 = pf + (0.25 if s['cover'] else 0.15)
    xr0 = s['xr'] + 0.15
    m.box((xr0, -fh, 0.035), (xf0, fh, 0.06), uv='carbon', shade=0.8)
    if s['cover']:      # 2025: the floor edge curls up, with the "fences" under the front
        for side in (1, -1):
            m.box((xr0, side * fh - 0.012, 0.06), (xf0 - 0.2, side * fh + 0.012, 0.13), uv='carbon', shade=0.8)
    # diffuser
    m.part(np.array([[xr0, -0.55, 0.06], [xr0, 0.55, 0.06], [s['xr'] - 0.42, 0.50, 0.30], [s['xr'] - 0.42, -0.50, 0.30]]),
           [[0, 1, 2], [0, 2, 3]], uv='carbon', flat=True, shade=0.5)
    if lod < 2:
        for k in range(-2, 3):
            y = k * 0.18
            m.box((s['xr'] - 0.42, y - 0.006, 0.06), (xr0, y + 0.006, 0.26), uv='carbon', shade=0.6)
    front_wing(m, s, lod)
    rear_wing(m, s, lod)
    if lod < 2:
        suspension(m, s, lod)
        mirrors(m, s)
        roll_hoop(m, s)
        if s['halo']:
            halo(m, s, lod)
        exhaust(m, s)
        # the cockpit opening: a dark oval sunk into the chassis top
        cf, cb = s['cockpit']
        op = []
        for k in range(16):
            a = 2 * np.pi * k / 16
            op.append((lerp(cf - 0.22, cb + 0.02, 0.5 + 0.5 * np.cos(a)), 0.22 * np.sin(a), s['rim'] + 0.004))
        cen = ((cf - 0.22 + cb) / 2, 0, s['rim'] - 0.02)
        m.part(np.array([cen] + op), [[0, k + 1, (k + 1) % 16 + 1] for k in range(16)], uv='carbon', flat=True, shade=0.35)
        if s['high_nose'] or s['finger']:
            # front wing pylons under the nose
            for side in (0.10, -0.10):
                x0 = s['fw'][0] + 0.05
                m.box((x0, side - 0.008, s['fw'][3] + 0.02), (x0 + 0.22, side + 0.008, chassis_profile(s, x0 + 0.1)[1] + 0.02), uv='carbon')
    helmet(m, s, lod)
    return m


def front_wing(m, s, lod):
    x0, x1, span, z, style = s['fw']
    chord = x1 - x0
    n = {-1: 10, 0: 6, 2: 2}[lod]
    if style == 'arch':          # 2025: the planes sweep up towards the endplates; the middle meets the nose
        for el, (c0, c1, dz) in enumerate([(0.0, 0.45, 0.0), (0.42, 0.72, 0.05), (0.70, 0.95, 0.10)][:3 if lod < 2 else 1]):
            sec = []
            for k in range(n + 1):
                y = -span + 2 * span * k / n
                lift = 0.10 * (abs(y) / span) ** 3 + dz
                xa = x1 - chord * c0 - 0.10 * (abs(y) / span) ** 2; xb = x1 - chord * c1 - 0.10 * (abs(y) / span) ** 2
                sec.append([(xa, y, z + lift), (xb, y, z + lift + 0.03 + dz * 0.4)])
            rows = [[p[0] for p in sec], [p[1] for p in sec]]
            m.sheet(rows, uv='paint' if el == 0 else 'carbon', shade=1.0)
            m.sheet([[(p[0], p[1], p[2] - 0.012) for p in rows[0]], [(p[0], p[1], p[2] - 0.012) for p in rows[1]]], uv='carbon', shade=0.5)
    else:
        m.box((x0, -span, z), (x1, span, z + 0.018), uv='paint' if style == 'flat' else 'carbon')
        if lod < 2:
            m.box((x0 - 0.02, -span + 0.05, z + 0.04), (x0 + chord * 0.45, span - 0.05, z + 0.055), uv='carbon')    # flap
            if style == 'cascade':
                for side in (1, -1):
                    m.box((x0 + 0.05, side * (span - 0.28) - 0.12, z + 0.10), (x0 + 0.20, side * (span - 0.28) + 0.12, z + 0.112), uv='carbon')
    # endplates
    h = s['fw_endplate']
    for side in (1, -1):
        y = side * span
        if style == 'arch':
            m.box((x0 - 0.06, y - 0.006 * side - 0.006, z + 0.02), (x1 - 0.08, y - 0.006 * side + 0.006, z + h * 0.8), uv='carbon')
        else:
            m.box((x0 - 0.04, y - 0.008, z - 0.02), (x1 + 0.02, y + 0.008, z + h), uv='second' if lod < 2 else 'carbon')


def rear_wing(m, s, lod):
    xa, xb, span, z, h = s['rw']           # main plane leading / trailing x, half span, main plane height, endplate height
    chord = xa - xb
    # main plane (the flap above it is the separate flap_<era> model)
    m.box((xb + chord * 0.45, -span, z), (xa, span, z + 0.03), uv='carbon')
    if s['rw2']:                            # 1990s: a second, higher element
        m.box((xb, -span, z + 0.17), (xa - 0.08, span, z + 0.20), uv='paint')
    for side in (1, -1):
        y = side * (span + 0.006)
        if s['cover'] and lod < 2:          # 2025: the endplate rolls into the wing tip
            m.box((xb - 0.02, y - 0.007, z - h * 0.55), (xa + 0.02, y + 0.007, z + 0.06), uv='paint')
            m.box((xb, y - side * 0.05 - 0.007, z + 0.06), (xa, y - side * 0.05 + 0.007, z + 0.20), uv='paint')
        else:
            m.box((xb - 0.04, y - 0.007, z - h * 0.65), (xa + 0.04, y + 0.007, z + h), uv='paint')
    # pylon(s) down to the gearbox
    if s['rw2']:
        for side in (0.10, -0.10):
            m.box((xb + 0.05, side - 0.006, 0.36), (xa - 0.05, side + 0.006, z + 0.17), uv='carbon')
    else:
        m.box((xb + 0.06, -0.012, 0.34), (xa - 0.04, 0.012, z), uv='carbon')
    if s['beam'] and lod < 2:               # beam wing low down behind the diffuser
        m.box((xb + 0.05, -span * 0.85, 0.42), (xb + 0.20, span * 0.85, 0.445), uv='carbon')
        m.box((xb + 0.10, -span * 0.85, 0.50), (xb + 0.22, span * 0.85, 0.52), uv='carbon')


def tube(m, a, b, r, uv='carbon', sides=5):
    m.tube([a, b], r, sides=sides, uv=uv, shade=0.8)


def suspension(m, s, lod):
    """wishbones and push / pull rods from the chassis out to the uprights"""
    R = s['R']
    for ax, wy, front in ((s['xf'], s['wheel_y'], True), (s['xr'], s['wheel_yr'], False)):
        hw_c = chassis_profile(s, ax)[0] if front else 0.20
        zc = R
        for side in (1, -1):
            hub = (ax, side * (wy - 0.12), zc)
            for dz, spread in ((0.10, 0.20), (-0.10, 0.25)):
                p_out = (ax, side * (wy - 0.15), zc + dz)
                for dx in (spread, -spread * 0.6):
                    p_in = (ax + dx, side * (hw_c - 0.02), zc + dz * 0.6 + (0.10 if front and s['high_nose'] else 0.0))
                    tube(m, p_in, p_out, 0.014 if lod < 0 else 0.018, uv='carbon', sides=4)
            tube(m, (ax, side * (hw_c - 0.02), zc + 0.25), (ax, side * (wy - 0.18), zc - 0.08), 0.012, uv='carbon', sides=4)
            if s['cover']:      # 2025: the brake-duct winglets over the front tyres
                if front:
                    m.box((ax - 0.12, side * (wy + s['wf'] / 2 - 0.02) - 0.012, R + 0.30), (ax + 0.22, side * (wy + s['wf'] / 2 - 0.02) + 0.012, R + 0.34), uv='carbon')


def mirrors(m, s):
    x, y, z = s['mirrors']
    for side in (1, -1):
        m.box((x - 0.05, side * y - 0.07, z), (x + 0.02, side * y + 0.07, z + 0.06), uv='paint')
        m.quad((x - 0.052, side * y - 0.06, z + 0.008), (x - 0.052, side * y + 0.06, z + 0.008), (x - 0.052, side * y + 0.06, z + 0.052),
               (x - 0.052, side * y - 0.06, z + 0.052), uv='glass', shade=1.3)
        tube(m, (x, side * (y - 0.06), z + 0.02), (x - 0.05, side * 0.24, s['rim'] - 0.04 if x > -0.1 else 0.50), 0.010, uv='carbon', sides=4)


def roll_hoop(m, s):
    cb = s['cockpit'][1]; xa, za, ha = s['airbox']
    m.box((cb - 0.02, -0.06, za - 0.02), (cb + 0.08, 0.06, za + 0.02), uv='carbon')
    if s['finger'] or s['cover']:      # TV camera pods on the roll hoop (2014+)
        for side in (1, -1):
            m.box((cb - 0.06, side * 0.11 - 0.02, za - 0.06), (cb + 0.12, side * 0.11 + 0.02, za - 0.02), uv='white')


def halo(m, s, lod):
    cf, cb = s['cockpit']
    hx, hz = s['head']
    n = 10 if lod < 0 else 6
    pts = []
    # the hoop round the driver's head, in a plane tilted back, meeting the chassis behind
    for k in range(n + 1):
        a = np.pi * k / n            # 0 = left rear .. pi = right rear, via the front
        x = lerp(cb + 0.05, cf - 0.10, np.sin(a))
        y = 0.36 * np.cos(a)
        z = 0.94 + 0.035 * np.sin(a)
        pts.append((x, y, z))
    m.tube(pts, 0.026, sides=6, uv='carbon', shade=0.9)
    for side_pt in (pts[0], pts[-1]):          # rear legs down to the chassis
        m.tube([side_pt, (side_pt[0] - 0.02, side_pt[1] * 0.85, s['rim'] - 0.02)], 0.024, sides=6, uv='carbon', shade=0.85)
    front = pts[n // 2]
    m.tube([front, (front[0] + 0.12, 0.0, 0.83), (cf + 0.16, 0.0, s['rim'] - 0.03)], 0.028, sides=6, uv='carbon', shade=0.9)   # central pillar


def exhaust(m, s):
    xr = s['cover_end'][0]
    if s['fin'] == 0 and not s['cover'] and not s['finger']:     # 1990s: twin exits on the cover
        for side in (1, -1):
            m.cyl((xr + 0.55, side * 0.20, 0.50), (xr + 0.35, side * 0.20, 0.50), 0.035, sides=8, uv='grey', shade=0.6)
    else:                                                          # one central tailpipe
        m.cyl((xr + 0.05, 0, 0.42), (xr - 0.08, 0, 0.42), 0.045, sides=8, uv='grey', shade=0.6)


def helmet(m, s, lod):
    hx, hz = s['head']
    n = 10 if lod < 0 else 6
    rings = []
    for k in range(1, n):
        a = np.pi * k / n
        r = 0.135 * np.sin(a)
        rings.append(ring(hx + 0.135 * np.cos(a) + 0.03, r, hz - r * 0.95, hz + r, n, p=2.0, flat_bottom=False))
    # the helmet is "glass": drawn from outside, left out from the driver's seat
    gl = lambda i, k: True
    m.loft(rings, uv='white', group='glass', glass=gl)
    # visor band
    m.quad((hx + 0.16, -0.10, hz + 0.005), (hx + 0.16, 0.10, hz + 0.005), (hx + 0.15, 0.10, hz + 0.05), (hx + 0.15, -0.10, hz + 0.05), uv='glass', group='glass', shade=0.4)


# ---------------------------------------------------------------------------------------------- liveries
STYLES = {      # per car: (second colour, accent colour, style)
    'm98_marchetti': ((0.95, 0.95, 0.95), (0.98, 0.85, 0.10), 'classic'),
    'm98_wexford': ((0.95, 0.95, 0.95), (0.85, 0.10, 0.10), 'band'),
    'm98_woking': ((0.08, 0.08, 0.09), (0.85, 0.10, 0.10), 'split'),
    'm98_benedetto': ((0.95, 0.95, 0.95), (0.98, 0.82, 0.10), 'band'),
    'm98_shamrock': ((0.08, 0.08, 0.09), (0.10, 0.60, 0.25), 'nose'),
    'm98_bleu': ((0.95, 0.95, 0.95), (0.85, 0.10, 0.10), 'classic'),
    'm98_arrowfield': ((0.82, 0.62, 0.15), (0.95, 0.95, 0.95), 'stripe'),
    'm98_tartan': ((0.12, 0.18, 0.45), (0.85, 0.10, 0.10), 'tartan'),
    'm98_hinwil': ((0.40, 0.65, 0.95), (0.95, 0.95, 0.95), 'split'),
    'm98_faenza': ((0.98, 0.82, 0.10), (0.15, 0.30, 0.75), 'nose'),
    'm10_marchetti': ((0.95, 0.95, 0.95), (0.10, 0.10, 0.10), 'classic'),
    'm10_woking': ((0.85, 0.08, 0.08), (0.10, 0.10, 0.10), 'split'),
    'm10_taurus': ((0.85, 0.10, 0.12), (0.98, 0.80, 0.10), 'bull'),
    'm10_sterne': ((0.10, 0.75, 0.70), (0.08, 0.08, 0.09), 'stripe'),
    'm10_wexford': ((0.95, 0.95, 0.95), (0.40, 0.70, 0.95), 'band'),
    'm10_enstone': ((0.08, 0.08, 0.09), (0.95, 0.95, 0.95), 'split'),
    'm10_tiger': ((0.98, 0.50, 0.08), (0.10, 0.60, 0.25), 'band'),
    'm10_faenza': ((0.85, 0.10, 0.12), (0.80, 0.80, 0.85), 'bull'),
    'm10_hinwil': ((0.10, 0.10, 0.12), (0.80, 0.10, 0.10), 'nose'),
    'm10_norfolk': ((0.98, 0.82, 0.10), (0.98, 0.82, 0.10), 'stripe'),
    'm16_sterne': ((0.10, 0.80, 0.72), (0.08, 0.08, 0.09), 'stripe'),
    'm16_marchetti': ((0.95, 0.95, 0.95), (0.08, 0.08, 0.09), 'classic'),
    'm16_taurus': ((0.85, 0.10, 0.12), (0.98, 0.80, 0.10), 'bull'),
    'm16_wexford': ((0.08, 0.14, 0.40), (0.85, 0.10, 0.10), 'stripe'),
    'm16_tiger': ((0.98, 0.50, 0.08), (0.75, 0.77, 0.80), 'split'),
    'm16_enstone': ((0.08, 0.08, 0.09), (0.95, 0.95, 0.95), 'split'),
    'm16_faenza': ((0.85, 0.10, 0.12), (0.80, 0.80, 0.85), 'bull'),
    'm16_woking': ((0.85, 0.08, 0.08), (0.95, 0.95, 0.95), 'stripe'),
    'm16_hinwil': ((0.98, 0.85, 0.10), (0.95, 0.95, 0.95), 'band'),
    'm16_kannapolis': ((0.85, 0.08, 0.08), (0.08, 0.08, 0.09), 'split'),
    'm25_papaya': ((0.08, 0.08, 0.09), (0.30, 0.70, 0.95), 'twotone'),
    'm25_taurus': ((0.85, 0.10, 0.12), (0.98, 0.80, 0.10), 'bull'),
    'm25_marchetti': ((0.95, 0.95, 0.95), (0.08, 0.08, 0.09), 'classic'),
    'm25_sterne': ((0.75, 0.77, 0.80), (0.10, 0.85, 0.78), 'twotone'),
    'm25_banbury': ((0.75, 0.95, 0.10), (0.95, 0.95, 0.95), 'stripe'),
    'm25_enstone': ((0.98, 0.45, 0.70), (0.95, 0.95, 0.95), 'band'),
    'm25_kannapolis': ((0.08, 0.08, 0.09), (0.85, 0.08, 0.08), 'nose'),
    'm25_faenza': ((0.15, 0.30, 0.85), (0.85, 0.08, 0.10), 'band'),
    'm25_hinwil': ((0.30, 0.95, 0.20), (0.95, 0.95, 0.95), 'split'),
    'm25_wexford': ((0.95, 0.95, 0.95), (0.20, 0.55, 0.95), 'nose'),
}
CAREER_STYLE = ((0.08, 0.08, 0.10), (0.95, 0.95, 0.97), 'stripe')
SPONSORS = ['AURORA', 'NEXUS', 'VELOCITY', 'CODE 4', 'NOVA FUEL', 'KESTREL', 'ORBIT', 'SUMMIT', 'HELIX', 'VANTA', 'GATO', 'APEX']


def paint_livery(p, c, s, look, text=True):
    """side / top projection. A = main colour, B = second, C = accent"""
    L = s['L']; xf = s['nose_tip'][0] + 0.1; xr = s['rw'][1] - 0.1
    A = c['colour']; B, C, style = look
    p.base(A)
    cf, cb = s['cockpit']; pf, pb, py, pz = s['pod']
    if style == 'classic':        # main colour all over, white rear wing / airbox stripe, accent pinstripe
        p.side_poly([(cb, 0.95), (cb - 0.6, 0.95), (cb - 0.6, 1.4), (cb, 1.4)], B)
        p.top_poly([(xf, 0.0), (xf, 0.03), (xr, 0.03), (xr, 0.0)], C)
    elif style == 'band':         # a broad band in the second colour along the sidepods and nose
        p.side_poly([(xf, 0.30), (xf, 0.44), (pb, 0.40), (pb, 0.26)], B)
        p.top_poly([(xf, 0.0), (xf, 0.06), (cf, 0.20), (pf, YMAX), (pb, YMAX), (pb, 0.0)], B)
        p.side_poly([(xf, 0.44), (xf, 0.47), (pb, 0.43), (pb, 0.40)], C)
    elif style == 'split':        # front half one colour, the back the other
        p.side_poly([(cf + 0.2, 0), (cf - 0.3, ZMAX), (xr, ZMAX), (xr, 0)], B)
        p.top_poly([(cf + 0.3, 0), (cf - 0.2, YMAX), (xr, YMAX), (xr, 0)], B)
        p.side_poly([(cf + 0.2, 0), (cf - 0.3, ZMAX), (cf - 0.22, ZMAX), (cf + 0.28, 0)], C)
    elif style == 'nose':         # contrasting nose and engine cover tips
        p.side_poly([(xf, 0), (xf, ZMAX), (cf + 0.7, ZMAX), (cf + 0.9, 0)], B)
        p.top_poly([(xf, 0), (xf, YMAX), (cf + 0.8, YMAX), (cf + 0.9, 0)], B)
        p.side_poly([(xr + 0.8, 0), (xr + 0.8, ZMAX), (xr, ZMAX), (xr, 0)], C)
    elif style == 'stripe':       # stripes from nose to tail
        p.top_poly([(xf, 0.05), (xf, 0.11), (xr, 0.11), (xr, 0.05)], B)
        p.side_poly([(xf, 0.32), (xf, 0.38), (xr, 0.50), (xr, 0.44)], B)
        p.side_poly([(xf, 0.38), (xf, 0.40), (xr, 0.52), (xr, 0.50)], C)
    elif style == 'bull':         # a sweeping red / yellow graphic on the engine cover and sidepod
        p.side_poly([(pf - 0.2, 0.25), (pb, 0.50), (pb - 0.3, 0.70), (pf - 0.5, 0.50)], B)
        p.side_poly([(pf - 0.1, 0.20), (pf - 0.5, 0.20), (pf - 0.35, 0.35)], C)
        p.top_poly([(xf, 0.0), (xf, 0.05), (cf, 0.07), (cf, 0.0)], C)
    elif style == 'twotone':      # the main colour on top, the second below a line along the car
        p.side_poly([(xf, 0), (xf, 0.30), (pf, 0.40), (xr, 0.50), (xr, 0)], B)
        p.top_poly([(pf, 0.45), (pf - 0.3, YMAX), (xr, YMAX), (xr, 0.32)], B)
        p.side_poly([(xf, 0.30), (xf, 0.33), (pf, 0.43), (xr, 0.53), (xr, 0.50), (pf, 0.40)], C)
    elif style == 'tartan':       # white with a tartan band
        for k in range(10):
            x0 = pf - k * 0.12
            p.side_poly([(x0, 0.22), (x0 - 0.06, 0.22), (x0 - 0.06, 0.48), (x0, 0.48)], B if k % 2 else C)
        p.top_poly([(xf, 0.0), (xf, 0.05), (xr, 0.05), (xr, 0.0)], B)
    carbon = (0.07, 0.07, 0.08)
    # carbon underside and the lower nose / floor edges
    p.side_poly([(xf, 0), (xf, 0.12), (xr, 0.12), (xr, 0)], carbon)
    # number panel on the nose top (the game prints the number on the side of the airbox)
    p.top_poly([(cf + 0.40, -0.10), (cf + 0.62, -0.10), (cf + 0.62, 0.10), (cf + 0.40, 0.10)], (0.97, 0.97, 0.95), mirror=False)
    # driver name plate under the cockpit, sponsor block on the sidepod
    rng = np.random.RandomState(zlib.crc32(c['id'].encode()) & 0xffff)
    names = [SPONSORS[i] for i in rng.permutation(len(SPONSORS))[:2]]
    f = G._font(p.S * 0.028)
    def side_text(x, z, text, col):
        tx, tz = p.sx(x), p.sz(z)
        p.d.text((tx, tz), text, fill=rgb(col), font=f)
    ink = (0.97, 0.97, 0.95) if sum(A) < 1.6 else (0.06, 0.06, 0.08)
    if text:
        # sponsor lettering on the top view only (the side view is shared by both sides, so text
        # there would read backwards on the right): along each sidepod top and the engine cover
        def top_text(x, y, text, col, size):
            ff = G._font(p.S * size)
            tw = p.d.textlength(text, font=ff)
            p.d.text((p.sx(x) - tw / 2, p.ty(y) - p.S * size * 0.6), text, fill=rgb(col), font=ff)
        top_text((pf + pb) / 2 + 0.1, 0.45, names[0], ink, 0.030)
        top_text((pf + pb) / 2 + 0.1, -0.45, names[0], ink, 0.030)
        top_text(cb - 0.7, 0.0, names[1], ink, 0.024)
    p.swatches(dict(glass=(0.10, 0.12, 0.16), white=(0.97, 0.96, 0.92), red=(0.95, 0.06, 0.05), carbon=carbon,
                    cage=(0.42, 0.44, 0.48), second=B, panel=C, paint=A, **G.HD_SWATCH))


# ---------------------------------------------------------------------------------------------- wheels, flaps, steering
def build_wheel(era, sides, detail):
    """one road wheel, axle along Y, outer face at +Y, at the rear tyre's size (the front is drawn
    narrower by the game). 1990s: grooved tyres. 2025: 18-inch rims with covers."""
    s = E[era]
    m = Mesh(1.0)
    R = s['R']; hw = s['wr'] / 2
    rim = 0.235 if era == 'ge' else 0.17         # 18-inch vs 13-inch
    prof = [(-hw, rim + 0.01), (-hw, R - 0.04), (-hw + 0.04, R), (hw - 0.04, R), (hw, R - 0.04), (hw, rim + 0.01)]
    if sides <= 8:       # far wheel: a square-shouldered tyre (3 bands instead of 5)
        prof = [(-hw, rim + 0.01), (-hw + 0.02, R), (hw - 0.02, R), (hw, rim + 0.01)]
    rings = []
    for k in range(sides + 1):
        a = k * 2 * np.pi / sides
        rings.append([(r_ * np.cos(a), y, r_ * np.sin(a)) for y, r_ in prof])
    m.sheet(rings, uv='rubber', shade=0.9)
    if s['groove'] and detail:      # four grooves round the tread
        for g in (-0.6, -0.2, 0.2, 0.6):
            y = g * hw
            gr = [[((R + 0.002) * np.cos(k * 2 * np.pi / sides), y - 0.006, (R + 0.002) * np.sin(k * 2 * np.pi / sides)),
                   ((R + 0.002) * np.cos(k * 2 * np.pi / sides), y + 0.006, (R + 0.002) * np.sin(k * 2 * np.pi / sides))] for k in range(sides + 1)]
            m.sheet(gr, uv='carbon', shade=0.35)
    # sidewall lettering band (a lighter ring)
    if detail:
        band = [[((R - 0.05) * np.cos(k * 2 * np.pi / sides), hw + 0.002, (R - 0.05) * np.sin(k * 2 * np.pi / sides)),
                 ((R - 0.03) * np.cos(k * 2 * np.pi / sides), hw + 0.002, (R - 0.03) * np.sin(k * 2 * np.pi / sides))] for k in range(sides + 1)]
        m.sheet(band, uv='white', shade=0.9)
    # rim face
    ang = [k * 2 * np.pi / sides for k in range(sides + 1)]
    if s['cover']:       # wheel cover: a flat disc nearly flush with the tyre
        m.part(np.array([(0, hw - 0.01, 0)] + [(rim * np.cos(a), hw - 0.012, rim * np.sin(a)) for a in ang]),
               [[0, k + 1, k + 2] for k in range(sides)], uv='carbon', flat=True, shade=0.8)
        if sides > 8:
            m.cyl((0, hw - 0.012, 0), (0, hw + 0.004, 0), 0.05, sides=8, uv='grey', shade=1.1)
    else:
        m.part(np.array([(0, hw - 0.07, 0)] + [(rim * np.cos(a), hw - 0.02, rim * np.sin(a)) for a in ang]),
               [[0, k + 1, k + 2] for k in range(sides)], uv='grey', flat=True, shade=0.65)
        if detail:
            for k in range(6):
                a = k * 2 * np.pi / 6
                m.tube([(0.04 * np.cos(a), hw - 0.06, 0.04 * np.sin(a)), (rim * np.cos(a), hw - 0.02, rim * np.sin(a))], 0.012, sides=4, uv='grey', shade=1.0)
        if detail or sides > 8:
            m.cyl((0, hw - 0.06, 0), (0, hw - 0.01, 0), 0.04, sides=8, uv='red', shade=1.1)
    m.part(np.array([(0, -hw, 0)] + [(R * np.cos(a), -hw, R * np.sin(a)) for a in ang]), [[0, k + 2, k + 1] for k in range(sides)], uv='carbon', flat=True, shade=0.35)
    if detail:
        m.cyl((0, -0.03, 0), (0, 0.0, 0), rim - 0.03, sides=sides, uv='cage', shade=0.6)     # brake disc
    return m


def build_flap(era):
    """the rear wing's top flap, its pivot (the hinge at the leading edge) at the origin; it extends
    back along -X. The game puts it on the car and tilts it up when DRS is open."""
    s = E[era]
    xa, xb, span, z, h = s['rw']
    chord = (xa - xb) * 0.55
    m = Mesh(1.0)
    m.box((-chord, -span + 0.01, 0.0), (0.0, span - 0.01, 0.022), uv='paint')
    m.box((-chord, -span + 0.01, 0.022), (-chord + 0.012, span - 0.01, 0.034), uv='white')    # gurney
    return m


def flap_pivot(era):
    s = E[era]
    xa, xb, span, z, h = s['rw']
    return (xb + (xa - xb) * 0.50, 0.0, z + 0.07)


def build_steering(era):
    """steering wheel in the YZ plane facing -X; the game draws the screen at x -0.012"""
    m = Mesh(1.0)
    if era == '90s':          # small round-topped wheel with grips, a few buttons and a little LCD
        n = 16
        for k in range(n):
            a0 = k * 2 * np.pi / n; a1 = (k + 1) * 2 * np.pi / n
            if 4.0 < (a0 + a1) / 2 < 5.4: continue
            m.tube([(0.004, 0.13 * np.cos(a0), 0.11 * np.sin(a0)), (0.004, 0.13 * np.cos(a1), 0.11 * np.sin(a1))], 0.018, sides=6, uv='rubber', shade=0.95)
        m.box((-0.006, -0.12, -0.075), (0.02, 0.12, 0.05), uv='carbon', shade=0.8)
    else:                     # rectangular "butterfly" wheel with a big screen (2010 smaller)
        wd = 0.15 if era == 'v8' else 0.165
        out = [(0.09, 0.075), (wd, 0.065), (wd + 0.012, 0.0), (wd, -0.07), (0.10, -0.09), (-0.10, -0.09), (-wd, -0.07), (-wd - 0.012, 0.0), (-wd, 0.065), (-0.09, 0.075)]
        for a, b in zip(out, out[1:]):
            m.tube([(0.004, a[0], a[1]), (0.004, b[0], b[1])], 0.016, sides=6, uv='carbon', shade=0.95)
        for s_ in (-1, 1):
            m.tube([(0.002, s_ * wd, 0.05), (0.002, s_ * (wd + 0.012), 0.0), (0.002, s_ * wd, -0.05)], 0.024, sides=6, uv='rubber', shade=0.9)
        m.box((-0.006, -0.12, -0.085), (0.025, 0.12, 0.07), uv='carbon', shade=0.8)
        cols = ('green', 'red', 'blue', 'white', 'yellow', 'orange')
        for s_ in (-1, 1):
            for k in range(6):
                y = s_ * (0.085 + (k % 2) * 0.022); z = 0.045 - (k // 2) * 0.028
                m.box((-0.012, y - 0.008, z - 0.008), (0.0, y + 0.008, z + 0.008), uv=cols[k], shade=1.4)
            m.cyl((-0.004, s_ * 0.04, -0.07), (-0.02, s_ * 0.04, -0.07), 0.013, sides=8, uv='red' if s_ < 0 else 'yellow', shade=1.3)
            m.box((0.03, s_ * 0.07 - 0.02, -0.03), (0.04, s_ * 0.07 + 0.02, 0.09), uv='grey', shade=0.9)       # paddles
    m.tube([(0.03, 0, 0), (0.30, 0, -0.03)], 0.022, sides=6, uv='grey', shade=0.6)
    return m


# ---------------------------------------------------------------------------------------------- cars
def read_cars():
    src = open(os.path.join(ROOT, 'src', 'data.cpp')).read()
    cars = []
    for mm in re.finditer(r'\{"(\w+)",\s*"[^"]*",\s*"[^"]*",\s*"[^"]*",\s*\{\d+, \d+\},\s*CLASS_\w+,\s*(ERA_\w+),.*?([\d.]+)f, C\(([\d.]+)f, ([\d.]+)f, ([\d.]+)f\)', src):
        era = {'ERA_90S': '90s', 'ERA_V8': 'v8', 'ERA_HYBRID': 'hybrid', 'ERA_GE': 'ge'}[mm.group(2)]
        cars.append(dict(id=mm.group(1), era=era, L=float(mm.group(3)), colour=(float(mm.group(4)), float(mm.group(5)), float(mm.group(6)))))
    return cars


def car_spec(c):
    s = dict(E[c['era']])
    if c['era'] in BLENDER and os.path.exists(BLENDER[c['era']]):
        return s                 # one Blender body for the whole grid: the livery follows its shape
    rng = np.random.RandomState(zlib.crc32(c['id'].encode()) & 0x7fffffff)
    # each team's own small differences
    xt, zt = s['nose_tip']
    s['nose_tip'] = (xt + rng.uniform(-0.04, 0.04), zt + rng.uniform(-0.03, 0.03))
    s['airbox'] = (s['airbox'][0], s['airbox'][1] + rng.uniform(-0.03, 0.02), s['airbox'][2] + rng.uniform(-0.02, 0.02))
    pf, pb, py, pz = s['pod']
    s['pod'] = (pf + rng.uniform(-0.05, 0.05), pb + rng.uniform(-0.08, 0.05), py + rng.uniform(-0.04, 0.02), pz + rng.uniform(-0.03, 0.03))
    return s


# LVY career liveries (the game repaints them from the player's colours)
KEY_A, KEY_B, KEY_C = (1.0, 0.0, 1.0), (0.0, 1.0, 1.0), (0.5, 0.0, 1.0)
LSTYLES = ['classic', 'band', 'split', 'nose', 'stripe', 'bull']


def write_livery_set(path, c, s, sizes=(512, 256, 128)):
    keys = {rgb(KEY_A): 1, rgb(KEY_B): 2, rgb(KEY_C): 3}
    roles = {}; images = []
    for style in LSTYLES:
        for ts in sizes:
            p = Paint(s['L'] + 1.2, S=ts)
            paint_livery(p, dict(c, colour=KEY_A, id='career'), s, (KEY_B, KEY_C, style), text=False)
            a = np.asarray(p.img, np.uint8).reshape(-1, 3)
            idx = np.zeros(len(a), np.uint8)
            cols, inv = np.unique(a, axis=0, return_inverse=True)
            for k, col in enumerate(tuple(int(x) for x in cc) for cc in cols):
                if col not in roles: roles[col] = len(roles)
                idx[inv.reshape(-1) == k] = roles[col]
            images.append(swizzle(idx, ts, ts))
    if len(roles) > 32:
        # text anti-aliasing adds many shades: fold the rarest into their nearest kept colour
        raise SystemExit(f'too many colour roles in the career livery ({len(roles)})')
    with open(path, 'wb') as f:
        f.write(b'LVY1'); f.write(struct.pack('<III', len(LSTYLES), len(sizes), len(roles)))
        f.write(struct.pack('<4I', *(list(sizes) + [0])))
        tab = [(0, 0)] * 32
        for col, r in roles.items():
            tab[r] = (keys.get(col, 0), 0xFF000000 | (col[2] << 16) | (col[1] << 8) | col[0])
        for kind, abgr in tab: f.write(struct.pack('<II', kind, abgr))
        for im in images: f.write(im.tobytes())
    return len(roles)


def main():
    os.makedirs(OUT, exist_ok=True)
    prev = sys.argv[1] if len(sys.argv) > 1 else None
    for c in read_cars():
        s = car_spec(c)
        career = c['id'].startswith('career')
        look = CAREER_STYLE if career else STYLES[c['id']]
        p = Paint(s['L'] + 1.2, S=1024)          # drawn at 2x, shrunk to 512 = smooth edges
        if career:
            # no lettering on the career car: its texture is repainted from colour roles
            paint_livery(p, dict(c, id='career'), s, look, text=False)
        else:
            paint_livery(p, c, s, look)
        bl = blender_lods(c['era'])
        if bl:
            lods = [(bl[0], p.img, 512), (bl[1], p.img, 256), (bl[2], p.img, 128)]
        else:
            lods = [(build_body(s, -1), p.img, 512), (build_body(s, 0), p.img, 256), (build_body(s, 2), p.img, 128)]
        # header extras: cabin x (0: the cockpit layout is per era), number panel y / x (airbox side)
        meta = (0.0, s['airbox'][2] + 0.01, s['airbox'][0] - 0.25)
        # EMX1: axles and wheel track, rear wheel radius; flags: bit 2 = open-wheeler (drawn as an F1 car)
        emx = (s['xf'], s['xr'], s['wheel_y'], s['R'], 4)
        n = write_emm(os.path.join(OUT, f"car_{c['id']}.emm"), lods, meta=meta, emx=emx)
        print(f"{c['id']:16s} {c['era']:6s} triangles {n}")
        if prev:
            p.img.save(os.path.join(prev, f"livery_{c['id']}.png"))
    # per era: road wheel, DRS flap, steering wheel
    t = Paint(5.0, 256)
    t.base((0.1, 0.1, 0.1))
    t.swatches(dict(glass=(0.08, 0.11, 0.16), white=(0.92, 0.92, 0.88), red=(0.85, 0.08, 0.06), carbon=(0.09, 0.09, 0.10),
                    cage=(0.50, 0.52, 0.56), second=(0.8, 0.1, 0.1), panel=(0.9, 0.1, 0.1), paint=(0.10, 0.10, 0.11), **G.HD_SWATCH))
    for era in ERAS:
        print('wheel', era, write_emm(os.path.join(OUT, f'wheel_{era}.emm'), [(build_wheel(era, 18, True), t.img, 128), (build_wheel(era, 10, False), t.img, 64), (build_wheel(era, 7, False), t.img, 32)]))
        print('flap', era, write_emm(os.path.join(OUT, f'flap_{era}.emm'), [(build_flap(era), t.img, 64)]))
        print('steering', era, write_emm(os.path.join(OUT, f'steering_{era}.emm'), [(build_steering(era), t.img, 256)]))
    # the career cars' livery sets
    for c in read_cars():
        if c['id'].startswith('career'):
            print('livery set', c['id'], write_livery_set(os.path.join(OUT, f"livery_{c['id']}.lvy"), c, car_spec(c)))


if __name__ == '__main__':
    main()
