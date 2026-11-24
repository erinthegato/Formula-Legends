"""Cut out-and-back spurs from a traced circuit outline (from Endurance Motor Racing)."""
def remove_spurs(tid, xy, extra, max_gap=9.0, span=(40.0, 500.0), base=0):
    """Cut out-and-back spurs: places where the traced loop runs up a road and comes straight back
    down it (the two legs' centre lines closer than max_gap, i.e. the roads overlap). A real hairpin
    always has room between its legs. Joins the loop across the base of the spur instead.
    `extra` is a list kept in step with the points (the heights)."""
    import numpy as np
    from scipy.spatial import cKDTree
    pts = list(xy); ex = list(extra)
    while True:
        a = np.array(pts); n = len(a)
        seg = np.hypot(*(np.roll(a, -1, 0) - a).T); s = np.concatenate([[0], np.cumsum(seg)[:-1]])
        tree = cKDTree(a)
        best = None
        for i in range(n):
            for j in tree.query_ball_point(a[i], max_gap):
                g = (s[j] - s[i]) % s[-1] if j != i else 0
                if span[0] < g < span[1] and (best is None or g > best[2]):
                    best = (i, j, g)
        if best is None:
            return pts, ex
        i, j, g = best
        # also drop `base` points either side of the spur, so the join becomes a round hairpin, not a V
        if j > i and i - base > 0 and j + base < n: i -= base; j += base
        print(f'   {tid}: cut a {g:.0f} m out-and-back spur at {s[i]:.0f} m')
        if j > i: pts = pts[:i + 1] + pts[j:]; ex = ex[:i + 1] + ex[j:]
        else: pts = pts[j:i + 1]; ex = ex[j:i + 1]


