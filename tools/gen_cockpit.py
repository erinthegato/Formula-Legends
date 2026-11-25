"""Formula Legends -- the steering wheels and the driver made in Blender (tools/blender/cockpit_parts.py)
as game models.

    python3 tools/gen_cockpit.py [preview_dir]

Reads tools/blender/cockpit_parts.json and writes, to HD/models:
  steering_<era>.emm   one per era (90s, v8, hybrid, ge), the gloves on the grips
  driver.emm           two levels of detail; palette entries 1-4 are the team's colours
                       (1 suit, 2 flashes, 3 helmet, 4 helmet stripe), as the pit crew
                       (the game swaps them per team: Renderer::teamTexture)
  wheel_<era>.emm      the road wheel, three levels of detail; entries 1-4 the sidewall stripe
                       (the game paints it in the tyre compound's colour)
  flap_<era>.emm       the rear wing's top flap; entries 1-4 the team's main / second colour
Every part is flat colours: one 8x8 cell per material on a 64 px texture.
"""
import json, os, struct, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from build_models import swizzle                 # noqa: E402

SRC = os.path.join(HERE, 'blender', 'cockpit_parts.json')
ERA = {'1998': '90s', '2010': 'v8', '2016': 'hybrid', '2025': 'ge'}    # ('1998': the 1980 parts, see main)
ROLES = ('suit', 'accent', 'helmet', 'stripe')   # palette entries 1-4 (team colours)
# the wheels' sidewall stripe takes all four role slots (the game paints it in the compound's colour);
# the flap: 1 / 3 the team's main colour, 2 / 4 its second colour
WHEEL_ROLES = ('band', 'band_b', 'band_c', 'band_d')
FLAP_ROLES = ('flap', 'tip', 'flap_b', 'tip_b')
TEX = 64
KEY = np.array([0.45, 0.35, 0.82]); KEY /= np.linalg.norm(KEY)


def atlas(names, palette):
    """palette entries in the order given (roles first), one 8x8 cell each"""
    pal = [0xFF808080] * 256
    pix = np.zeros((TEX, TEX), np.uint8)
    cell = {}
    for k, m in enumerate(names):
        r, g, b = (int(round(max(0, min(1, c)) * 255)) for c in palette[m.split('_')[0]])   # (band_b: a spare role slot)
        pal[k + 1] = 0xFF000000 | (b << 16) | (g << 8) | r
        cx, cy = (k % 8) * 8, (k // 8) * 8
        pix[cy:cy + 8, cx:cx + 8] = k + 1
        cell[m] = ((cx + 4) / TEX, (cy + 4) / TEX)
    return pal, swizzle(pix.reshape(-1), TEX, TEX), cell


def records(part, cell, centre, scale, light_k=1.0):
    P = np.array(part['p'], np.float64).reshape(-1, 3)
    N = np.array(part['n'], np.float64).reshape(-1, 3)
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-6)
    light = np.clip((0.52 + 0.28 * (N[:, 2] * 0.5 + 0.5) + 0.30 * np.clip(N @ KEY, 0, 1)) * light_k, 0, 1)
    g = np.round(light * 255).astype(np.uint32)
    uv = np.array([cell[part['mats'][mi]] for mi in part['m'] for _ in range(3)])
    q = np.clip(np.round((P - centre) / scale * 32767), -32767, 32767).astype(np.int16)
    rec = np.zeros(len(P), dtype=[('u', '<u2'), ('v', '<u2'), ('c', '<u4'), ('x', '<i2'), ('y', '<i2'), ('z', '<i2'), ('pad', '<i2')])
    rec['u'] = np.round(uv[:, 0] * 32767); rec['v'] = np.round(uv[:, 1] * 32767)
    rec['c'] = 0xFF000000 | (g << 16) | (g << 8) | g
    rec['x'] = q[:, 0]; rec['y'] = q[:, 1]; rec['z'] = q[:, 2]
    return rec


def write(path, parts, palette, roles=()):
    names = list(roles) + sorted({m for p in parts for m in p['mats']} - set(roles))
    pal, pix, cell = atlas(names, palette)
    allp = np.concatenate([np.array(p['p']).reshape(-1, 3) for p in parts])
    mn, mx = allp.min(0), allp.max(0)
    centre = (mn + mx) / 2; scale = np.maximum((mx - mn) / 2, 1e-4) * 1.001
    with open(path, 'wb') as f:
        f.write(b'EMM1'); f.write(struct.pack('<I', len(parts)))
        f.write(struct.pack('<3f', *scale)); f.write(struct.pack('<3f', *centre))
        for i, p in enumerate(parts):
            rec = records(p, cell, centre, scale)
            f.write(struct.pack('<IIIf', len(rec), TEX if i == 0 else 0, 0, 0.0))   # level 1+ share level 0's texture
            if i == 0:
                f.write(struct.pack('<256I', *pal)); f.write(pix.tobytes())
            f.write(rec.tobytes())
    return os.path.getsize(path), [p['tris'] for p in parts], [round(float(x), 3) for x in (mx - mn)]


def main(prev=None):
    d = json.load(open(SRC))
    # the 1980 era (internal id 90s) has its own parts, built in code (tools/gen_parts_1980.py)
    p80 = json.load(open(os.path.join(HERE, 'blender', 'parts_1980.json')))
    d['palette'].update(p80['palette'])
    d['steering']['1998'] = p80['steering']
    d['wheels']['1998'] = p80['wheels']
    d['flaps']['1998'] = p80['flap']
    for folder in ('HD/models',):            # the F1 game's models are Vita (HD) only
        out = os.path.join(ROOT, folder)
        for year, era in ERA.items():
            size, tris, dims = write(os.path.join(out, f'steering_{era}.emm'), [d['steering'][year]], d['palette'])
            if folder == 'HD/models':
                print(f'steering {year}: {tris[0]} triangles, {dims} m, {size / 1024:.0f} KB')
        size, tris, dims = write(os.path.join(out, 'driver.emm'), d['driver'], d['palette'], ROLES)
        print(f'driver: triangles {tris}, {dims} m, {size / 1024:.0f} KB')
        size, tris, dims = write(os.path.join(out, 'grid_girl.emm'), [p80['grid_girl']], d['palette'], ROLES)
        print(f'grid girl: triangles {tris}, {dims} m, {size / 1024:.0f} KB')
        size, tris, dims = write(os.path.join(out, 'driver_90s.emm'), p80['driver'], d['palette'], ROLES)
        print(f'driver 1980 (no HANS): triangles {tris}, {size / 1024:.0f} KB')
        for year, era in ERA.items():
            size, tris, dims = write(os.path.join(out, f'wheel_{era}.emm'), d['wheels'][year], d['palette'], WHEEL_ROLES)
            print(f'wheel {year}: triangles {tris}, {dims} m, {size / 1024:.0f} KB')
            size, tris, dims = write(os.path.join(out, f'flap_{era}.emm'), [d['flaps'][year]], d['palette'], FLAP_ROLES)
            print(f'flap {year}: triangles {tris}, {dims} m, {size / 1024:.0f} KB')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else None)
