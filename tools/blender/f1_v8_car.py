"""Formula Legends -- the 2010 (V8 era) car, made in Blender (the first-generation low-poly car).

Run it in Blender (Scripting tab: open this file, then Run Script / Alt+P). It builds:

  FL_V8_Car / FL_V8_LOD0   the near model (the original first-generation car)
              FL_V8_LOD1   the middle model (rounder parts with fewer sides, no suspension rods)
              FL_V8_LOD2   the far model
              FL_V8_Preview  wheels and the DRS flap, only to see the whole car: the game draws
                             these itself, so they are not exported

and exports the three LODs for tools/gen_f1.py, which paints every team's livery onto them:

    blender -b -P tools/blender/f1_v8_car.py -- --export tools/blender/f1_v8_car.json

Edited the model by hand in Blender? Re-export without rebuilding it:

    blender -b your.blend -P tools/blender/f1_v8_car.py -- --export-only --export tools/blender/f1_v8_car.json
    (or, in Blender's Python console: export_json("C:/path/to/f1_v8_car.json"))

Conventions (the game's): +X forward, +Y left, +Z up, metres, origin on the ground under the middle
of the car. No driver: the cockpit is hollow for the game's steering wheel and its display.
Materials are the game's texture swatches, FL_<swatch>[_<shade %>]:
  FL_livery  the team livery (the blue body), projected from the side / top like the other cars
  FL_second  the team's second colour (the orange parts)
  FL_paint   the team's main colour;  FL_carbon, FL_white, FL_glass ...  fixed colours
Objects whose names start with GLASS_ are not drawn from the driver's own seat (none on this car).

The numbers the game shares with this car (keep them in step): tools/gen_f1.py E['v8'] (axles,
wheel track, rear wing / DRS flap) and src/render.cpp (COCKPIT[1], FLAP_PIVOT[1]).
"""
import bpy, bmesh, math, json, os, sys
from mathutils import Vector

AXLE_F, AXLE_R, R = 1.60, -1.60, 0.33
WHEEL_YF, WHEEL_YR, TYRE_WF, TYRE_WR = 0.725, 0.71, 0.274, 0.38    # 1.80 m over the wheels (2010 rules 3.3, 12.4)
FLAP = (-2.015, 0.87, 0.1815)        # the game's DRS flap: hinge x, z and chord
FLOOR_TOP = 0.10                     # the floor's top: the sidepods and engine cover sit on it
RW_HALF = 0.375                      # rear wing half span: 0.75 m above 200 mm behind the axle (2010 rules 3.5.2)
OPEN = (0.78, 0.08, 0.22)            # the hollow cockpit: front x, back x, half width (room for the
                                     # steering wheel and its display; no driver)

SWATCH = dict(livery=(0.02, 0.08, 0.40), second=(1.0, 0.33, 0.02), paint=(0.02, 0.08, 0.40),
              carbon=(0.025, 0.025, 0.03), white=(0.95, 0.85, 0.10), glass=(0.10, 0.12, 0.16),
              rubber=(0.03, 0.03, 0.03), grey=(0.55, 0.55, 0.58))


def material(sw, shade=100):
    name = 'FL_%s' % sw if shade == 100 else 'FL_%s_%d' % (sw, shade)
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    c = SWATCH.get(sw, (0.5, 0.5, 0.5)); k = min(shade / 100.0, 1.0)
    m.diffuse_color = (c[0] * k, c[1] * k, c[2] * k, 1.0)      # (blue / orange in Blender: the game repaints)
    if True:
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
    def __init__(self, lod, col):
        self.lod = lod; self.col = col

    def mesh(self, name, V, F, mat, glass=False):
        full = ('GLASS_' if glass else '') + name + '.L%d' % self.lod
        me = bpy.data.meshes.new(full)
        me.from_pydata([tuple(v) for v in V], [], [tuple(f) for f in F])
        me.validate()
        bm = bmesh.new(); bm.from_mesh(me)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        bm.to_mesh(me); bm.free()
        me.materials.append(material(*mat) if isinstance(mat, tuple) else material(mat))
        ob = bpy.data.objects.new(full, me)
        self.col.objects.link(ob)
        return ob

    def loft(self, name, secs, mat, yc=0.0, n=8, e=0.6):
        """rounded octagon-ish sections (x, half width, bottom, top), flat shaded"""
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
        n = len(pts)
        v = [(x, yc - depth / 2, z) for (x, z) in pts] + [(x, yc + depth / 2, z) for (x, z) in pts]
        f = [tuple(range(n)), tuple(range(n, 2 * n))]
        for i in range(n):
            j = (i + 1) % n
            f.append((i, j, n + j, n + i))
        return self.mesh(name, v, f, mat)

    def box(self, name, loc, size, mat):
        x, y, z = loc
        sx, sy, sz = size[0] / 2, size[1] / 2, size[2] / 2
        v = [(x + a * sx, y + b * sy, z + c * sz) for a in (-1, 1) for b in (-1, 1) for c in (-1, 1)]
        f = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
        return self.mesh(name, v, f, mat)

    def rod(self, name, p1, p2, r, mat):
        p1 = Vector(p1); p2 = Vector(p2)
        q = (p2 - p1).to_track_quat('Z', 'Y')
        v = []
        for p in (p1, p2):
            for (a, b) in ((r, 0), (0, r), (-r, 0), (0, -r)):
                v.append(tuple(p + q @ Vector((a, b, 0))))
        f = [(0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
        return self.mesh(name, v, f, mat)

    def shell(self, name, rings, mat):
        """a closed loft through rings of points (the cockpit cutter)"""
        n = len(rings[0])
        V = [p for r in rings for p in r]
        F = [(i * n + k, i * n + (k + 1) % n, (i + 1) * n + (k + 1) % n, (i + 1) * n + k) for i in range(len(rings) - 1) for k in range(n)]
        F += [tuple(range(n))[::-1], tuple(range((len(rings) - 1) * n, len(rings) * n))]
        return self.mesh(name, V, F, mat)

    def sphere(self, name, c, r, seg, rings, mat, glass=False):
        v = [(c[0], c[1], c[2] + r)]
        for i in range(1, rings):
            ph = math.pi * i / rings
            for j in range(seg):
                th = 2 * math.pi * j / seg
                v.append((c[0] + r * math.sin(ph) * math.cos(th), c[1] + r * math.sin(ph) * math.sin(th), c[2] + r * math.cos(ph)))
        v.append((c[0], c[1], c[2] - r))
        f = []
        for j in range(seg):
            f.append((0, 1 + j, 1 + (j + 1) % seg))
        for i in range(rings - 2):
            for j in range(seg):
                a = 1 + i * seg + j; b = 1 + i * seg + (j + 1) % seg
                f.append((a, a + seg, b + seg, b))
        last = len(v) - 1; base = 1 + (rings - 2) * seg
        for j in range(seg):
            f.append((base + j, last, base + (j + 1) % seg))
        return self.mesh(name, v, f, mat, glass=glass)


def opening(n, z):
    """the cockpit opening's outline (a rounded rectangle) at height z"""
    x0, x1, hw = OPEN
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


# ------------------------------------------------------------------ the first-generation car
def build_lod(B):
    lod = B.lod
    n = {0: 8, 1: 6, 2: 4}[lod]          # sides of the rounded parts
    BODY, ACC, CARB = 'livery', 'second', 'carbon'
    # monocoque + raised nose
    tub = B.loft('Tub', [(2.55, 0.06, 0.30, 0.38), (2.3, 0.11, 0.32, 0.47), (1.9, 0.16, 0.35, 0.57),
                   (1.5, 0.20, 0.33, 0.63), (1.1, 0.24, 0.20, 0.67), (0.6, 0.30, 0.10, 0.69),
                   (0.0, 0.32, 0.08, 0.66), (-0.35, 0.30, 0.08, 0.62)], BODY, n=n)
    # the hollow cockpit for the steering wheel and its display (no driver); far away a dark patch
    if lod < 2:
        k = 16 if lod == 0 else 10
        apply_cut(tub, B.shell('Cutter', [opening(k, 0.26), opening(k, 1.30)], ('carbon', 45)))
    else:
        rim = opening(8, 0.70)
        cen = ((OPEN[0] + OPEN[1]) / 2, 0.0, 0.68)
        B.mesh('CockpitDark', [cen] + rim, [(0, j + 1, (j + 1) % 8 + 1) for j in range(8)], ('carbon', 35))
    # engine cover / airbox
    B.loft('EngineCover', [(0.05, 0.12, 0.55, 0.92), (-0.2, 0.15, 0.50, 0.95), (-0.6, 0.19, FLOOR_TOP, 0.85),
                           (-1.1, 0.19, FLOOR_TOP, 0.70), (-1.6, 0.13, FLOOR_TOP, 0.54), (-2.0, 0.07, FLOOR_TOP + 0.02, 0.40)], BODY, n=n)
    B.box('AirboxInlet', (0.06, 0, 0.82), (0.04, 0.16, 0.14), CARB)
    B.prism('SharkFin', [(-0.2, 0.94), (-1.95, 0.72), (-1.95, 0.42), (-0.7, 0.62)], 0.012, 0, ACC)
    for s, sn in ((-1, 'R'), (1, 'L')):
        B.loft('Sidepod' + sn, [(0.55, 0.15, 0.12, 0.50), (0.2, 0.19, 0.10, 0.55), (-0.4, 0.17, 0.10, 0.50),
                                (-1.0, 0.11, 0.10, 0.38), (-1.5, 0.06, 0.10, 0.27)], BODY, yc=s * 0.43, n=n)
        if lod < 2:
            B.box('SidepodInlet' + sn, (0.56, s * 0.43, 0.31), (0.02, 0.24, 0.30), CARB)
        B.prism('FW_Flap' + sn, [(2.32, 0.13), (2.16, 0.25), (2.12, 0.25), (2.28, 0.12)], 0.62, s * 0.56, ACC)
        B.prism('FW_End' + sn, [(2.60, 0.075), (2.60, 0.20), (2.37, 0.275), (2.07, 0.27), (2.07, 0.075)], 0.015, s * 0.8825, BODY)
        if lod < 2:
            B.prism('NosePylon' + sn, [(2.44, 0.10), (2.40, 0.34), (2.22, 0.36), (2.22, 0.10)], 0.012, s * 0.06, CARB)
        B.prism('RW_End' + sn, [(-1.83, 0.30), (-1.83, 0.86), (-1.92, 0.95), (-2.20, 0.95), (-2.20, 0.30)], 0.015, s * (RW_HALF - 0.008), BODY)
        if lod < 2:
            B.box('Mirror' + sn, (0.72, s * 0.36, 0.73), (0.04, 0.12, 0.05), BODY)
            B.rod('MirrorStalk' + sn, (0.78, s * 0.26, 0.64), (0.73, s * 0.33, 0.72), 0.01, CARB)
        if lod == 0:
            r = 0.012
            B.rod('FUpper1' + sn, (1.75, s * 0.15, 0.50), (1.6, s * 0.60, 0.45), r, CARB)
            B.rod('FUpper2' + sn, (1.45, s * 0.18, 0.52), (1.6, s * 0.60, 0.45), r, CARB)
            B.rod('FLower1' + sn, (1.80, s * 0.12, 0.33), (1.6, s * 0.60, 0.22), r, CARB)
            B.rod('FLower2' + sn, (1.40, s * 0.20, 0.30), (1.6, s * 0.60, 0.22), r, CARB)
            B.rod('RUpper1' + sn, (-1.40, s * 0.22, 0.45), (-1.6, s * 0.54, 0.45), r, CARB)
            B.rod('RUpper2' + sn, (-1.80, s * 0.15, 0.45), (-1.6, s * 0.54, 0.45), r, CARB)
            B.rod('RLower1' + sn, (-1.35, s * 0.30, 0.20), (-1.6, s * 0.54, 0.20), r, CARB)
            B.rod('RLower2' + sn, (-1.85, s * 0.15, 0.20), (-1.6, s * 0.54, 0.20), r, CARB)
    # the bodywork deck (from behind the cockpit): fills the channels between the sidepods, the tub and the engine cover down to
    # the floor, narrowing to the gearbox ("coke bottle"); a little lower than the pods so they still show
    B.loft('Deck', [(0.06, 0.56, FLOOR_TOP, 0.47), (-0.40, 0.54, FLOOR_TOP, 0.44),
                    (-1.00, 0.46, FLOOR_TOP, 0.33), (-1.50, 0.30, FLOOR_TOP, 0.25), (-2.00, 0.10, FLOOR_TOP + 0.02, 0.22)],
           BODY, n=n, e=0.35)
    B.prism('FW_Main', [(2.60, 0.09), (2.50, 0.125), (2.30, 0.115), (2.18, 0.09), (2.37, 0.075)], 1.75, 0, CARB)
    B.prism('RW_Main', [(-1.85, 0.80), (-1.90, 0.83), (-1.98, 0.835), (-2.005, 0.82), (-2.01, 0.80), (-1.95, 0.79)], 2 * RW_HALF - 0.032, 0, BODY)
    # (the top flap of the rear wing is the game's DRS flap, drawn on its own so it can open)
    if lod < 2:
        B.prism('BeamWing', [(-1.86, 0.40), (-1.90, 0.43), (-2.04, 0.42), (-2.08, 0.40)], 2 * RW_HALF - 0.032, 0, CARB)
    B.prism('RW_Pillar', [(-1.70, 0.25), (-1.90, 0.79), (-1.98, 0.79), (-1.85, 0.25)], 0.02, 0, CARB)
    # the floor: its top meets the sidepods, the tub and the engine cover (one piece with the body)
    fz, ft = (0.035 + FLOOR_TOP) / 2, FLOOR_TOP - 0.035
    B.box('Floor', (-0.125, 0, fz), (2.25, 1.36, ft), CARB)
    B.box('FloorRear', (-1.625, 0, fz), (0.75, 1.0, ft), CARB)
    B.prism('Diffuser', [(-1.70, 0.05), (-2.15, 0.05), (-2.15, 0.20), (-1.95, 0.18)], 0.9, 0, CARB)


def build_preview(col):
    """the wheels and the DRS flap as the game places them (not exported)"""
    B = Builder(9, col)
    for ax, wy, w in ((AXLE_F, WHEEL_YF, TYRE_WF), (AXLE_R, WHEEL_YR, TYRE_WR)):
        for s, sn in ((1, 'L'), (-1, 'R')):
            ob = B.loft('PREVIEW_Tyre' + sn, [(-w / 2, R, -R, R), (w / 2, R, -R, R)], ('rubber', 100), n=16, e=1.0)
            # the loft runs along X: turn it so the axle runs along Y
            ob.rotation_euler = (0, 0, math.pi / 2)
            ob.location = (ax, s * wy, R)
    B.prism('PREVIEW_DRSFlap', [(FLAP[0], FLAP[1]), (FLAP[0] - FLAP[2], FLAP[1]), (FLAP[0] - FLAP[2], FLAP[1] + 0.022), (FLAP[0], FLAP[1] + 0.022)],
            2 * RW_HALF - 0.032, 0, 'second')


def build():
    scene = bpy.context.scene
    old = bpy.data.collections.get('FL_V8_Car')
    if old:
        for c in [old] + list(old.children_recursive):
            for ob in list(c.objects):
                bpy.data.objects.remove(ob, do_unlink=True)
        for c in list(old.children_recursive):
            bpy.data.collections.remove(c)
        bpy.data.collections.remove(old)
    for me in [m for m in bpy.data.meshes if m.users == 0]:
        bpy.data.meshes.remove(me)
    root = collection('FL_V8_Car', scene.collection)
    for lod in (0, 1, 2):
        build_lod(Builder(lod, collection('FL_V8_LOD%d' % lod, root)))
    build_preview(collection('FL_V8_Preview', root))
    lc = bpy.context.view_layer.layer_collection.children['FL_V8_Car']
    for name in ('FL_V8_LOD1', 'FL_V8_LOD2'):
        lc.children[name].hide_viewport = True
    return stats()


# ------------------------------------------------------------------ export for tools/gen_f1.py
def lod_objects(lod):
    c = bpy.data.collections.get('FL_V8_LOD%d' % lod)
    return [o for o in c.all_objects if o.type == 'MESH'] if c else []


def stats():
    out = {}
    dg = bpy.context.evaluated_depsgraph_get()
    for lod in (0, 1, 2):
        n = 0
        for ob in lod_objects(lod):
            me = ob.evaluated_get(dg).to_mesh()
            n += sum(len(p.vertices) - 2 for p in me.polygons)
            ob.evaluated_get(dg).to_mesh_clear()
        out['LOD%d' % lod] = n
    return out


def export_json(path, smooth_angle=40.0):
    """the three LODs as triangles: positions, corner normals (smoothed within smooth_angle on
    smooth faces), material (swatch name) and the glass flag"""
    cos_lim = math.cos(math.radians(smooth_angle))
    mats = []
    lods = []
    dg = bpy.context.evaluated_depsgraph_get()
    for lod in (0, 1, 2):
        P, N, M, G = [], [], [], []
        for ob in lod_objects(lod):
            ev = ob.evaluated_get(dg)
            me = ev.to_mesh()
            bm = bmesh.new(); bm.from_mesh(me)
            ev.to_mesh_clear()
            bm.transform(ob.matrix_world)
            bmesh.ops.triangulate(bm, faces=bm.faces[:])
            bm.normal_update()
            slots = [s.material.name if s.material else 'FL_carbon' for s in ob.material_slots] or ['FL_carbon']
            glass = ob.name.startswith('GLASS_')
            for f in bm.faces:
                if f.calc_area() < 1e-9:
                    continue
                name = slots[min(f.material_index, len(slots) - 1)].split('.')[0]
                key = name[3:] if name.startswith('FL_') else 'carbon'
                if key not in mats:
                    mats.append(key)
                fn = f.normal
                for lp in f.loops:
                    v = lp.vert
                    if f.smooth:
                        acc = Vector((0, 0, 0))
                        for g in v.link_faces:
                            if g.smooth and g.normal.dot(fn) >= cos_lim:
                                acc += g.normal * g.calc_area()
                        n = acc.normalized() if acc.length > 1e-12 else fn
                    else:
                        n = fn
                    P += [round(c, 4) for c in v.co]
                    N += [round(c, 3) for c in n]
                M.append(mats.index(key)); G.append(1 if glass else 0)
            bm.free()
        lods.append(dict(tris=len(M), p=P, n=N, m=M, g=G))
    data = dict(format='FL-blender-1', car='2010 V8 (first generation)', axes='+X forward, +Y left, +Z up, metres',
                materials=mats, lods=lods)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, 'w') as f:
        json.dump(data, f, separators=(',', ':'))
    return {'path': path, 'triangles': [l['tris'] for l in lods], 'materials': mats}


if __name__ == '__main__':
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    if '--export-only' not in argv:
        print('built', build())
    if '--export' in argv:
        print('exported', export_json(argv[argv.index('--export') + 1]))
