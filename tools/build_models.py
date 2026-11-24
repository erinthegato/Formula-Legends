"""Build the PSP model packs (.emm) from the models exported out of the Unity project.

  python3 tools/build_models.py <src_models dir> <out dir>

For every model: bake 3 levels of detail (near / mid / far) with tools/meshtool/bake,
quantise each baked texture to 256 colours, and write one .emm file:

  'EMM1' u32 lodCount, f32 scale[3], f32 offset[3]
  per LOD: u32 vertCount, u32 texSize, u32 pad[2], u32 palette[256] (ABGR), u8 pixels[texSize*texSize] (swizzled),
           vertCount * Vertex { u16 u, v; u32 colour; s16 x, y, z, pad }   (16 bytes, GU 16-bit layout)
  The colour is light baked from the normal (sky above, darker underneath, a warm key light from the
  front-left), so the PSP needs no real-time lighting.

Positions are stored -32767..32767 and the game multiplies by `scale` and adds `offset` (metres).
Model space for the game: +X forward, +Y left, +Z up, ground at z = 0.
"""
import os, sys, struct, subprocess, json, math
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
BAKE = os.path.join(HERE, 'meshtool', 'bake')

# name -> (length in metres, forward axis in the Unity export, LOD tris, LOD texture sizes)
CAR_LOD = [(1200, 256), (420, 128), (130, 64)]
LONG_AXIS = {'ai_car_ardenza296': '-y', 'ai_car_ardenza499_z': '+x'}   # everything else: front is -x
# ai_car_ardenza499 comes from a glTF file (Y-up); tools/build_all.sh turns it Z-up into *_z.mesh first.
HAS_WHEELS = {'ardenza499'}   # the other AI cars were generated without wheels
CARS = {
    # id: (length m, class)
    'ardenza499': 5.05, 'kaizen10': 5.10, 'regalvr8': 5.10, 'isarmh8': 5.10, 'origingx01': 5.05,
    'montagne26': 5.00, 'lionne9x9': 5.05, 'ashcombevalkyr': 5.00,
    'ardenza296': 4.60, 'zuffen911': 4.60, 'isarm4': 4.85, 'sterneamg': 4.75, 'ashcombevanguard': 4.70,
    'kentuckyz6': 4.75, 'woking720': 4.60, 'dearbornstallion': 4.85, 'kotobukircg': 4.75,
}
# Which exported file is each car (some have a " 1" copy in the Unity project).
CAR_FILE = {k: 'ai_car_' + k for k in CARS}
CAR_FILE['dearbornstallion'] = 'ai_car_dearbornstallion_1'
CAR_FILE['isarmh8'] = 'ai_car_isarmh8_1'
CAR_FILE['ardenza499'] = 'ai_car_ardenza499_z'

PROPS = {
    # name: (exported file, real size in metres along its biggest axis, LODs)
    'grandstand': ('ai_prop_grandstand_1', 30.0, [(600, 128), (120, 64)]),
    'tree_pine': ('ai_prop_tree_pine_1', 14.0, [(160, 64), (40, 32)]),
    'tree_round': ('ai_prop_tree_round_1', 9.0, [(160, 64), (40, 32)]),
    'tyre_stack': ('ai_prop_tyre_stack_1', 1.6, [(120, 64), (30, 32)]),
    'billboard': ('ai_prop_billboard_1', 8.0, [(120, 128), (30, 64)]),
    'marshal_post': ('ai_prop_marshal_post_1', 3.5, [(200, 64), (50, 32)]),
    'fuel_rig': ('ai_prop_fuel_rig_1', 2.2, [(200, 64)]),
    'pit_crew': ('pit_crew', 1.8, [(450, 128)]),
}
# Rotations (rows = new x, y, z in terms of old x, y, z) that stand the AI parts the game's way up:
# +X forward, +Y left, +Z up. Found by measuring each mesh (thinnest / longest axes) and looking at it.
ROT = {
    **{f'crew_{a}_{f}': [[0, 0, -1], [-1, 0, 0], [0, 1, 0]] for a in ('idle', 'wheelgun', 'push', 'celebrate') for f in range(4)},   # Unity y-up, the rig faces -z -> +X forward, Z up (a turn, no mirror)
    'steering_wheel': [[0, 0, -1], [0, 1, 0], [1, 0, 0]],   # lay-flat wheel -> upright, face towards the driver
    'race_wheel': [[0, 1, 0], [-1, 0, 0], [0, 0, 1]],       # axle along the car's Y axis, rim face to +Y
    'driver_arms': [[0, 1, 0], [-1, 0, 0], [0, 0, 1]],      # arms reach forward (+X)
    'cockpit_hypercar': [[0, -1, 0], [1, 0, 0], [0, 0, 1]], # generated facing -Y: turn to face +X
    'cockpit_gt3': [[0, -1, 0], [1, 0, 0], [0, 0, 1]],
}
COCKPIT = {
    # name: (file, size m, LODs) -- one LOD, more detail: it fills the screen
    'cockpit_hypercar': ('cockpit_hypercar', 1.95, [(1600, 256)]),
    'cockpit_gt3': ('cockpit_gt3', 1.95, [(1600, 256)]),
    'steering_wheel': ('steering_wheel', 0.30, [(700, 128)]),
    'driver_arms': ('driver_arms', 0.72, [(900, 128)]),
    'race_wheel': ('race_wheel', 0.72, [(160, 64), (48, 32), (20, 32)]),
}


def read_mesh(fn):
    d = open(fn, 'rb').read()
    vc, ic = struct.unpack('<ii', d[:8])
    v = np.frombuffer(d[8:8 + vc * 32], np.float32).reshape(-1, 8).copy()
    return v


def swizzle(pix, w, h):
    """PSP texture swizzle for 8-bit textures (16-byte x 8-row blocks)."""
    out = np.zeros(w * h, np.uint8)
    src = pix.reshape(h, w)
    bw = w // 16
    o = 0
    for by in range(h // 8):
        for bx in range(bw):
            blk = src[by * 8:by * 8 + 8, bx * 16:bx * 16 + 16]
            out[o:o + 128] = blk.reshape(-1)
            o += 128
    return out


def quantise(img):
    q = img.convert('RGB').quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:768]
    pal += [0] * (768 - len(pal))
    palette = [0xFF000000 | (pal[i * 3 + 2] << 16) | (pal[i * 3 + 1] << 8) | pal[i * 3] for i in range(256)]
    return palette, np.asarray(q, np.uint8)


def to_game_axes(v, long_axis):
    """Unity export is Z-up with the car's front at -X. Game: +X forward, +Y left, +Z up."""
    p = v[:, 0:3].copy(); n = v[:, 3:6].copy()
    if long_axis == '+x':
        return v
    if long_axis == '-y':     # front at -Y: rotate so -Y -> -X first
        p = np.stack([p[:, 1], -p[:, 0], p[:, 2]], 1); n = np.stack([n[:, 1], -n[:, 0], n[:, 2]], 1)
    # front -X -> +X: rotate 180 deg about Z
    p[:, 0] *= -1; p[:, 1] *= -1; n[:, 0] *= -1; n[:, 1] *= -1
    v[:, 0:3] = p; v[:, 3:6] = n
    return v


def build(name, src_mesh, src_png, lods, size_m, out_dir, work, long_axis=None, ground=True, size_axis='max'):
    img = Image.open(src_png).convert('RGB')
    raw = os.path.join(work, name + '_src.raw')
    open(raw, 'wb').write(img.tobytes())
    W, H = img.size
    lod_data = []
    bbox = None
    for li, (tris, ts) in enumerate(lods):
        om = os.path.join(work, f'{name}_l{li}.mesh'); ot = os.path.join(work, f'{name}_l{li}.raw')
        r = subprocess.run([BAKE, src_mesh, raw, str(W), str(H), om, ot, str(ts), str(tris)], capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(r.stderr)
        print('  ', r.stderr.strip())
        v = read_mesh(om)
        if long_axis is not None:
            v = to_game_axes(v, long_axis)
        if name in ROT:
            M = np.array(ROT[name], np.float32)
            v[:, 0:3] = v[:, 0:3] @ M.T; v[:, 3:6] = v[:, 3:6] @ M.T
        tex = Image.frombytes('RGB', (ts, ts), open(ot, 'rb').read())
        tex.save(os.path.join(work, f'{name}_l{li}.png'))
        if bbox is None:
            bbox = (v[:, :3].min(0), v[:, :3].max(0))
        lod_data.append((v, tex, ts))
    mn, mx = bbox
    ext = mx - mn
    if size_axis == 'fixed':
        s = size_m                       # metres per source unit (poses keep their real height)
    elif size_axis == 'max':
        s = size_m / float(ext.max())
    else:
        s = size_m / float(ext[size_axis])
    centre = (mn + mx) / 2
    # metres: (p - centre) * s, then lift so the lowest point sits on the ground
    lift = -(mn[2] - centre[2]) * s if ground else 0.0
    half = ext * s / 2
    scale = [float(max(half[i], 1e-4)) for i in range(3)]
    offset = [0.0, 0.0, float(lift)]
    with open(os.path.join(out_dir, name + '.emm'), 'wb') as f:
        f.write(b'EMM1'); f.write(struct.pack('<I', len(lod_data)))
        f.write(struct.pack('<3f', *scale)); f.write(struct.pack('<3f', *offset))
        for v, tex, ts in lod_data:
            pal, pix = quantise(tex)
            f.write(struct.pack('<IIII', len(v), ts, 0, 0))   # 16 bytes: palette and pixels stay 16-byte aligned for the PSP
            f.write(struct.pack('<256I', *pal))
            f.write(swizzle(pix.reshape(-1), ts, ts).tobytes())
            p = (v[:, :3] - centre) * s
            q = np.clip(np.round(p / np.array(scale) * 32767), -32767, 32767).astype(np.int16)
            uv = np.clip(np.round(v[:, 6:8] * np.array([1, -1]) + np.array([0, 1]), 6), 0, 1)  # v flipped: texture row 0 at top
            uv16 = np.clip(np.round(uv * 32767), 0, 65535).astype(np.uint16)
            n = v[:, 3:6] / np.maximum(np.linalg.norm(v[:, 3:6], axis=1, keepdims=True), 1e-6)
            key = np.array([0.45, 0.35, 0.82]); key /= np.linalg.norm(key)
            light = 0.50 + 0.30 * (n[:, 2] * 0.5 + 0.5) + 0.30 * np.clip(n @ key, 0, 1)
            g = np.clip(np.round(light * 255), 0, 255).astype(np.uint32)
            col = 0xFF000000 | (g << 16) | (g << 8) | g
            rec = np.zeros(len(v), dtype=[('u', '<u2'), ('v', '<u2'), ('c', '<u4'), ('x', '<i2'), ('y', '<i2'), ('z', '<i2'), ('pad', '<i2')])
            rec['u'] = uv16[:, 0]; rec['v'] = uv16[:, 1]; rec['c'] = col
            rec['x'] = q[:, 0]; rec['y'] = q[:, 1]; rec['z'] = q[:, 2]
            f.write(rec.tobytes())
    return dict(scale=scale, offset=offset, size=[float(e * s) for e in ext])


def main():
    src, out = sys.argv[1], sys.argv[2]
    only = set(sys.argv[3:])
    work = os.path.join(out, '_work')
    os.makedirs(work, exist_ok=True)
    info = {}
    jobs = []
    for cid, L in CARS.items():
        fn = CAR_FILE[cid]
        jobs.append(('car_' + cid, fn, CAR_LOD, L, LONG_AXIS.get(fn, '-x'), True, 0))
    for name, (fn, size, lods) in PROPS.items():
        jobs.append((name, fn, lods, size, None, True, 'max'))
    for name, (fn, size, lods) in COCKPIT.items():
        jobs.append((name, fn, lods, size, None, False, 'max'))
    for an in ('idle', 'wheelgun', 'push', 'celebrate'):
        for f in range(4):
            # baked from the rigged crew + Unity AI motions (1 unit = 1.8 m standing height)
            jobs.append((f'crew_{an}_{f}', f'crew_{an}_{f}', [(380, 128)], 1.8, None, True, 'fixed'))
    for name, fn, lods, size, axis, ground, sa in jobs:
        if only and name not in only:
            continue
        mesh = os.path.join(src, fn + '.mesh'); png = os.path.join(src, (fn[:-2] if fn.endswith('_z') else fn) + '.png')
        if fn.startswith('crew_'): png = os.path.join(src, 'pit_crew_rig.png')
        if not os.path.exists(mesh):
            print('missing', fn); continue
        print(name)
        info[name] = build(name, mesh, png, lods, size, out, work, long_axis=axis, ground=ground, size_axis=sa)
    old = {}
    ip = os.path.join(out, 'models.json')
    if os.path.exists(ip):
        old = json.load(open(ip))
    old.update(info)
    json.dump(old, open(ip, 'w'), indent=1)


if __name__ == '__main__':
    main()
