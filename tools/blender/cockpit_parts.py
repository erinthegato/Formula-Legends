"""Formula Legends -- the drivers' steering wheels (one per era) and the driver in the car, made in
Blender, low-poly for the PS Vita.

Run in Blender (Scripting tab, or the Blender MCP):
    exec(open(r'<path>/cockpit_parts.py').read()); build(); export_json(r'<path>/cockpit_parts.json')

Collection FL_Cockpit holds:
  FL_Steer_1998 / 2010 / 2016 / 2025   the steering wheels, each with the driver's gloves on its grips
  FL_Driver_LOD0 / LOD1                the driver as seen from outside: helmet, HANS, shoulders, belts, arms
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
    paddle=(0.20, 0.21, 0.23), led=(0.12, 0.14, 0.12))


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
SCREEN = (-0.074, 0.074, -0.044, 0.054)       # y0, y1, z0, z1: the window the game draws its screen in


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


def steering_wheel(year, K):
    if year == '1998':
        # small and nearly round: a narrow carbon plate, the rim a flat-topped oval with suede grips,
        # a handful of buttons and two knobs, the gear paddles behind
        w = 0.130
        plate = rounded_rect(-0.088, 0.088, -0.070, 0.064, 0.022)
        K.slab('Plate', plate, -0.008, 0.016, 'carbon', bevel=0.003)
        top = [(0.004, w * math.sin(math.radians(a)), 0.095 * math.cos(math.radians(a)) - 0.005) for a in range(-95, 96, 19)]
        K.tube('RimTop', top, 0.014, 'carbon', sides=6)
        for s in (-1, 1):
            K.tube('Grip%d' % s, [(0.004, s * w, 0.030), (0.004, s * (w + 0.006), -0.010), (0.004, s * (w - 0.006), -0.055),
                                  (0.004, s * (w - 0.030), -0.080)], 0.019, 'alcantara', sides=8)
            K.tube('Spoke%d' % s, [(0.006, s * 0.085, -0.040), (0.006, s * (w - 0.004), -0.030)], 0.011, 'carbon', sides=6)
            button(K, 'BtnA%d' % s, s * 0.060, -0.058, 'red' if s > 0 else 'yellow')
            rotary(K, 'Knob%d' % s, s * 0.035, -0.058, r=0.010)
            K.slab('Paddle%d' % s, [(s * 0.040, 0.020), (s * 0.105, 0.030), (s * 0.112, -0.010), (s * 0.045, -0.020)],
                   0.024, 0.030, 'paddle')
        K.box('Lcd', (-0.010, -0.040, 0.040), (-0.008, 0.040, 0.056), 'led')
        grips = w
    else:
        # the butterfly wheels: a wide face plate with the screen, grips on each side, the top edge
        # carrying the shift lights; each era a little bigger, with more knobs
        big = {'2010': 0, '2016': 1, '2025': 2}[year]
        w = (0.142, 0.150, 0.152)[big]
        top, bot = (0.072, 0.078, 0.082)[big], (-0.088, -0.092, -0.095)[big]
        plate = [(-w + 0.020, top), (-0.060, top + 0.006), (0.060, top + 0.006), (w - 0.020, top),
                 (w - 0.006, 0.040), (w - 0.006, -0.050), (w - 0.035, bot), (0.050, bot - 0.004),
                 (-0.050, bot - 0.004), (-w + 0.035, bot), (-w + 0.006, -0.050), (-w + 0.006, 0.040)]
        plate = [(-y, z) for (y, z) in reversed(plate)]
        K.slab('Plate', plate, -0.008, 0.018, 'carbon', bevel=0.003)
        # the screen bezel (the game draws the screen in this window)
        # (a frame round the window, standing 2 mm proud; the window itself is the plate, under the game's screen)
        K.slab('Bezel', rounded_rect(SCREEN[0] - 0.006, SCREEN[1] + 0.006, SCREEN[2] - 0.006, SCREEN[3] + 0.006, 0.008),
               -0.010, -0.007, 'carbonlite')
        if year == '2025':      # the top cut away in the middle: the curved top edge of the 2020s wheels
            K.tube('TopArc', [(0.0, -0.11, top - 0.004), (0.0, -0.05, top + 0.016), (0.0, 0.05, top + 0.016),
                              (0.0, 0.11, top - 0.004)], 0.010, 'carbon', sides=6)
        for s in (-1, 1):
            # grips: suede-covered, raked out at the bottom
            gy = s * (w + 0.010)
            K.tube('Grip%d' % s, [(0.002, s * (w - 0.004), 0.058), (0.002, gy, 0.030), (0.002, gy + s * 0.004, -0.010),
                                  (0.002, gy, -0.050), (0.002, s * (w - 0.012), -0.080)], 0.021, 'alcantara', sides=8,
                   rs=[0.017, 0.021, 0.023, 0.021, 0.017])
            # buttons beside the screen: two columns, coloured like the real ones
            cols = [('green', 'red', 'blue', 'white'), ('yellow', 'orange', 'white', 'blue'), ('purple', 'green', 'yellow', 'red')][big]
            for k in range(3 if big == 0 else 4):
                y = s * (0.094 + (k % 2) * 0.020); z = 0.040 - (k // 2) * 0.028
                button(K, 'Btn%d_%d' % (s, k), y, z, cols[k], r=0.0075, round_=(big != 1))
            # big buttons at the thumbs: pit limiter / radio / drink / neutral
            button(K, 'Thumb%d' % s, s * 0.120, 0.052, 'yellow' if s > 0 else 'red', r=0.009)
            # the rotaries below the screen (more of them each era)
            n_rot = (1, 2, 2)[big]
            for k in range(n_rot):
                rotary(K, 'Rot%d_%d' % (s, k), s * (0.030 + 0.040 * k), -0.066, r=0.011 if big else 0.012,
                       ring=('blue', 'orange', 'green')[k % 3] if big else None)
            if big >= 1:        # thumb wheels at the bottom corners
                K.cyl('Thumbwheel%d' % s, (-0.016, s * 0.105, -0.072), (-0.016, s * 0.105 - s * 0.012, -0.072), 0.012,
                      'alu', sides=10, smooth=True)
            # paddles behind: gears on top, clutch below
            K.slab('Paddle%d' % s, [(s * 0.050, 0.040), (s * (w + 0.010), 0.050), (s * (w + 0.016), 0.010),
                                    (s * 0.055, 0.000)], 0.026, 0.032, 'paddle', bevel=0.002)
            K.slab('Clutch%d' % s, [(s * 0.050, -0.012), (s * (w + 0.000), -0.010), (s * (w - 0.004), -0.045),
                                    (s * 0.055, -0.040)], 0.034, 0.040, 'carbonlite', bevel=0.002)
        # shift-light strip along the top edge (lit by the game's screen; the strip is the housing)
        K.box('LedBar', (-0.011, -0.070, top - 0.012), (-0.006, 0.070, top - 0.004), 'led')
        grips = w + 0.010
    # quick release hub and the column
    K.cyl('Hub', (0.018, 0, 0), (0.050, 0, 0), 0.030, 'alu', sides=10, smooth=True)
    K.cyl('Column', (0.050, 0, 0), (0.300, 0, -0.030), 0.020, 'carbonlite', sides=8, smooth=True)
    # the driver's gloves on the grips
    for s in (-1, 1):
        glove(K, 'Glove%d' % s, s * grips, -0.004, s)


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
    with open(path, 'w') as f:
        json.dump(out, f, separators=(',', ':'))
    return {'steering': {y: out['steering'][y]['tris'] for y in ERAS}, 'driver': [d['tris'] for d in out['driver']]}
