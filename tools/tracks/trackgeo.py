"""Shared helpers for building the real-world circuits.

Coordinates: each circuit gets a local flat frame in metres around its first point
(x = east, y = north). Good to a few cm over a 5 km circuit."""
import math, json

R_LAT = 110540.0
R_LON = 111320.0

def to_local(pts, origin):
    lat0, lon0 = origin
    c = math.cos(math.radians(lat0))
    return [((lon - lon0) * R_LON * c, (lat - lat0) * R_LAT) for lat, lon in pts]

def to_latlon(xy, origin):
    lat0, lon0 = origin
    c = math.cos(math.radians(lat0))
    return [(lat0 + y / R_LAT, lon0 + x / (R_LON * c)) for x, y in xy]

def length(xy, closed=True):
    s = 0.0
    n = len(xy)
    for i in range(n if closed else n - 1):
        a, b = xy[i], xy[(i + 1) % n]
        s += math.hypot(b[0] - a[0], b[1] - a[1])
    return s

def dedupe_closed(xy):
    out = []
    for p in xy:
        if not out or math.hypot(p[0] - out[-1][0], p[1] - out[-1][1]) > 0.5:
            out.append(p)
    if math.hypot(out[0][0] - out[-1][0], out[0][1] - out[-1][1]) < 0.5:
        out.pop()
    return out

def resample_closed(xy, step):
    """Even spacing along a closed polyline."""
    n = len(xy)
    seg = [math.hypot(xy[(i + 1) % n][0] - xy[i][0], xy[(i + 1) % n][1] - xy[i][1]) for i in range(n)]
    total = sum(seg)
    m = max(8, int(round(total / step)))
    step = total / m
    out = []
    i, acc = 0, 0.0
    for k in range(m):
        d = k * step
        while acc + seg[i] < d and i < n - 1:
            acc += seg[i]; i += 1
        t = (d - acc) / seg[i] if seg[i] > 0 else 0
        a, b = xy[i], xy[(i + 1) % n]
        out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    return out

def smooth_closed(xy, passes=2, keep=0.5):
    """Corner-cutting smoothing for a closed loop (removes the polygon look of hand-traced data)."""
    pts = list(xy)
    for _ in range(passes):
        n = len(pts)
        pts = [(keep * pts[i][0] + (1 - keep) * 0.5 * (pts[i - 1][0] + pts[(i + 1) % n][0]),
                keep * pts[i][1] + (1 - keep) * 0.5 * (pts[i - 1][1] + pts[(i + 1) % n][1])) for i in range(n)]
    return pts

def smooth_values_closed(v, window):
    """Moving average over a closed loop (window in samples, odd)."""
    n = len(v); h = window // 2
    pref = [0.0]
    ext = v[-h:] + v + v[:h]
    for x in ext:
        pref.append(pref[-1] + x)
    return [(pref[i + window] - pref[i]) / window for i in range(n)]

def signed_area(xy):
    n = len(xy)
    return sum(xy[i][0] * xy[(i + 1) % n][1] - xy[(i + 1) % n][0] * xy[i][1] for i in range(n)) / 2
