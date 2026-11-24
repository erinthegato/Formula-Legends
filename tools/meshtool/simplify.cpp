// Endurance Motor Racing 3D - mesh simplifier for the PSP port.
// Reads the .mesh files exported from Unity (pos, normal, uv per vertex + triangle list),
// welds vertices by position, reduces the triangle count with quadric edge collapses
// (Garland & Heckbert), then gives every corner of the new triangles a UV taken from the
// closest point on the original surface, staying inside one UV island per triangle so the
// AI-made textures still line up.
//
// usage: simplify in.mesh out.mesh targetTris [axisFix]
// Output uses the same .mesh layout (unwelded corners: 3 vertices per triangle).
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

// ---------------------------------------------------------------- quadrics
struct Q { double a[10]; };
static void qzero(Q& q) { memset(q.a, 0, sizeof q.a); }
static Q qplane(double a, double b, double c, double d, double w) {
    Q q; double* m = q.a;
    m[0] = a * a; m[1] = a * b; m[2] = a * c; m[3] = a * d;
    m[4] = b * b; m[5] = b * c; m[6] = b * d;
    m[7] = c * c; m[8] = c * d; m[9] = d * d;
    for (double& x : q.a) x *= w;
    return q;
}
static void qadd(Q& q, const Q& r) { for (int i = 0; i < 10; i++) q.a[i] += r.a[i]; }
static double qerr(const Q& q, V3 v) {
    const double* m = q.a;
    double x = v.x, y = v.y, z = v.z;
    return m[0] * x * x + 2 * m[1] * x * y + 2 * m[2] * x * z + 2 * m[3] * x
         + m[4] * y * y + 2 * m[5] * y * z + 2 * m[6] * y
         + m[7] * z * z + 2 * m[8] * z + m[9];
}
// Solve the optimal point; false if the system is badly conditioned.
static bool qsolve(const Q& q, V3& out) {
    const double* m = q.a;
    double A[3][3] = {{m[0], m[1], m[2]}, {m[1], m[4], m[5]}, {m[2], m[5], m[7]}};
    double b[3] = {-m[3], -m[6], -m[8]};
    double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1])
               - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0])
               + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if (std::fabs(det) < 1e-12) return false;
    double inv = 1.0 / det;
    out.x = (b[0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (b[1] * A[2][2] - A[1][2] * b[2]) + A[0][2] * (b[1] * A[2][1] - A[1][1] * b[2])) * inv;
    out.y = (A[0][0] * (b[1] * A[2][2] - A[1][2] * b[2]) - b[0] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) + A[0][2] * (A[1][0] * b[2] - b[1] * A[2][0])) * inv;
    out.z = (A[0][0] * (A[1][1] * b[2] - b[1] * A[2][1]) - A[0][1] * (A[1][0] * b[2] - b[1] * A[2][0]) + b[0] * (A[1][0] * A[2][1] - A[1][1] * A[2][0])) * inv;
    return true;
}

// ---------------------------------------------------------------- simplifier
struct Tri { int v[3]; bool dead; };
struct Edge { double cost; int a, b; int stampA, stampB; V3 target; };
struct EdgeCmp { bool operator()(const Edge& x, const Edge& y) const { return x.cost > y.cost; } };

struct Simplifier {
    std::vector<V3> P; std::vector<Q> Qs; std::vector<int> stamp; std::vector<bool> deadV;
    std::vector<Tri> T; std::vector<std::vector<int>> vt; // vertex -> triangles
    std::priority_queue<Edge, std::vector<Edge>, EdgeCmp> heap;
    int alive = 0;

    V3 triNormal(const Tri& t) const { return cross(P[t.v[1]] - P[t.v[0]], P[t.v[2]] - P[t.v[0]]); }

    void build() {
        Qs.assign(P.size(), Q{}); for (auto& q : Qs) qzero(q);
        stamp.assign(P.size(), 0); deadV.assign(P.size(), false);
        vt.assign(P.size(), {});
        for (int i = 0; i < (int)T.size(); i++) {
            Tri& t = T[i];
            V3 n = triNormal(t); double area = len(n) * 0.5; n = norm(n);
            double d = -dot(n, P[t.v[0]]);
            Q q = qplane(n.x, n.y, n.z, d, area);
            for (int k = 0; k < 3; k++) { qadd(Qs[t.v[k]], q); vt[t.v[k]].push_back(i); }
        }
        alive = (int)T.size();
        // Border edges get perpendicular constraint planes so outlines keep their shape.
        std::unordered_map<long long, int> ec;
        auto key = [](int a, int b) { if (a > b) std::swap(a, b); return ((long long)a << 32) | (unsigned)b; };
        for (auto& t : T) for (int k = 0; k < 3; k++) ec[key(t.v[k], t.v[(k + 1) % 3])]++;
        for (auto& t : T) for (int k = 0; k < 3; k++) {
            int a = t.v[k], b = t.v[(k + 1) % 3];
            if (ec[key(a, b)] != 1) continue;
            V3 e = P[b] - P[a]; V3 n = norm(triNormal(t));
            V3 pn = norm(cross(e, n)); double d = -dot(pn, P[a]);
            Q q = qplane(pn.x, pn.y, pn.z, d, dot(e, e) * 10.0);
            qadd(Qs[a], q); qadd(Qs[b], q);
        }
        for (auto& t : T) for (int k = 0; k < 3; k++) { int a = t.v[k], b = t.v[(k + 1) % 3]; if (a < b) push(a, b); }
    }

    void push(int a, int b) {
        Q q = Qs[a]; qadd(q, Qs[b]);
        V3 best; double cost;
        V3 mid = (P[a] + P[b]) * 0.5;
        if (qsolve(q, best) && len(best - mid) < len(P[a] - P[b]) * 2.0) cost = qerr(q, best);
        else {
            double ea = qerr(q, P[a]), eb = qerr(q, P[b]), em = qerr(q, mid);
            if (ea <= eb && ea <= em) { best = P[a]; cost = ea; }
            else if (eb <= em) { best = P[b]; cost = eb; }
            else { best = mid; cost = em; }
        }
        heap.push(Edge{std::max(cost, 0.0), a, b, stamp[a], stamp[b], best});
    }

    // Reject collapses that flip a neighbouring triangle or make a sliver.
    bool flips(int keep, int gone, V3 np) {
        for (int ti : vt[keep]) if (!T[ti].dead) if (!okTri(ti, keep, gone, np)) return true;
        for (int ti : vt[gone]) if (!T[ti].dead) if (!okTri(ti, gone, keep, np)) return true;
        return false;
    }
    bool okTri(int ti, int self, int other, V3 np) {
        Tri& t = T[ti];
        bool hasOther = false;
        for (int k = 0; k < 3; k++) if (t.v[k] == other) hasOther = true;
        if (hasOther) return true; // will be removed
        V3 p[3];
        for (int k = 0; k < 3; k++) p[k] = t.v[k] == self ? np : P[t.v[k]];
        V3 n0 = triNormal(t);
        V3 n1 = cross(p[1] - p[0], p[2] - p[0]);
        if (dot(norm(n0), norm(n1)) < 0.2) return false;
        return true;
    }

    void run(int target) {
        while (alive > target && !heap.empty()) {
            Edge e = heap.top(); heap.pop();
            if (deadV[e.a] || deadV[e.b] || e.stampA != stamp[e.a] || e.stampB != stamp[e.b]) continue;
            if (flips(e.a, e.b, e.target)) continue;
            int keep = e.a, gone = e.b;
            P[keep] = e.target; qadd(Qs[keep], Qs[gone]);
            deadV[gone] = true;
            for (int ti : vt[gone]) {
                Tri& t = T[ti]; if (t.dead) continue;
                bool shared = false;
                for (int k = 0; k < 3; k++) if (t.v[k] == keep) shared = true;
                if (shared) { t.dead = true; alive--; continue; }
                for (int k = 0; k < 3; k++) if (t.v[k] == gone) t.v[k] = keep;
                vt[keep].push_back(ti);
            }
            vt[gone].clear();
            // compact keep's list and refresh its edges
            std::vector<int> nl; for (int ti : vt[keep]) if (!T[ti].dead) nl.push_back(ti);
            std::sort(nl.begin(), nl.end()); nl.erase(std::unique(nl.begin(), nl.end()), nl.end());
            vt[keep] = nl; stamp[keep]++;
            std::vector<int> nb;
            for (int ti : nl) for (int k = 0; k < 3; k++) if (T[ti].v[k] != keep) nb.push_back(T[ti].v[k]);
            std::sort(nb.begin(), nb.end()); nb.erase(std::unique(nb.begin(), nb.end()), nb.end());
            for (int n : nb) { stamp[n]++; }
            for (int n : nb) {
                // re-push all edges of n so its stamp stays valid
                std::vector<int> nn;
                for (int ti : vt[n]) if (!T[ti].dead) for (int k = 0; k < 3; k++) if (T[ti].v[k] != n) nn.push_back(T[ti].v[k]);
                std::sort(nn.begin(), nn.end()); nn.erase(std::unique(nn.begin(), nn.end()), nn.end());
                for (int m : nn) push(n, m);
            }
        }
    }
};

// ---------------------------------------------------------------- closest point on triangle
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

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: simplify in.mesh out.mesh targetTris\n"); return 1; }
    std::vector<SrcVert> sv; std::vector<int> si;
    if (!loadMesh(argv[1], sv, si)) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    int target = atoi(argv[3]);
    int ntri = (int)si.size() / 3;

    // UV islands of the source = connected components over (unwelded) vertex indices.
    std::vector<int> parent(sv.size()); for (size_t i = 0; i < sv.size(); i++) parent[i] = (int)i;
    std::function<int(int)> find = [&](int x) { while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; } return x; };
    for (int t = 0; t < ntri; t++) { int a = find(si[t * 3]), b = find(si[t * 3 + 1]), c = find(si[t * 3 + 2]); parent[b] = a; parent[find(c)] = a; }
    std::vector<int> triIsland(ntri);
    for (int t = 0; t < ntri; t++) triIsland[t] = find(si[t * 3]);

    // Weld by position.
    Simplifier S;
    std::unordered_map<long long, int> wmap; std::vector<int> remap(sv.size());
    auto q = [](float f) { return (long long)std::llround(f * 200000.0); };
    for (size_t i = 0; i < sv.size(); i++) {
        long long k = (q(sv[i].p[0]) * 73856093LL) ^ (q(sv[i].p[1]) * 19349663LL) ^ (q(sv[i].p[2]) * 83492791LL);
        // collision-safe: verify
        auto it = wmap.find(k);
        if (it != wmap.end()) {
            V3 o = S.P[it->second];
            if (std::fabs(o.x - sv[i].p[0]) < 1e-5 && std::fabs(o.y - sv[i].p[1]) < 1e-5 && std::fabs(o.z - sv[i].p[2]) < 1e-5) { remap[i] = it->second; continue; }
        }
        remap[i] = (int)S.P.size(); wmap[k] = remap[i];
        S.P.push_back({sv[i].p[0], sv[i].p[1], sv[i].p[2]});
    }
    for (int t = 0; t < ntri; t++) {
        Tri tr{{remap[si[t * 3]], remap[si[t * 3 + 1]], remap[si[t * 3 + 2]]}, false};
        if (tr.v[0] == tr.v[1] || tr.v[1] == tr.v[2] || tr.v[0] == tr.v[2]) continue;
        S.T.push_back(tr);
    }
    S.build();
    if (target < (int)S.T.size()) S.run(target);

    // Source triangles, bucketed by island, with a coarse grid for the nearest search.
    std::vector<V3> sp(sv.size()); for (size_t i = 0; i < sv.size(); i++) sp[i] = {sv[i].p[0], sv[i].p[1], sv[i].p[2]};
    std::unordered_map<int, std::vector<int>> byIsland;
    for (int t = 0; t < ntri; t++) byIsland[triIsland[t]].push_back(t);

    auto nearest = [&](V3 p, const std::vector<int>* subset, V3 wantN, int& bestT, double& bu, double& bv, double& bw) {
        double bd = 1e30; bestT = -1;
        auto test = [&](int t) {
            V3 a = sp[si[t * 3]], b = sp[si[t * 3 + 1]], c = sp[si[t * 3 + 2]];
            double u, v, w; V3 x = closestOnTri(p, a, b, c, u, v, w);
            double d = dot(x - p, x - p);
            V3 n = norm(cross(b - a, c - a));
            if (dot(n, wantN) < 0.0) d += 1e-4; // prefer same-facing surface (thin parts)
            if (d < bd) { bd = d; bestT = t; bu = u; bv = v; bw = w; }
        };
        if (subset) for (int t : *subset) test(t);
        else for (int t = 0; t < ntri; t++) test(t);
    };

    std::vector<SrcVert> out;
    // smooth normals on the simplified mesh (angle limited)
    std::vector<V3> faceN(S.T.size());
    for (size_t i = 0; i < S.T.size(); i++) if (!S.T[i].dead) faceN[i] = norm(S.triNormal(S.T[i]));
    for (size_t i = 0; i < S.T.size(); i++) {
        Tri& t = S.T[i]; if (t.dead) continue;
        V3 c = (S.P[t.v[0]] + S.P[t.v[1]] + S.P[t.v[2]]) * (1.0 / 3);
        int ct; double u, v, w;
        nearest(c, nullptr, faceN[i], ct, u, v, w);
        const std::vector<int>* isl = &byIsland[triIsland[ct]];
        double uc0, uc1;
        { const SrcVert &A = sv[si[ct * 3]], &B = sv[si[ct * 3 + 1]], &C = sv[si[ct * 3 + 2]];
          uc0 = A.uv[0] * u + B.uv[0] * v + C.uv[0] * w; uc1 = A.uv[1] * u + B.uv[1] * v + C.uv[1] * w; }
        for (int k = 0; k < 3; k++) {
            V3 p = S.P[t.v[k]];
            V3 pin = p * 0.8 + c * 0.2;
            int bt; double a, b, cc;
            nearest(pin, isl, faceN[i], bt, a, b, cc);
            const SrcVert &A = sv[si[bt * 3]], &B = sv[si[bt * 3 + 1]], &C = sv[si[bt * 3 + 2]];
            // extrapolate the UV from the inner point back to the corner using the source triangle's mapping
            SrcVert o;
            o.p[0] = (float)p.x; o.p[1] = (float)p.y; o.p[2] = (float)p.z;
            double ui0 = A.uv[0] * a + B.uv[0] * b + C.uv[0] * cc;
            double ui1 = A.uv[1] * a + B.uv[1] * b + C.uv[1] * cc;
            o.uv[0] = (float)(uc0 + (ui0 - uc0) * 1.25);
            o.uv[1] = (float)(uc1 + (ui1 - uc1) * 1.25);
            V3 n{0, 0, 0};
            for (int ti : S.vt[t.v[k]]) if (!S.T[ti].dead && dot(faceN[ti], faceN[i]) > 0.6) n = n + faceN[ti];
            n = norm(n);
            o.n[0] = (float)n.x; o.n[1] = (float)n.y; o.n[2] = (float)n.z;
            out.push_back(o);
        }
    }
    FILE* f = fopen(argv[2], "wb");
    int vc = (int)out.size(), ic = vc;
    fwrite(&vc, 4, 1, f); fwrite(&ic, 4, 1, f);
    fwrite(out.data(), sizeof(SrcVert), vc, f);
    for (int i = 0; i < ic; i++) fwrite(&i, 4, 1, f);
    fclose(f);
    fprintf(stderr, "%s: %d -> %d tris\n", argv[1], ntri, vc / 3);
    return 0;
}
