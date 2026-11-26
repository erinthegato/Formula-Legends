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
# narrower stretches (metres round the final lap, half width): Baku's old-town castle section is 7.6 m wide
NARROW = {'baku': [(2040.0, 2420.0, 3.9), (2080.0, 2300.0, 3.5)]}   # the castle chicane 7 m
# two parts of a lap must keep this much between their road edges (walls, a little run-off): where
# the traced outline brings them closer (Baku's castle section beside the run back down), they are
# eased apart
LEG_GAP = 7.0


def half_widths(tid, hw, n, L):
    w = np.full(n, float(hw))
    s = np.arange(n) * L / n
    for a, b, h in NARROW.get(tid, []):
        k = np.clip(np.minimum(s - a, b - s) / 40.0, 0, 1)       # 40 m blend at each end
        w = np.minimum(w, hw + (h - hw) * (k * k * (3 - 2 * k)))
    return w


def separate_legs(line, w, L, z):
    """push apart the parts of the lap (more than 150 m apart along it) whose roads come closer than
    LEG_GAP edge to edge; the push is spread smoothly along both legs so the shapes stay the same"""
    from scipy.spatial import cKDTree
    P = np.array(line, float); n = len(P)
    T = np.roll(P, -1, 0) - np.roll(P, 1, 0); T /= np.hypot(*T.T)[:, None]
    skip = int(150.0 / (L / n))
    sig = 10.0                                                    # points (~50 m)
    kern = np.exp(-0.5 * (np.arange(-30, 31) / sig) ** 2); kern /= kern.max()
    moved = 0.0
    for it in range(60):
        tree = cKDTree(P)
        D = np.zeros_like(P); worst = 0.0
        for i, j in tree.query_pairs(2 * w.max() + LEG_GAP):
            dij = abs(i - j); dij = min(dij, n - dij)
            # (a crossing on a bridge -- Suzuka -- meets at an angle or passes well above: left alone)
            if dij < skip or abs(z[i] - z[j]) > 5.5 or abs(T[i] @ T[j]) < 0.7:
                continue
            v = P[i] - P[j]; d = np.hypot(*v)
            need = w[i] + w[j] + LEG_GAP
            if d < need and d > 1e-6:
                push = (need - d) * 0.5 * v / d
                D[i] += push; D[j] -= push; worst = max(worst, need - d)
        if worst < 0.05:
            break
        # spread each push along the lap (both directions), never less than the push itself
        S = np.zeros_like(P)
        for k, kv in zip(range(-30, 31), kern):
            S += np.roll(D, k, axis=0) * kv
        P += S * 0.35
        moved = max(moved, worst)
    if moved > 0:
        print(f'   legs eased apart (closest pass was {moved:.1f} m short)')
    return [tuple(p) for p in P]


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


def layout(tid):
    """the lap as the game lays it out (before the heights): line, lap fractions, and how the local
    metres map back to latitude / longitude (origin, centre, scale)"""
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
    return dict(line=line, fr=fr, origin=ll[0], centre=(cx, cy), scale=k, official=official)


def game_to_latlon(lay, pts):
    """game x, y (metres) -> latitude, longitude"""
    cx, cy = lay['centre']; k = lay['scale']
    return to_latlon([(cx + (x - cx) / k, cy + (y - cy) / k) for x, y in pts], lay['origin'])


def load_dem(tid):
    """a fine terrain grid fetched by tools/tracks/fetch_dem.py (tools/tracks/dem/<id>.json), or None"""
    fp = os.path.join(HERE, 'tracks', 'dem', tid + '.json')
    if not os.path.exists(fp):
        return None
    d = json.load(open(fp))
    g = np.array([np.nan if v is None else v for v in d['z']], float)
    if np.isnan(g).any():                           # holes (no data): nearest neighbour fill
        idx = np.where(~np.isnan(g))[0]
        for i in np.where(np.isnan(g))[0]:
            g[i] = g[idx[np.argmin(np.abs(idx - i))]]
    return (d['gx0'], d['gy0'], d['gs'], d['gnx'], d['gny'], g)


def grid_at(grid, x, y):
    gx0, gy0, gs, gnx, gny, g = grid
    g = np.asarray(g).reshape(gny, gnx)
    fx = np.clip((np.asarray(x) - gx0) / gs, 0, gnx - 1.001); fy = np.clip((np.asarray(y) - gy0) / gs, 0, gny - 1.001)
    ix = fx.astype(int); iy = fy.astype(int); u = fx - ix; v = fy - iy
    return (g[iy, ix] * (1 - u) * (1 - v) + g[iy, ix + 1] * u * (1 - v) + g[iy + 1, ix] * (1 - u) * v + g[iy + 1, ix + 1] * u * v)


def build_one(tid, out):
    fn, hw, street, emr = CIRCUITS[tid]
    lay = layout(tid)
    line, fr, official = lay['line'], lay['fr'], lay['official']
    L = length(line)
    w = half_widths(tid, hw, len(line), L)
    dem = load_dem(tid)
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
    if dem is not None:
        # the fine grid (50 m) for the terrain; where the lap's own profile is only 40 samples, the
        # heights along the lap come from it too (smoothed over 120 m: a car-width of noise is not a hill)
        gx0, gy0, gs, gnx, gny, grid = dem
        grid = np.asarray(grid, float)
        if not emr:
            P = np.array(line)
            zz = grid_at(dem, P[:, 0], P[:, 1])
            z = np.array(smooth_values_closed(list(zz), max(3, int(120 / STEP) | 1)))
        print(f'   fine terrain grid {gnx}x{gny} at {gs:.0f} m')
    # ease apart legs that come too close (not where one crosses over the other on a bridge)
    sep = separate_legs(line, w, L, z)
    if sep != line:
        P = np.array(sep); c = P.mean(0); k2 = L / length(sep)     # back to the official length
        line = [tuple(c + (p - c) * k2) for p in P]
    L = length(line)
    sh = START_SHIFT.get(tid, 0.0)
    if sh:
        k = int(round(sh / (L / len(line)))) % len(line)
        line = line[k:] + line[:k]; z = np.concatenate([z[k:], z[:k]]); w = np.concatenate([w[k:], w[:k]])
    zmin = float(z.min())
    with open(os.path.join(out, tid + '.emt'), 'wb') as f:
        f.write(b'EMT1'); f.write(struct.pack('<Iff', len(line), L / len(line), L))
        for (x, y), h, hwi in zip(line, z, w):
            f.write(struct.pack('<4f', x, y, h - zmin, float(hwi)))
        f.write(struct.pack('<3f2I', gx0, gy0, gs, gnx, gny))
        f.write(struct.pack(f'<{len(grid)}f', *[float(g) - zmin for g in grid]))
        f.write(struct.pack('<2f', zmin, float(z.max())))
    n = len(z); w = int(100 / STEP)
    grades = [(z[(i + w) % n] - z[i]) / 100.0 for i in range(n)]
    return dict(length=L, official=official, zmin=zmin, zmax=float(z.max()), rise=float(z.max() - zmin),
                maxClimb=max(grades) * 100, maxDrop=-min(grades) * 100, cw=bool(signed_area(line) < 0), street=bool(street))


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
