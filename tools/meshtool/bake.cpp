// Endurance Motor Racing 3D - turn an AI-made Unity model into a PSP-sized model.
//
//  1. weld the exported mesh by position and reduce it with quadric edge collapses
//     (every face counts the same, so small parts like rear wings and mirrors survive);
//  2. split the low-poly mesh into flat-ish "charts" and lay them out on a fresh square texture;
//  3. bake: every texel looks up the closest point on the ORIGINAL model and copies its colour.
//
// The AI textures are cut into ~1700 tiny pieces per model, so re-using the old texture
// coordinates smears; baking a new texture keeps the paint clean.
//
// usage: bake in.mesh in_rgb.raw texW texH out.mesh out_rgb.raw outSize targetTris
//   in_rgb.raw : the original texture as raw RGB bytes, top row first (made by tools/build_models.py)
//   out.mesh   : same layout as the input, 3 corners per triangle, UVs into the new texture (v up)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <functional>

struct V3 { double x, y, z; };
static V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
static double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static double len(V3 a) { return std::sqrt(dot(a, a)); }
static V3 norm(V3 a) { double l = len(a); return l > 1e-20 ? a * (1.0 / l) : V3{0, 0, 1}; }

struct SrcVert { float p[3], n[3], uv[2]; };

static bool loadMesh(const char* fn, std::vector<SrcVert>& v, std::vector<int>& idx) {
    FILE* f = fopen(fn, "rb");
    if (!f) return false;
    int vc, ic;
    if (fread(&vc, 4, 1, f) != 1 || fread(&ic, 4, 1, f) != 1) { fclose(f); return false; }
    v.resize(vc); idx.resize(ic);
    size_t a = fread(v.data(), sizeof(SrcVert), vc, f);
    size_t b = fread(idx.data(), 4, ic, f);
    fclose(f);
    return a == (size_t)vc && b == (size_t)ic;
}

// ======================================================================= quadric simplifier
struct Q { double a[10]; };
static Q qplane(V3 n, double d, double w) {
    Q q; double* m = q.a;
    m[0] = n.x * n.x; m[1] = n.x * n.y; m[2] = n.x * n.z; m[3] = n.x * d;
    m[4] = n.y * n.y; m[5] = n.y * n.z; m[6] = n.y * d;
    m[7] = n.z * n.z; m[8] = n.z * d; m[9] = d * d;
    for (double& x : q.a) x *= w;
    return q;
}
static void qadd(Q& q, const Q& r) { for (int i = 0; i < 10; i++) q.a[i] += r.a[i]; }
static double qerr(const Q& q, V3 v) {
    const double* m = q.a; double x = v.x, y = v.y, z = v.z;
    return m[0] * x * x + 2 * m[1] * x * y + 2 * m[2] * x * z + 2 * m[3] * x + m[4] * y * y + 2 * m[5] * y * z + 2 * m[6] * y + m[7] * z * z + 2 * m[8] * z + m[9];
}
static bool qsolve(const Q& q, V3& out) {
    const double* m = q.a;
    double A[3][3] = {{m[0], m[1], m[2]}, {m[1], m[4], m[5]}, {m[2], m[5], m[7]}};
    double b[3] = {-m[3], -m[6], -m[8]};
    double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if (std::fabs(det) < 1e-14) return false;
    double inv = 1.0 / det;
    out.x = (b[0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (b[1] * A[2][2] - A[1][2] * b[2]) + A[0][2] * (b[1] * A[2][1] - A[1][1] * b[2])) * inv;
    out.y = (A[0][0] * (b[1] * A[2][2] - A[1][2] * b[2]) - b[0] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) + A[0][2] * (A[1][0] * b[2] - b[1] * A[2][0])) * inv;
    out.z = (A[0][0] * (A[1][1] * b[2] - b[1] * A[2][1]) - A[0][1] * (A[1][0] * b[2] - b[1] * A[2][0]) + b[0] * (A[1][0] * A[2][1] - A[1][1] * A[2][0])) * inv;
    return true;
}

struct Tri { int v[3]; bool dead; };
struct Edge { double cost; int a, b, sa, sb; V3 target; };
struct EdgeCmp { bool operator()(const Edge& x, const Edge& y) const { return x.cost > y.cost; } };

struct Simplifier {
    std::vector<V3> P; std::vector<Q> Qs; std::vector<int> stamp; std::vector<char> deadV;
    std::vector<Tri> T; std::vector<std::vector<int>> vt;
    std::priority_queue<Edge, std::vector<Edge>, EdgeCmp> heap;
    int alive = 0;
    V3 tn(const Tri& t) const { return cross(P[t.v[1]] - P[t.v[0]], P[t.v[2]] - P[t.v[0]]); }

    void build() {
        Qs.assign(P.size(), Q{}); stamp.assign(P.size(), 0); deadV.assign(P.size(), 0); vt.assign(P.size(), {});
        for (int i = 0; i < (int)T.size(); i++) {
            Tri& t = T[i]; V3 n = norm(tn(t));
            // weight 1 per face (not area): small details are as important as big panels
            Q q = qplane(n, -dot(n, P[t.v[0]]), 1.0);
            for (int k = 0; k < 3; k++) { qadd(Qs[t.v[k]], q); vt[t.v[k]].push_back(i); }
        }
        alive = (int)T.size();
        std::unordered_map<long long, int> ec;
        auto key = [](int a, int b) { if (a > b) std::swap(a, b); return ((long long)a << 32) | (unsigned)b; };
        for (auto& t : T) for (int k = 0; k < 3; k++) ec[key(t.v[k], t.v[(k + 1) % 3])]++;
        for (auto& t : T) for (int k = 0; k < 3; k++) {
            int a = t.v[k], b = t.v[(k + 1) % 3];
            if (ec[key(a, b)] != 1) continue;
            V3 e = P[b] - P[a]; V3 pn = norm(cross(e, norm(tn(t))));
            Q q = qplane(pn, -dot(pn, P[a]), 4.0);
            qadd(Qs[a], q); qadd(Qs[b], q);
        }
        for (auto& t : T) for (int k = 0; k < 3; k++) { int a = t.v[k], b = t.v[(k + 1) % 3]; if (a < b) push(a, b); }
    }
    void push(int a, int b) {
        Q q = Qs[a]; qadd(q, Qs[b]);
        V3 best, mid = (P[a] + P[b]) * 0.5; double cost;
        if (qsolve(q, best) && len(best - mid) < len(P[a] - P[b])) cost = qerr(q, best);
        else {
            double ea = qerr(q, P[a]), eb = qerr(q, P[b]), em = qerr(q, mid);
            if (ea <= eb && ea <= em) { best = P[a]; cost = ea; } else if (eb <= em) { best = P[b]; cost = eb; } else { best = mid; cost = em; }
        }
        // tiny length term keeps edges evenly sized once the shape error is ~0
        cost = std::max(cost, 0.0) + 1e-7 * dot(P[a] - P[b], P[a] - P[b]);
        heap.push(Edge{cost, a, b, stamp[a], stamp[b], best});
    }
    bool okTri(int ti, int self, int other, V3 np) {
        Tri& t = T[ti];
        for (int k = 0; k < 3; k++) if (t.v[k] == other) return true;
        V3 p[3]; for (int k = 0; k < 3; k++) p[k] = t.v[k] == self ? np : P[t.v[k]];
        V3 n1 = cross(p[1] - p[0], p[2] - p[0]);
        if (len(n1) < 1e-12) return false;
        return dot(norm(tn(t)), norm(n1)) > 0.3;
    }
    bool flips(int a, int b, V3 np) {
        for (int ti : vt[a]) if (!T[ti].dead && !okTri(ti, a, b, np)) return true;
        for (int ti : vt[b]) if (!T[ti].dead && !okTri(ti, b, a, np)) return true;
        return false;
    }
    void run(int target) {
        while (alive > target && !heap.empty()) {
            Edge e = heap.top(); heap.pop();
            if (deadV[e.a] || deadV[e.b] || e.sa != stamp[e.a] || e.sb != stamp[e.b]) continue;
            if (flips(e.a, e.b, e.target)) continue;
            int keep = e.a, gone = e.b;
            P[keep] = e.target; qadd(Qs[keep], Qs[gone]); deadV[gone] = 1;
            for (int ti : vt[gone]) {
                Tri& t = T[ti]; if (t.dead) continue;
                bool shared = false; for (int k = 0; k < 3; k++) if (t.v[k] == keep) shared = true;
                if (shared) { t.dead = true; alive--; continue; }
                for (int k = 0; k < 3; k++) if (t.v[k] == gone) t.v[k] = keep;
                vt[keep].push_back(ti);
            }
            vt[gone].clear();
            std::vector<int> nl; for (int ti : vt[keep]) if (!T[ti].dead) nl.push_back(ti);
            std::sort(nl.begin(), nl.end()); nl.erase(std::unique(nl.begin(), nl.end()), nl.end()); vt[keep] = nl;
            std::vector<int> nb; for (int ti : nl) for (int k = 0; k < 3; k++) nb.push_back(T[ti].v[k]);
            std::sort(nb.begin(), nb.end()); nb.erase(std::unique(nb.begin(), nb.end()), nb.end());
            for (int n : nb) stamp[n]++;
            for (int n : nb) {
                std::vector<int> nn;
                for (int ti : vt[n]) if (!T[ti].dead) for (int k = 0; k < 3; k++) if (T[ti].v[k] != n) nn.push_back(T[ti].v[k]);
                std::sort(nn.begin(), nn.end()); nn.erase(std::unique(nn.begin(), nn.end()), nn.end());
                for (int m : nn) if (n < m) push(n, m); else push(m, n);
            }
        }
    }
};

// ======================================================================= closest point queries
static V3 closestOnTri(V3 p, V3 a, V3 b, V3 c, double& u, double& v, double& w) {
    V3 ab = b - a, ac = c - a, ap = p - a;
    double d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) { u = 1; v = 0; w = 0; return a; }
    V3 bp = p - b; double d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) { u = 0; v = 1; w = 0; return b; }
    double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) { double t = d1 / (d1 - d3); u = 1 - t; v = t; w = 0; return a + ab * t; }
    V3 cp = p - c; double d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) { u = 0; v = 0; w = 1; return c; }
    double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) { double t = d2 / (d2 - d6); u = 1 - t; v = 0; w = t; return a + ac * t; }
    double va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) { double t = (d4 - d3) / ((d4 - d3) + (d5 - d6)); u = 0; v = 1 - t; w = t; return b + (c - b) * t; }
    double den = 1.0 / (va + vb + vc); v = vb * den; w = vc * den; u = 1 - v - w;
    return a + ab * v + ac * w;
}

// Uniform grid over the source triangles for nearest-surface lookups.
struct Grid {
    V3 lo; double cell; int nx, ny, nz;
    std::vector<std::vector<int>> cells;
    const std::vector<V3>* P; const std::vector<int>* I;
    void build(const std::vector<V3>& p, const std::vector<int>& idx, int res) {
        P = &p; I = &idx;
        V3 mn{1e30, 1e30, 1e30}, mx{-1e30, -1e30, -1e30};
        for (auto& v : p) { mn = {std::min(mn.x, v.x), std::min(mn.y, v.y), std::min(mn.z, v.z)}; mx = {std::max(mx.x, v.x), std::max(mx.y, v.y), std::max(mx.z, v.z)}; }
        double ext = std::max(mx.x - mn.x, std::max(mx.y - mn.y, mx.z - mn.z));
        cell = ext / res; lo = mn - V3{cell, cell, cell};
        nx = (int)((mx.x - lo.x) / cell) + 2; ny = (int)((mx.y - lo.y) / cell) + 2; nz = (int)((mx.z - lo.z) / cell) + 2;
        cells.assign((size_t)nx * ny * nz, {});
        for (int t = 0; t < (int)idx.size() / 3; t++) {
            V3 a = p[idx[t * 3]], b = p[idx[t * 3 + 1]], c = p[idx[t * 3 + 2]];
            int x0 = (int)((std::min(a.x, std::min(b.x, c.x)) - lo.x) / cell), x1 = (int)((std::max(a.x, std::max(b.x, c.x)) - lo.x) / cell);
            int y0 = (int)((std::min(a.y, std::min(b.y, c.y)) - lo.y) / cell), y1 = (int)((std::max(a.y, std::max(b.y, c.y)) - lo.y) / cell);
            int z0 = (int)((std::min(a.z, std::min(b.z, c.z)) - lo.z) / cell), z1 = (int)((std::max(a.z, std::max(b.z, c.z)) - lo.z) / cell);
            for (int z = z0; z <= z1; z++) for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) cells[((size_t)z * ny + y) * nx + x].push_back(t);
        }
    }
    // nearest triangle; prefers surfaces facing like `want` (so both sides of a thin wing keep their own paint)
    int nearest(V3 q, V3 want, double& bu, double& bv, double& bw) const {
        int cx = (int)((q.x - lo.x) / cell), cy = (int)((q.y - lo.y) / cell), cz = (int)((q.z - lo.z) / cell);
        double best = 1e30; int bt = -1;
        for (int r = 0; r < std::max(nx, std::max(ny, nz)); r++) {
            for (int z = cz - r; z <= cz + r; z++) for (int y = cy - r; y <= cy + r; y++) for (int x = cx - r; x <= cx + r; x++) {
                if (std::max(std::abs(z - cz), std::max(std::abs(y - cy), std::abs(x - cx))) != r) continue;
                if (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz) continue;
                for (int t : cells[((size_t)z * ny + y) * nx + x]) {
                    V3 a = (*P)[(*I)[t * 3]], b = (*P)[(*I)[t * 3 + 1]], c = (*P)[(*I)[t * 3 + 2]];
                    double u, v, w; V3 x2 = closestOnTri(q, a, b, c, u, v, w);
                    double d = len(x2 - q);
                    V3 n = norm(cross(b - a, c - a));
                    double facing = dot(n, want);
                    d += (1.0 - facing) * cell * 0.15; // facing penalty
                    if (d < best) { best = d; bt = t; bu = u; bv = v; bw = w; }
                }
            }
            if (bt >= 0 && best < (r - 1) * cell) break;
        }
        return bt;
    }
};

// ======================================================================= charts + packing
struct Chart { std::vector<int> tris; V3 axisU, axisV; double minU, minV, w, h; int px, py, pw, ph; double scale; };

int main(int argc, char** argv) {
    if (argc < 9) { fprintf(stderr, "usage: bake in.mesh in.raw W H out.mesh out.raw size tris\n"); return 1; }
    std::vector<SrcVert> sv; std::vector<int> si;
    if (!loadMesh(argv[1], sv, si)) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    int TW = atoi(argv[3]), TH = atoi(argv[4]);
    std::vector<unsigned char> tex((size_t)TW * TH * 3);
    { FILE* f = fopen(argv[2], "rb"); if (!f || fread(tex.data(), 1, tex.size(), f) != tex.size()) { fprintf(stderr, "bad texture\n"); return 1; } fclose(f); }
    const char* outMesh = argv[5]; const char* outTex = argv[6];
    int S = atoi(argv[7]); int target = atoi(argv[8]);
    int ntri = (int)si.size() / 3;

    // --- simplify
    Simplifier sim;
    std::unordered_map<long long, int> wmap; std::vector<int> remap(sv.size());
    auto qz = [](float f) { return (long long)std::llround(f * 100000.0); };
    for (size_t i = 0; i < sv.size(); i++) {
        long long k = (qz(sv[i].p[0]) * 73856093LL) ^ (qz(sv[i].p[1]) * 19349663LL) ^ (qz(sv[i].p[2]) * 83492791LL);
        auto it = wmap.find(k);
        if (it != wmap.end()) { V3 o = sim.P[it->second]; if (std::fabs(o.x - sv[i].p[0]) < 2e-5 && std::fabs(o.y - sv[i].p[1]) < 2e-5 && std::fabs(o.z - sv[i].p[2]) < 2e-5) { remap[i] = it->second; continue; } }
        remap[i] = (int)sim.P.size(); wmap[k] = remap[i]; sim.P.push_back({sv[i].p[0], sv[i].p[1], sv[i].p[2]});
    }
    for (int t = 0; t < ntri; t++) {
        Tri tr{{remap[si[t * 3]], remap[si[t * 3 + 1]], remap[si[t * 3 + 2]]}, false};
        if (tr.v[0] == tr.v[1] || tr.v[1] == tr.v[2] || tr.v[0] == tr.v[2]) continue;
        sim.T.push_back(tr);
    }
    sim.build();
    if (target > 0 && target < (int)sim.T.size()) sim.run(target);
    std::vector<int> lt; // live triangles
    for (int i = 0; i < (int)sim.T.size(); i++) if (!sim.T[i].dead) lt.push_back(i);
    int L = (int)lt.size();
    std::vector<V3> fn(L); std::vector<double> farea(L);
    for (int i = 0; i < L; i++) { V3 n = sim.tn(sim.T[lt[i]]); farea[i] = len(n) * 0.5; fn[i] = norm(n); }

    // --- charts: grow regions of connected faces whose normals stay within ~40 deg of the seed
    std::unordered_map<long long, std::vector<int>> edgeFaces;
    auto ek = [](int a, int b) { if (a > b) std::swap(a, b); return ((long long)a << 32) | (unsigned)b; };
    for (int i = 0; i < L; i++) { auto& t = sim.T[lt[i]]; for (int k = 0; k < 3; k++) edgeFaces[ek(t.v[k], t.v[(k + 1) % 3])].push_back(i); }
    std::vector<int> chartOf(L, -1); std::vector<Chart> charts;
    std::vector<int> order(L); for (int i = 0; i < L; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return farea[a] > farea[b]; });
    for (int seed : order) {
        if (chartOf[seed] >= 0) continue;
        Chart c; V3 cn = fn[seed]; int id = (int)charts.size();
        std::vector<int> st{seed}; chartOf[seed] = id;
        V3 sumN{0, 0, 0};
        while (!st.empty()) {
            int f = st.back(); st.pop_back(); c.tris.push_back(f); sumN = sumN + fn[f] * farea[f];
            auto& t = sim.T[lt[f]];
            for (int k = 0; k < 3; k++) for (int g : edgeFaces[ek(t.v[k], t.v[(k + 1) % 3])]) {
                if (chartOf[g] >= 0) continue;
                if (dot(fn[g], cn) < 0.77) continue;
                chartOf[g] = id; st.push_back(g);
            }
        }
        V3 n = norm(sumN);
        V3 up = std::fabs(n.z) < 0.9 ? V3{0, 0, 1} : V3{1, 0, 0};
        c.axisU = norm(cross(up, n)); c.axisV = cross(n, c.axisU);
        double mnU = 1e30, mxU = -1e30, mnV = 1e30, mxV = -1e30;
        for (int f : c.tris) { auto& t = sim.T[lt[f]]; for (int k = 0; k < 3; k++) { V3 p = sim.P[t.v[k]]; double u = dot(p, c.axisU), v = dot(p, c.axisV); mnU = std::min(mnU, u); mxU = std::max(mxU, u); mnV = std::min(mnV, v); mxV = std::max(mxV, v); } }
        c.minU = mnU; c.minV = mnV; c.w = mxU - mnU; c.h = mxV - mnV;
        charts.push_back(c);
    }

    // --- pack: shelf packing, binary search on texel density so everything fits in S x S
    const int pad = 2;
    auto tryPack = [&](double dens) {
        std::vector<int> ord(charts.size()); for (size_t i = 0; i < ord.size(); i++) ord[i] = (int)i;
        for (auto& c : charts) { c.pw = std::max(1, (int)std::ceil(c.w * dens)) + pad * 2; c.ph = std::max(1, (int)std::ceil(c.h * dens)) + pad * 2; }
        std::sort(ord.begin(), ord.end(), [&](int a, int b) { return charts[a].ph > charts[b].ph; });
        int x = 0, y = 0, rowH = 0;
        for (int i : ord) {
            Chart& c = charts[i];
            if (c.pw > S) return false;
            if (x + c.pw > S) { x = 0; y += rowH; rowH = 0; }
            if (y + c.ph > S) return false;
            c.px = x; c.py = y; x += c.pw; rowH = std::max(rowH, c.ph);
        }
        return true;
    };
    double lo = 1, hi = 100000;
    for (int it = 0; it < 40; it++) { double m = std::sqrt(lo * hi); if (tryPack(m)) lo = m; else hi = m; }
    tryPack(lo);
    double dens = lo;

    // --- output corners with new UVs
    std::vector<SrcVert> out;
    std::vector<std::vector<int>> vfaces(sim.P.size());
    for (int i = 0; i < L; i++) for (int k = 0; k < 3; k++) vfaces[sim.T[lt[i]].v[k]].push_back(i);
    auto uvOf = [&](const Chart& c, V3 p, double& u, double& v) {
        double x = c.px + pad + (dot(p, c.axisU) - c.minU) * dens;
        double y = c.py + pad + (dot(p, c.axisV) - c.minV) * dens;
        u = x / S; v = 1.0 - y / S;
    };
    for (int i = 0; i < L; i++) {
        auto& t = sim.T[lt[i]]; const Chart& c = charts[chartOf[i]];
        for (int k = 0; k < 3; k++) {
            V3 p = sim.P[t.v[k]];
            SrcVert o; o.p[0] = (float)p.x; o.p[1] = (float)p.y; o.p[2] = (float)p.z;
            double u, v; uvOf(c, p, u, v); o.uv[0] = (float)u; o.uv[1] = (float)v;
            V3 n{0, 0, 0}; for (int g : vfaces[t.v[k]]) if (dot(fn[g], fn[i]) > 0.5) n = n + fn[g] * farea[g];
            n = norm(n); o.n[0] = (float)n.x; o.n[1] = (float)n.y; o.n[2] = (float)n.z;
            out.push_back(o);
        }
    }

    // --- bake
    std::vector<V3> sp(sv.size()); for (size_t i = 0; i < sv.size(); i++) sp[i] = {sv[i].p[0], sv[i].p[1], sv[i].p[2]};
    Grid grid; grid.build(sp, si, 48);
    std::vector<float> acc((size_t)S * S * 3, 0.f); std::vector<int> cov((size_t)S * S, 0);
    auto sampleSrc = [&](double u, double v, float rgb[3]) {
        // bilinear
        double x = (u - std::floor(u)) * TW - 0.5, y = (1.0 - (v - std::floor(v))) * TH - 0.5;
        int x0 = (int)std::floor(x), y0 = (int)std::floor(y); double fx = x - x0, fy = y - y0;
        for (int c = 0; c < 3; c++) {
            auto px = [&](int xx, int yy) { xx = std::min(std::max(xx, 0), TW - 1); yy = std::min(std::max(yy, 0), TH - 1); return (double)tex[((size_t)yy * TW + xx) * 3 + c]; };
            rgb[c] = (float)((px(x0, y0) * (1 - fx) + px(x0 + 1, y0) * fx) * (1 - fy) + (px(x0, y0 + 1) * (1 - fx) + px(x0 + 1, y0 + 1) * fx) * fy);
        }
    };
    for (int i = 0; i < L; i++) {
        const SrcVert* c3 = &out[i * 3];
        double X[3], Y[3];
        for (int k = 0; k < 3; k++) { X[k] = c3[k].uv[0] * S; Y[k] = (1.0 - c3[k].uv[1]) * S; }
        int x0 = std::max(0, (int)std::floor(std::min(X[0], std::min(X[1], X[2]))) - 1), x1 = std::min(S - 1, (int)std::ceil(std::max(X[0], std::max(X[1], X[2]))) + 1);
        int y0 = std::max(0, (int)std::floor(std::min(Y[0], std::min(Y[1], Y[2]))) - 1), y1 = std::min(S - 1, (int)std::ceil(std::max(Y[0], std::max(Y[1], Y[2]))) + 1);
        double den = (Y[1] - Y[2]) * (X[0] - X[2]) + (X[2] - X[1]) * (Y[0] - Y[2]);
        if (std::fabs(den) < 1e-12) continue;
        for (int py = y0; py <= y1; py++) for (int px = x0; px <= x1; px++) {
            double cx = px + 0.5, cy = py + 0.5;
            double l0 = ((Y[1] - Y[2]) * (cx - X[2]) + (X[2] - X[1]) * (cy - Y[2])) / den;
            double l1 = ((Y[2] - Y[0]) * (cx - X[2]) + (X[0] - X[2]) * (cy - Y[2])) / den;
            double l2 = 1 - l0 - l1;
            // texels just outside the triangle (up to ~1 px) are baked too, so filtering never shows the background
            double m = std::min(l0, std::min(l1, l2));
            double edge = 1.2 / std::max(1.0, std::sqrt(std::fabs(den)));
            if (m < -edge) continue;
            l0 = std::max(0.0, l0); l1 = std::max(0.0, l1); l2 = std::max(0.0, l2); double s = l0 + l1 + l2; l0 /= s; l1 /= s; l2 /= s;
            V3 p = V3{c3[0].p[0], c3[0].p[1], c3[0].p[2]} * l0 + V3{c3[1].p[0], c3[1].p[1], c3[1].p[2]} * l1 + V3{c3[2].p[0], c3[2].p[1], c3[2].p[2]} * l2;
            double bu, bv, bw; int st = grid.nearest(p, fn[i], bu, bv, bw);
            if (st < 0) continue;
            const SrcVert &A = sv[si[st * 3]], &B = sv[si[st * 3 + 1]], &C = sv[si[st * 3 + 2]];
            float rgb[3]; sampleSrc(A.uv[0] * bu + B.uv[0] * bv + C.uv[0] * bw, A.uv[1] * bu + B.uv[1] * bv + C.uv[1] * bw, rgb);
            size_t o = (size_t)py * S + px;
            bool inside = m >= 0;
            if (cov[o] == 0 || inside) {
                if (inside && cov[o] < 1000) { if (cov[o] < 1000) { acc[o * 3] = rgb[0]; acc[o * 3 + 1] = rgb[1]; acc[o * 3 + 2] = rgb[2]; cov[o] = 1000; } }
                else if (cov[o] == 0) { acc[o * 3] = rgb[0]; acc[o * 3 + 1] = rgb[1]; acc[o * 3 + 2] = rgb[2]; cov[o] = 1; }
            }
        }
    }
    // dilate into empty space so mip/bilinear at chart edges stays clean
    for (int pass = 0; pass < 4; pass++) {
        std::vector<int> nc = cov; std::vector<float> na = acc;
        for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
            size_t o = (size_t)y * S + x; if (cov[o]) continue;
            float s[3] = {0, 0, 0}; int n = 0;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                int xx = x + dx, yy = y + dy; if (xx < 0 || yy < 0 || xx >= S || yy >= S) continue;
                size_t q = (size_t)yy * S + xx; if (!cov[q]) continue;
                s[0] += acc[q * 3]; s[1] += acc[q * 3 + 1]; s[2] += acc[q * 3 + 2]; n++;
            }
            if (n) { na[o * 3] = s[0] / n; na[o * 3 + 1] = s[1] / n; na[o * 3 + 2] = s[2] / n; nc[o] = 1; }
        }
        cov = nc; acc = na;
    }
    std::vector<unsigned char> img((size_t)S * S * 3);
    for (size_t i = 0; i < img.size(); i++) img[i] = (unsigned char)std::min(255.f, std::max(0.f, acc[i] + 0.5f));
    FILE* f = fopen(outTex, "wb"); fwrite(img.data(), 1, img.size(), f); fclose(f);
    f = fopen(outMesh, "wb");
    int vc = (int)out.size(); fwrite(&vc, 4, 1, f); fwrite(&vc, 4, 1, f);
    fwrite(out.data(), sizeof(SrcVert), vc, f);
    for (int i = 0; i < vc; i++) fwrite(&i, 4, 1, f);
    fclose(f);
    fprintf(stderr, "%s: %d -> %d tris, %zu charts, %.0f texels/unit\n", argv[1], ntri, L, charts.size(), dens);
    return 0;
}
