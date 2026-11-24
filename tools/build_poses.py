"""Build the pit crew pose models (crew_<anim>_<frame>.emm) so they can blend smoothly.

  python3 tools/build_poses.py <src_models dir> <out dir>

Every pose was baked out of Unity from the same rigged body, so the source poses share the
same vertices (same count, same order). Here we:
  1. simplify + texture ONE pose (crew_idle_0) with tools/meshtool/bake;
  2. tie every simplified vertex to its closest vertex on that source pose;
  3. for every other pose, take the same source vertices -> all 16 models have the same
     triangles, the same texture and the same vertex order, only the positions move;
  4. use one shared scale, the body centred on the hips, feet on the ground per animation.
The game can then mix two poses vertex by vertex (see Renderer::drawPerson).
"""
import os, sys, struct, subprocess
import numpy as np
from PIL import Image
from scipy.spatial import cKDTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_models import BAKE, ROT, read_mesh, quantise, swizzle

ANIMS = ('idle', 'wheelgun', 'push', 'celebrate')
FRAMES = 4
TRIS, TEX = int(os.environ.get("CREW_TRIS", 380)), int(os.environ.get("CREW_TEX", 128))   # Vita: 1600, 256
HEIGHT = 1.8   # metres per source unit


def read_full(fn):
    d = open(fn, 'rb').read()
    vc, ic = struct.unpack('<ii', d[:8])
    return np.frombuffer(d[8:8 + vc * 32], np.float32).reshape(-1, 8).copy()


def main():
    src, out = sys.argv[1], sys.argv[2]
    work = os.path.join(out, '_work'); os.makedirs(work, exist_ok=True)
    img = Image.open(os.path.join(src, 'pit_crew_rig.png')).convert('RGB')
    raw = os.path.join(work, 'crew_src.raw'); open(raw, 'wb').write(img.tobytes())
    base = os.path.join(src, 'crew_idle_0.mesh')
    om = os.path.join(work, 'crew_base.mesh'); ot = os.path.join(work, 'crew_base.raw')
    r = subprocess.run([BAKE, base, raw, str(img.size[0]), str(img.size[1]), om, ot, str(TEX), str(TRIS)], capture_output=True, text=True)
    if r.returncode: raise SystemExit(r.stderr)
    print(r.stderr.strip())
    low = read_mesh(om)                              # simplified idle_0 (3 corners per triangle)
    tex = Image.frombytes('RGB', (TEX, TEX), open(ot, 'rb').read())
    pal, pix = quantise(tex)
    sw = swizzle(pix.reshape(-1), TEX, TEX)

    srcs = {(a, f): read_full(os.path.join(src, f'crew_{a}_{f}.mesh')) for a in ANIMS for f in range(FRAMES)}
    ref = srcs[('idle', 0)]
    _, near = cKDTree(ref[:, :3]).query(low[:, :3])  # simplified vertex -> source vertex
    M = np.array(ROT['crew_idle_0'], np.float32)

    poses = {}
    for k, v in srcs.items():
        assert len(v) == len(ref), k
        p = v[near, 0:3] @ M.T
        n = v[near, 3:6] @ M.T
        poses[k] = (p * HEIGHT, n)
    allp = np.concatenate([p for p, _ in poses.values()])
    half = np.abs(allp).max(0) * 1.02
    half[2] = (allp[:, 2].max() - allp[:, 2].min()) * 1.02
    scale = [float(half[0]), float(half[1]), float(half[2] / 2)]

    uv = np.clip(np.round(low[:, 6:8] * np.array([1, -1]) + np.array([0, 1]), 6), 0, 1)
    uv16 = np.clip(np.round(uv * 32767), 0, 65535).astype(np.uint16)
    key = np.array([0.45, 0.35, 0.82]); key /= np.linalg.norm(key)
    for a in ANIMS:
        ground = min(poses[(a, f)][0][:, 2].min() for f in range(FRAMES))
        for f in range(FRAMES):
            p, n = poses[(a, f)]
            p = p.copy(); p[:, 2] -= ground + half[2] / 2     # stored centred; offset lifts it back
            q = np.clip(np.round(p / np.array(scale) * 32767), -32767, 32767).astype(np.int16)
            n = n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-6)
            light = 0.50 + 0.30 * (n[:, 2] * 0.5 + 0.5) + 0.30 * np.clip(n @ key, 0, 1)
            g = np.clip(np.round(light * 255), 0, 255).astype(np.uint32)
            rec = np.zeros(len(p), dtype=[('u', '<u2'), ('v', '<u2'), ('c', '<u4'), ('x', '<i2'), ('y', '<i2'), ('z', '<i2'), ('pad', '<i2')])
            rec['u'] = uv16[:, 0]; rec['v'] = uv16[:, 1]; rec['c'] = 0xFF000000 | (g << 16) | (g << 8) | g
            rec['x'] = q[:, 0]; rec['y'] = q[:, 1]; rec['z'] = q[:, 2]
            with open(os.path.join(out, f'crew_{a}_{f}.emm'), 'wb') as fo:
                fo.write(b'EMM1'); fo.write(struct.pack('<I', 1))
                fo.write(struct.pack('<3f', *scale)); fo.write(struct.pack('<3f', 0.0, 0.0, float(half[2] / 2)))
                first = (a, f) == ('idle', 0)   # only the first pose carries the texture; the game shares it
                fo.write(struct.pack('<IIII', len(p), TEX if first else 0, 0, 0))
                if first: fo.write(struct.pack('<256I', *pal)); fo.write(sw.tobytes())
                fo.write(rec.tobytes())
    print('poses:', len(poses), 'verts', len(low), 'scale', scale)


if __name__ == '__main__':
    main()
