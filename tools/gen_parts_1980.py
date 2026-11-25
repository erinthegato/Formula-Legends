"""Formula Legends -- the 1980 era's own parts, built in code (no Blender needed).

    python3 tools/gen_parts_1980.py          # writes tools/blender/parts_1980.json

tools/gen_cockpit.py turns them into the era's game models (the era's internal id is still "90s"):
  wheel_90s.emm     slick tyres on 13 in rims, the rear size (0.335 m radius, 0.42 m wide); the game
                    draws the fronts smaller and narrower (Renderer FRONT_R / FRONT_W)
  flap_90s.emm      the top flap of the wide 1980 rear wing (its hinge at the origin, running back)
  steering_90s.emm  a small round leather-rimmed wheel on three alloy spokes, no buttons, no screen
                    (the instruments are on the dash), with the driver's gloves at a quarter to three
  driver_90s.emm    the Blender driver (tools/blender/cockpit_parts.py) without the HANS collar and
                    the helmet's air intake, which 1980 drivers did not have, the helmet lowered onto
                    the shoulders (no long bare neck)

Axes as the other parts: the wheel's axle along Y (outer face +Y); the steering wheel in the YZ plane
facing -X; metres.
"""
import json, math, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gen_car_1980 import Builder          # noqa: E402

OUT = os.path.join(HERE, 'blender', 'parts_1980.json')
PARTS = os.path.join(HERE, 'blender', 'cockpit_parts.json')


def part(B):
    mats, d = B.json()
    d['mats'] = mats
    return d


def lathe(B, prof, sides, mats, smooth=None):
    """a surface of revolution about Y; band k (points k to k+1) uses mats[k]"""
    rings = [[(r * math.cos(2 * math.pi * i / sides), y, r * math.sin(2 * math.pi * i / sides)) for i in range(sides)] for (y, r) in prof]
    B.group = smooth
    for k in range(len(prof) - 1):
        ref = np.array((0.0, (prof[k][0] + prof[k + 1][0]) / 2, 0.0))
        for i in range(sides):
            j = (i + 1) % sides
            B.face([rings[k][i], rings[k][j], rings[k + 1][j], rings[k + 1][i]], mats[k], ref)
    B.group = None


def disc_y(B, y, r, sides, mat, facing):
    pts = [(r * math.cos(2 * math.pi * i / sides), y, r * math.sin(2 * math.pi * i / sides)) for i in range(sides)]
    B.face(pts, mat, np.array((0.0, y - facing, 0.0)))


def wheel(lod):
    """a 1980 slick: tall rounded sidewalls, a smooth tread, the white lettering ring, a 13 in
    three-piece rim with a polished lip, a dished centre with spokes and the centre-lock nut"""
    B = Builder(lod)
    R, hw, rim = 0.335, 0.21, 0.17
    sides = (16, 10, 7)[lod]
    if lod == 2:
        prof = [(-hw, rim + 0.01), (-hw + 0.03, R), (hw - 0.03, R), (hw, rim + 0.01)]
        mats = ['rubber', 'tread', 'rubber']
    else:
        prof = [(-hw, rim + 0.012), (-hw - 0.010, R - 0.07), (-hw + 0.008, R - 0.016), (-hw + 0.05, R),
                (hw - 0.05, R), (hw - 0.008, R - 0.016), (hw + 0.010, R - 0.06), (hw + 0.012, R - 0.10), (hw, rim + 0.012)]
        mats = ['rubber', 'rubber', 'rubber', 'tread', 'rubber', 'rubber', 'band' if lod == 0 else 'rubber', 'rubber']
    lathe(B, prof, sides, mats, smooth='tyre')
    disc_y(B, -hw, rim + 0.012, sides, 'rimdark', -1)
    if lod == 2:
        disc_y(B, hw - 0.03, rim + 0.012, sides, 'rimdark', 1)
        return part(B)
    # the rim: polished lip, the barrel going in, the dished centre
    lathe(B, [(hw - 0.004, rim + 0.012), (hw - 0.010, rim - 0.006), (hw - 0.10, rim - 0.010)], sides, ['rim', 'rimdark'])
    disc_y(B, hw - 0.09, rim - 0.008, sides, ('disc', 100), 1)
    if lod == 0:
        for k in range(8):            # eight spokes from the hub out to the lip
            a = 2 * math.pi * k / 8
            c, s = math.cos(a), math.sin(a)
            B.rod((0.05 * c, hw - 0.07, 0.05 * s), ((rim - 0.01) * c, hw - 0.015, (rim - 0.01) * s), 0.011, 'rim')
    B.rod((0, hw - 0.08, 0), (0, hw - 0.03, 0), 0.05, 'rimdark', sides=8)
    B.rod((0, hw - 0.035, 0), (0, hw + 0.004, 0), 0.034, 'nut', sides=6)
    return part(B)


def flap():
    """the 1980 rear wing's top flap: 1.085 m wide, its hinge (leading edge) at the origin, running
    back along -X; the team's colour on top, the second colour along the trailing edge"""
    B = Builder(0)
    c, hs = 0.22, 0.540
    sec = [(0.0, 0.010), (-0.08 * c, 0.022), (-0.35 * c, 0.026), (-0.75 * c, 0.024), (-c, 0.040),
           (-c, 0.030), (-0.75 * c, 0.010), (-0.35 * c, 0.002), (-0.08 * c, 0.000), (0.0, 0.004)]
    n = len(sec)
    L = [(x, -hs, z) for (x, z) in sec]; Rr = [(x, hs, z) for (x, z) in sec]
    mid = np.array((-c / 2, 0.0, 0.018))
    B.face(L, 'flap', mid); B.face(Rr, 'flap', mid)
    for i in range(n):
        j = (i + 1) % n
        B.face([L[i], L[j], Rr[j], Rr[i]], 'tip' if i in (3, 4) else 'flap', mid)
    return part(B)


def steering():
    """a small round wheel (about 26 cm across): a padded leather rim on three drilled alloy spokes
    (at 3, 9 and 6 o'clock), a little centre boss, the quick-release on the column; the gloves at a
    quarter to three"""
    B = Builder(0)
    Rw, rr, N = 0.130, 0.016, 28
    # the rim: a torus in the YZ plane
    B.group = 'rim'
    ring = []
    for i in range(N):
        a = 2 * math.pi * i / N
        ring.append(((0.004, Rw * math.cos(a), Rw * math.sin(a)), a))
    for i in range(N):
        (p0, a0), (p1, a1) = ring[i], ring[(i + 1) % N]
        for k in range(6):
            b0, b1 = 2 * math.pi * k / 6, 2 * math.pi * (k + 1) / 6
            def pt(a, b):
                return (0.004 + rr * math.sin(b), (Rw + rr * math.cos(b)) * math.cos(a), (Rw + rr * math.cos(b)) * math.sin(a))
            ref = np.array((0.004, Rw * math.cos((a0 + a1) / 2), Rw * math.sin((a0 + a1) / 2)))
            B.face([pt(a0, b0), pt(a1, b0), pt(a1, b1), pt(a0, b1)], 'leather', ref)
    B.group = None
    # three flat alloy spokes, dished back towards the boss
    for ang in (0.0, math.pi, -math.pi / 2):
        c, s = math.cos(ang), math.sin(ang)
        a = np.array((0.022, 0.026 * c, 0.026 * s)); b = np.array((0.006, (Rw - 0.006) * c, (Rw - 0.006) * s))
        side = np.array((0.0, -s, c)) * 0.016
        th = np.array((0.004, 0, 0))
        V = [a - side, b - side * 0.8, b + side * 0.8, a + side]
        V += [v + th for v in V]
        B.mesh(V, [(0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)], 'alu')
        for f in (0.45, 0.7):                     # lightening holes (dark dots)
            p = a + (b - a) * f - th * 0.6
            B.rod(p, p - np.array((0.002, 0, 0)), 0.005, 'carbon', sides=6)
    # the boss and the horn-push badge, the quick release and the column
    B.rod((0.030, 0, 0), (0.012, 0, 0), 0.030, 'carbon', sides=10)
    B.rod((0.012, 0, 0), (0.008, 0, 0), 0.020, 'red', sides=10)
    B.rod((0.030, 0, 0), (0.060, 0, 0), 0.022, 'alu', sides=8)
    B.rod((0.060, 0, 0), (0.300, 0, -0.030), 0.018, 'carbonlite', sides=8)
    # the gloves round the rim at a quarter to three
    for sgn in (-1, 1):
        y, z = sgn * Rw, 0.010
        B.group = 'glove%d' % sgn
        B.rod((-0.002, y, z + 0.040), (-0.002, y, z - 0.040), 0.028, 'glove', sides=8)
        B.rod((-0.016, y - sgn * 0.010, z + 0.030), (-0.022, y - sgn * 0.036, z + 0.034), 0.010, 'glove', sides=6)
        B.rod((-0.012, y, z - 0.006), (-0.075, y + sgn * 0.018, z - 0.040), 0.032, 'glove', sides=8)
        B.group = None
        B.rod((-0.050, y + sgn * 0.011, z - 0.024), (-0.062, y + sgn * 0.014, z - 0.031), 0.037, 'glovetrim', sides=8)
    return part(B)


def driver():
    """the Blender driver without the HANS (material 'hans': the collar, its back plate and the helmet's
    top intake): 1980 drivers wore a full-face helmet with nothing round the neck"""
    d = json.load(open(PARTS))
    out = []
    for lod in d['driver']:
        mats = lod['mats']
        keep = [i for i, m in enumerate(lod['m']) if mats[m] != 'hans']
        # without the collar the neck showed too much: the helmet sits 9 cm lower on the shoulders
        # (its shell, and the top of the neck tube; the neck's base stays on the shoulders)
        Pk = np.array(lod['p']).reshape(-1, 3, 3)[keep]
        Mk = [mats[lod['m'][i]] for i in keep]
        for t, m in enumerate(Mk):
            for v in range(3):
                if m in ('helmet', 'visor', 'stripe', 'visorband') or (m == 'balaclava' and Pk[t, v, 2] > -0.15):
                    Pk[t, v, 2] -= 0.09
        P = Pk.reshape(-1).tolist()
        N = np.array(lod['n']).reshape(-1, 9)[keep].reshape(-1).tolist()
        out.append(dict(tris=len(keep), p=P, n=N, mats=mats, m=[lod['m'][i] for i in keep]))
    return out


def main():
    res = dict(format='FL-parts-1980', wheels=[wheel(l) for l in (0, 1, 2)], flap=flap(), steering=steering(), driver=driver(),
               palette=dict(leather=(0.10, 0.08, 0.07)))
    json.dump(res, open(OUT, 'w'), separators=(',', ':'))
    print('wheel', [w['tris'] for w in res['wheels']], 'flap', res['flap']['tris'], 'steering', res['steering']['tris'],
          'driver', [x['tris'] for x in res['driver']])


if __name__ == '__main__':
    main()
