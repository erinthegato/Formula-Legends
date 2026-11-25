"""Formula Legends -- 1998, 2016 and 2025 F1 cars, built in Blender, low-poly for the PS Vita.

Run it in Blender (Scripting tab: open this file, Run Script / Alt+P) in the same scene as the 2010 car
(tools/blender/f1_v8_car.py). Each car gets its own collection, FL_<year>_Car, with:

  FL_<year>_LOD0 / LOD1 / LOD2   near, middle and far models (three levels of detail)
  FL_<year>_Preview              its wheels (the game draws wheels itself, so they are not exported)

and one empty, FL_<year>, that holds the whole car: move the empty to move the car. They stand side by
side about a metre apart (wheel to wheel): 1998, the 2010 car (at the origin), 2016, 2025.

Same style and conventions as the 2010 car: flat-shaded, no driver, the cockpit hollowed out for the
game's steering wheel and its display; +X forward, +Y left, +Z up, metres. Materials are the game's
swatches, FL_<swatch>[_<shade %>].<year> (livery = the team livery, second = the team's second colour);
the colours you see in Blender are just one look per car.

Export one car for tools/gen_f1.py (its LODs, relative to its empty):
    export_json('1998', 'C:/path/to/f1_1998_car.json')
"""
import bpy, bmesh, math, json, os
from mathutils import Vector

SPACING = 1.0      # metres between the cars' widest points

COLOURS = {   # one look per car in Blender (the game paints its own liveries)
    '1998': dict(livery=(0.03, 0.30, 0.12), second=(0.95, 0.95, 0.93)),
    '2016': dict(livery=(0.26, 0.06, 0.40), second=(1.00, 0.80, 0.05)),
    '2025': dict(livery=(0.40, 0.04, 0.08), second=(0.95, 0.95, 0.95)),
}
FIXED = dict(carbon=(0.025, 0.025, 0.03), grey=(0.45, 0.46, 0.48), rubber=(0.03, 0.03, 0.03),
             glass=(0.10, 0.12, 0.16), red=(0.9, 0.05, 0.05), white=(0.95, 0.95, 0.92),
             orange=(1.0, 0.50, 0.04), green=(0.10, 0.85, 0.20))
# the safety cars: each era its own body colour and trim
COLOURS['SC1998'] = dict(livery=(0.08, 0.30, 0.17), second=(0.72, 0.73, 0.75))    # deep green, silver trim
COLOURS['SC2010'] = dict(livery=(0.80, 0.81, 0.83), second=(0.06, 0.06, 0.07))    # bright silver, black trim
COLOURS['SC2016'] = dict(livery=(0.22, 0.23, 0.25), second=(0.85, 0.08, 0.06))    # graphite, red trim
COLOURS['SC2025'] = dict(livery=(0.98, 0.76, 0.02), second=(0.03, 0.03, 0.035))   # vivid yellow, black


def material(year, sw, shade=100):
    name = ('FL_%s' % sw if shade == 100 else 'FL_%s_%d' % (sw, shade)) + '.' + year
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    c = COLOURS[year].get(sw) or FIXED.get(sw, (0.5, 0.5, 0.5))
    k = min(shade / 100.0, 1.0)
    m.diffuse_color = (c[0] * k, c[1] * k, c[2] * k, 1.0)
    try:
        m.use_nodes = True
        b = m.node_tree.nodes.get('Principled BSDF')
        if b: b.inputs['Base Color'].default_value = m.diffuse_color
    except Exception:
        pass
    return m


def collection(name, parent):
    c = bpy.data.collections.get(name) or bpy.data.collections.new(name)
    if c.name not in parent.children:
        parent.children.link(c)
    return c


class Builder:
    def __init__(self, year, lod, col, root):
        self.year = year; self.lod = lod; self.col = col; self.root = root

    def mesh(self, name, V, F, mat):
        full = '%s_%s.L%d' % (self.year, name, self.lod)
        me = bpy.data.meshes.new(full)
        me.from_pydata([tuple(v) for v in V], [], [tuple(f) for f in F])
        me.validate()
        bm = bmesh.new(); bm.from_mesh(me)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        bm.to_mesh(me); bm.free()
        me.materials.append(material(self.year, *mat) if isinstance(mat, tuple) else material(self.year, mat))
        ob = bpy.data.objects.new(full, me)
        self.col.objects.link(ob)
        ob.parent = self.root
        return ob

    def loft(self, name, secs, mat, yc=0.0, n=8, e=0.6):
        """rounded sections (x, half width, bottom, top) along X"""
        v = []; f = []
        for (x, w, z0, z1) in secs:
            zc = (z0 + z1) / 2; h = (z1 - z0) / 2
            for i in range(n):
                a = 2 * math.pi * (i + 0.5) / n
                ca, sa = math.cos(a), math.sin(a)
                v.append((x, yc + w * math.copysign(abs(ca) ** e, ca), zc + h * math.copysign(abs(sa) ** e, sa)))
        k = len(secs)
        for s in range(k - 1):
            for i in range(n):
                j = (i + 1) % n
                f.append((s * n + i, s * n + j, (s + 1) * n + j, (s + 1) * n + i))
        f.append(tuple(range(n)))
        f.append(tuple(range((k - 1) * n, k * n)))
        return self.mesh(name, v, f, mat)

    def prism(self, name, pts, depth, yc, mat):
        """an (x, z) outline pushed out sideways: wings, endplates, fins"""
        n = len(pts)
        v = [(x, yc - depth / 2, z) for (x, z) in pts] + [(x, yc + depth / 2, z) for (x, z) in pts]
        f = [tuple(range(n)), tuple(range(n, 2 * n))]
        for i in range(n):
            j = (i + 1) % n
            f.append((i, j, n + j, n + i))
        return self.mesh(name, v, f, mat)

    def plate(self, name, pts, depth, xc, mat):
        """a (y, z) outline pushed out along X: bargeboards, winglets"""
        n = len(pts)
        v = [(xc - depth / 2, y, z) for (y, z) in pts] + [(xc + depth / 2, y, z) for (y, z) in pts]
        f = [tuple(range(n)), tuple(range(n, 2 * n))]
        for i in range(n):
            j = (i + 1) % n
            f.append((i, j, n + j, n + i))
        return self.mesh(name, v, f, mat)

    def box(self, name, lo, hi, mat):
        V = [(x, y, z) for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
        F = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
        return self.mesh(name, V, F, mat)

    def rod(self, name, p1, p2, r, mat):
        p1 = Vector(p1); p2 = Vector(p2)
        q = (p2 - p1).to_track_quat('Z', 'Y')
        v = []
        for p in (p1, p2):
            for (a, b) in ((r, 0), (0, r), (-r, 0), (0, -r)):
                v.append(tuple(p + q @ Vector((a, b, 0))))
        return self.mesh(name, v, [(0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)], mat)

    def tube(self, name, pts, r, mat, sides=6):
        """a tube along a path of points"""
        pts = [Vector(p) for p in pts]
        V = []; F = []
        for i, p in enumerate(pts):
            a = pts[max(i - 1, 0)]; b = pts[min(i + 1, len(pts) - 1)]
            t = (b - a).normalized()
            ref = Vector((0, 0, 1)) if abs(t.z) < 0.9 else Vector((1, 0, 0))
            u = t.cross(ref).normalized(); w = t.cross(u)
            for k in range(sides):
                ang = k * 2 * math.pi / sides + math.pi / sides
                V.append(tuple(p + r * (math.cos(ang) * u + math.sin(ang) * w)))
        for i in range(len(pts) - 1):
            for k in range(sides):
                a = i * sides + k; b = i * sides + (k + 1) % sides
                F.append((a, b, b + sides, a + sides))
        F.append(tuple(range(sides))[::-1])
        F.append(tuple(range((len(pts) - 1) * sides, len(pts) * sides)))
        return self.mesh(name, V, F, mat)

    def shell(self, name, rings, mat):
        n = len(rings[0])
        V = [p for r in rings for p in r]
        F = [(i * n + k, i * n + (k + 1) % n, (i + 1) * n + (k + 1) % n, (i + 1) * n + k) for i in range(len(rings) - 1) for k in range(n)]
        F += [tuple(range(n))[::-1], tuple(range((len(rings) - 1) * n, len(rings) * n))]
        return self.mesh(name, V, F, mat)

    def tyre(self, name, ax, y, R, w, rim, grooves=0, cover=False, seg=16):
        """a tyre (rings along Y; grooves cut into the tread for 1998) and its rim / wheel cover"""
        ys, rs = [], []
        if grooves:
            nb = grooves * 2 + 1
            for i in range(nb + 1):
                ys.append(-w / 2 + w * i / nb)
            for i in range(nb):
                rs.append(R if i % 2 == 0 else R - 0.008)
            rings = []
            for i in range(nb):
                rings.append((ys[i], rs[i])); rings.append((ys[i + 1], rs[i]))
        else:
            rings = [(-w / 2, R), (w / 2, R)]
        V = []
        for (yy, rr) in rings:
            for k in range(seg):
                a = 2 * math.pi * k / seg
                V.append((ax + rr * math.cos(a), y + yy, R + rr * math.sin(a)))
        F = []
        for i in range(len(rings) - 1):
            for k in range(seg):
                a = i * seg + k; b = i * seg + (k + 1) % seg
                F.append((a, b, b + seg, a + seg))
        F.append(tuple(range(seg)))
        F.append(tuple(range((len(rings) - 1) * seg, len(rings) * seg)))
        self.mesh(name, V, F, ('rubber', 100))
        # rim face on the outer side (a disc for the wheel cover)
        side = 1 if y > 0 else -1
        yo = y + side * (w / 2 + 0.002)
        rv = [(ax, yo, R)] + [(ax + rim * math.cos(2 * math.pi * k / 12), yo, R + rim * math.sin(2 * math.pi * k / 12)) for k in range(12)]
        self.mesh(name + 'Rim', rv, [(0, 1 + k, 1 + (k + 1) % 12) for k in range(12)], ('grey', 100 if cover else 70))


def opening(spec, n, z):
    x0, x1, hw = spec
    cx, a = (x0 + x1) / 2, (x0 - x1) / 2
    pts = []
    for k in range(n):
        t = 2 * math.pi * k / n
        c, s = math.cos(t), math.sin(t)
        pts.append((cx + a * math.copysign(abs(c) ** 0.6, c), hw * math.copysign(abs(s) ** 0.6, s), z))
    return pts


def apply_cut(ob, cutter):
    """hollow the cockpit out of the tub: the cutter's sides and bottom become its walls and floor"""
    mod = ob.modifiers.new('cockpit', 'BOOLEAN')
    mod.operation = 'DIFFERENCE'; mod.object = cutter
    for attr, val in (('solver', 'EXACT'), ('material_mode', 'TRANSFER')):
        try: setattr(mod, attr, val)
        except Exception: pass
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    old = ob.data; name = old.name
    ob.modifiers.remove(mod)
    ob.data = me
    bpy.data.meshes.remove(old)
    me.name = name
    bpy.data.objects.remove(cutter, do_unlink=True)


def tub_and_cockpit(B, secs, open_spec, n):
    tub = B.loft('Tub', secs, 'livery', n=n)
    if B.lod < 2:
        k = 16 if B.lod == 0 else 10
        apply_cut(tub, B.shell('Cutter', [opening(open_spec, k, 0.26), opening(open_spec, k, 1.40)], ('carbon', 45)))
    else:
        rim = opening(open_spec, 8, 0.70)
        cen = ((open_spec[0] + open_spec[1]) / 2, 0.0, 0.68)
        B.mesh('CockpitDark', [cen] + rim, [(0, j + 1, (j + 1) % 8 + 1) for j in range(8)], ('carbon', 35))


def suspension(B, xf, xr, yf, yr, R, hwf=0.12, hwr=0.20):
    if B.lod != 0:
        return
    r = 0.012
    for s, sn in ((1, 'L'), (-1, 'R')):
        for dx in (0.15, -0.15):
            B.rod('FUpper' + sn, (xf + dx, s * hwf, R + 0.17), (xf, s * (yf - 0.13), R + 0.12), r, 'carbon')
            B.rod('FLower' + sn, (xf + dx * 1.3, s * hwf, R - 0.03), (xf, s * (yf - 0.13), R - 0.11), r, 'carbon')
            B.rod('RUpper' + sn, (xr + dx, s * hwr, R + 0.12), (xr, s * (yr - 0.17), R + 0.12), r, 'carbon')
            B.rod('RLower' + sn, (xr + dx * 1.3, s * hwr, R - 0.13), (xr, s * (yr - 0.17), R - 0.13), r, 'carbon')


def mirrors(B, x, y, z, ay, az):
    """mirrors on stalks; each stalk starts at (ay, az), a point inside the bodywork, so it is attached"""
    if B.lod > 1:
        return
    for s, sn in ((1, 'L'), (-1, 'R')):
        B.box('Mirror' + sn, (x - 0.02, s * y - 0.06, z - 0.025), (x + 0.02, s * y + 0.06, z + 0.025), 'livery')
        B.rod('MirrorStalk' + sn, (x + 0.03, s * ay, az), (x, s * (y - 0.02), z - 0.01), 0.012, 'carbon')


# ------------------------------------------------------------------ 1998: narrow, grooved tyres
# 1998 rules: 1.80 m wide with wheels (3.3), bodywork 1.40 m wide ahead of the rear axle (3.4) and 1.00 m
# behind it (3.5), 0.95 m tall (3.6); front wing 5-25 cm high (3.7) and no more than 90 cm ahead of the
# front axle away from the centre (3.13); rear overhang 50 cm (3.13); behind the rear axle 60 cm high up to
# 15 cm back, 80 cm beyond (3.9, 3.10); wheels 660 mm, front 305-380 mm and rear 365-380 mm wide (12.2).
CAR_1998 = dict(xf=1.48, xr=-1.50, R=0.33, yf=0.745, yr=0.708, wf=0.305, wr=0.38, rim=0.17, half=0.90)


def car_1998(B):
    lod = B.lod; n = {0: 8, 1: 6, 2: 4}[lod]
    tub_and_cockpit(B, [(2.42, 0.07, 0.30, 0.40), (2.15, 0.12, 0.30, 0.50), (1.70, 0.18, 0.30, 0.60),
                        (1.20, 0.24, 0.18, 0.66), (0.65, 0.29, 0.10, 0.68), (0.00, 0.30, 0.08, 0.65),
                        (-0.35, 0.28, 0.08, 0.60)], (0.62, 0.04, 0.21), n)
    # engine cover with a round airbox over the driver; no fin
    B.loft('EngineCover', [(0.02, 0.11, 0.55, 0.92), (-0.25, 0.15, 0.50, 0.93), (-0.70, 0.19, 0.38, 0.78),
                           (-1.20, 0.17, 0.10, 0.58), (-1.60, 0.11, 0.10, 0.44), (-1.92, 0.07, 0.12, 0.34)], 'livery', n=n)
    rain_light(B, -1.92, 0.25)
    if lod < 2:
        B.box('AirboxInlet', (0.01, -0.07, 0.76), (0.04, 0.07, 0.89), ('carbon', 40))
    for s, sn in ((1, 'L'), (-1, 'R')):
        B.loft('Sidepod' + sn, [(0.45, 0.14, 0.12, 0.48), (0.15, 0.18, 0.10, 0.52), (-0.45, 0.16, 0.10, 0.46),
                                (-1.00, 0.10, 0.10, 0.34), (-1.40, 0.06, 0.10, 0.25)], 'livery', yc=s * 0.42, n=n)
        if lod < 2:
            B.box('PodInlet' + sn, (0.45, s * 0.42 - 0.10, 0.15), (0.47, s * 0.42 + 0.10, 0.44), ('carbon', 30))
            # tall bargeboard behind the front wheels
            B.plate('Bargeboard' + sn, [(s * 0.37, 0.10), (s * 0.37, 0.36), (s * 0.39, 0.40), (s * 0.39, 0.10)], 0.34, 0.95, 'second')
        # front wing 1.40 m wide, 35-90 cm ahead of the front axle, 5-25 cm high
        B.prism('FWFlap' + sn, [(2.12, 0.10), (2.00, 0.18), (1.97, 0.18), (2.09, 0.095)], 0.45, s * 0.45, 'second')
        B.prism('FWEndplate' + sn, [(2.38, 0.05), (2.38, 0.17), (2.20, 0.24), (1.88, 0.22), (1.88, 0.05)], 0.015, s * 0.6925, 'livery')
        if lod < 2:
            B.prism('NosePylon' + sn, [(2.30, 0.08), (2.27, 0.31), (2.10, 0.32), (2.10, 0.08)], 0.012, s * 0.06, 'carbon')
        # rear wing endplates: from 16 cm behind the rear axle (under 60 cm there) to 50 cm, 80 cm tall
        B.prism('RWEndplate' + sn, [(-1.66, 0.30), (-1.66, 0.60), (-1.72, 0.80), (-2.00, 0.80), (-2.00, 0.30)], 0.015, s * 0.4925, 'livery')
    B.prism('FrontWing', [(2.36, 0.07), (2.30, 0.10), (2.12, 0.095), (1.90, 0.075), (2.10, 0.06)], 1.40, 0, 'carbon')
    # stacked rear wing, 1.0 m wide, in the 50-80 cm band; beam wing at 30+ cm
    B.prism('RWMain', [(-1.68, 0.58), (-1.72, 0.61), (-1.84, 0.62), (-1.95, 0.61), (-1.98, 0.59), (-1.84, 0.575)], 0.985, 0, 'livery')
    B.prism('RWUpper', [(-1.72, 0.66), (-1.77, 0.69), (-1.88, 0.70), (-1.92, 0.685), (-1.80, 0.655)], 0.985, 0, 'second')
    if lod < 2:
        B.prism('RWTop', [(-1.82, 0.73), (-1.88, 0.76), (-1.97, 0.785), (-1.98, 0.76), (-1.88, 0.72)], 0.985, 0, 'second')
        B.prism('BeamWing', [(-1.70, 0.34), (-1.74, 0.37), (-1.88, 0.36), (-1.92, 0.34)], 0.985, 0, 'carbon')
    B.prism('RWPillar', [(-1.55, 0.22), (-1.76, 0.58), (-1.86, 0.58), (-1.70, 0.22)], 0.02, 0, 'carbon')
    B.box('Floor', (-1.30, -0.65, 0.035), (1.00, 0.65, 0.10), 'carbon')
    B.box('FloorRear', (-1.50, -0.45, 0.035), (-1.30, 0.45, 0.10), 'carbon')
    # behind the rear axle only the middle 30 cm may stay low (3.10)
    B.prism('Diffuser', [(-1.50, 0.05), (-1.95, 0.05), (-1.95, 0.20), (-1.75, 0.17)], 0.30, 0, ('carbon', 60))
    B.loft('Deck', [(0.02, 0.52, 0.10, 0.44), (-0.45, 0.50, 0.10, 0.40), (-1.00, 0.42, 0.10, 0.30),
                    (-1.40, 0.28, 0.10, 0.22), (-1.90, 0.10, 0.12, 0.20)], 'livery', n=n, e=0.35)
    mirrors(B, 0.50, 0.36, 0.66, ay=0.18, az=0.45)
    c = CAR_1998
    suspension(B, c['xf'], c['xr'], c['yf'], c['yr'], c['R'])


# ------------------------------------------------------------------ 2016: low finger nose, 1.65 m wing
# 2016 rules: 1.80 m wide (3.3), 0.95 m tall (3.6); 1.40 m between the axles, 1.65 m ahead of the front
# axle (3.4.1); behind the rear axle 1.00 m wide below 150 mm, 0.75 m above (3.5); front wing 75-275 mm
# high (3.7.1); nose tip at or under 220 mm (3.7.9); overhangs 1.20 m front (1.00 m away from the centre)
# and 0.60 m rear, 0.35 m beyond 375 mm from the centre (3.14, 3.10); wheels 660 mm, front 305-355 mm,
# rear 365-380 mm (12.4).
CAR_2016 = dict(xf=1.70, xr=-1.55, R=0.33, yf=0.745, yr=0.708, wf=0.305, wr=0.38, rim=0.17, half=0.90)


def car_2016(B):
    lod = B.lod; n = {0: 8, 1: 6, 2: 4}[lod]
    tub_and_cockpit(B, [(2.82, 0.05, 0.12, 0.21), (2.62, 0.08, 0.15, 0.27), (2.30, 0.11, 0.24, 0.44),
                        (1.90, 0.16, 0.32, 0.56), (1.45, 0.21, 0.30, 0.63), (1.05, 0.25, 0.18, 0.67),
                        (0.60, 0.30, 0.10, 0.69), (0.00, 0.31, 0.08, 0.66), (-0.35, 0.29, 0.08, 0.62)], (0.72, 0.06, 0.22), n)
    B.loft('EngineCover', [(0.02, 0.13, 0.55, 0.93), (-0.25, 0.16, 0.50, 0.94), (-0.70, 0.20, 0.38, 0.80),
                           (-1.20, 0.17, 0.10, 0.60), (-1.70, 0.10, 0.10, 0.40), (-1.95, 0.06, 0.12, 0.32)], 'livery', n=n)
    rain_light(B, -1.95, 0.24)
    if lod < 2:
        B.box('AirboxInlet', (0.01, -0.09, 0.78), (0.04, 0.09, 0.91), ('carbon', 40))
        # the small "monkey seat" wing over the exhaust (within 100 mm of the centre)
        B.prism('MonkeySeat', [(-1.62, 0.50), (-1.66, 0.53), (-1.78, 0.53), (-1.80, 0.50)], 0.20, 0, 'carbon')
    for s, sn in ((1, 'L'), (-1, 'R')):
        B.loft('Sidepod' + sn, [(0.55, 0.16, 0.12, 0.50), (0.20, 0.21, 0.10, 0.55), (-0.40, 0.18, 0.10, 0.48),
                                (-1.00, 0.11, 0.10, 0.34), (-1.45, 0.05, 0.10, 0.22)], 'livery', yc=s * 0.45, n=n)
        if lod < 2:
            B.box('PodInlet' + sn, (0.55, s * 0.45 - 0.12, 0.15), (0.57, s * 0.45 + 0.12, 0.46), ('carbon', 30))
            B.plate('Bargeboard' + sn, [(s * 0.42, 0.10), (s * 0.42, 0.34), (s * 0.44, 0.38), (s * 0.44, 0.10)], 0.45, 1.12, 'carbon')
            B.prism('TurningVane' + sn, [(2.05, 0.10), (2.05, 0.26), (1.75, 0.26), (1.75, 0.10)], 0.01, s * 0.16, 'carbon')
        # 1.65 m front wing: two stacked flaps a side, 75-275 mm high, outboard parts <= 1.0 m ahead
        B.prism('FWFlap1' + sn, [(2.44, 0.11), (2.32, 0.20), (2.29, 0.20), (2.40, 0.10)], 0.52, s * 0.53, 'second')
        if lod < 2:
            B.prism('FWFlap2' + sn, [(2.38, 0.20), (2.28, 0.27), (2.25, 0.27), (2.34, 0.19)], 0.52, s * 0.55, 'second')
        B.prism('FWEndplate' + sn, [(2.70, 0.075), (2.70, 0.22), (2.50, 0.275), (2.25, 0.27), (2.25, 0.075)], 0.015, s * 0.8175, 'livery')
        if lod < 2:
            B.prism('NosePylon' + sn, [(2.62, 0.09), (2.60, 0.15), (2.45, 0.21), (2.45, 0.09)], 0.012, s * 0.05, 'carbon')
        # rear wing endplates inside 375 mm of the centre, so they may reach 600 mm behind the axle
        B.prism('RWEndplate' + sn, [(-1.78, 0.35), (-1.78, 0.83), (-1.86, 0.95), (-2.15, 0.95), (-2.15, 0.35)], 0.015, s * 0.3675, 'livery')
    B.prism('FrontWing', [(2.70, 0.075), (2.62, 0.10), (2.42, 0.095), (2.30, 0.08), (2.50, 0.075)], 1.62, 0, 'carbon')
    if lod < 2:
        B.box('NoseTipWing', (2.70, -0.20, 0.075), (2.86, 0.20, 0.10), 'carbon')     # the centre section ahead
    # 0.75 m rear wing with its DRS flap, top at 0.95 m, and a beam wing
    B.prism('RWMain', [(-1.82, 0.76), (-1.88, 0.79), (-2.00, 0.80), (-2.10, 0.79), (-2.12, 0.77), (-1.98, 0.755)], 0.72, 0, 'livery')
    B.prism('RWFlap', [(-2.02, 0.82), (-2.07, 0.88), (-2.15, 0.93), (-2.145, 0.90), (-2.05, 0.81)], 0.72, 0, 'second')
    if lod < 2:
        B.prism('BeamWing', [(-1.82, 0.40), (-1.86, 0.43), (-1.98, 0.42), (-2.02, 0.40)], 0.72, 0, 'carbon')
    B.prism('RWPillar', [(-1.62, 0.22), (-1.90, 0.76), (-1.98, 0.76), (-1.80, 0.22)], 0.02, 0, 'carbon')
    B.box('Floor', (-1.25, -0.68, 0.035), (1.05, 0.68, 0.10), 'carbon')
    B.box('FloorRear', (-1.85, -0.50, 0.035), (-1.25, 0.50, 0.10), 'carbon')
    B.prism('Diffuser', [(-1.55, 0.05), (-2.05, 0.05), (-2.05, 0.26), (-1.85, 0.22)], 0.74, 0, ('carbon', 60))
    B.loft('Deck', [(0.04, 0.56, 0.10, 0.46), (-0.40, 0.54, 0.10, 0.42), (-1.00, 0.45, 0.10, 0.31),
                    (-1.45, 0.28, 0.10, 0.21), (-1.90, 0.10, 0.12, 0.20)], 'livery', n=n, e=0.35)
    mirrors(B, 0.66, 0.42, 0.66, ay=0.18, az=0.45)
    c = CAR_2016
    suspension(B, c['xf'], c['xr'], c['yf'], c['yr'], c['R'])


# ------------------------------------------------------------------ 2025: wide, ground effect, 18" wheels
# 2025 rules: nothing but the wheels outboard of Y=1000 mm (3.4.1), wheelbase 3600 mm (3.4.2); tyres
# 725 mm, front 345-375 mm and rear 440-470 mm wide on 18" rims (490.6 mm lip) (10.7, 10.8); rear wing
# about 300-650 mm behind the rear axle, its mainplane ~0.90 m and flap ~0.875 m high, nothing outboard
# of Y=490 (3.10, 3.15); exhaust exit 550 mm behind the axle above 350 mm (3.8.2); mirrors at
# Y=440-630, under Z=720 (3.6.4); halo fixed at Z=660 in front and Z=695 at the back (12.4.2).
CAR_2025 = dict(xf=1.85, xr=-1.75, R=0.3625, yf=0.813, yr=0.773, wf=0.36, wr=0.45, rim=0.245, half=1.0, cover=True)


def car_2025(B):
    lod = B.lod; n = {0: 8, 1: 6, 2: 4}[lod]
    tub_and_cockpit(B, [(2.95, 0.08, 0.12, 0.20), (2.65, 0.12, 0.16, 0.32), (2.25, 0.16, 0.24, 0.46),
                        (1.85, 0.20, 0.30, 0.58), (1.40, 0.25, 0.26, 0.64), (0.95, 0.29, 0.16, 0.68),
                        (0.50, 0.31, 0.10, 0.70), (0.00, 0.31, 0.08, 0.68), (-0.35, 0.29, 0.08, 0.64)], (0.80, 0.08, 0.22), n)
    B.loft('EngineCover', [(0.05, 0.14, 0.55, 0.95), (-0.25, 0.17, 0.50, 0.95), (-0.80, 0.21, 0.36, 0.82),
                           (-1.40, 0.17, 0.10, 0.58), (-1.90, 0.09, 0.10, 0.42), (-2.20, 0.06, 0.12, 0.36)], 'livery', n=n)
    rain_light(B, -2.20, 0.26)
    if lod < 2:
        B.box('AirboxInlet', (0.04, -0.09, 0.80), (0.07, 0.09, 0.93), ('carbon', 40))
        B.prism('Fin', [(-0.55, 0.88), (-1.95, 0.72), (-1.95, 0.45), (-0.80, 0.62)], 0.01, 0, 'second')
        # the tailpipe: exits 550 mm behind the rear axle, above 350 mm
        B.tube('Exhaust', [(-2.18, 0, 0.42), (-2.30, 0, 0.42)], 0.058, ('grey', 60), sides=6)
    for s, sn in ((1, 'L'), (-1, 'R')):
        # ground-effect sidepods: high inlets, deep undercut, sloping down to the floor
        B.loft('Sidepod' + sn, [(0.70, 0.18, 0.30, 0.62), (0.35, 0.24, 0.22, 0.62), (-0.30, 0.22, 0.12, 0.54),
                                (-0.90, 0.15, 0.10, 0.40), (-1.50, 0.08, 0.10, 0.26)], 'livery', yc=s * 0.52, n=n)
        if lod < 2:
            B.box('PodInlet' + sn, (0.70, s * 0.52 - 0.12, 0.34), (0.72, s * 0.52 + 0.12, 0.58), ('carbon', 30))
            # venturi tunnel inlets under the sidepods, and the floor's edge wing
            B.box('TunnelInlet' + sn, (1.10, s * 0.45 - 0.16, 0.04), (1.16, s * 0.45 + 0.16, 0.22), ('carbon', 25))
            B.prism('FloorEdge' + sn, [(1.10, 0.10), (0.40, 0.10), (0.40, 0.16), (0.95, 0.20)], 0.012, s * 0.80, 'carbon')
            wheel_winglet(B, s, sn)
        # four-element front wing (0.55-1.10 m ahead of the front axle); curved endplates
        B.prism('FWEndplate' + sn, [(2.98, 0.03), (2.98, 0.16), (2.72, 0.30), (2.36, 0.30), (2.36, 0.03)], 0.015, s * 0.94, 'livery')
        # rear wing tips (endplates) to 650 mm behind the axle, down to 585 mm
        B.prism('RWTip' + sn, [(-2.00, 0.585), (-2.00, 0.86), (-2.12, 0.95), (-2.40, 0.95), (-2.40, 0.70), (-2.28, 0.585)], 0.015, s * 0.495, 'livery')
    B.prism('FrontWing', [(2.95, 0.06), (2.86, 0.09), (2.66, 0.085), (2.55, 0.07), (2.76, 0.055)], 1.86, 0, 'carbon')
    B.prism('FWElement2', [(2.62, 0.10), (2.50, 0.15), (2.47, 0.15), (2.58, 0.095)], 1.80, 0, 'second')
    if lod < 2:
        B.prism('FWElement3', [(2.50, 0.16), (2.42, 0.21), (2.39, 0.21), (2.46, 0.155)], 1.72, 0, 'carbon')
        B.prism('FWElement4', [(2.42, 0.22), (2.36, 0.27), (2.33, 0.27), (2.38, 0.215)], 1.60, 0, 'second')
    # rear wing: two elements (mainplane + DRS flap), 0.98 m wide, two-element beam wing; swan-neck pillar
    B.prism('RWMain', [(-2.05, 0.80), (-2.11, 0.83), (-2.25, 0.84), (-2.34, 0.83), (-2.36, 0.81), (-2.22, 0.795)], 0.975, 0, 'livery')
    B.prism('RWFlap', [(-2.28, 0.86), (-2.32, 0.91), (-2.40, 0.95), (-2.395, 0.92), (-2.31, 0.85)], 0.975, 0, 'second')
    if lod < 2:
        B.prism('BeamWing1', [(-2.02, 0.38), (-2.06, 0.41), (-2.18, 0.40), (-2.22, 0.38)], 0.90, 0, 'carbon')
        B.prism('BeamWing2', [(-2.14, 0.44), (-2.18, 0.47), (-2.28, 0.46), (-2.30, 0.44)], 0.90, 0, 'carbon')
    B.prism('RWPillar', [(-2.20, 0.84), (-2.28, 0.84), (-2.10, 0.30), (-1.98, 0.30)], 0.02, 0, 'carbon')
    B.box('Floor', (-1.40, -0.80, 0.035), (1.15, 0.80, 0.10), 'carbon')
    B.box('FloorRear', (-2.10, -0.55, 0.035), (-1.40, 0.55, 0.10), 'carbon')
    B.prism('Diffuser', [(-1.75, 0.06), (-2.22, 0.06), (-2.22, 0.36), (-2.02, 0.30)], 1.0, 0, ('carbon', 60))
    B.loft('Deck', [(0.06, 0.66, 0.10, 0.46), (-0.30, 0.66, 0.10, 0.44), (-0.90, 0.56, 0.10, 0.32),
                    (-1.50, 0.34, 0.10, 0.22), (-2.10, 0.10, 0.12, 0.20)], 'livery', n=n, e=0.35)
    mirrors(B, 0.62, 0.55, 0.68, ay=0.48, az=0.56)
    halo(B)
    c = CAR_2025
    suspension(B, c['xf'], c['xr'], c['yf'], c['yr'], c['R'], hwf=0.14, hwr=0.22)


def wheel_winglet(B, s, sn, gap=0.035, half_angle=38.0, t=0.01):
    """2025: a thin winglet curved round the top of the front tyre, a little above the tread, held by
    a strut from its inner edge down to the side of the nose"""
    c = CAR_2025
    ax, R, yf, wf = c['xf'], c['R'], c['yf'], c['wf']
    y0, y1 = s * (yf - wf / 2 - 0.03), s * (yf + 0.05)      # from just inboard of the tyre to past its middle
    k = 7 if B.lod == 0 else 4
    V = []
    for i in range(k):
        a = math.radians(90 - half_angle + 2 * half_angle * i / (k - 1))
        for rr in (R + gap, R + gap + t):
            for y in (y0, y1):
                V.append((ax + rr * math.cos(a), y, R + rr * math.sin(a)))
    F = []
    for i in range(k - 1):
        b, n = i * 4, (i + 1) * 4          # per point: inner y0, inner y1, outer y0, outer y1
        F += [(b, b + 1, n + 1, n), (b + 2, n + 2, n + 3, b + 3), (b, n, n + 2, b + 2), (b + 1, b + 3, n + 3, n + 1)]
    F += [(0, 2, 3, 1), ((k - 1) * 4, (k - 1) * 4 + 1, (k - 1) * 4 + 3, (k - 1) * 4 + 2)]
    B.mesh('WheelWinglet' + sn, V, F, 'carbon')
    # the support: a flat plate under the winglet's inner edge, a little shorter than the winglet,
    # running down to the side of the nose
    r = R + gap
    xs = 0.85 * r * math.sin(math.radians(half_angle))          # 85 % of the winglet's length
    top = [(ax + x, y0, R + math.sqrt(r * r - x * x) + 0.005) for x in (xs, xs / 2, 0.0, -xs / 2, -xs)]   # into the winglet
    bot = [(ax - 0.80 * xs, s * 0.19, 0.48), (ax + 0.80 * xs, s * 0.19, 0.48)]
    front = [Vector(p) for p in top + bot]
    nrm = (front[0] - front[4]).cross(front[6] - front[4]).normalized() * 0.006
    V = [tuple(p + nrm) for p in front] + [tuple(p - nrm) for p in front]
    m = len(front)
    F = [tuple(range(m)), tuple(range(2 * m - 1, m - 1, -1))]
    F += [(i, (i + 1) % m, m + (i + 1) % m, m + i) for i in range(m)]
    B.mesh('WingletSupport' + sn, V, F, 'carbon')


def rain_light(B, x, zc):
    """the single red rear light in the middle of the rear crash structure (the game lights it: rain,
    energy harvesting, pit lane limiter -- Renderer::rearLightMode). A dark lens when off."""
    if B.lod < 2:
        B.box('RainLight', (x - 0.012, -0.042, zc - 0.03), (x + 0.008, 0.042, zc + 0.03), ('red', 45))


def halo(B, cx=0.20, r=0.26, z_back=0.78, z_front=0.86, rim=0.66, post_x=0.81, z_mount=0.695):
    """the 2025 halo: a half circle round the cockpit (open at the back), curving slightly upwards to
    the front; its two back ends run back into the sides of the airbox behind the cockpit, and a centre
    pillar holds the front on the nose"""
    k = {0: 9, 1: 5, 2: 4}[B.lod]                 # points round the half circle
    sides = 6 if B.lod == 0 else 4
    hoop = []
    for i in range(k):
        a = -math.pi / 2 + math.pi * i / (k - 1)  # -90 deg (right) .. +90 deg (left), through the front
        lift = math.cos(a)                        # 0 at the back ends, 1 at the front
        hoop.append((cx + r * math.cos(a), r * math.sin(a), z_back + (z_front - z_back) * lift ** 0.8))
    # the back ends: curving in and back into the airbox's sides (it starts at x 0.05, 0.14 wide)
    def mount(end):
        sy = 1 if end[1] > 0 else -1
        return [(0.0, sy * 0.12, z_mount), (0.10, sy * 0.19, (z_mount + z_back) / 2)]
    path = mount(hoop[0]) + hoop + mount(hoop[-1])[::-1]
    B.tube('Halo', path, 0.024, 'carbon', sides=sides)
    # the centre pillar: from the front of the hoop down onto the front edge of the cockpit opening
    # (the opening's front is at x 0.80)
    front = hoop[k // 2]
    B.tube('HaloPillar', [front, ((front[0] + post_x) / 2, 0.0, front[2] - 0.05), (post_x, 0.0, rim)], 0.022, 'carbon', sides=sides)


# ------------------------------------------------------------------ the safety cars
# Original front-engined road cars in each era's style (not copies of any real model), low-poly like the
# F1 cars: a body loft with its wheel arches cut out, a glass cabin with a painted roof, lights, and the
# roof light bar (amber lamps, green at the ends). Units and axes as the F1 cars.
SC_SPECS = {
    # 1998: a rounded two-door coupe with a separate boot (4.57 x 1.72 x 1.37 m), small wheels, a tall bar
    'SC1998': dict(wb=2.69, of=0.86, ov=1.02, W=1.72, H=1.37, belt=0.86, hood=0.79, deck=0.87, cowl=0.50,
                   roof_f=0.12, roof_r=-0.62, rw_base=-0.92, gw=0.82, e=0.40, R=0.31, rim=0.19, twf=0.215, twr=0.215,
                   lb_w=0.60, lb_h=0.16, bar='box', spoiler='lip', grille=0.55, roof='livery'),
    # 2010: a long-hood GT coupe (4.64 x 1.94 x 1.26 m), the cabin pushed right back, short fastback, wing vents
    'SC2010': dict(wb=2.68, of=0.80, ov=1.16, W=1.94, H=1.26, belt=0.82, hood=0.74, deck=0.84, cowl=-0.20,
                   roof_f=-0.58, roof_r=-0.98, rw_base=-1.42, gw=0.70, e=0.38, R=0.34, rim=0.24, twf=0.265, twr=0.295,
                   lb_w=0.42, lb_h=0.07, bar='slim', spoiler=None, grille=0.62, wing_vents=True, roof='livery'),
    # 2016: a fastback GT coupe (4.55 x 1.94 x 1.29 m): sloping roof to the tail, ducktail spoiler
    'SC2016': dict(wb=2.63, of=0.80, ov=1.12, W=1.94, H=1.29, belt=0.80, hood=0.72, deck=0.80, cowl=0.20,
                   roof_f=-0.18, roof_r=-0.45, rw_base=-1.70, gw=0.72, e=0.32, R=0.345, rim=0.25, twf=0.265, twr=0.295,
                   lb_w=0.44, lb_h=0.05, bar='slim', spoiler='duck', grille=0.66, roof='livery'),
    # 2025: a wide front-engined GT coupe (4.50 x 1.98 x 1.27 m): flared arches, canards, splitter, big wing, black roof
    'SC2025': dict(wb=2.70, of=0.80, ov=1.00, W=1.98, H=1.27, belt=0.79, hood=0.68, deck=0.82, cowl=0.15,
                   roof_f=-0.25, roof_r=-0.62, rw_base=-1.20, gw=0.66, e=0.30, R=0.35, rim=0.265, twf=0.285, twr=0.315,
                   lb_w=0.40, lb_h=0.035, bar='slim', spoiler='wing', splitter=True, vents=True, canards=True,
                   flare=0.10, grille=0.72, roof='second', cover=True),
}


def cylinder_y(B, name, ax, y0, y1, z, r, seg, mat):
    """a closed cylinder along Y (a wheel-arch cutter)"""
    V = []
    for y in (y0, y1):
        for k in range(seg):
            a = 2 * math.pi * k / seg
            V.append((ax + r * math.cos(a), y, z + r * math.sin(a)))
    F = [(k, (k + 1) % seg, seg + (k + 1) % seg, seg + k) for k in range(seg)]
    F += [tuple(range(seg))[::-1], tuple(range(seg, 2 * seg))]
    return B.mesh(name, V, F, mat)


def road_car(B, s):
    lod = B.lod; n = {0: 8, 1: 6, 2: 4}[lod]
    hw = s['W'] / 2 - 0.01
    xf, xr = s['wb'] / 2, -s['wb'] / 2
    xn, xt = xf + s['of'], xr - s['ov']
    belt, hood, deck, H = s['belt'], s['hood'], s['deck'], s['H']
    pinch = 1.0 - s.get('flare', 0.0)          # wide-body: the waist between the arches is narrower
    rw = s['rw_base']
    secs = [(xn, hw * 0.80, 0.30, hood - 0.12), (xn - 0.18, hw * 0.96, 0.20, hood - 0.03),
            (xf, hw, 0.17, hood), (s['cowl'], hw * pinch, 0.17, belt), (rw, hw * pinch, 0.17, belt if rw > xr else deck + 0.06),
            (xr, hw, 0.17, deck + 0.02 if rw > xr else deck + 0.08), (xt + 0.16, hw * 0.97, 0.20, deck), (xt, hw * 0.86, 0.30, deck - 0.06)]
    if s.get('flare'):                           # the flares: full width over the axles only
        secs += [(xf - 0.45, hw * pinch, 0.17, hood + 0.01), (xr + 0.45, hw * pinch, 0.17, belt)]
    secs = sorted(secs, key=lambda q: -q[0])
    body = B.loft('Body', secs, 'livery', n=n, e=s['e'])
    if lod < 2:                                  # wheel arches
        for ax in (xf, xr):
            for side in (1, -1):
                cut = cylinder_y(B, 'Arch', ax, side * (hw - 0.34), side * (hw + 0.10), s['R'], s['R'] + 0.035,
                                 14 if lod == 0 else 8, ('carbon', 30))
                apply_cut(body, cut)
    # the cabin: glass, with a painted roof
    gw = hw * s['gw']
    B.loft('Cabin', [(s['cowl'], gw * 1.05, belt - 0.03, belt + 0.01), (s['roof_f'], gw, belt - 0.03, H - 0.04),
                     (s['roof_r'], gw, belt - 0.03, H - 0.04), (s['rw_base'], gw * 1.02, belt - 0.03, belt + 0.01)],
           ('glass', 70), n=n, e=0.5)
    B.box('Roof', (s['roof_r'], -gw * 0.92, H - 0.05), (s['roof_f'], gw * 0.92, H), s.get('roof', 'livery'))
    # the light bar on the roof: amber lamps, green at the ends
    lx = (s['roof_f'] + s['roof_r']) / 2 + 0.05
    lw, lh = s['lb_w'], s['lb_h']
    if s.get('bar') == 'box':                   # 1990s: a tall bar on feet, big lamps
        B.box('LightBar', (lx - 0.14, -lw, H + 0.04), (lx + 0.14, lw, H + lh * 0.45), 'second')
        for side in (1, -1):
            B.box('BarFoot', (lx - 0.10, side * lw * 0.7 - 0.04, H - 0.005), (lx + 0.10, side * lw * 0.7 + 0.04, H + 0.045), ('carbon', 60))
    else:
        B.box('LightBar', (lx - 0.11, -lw, H), (lx + 0.11, lw, H + lh * 0.45), 'second' if s.get('roof') != 'second' else ('carbon', 60))
    if lod < 2:
        for side in (1, -1):
            B.box('LampAmber', (lx - 0.09, min(side * 0.04, side * (lw - 0.16)), H + lh * 0.40),
                  (lx + 0.09, max(side * 0.04, side * (lw - 0.16)), H + lh), 'orange')
            B.box('LampGreen', (lx - 0.09, min(side * (lw - 0.14), side * lw), H + lh * 0.40),
                  (lx + 0.09, max(side * (lw - 0.14), side * lw), H + lh), 'green')
    else:
        B.box('Lamps', (lx - 0.09, -lw, H + lh * 0.4), (lx + 0.09, lw, H + lh), 'orange')
    if lod < 2:
        # grille, headlights, tail lights, mirrors
        g = s.get('grille', 0.45)
        B.box('Grille', (xn - 0.03, -hw * g, 0.30), (xn + 0.005, hw * g, hood - 0.16), ('carbon', 40))
        for side, sn in ((1, 'L'), (-1, 'R')):
            ys = sorted((side * hw * 0.50, side * hw * 0.82))
            B.box('Headlight' + sn, (xn - 0.16, ys[0], hood - 0.17), (xn - 0.02, ys[1], hood - 0.09), 'white')
            B.box('TailLight' + sn, (xt - 0.005, ys[0], deck - 0.14), (xt + 0.04, ys[1], deck - 0.07), 'red')
            B.box('Mirror' + sn, (s['cowl'] - 0.12, side * (hw * pinch + 0.10) - 0.06, belt + 0.04),
                  (s['cowl'] - 0.02, side * (hw * pinch + 0.10) + 0.06, belt + 0.11), 'second')
            B.rod('MirrorArm' + sn, (s['cowl'] - 0.07, side * hw * pinch * 0.55, belt - 0.12),
                  (s['cowl'] - 0.07, side * (hw * pinch + 0.06), belt + 0.07), 0.018, 'second')
            if s.get('wing_vents'):              # 2010: the vents in the front wings
                for k in range(3):
                    B.box('WingVent' + sn, (xf - 0.62 + k * 0.10, side * (hw + 0.002) - 0.004, belt - 0.30),
                          (xf - 0.56 + k * 0.10, side * (hw + 0.002) + 0.004, belt - 0.12), ('carbon', 40))
            if s.get('canards'):                 # 2025: dive planes on the front corners
                B.prism('Canard' + sn, [(xn - 0.05, 0.36), (xn - 0.30, 0.40), (xn - 0.30, 0.38), (xn - 0.08, 0.34)],
                        0.14, side * (hw * 0.86), 'second')
        B.box('Plate', (xt - 0.004, -0.16, 0.36), (xt + 0.01, 0.16, 0.47), 'white')
    # era details
    if s['spoiler'] == 'duck':                   # 2016: the roof ends in a ducktail
        B.prism('Spoiler', [(xt + 0.32, deck + 0.02), (xt + 0.02, deck + 0.10), (xt - 0.01, deck + 0.07), (xt + 0.02, deck + 0.01)],
                hw * 1.7, 0, 'second')
    elif s['spoiler'] == 'lip':
        B.prism('Spoiler', [(xt + 0.30, deck + 0.005), (xt + 0.05, deck + 0.05), (xt + 0.03, deck + 0.005)], hw * 1.6, 0, 'livery')
    elif s['spoiler'] == 'wing':
        B.prism('RearWing', [(xt + 0.40, deck + 0.24), (xt + 0.25, deck + 0.29), (xt + 0.08, deck + 0.30), (xt + 0.03, deck + 0.26)],
                hw * 1.75, 0, 'second')
        for side in (1, -1):
            B.prism('WingEnd', [(xt + 0.42, deck + 0.18), (xt + 0.42, deck + 0.33), (xt + 0.0, deck + 0.33), (xt + 0.0, deck + 0.18)],
                    0.012, side * hw * 0.88, 'second')
        for side in (1, -1):
            B.prism('WingPylon', [(xt + 0.30, deck), (xt + 0.24, deck + 0.25), (xt + 0.14, deck + 0.25), (xt + 0.16, deck)],
                    0.012, side * hw * 0.55, ('carbon', 100))
    if s.get('splitter') and lod < 2:
        B.box('Splitter', (xn - 0.20, -hw * 0.92, 0.10), (xn + 0.06, hw * 0.92, 0.125), 'second')
        B.prism('Diffuser', [(xt + 0.40, 0.17), (xt - 0.04, 0.17), (xt - 0.04, 0.30)], hw * 1.3, 0, 'second')
    if s.get('vents') and lod < 2:
        for side in (1, -1):
            ys = sorted((side * 0.18, side * 0.42))
            B.box('HoodVent', (xf - 0.30, ys[0], hood - 0.005), (xf + 0.05, ys[1], hood + 0.012), 'second')


def sc_spec(key):
    s = SC_SPECS[key]
    return dict(xf=s['wb'] / 2, xr=-s['wb'] / 2, R=s['R'], yf=s['W'] / 2 - 0.02 - s['twf'] / 2,
                yr=s['W'] / 2 - 0.02 - s['twr'] / 2, wf=s['twf'], wr=s['twr'], rim=s['rim'], half=s['W'] / 2)


SC_ROW_X = -7.0        # the safety cars park behind their era's F1 car


def build_safety_cars(years=('1998', '2010', '2016', '2025')):
    scene = bpy.context.scene
    offs = offsets(); offs['2010'] = 0.0
    out = {}
    for year in years:
        key = 'SC' + year
        remove_car(key)
        for me in [m for m in bpy.data.meshes if m.users == 0]:
            bpy.data.meshes.remove(me)
        spec = sc_spec(key)
        carcol = collection('FL_%s_Car' % key, scene.collection)
        root = bpy.data.objects.new('FL_%s' % key, None)
        root.empty_display_type = 'ARROWS'; root.empty_display_size = 0.5
        carcol.objects.link(root)
        for lod in (0, 1, 2):
            road_car(Builder(key, lod, collection('FL_%s_LOD%d' % (key, lod), carcol), root), SC_SPECS[key])
        P = Builder(key, 9, collection('FL_%s_Preview' % key, carcol), root)
        for ax, y, w in ((spec['xf'], spec['yf'], spec['wf']), (spec['xr'], spec['yr'], spec['wr'])):
            for sd, sn in ((1, 'L'), (-1, 'R')):
                P.tyre('Tyre' + sn, ax, sd * y, spec['R'], w, spec['rim'], cover=SC_SPECS[key].get('cover', False))
        root.location = (SC_ROW_X, offs[year], 0)
        lc = bpy.context.view_layer.layer_collection.children['FL_%s_Car' % key]
        for name in ('FL_%s_LOD1' % key, 'FL_%s_LOD2' % key):
            lc.children[name].hide_viewport = True
        out[key] = stats(key)
    return out


CARS = {'1998': (car_1998, CAR_1998), '2016': (car_2016, CAR_2016), '2025': (car_2025, CAR_2025)}
HALF_2010 = 0.90          # the 2010 car's half width (1.80 m, 2010 rules 3.3)


def offsets():
    """side by side, SPACING apart wheel to wheel: 1998, the 2010 car at y = 0, 2016, 2025"""
    h98, h16, h25 = CAR_1998['half'], CAR_2016['half'], CAR_2025['half']
    y16 = HALF_2010 + SPACING + h16
    return {'1998': -(HALF_2010 + SPACING + h98), '2016': y16, '2025': y16 + h16 + SPACING + h25}


def remove_car(year):
    old = bpy.data.collections.get('FL_%s_Car' % year)
    if old:
        for c in [old] + list(old.children_recursive):
            for ob in list(c.objects):
                bpy.data.objects.remove(ob, do_unlink=True)
        for c in list(old.children_recursive):
            bpy.data.collections.remove(c)
        bpy.data.collections.remove(old)


def build(years=('1998', '2016', '2025')):
    scene = bpy.context.scene
    out = {}
    offs = offsets()
    for year in years:
        remove_car(year)
        for me in [m for m in bpy.data.meshes if m.users == 0]:
            bpy.data.meshes.remove(me)
        fn, spec = CARS[year]
        carcol = collection('FL_%s_Car' % year, scene.collection)
        root = bpy.data.objects.new('FL_%s' % year, None)
        root.empty_display_type = 'ARROWS'; root.empty_display_size = 0.5
        carcol.objects.link(root)
        for lod in (0, 1, 2):
            fn(Builder(year, lod, collection('FL_%s_LOD%d' % (year, lod), carcol), root))
        P = Builder(year, 9, collection('FL_%s_Preview' % year, carcol), root)
        for ax, y, w in ((spec['xf'], spec['yf'], spec['wf']), (spec['xr'], spec['yr'], spec['wr'])):
            for s, sn in ((1, 'L'), (-1, 'R')):
                P.tyre('Tyre' + sn, ax, s * y, spec['R'], w, spec['rim'], grooves=4 if year == '1998' else 0,
                       cover=spec.get('cover', False))
        root.location = (0, offs[year], 0)
        lc = bpy.context.view_layer.layer_collection.children['FL_%s_Car' % year]
        for name in ('FL_%s_LOD1' % year, 'FL_%s_LOD2' % year):
            lc.children[name].hide_viewport = True
        out[year] = stats(year)
    return out


def lod_objects(year, lod):
    c = bpy.data.collections.get('FL_%s_LOD%d' % (year, lod))
    return [o for o in c.all_objects if o.type == 'MESH'] if c else []


def stats(year):
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    out = []
    for lod in (0, 1, 2):
        n = 0
        for ob in lod_objects(year, lod):
            if any(k in ob.name for k in GAME_DRAWS):
                continue
            ev = ob.evaluated_get(dg); me = ev.to_mesh()
            n += sum(len(p.vertices) - 2 for p in me.polygons)
            ev.to_mesh_clear()
        out.append(n)
    return out


# the rear wing's top element: the game draws it itself (its DRS flap model) so it can open
GAME_DRAWS = ('RWFlap', 'RWTop')


def export_json(year, path):
    """one car's three LODs, relative to its empty, in the same format as the 2010 car (without the
    parts the game draws itself: wheels, the rear wing's top flap)"""
    mats = []; lods = []
    dg = bpy.context.evaluated_depsgraph_get()
    root = bpy.data.objects.get('FL_%s' % year)
    to_car = root.matrix_world.inverted() if root else None
    for lod in (0, 1, 2):
        P, N, M, G = [], [], [], []
        for ob in lod_objects(year, lod):
            if any(k in ob.name for k in GAME_DRAWS):
                continue
            ev = ob.evaluated_get(dg); me = ev.to_mesh()
            bm = bmesh.new(); bm.from_mesh(me); ev.to_mesh_clear()
            bm.transform((to_car @ ob.matrix_world) if to_car else ob.matrix_world)
            bmesh.ops.triangulate(bm, faces=bm.faces[:]); bm.normal_update()
            slots = [s.material.name if s.material else 'FL_carbon' for s in ob.material_slots] or ['FL_carbon']
            for f in bm.faces:
                if f.calc_area() < 1e-9:
                    continue
                name = slots[min(f.material_index, len(slots) - 1)].split('.')[0]
                key = name[3:] if name.startswith('FL_') else 'carbon'
                if key not in mats: mats.append(key)
                for lp in f.loops:
                    P += [round(c, 4) for c in lp.vert.co]
                    N += [round(c, 3) for c in f.normal]
                M.append(mats.index(key)); G.append(0)
            bm.free()
        lods.append(dict(tris=len(M), p=P, n=N, m=M, g=G))
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, 'w') as f:
        json.dump(dict(format='FL-blender-1', car=year, axes='+X forward, +Y left, +Z up, metres',
                       materials=mats, lods=lods), f, separators=(',', ':'))
    return {'path': path, 'triangles': [l['tris'] for l in lods]}


if __name__ == '__main__':
    print('built', build())
