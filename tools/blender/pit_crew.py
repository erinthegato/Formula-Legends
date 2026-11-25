"""Formula Legends -- the pit crew member, rigged, with his four motions, made for the PS Vita.

Run in Blender (Scripting tab, or the Blender MCP):
    exec(open(r'<path>/pit_crew.py').read()); build(); export_json(r'<path>/pit_crew.json')

* One low-poly body (about 560 triangles) on an armature: hips, spine, chest, neck, head, arms,
  legs and a wheel gun held in the right hand. Each piece of the suit follows one bone (the knee,
  elbow and waist rings are shared by two, so the joints bend without gaps).
* The motions are written below as joint angles (degrees, about the armature's own axes:
  X forward, Y to the left, Z up; a positive Y angle leans a body part forward / swings a hanging
  limb backward). Each motion has four poses; the game plays them forwards and back, blending.
  They are keyed on the timeline too (idle 1-10, wheel gun 11-20, push 21-30, celebrate 31-40),
  so you can scrub through them or change a key and re-export.
* The man faces +X (the way the game places people) with his feet on the ground.
* export_json() writes the triangles once and the vertex positions of all 16 poses (the same
  vertices in the same order, so the game can blend them); tools/gen_crew.py makes the game files.
"""
import bpy, json, math
from mathutils import Vector, Matrix, Euler

# suit, accent, helmet and stripe are the team's colours in the game (tools/gen_crew.py swaps them);
# these are the defaults: navy suit, orange flashes, a white helmet with a navy stripe
PALETTE = dict(suit=(0.08, 0.13, 0.32), accent=(1.0, 0.45, 0.05), stripe=(0.08, 0.13, 0.32), panel=(0.93, 0.93, 0.91),
               reflect=(0.78, 0.80, 0.84), belt=(0.10, 0.10, 0.11), glove=(0.06, 0.06, 0.07),
               boot=(0.05, 0.05, 0.06), helmet=(0.93, 0.93, 0.92), visor=(0.32, 0.16, 0.04),
               balaclava=(0.13, 0.13, 0.14), gun=(0.74, 0.75, 0.78), gundark=(0.12, 0.12, 0.13))
ORIGIN = (-24.0, 0.0, 0.0)       # in the scene: behind the trackside objects
NAME = 'PitCrew'

# ---- the skeleton (rest pose: standing, arms down; X forward, +Y = his left)
BONES = [  # name, head, tail, parent
    ('hips', (0, 0, 0.95), (0, 0, 1.05), None),
    ('spine', (0, 0, 1.05), (0, 0, 1.28), 'hips'),
    ('chest', (0, 0, 1.28), (0, 0, 1.50), 'spine'),
    ('neck', (0, 0, 1.50), (0, 0, 1.60), 'chest'),
    ('head', (0, 0, 1.60), (0, 0, 1.82), 'neck'),
]
for s, sd in ((1, 'L'), (-1, 'R')):
    BONES += [
        ('upperarm.' + sd, (0, s * 0.19, 1.47), (0, s * 0.23, 1.18), 'chest'),
        ('forearm.' + sd, (0, s * 0.23, 1.18), (0.01, s * 0.24, 0.95), 'upperarm.' + sd),
        ('hand.' + sd, (0.01, s * 0.24, 0.95), (0.02, s * 0.245, 0.84), 'forearm.' + sd),
        ('thigh.' + sd, (0, s * 0.095, 0.93), (0, s * 0.10, 0.51), 'hips'),
        ('shin.' + sd, (0, s * 0.10, 0.51), (0, s * 0.10, 0.10), 'thigh.' + sd),
        ('foot.' + sd, (0, s * 0.10, 0.10), (0.16, s * 0.10, 0.03), 'shin.' + sd),
    ]
BONES.append(('gun', (0.03, -0.245, 0.88), (0.25, -0.245, 0.88), 'hand.R'))
HEAD = {b[0]: Vector(b[1]) for b in BONES}


# ---- the motions -------------------------------------------------------------------------------
def both(d, **kw):
    """d[bone.L] = angles, and the mirror image for bone.R (X and Z angles change sign)"""
    for k, (x, y, z) in kw.items():
        d[k + '.L'] = (x, y, z); d[k + '.R'] = (-x, y, -z)
    return d


def pose(off=(0, 0, 0), gun=0.0, **bones):
    d = {'off': off, 'gunscale': gun}
    for k, v in bones.items():
        d[k.replace('_L', '.L').replace('_R', '.R')] = v
    return d


def flat_feet(p, extra=(0, 0)):
    """turn each foot so it stays level whatever the hip, thigh and shin do (plus `extra` degrees)"""
    for i, sd in enumerate(('L', 'R')):
        tot = sum(p.get(b, (0, 0, 0))[1] for b in ('hips', 'thigh.' + sd, 'shin.' + sd))
        p['foot.' + sd] = (0, -tot + extra[i], 0)
    return p


def idle():
    """standing at ease: breathing, the weight drifting onto one leg, looking round the garage"""
    f = []
    for k in range(4):
        a = k / 3.0
        p = pose(off=(0, 0.022 * a, -0.008 * a),
                 spine=(-3 * a, 1 * a, 0), chest=(-1 * a, 2 * a - 1, 0), neck=(0, 0, 6 * a),
                 head=(0, 4 * math.sin(a * 3), 26 * a - 4),
                 thigh_L=(-1.5 * a, 0, 0), thigh_R=(-1.5 * a, -7 * a, 0), shin_R=(0, 14 * a, 0))
        both(p, upperarm=(6, -4, 0), forearm=(0, -14, 0), hand=(0, 4, 0))
        p['upperarm.R'] = (-6 - 2 * a, -4 - 9 * a, -6 * a)    # right hand comes up as he turns
        p['forearm.R'] = (0, -14 - 18 * a, 0)
        p['upperarm.L'] = (6 - 1 * a, -4 + 4 * a, 0)
        f.append(flat_feet(p))
    return f


def wheelgun():
    """down on the right knee at the wheel, both hands on the gun: in, tighten (the gun twists), back"""
    f = []
    for k, (reach, twist, bob) in enumerate(((0.0, 0, 0.0), (1.0, 0, -0.01), (1.0, 18, -0.012), (0.6, 30, -0.006))):
        p = pose(off=(-0.30 + 0.05 * reach, 0, -0.457 + bob), gun=1.0,
                 spine=(0, 16 + 4 * reach, 0), chest=(0, 12, 0), neck=(0, -10, 0), head=(0, -14 + 3 * reach, 0),
                 thigh_L=(4, -95, 0), shin_L=(0, 95, 0),
                 thigh_R=(0, -5, 0), shin_R=(0, 95, 0), foot_R=(0, 85, 0))
        p['foot.L'] = (0, 0, 0)
        p['upperarm.R'] = (12, -42 - 10 * reach, 0)
        p['forearm.R'] = (0, -16 + 12 * reach, 0)
        p['hand.R'] = (twist * 0.6, 56 - 4 * reach, 0)
        p['upperarm.L'] = (-24, -44 - 10 * reach, -10)
        p['forearm.L'] = (0, -26 + 10 * reach, -14)
        p['hand.L'] = (-twist * 0.4, 40, 0)
        f.append(p)
    return f


def push():
    """running behind the car, leant into it, hands on the rear: the legs stride, the arms stay put"""
    legs = (((-40, 30), (30, 45)), ((-20, 25), (0, 85)), ((10, 25), (-30, 70)), ((30, 45), (-40, 30)))
    f = []
    for k, ((tl, sl), (tr, sr)) in enumerate(legs):
        bob = (-0.09, -0.05, -0.05, -0.09)[k]
        p = pose(off=(-0.10, 0, bob), hips=(0, 14, 0), spine=(0, 16, 0), chest=(0, 16, 0),
                 neck=(0, -22, 0), head=(0, -20, 0),
                 thigh_L=(0, tl, 0), shin_L=(0, sl, 0), thigh_R=(0, tr, 0), shin_R=(0, sr, 0))
        both(p, upperarm=(-6, -76 + 2 * (k % 2), 0), forearm=(0, -6, 0), hand=(0, 50, 0))
        flat_feet(p)
        # the back foot pushes off on its toes; a foot swinging through hangs from the bent knee
        for sd, th, sh in (('L', tl, sl), ('R', tr, sr)):
            if sh > 60:
                p['foot.' + sd] = (0, -25, 0)
            elif th > 15:
                p['foot.' + sd] = (0, p['foot.' + sd][1] + 35, 0)
        f.append(p)
    return f


def celebrate():
    """on the podium: crouch, then jump with both fists in the air"""
    f = []
    for k, (crouch, arms, fore, lift) in enumerate(((1.0, 100, 40, 0.0), (0.45, 135, 22, 0.03), (0.0, 160, 4, 0.10), (0.0, 166, -8, 0.17))):
        th = -28 * crouch
        p = pose(off=(0, 0, -0.11 * crouch + lift), spine=(0, 8 * crouch - 6 * (k == 3), 0), chest=(0, 3, 0),
                 head=(0, -6 - 14 * (1 - crouch), 0))
        both(p, thigh=(0, th - 2 * (1 - crouch), 0), shin=(0, -2 * th + 6 * (1 - crouch), 0),
             upperarm=(arms, -6, 0), forearm=(-fore, -10, 0), hand=(0, -10, 0))
        flat_feet(p, extra=(32 * (1 - crouch), 32 * (1 - crouch)))   # toes pointed in the air
        f.append(p)
    return f


ANIMS = {'idle': idle, 'wheelgun': wheelgun, 'push': push, 'celebrate': celebrate}


# ---- the body ------------------------------------------------------------------------------------
class Body:
    def __init__(self):
        self.V, self.W, self.F, self.M, self.S = [], [], [], [], []   # verts, weights, faces, materials, smooth
        self.mats = []

    def mat(self, sw):
        if sw not in self.mats:
            self.mats.append(sw)
        return self.mats.index(sw)

    def face(self, idx, sw, smooth):
        self.F.append(idx); self.M.append(self.mat(sw)); self.S.append(smooth)

    def loft(self, st, sides, mats, cap0=False, cap1=False, phase=0.0, ref=(1, 0, 0)):
        """a tube through stations (centre, r_forward, r_side, {bone: weight}); band k uses mats[k]"""
        rings = []
        for k, (c, ru, rv, w) in enumerate(st):
            c = Vector(c)
            a = Vector(st[max(k - 1, 0)][0]); b = Vector(st[min(k + 1, len(st) - 1)][0])
            t = (b - a).normalized()
            r = Vector(ref)
            if abs(r.dot(t)) > 0.9:
                r = Vector((0, 0, 1))
            u = (r - t * r.dot(t)).normalized(); v = t.cross(u)
            if ru == 0 and rv == 0:
                rings.append([self._v(c, w)])
                continue
            ring = []
            for i in range(sides):
                ang = 2 * math.pi * i / sides + phase
                ring.append(self._v(c + u * (ru * math.cos(ang)) + v * (rv * math.sin(ang)), w))
            rings.append(ring)
        for k in range(len(rings) - 1):
            A, B = rings[k], rings[k + 1]
            for i in range(sides):
                j = (i + 1) % sides
                if len(B) == 1:
                    self.face((A[i], A[j], B[0]), mats[k], True)
                elif len(A) == 1:
                    self.face((A[0], B[j], B[i]), mats[k], True)
                else:
                    self.face((A[i], A[j], B[j], B[i]), mats[k], True)
        if cap0 and len(rings[0]) > 1:
            self.face(tuple(reversed(rings[0])), mats[0], False)
        if cap1 and len(rings[-1]) > 1:
            self.face(tuple(rings[-1]), mats[-1], False)

    def box(self, lo, hi, w, sw):
        b = [self._v((x, y, z), w) for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
        for f in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
            self.face(tuple(b[i] for i in f), sw, False)

    def quad_strip(self, rows, w, sw):
        for i in range(len(rows[0]) - 1):
            a, b = rows[0], rows[1]
            idx = [self._v(a[i], w), self._v(a[i + 1], w), self._v(b[i + 1], w), self._v(b[i], w)]
            self.face(tuple(idx), sw, True)

    def _v(self, p, w):
        self.V.append(tuple(p)); self.W.append(w)
        return len(self.V) - 1


# helmet: a full-face racing helmet, not a ball -- longer front to back than side to side, a flatter
# oval dome, the lower edge hanging lower at the back than at the chin, a wide visor band across the
# front and a chin bar that juts out under it (like the old code-made crew helmet and a Bell GP helmet)
HLM = dict(cx=0.0, a=0.150, b=0.124, p=2.4, zc=1.69, hz=0.145, vis=(1.665, 1.738))


def helmet_bottom(th):
    """the lower edge: highest under the ears, lower at the back of the neck and at the chin"""
    c = math.cos(th)
    return 1.600 - 0.038 * max(0.0, -c) - 0.018 * max(0.0, c) ** 2


def helmet_pt(th, z):
    """the shell at angle th round the head (0 = straight ahead, + to his left) and height z"""
    H = HLM
    zb = helmet_bottom(th)
    if z <= H['zc']:
        rf = 0.90 + 0.10 * max((z - zb) / (H['zc'] - zb), 0.0) ** 0.6     # the wall bulges out to the widest point
    else:
        rf = math.sqrt(max(0.0, 1 - ((z - H['zc']) / H['hz']) ** 2))      # the oval dome
    c, s_ = math.cos(th), math.sin(th)
    # the chin bar: below the visor the front of the shell juts forward
    chin = min(max((H['vis'][0] - z) / (H['vis'][0] - zb), 0.0), 1.0)
    rf *= 1 + 0.18 * max(0.0, c) ** 4 * chin
    k = (abs(c) ** H['p'] + abs(s_) ** H['p']) ** (-1 / H['p'])       # squared-off oval in plan
    return (H['cx'] + H['a'] * k * c * rf, H['b'] * k * s_ * rf, z)


def helmet(B):
    W = {'head': 1.0}
    N = 12
    H = HLM
    ths = [2 * math.pi * i / N for i in range(N)]
    zs = [None, 0.55, H['vis'][0], H['zc'], H['vis'][1]] + [H['zc'] + H['hz'] * f for f in (0.586, 0.828, 0.979)]
    rows = []
    for z in zs:
        if z is None or z < 1:                     # the wall rows follow the sloping lower edge
            u = 0.0 if z is None else z
            rows.append([helmet_pt(t, helmet_bottom(t) + u * (H['zc'] - helmet_bottom(t))) for t in ths])
        else:
            rows.append([helmet_pt(t, z) for t in ths])
    # bands, bottom to top: chin/wall, wall, visor, visor, brow, team stripe, crown, crown
    band = ['helmet', 'helmet', 'V', 'V', 'helmet', 'stripe', 'helmet']
    R = [[B._v(p, W) for p in r] for r in rows]
    for k in range(len(R) - 1):
        for i in range(N):
            j = (i + 1) % N
            mid = math.degrees(ths[i]) + 180.0 / N
            mid = (mid + 180) % 360 - 180
            m = band[k]
            if m == 'V':                           # the visor: a wide band across the front, flush with the shell
                m = 'visor' if abs(mid) <= 60 else 'helmet'
            B.face((R[k][i], R[k][j], R[k + 1][j], R[k + 1][i]), m, True)
    top = B._v((H['cx'] - 0.01, 0, H['zc'] + H['hz']), W)
    for i in range(N):
        B.face((R[-1][i], R[-1][(i + 1) % N], top), 'helmet', True)
    B.face(tuple(reversed(R[0])), 'balaclava', False)                  # underneath (the neck goes in here)


def make_body():
    B = Body()
    H, S, C = {'hips': 1.0}, {'spine': 1.0}, {'chest': 1.0}
    # torso: crotch to neck
    B.loft([((0, 0, 0.84), 0.10, 0.13, H), ((0, 0, 0.98), 0.12, 0.17, H), ((0, 0, 1.03), 0.121, 0.168, H),
            ((0, 0, 1.10), 0.118, 0.162, {'hips': 0.5, 'spine': 0.5}), ((0, 0, 1.20), 0.118, 0.160, S),
            ((0, 0, 1.38), 0.13, 0.19, {'spine': 0.35, 'chest': 0.65}), ((0, 0, 1.49), 0.11, 0.17, C),
            ((0, 0, 1.53), 0.06, 0.075, C), ((0, 0, 1.62), 0.05, 0.05, {'neck': 1.0})],
           8, ['suit', 'suit', 'belt', 'suit', 'suit', 'suit', 'suit', 'balaclava'], cap0=True)
    # chest panel and the team's shoulder flashes
    B.box((0.128, -0.075, 1.29), (0.142, 0.075, 1.39), C, 'panel')
    # helmet
    helmet(B)
    for s, sd in ((1, 'L'), (-1, 'R')):
        U, F_, Hd = {'upperarm.' + sd: 1.0}, {'forearm.' + sd: 1.0}, {'hand.' + sd: 1.0}
        # arm: shoulder flash, sleeve, cuff
        B.loft([((0, s * 0.19, 1.48), 0.068, 0.062, {'upperarm.' + sd: 0.85, 'chest': 0.15}),
                ((0, s * 0.205, 1.40), 0.062, 0.058, U),
                ((0, s * 0.23, 1.18), 0.052, 0.050, {'upperarm.' + sd: 0.5, 'forearm.' + sd: 0.5}),
                ((0.008, s * 0.238, 0.99), 0.044, 0.042, F_), ((0.01, s * 0.24, 0.945), 0.043, 0.041, F_)],
               6, ['accent', 'suit', 'suit', 'accent'], cap1=True)
        B.box((-0.03, s * 0.24 - 0.03, 0.83), (0.055, s * 0.24 + 0.03, 0.955), Hd, 'glove')
        # leg: thigh, knee, the reflective band on the shin, boot
        T_, Sh = {'thigh.' + sd: 1.0}, {'shin.' + sd: 1.0}
        B.loft([((0, s * 0.095, 0.95), 0.092, 0.088, T_), ((0, s * 0.10, 0.72), 0.082, 0.078, T_),
                ((0, s * 0.10, 0.51), 0.066, 0.064, {'thigh.' + sd: 0.5, 'shin.' + sd: 0.5}),
                ((0, s * 0.10, 0.36), 0.060, 0.058, Sh), ((0, s * 0.10, 0.31), 0.058, 0.056, Sh),
                ((0, s * 0.10, 0.11), 0.050, 0.048, Sh)],
               8, ['suit', 'suit', 'suit', 'reflect', 'suit'])
        B.box((-0.065, s * 0.10 - 0.05, 0.0), (0.20, s * 0.10 + 0.05, 0.125), {'foot.' + sd: 1.0}, 'boot')
    # wheel gun in the right hand (shrunk away inside the glove when he isn't using it)
    G = {'gun': 1.0}
    B.loft([((0.0, -0.245, 0.88), 0.036, 0.036, G), ((0.20, -0.245, 0.88), 0.036, 0.036, G)], 6, ['gun'],
           cap0=True, cap1=True, ref=(0, 0, 1))
    B.loft([((0.20, -0.245, 0.88), 0.024, 0.024, G), ((0.28, -0.245, 0.88), 0.022, 0.022, G)], 6, ['gundark'],
           cap1=True, ref=(0, 0, 1))
    B.box((0.02, -0.265, 0.79), (0.07, -0.225, 0.88), G, 'gundark')
    return B


# ---- Blender objects ---------------------------------------------------------------------------
def material(sw):
    name = 'FL_%s.CREW' % sw
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    c = PALETTE[sw]
    m.diffuse_color = (c[0], c[1], c[2], 1.0)
    return m


def clear():
    for o in list(bpy.data.objects):
        if o.name.startswith(NAME):
            bpy.data.objects.remove(o, do_unlink=True)
    for d in (bpy.data.meshes, bpy.data.armatures, bpy.data.actions):
        for b in list(d):
            if b.name.startswith(NAME) and b.users == 0:
                d.remove(b)


def build():
    clear()
    coll = bpy.data.collections.get('Pit crew') or bpy.data.collections.new('Pit crew')
    if coll.name not in bpy.context.scene.collection.children:
        bpy.context.scene.collection.children.link(coll)
    # armature
    arm = bpy.data.armatures.new(NAME + '_rig')
    rig = bpy.data.objects.new(NAME + '_rig', arm)
    rig.location = ORIGIN
    coll.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    for o in bpy.context.selected_objects:
        o.select_set(False)
    rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    for name, h, t, par in BONES:
        eb = arm.edit_bones.new(name)
        eb.head, eb.tail = h, t
        eb.roll = 0
        if par:
            eb.parent = arm.edit_bones[par]
            eb.use_connect = False
    bpy.ops.object.mode_set(mode='OBJECT')
    arm.display_type = 'STICK'
    rig.show_in_front = True
    # body
    B = make_body()
    me = bpy.data.meshes.new(NAME)
    me.from_pydata(B.V, [], B.F)
    me.update()
    for sw in B.mats:
        me.materials.append(material(sw))
    for p, mi, sm in zip(me.polygons, B.M, B.S):
        p.material_index = mi; p.use_smooth = sm
    ob = bpy.data.objects.new(NAME, me)
    coll.objects.link(ob)
    ob.parent = rig
    for name, *_ in BONES:
        ob.vertex_groups.new(name=name)
    for i, w in enumerate(B.W):
        for bn, wt in w.items():
            ob.vertex_groups[bn].add([i], wt, 'REPLACE')
    mod = ob.modifiers.new('Armature', 'ARMATURE')
    mod.object = rig
    key_all(rig)
    set_pose(rig, ANIMS['idle']()[0])
    return {'triangles': sum(len(f) - 2 for f in B.F), 'vertices': len(B.V), 'materials': B.mats}


def set_pose(rig, p):
    """p: bone -> (x, y, z) degrees about the armature's axes at the bone's head; 'off' moves the hips"""
    for pb in rig.pose.bones:
        rest = pb.bone.matrix_local
        h = HEAD[pb.name]
        L = Matrix.Identity(4)
        if pb.name in p and pb.name != 'gun':
            rx, ry, rz = p[pb.name]
            R = Euler((math.radians(rx), math.radians(ry), math.radians(rz)), 'XYZ').to_matrix().to_4x4()
            L = Matrix.Translation(h) @ R @ Matrix.Translation(-h)
        if pb.name == 'hips':
            L = Matrix.Translation(Vector(p.get('off', (0, 0, 0)))) @ L
        if pb.name == 'gun':
            g = max(p.get('gunscale', 0.0), 0.001)
            L = Matrix.Translation(h) @ Matrix.Diagonal((g, g, g, 1)) @ Matrix.Translation(-h)
        pb.matrix_basis = rest.inverted() @ L @ rest
    bpy.context.view_layer.update()


def key_all(rig):
    """put every pose on the timeline: motion a at frames 10a + 1, 3, 5, 7 (markers name them)"""
    sc = bpy.context.scene
    for m in list(sc.timeline_markers):
        if m.name.startswith('crew '):
            sc.timeline_markers.remove(m)
    rig.animation_data_clear()
    for a, (nm, fn) in enumerate(ANIMS.items()):
        sc.timeline_markers.new('crew ' + nm, frame=10 * a + 1)
        for k, p in enumerate(fn()):
            set_pose(rig, p)
            fr = 10 * a + 1 + 2 * k
            for pb in rig.pose.bones:
                pb.rotation_mode = 'QUATERNION'
                for path in ('location', 'rotation_quaternion', 'scale'):
                    pb.keyframe_insert(path, frame=fr)
    if rig.animation_data and rig.animation_data.action:
        rig.animation_data.action.name = NAME + '_motions'
    sc.frame_start, sc.frame_end = 1, 40


def export_json(path):
    rig = bpy.data.objects[NAME + '_rig']; ob = bpy.data.objects[NAME]
    mats = [m.name.split('.')[0][3:] for m in ob.data.materials]
    out = {'format': 'FL-crew-1', 'palette': PALETTE, 'materials': mats, 'anims': {}}
    tris = None
    for nm, fn in ANIMS.items():
        frames = []
        for p in fn():
            set_pose(rig, p)
            dg = bpy.context.evaluated_depsgraph_get()
            ev = ob.evaluated_get(dg)
            me = ev.to_mesh()
            me.calc_loop_triangles()
            P, N, M = [], [], []
            cn = me.corner_normals
            for t in me.loop_triangles:
                for li, vi in zip(t.loops, t.vertices):
                    P += [round(c, 5) for c in me.vertices[vi].co]
                    N += [round(c, 4) for c in cn[li].vector]
                M.append(t.material_index)
            ev.to_mesh_clear()
            if tris is None:
                tris = M
            assert M == tris
            frames.append({'p': P, 'n': N})
        out['anims'][nm] = frames
    out['m'] = tris
    set_pose(rig, ANIMS['idle']()[0])
    json.dump(out, open(path, 'w'))
    return {'triangles': len(tris), 'poses': sum(len(v) for v in out['anims'].values())}
