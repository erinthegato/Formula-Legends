"""Formula Legends -- trackside objects, built in Blender, low-poly and low-memory for the PS Vita.

Run it in Blender in the same scene as the cars (Scripting tab, Run Script). It builds, behind the cars:

  FL_PROP_<name> / LOD0, LOD1    near and far model of each object, held by an empty FL_PROP_<name>

  gantry      the start-light gantry over the grid (20 m span; the game stretches it to the track)
  bleacher    a small corner spectator stand with its crowd and a canopy
  tent        a white spectator marquee
  flagpanel   a marshal light panel on a post
  crane       a recovery crane truck, parked at a run-off
  tecpro      a 6 m row of energy-absorbing barrier blocks

Every object is coloured only by the game's swatch colours (materials FL_<swatch>[_<shade %>].PROP), so
tools/gen_trackside.py gives them all one tiny 64x64 texture: a few KB each instead of 300 KB.
Facing: +X points at the track (the gantry spans along X); +Z up; metres.

    export_all("C:/path/to/formula-legends/tools/blender/props")   # one <name>.json per object
"""
import bpy, bmesh, math, json, os
from mathutils import Vector

PALETTE = dict(concrete=(0.62, 0.62, 0.60), steel=(0.60, 0.62, 0.65), carbon=(0.06, 0.06, 0.07),
               red=(0.90, 0.06, 0.05), white=(0.95, 0.95, 0.93), yellow=(1.0, 0.80, 0.05),
               blue=(0.10, 0.28, 0.70), rubber=(0.04, 0.04, 0.045), orange=(1.0, 0.50, 0.05),
               green=(0.15, 0.60, 0.25), glass=(0.25, 0.35, 0.45), grey=(0.40, 0.41, 0.43),
               crowd1=(0.85, 0.20, 0.15), crowd2=(0.15, 0.35, 0.75), crowd3=(0.95, 0.85, 0.20),
               crowd4=(0.20, 0.60, 0.25), skin=(0.80, 0.62, 0.50),
               crowd=(0.55, 0.40, 0.38))
ROW_X = -16.0          # the objects stand in a row behind the safety cars


def material(sw, shade=100):
    name = ('FL_%s' % sw if shade == 100 else 'FL_%s_%d' % (sw, shade)) + '.PROP'
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    c = PALETTE.get(sw, (0.5, 0.5, 0.5)); k = min(shade / 100.0, 1.0)
    m.diffuse_color = (c[0] * k, c[1] * k, c[2] * k, 1.0)
    return m


def collection(name, parent):
    c = bpy.data.collections.get(name) or bpy.data.collections.new(name)
    if c.name not in parent.children:
        parent.children.link(c)
    return c


class Builder:
    def __init__(self, name, lod, col, root):
        self.name, self.lod, self.col, self.root = name, lod, col, root

    def mesh(self, part, V, F, mat):
        full = '%s_%s.L%d' % (self.name, part, self.lod)
        me = bpy.data.meshes.new(full)
        me.from_pydata([tuple(v) for v in V], [], [tuple(f) for f in F])
        me.validate()
        bm = bmesh.new(); bm.from_mesh(me)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        bm.to_mesh(me); bm.free()
        me.materials.append(material(*mat) if isinstance(mat, tuple) else material(mat))
        ob = bpy.data.objects.new(full, me)
        self.col.objects.link(ob); ob.parent = self.root
        return ob

    def box(self, part, lo, hi, mat):
        V = [(x, y, z) for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
        F = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
        return self.mesh(part, V, F, mat)

    def cyl(self, part, a, b, r, sides, mat):
        a, b = Vector(a), Vector(b)
        t = (b - a).normalized()
        ref = Vector((0, 0, 1)) if abs(t.z) < 0.9 else Vector((1, 0, 0))
        u = t.cross(ref).normalized(); w = t.cross(u)
        V = [tuple(p + r * (math.cos(2 * math.pi * k / sides) * u + math.sin(2 * math.pi * k / sides) * w))
             for p in (a, b) for k in range(sides)]
        F = [(k, (k + 1) % sides, sides + (k + 1) % sides, sides + k) for k in range(sides)]
        F += [tuple(range(sides))[::-1], tuple(range(sides, 2 * sides))]
        return self.mesh(part, V, F, mat)

    def poly(self, part, V, F, mat):
        return self.mesh(part, V, F, mat)


# ------------------------------------------------------------------ the objects
def gantry(B):
    """start-light gantry: two pillars, a truss beam across the track, the light panel in the middle"""
    for sx in (-10.0, 10.0):
        B.box('Pillar', (sx - 0.25, -0.25, 0), (sx + 0.25, 0.25, 6.4), 'grey')
        if B.lod == 0:
            B.box('Foot', (sx - 0.5, -0.5, 0), (sx + 0.5, 0.5, 0.4), 'concrete')
    B.box('Beam', (-10.0, -0.30, 5.6), (10.0, 0.30, 6.4), 'grey')
    B.box('Panel', (-2.6, -0.22, 4.3), (2.6, 0.22, 5.6), 'carbon')
    if B.lod == 0:
        B.box('BeamBand', (-10.0, -0.31, 5.8), (10.0, 0.31, 6.2), 'white')
        for k in range(5):                    # five pairs of unlit lamps; the game lights them at the start
            x = -2.0 + k * 1.0
            for z in (4.55, 5.05):
                B.box('Lamp', (x - 0.18, -0.26, z), (x + 0.18, 0.26, z + 0.32), ('red', 30))


def bleacher(B):
    """corner bleachers: five stepped rows rising away from the track (+X), the crowd, a canopy"""
    W = 5.0                                   # half width along the track
    rows = 5
    for i in range(rows):
        x0 = -i * 0.9
        B.box('Row', (x0 - 0.9, -W, 0), (x0, W, 0.45 + i * 0.55), 'concrete')
        z = 0.45 + i * 0.55
        # the crowd: one strip a row; the game paints it with a pattern of people (tools/gen_trackside.py)
        B.box('Crowd', (x0 - 0.65, -W + 0.3, z), (x0 - 0.25, W - 0.3, z + 0.55), 'crowd')
    back = -rows * 0.9
    B.box('Back', (back - 0.15, -W, 0), (back, W, 4.4), 'concrete')
    for y in (-W, W):
        B.box('Post', (back - 0.1, y - 0.1, 0), (back + 0.1, y + 0.1, 4.6), 'steel')
        if B.lod == 0:
            B.box('PostFront', (-0.15, y - 0.08, 0), (0.0, y + 0.08, 4.1), 'steel')
    # the canopy: sloping roof from the back down to the front
    V = [(back - 0.3, -W - 0.3, 4.6), (back - 0.3, W + 0.3, 4.6), (0.4, W + 0.3, 4.0), (0.4, -W - 0.3, 4.0)]
    V += [(v[0], v[1], v[2] + 0.12) for v in V]
    B.poly('Canopy', V, [(0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)], 'white')
    if B.lod == 0:
        B.box('Rail', (0.0, -W, 0.9), (0.06, W, 1.0), 'steel')


def tent(B):
    """a 4 x 4 m white marquee: four poles, a peaked roof, a red valance"""
    h, s = 2.4, 2.0
    for x in (-s, s):
        for y in (-s, s):
            B.box('Pole', (x - 0.04, y - 0.04, 0), (x + 0.04, y + 0.04, h), 'steel')
    V = [(-s - 0.1, -s - 0.1, h), (s + 0.1, -s - 0.1, h), (s + 0.1, s + 0.1, h), (-s - 0.1, s + 0.1, h), (0, 0, h + 1.3)]
    B.poly('Roof', V, [(0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4), (3, 2, 1, 0)], 'white')
    if B.lod == 0:
        for (a, b) in (((-s, -s), (s, -s)), ((s, -s), (s, s)), ((s, s), (-s, s)), ((-s, s), (-s, -s))):
            x0, y0 = min(a[0], b[0]) - 0.1, min(a[1], b[1]) - 0.1
            x1, y1 = max(a[0], b[0]) + 0.1, max(a[1], b[1]) + 0.1
            if x1 - x0 < 0.3: x1 = x0 + 0.02
            if y1 - y0 < 0.3: y1 = y0 + 0.02
            B.box('Valance', (x0, y0, h - 0.3), (x1, y1, h), 'red')
        B.box('Table', (-1.2, -0.4, 0.75), (1.2, 0.4, 0.8), 'white')


def flagpanel(B):
    """a marshal light panel on a post, facing the track (+X)"""
    B.box('Post', (-0.06, -0.06, 0), (0.06, 0.06, 2.6), 'steel')
    B.box('Panel', (0.06, -0.40, 2.1), (0.20, 0.40, 2.7), 'carbon')
    B.box('Light', (0.20, -0.32, 2.18), (0.22, 0.32, 2.62), 'yellow')
    if B.lod == 0:
        B.box('Hood', (0.06, -0.44, 2.7), (0.36, 0.44, 2.76), 'carbon')
        B.box('Base', (-0.30, -0.30, 0), (0.30, 0.30, 0.15), 'concrete')


def crane(B):
    """a recovery crane truck: cab, chassis, outriggers, the boom reaching towards the track (+X)"""
    B.box('Chassis', (-3.2, -1.1, 0.5), (2.4, 1.1, 1.1), 'carbon')
    B.box('Cab', (1.0, -1.15, 1.1), (2.4, 1.15, 2.9), 'yellow')
    B.box('Body', (-3.2, -1.15, 1.1), (0.9, 1.15, 1.9), 'yellow')
    B.box('Turret', (-2.2, -0.8, 1.9), (-0.8, 0.8, 2.6), 'orange')
    # the boom from the turret up and forward, and the hook line hanging from its end
    a, b = Vector((-1.5, 0, 2.4)), Vector((2.8, 0, 6.2))
    B.cyl('Boom', a, b, 0.32, 4, 'yellow')
    B.cyl('Line', b, (2.8, 0, 3.0), 0.03, 4, 'carbon')
    if B.lod == 0:
        B.box('Window', (2.41, -0.95, 1.9), (2.43, 0.95, 2.7), 'glass')
        B.box('Hook', (2.65, -0.15, 2.7), (2.95, 0.15, 3.0), 'red')
        for x in (-2.6, 1.6):
            for s in (1, -1):
                B.box('Outrigger', (x - 0.12, min(s * 1.1, s * 2.0), 0.05), (x + 0.12, max(s * 1.1, s * 2.0), 0.35), 'red')
        for x in (-2.4, -1.2, 1.7):
            for s in (1, -1):
                B.cyl('Wheel', (x, s * 0.95, 0.5), (x, s * 1.2, 0.5), 0.5, 10, 'rubber')
    else:
        B.box('Wheels', (-2.9, -1.2, 0.0), (2.2, 1.2, 0.5), 'rubber')


def tecpro(B):
    """a 6 m row of barrier blocks (along Y), blue and red alternating, strapped together"""
    n = 6 if B.lod == 0 else 1
    L = 6.0
    for k in range(n):
        y0 = -L / 2 + k * L / n
        col = ('blue' if k % 2 == 0 else 'red') if n > 1 else 'blue'
        B.box('Block', (-0.4, y0 + 0.02, 0), (0.4, y0 + L / n - 0.02, 1.0), col)
    if B.lod == 0:
        B.box('Strap', (-0.41, -L / 2, 0.70), (0.41, L / 2, 0.78), 'carbon')


PROPS = {'gantry': gantry, 'bleacher': bleacher, 'tent': tent, 'flagpanel': flagpanel, 'crane': crane, 'tecpro': tecpro}
SPOTS = {'gantry': (-8.0, 1.5), 'bleacher': (0.0, -6.0), 'tent': (0.0, 3.0), 'flagpanel': (0.0, 7.0),
         'crane': (0.0, 11.5), 'tecpro': (0.0, 17.0)}


def remove(name):
    old = bpy.data.collections.get('FL_PROP_%s' % name)
    if old:
        for c in [old] + list(old.children_recursive):
            for ob in list(c.objects):
                bpy.data.objects.remove(ob, do_unlink=True)
        for c in list(old.children_recursive):
            bpy.data.collections.remove(c)
        bpy.data.collections.remove(old)


def build():
    scene = bpy.context.scene
    out = {}
    for name, fn in PROPS.items():
        remove(name)
        for me in [m for m in bpy.data.meshes if m.users == 0]:
            bpy.data.meshes.remove(me)
        col = collection('FL_PROP_%s' % name, scene.collection)
        root = bpy.data.objects.new('FL_PROP_%s' % name, None)
        root.empty_display_type = 'ARROWS'; root.empty_display_size = 0.8
        col.objects.link(root)
        for lod in (0, 1):
            fn(Builder(name, lod, collection('FL_PROP_%s_LOD%d' % (name, lod), col), root))
        dx, y = SPOTS[name]
        root.location = (ROW_X + dx, y, 0)
        lc = bpy.context.view_layer.layer_collection.children['FL_PROP_%s' % name]
        lc.children['FL_PROP_%s_LOD1' % name].hide_viewport = True
        out[name] = [tris(name, 0), tris(name, 1)]
    return out


def objects(name, lod):
    c = bpy.data.collections.get('FL_PROP_%s_LOD%d' % (name, lod))
    return [o for o in c.all_objects if o.type == 'MESH'] if c else []


def tris(name, lod):
    return sum(len(p.vertices) - 2 for o in objects(name, lod) for p in o.data.polygons)


def export_json(name, path):
    """one object's two LODs (relative to its empty): triangles, flat normals, swatch names"""
    mats = []; lods = []
    dg = bpy.context.evaluated_depsgraph_get()
    root = bpy.data.objects.get('FL_PROP_%s' % name)
    inv = root.matrix_world.inverted()
    for lod in (0, 1):
        P, N, M = [], [], []
        for ob in objects(name, lod):
            ev = ob.evaluated_get(dg); me = ev.to_mesh()
            bm = bmesh.new(); bm.from_mesh(me); ev.to_mesh_clear()
            bm.transform(inv @ ob.matrix_world)
            bmesh.ops.triangulate(bm, faces=bm.faces[:]); bm.normal_update()
            slots = [s.material.name if s.material else 'FL_grey' for s in ob.material_slots] or ['FL_grey']
            for f in bm.faces:
                if f.calc_area() < 1e-9:
                    continue
                key = slots[min(f.material_index, len(slots) - 1)].split('.')[0][3:]
                if key not in mats: mats.append(key)
                for lp in f.loops:
                    P += [round(c, 4) for c in lp.vert.co]
                    N += [round(c, 3) for c in f.normal]
                M.append(mats.index(key))
            bm.free()
        lods.append(dict(tris=len(M), p=P, n=N, m=M))
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, 'w') as f:
        json.dump(dict(format='FL-prop-1', prop=name, palette={k: PALETTE[k] for k in PALETTE},
                       materials=mats, lods=lods), f, separators=(',', ':'))
    return [l['tris'] for l in lods]


def export_all(folder):
    return {name: export_json(name, os.path.join(folder, name + '.json')) for name in PROPS}


if __name__ == '__main__':
    print('built', build())
