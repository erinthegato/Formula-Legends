"""Formula Legends -- the era safety cars made in Blender (tools/blender/f1_eras.py, build_safety_cars)
as game models.

    python3 tools/gen_safety_cars.py

Reads tools/blender/sc_<year>.json (three levels of detail, tyres included; each material's colour in
'palette') and writes HD/models/safety_car_<era>.emm: flat colours on one 64 px texture that the
three levels share. The game picks the race's era (Renderer::loadEraParts).
"""
import json, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gen_props import Atlas, Kit, write_emm, OUT      # noqa: E402

ERAS = {'1998': '90s', '2010': 'v8', '2016': 'hybrid', '2025': 'ge'}


def build(year):
    d = json.load(open(os.path.join(HERE, 'blender', f'sc_{year}.json')))
    a = Atlas(64, 8)
    for k in d['materials']:
        a.solid(k, d['palette'][k])
    kits = []
    for lod in d['lods']:
        k = Kit()
        P = np.array(lod['p'], float).reshape(-1, 3, 3)
        N = np.array(lod['n'], float).reshape(-1, 3, 3)
        for i in range(len(P)):
            u0, v0, u1, v1 = a.rects[d['materials'][lod['m'][i]]]
            c = ((u0 + u1) / 2, (v0 + v1) / 2)
            k._add(P[i], [c, c, c], 1.0, N[i])
        kits.append(k)
    path = os.path.join(OUT, f'safety_car_{ERAS[year]}.emm')
    n, size = write_emm(path, [(kits[0], a.img, 64), (kits[1], None, 64), (kits[2], None, 64)])
    print(f'safety car {year}: triangles {n}  size {size} m  file {os.path.getsize(path) / 1024:.0f} KB')


if __name__ == '__main__':
    for y in ERAS:
        build(y)
