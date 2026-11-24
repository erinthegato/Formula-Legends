"""Tiny software renderer for checking .mesh files (textured, z-buffered, one light).
usage: preview.py out.png W H mesh.mesh tex.png [yaw pitch] [mesh2 tex2 ...]
Models are Z-up as exported from Unity's FBX import."""
import sys, math
import numpy as np
from PIL import Image


def load_mesh(fn):
    d = open(fn, 'rb').read()
    vc, ic = np.frombuffer(d[:8], np.int32)
    v = np.frombuffer(d[8:8 + vc * 32], np.float32).reshape(-1, 8)
    i = np.frombuffer(d[8 + vc * 32:8 + vc * 32 + ic * 4], np.int32).reshape(-1, 3)
    return v, i


def render(meshes, W=400, H=300, yaw=35, pitch=20, dist=2.2, fov=40, bg=(70, 80, 95), centre=None):
    img = np.zeros((H, W, 3), np.float32); img[:] = np.array(bg) / 255
    zb = np.full((H, W), 1e9, np.float32)
    allp = np.concatenate([m[0][:, :3] for m in meshes])
    c = (allp.min(0) + allp.max(0)) / 2 if centre is None else np.array(centre)
    rad = np.linalg.norm(allp.max(0) - allp.min(0)) / 2
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    # camera looks at c from direction (yaw around Z, pitch up)
    fwd = np.array([-cy * cp, -sy * cp, -sp])
    right = np.cross(fwd, [0, 0, 1]); right /= np.linalg.norm(right)
    up = np.cross(right, fwd)
    eye = c - fwd * rad * dist
    f = 1 / math.tan(math.radians(fov) / 2)
    light = np.array([0.4, 0.3, 0.85]); light /= np.linalg.norm(light)
    for v, idx, tex in meshes:
        p = v[:, :3] - eye
        x = p @ right; y = p @ up; z = p @ fwd
        sx = W / 2 + x / z * f * H / 2; syy = H / 2 - y / z * f * H / 2
        T = np.asarray(tex, np.float32) / 255 if tex is not None else None
        th, tw = (T.shape[0], T.shape[1]) if T is not None else (1, 1)
        for tri in idx:
            a, b, cc = tri
            if z[a] < 0.01 or z[b] < 0.01 or z[cc] < 0.01: continue
            xs = np.array([sx[a], sx[b], sx[cc]]); ys = np.array([syy[a], syy[b], syy[cc]])
            x0, x1 = int(max(0, xs.min())), int(min(W - 1, xs.max() + 1))
            y0, y1 = int(max(0, ys.min())), int(min(H - 1, ys.max() + 1))
            if x0 > x1 or y0 > y1: continue
            den = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
            if abs(den) < 1e-9: continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            l0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / den
            l1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / den
            l2 = 1 - l0 - l1
            m = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            if not m.any(): continue
            zz = l0 * z[a] + l1 * z[b] + l2 * z[cc]
            reg = zb[y0:y1 + 1, x0:x1 + 1]
            m &= zz < reg
            if not m.any(): continue
            n = np.cross(v[b, :3] - v[a, :3], v[cc, :3] - v[a, :3]); nl = np.linalg.norm(n)
            n = n / nl if nl > 0 else n
            if np.dot(n, fwd) > 0: n = -n
            shade = 0.45 + 0.55 * max(0.0, float(np.dot(n, light)))
            if T is not None:
                # perspective-correct uv
                w0, w1, w2 = l0 / z[a], l1 / z[b], l2 / z[cc]; ws = w0 + w1 + w2
                u = (w0 * v[a, 6] + w1 * v[b, 6] + w2 * v[cc, 6]) / ws
                vv = (w0 * v[a, 7] + w1 * v[b, 7] + w2 * v[cc, 7]) / ws
                tx = np.clip((u % 1) * tw, 0, tw - 1).astype(int)
                ty = np.clip((1 - (vv % 1)) * th, 0, th - 1).astype(int)
                col = T[ty, tx, :3]
            else:
                col = np.ones(gx.shape + (3,)) * 0.8
            reg[m] = zz[m]
            img[y0:y1 + 1, x0:x1 + 1][m] = col[m] * shade
    return Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))


if __name__ == '__main__':
    out, W, H = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    args = sys.argv[4:]
    yaw, pitch = 35, 20
    meshes = []
    i = 0
    while i < len(args):
        if args[i] == '--view':
            yaw, pitch = float(args[i + 1]), float(args[i + 2]); i += 3; continue
        v, idx = load_mesh(args[i])
        tex = Image.open(args[i + 1]).convert('RGB') if args[i + 1] != '-' else None
        meshes.append((v, idx, tex)); i += 2
    render(meshes, W, H, yaw, pitch).save(out)
