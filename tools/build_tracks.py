"""Build the Formula Legends circuit files (.emt) from real-world data.

  python3 tools/build_tracks.py            -> DATA/tracks/<id>.emt + DATA/tracks/tracks.json

Sources (see tools/tracks/README.md)
  * outlines: bacinger/f1-circuits (MIT), tools/tracks/circuits/*.geojson -- the real layouts, not
    reshaped: every lap is scaled to the circuit's official length
  * elevation along the lap
      - 8 circuits (Imola, Spa, Interlagos, COTA, Barcelona, Monza, Lusail, Bahrain): the EU-DEM 25 m /
        SRTM 30 m profiles fetched for Endurance Motor Racing (OpenTopoData), every 20 m, stored in
        tools/tracks/emr/<emr id>.emt together with the real terrain grid round the circuit
      - the others: 40 samples round the lap from OpenTopoData (EU-DEM 25 m in Europe, SRTM 30 m
        elsewhere), tools/tracks/elev_opentopodata.json. Mexico City is flat (2,232 m).
    Surface models see buildings in city centres, so street circuits keep the lower envelope.

.emt layout (little endian) -- unchanged from EMR
  'EMT1' u32 n, f32 step, f32 length
  n * { f32 x, y, z, halfWidth }        centre line every `step` metres, x east, y north, z up (lowest = 0)
  f32 gx0, gy0, gstep, u32 gnx, gny, gnx*gny * f32 terrain height (same datum)
  f32 elevMin, elevMax                  real heights above sea level (for the menu)
"""
import os, sys, json, math, struct
import numpy as np
from scipy.interpolate import PchipInterpolator
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'tracks'))
from trackgeo import *

ROOT = os.path.dirname(HERE)
STEP = 5.0
# id: (geojson file, half width m, street circuit, EMR file with its DEM profile or None)
CIRCUITS = {
    'interlagos':  ('br-1940', 6.5, False, 'planalto'),
    'imola':       ('it-1953', 6.0, False, 'santerno'),
    'monaco':      ('mc-1929', 4.6, True,  None),
    'barcelona':   ('es-1991', 7.0, False, 'montcada'),
    'montreal':    ('ca-1978', 6.0, True,  None),
    'magnycours':  ('fr-1960', 7.0, False, None),
    'hungaroring': ('hu-1986', 6.5, False, None),
    'spa':         ('be-1925', 6.5, False, 'ardennes'),
    'monza':       ('it-1922', 7.0, False, 'parco'),
    'suzuka':      ('jp-1962', 6.0, False, None),
    'bahrain':     ('bh-2002', 7.5, False, 'sahra'),
    'melbourne':   ('au-1953', 6.5, True,  None),
    'sepang':      ('my-1999', 8.0, False, None),
    'shanghai':    ('cn-2004', 8.0, False, None),
    'istanbul':    ('tr-2005', 7.5, False, None),
    'singapore':   ('sg-2008', 5.5, True,  None),
    'abudhabi':    ('ae-2009', 7.5, False, None),
    'sochi':       ('ru-2014', 7.0, True,  None),
    'baku':        ('az-2016', 5.5, True,  None),
    'redbullring': ('at-1969', 6.5, False, None),
    'silverstone': ('gb-1948', 7.5, False, None),
    'hockenheim':  ('de-1932', 7.0, False, None),
    'cota':        ('us-2012', 7.5, False, 'lonehill'),
    'mexico':      ('mx-1962', 6.5, False, None),
    'jeddah':      ('sa-2021', 6.0, True,  None),
    'miami':       ('us-2022', 6.5, True,  None),
    'zandvoort':   ('nl-1948', 6.0, False, None),
    'lasvegas':    ('us-2023', 6.5, True,  None),
    'lusail':      ('qa-2004', 7.0, False, 'albidda'),
}
FLAT = {'mexico': 2232.0}
# street circuits in tall city centres: a wider lower envelope (samples either side)
# metres to move the start line along the traced lap (Monaco's outline starts at Casino Square)
START_SHIFT = {'monaco': 2395.0}
ENVELOPE = {'singapore': 3, 'lasvegas': 2, 'jeddah': 2, 'miami': 2}


def read_emt(path):
    b = open(path, 'rb').read()
    n, step, L = struct.unpack_from('<Iff', b, 4)
    line = np.frombuffer(b, '<f4', n * 4, 16).reshape(n, 4)
    o = 16 + n * 16
    gx0, gy0, gs = struct.unpack_from('<3f', b, o); gnx, gny = struct.unpack_from('<2I', b, o + 12)
    grid = np.frombuffer(b, '<f4', gnx * gny, o + 20)
    emin, emax = struct.unpack_from('<2f', b, o + 20 + gnx * gny * 4)
    return dict(z=line[:, 2] + emin, grid=(gx0, gy0, gs, gnx, gny, grid + emin), emin=emin, emax=emax)


def closed_interp(vals, m):
    """periodic smooth interpolation of samples spread evenly round the lap -> m samples"""
    v = np.asarray(vals, float)
    n = len(v)
    good = ~np.isnan(v)
    xs = np.arange(n)[good]; ys = v[good]
    xs = np.concatenate([xs - n, xs, xs + n]); ys = np.concatenate([ys, ys, ys])
    f = PchipInterpolator(xs, ys)
    return f(np.arange(m) * n / m)


def profile(tid, street, m):
    """heights (m above sea level) at m points evenly round the lap"""
    if tid in FLAT:
        return np.full(m, FLAT[tid])
    r = json.load(open(os.path.join(HERE, 'tracks', 'elev_opentopodata.json')))[tid]
    v = np.full(40, np.nan)
    for start, vals in r.items():
        for i, x in enumerate(vals):
            v[int(start) + i] = x
    n = len(v)
    # surface models: buildings and trees stand above the road. Street circuits: lower envelope
    # (the lowest of each sample and its neighbours); elsewhere a 3-sample median.
    win = ENVELOPE.get(tid, 1 if street else 0)
    if win:
        w = np.array([v[(np.arange(n) + k) % n] for k in range(-win, win + 1)])
        v = np.nanmin(w, 0)
    else:
        w = np.array([v[(np.arange(n) + k) % n] for k in (-1, 0, 1)])
        v = np.nanmedian(w, 0)
    z = closed_interp(v, m)
    return np.array(smooth_values_closed(list(z), max(3, int(120 / STEP) | 1)))


def synth_grid(xy, z, margin=700.0, gs=200.0):
    """terrain round a circuit from its own height profile (inverse-distance weights)"""
    xy = np.asarray(xy); z = np.asarray(z)
    lo = xy.min(0) - margin; hi = xy.max(0) + margin
    gnx = int(math.ceil((hi[0] - lo[0]) / gs)) + 1; gny = int(math.ceil((hi[1] - lo[1]) / gs)) + 1
    sub = xy[::8]; zs = z[::8]
    g = np.zeros(gnx * gny)
    for j in range(gny):
        for i in range(gnx):
            p = lo + np.array([i * gs, j * gs])
            d2 = ((sub - p) ** 2).sum(1)
            w = 1.0 / (d2 + 80.0 ** 2)
            g[j * gnx + i] = (w * zs).sum() / w.sum()
    return (float(lo[0]), float(lo[1]), gs, gnx, gny, g)


def build_one(tid, out):
    fn, hw, street, emr = CIRCUITS[tid]
    gj = json.load(open(os.path.join(HERE, 'tracks', 'circuits', fn + '.geojson')))['features'][0]
    official = float(gj['properties']['length'])
    ll = [(c[1], c[0]) for c in gj['geometry']['coordinates']]
    xy = dedupe_closed(to_local(ll, ll[0]))
    dense = resample_closed(xy, STEP)
    n0 = len(dense)
    frac = [i / n0 for i in range(n0)]
    import build_tracks_spurs as SP
    dense, frac = SP.remove_spurs(tid, dense, frac, base=14)
    sm = smooth_closed(dense, passes=2 if street else 6, keep=0.5)
    # keep each point's place round the original lap (for the heights)
    sm_f = frac
    # even spacing again, carrying the lap fraction along
    seg = [math.hypot(sm[(i + 1) % len(sm)][0] - sm[i][0], sm[(i + 1) % len(sm)][1] - sm[i][1]) for i in range(len(sm))]
    cum = np.concatenate([[0], np.cumsum(seg)]); Ls = cum[-1]
    m = max(8, int(round(Ls / STEP)))
    targets = np.arange(m) * Ls / m
    P = np.array(sm + [sm[0]]); Fr = np.array(sm_f + [1.0 + sm_f[0]])
    Fr = np.unwrap(Fr * 2 * np.pi) / (2 * np.pi)
    xs = np.interp(targets, cum, P[:, 0]); ys = np.interp(targets, cum, P[:, 1]); fr = np.interp(targets, cum, Fr) % 1.0
    # scale to the official lap length (about the middle of the circuit)
    k = official / Ls
    cx, cy = xs.mean(), ys.mean()
    xs = cx + (xs - cx) * k; ys = cy + (ys - cy) * k
    line = list(zip(xs.tolist(), ys.tolist()))
    L = length(line)
    # heights
    if emr:
        e = read_emt(os.path.join(HERE, 'tracks', 'emr', emr + '.emt'))
        ez = np.asarray(e['z'], float)
        z = np.interp(fr * len(ez), np.arange(len(ez) + 1), np.concatenate([ez, ez[:1]]))
        gx0, gy0, gs, gnx, gny, grid = e['grid']
        grid = np.asarray(grid, float)
    else:
        pz = profile(tid, street, 2000)
        z = np.interp(fr * 2000, np.arange(2001), np.concatenate([pz, pz[:1]]))
        gx0, gy0, gs, gnx, gny, grid = synth_grid(line, z)
    sh = START_SHIFT.get(tid, 0.0)
    if sh:
        k = int(round(sh / (L / len(line)))) % len(line)
        line = line[k:] + line[:k]; z = np.concatenate([z[k:], z[:k]])
    zmin = float(z.min())
    with open(os.path.join(out, tid + '.emt'), 'wb') as f:
        f.write(b'EMT1'); f.write(struct.pack('<Iff', len(line), L / len(line), L))
        for (x, y), h in zip(line, z):
            f.write(struct.pack('<4f', x, y, h - zmin, hw))
        f.write(struct.pack('<3f2I', gx0, gy0, gs, gnx, gny))
        f.write(struct.pack(f'<{len(grid)}f', *[float(g) - zmin for g in grid]))
        f.write(struct.pack('<2f', zmin, float(z.max())))
    n = len(z); w = int(100 / STEP)
    grades = [(z[(i + w) % n] - z[i]) / 100.0 for i in range(n)]
    return dict(length=L, official=official, zmin=zmin, zmax=float(z.max()), rise=float(z.max() - zmin),
                maxClimb=max(grades) * 100, maxDrop=-min(grades) * 100, cw=signed_area(line) < 0, street=street)


def main():
    out = os.path.join(ROOT, 'DATA', 'tracks')
    os.makedirs(out, exist_ok=True)
    only = sys.argv[1:]
    rep = {}
    for tid in CIRCUITS:
        if only and tid not in only: continue
        r = build_one(tid, out)
        rep[tid] = r
        print(f"{tid:12s} {r['length']:6.0f} m  height {r['zmin']:7.1f}-{r['zmax']:7.1f} (rise {r['rise']:5.1f} m)  "
              f"steepest +{r['maxClimb']:.1f}% / -{r['maxDrop']:.1f}%  {'clockwise' if r['cw'] else 'anticlockwise'}")
    if not only:
        json.dump(rep, open(os.path.join(out, 'tracks.json'), 'w'), indent=1)


if __name__ == '__main__':
    main()
