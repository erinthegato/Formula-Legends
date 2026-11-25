"""Formula Legends -- the trackside objects made in Blender (tools/blender/trackside.py) as game models.

    python3 tools/gen_trackside.py [preview_dir]

Reads tools/blender/props/<name>.json and writes HD/models/<name>.emm (two LODs). Every object is
coloured from swatches only, so each gets one 64x64 texture (8x8 cells of flat colour): about 5 KB
instead of the 300 KB of a painted 512x512 atlas.
"""
import json, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gen_props import Atlas, Kit, write_emm, OUT      # noqa: E402

PROPS = ('gantry', 'bleacher', 'tent', 'flagpanel', 'crane', 'tecpro')
SRC = os.path.join(HERE, 'blender', 'props')


SHIRTS = [(200, 40, 35), (35, 80, 180), (240, 210, 50), (40, 150, 60), (235, 235, 230), (30, 30, 35), (230, 120, 30), (150, 40, 140)]
SKIN = [(225, 180, 150), (190, 140, 105), (130, 90, 65), (235, 200, 170)]


def crowd(a):
    """a 64 x 8 pixel strip of fans: a head over a shirt every two pixels, seats showing between"""
    x0, y0, x1, y1 = [int(v) for v in a.add('crowd', 8, 1)]
    rng = np.random.RandomState(7)
    px = a.img.load()
    for x in range(x0 - 1, x1 + 1):
        for y in range(y0 - 1, y1 + 1):
            px[x, y] = (70, 70, 74)                                        # seats
        if (x - x0) % 2 == 0 and rng.rand() < 0.85:
            shirt, skin = SHIRTS[rng.randint(len(SHIRTS))], SKIN[rng.randint(len(SKIN))]
            top = y0 - 1 + rng.randint(0, 2)
            for y in range(top, y1 + 1):
                px[x, y] = skin if y < top + 2 else shirt


def build(name, prev=None):
    d = json.load(open(os.path.join(SRC, name + '.json')))
    a = Atlas(64, 8)
    used = {m.split('_')[0] for m in d['materials']}
    if 'crowd' in used:
        crowd(a)
    for sw, col in d['palette'].items():
        if sw in used and sw != 'crowd':
            a.solid(sw, col)
    kits = []
    for lod in d['lods']:
        k = Kit()
        P = np.array(lod['p'], float).reshape(-1, 3, 3)
        N = np.array(lod['n'], float).reshape(-1, 3, 3)
        for i in range(len(P)):
            sw, _, shade = d['materials'][lod['m'][i]].partition('_')
            u0, v0, u1, v1 = a.rects[sw]
            if sw == 'crowd':      # people along the row: u follows the strip's length, v its height
                y0, y1 = -4.7, 4.7                                 # the crowd strip spans y = +-(W - 0.3)
                row = int(round(-(P[i, :, 0].mean() + 0.45) / 0.9))
                uv = []
                for x, y, z in P[i]:
                    f = (y - y0) / (y1 - y0)
                    if row % 2: f = 1 - f                         # rows don't line up
                    zb = 0.45 + row * 0.55
                    g = 1 - min(max((z - zb) / 0.55, 0), 1)       # top of the strip = heads
                    uv.append((u0 + f * (u1 - u0), v0 + g * (v1 - v0)))
            else:
                c = ((u0 + u1) / 2, (v0 + v1) / 2)                # flat colour: the middle of its cell
                uv = [c, c, c]
            k._add(P[i], uv, int(shade) / 100.0 if shade else 1.0, N[i])
        kits.append(k)
    n, size = write_emm(os.path.join(OUT, name + '.emm'), [(kits[0], a.img, 64), (kits[1], a.img, 64)])
    kb = os.path.getsize(os.path.join(OUT, name + '.emm')) / 1024
    print(f'{name:10s} triangles {n}  size {size} m  file {kb:.1f} KB')
    if prev:
        a.img.resize((256, 256)).save(os.path.join(prev, f'atlas_{name}.png'))


if __name__ == '__main__':
    prev = sys.argv[1] if len(sys.argv) > 1 else None
    for name in PROPS:
        build(name, prev)
