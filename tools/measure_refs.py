"""Measure the Unity AI reference cars so tools/gen_cars.py can build its code-made HD cars to the
same proportions: heights of the nose, bonnet / front fenders, roof, tail, rear wing, fin, and
where the cabin sits. Writes tools/ref_shapes.json (read by gen_cars.py --hd).

  python3 tools/measure_refs.py <PSP3DExport_Data/models dir>
"""
import os, sys, json, struct
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_models as BM


def load(fn):
    d = open(fn, 'rb').read(); vc, ic = struct.unpack('<ii', d[:8])
    v = np.frombuffer(d[8:8 + vc * 32], np.float32).reshape(-1, 8).copy()
    return v


def z_up(v):
    p = v[:, 0:3].copy(); v[:, 0] = p[:, 0]; v[:, 1] = -p[:, 2]; v[:, 2] = p[:, 1]; return v


def main():
    src = sys.argv[1]
    out = {}
    for cid, L in BM.CARS.items():
        fn = BM.CAR_FILE[cid]
        v = load(os.path.join(src, (fn[:-2] if fn.endswith('_z') else fn) + '.mesh'))
        axis = BM.LONG_AXIS.get(fn, '-x')
        if fn.endswith('_z'):
            v = z_up(v); axis = '-y'
        v = BM.to_game_axes(v, axis)
        p = v[:, :3]; mn, mx = p.min(0), p.max(0)
        s = L / (mx[0] - mn[0]); p = (p - (mn + mx) / 2) * s; p[:, 2] -= p[:, 2].min()
        hw = float(np.percentile(np.abs(p[:, 1]), 99))
        def top(x0, x1, y0, y1, q=96):
            m = (p[:, 0] >= x0) & (p[:, 0] < x1) & (np.abs(p[:, 1]) >= y0) & (np.abs(p[:, 1]) < y1)
            return float(np.percentile(p[m, 2], q)) if m.sum() > 8 else None
        xf, xr = L / 2, -L / 2
        prof = [top(x, x + 0.1, 0, 0.35) for x in np.arange(xr, xf, 0.1)]
        r = dict(L=L, halfWidth=hw)
        r['nose'] = top(xf - 0.25, xf, 0, hw * 0.6)
        r['front_fender'] = top(xf * 0.35, xf * 0.75, hw * 0.55, hw)
        r['rear_fender'] = top(xr * 0.75, xr * 0.35, hw * 0.55, hw)
        mid = [(x, z) for x, z in zip(np.arange(xr, xf, 0.1), prof) if z is not None and -L * 0.30 < x < L * 0.25]
        zs = np.array([z for _, z in mid]); xs = np.array([x for x, _ in mid])
        r['roof'] = float(zs.max()); top_x = xs[zs >= zs.max() - 0.04]
        r['roof_x'] = float(top_x.mean())
        r['tail'] = top(xr, xr + 0.25, 0, hw * 0.6, q=60)
        r['wing'] = top(xr, xr + 0.6, 0, hw, q=99.5)
        r['fin'] = top(xr + 0.4, -0.6, 0, 0.06, q=99)
        r['front_axle_area'] = top(xf * 0.45, xf * 0.65, 0, hw * 0.45)
        out[cid] = {k: (round(v, 3) if isinstance(v, float) else v) for k, v in r.items()}
        print(cid.ljust(18), ' '.join(f"{k}={out[cid][k]}" for k in ('nose', 'front_fender', 'rear_fender', 'roof', 'roof_x', 'tail', 'wing', 'fin')))
    json.dump(out, open(os.path.join(HERE, 'ref_shapes.json'), 'w'), indent=1)


if __name__ == '__main__':
    main()
