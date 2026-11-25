"""Formula Legends -- the drivers' steering wheels (one per era) and the driver in the car, made in
Blender, low-poly for the PS Vita.

Run in Blender (Scripting tab, or the Blender MCP):
    exec(open(r'<path>/cockpit_parts.py').read()); build(); export_json(r'<path>/cockpit_parts.json')

Collection FL_Cockpit holds:
  FL_Steer_1998 / 2010 / 2016 / 2025   the steering wheels, each with the driver's gloves on its grips
  FL_Driver_LOD0 / LOD1                the driver as seen from outside: helmet, HANS, shoulders, belts, arms
  FL_Wheel_<year>_LOD0 / 1 / 2         each era's road wheel (tyre, rim, nut; 1998 grooved, 2025 covered)
  FL_Flap_<year>                       each era's rear wing top flap (the part DRS opens)
Each sits under its own empty (move the empty to move the part); they stand side by side in the scene.

Steering wheel axes (the game's, tools/gen_f1.py build_steering): the wheel lies in the YZ plane and
faces -X (towards the driver); +Y left, +Z up, metres; the column runs off along +X. The game draws
the wheel's screen at x -0.012, 0.136 x 0.092 m, centred 4 mm above the hub (Renderer::
drawNewInstruments), so every face plate leaves that window flat.

Driver axes: the helmet's centre at the origin, +X forward, +Y left, +Z up. The game places it at the
era's eye point (Renderer::drawDriver). Materials named suit, accent, helmet and stripe are painted in
the team's colours by the game (palette entries 1-4, as the pit crew).
"""
import bpy, bmesh, math, json, os
from mathutils import Vector, Matrix

ROOT_NAME = 'FL_Cockpit'
ERAS = ('1998', '2010', '2016', '2025')
PALETTE = dict(
    # the team's colours (defaults: a red driver, white flashes)
    suit=(0.72, 0.06, 0.05), accent=(0.95, 0.95, 0.93), helmet=(0.86, 0.08, 0.06), stripe=(0.96, 0.96, 0.94),
    # driver
    visor=(0.05, 0.06, 0.09), visorband=(0.85, 0.70, 0.10), hans=(0.04, 0.04, 0.045), belt=(0.10, 0.20, 0.55),
    buckle=(0.70, 0.71, 0.74), glove=(0.06, 0.06, 0.07), glovetrim=(0.85, 0.85, 0.82), balaclava=(0.12, 0.12, 0.13),
    # steering wheels
    carbon=(0.035, 0.035, 0.04), carbonlite=(0.09, 0.09, 0.10), alcantara=(0.06, 0.06, 0.065), alu=(0.60, 0.61, 0.64),
    screen=(0.01, 0.012, 0.02), red=(0.88, 0.07, 0.06), yellow=(0.98, 0.80, 0.05), green=(0.10, 0.75, 0.25),
    blue=(0.10, 0.35, 0.90), white=(0.94, 0.94, 0.92), orange=(1.0, 0.45, 0.05), purple=(0.55, 0.20, 0.80),
    paddle=(0.20, 0.21, 0.23), led=(0.12, 0.14, 0.12),
    # road wheels and DRS flaps (band: the sidewall stripe, painted in the compound's colour by the game;
    # flap / tip: the team's main and second colours)
    band=(0.95, 0.95, 0.93), rubber=(0.055, 0.055, 0.06), tread=(0.085, 0.085, 0.09), groove=(0.02, 0.02, 0.022),
    rim=(0.55, 0.56, 0.58), rimdark=(0.16, 0.16, 0.17), disc=(0.30, 0.22, 0.18), nut=(0.85, 0.10, 0.08),
    cover=(0.06, 0.06, 0.065), flap=(0.10, 0.10, 0.11), tip=(0.93, 0.93, 0.91))


def material(sw):
    name = 'FL_%s.CK' % sw
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    c = PALETTE[sw]
    m.diffuse_color = (c[0], c[1], c[2], 1.0)
    try:
        m.use_nodes = True
        b = m.node_tree.nodes.get('Principled BSDF')
        if b: b.inputs['Base Color'].default_value = m.diffuse_color
    except Exception:
        pass
    return m


# ---- a small mesh kit ----------------------------------------------------------------------------
class Kit:
    """builds named mesh objects under one empty; smooth=True objects export vertex normals"""
    def __init__(self, tag, root, col):
        self.tag, self.root, self.col = tag, root, col

    def mesh(self, name, V, F, mat, smooth=False, bevel=0.0, seg=1):
        full = '%s_%s' % (self.tag, name)
        me = bpy.data.meshes.new(full)
        me.from_pydata([tuple(v) for v in V], [], [tuple(f) for f in F])
        me.validate()
        bm = bmesh.new(); bm.from_mesh(me)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        if bevel > 0:
            bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=seg, profile=0.5, affect='EDGES', clamp_overlap=True)
        bm.to_mesh(me); bm.free()
        for p in me.polygons:
            p.use_smooth = smooth
        mats = mat if isinstance(mat, (list, tuple)) else [mat]
        for m in mats:
            me.materials.append(material(m))
        ob = bpy.data.objects.new(full, me)
        ob['fl_smooth'] = bool(smooth)
        self.col.objects.link(ob)
        ob.parent = self.root
        return ob

    def box(self, name, lo, hi, mat, bevel=0.0):
        V = [(x, y, z) for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
        F = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
        return self.mesh(name, V, F, mat, bevel=bevel)

    def slab(self, name, outline, x0, x1, mat, bevel=0.0):
        """a (y, z) outline pushed out along X from x0 to x1 (face plates, buttons, paddles)"""
        n = len(outline)
        V = [(x0, y, z) for (y, z) in outline] + [(x1, y, z) for (y, z) in outline]
        F = [tuple(range(n))[::-1], tuple(range(n, 2 * n))]
        for i in range(n):
            j = (i + 1) % n
            F.append((i, j, n + j, n + i))
        return self.mesh(name, V, F, mat, bevel=bevel)

    def cyl(self, name, a, b, r, mat, sides=8, smooth=False, r2=None):
        a, b = Vector(a), Vector(b)
        t = (b - a).normalized()
        ref = Vector((0, 0, 1)) if abs(t.z) < 0.9 else Vector((1, 0, 0))
        u = t.cross(ref).normalized(); w = t.cross(u)
        V = []
        for p, rr in ((a, r), (b, r if r2 is None else r2)):
            for k in range(sides):
                ang = 2 * math.pi * k / sides
                V.append(tuple(p + rr * (math.cos(ang) * u + math.sin(ang) * w)))
        F = [tuple(range(sides))[::-1], tuple(range(sides, 2 * sides))]
        for k in range(sides):
            j = (k + 1) % sides
            F.append((k, j, sides + j, sides + k))
        return self.mesh(name, V, F, mat, smooth=smooth)

    def tube(self, name, pts, r, mat, sides=6, smooth=True, caps=True, rs=None):
        """a tube through points (radius r, or rs[i] per point)"""
        pts = [Vector(p) for p in pts]
        V, F = [], []
        prev_u = None
        for i, p in enumerate(pts):
            a = pts[max(i - 1, 0)]; b = pts[min(i + 1, len(pts) - 1)]
            t = (b - a).normalized()
            ref = Vector((0, 0, 1)) if abs(t.z) < 0.9 else Vector((1, 0, 0))
            u = t.cross(ref).normalized()
            if prev_u is not None and u.dot(prev_u) < 0:
                u = -u
            prev_u = u
            w = t.cross(u)
            rr = rs[i] if rs else r
            rx, ry = rr if isinstance(rr, tuple) else (rr, rr)
            for k in range(sides):
                ang = 2 * math.pi * k / sides + math.pi / sides
                V.append(tuple(p + rx * math.cos(ang) * u + ry * math.sin(ang) * w))
        for i in range(len(pts) - 1):
            for k in range(sides):
                a = i * sides + k; b = i * sides + (k + 1) % sides
                F.append((a, b, b + sides, a + sides))
        if caps:
            F.append(tuple(range(sides))[::-1])
            F.append(tuple(range((len(pts) - 1) * sides, len(pts) * sides)))
        return self.mesh(name, V, F, mat, smooth=smooth)

    def rings(self, name, R, mat_of_band, smooth=True, cap0=True, top=None):
        """faces between rings of points (all rings the same length); mat_of_band(k, i) -> material"""
        n = len(R[0])
        V = [p for r in R for p in r]
        F, M = [], []
        names = []
        for k in range(len(R) - 1):
            for i in range(n):
                j = (i + 1) % n
                F.append((k * n + i, k * n + j, (k + 1) * n + j, (k + 1) * n + i))
                M.append(mat_of_band(k, i))
        if top is not None:
            V.append(top); t = len(V) - 1
            for i in range(n):
                F.append(((len(R) - 1) * n + i, (len(R) - 1) * n + (i + 1) % n, t)); M.append(mat_of_band(len(R) - 1, i))
        if cap0:
            F.append(tuple(range(n))[::-1]); M.append(mat_of_band(-1, 0))
        for m in M:
            if m not in names:
                names.append(m)
        ob = self.mesh(name, V, F, names, smooth=smooth)
        for p, m in zip(ob.data.polygons, M):
            p.material_index = names.index(m)
        return ob


def make_root(name, at, col):
    e = bpy.data.objects.get(name)
    if e is None:
        e = bpy.data.objects.new(name, None)
        col.objects.link(e)
    e.location = at
    e.empty_display_size = 0.1
    return e


# ---- steering wheels -----------------------------------------------------------------------------
# Where the game draws each era's screen on the wheel (Renderer::drawCockpit, WS_Z / WS_K): the
# centre height and scale of its 0.24 x 0.154 m panel (gear, speed, lap time, rev bar, shift lights on
# top). 1998: a small LCD at the top of the hub; 2010: a small screen; 2016 and 2025: the big screen.
# The plate stays flat behind each window, and nothing stands in front of it.
SCREENS = {'1998': (0.046, 0.42), '2010': (0.030, 0.55), '2016': (0.006, 0.72), '2025': (0.004, 0.72)}


def screen_window(year, pad=0.006):
    zc, k = SCREENS[year]
    hw = 0.12 * k + pad
    return (-hw, hw, zc - 0.070 * k - pad, zc + 0.084 * k + pad)


def rounded_rect(y0, y1, z0, z1, r, n=3):
    pts = []
    for (cy, cz, a0) in ((y1 - r, z1 - r, 0), (y0 + r, z1 - r, 90), (y0 + r, z0 + r, 180), (y1 - r, z0 + r, 270)):
        for k in range(n + 1):
            a = math.radians(a0 + 90 * k / n)
            pts.append((cy + r * math.cos(a), cz + r * math.sin(a)))
    return pts


def button(K, name, y, z, col, r=0.0085, x=-0.010, round_=True):
    if round_:
        K.cyl(name, (x + 0.004, y, z), (x - 0.004, y, z), r, col, sides=8)
    else:
        K.box(name, (x - 0.004, y - r, z - r * 0.8), (x + 0.004, y + r, z + r * 0.8), col)


def rotary(K, name, y, z, r=0.012, col='alu', ring=None):
    K.cyl(name, (-0.004, y, z), (-0.020, y, z), r, col, sides=10, smooth=True)
    K.cyl(name + 'Cap', (-0.020, y, z), (-0.024, y, z), r * 0.7, 'carbonlite', sides=10)
    if ring:
        K.cyl(name + 'Ring', (-0.006, y, z), (-0.010, y, z), r * 1.35, ring, sides=10)


def glove(K, name, y, z, s):
    """a gloved hand closed round the grip at (y, z); s = +1 left hand, -1 right"""
    # the palm and fingers wrap the grip: a squashed tube round it, a bit longer than the hand
    K.tube(name + 'Fist', [(-0.002, y, z + 0.045), (-0.002, y, z - 0.040)], (0.030, 0.026), 'glove', sides=8,
           rs=[(0.030, 0.027), (0.028, 0.025)])
    # thumb resting on the face plate, towards the buttons
    K.tube(name + 'Thumb', [(-0.016, y - s * 0.010, z + 0.035), (-0.022, y - s * 0.038, z + 0.040)], 0.010, 'glove', sides=6)
    # the wrist and cuff, running back towards the driver's elbows (-X, out and down)
    K.tube(name + 'Cuff', [(-0.012, y, z - 0.006), (-0.075, y + s * 0.018, z - 0.040)], 0.034, 'glove', sides=8,
           rs=[0.029, 0.036])
    K.cyl(name + 'Trim', (-0.050, y + s * 0.011, z - 0.024), (-0.062, y + s * 0.014, z - 0.031), 0.037, 'glovetrim', sides=8)


def bezel(K, year):
    """the frame round the screen window, standing 2 mm proud of the plate"""
    y0, y1, z0, z1 = screen_window(year)
    K.slab('Bezel', rounded_rect(y0, y1, z0, z1, 0.006), -0.010, -0.007, 'carbonlite')


def grip(K, name, pts, rs, mat='alcantara'):
    K.tube(name, pts, 0.02, mat, sides=8, rs=rs)


def wheel_1998(K):
    """late-90s: a closed rim, nearly round with flattened top and bottom, suede on the sides where the
    hands go; a small carbon hub with a little LCD at the top, a handful of round buttons and two
    knobs, slim spokes out to the rim; the gear paddles behind"""
    A, B, zo = 0.135, 0.100, -0.004
    def rim_pt(deg):
        t = math.radians(deg)
        c, s_ = math.cos(t), math.sin(t)
        y = A * math.copysign(abs(c) ** 0.75, c)
        z = zo + B * math.copysign(abs(s_) ** 0.85, s_)
        return (0.004, y, max(z, -0.088))
    # the grips (sides, suede) and the carbon top and bottom of the rim
    for sgn, a0 in ((1, -48), (-1, 132)):
        grip(K, 'Grip%d' % sgn, [rim_pt(a0 + k * 12) for k in range(9)], [0.017, 0.019, 0.020, 0.021, 0.021, 0.021, 0.020, 0.019, 0.017])
    K.tube('RimTop', [rim_pt(48 + k * 12) for k in range(8)], 0.013, 'carbon', sides=6)
    K.tube('RimBottom', [rim_pt(228 + k * 12) for k in range(8)], 0.013, 'carbon', sides=6)
    # the hub plate: a rounded trapezoid
    plate = [(-0.060, -0.066), (0.060, -0.066), (0.074, 0.040), (0.068, 0.092), (-0.068, 0.092), (-0.074, 0.040)]
    K.slab('Plate', plate, -0.008, 0.016, 'carbon', bevel=0.004)
    bezel(K, '1998')
    for sgn in (-1, 1):
        K.tube('Spoke%d' % sgn, [(0.006, sgn * 0.066, 0.004), (0.006, sgn * 0.110, 0.000)], 0.012, 'carbon', sides=6)
        K.tube('SpokeLow%d' % sgn, [(0.006, sgn * 0.050, -0.060), (0.006, sgn * 0.075, -0.085)], 0.010, 'carbon', sides=6)
        # two round buttons a side and a knob below
        button(K, 'BtnA%d' % sgn, sgn * 0.050, -0.004, 'yellow' if sgn > 0 else 'blue', r=0.0085)
        button(K, 'BtnB%d' % sgn, sgn * 0.050, -0.030, 'red' if sgn > 0 else 'white', r=0.0085)
        rotary(K, 'Knob%d' % sgn, sgn * 0.020, -0.048, r=0.011)
        # the gear paddles behind the rim
        K.slab('Paddle%d' % sgn, [(sgn * 0.060, 0.030), (sgn * 0.128, 0.040), (sgn * 0.132, 0.004), (sgn * 0.064, -0.006)],
               0.022, 0.028, 'paddle', bevel=0.002)
    return A


def wheel_2010(K):
    """2010 (V8): a rectangular wheel, grips on each side, a small screen in the upper middle (its shift
    lights along the top), two columns of square buttons a side, a row of rotaries below and
    the thumb buttons at the top corners"""
    w, top, bot = 0.118, 0.084, -0.074
    plate = [(-w, top - 0.010), (-w + 0.012, top), (w - 0.012, top), (w, top - 0.010),
             (w, bot + 0.016), (w - 0.020, bot), (-w + 0.020, bot), (-w, bot + 0.016)]
    K.slab('Plate', plate, -0.008, 0.018, 'carbon', bevel=0.003)
    bezel(K, '2010')
    gy = w + 0.016
    for sgn in (-1, 1):
        grip(K, 'Grip%d' % sgn, [(0.002, sgn * (w - 0.004), 0.054), (0.002, sgn * gy, 0.030), (0.002, sgn * (gy + 0.002), -0.012),
                                 (0.002, sgn * gy, -0.050), (0.002, sgn * (w - 0.008), -0.072)], [0.016, 0.020, 0.022, 0.020, 0.016])
        # square buttons in two columns beside the screen, labelled by colour
        cols = ('green', 'white', 'blue', 'red', 'yellow', 'white')
        for k in range(6):
            y = sgn * (0.081 + (k % 2) * 0.020); z = 0.050 - (k // 2) * 0.024
            button(K, 'Btn%d_%d' % (sgn, k), y, z, cols[k], r=0.0072, round_=False)
        button(K, 'Thumb%d' % sgn, sgn * 0.100, 0.072, 'yellow' if sgn > 0 else 'red', r=0.009)
        # the rotaries: two a side along the bottom
        for k, y in enumerate((0.026, 0.068)):
            rotary(K, 'Rot%d_%d' % (sgn, k), sgn * y, -0.048, r=0.011)
        K.slab('Paddle%d' % sgn, [(sgn * 0.050, 0.040), (sgn * (w + 0.012), 0.048), (sgn * (w + 0.016), 0.010), (sgn * 0.055, 0.000)],
               0.026, 0.032, 'paddle', bevel=0.002)
        K.slab('Clutch%d' % sgn, [(sgn * 0.050, -0.012), (sgn * w, -0.010), (sgn * (w - 0.004), -0.044), (sgn * 0.055, -0.040)],
               0.034, 0.040, 'carbonlite', bevel=0.002)
    return gy


def wheel_2016(K):
    """2016 (hybrid): bigger, the large colour screen in the middle, shift lights in a strip across the
    top, a rotary each side of the screen and four along the bottom, buttons all round in team
    colours, thumb wheels at the lower corners and a notch in the bottom edge"""
    w, top, bot = 0.135, 0.074, -0.092
    plate = [(-w, top - 0.012), (-w + 0.010, top), (-0.060, top + 0.004), (0.060, top + 0.004), (w - 0.010, top), (w, top - 0.012),
             (w, bot + 0.022), (w - 0.026, bot), (0.030, bot), (0.020, bot + 0.012), (-0.020, bot + 0.012), (-0.030, bot),
             (-w + 0.026, bot), (-w, bot + 0.022)]
    K.slab('Plate', plate, -0.008, 0.018, 'carbon', bevel=0.003)
    bezel(K, '2016')
    gy = w + 0.016
    for sgn in (-1, 1):
        grip(K, 'Grip%d' % sgn, [(0.002, sgn * (w - 0.004), 0.062), (0.002, sgn * gy, 0.034), (0.002, sgn * (gy + 0.004), -0.012),
                                 (0.002, sgn * gy, -0.056), (0.002, sgn * (w - 0.010), -0.088)], [0.017, 0.021, 0.024, 0.021, 0.017])
        rotary(K, 'RotSide%d' % sgn, sgn * 0.110, -0.030, r=0.011, ring='orange')
        for k, y in enumerate((0.026, 0.066)):
            rotary(K, 'Rot%d_%d' % (sgn, k), sgn * y, -0.064, r=0.0105, ring=('blue', 'green')[k])
        for k, (y, z, col) in enumerate(((0.104, 0.058, 'yellow'), (0.122, 0.046, 'purple'), (0.104, 0.034, 'white'),
                                         (0.122, 0.020, 'green'), (0.108, 0.006, 'red'))):
            button(K, 'Btn%d_%d' % (sgn, k), sgn * y, z, col, r=0.0070)
        K.cyl('Thumbwheel%d' % sgn, (-0.016, sgn * 0.112, -0.074), (-0.016, sgn * 0.100, -0.074), 0.012, 'alu', sides=10, smooth=True)
        K.slab('Paddle%d' % sgn, [(sgn * 0.050, 0.040), (sgn * (w + 0.012), 0.052), (sgn * (w + 0.018), 0.010), (sgn * 0.055, 0.000)],
               0.026, 0.032, 'paddle', bevel=0.002)
        K.slab('Clutch%d' % sgn, [(sgn * 0.040, -0.014), (sgn * w, -0.012), (sgn * (w - 0.004), -0.050), (sgn * 0.045, -0.046)],
               0.034, 0.040, 'carbonlite', bevel=0.002)
    return gy


def wheel_2025(K):
    """2025 (ground effect): the widest; the screen sunk in a raised surround with the shift lights
    round its top, the top edge swept up into horns over the grips, a deep U cut out of the bottom
    between two lobes that carry the big rotaries, chunky moulded grips with finger ridges"""
    w, top, bot = 0.140, 0.062, -0.098
    plate = [(-w, top + 0.012), (-w + 0.022, top + 0.020), (-0.080, top), (0.080, top), (w - 0.022, top + 0.020), (w, top + 0.012),
             (w, -0.040), (w - 0.020, bot), (0.050, bot), (0.034, -0.066), (-0.034, -0.066), (-0.050, bot),
             (-w + 0.020, bot), (-w, -0.040)]
    K.slab('Plate', plate, -0.008, 0.020, 'carbon', bevel=0.003)
    # the raised surround of the screen, its top edge carrying the shift lights
    y0, y1, z0, z1 = screen_window('2025', pad=0.010)
    K.slab('Surround', rounded_rect(y0, y1, z0, z1, 0.010), -0.014, -0.007, 'carbonlite')
    bezel(K, '2025')
    gy = w + 0.014
    for sgn in (-1, 1):
        pts = [(0.000, sgn * (w - 0.006), 0.074), (0.000, sgn * gy, 0.046), (0.000, sgn * (gy + 0.006), 0.004),
               (0.000, sgn * (gy + 0.004), -0.040), (0.000, sgn * (w - 0.004), -0.082)]
        grip(K, 'Grip%d' % sgn, pts, [0.016, 0.024, 0.027, 0.025, 0.017], mat='rubber' if 'rubber' in PALETTE else 'alcantara')
        for k, z in enumerate((0.026, 0.006, -0.014)):       # finger ridges on the front of the grip
            K.cyl('Ridge%d_%d' % (sgn, k), (-0.022, sgn * (gy + 0.004), z), (-0.026, sgn * (gy + 0.004), z), 0.010, 'alcantara', sides=6)
        # the big rotaries on the lower lobes, smaller ones above them
        rotary(K, 'BigRot%d' % sgn, sgn * 0.092, -0.082, r=0.015, ring='orange')
        rotary(K, 'Rot%d' % sgn, sgn * 0.058, -0.076, r=0.010, ring='blue')
        rotary(K, 'RotUp%d' % sgn, sgn * 0.114, -0.036, r=0.010, ring='green')
        for k, (y, z, col) in enumerate(((0.105, 0.058, 'red'), (0.125, 0.046, 'yellow'), (0.105, 0.034, 'white'),
                                         (0.125, 0.020, 'purple'), (0.105, 0.010, 'green'), (0.110, -0.010, 'blue'))):
            button(K, 'Btn%d_%d' % (sgn, k), sgn * y, z, col, r=0.0068)
        # the thumb rollers on top of the grips
        K.cyl('Thumbwheel%d' % sgn, (-0.012, sgn * (w - 0.010), 0.066), (-0.012, sgn * (w - 0.024), 0.066), 0.011, 'alu', sides=10, smooth=True)
        K.slab('Paddle%d' % sgn, [(sgn * 0.050, 0.040), (sgn * (w + 0.012), 0.050), (sgn * (w + 0.018), 0.008), (sgn * 0.055, -0.002)],
               0.028, 0.034, 'paddle', bevel=0.002)
        K.slab('Clutch%d' % sgn, [(sgn * 0.040, -0.016), (sgn * w, -0.014), (sgn * (w - 0.004), -0.054), (sgn * 0.045, -0.050)],
               0.036, 0.042, 'carbonlite', bevel=0.002)
    return gy + 0.004


def steering_wheel(year, K):
    grips = {'1998': wheel_1998, '2010': wheel_2010, '2016': wheel_2016, '2025': wheel_2025}[year](K)
    # quick release hub and the column
    K.cyl('Hub', (0.018, 0, 0), (0.050, 0, 0), 0.030, 'alu', sides=10, smooth=True)
    K.cyl('Column', (0.050, 0, 0), (0.300, 0, -0.030), 0.020, 'carbonlite', sides=8, smooth=True)
    # the driver's gloves on the grips
    for sgn in (-1, 1):
        glove(K, 'Glove%d' % sgn, sgn * grips, -0.004, sgn)


# ---- the driver ----------------------------------------------------------------------------------
# a full-face helmet: longer than wide, an oval dome, the edge lower at the back; a visor opening across
# the front; for 2016-on style a small spoiler at the back of the crown
HL = dict(a=0.148, b=0.122, p=2.3, zc=0.012, hz=0.140, bot=-0.110, vis=(-0.010, 0.052))


def helmet_pt(th, z):
    zb = HL['bot'] - 0.030 * max(0.0, -math.cos(th)) - 0.012 * max(0.0, math.cos(th)) ** 2
    zz = max(z, zb)
    if zz <= HL['zc']:
        rf = 0.92 + 0.08 * max((zz - zb) / (HL['zc'] - zb), 0.0) ** 0.6
    else:
        rf = math.sqrt(max(0.0, 1 - ((zz - HL['zc']) / HL['hz']) ** 2))
    c, s_ = math.cos(th), math.sin(th)
    chin = min(max((HL['vis'][0] - zz) / (HL['vis'][0] - zb), 0.0), 1.0)
    rf *= 1 + 0.16 * max(0.0, c) ** 4 * chin
    k = (abs(c) ** HL['p'] + abs(s_) ** HL['p']) ** (-1 / HL['p'])
    return (HL['a'] * k * c * rf - 0.004, HL['b'] * k * s_ * rf, zz)


def helmet(K, lod):
    N = 16 if lod == 0 else 10
    ths = [2 * math.pi * i / N for i in range(N)]
    zs = [HL['bot'] - 0.05, (HL['bot'] + HL['vis'][0]) / 2, HL['vis'][0], HL['zc'], HL['vis'][1]]
    zs += [HL['zc'] + HL['hz'] * f for f in ((0.55, 0.78, 0.94) if lod == 0 else (0.65, 0.92))]
    R = [[helmet_pt(t, z) for t in ths] for z in zs]
    band = ['helmet', 'helmet', 'V', 'V', 'stripe', 'helmet', 'helmet', 'helmet']
    def mat(k, i):
        if k < 0:
            return 'balaclava'
        m = band[min(k, len(band) - 1)]
        if m == 'V':
            mid = math.degrees(ths[i]) + 180.0 / N
            mid = (mid + 180) % 360 - 180
            m = 'visor' if abs(mid) <= 62 else 'helmet'
        return m
    K.rings('Helmet', R, mat, smooth=True, top=(-0.010, 0, HL['zc'] + HL['hz']))
    if lod == 0:
        # the visor's tear-off tab and the peak over it, the top air intake, the rear spoiler
        K.box('VisorTab', (0.135, 0.070, HL['vis'][0] + 0.004), (0.142, 0.085, HL['vis'][0] + 0.018), 'visorband')
        K.slab('Peak', [(-0.095, HL['vis'][1] + 0.006), (0.095, HL['vis'][1] + 0.006), (0.090, HL['vis'][1] + 0.014),
                        (-0.090, HL['vis'][1] + 0.014)], 0.118, 0.138, 'helmet')
        K.box('Intake', (0.020, -0.022, HL['zc'] + HL['hz'] - 0.008), (0.090, 0.022, HL['zc'] + HL['hz'] + 0.008), 'hans', bevel=0.004)
        K.slab('Spoiler', [(-0.075, 0.105), (0.075, 0.105), (0.070, 0.118), (-0.070, 0.118)], -0.128, -0.100, 'stripe')


def driver(K, lod):
    """the driver from the helmet down to the elbows; the game puts the helmet's centre ~10 cm above
    the eye point it uses for the cockpit camera, so the helmet and HANS show above the cockpit rim and
    the shoulders and arms sit low inside it, hands at the wheel (~0.47 m ahead, 0.37 m down)"""
    smooth = lod == 0
    sides = 8 if lod == 0 else 5
    dz = -0.075                                       # everything below the helmet sits this much lower
    P = lambda x, y, z: (x, y, z + dz)
    helmet(K, lod)
    # the neck and the HANS collar: a carbon yoke on the shoulders, its back plate up behind the helmet
    K.tube('Neck', [(-0.010, 0, -0.08), (-0.030, 0, -0.24)], 0.055, 'balaclava', sides=sides)
    yoke = [P(-0.13, 0, -0.17), P(-0.09, 0.10, -0.19), P(0.02, 0.15, -0.22), P(0.10, 0.15, -0.26)]
    K.tube('HansL', yoke, 0.026, 'hans', sides=sides, smooth=smooth)
    K.tube('HansR', [(x, -y, z) for (x, y, z) in yoke], 0.026, 'hans', sides=sides, smooth=smooth)
    K.box('HansBack', (-0.175, -0.07, -0.20 + dz), (-0.115, 0.07, -0.07), 'hans', bevel=0.012 if lod == 0 else 0.0)
    # the torso, lying back in the seat: shoulders behind the helmet, chest down towards the wheel
    torso = [(P(-0.06, 0, -0.20), 0.08, 0.17), (P(0.00, 0, -0.27), 0.11, 0.215), (P(0.12, 0, -0.34), 0.12, 0.20),
             (P(0.28, 0, -0.42), 0.11, 0.17)]
    T = []
    n = 10 if lod == 0 else 6
    for (c, ru, rv) in torso:
        ring = []
        for i in range(n):
            a = 2 * math.pi * i / n
            ring.append((c[0] + ru * 0.45 * math.cos(a), c[1] + rv * math.sin(a), c[2] + ru * math.cos(a) * 0.9))
        T.append(ring)
    def tmat(k, i):
        if k < 0:
            return 'suit'
        a = 360.0 * (i + 0.5) / n
        return 'accent' if (k == 1 and 60 < a < 120) or (k == 1 and 240 < a < 300) else 'suit'
    K.rings('Torso', T, tmat, smooth=smooth)
    # the belts: shoulder straps over the HANS, down the chest to the buckle
    for s in (-1, 1):
        K.tube('Belt%d' % s, [P(-0.15, s * 0.06, -0.18), P(-0.06, s * 0.085, -0.15), P(0.03, s * 0.085, -0.20),
                               P(0.16, s * 0.04, -0.29)], 0.012, 'belt', sides=4, smooth=False)
    K.cyl('Buckle', P(0.17, 0, -0.27), P(0.19, 0, -0.30), 0.03, 'buckle', sides=8)
    # arms: from the shoulders out and forward, elbows bent and low, hands on the wheel
    for s, sd in ((1, 'L'), (-1, 'R')):
        sh = P(0.00, s * 0.205, -0.235); el = (0.17, s * 0.240, -0.430); wr = (0.42, s * 0.170, -0.385)
        K.tube('UpperArm' + sd, [sh, el], 0.052, 'suit', sides=sides, smooth=smooth, rs=[0.056, 0.046])
        K.tube('Forearm' + sd, [el, wr], 0.042, 'suit', sides=sides, smooth=smooth, rs=[0.046, 0.036])
        K.tube('Shoulder' + sd, [P(-0.03, s * 0.18, -0.205), P(0.03, s * 0.215, -0.235)], 0.050, 'accent', sides=sides, smooth=smooth)
        if lod == 0:
            K.tube('Glove' + sd, [wr, (0.47, s * 0.160, -0.372)], 0.034, 'glove', sides=6, rs=[0.032, 0.030])
            K.cyl('Cuff' + sd, (0.405, s * 0.173, -0.389), (0.425, s * 0.169, -0.384), 0.038, 'glovetrim', sides=6)


# ---- road wheels and DRS flaps --------------------------------------------------------------------
# per era (tools/gen_f1.py E): rear tyre radius and width, rim radius, grooved tread (1998), wheel covers
# (2025), and the rear wing's top flap: (leading x, trailing x, half span) of the wing
WHEEL = {'1998': dict(R=0.330, w=0.38, rim=0.170, groove=True, cover=False),
         '2010': dict(R=0.330, w=0.38, rim=0.170, groove=False, cover=False),
         '2016': dict(R=0.330, w=0.38, rim=0.170, groove=False, cover=False),
         '2025': dict(R=0.3625, w=0.45, rim=0.235, groove=False, cover=True)}
WING = {'1998': (-1.6745, -1.9655, 0.4925), '2010': (-1.85, -2.18, 0.375), '2016': (-1.902, -2.138, 0.3675),
        '2025': (-2.171, -2.389, 0.4875)}


def lathe(K, name, prof, sides, mats, smooth=True):
    """a surface of revolution about Y: prof = [(y, r)], band k between points k and k+1 uses mats[k]"""
    R = [[(r * math.cos(2 * math.pi * i / sides), y, r * math.sin(2 * math.pi * i / sides)) for i in range(sides)]
         for (y, r) in prof]
    return K.rings(name, R, lambda k, i: mats[max(k, 0)], smooth=smooth, cap0=False)


def wheel(year, K, lod):
    """axle along Y, the outer face at +Y, centred on the axle (the game spins it about Y and squeezes
    the fronts narrower)"""
    W = WHEEL[year]
    R, hw, rim = W['R'], W['w'] / 2, W['rim']
    sides = (16, 10, 7)[lod]
    # the tyre: inner sidewall, shoulder, tread (grooved in 1998), shoulder, outer sidewall with the stripe
    if lod == 2:
        prof = [(-hw, rim + 0.01), (-hw + 0.02, R), (hw - 0.02, R), (hw, rim + 0.01)]
        mats = ['rubber', 'tread', 'rubber']
    elif lod == 1:
        prof = [(-hw, rim + 0.012), (-hw, R - 0.03), (-hw + 0.03, R), (hw - 0.03, R), (hw, R - 0.03), (hw, rim + 0.012)]
        mats = ['rubber', 'rubber', 'tread', 'rubber', 'rubber']
    else:
        tread = [(-hw + 0.035, R)]
        tm = []
        if W['groove'] and lod == 0:
            g = 0.010
            for c in (-0.6, -0.2, 0.2, 0.6):          # four grooves round the tread (dark bands, slightly sunk)
                y = c * (hw - 0.035)
                tread += [(y - g, R - 0.004), (y + g, R - 0.004)]
                tm += ['tread', 'groove']
        tread += [(hw - 0.035, R)]
        tm += ['tread']
        prof = [(-hw, rim + 0.012), (-hw - 0.004, R - 0.06), (-hw + 0.004, R - 0.018)] + tread + \
               [(hw - 0.004, R - 0.018), (hw + 0.004, R - 0.045), (hw + 0.005, R - 0.075), (hw, rim + 0.012)]
        mats = ['rubber', 'rubber', 'rubber'] + tm + ['rubber', 'rubber', 'band' if lod == 0 else 'rubber', 'rubber']
        assert len(mats) == len(prof) - 1
    lathe(K, 'Tyre', prof, sides, mats, smooth=True)
    # the inner face: closed off dark
    K.mesh('Inner', [((rim + 0.012) * math.cos(2 * math.pi * i / sides), -hw, (rim + 0.012) * math.sin(2 * math.pi * i / sides)) for i in range(sides)],
           [tuple(range(sides))], 'rimdark')
    if W['cover']:
        # 18-inch rim behind a flat aero cover, the wheel nut in the middle
        K.cyl('Cover', (0, hw - 0.020, 0), (0, hw - 0.008, 0), rim + 0.012, 'cover', sides=sides)
        if lod == 0:
            lathe(K, 'Lip', [(hw - 0.012, rim + 0.014), (hw - 0.006, rim + 0.002)], sides, ['rim'], smooth=False)
            K.cyl('Nut', (0, hw - 0.010, 0), (0, hw + 0.010, 0), 0.045, 'nut', sides=8)
    else:
        # 13-inch rim: the barrel, a dished face with spokes, the brake disc showing behind, the nut
        if lod == 0:
            lathe(K, 'Barrel', [(hw - 0.012, rim + 0.012), (hw - 0.020, rim - 0.004), (hw - 0.080, rim - 0.012)], sides,
                  ['rim', 'rimdark'], smooth=False)
            K.cyl('Disc', (0, hw - 0.17, 0), (0, hw - 0.14, 0), rim - 0.03, 'disc', sides=max(sides // 2, 6))
        else:
            K.cyl('Face', (0, hw - 0.06, 0), (0, hw - 0.02, 0), rim + 0.01, 'rim' if lod == 1 else 'rimdark', sides=sides)
        if lod == 0:
            for k in range(6):
                a = 2 * math.pi * k / 6
                c, s_ = math.cos(a), math.sin(a)
                K.tube('Spoke%d' % k, [(0.045 * c, hw - 0.050, 0.045 * s_), (0.11 * c, hw - 0.040, 0.11 * s_),
                                       ((rim - 0.008) * c, hw - 0.022, (rim - 0.008) * s_)], 0.012, 'rim', sides=4,
                       smooth=False)
        if lod == 0:
            K.cyl('Hub', (0, hw - 0.070, 0), (0, hw - 0.030, 0), 0.050, 'rimdark', sides=8)
        if lod < 2:
            K.cyl('Nut', (0, hw - 0.035, 0), (0, hw - 0.005, 0), 0.034, 'nut', sides=6)


def flap(year, K):
    """the rear wing's top flap, its hinge (the leading edge) at the origin, running back along -X; the
    game tilts it up when DRS opens. An aerofoil section, the team's colour on top, a second-colour tip
    along the trailing edge, and the DRS actuator pod in the middle (2010 on)"""
    xa, xb, span = WING[year]
    c = (xa - xb) * 0.55
    hs = span - 0.01
    t = 0.022
    # section in X-Z (leading edge at 0, trailing edge at -c), cambered: high at the front, curled up at the back
    sec = [(0.0, 0.010), (-0.08 * c, 0.022), (-0.35 * c, 0.024), (-0.75 * c, 0.020), (-c, 0.032),
           (-c, 0.024), (-0.75 * c, 0.008), (-0.35 * c, 0.002), (-0.08 * c, 0.000), (0.0, 0.004)]
    n = len(sec)
    V = [(x, -hs, z) for (x, z) in sec] + [(x, hs, z) for (x, z) in sec]
    F = [tuple(range(n))[::-1], tuple(range(n, 2 * n))]
    M = ['flap', 'flap']
    for i in range(n):
        j = (i + 1) % n
        F.append((i, j, n + j, n + i))
        M.append('tip' if i in (3, 4) else 'flap')          # the band along the trailing edge
    names = ['flap', 'tip']
    ob = K.mesh('Flap', V, F, names, smooth=False)
    for p, m in zip(ob.data.polygons, M):
        p.material_index = names.index(m)
    # the gurney strip on the trailing edge
    K.box('Gurney', (-c - 0.004, -hs, 0.024), (-c + 0.002, hs, 0.040), 'carbon')
    if year != '1998':
        K.box('Pod', (-0.45 * c, -0.035, 0.022), (-0.05 * c, 0.035, 0.040), 'carbon', bevel=0.004)


# ---- build / export ------------------------------------------------------------------------------
def clear():
    col = bpy.data.collections.get(ROOT_NAME)
    if col:
        for ob in list(col.objects):
            bpy.data.objects.remove(ob, do_unlink=True)
    for me in list(bpy.data.meshes):
        if me.users == 0:
            bpy.data.meshes.remove(me)


def build(origin=(0.0, 6.0, 1.0)):
    clear()
    scene = bpy.context.scene.collection
    col = bpy.data.collections.get(ROOT_NAME) or bpy.data.collections.new(ROOT_NAME)
    if col.name not in scene.children:
        scene.children.link(col)
    ox, oy, oz = origin
    for i, year in enumerate(ERAS):
        root = make_root('FL_Steer_%s' % year, (ox, oy + 0.45 * i, oz), col)
        steering_wheel(year, Kit('Steer%s' % year, root, col))
    for lod in (0, 1):
        root = make_root('FL_Driver_LOD%d' % lod, (ox + 1.0, oy + 0.7 * lod, oz), col)
        driver(Kit('Driver%d' % lod, root, col), lod)
    for i, year in enumerate(ERAS):
        for lod in (0, 1, 2):
            root = make_root('FL_Wheel_%s_LOD%d' % (year, lod), (ox + 2.0 + 0.9 * lod, oy + 0.9 * i, oz), col)
            wheel(year, Kit('Wheel%s_%d' % (year, lod), root, col), lod)
        root = make_root('FL_Flap_%s' % year, (ox + 5.0, oy + 1.2 * i, oz), col)
        flap(year, Kit('Flap%s' % year, root, col))
    return {'objects': len(col.objects)}


def export_part(root):
    dg = bpy.context.evaluated_depsgraph_get()
    to_local = root.matrix_world.inverted()
    P, N, M = [], [], []
    mats = []
    for ob in root.children:
        if ob.type != 'MESH':
            continue
        smooth = bool(ob.get('fl_smooth', False))
        ev = ob.evaluated_get(dg); me = ev.to_mesh()
        bm = bmesh.new(); bm.from_mesh(me); ev.to_mesh_clear()
        bm.transform(to_local @ ob.matrix_world)
        bmesh.ops.triangulate(bm, faces=bm.faces[:]); bm.normal_update()
        slots = [s.material.name.split('.')[0][3:] for s in ob.material_slots]
        for f in bm.faces:
            if f.calc_area() < 1e-10:
                continue
            key = slots[min(f.material_index, len(slots) - 1)]
            if key not in mats:
                mats.append(key)
            for lp in f.loops:
                P += [round(c, 5) for c in lp.vert.co]
                N += [round(c, 4) for c in (lp.vert.normal if smooth else f.normal)]
            M.append(mats.index(key))
        bm.free()
    return dict(tris=len(M), p=P, n=N, mats=mats, m=M)


def export_json(path):
    out = dict(format='FL-cockpit-1', palette={k: list(v) for k, v in PALETTE.items()}, steering={}, driver=[])
    for year in ERAS:
        out['steering'][year] = export_part(bpy.data.objects['FL_Steer_%s' % year])
    for lod in (0, 1):
        out['driver'].append(export_part(bpy.data.objects['FL_Driver_LOD%d' % lod]))
    out['wheels'] = {y: [export_part(bpy.data.objects['FL_Wheel_%s_LOD%d' % (y, l)]) for l in (0, 1, 2)] for y in ERAS}
    out['flaps'] = {y: export_part(bpy.data.objects['FL_Flap_%s' % y]) for y in ERAS}
    with open(path, 'w') as f:
        json.dump(out, f, separators=(',', ':'))
    return {'steering': {y: out['steering'][y]['tris'] for y in ERAS}, 'driver': [d['tris'] for d in out['driver']],
            'wheels': {y: [w['tris'] for w in out['wheels'][y]] for y in ERAS}, 'flaps': {y: out['flaps'][y]['tris'] for y in ERAS}}
