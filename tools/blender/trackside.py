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

The circuit buildings share one 512 px "trackside atlas" between them (tools/gen_trackside.py paints it):
flat swatches plus pictures -- sponsor boards, the timing screen, the crowd, glass -- on decal faces
(materials FL_decal_<picture>, with UVs over the whole picture):

  grandstand    stepped stand along the start straight: a seated crowd on every row, a roof, a banner
  billboard     an 8 x 2 m sponsor board on legs
  ad_bridge     an advertising footbridge on two lattice towers (30 m span; the game stretches it)
  race_control  the race control tower with its timing screen
  hospitality   a two-storey glass team hospitality unit with a roof terrace
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
               crowd=(0.55, 0.40, 0.38), slab=(0.24, 0.25, 0.27), cream=(0.93, 0.88, 0.75),
               black=(0.03, 0.03, 0.035))
# the decal pictures (painted by tools/gen_trackside.py), shown in Blender as their main colour
DECALS = dict(ad1=(0.10, 0.20, 0.55), ad2=(0.10, 0.55, 0.25), ad3=(0.95, 0.80, 0.10), ad4=(0.85, 0.10, 0.10),
              ad5=(0.55, 0.85, 0.10), ad6=(0.05, 0.05, 0.05), ad7=(0.45, 0.15, 0.60), ad8=(0.20, 0.20, 0.24),
              timing=(0.02, 0.02, 0.02), crowd=(0.55, 0.40, 0.38), crowdrows=(0.55, 0.40, 0.38),
              glass=(0.30, 0.45, 0.60))
ROW_X = -16.0          # the objects stand in a row behind the safety cars


def material(sw, shade=100):
    name = ('FL_%s' % sw if shade == 100 else 'FL_%s_%s' % (sw, shade)) + '.PROP'
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    c = PALETTE.get(sw) or DECALS.get(str(shade), (0.5, 0.5, 0.5)); k = min(shade / 100.0, 1.0) if isinstance(shade, int) else 1.0
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

    def mesh(self, part, V, F, mat, UV=None):
        full = '%s_%s.L%d' % (self.name, part, self.lod)
        me = bpy.data.meshes.new(full)
        me.from_pydata([tuple(v) for v in V], [], [tuple(f) for f in F])
        me.validate()
        if UV is None:              # closed shapes: point every face outwards
            bm = bmesh.new(); bm.from_mesh(me)
            bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
            bm.to_mesh(me); bm.free()
        else:                       # decals: wound to face the viewer, with their picture's UVs
            uvl = me.uv_layers.new(name='UV')
            k = 0
            for poly, fuv in zip(me.polygons, UV):
                for li, uv in zip(poly.loop_indices, fuv):
                    uvl.data[li].uv = uv
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

    def prism(self, part, prof, y0, y1, mat):
        """a profile in the X-Z plane (counter-clockwise), extruded from y0 to y1"""
        n = len(prof)
        V = [(x, y0, z) for x, z in prof] + [(x, y1, z) for x, z in prof]
        F = [tuple(range(n))[::-1], tuple(range(n, 2 * n))] + [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
        return self.mesh(part, V, F, mat)

    def rod(self, part, a, b, r, mat):
        return self.cyl(part, a, b, r, 4, mat)

    def decal(self, part, c, pic, u=(0.0, 1.0), v=(0.0, 1.0)):
        """a picture on a quad: corners lower-left, lower-right, upper-right, upper-left as seen from in front"""
        uv = [(u[0], v[0]), (u[1], v[0]), (u[1], v[1]), (u[0], v[1])]
        return self.mesh(part, c, [(0, 1, 2, 3)], ('decal', pic), UV=[uv])

    # pictures on the faces of axis-aligned boxes
    def decal_x(self, part, x, y0, y1, z0, z1, pic, back=False, **kw):     # facing +X (or -X)
        c = [(x, y0, z0), (x, y1, z0), (x, y1, z1), (x, y0, z1)] if not back else [(x, y1, z0), (x, y0, z0), (x, y0, z1), (x, y1, z1)]
        return self.decal(part, c, pic, **kw)

    def decal_y(self, part, y, x0, x1, z0, z1, pic, back=False, **kw):     # facing +Y (or -Y)
        c = [(x1, y, z0), (x0, y, z0), (x0, y, z1), (x1, y, z1)] if not back else [(x0, y, z0), (x1, y, z0), (x1, y, z1), (x0, y, z1)]
        return self.decal(part, c, pic, **kw)


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
    B.box('Light', (0.20, -0.32, 2.18), (0.22, 0.32, 2.62), ('black', 100))   # dark: the game lights it
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


# ------------------------------------------------------------------ the circuit buildings (shared atlas)
def grandstand(B):
    """10 x 31 m, 9.4 m tall; the front (+X) faces the track"""
    W = 15.5
    rows = 8
    top = 1.1 + (rows - 1) * 0.72
    xb = 5.0 - rows * 0.95                     # back of the top row
    if B.lod == 0:
        for i in range(rows):                  # stepped concrete terraces, a seated crowd on each
            xf = 5.0 - i * 0.95; z = 1.1 + i * 0.72
            B.box('Row%d' % i, (xf - 0.95, -W, 0), (xf, W, z), ('concrete', 92 - i * 2))
            for k in range(4):                 # four 7.5 m strips of the crowd picture
                y0 = -W + 0.2 + k * 7.75
                B.decal_x('Crowd%d_%d' % (i, k), xf - 0.35, y0, y0 + 7.5, z, z + 0.62, 'crowd',
                          u=(0.0, 1.0) if (i + k) % 2 else (1.0, 0.0))
        for k, y in enumerate((-14.6, -7.3, 0.0, 7.3, 14.6)):      # roof columns
            B.box('Column%d' % k, (-3.85, y - 0.15, 0), (-3.55, y + 0.15, 9.2), 'steel')
        for k in range(4):                     # sponsor banners along the front wall
            y0 = -W + 0.2 + k * 7.75
            B.decal_x('Banner%d' % k, 5.21, y0, y0 + 7.5, 0.15, 1.05, 'ad%d' % (k + 1))
    else:
        B.prism('Terrace', [(5.0, 0), (5.0, 1.1), (xb + 0.95, top), (xb, top), (xb, 0)], -W, W, 'concrete')
        B.decal('Crowd', [(5.0, -W, 1.15), (5.0, W, 1.15), (xb + 0.6, W, top + 0.5), (xb + 0.6, -W, top + 0.5)], 'crowdrows')
        B.decal_x('Banner', 5.21, -W, W, 0.15, 1.05, 'ad8')
    B.box('FrontWall', (5.0, -W, 0), (5.2, W, 1.4), 'white')
    B.box('BackWall', (-3.9, -W, 0), (xb, W, top + 0.8), ('concrete', 80))
    for y0, y1 in ((-W - 0.2, -W), (W, W + 0.2)):              # side walls
        B.prism('Side', [(5.2, 0), (5.2, 1.4), (xb + 0.95, top + 0.6), (-3.9, top + 0.8), (-3.9, 0)], y0, y1, 'white')
    B.prism('Roof', [(-3.9, 9.15), (5.4, 8.55), (5.4, 8.75), (-3.9, 9.4)], -W - 0.3, W + 0.3, 'white')


def billboard(B):
    """an 8 x 2 m board on two legs; +Y faces the track, a different sponsor on the back"""
    if B.lod == 0:
        for x in (-3.0, 3.0):
            B.box('Leg', (x - 0.07, -0.15, 0), (x + 0.07, -0.01, 1.25), 'steel')
    B.box('Board', (-4.0, -0.14, 1.2), (4.0, -0.02, 3.2), 'grey')
    B.decal_y('Front', -0.015, -3.92, 3.92, 1.26, 3.14, 'ad2')
    B.decal_y('Back', -0.145, -3.92, 3.92, 1.26, 3.14, 'ad6', back=True)


def ad_bridge(B):
    """a footbridge across the track (along X; towers 30 m apart, the game stretches it), sponsors both ways"""
    for sx in (-1, 1):
        cx = 15.0 * sx
        if B.lod == 0:
            for lx in (-0.75, 0.75):                               # the lattice tower: four legs, cross braces
                for ly in (-0.85, 0.85):
                    B.box('Leg', (cx + lx - 0.09, ly - 0.09, 0), (cx + lx + 0.09, ly + 0.09, 6.4), 'steel')
            for z0, z1 in ((0.3, 3.0), (3.0, 5.6)):
                for ly in (-0.85, 0.85):
                    B.rod('Brace', (cx - 0.75, ly, z0), (cx + 0.75, ly, z1), 0.05, 'steel')
                    B.rod('Brace', (cx + 0.75, ly, z0), (cx - 0.75, ly, z1), 0.05, 'steel')
                for lx in (-0.75, 0.75):
                    B.rod('Brace', (cx + lx, -0.85, z0), (cx + lx, 0.85, z1), 0.05, 'steel')
                B.box('Ring', (cx - 0.85, -0.95, z1 - 0.06), (cx + 0.85, 0.95, z1 + 0.06), 'steel')
        else:
            B.box('Tower', (cx - 0.85, -0.95, 0), (cx + 0.85, 0.95, 6.4), ('steel', 70))
    B.box('Deck', (-15.9, -1.0, 5.35), (15.9, 1.0, 5.6), 'grey')
    if B.lod == 0:
        for y in (-0.95, 0.95):                                    # handrails
            B.box('Rail', (-15.9, y - 0.03, 6.3), (15.9, y + 0.03, 6.38), 'white')
    B.box('Panel', (-14.6, -0.08, 5.6), (14.6, 0.08, 7.6), 'white')
    for k in range(4):                                             # four sponsors on each face
        x0 = -14.5 + k * 7.25
        B.decal_y('AdF%d' % k, 0.085, x0 + 0.05, x0 + 7.2, 5.66, 7.54, 'ad%d' % (k + 1))
        B.decal_y('AdB%d' % k, -0.085, x0 + 0.05, x0 + 7.2, 5.66, 7.54, 'ad%d' % (k + 5), back=True)
    B.box('Cap', (-14.7, -0.12, 7.6), (14.7, 0.12, 7.75), 'grey')


def race_control(B):
    """the race control tower: a podium, three glass storeys, the control room on top, the timing
    screen facing the track (+X)"""
    B.box('Podium', (-6.4, -5.3, 0), (5.6, 5.3, 3.0), 'slab')
    B.decal_x('PodiumGlass', 5.61, -4.8, 4.8, 0.4, 2.6, 'glass')
    B.box('Core', (-5.0, -4.0, 3.0), (4.0, 4.0, 15.0), ('white', 92))
    for f in range(4):
        z = 3.0 + 3.0 * f
        B.box('Floor%d' % f, (-5.2, -4.2, z), (4.2, 4.2, z + 0.35), 'white')
        if f < 4 and (B.lod == 0 or f == 1):
            zz = (z + 0.55, z + 2.75) if B.lod == 0 else (3.5, 14.8)
            B.decal_x('Glass%d' % f, 4.01, -3.8, 0.4, zz[0], zz[1], 'glass')
            B.decal_y('GlassL%d' % f, 4.01, -4.8, 3.8, zz[0], zz[1], 'glass')
            B.decal_y('GlassR%d' % f, -4.01, -4.8, 3.8, zz[0], zz[1], 'glass', back=True)
    # the timing screen
    B.box('ScreenBox', (4.0, 0.5, 5.6), (4.35, 3.9, 11.0), 'black')
    B.decal_x('Screen', 4.36, 0.7, 3.7, 5.9, 10.7, 'timing')
    # the control room: its window leans out over the track
    B.prism('Room', [(4.0, 15.0), (5.6, 18.5), (-5.5, 18.5), (-5.5, 15.0)], -4.6, 4.6, 'slab')
    B.decal('RoomGlass', [(4.05, -4.5, 15.2), (4.05, 4.5, 15.2), (5.62, 4.5, 18.3), (5.62, -4.5, 18.3)], 'glass')
    B.box('Roof', (-5.8, -4.9, 18.5), (5.8, 4.9, 19.0), 'white')
    if B.lod == 0:
        B.cyl('Mast', (-3.5, 2.5, 19.0), (-3.5, 2.5, 21.0), 0.06, 5, 'steel')
        B.box('MastLight', (-3.6, 2.4, 21.0), (-3.4, 2.6, 21.2), 'red')


def hospitality(B):
    """a team hospitality unit: two glass storeys (front +X) and a roof terrace with umbrellas"""
    B.box('Ground', (-5.4, -7.4, 0), (5.4, 7.4, 3.6), 'slab')
    B.box('Slab1', (-5.6, -7.6, 3.6), (5.6, 7.6, 4.0), 'white')
    B.box('Upper', (-5.2, -7.2, 4.0), (5.2, 7.2, 7.4), 'slab')
    B.box('Slab2', (-5.6, -7.6, 7.4), (5.6, 7.6, 7.8), 'white')
    B.box('Accent', (5.6, -7.6, 3.74), (5.63, 7.6, 3.86), 'orange')
    panes = 5 if B.lod == 0 else 1
    for z0, z1, x, y in ((0.25, 3.35, 5.41, 7.41), (4.2, 7.2, 5.21, 7.21)):
        w = 2 * (y - 0.3) / panes
        for k in range(panes):                     # glass panes on the front
            y0 = -(y - 0.3) + k * w
            B.decal_x('Pane', x, y0 + 0.06, y0 + w - 0.06, z0, z1, 'glass')
        B.decal_y('SideL', y, -(x - 0.5), x - 0.5, z0, z1, 'glass')
        B.decal_y('SideR', -y, -(x - 0.5), x - 0.5, z0, z1, 'glass', back=True)
    if B.lod == 0:
        for k, y in enumerate((-4.5, -1.5, 1.5, 4.5)):     # roof terrace umbrellas
            B.cyl('Pole', (1.5, y, 7.8), (1.5, y, 9.5), 0.04, 4, 'steel')
            ring = [(1.5 + 1.2 * math.cos(a * math.pi / 3), y + 1.2 * math.sin(a * math.pi / 3), 9.3) for a in range(6)]
            B.poly('Shade', ring + [(1.5, y, 9.8)], [(i, (i + 1) % 6, 6) for i in range(6)] + [tuple(range(6))[::-1]], 'cream')
        B.box('RailGlass', (5.3, -7.5, 7.8), (5.4, 7.5, 8.8), ('glass', 80))


SHARED_ATLAS = ('grandstand', 'billboard', 'ad_bridge', 'race_control', 'hospitality')


PROPS = {'gantry': gantry, 'bleacher': bleacher, 'tent': tent, 'flagpanel': flagpanel, 'crane': crane, 'tecpro': tecpro,
         'grandstand': grandstand, 'billboard': billboard, 'ad_bridge': ad_bridge, 'race_control': race_control,
         'hospitality': hospitality}
SPOTS = {'gantry': (-8.0, 1.5), 'bleacher': (0.0, -6.0), 'tent': (0.0, 3.0), 'flagpanel': (0.0, 7.0),
         'crane': (0.0, 11.5), 'tecpro': (0.0, 17.0),
         'grandstand': (-14.0, 0.0), 'billboard': (-14.0, 22.0), 'ad_bridge': (-30.0, 0.0),
         'race_control': (-40.0, -12.0), 'hospitality': (-40.0, 8.0)}


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
        P, N, M, UV = [], [], [], []
        for ob in objects(name, lod):
            ev = ob.evaluated_get(dg); me = ev.to_mesh()
            bm = bmesh.new(); bm.from_mesh(me); ev.to_mesh_clear()
            bm.transform(inv @ ob.matrix_world)
            bmesh.ops.triangulate(bm, faces=bm.faces[:]); bm.normal_update()
            uvl = bm.loops.layers.uv.active
            slots = [s.material.name if s.material else 'FL_grey' for s in ob.material_slots] or ['FL_grey']
            for f in bm.faces:
                if f.calc_area() < 1e-9:
                    continue
                key = slots[min(f.material_index, len(slots) - 1)].split('.')[0][3:]
                if key not in mats: mats.append(key)
                for lp in f.loops:
                    P += [round(c, 4) for c in lp.vert.co]
                    N += [round(c, 3) for c in f.normal]
                    UV += [round(c, 4) for c in lp[uvl].uv] if uvl is not None else [0.5, 0.5]
                M.append(mats.index(key))
            bm.free()
        lods.append(dict(tris=len(M), p=P, n=N, m=M, uv=UV))
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, 'w') as f:
        json.dump(dict(format='FL-prop-1', prop=name, palette={k: PALETTE[k] for k in PALETTE},
                       materials=mats, lods=lods), f, separators=(',', ':'))
    return [l['tris'] for l in lods]


def export_all(folder):
    return {name: export_json(name, os.path.join(folder, name + '.json')) for name in PROPS}


if __name__ == '__main__':
    print('built', build())
