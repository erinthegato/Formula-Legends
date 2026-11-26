"""Fetch a fine terrain grid (50 m) round a circuit from OpenTopoData, in the game's own coordinates.

  python3 tools/tracks/fetch_dem.py interlagos spa redbullring   -> tools/tracks/dem/<id>.json

Runs on GitHub Actions (.github/workflows/fetch_dem.yml): the public API allows 100 points a call and
one call a second. Europe uses EU-DEM 25 m (bare ground); elsewhere SRTM 30 m. build_tracks.py then
uses the grid for the terrain (and, for circuits without an EMR profile, for the heights along the lap).
"""
import os, sys, json, time, math, urllib.request
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import build_tracks as BT

EUROPE = {'spa', 'redbullring', 'imola', 'monza', 'barcelona', 'hungaroring', 'silverstone', 'hockenheim',
          'magnycours', 'zandvoort', 'monaco'}
GS, MARGIN = 50.0, 700.0


def fetch(dataset, pts):
    out = []
    for i in range(0, len(pts), 100):
        q = '|'.join(f'{a:.6f},{b:.6f}' for a, b in pts[i:i + 100])
        url = f'https://api.opentopodata.org/v1/{dataset}?locations={q}'
        for attempt in range(6):
            try:
                with urllib.request.urlopen(url, timeout=60) as r:
                    res = json.load(r)['results']
                break
            except Exception as e:
                print('  retry', e); time.sleep(3 + attempt * 3)
        else:
            raise SystemExit('OpenTopoData did not answer')
        out += [x['elevation'] for x in res]
        time.sleep(1.1)
        if (i // 100) % 10 == 0: print(f'  {i + len(res)} / {len(pts)}', flush=True)
    return out


def main():
    os.makedirs(os.path.join(HERE, 'dem'), exist_ok=True)
    for tid in sys.argv[1:]:
        lay = BT.layout(tid)
        xs = [p[0] for p in lay['line']]; ys = [p[1] for p in lay['line']]
        gx0 = min(xs) - MARGIN; gy0 = min(ys) - MARGIN
        gnx = int(math.ceil((max(xs) + MARGIN - gx0) / GS)) + 1; gny = int(math.ceil((max(ys) + MARGIN - gy0) / GS)) + 1
        pts = [(gx0 + i * GS, gy0 + j * GS) for j in range(gny) for i in range(gnx)]
        ll = BT.game_to_latlon(lay, pts)
        print(f'{tid}: {gnx}x{gny} = {len(pts)} points', flush=True)
        z = fetch('eudem25m' if tid in EUROPE else 'srtm30m', ll)
        if tid in EUROPE and any(v is None for v in z):       # (outside EU-DEM's coverage)
            miss = [k for k, v in enumerate(z) if v is None]
            fill = fetch('srtm30m', [ll[k] for k in miss])
            for k, v in zip(miss, fill): z[k] = v
        json.dump(dict(gx0=gx0, gy0=gy0, gs=GS, gnx=gnx, gny=gny, z=[None if v is None else round(v, 2) for v in z]),
                  open(os.path.join(HERE, 'dem', tid + '.json'), 'w'))


if __name__ == '__main__':
    main()
