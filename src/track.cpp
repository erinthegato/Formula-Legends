#include "track.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

static const int CHUNK_PTS = 20;          // 100 m of track per chunk
static const float KERB_W = 1.3f;
static const float WALL_H = 1.1f;

static u32 shade(u32 c, float k) {
    int r = (int)((c & 255) * k), g = (int)(((c >> 8) & 255) * k), b = (int)(((c >> 16) & 255) * k);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (c & 0xFF000000) | (u32)r | ((u32)g << 8) | ((u32)b << 16);
}

// tiny deterministic random numbers (same scenery every time)
static uint32_t rngState = 1;
static float rnd() { rngState = rngState * 1664525u + 1013904223u; return (rngState >> 8) * (1.0f / 16777216.0f); }

bool Track::noRunoffCut = false;
bool Track::keepGround = false;
bool Track::noGroundCap = false;

bool Track::load(const TrackDef* d, bool scenery) {
    free();
    def = d;
    char path[96];
    snprintf(path, sizeof path, "DATA/tracks/%s.emt", d->id);
    int size = 0;
    u8* f = (u8*)sys::loadFile(path, &size);
    if (!f) { sys::log("missing %s\n", path); return false; }
    const u8* p = f;
    if (memcmp(p, "EMT1", 4) != 0) { sys::freeFile(f); return false; }
    uint32_t cnt; memcpy(&cnt, p + 4, 4); memcpy(&step, p + 8, 4); memcpy(&length, p + 12, 4); p += 16;
    n = (int)cnt;
    pts.resize(n);
    for (int i = 0; i < n; i++) {
        float v[4]; memcpy(v, p, 16); p += 16;
        pts[i].p = V3(v[0], v[1], v[2]); pts[i].hw = v[3];
    }
    float g[3]; memcpy(g, p, 12); p += 12;
    gx0 = g[0]; gy0 = g[1]; gstep = g[2];
    uint32_t nx, ny; memcpy(&nx, p, 4); memcpy(&ny, p + 4, 4); p += 8;
    gnx = (int)nx; gny = (int)ny;
    gz.resize(gnx * gny);
    memcpy(gz.data(), p, gnx * gny * 4); p += gnx * gny * 4;
    memcpy(&elevMin, p, 4); memcpy(&elevMax, p + 4, 4);
    sys::freeFile(f);

    // tangents, normals, grade, curvature
    std::vector<float> heading(n), rawCurv(n);
    for (int i = 0; i < n; i++) {
        const V3& a = pts[(i - 1 + n) % n].p; const V3& b = pts[(i + 1) % n].p;
        V3 t = norm(b - a);
        pts[i].t = t;
        V3 h = norm(V3(t.x, t.y, 0));
        pts[i].n = V3(-h.y, h.x, 0);
        heading[i] = atan2f(h.y, h.x);
        float run = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        pts[i].grade = run > 0.01f ? (b.z - a.z) / run : 0;
    }
    for (int i = 0; i < n; i++) {
        int k = 2;   // over +-10 m
        float dh = wrapPi(heading[(i + k) % n] - heading[(i - k + n) % n]);
        rawCurv[i] = dh / (2 * k * step);
    }
    for (int i = 0; i < n; i++) {
        float s = 0; int w = 3;
        for (int k = -w; k <= w; k++) s += rawCurv[(i + k + n) % n];
        pts[i].curv = s / (2 * w + 1);
    }
    for (int i = 0; i < n; i++) {
        float c = fabsf(pts[i].curv);
        // kerbs where the bend is tighter than ~400 m radius (look a little around so they run through the corner)
        float cmax = 0; for (int k = -3; k <= 3; k++) cmax = fmaxf(cmax, fabsf(pts[(i + k + n) % n].curv));
        pts[i].kerb = cmax > 1.0f / 400.0f;
        bool left = pts[i].curv > 0;
        // gravel on the outside of tight corners, wider run-off on the outside of any bend
        float outside = 14 + fminf(c * 4000.0f, 14.0f);
        pts[i].outL = left ? 12 : outside;
        pts[i].outR = left ? outside : 12;
        pts[i].gravelL = !left && cmax > 1.0f / 160.0f;
        pts[i].gravelR = left && cmax > 1.0f / 160.0f;
        if (d->street) {
            // street circuit: the walls stand right by the road, a little more room on the outside of bends
            float out2 = 2.2f + fminf(c * 900.0f, 5.0f);
            pts[i].outL = left ? 1.2f : out2;
            pts[i].outR = left ? out2 : 1.2f;
            pts[i].gravelL = pts[i].gravelR = false;
        }
    }
    // smooth run-off widths so walls don't zig-zag
    for (int pass = 0; pass < 3; pass++) {
        std::vector<TrackPoint> c = pts;
        for (int i = 0; i < n; i++) {
            pts[i].outL = (c[(i - 1 + n) % n].outL + c[i].outL * 2 + c[(i + 1) % n].outL) * 0.25f;
            pts[i].outR = (c[(i - 1 + n) % n].outR + c[i].outR * 2 + c[(i + 1) % n].outR) * 0.25f;
        }
    }
    // the pit lane and garages (left of the main straight, 370 m before the line to 270 m after) sit
    // on flat ground at road level: widen the left run-off there and keep it flat (see buildMesh)
    for (int i = 0; i < n; i++) {
        float s = i * step; if (s > length * 0.5f) s -= length;
        if (s > PIT_FROM && s < PIT_TO) { pts[i].outL = fmaxf(pts[i].outL, PIT_W); pts[i].gravelL = false; pts[i].kerb = false; }
    }
    // where another part of the lap runs close by (a hairpin's two legs, a chicane folding back), the
    // run-off on that side must stop halfway between the two roads, or one road's barrier would stand
    // across the other road. Grid of 40 m cells so this stays quick on the PSP.
    {
        const float CELL = 40.0f;
        float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
        for (auto& q : pts) { minx = fminf(minx, q.p.x); miny = fminf(miny, q.p.y); maxx = fmaxf(maxx, q.p.x); maxy = fmaxf(maxy, q.p.y); }
        int cw = (int)((maxx - minx) / CELL) + 1, ch = (int)((maxy - miny) / CELL) + 1;
        std::vector<std::vector<int>> grid(cw * ch);
        for (int i = 0; i < n; i += 2) {
            int cx = (int)((pts[i].p.x - minx) / CELL), cy = (int)((pts[i].p.y - miny) / CELL);
            grid[cy * cw + cx].push_back(i);
        }
        int skip = (int)(150.0f / step);          // parts of the lap closer than 150 m along it are the same corner
        std::vector<float> limL(n, 1e9f), limR(n, 1e9f);
        int clamped = 0;
        for (int i = 0; i < n; i++) {
            const TrackPoint& q = pts[i];
            int cx = (int)((q.p.x - minx) / CELL), cy = (int)((q.p.y - miny) / CELL);
            for (int gy = cy - 1; gy <= cy + 1; gy++) for (int gx = cx - 1; gx <= cx + 1; gx++) {
                if (gx < 0 || gy < 0 || gx >= cw || gy >= ch) continue;
                for (int j : grid[gy * cw + gx]) {
                    int dij = abs(i - j); if (dij > n / 2) dij = n - dij;
                    if (dij < skip) continue;
                    float dx = pts[j].p.x - q.p.x, dy = pts[j].p.y - q.p.y;
                    float side = dx * q.n.x + dy * q.n.y;             // + = to the left
                    float along = dx * q.t.x + dy * q.t.y;
                    if (fabsf(along) > 12.0f) continue;
                    float gap = fabsf(side) - q.hw - pts[j].hw;     // grass between the two roads
                    float lim = fmaxf(1.5f, gap * 0.5f - 0.5f);
                    if (side > 0) limL[i] = fminf(limL[i], lim); else limR[i] = fminf(limR[i], lim);
                }
            }
        }
        // spread each limit a little along the lap so the barrier bends in smoothly
        for (int pass = 0; pass < 2; pass++) {
            std::vector<float> a = limL, b = limR;
            for (int i = 0; i < n; i++)
                for (int k = -4; k <= 4; k++) {
                    int j = (i + k + n) % n; float grow = fabsf((float)k) * step * 0.35f;
                    limL[i] = fminf(limL[i], a[j] + grow); limR[i] = fminf(limR[i], b[j] + grow);
                }
        }
        for (int i = 0; i < n; i++) {
            if (noRunoffCut) break;
            if (pts[i].outL > limL[i]) { pts[i].outL = limL[i]; pts[i].gravelL = false; clamped++; }
            if (pts[i].outR > limR[i]) { pts[i].outR = limR[i]; pts[i].gravelR = false; clamped++; }
        }
        closeRunoff = clamped;
    }
    desert = !strcmp(d->id, "bahrain") || !strcmp(d->id, "abudhabi") || !strcmp(d->id, "lusail") || !strcmp(d->id, "jeddah") || !strcmp(d->id, "lasvegas");
    street = d->street;
    grassTint = desert ? RGBA(235, 205, 160) : RGBA(255, 255, 255);
    terrainTint = desert ? RGBA(225, 195, 150) : RGBA(205, 215, 200);
    if (street && !desert) { grassTint = RGBA(200, 205, 190); terrainTint = RGBA(150, 152, 150); }   // the city: parks and paving

    // map outline scale
    float mnx = 1e9f, mny = 1e9f, mxx = -1e9f, mxy = -1e9f;
    for (auto& q : pts) { mnx = fminf(mnx, q.p.x); mny = fminf(mny, q.p.y); mxx = fmaxf(mxx, q.p.x); mxy = fmaxf(mxy, q.p.y); }
    mapMinX = mnx; mapMinY = mny; mapScale = 1.0f / fmaxf(mxx - mnx, mxy - mny);

    if (!scenery) return true;
    buildMesh();
    buildProps();
    buildBoards();
    placeSpeakers();
    groupProps();
    gfx::flushCache(verts.data(), (int)(verts.size() * sizeof(WorldVertex)));
    sys::log("track %s: %d points, %.0f m, %d chunks, %d verts, %d props, %d distance boards, %d run-offs cut short\n", d->id, n, length, (int)chunks.size(), (int)verts.size(), (int)props.size(), (int)boards.size(), closeRunoff);
    return true;
}

// give the memory back (clear() alone keeps it reserved, so the biggest circuit's mesh would stay
// allocated for the rest of the session)
template <class T> static void release(std::vector<T>& v) { std::vector<T>().swap(v); }
void Track::free() {
    release(pts); release(gz); release(verts); release(chunks); release(props); release(propCells); release(boards); release(speakers); n = 0;
}

void Track::frame(float s, V3& pos, V3& tangent, V3& left, float& hw) const {
    s = wrap(s);
    float f = s / step; int i = (int)f; float t = f - i; i %= n; int j = (i + 1) % n;
    const TrackPoint &a = pts[i], &b = pts[j];
    pos = a.p * (1 - t) + b.p * t;
    tangent = norm(a.t * (1 - t) + b.t * t);
    left = norm(a.n * (1 - t) + b.n * t);
    hw = a.hw * (1 - t) + b.hw * t;
}

float Track::curvAt(float s) const {
    s = wrap(s); float f = s / step; int i = (int)f; float t = f - i; i %= n;
    return pts[i].curv * (1 - t) + pts[(i + 1) % n].curv * t;
}

float Track::gradeAt(float s) const {
    s = wrap(s); float f = s / step; int i = (int)f; float t = f - i; i %= n;
    return pts[i].grade * (1 - t) + pts[(i + 1) % n].grade * t;
}

float Track::vergeEdgeZ(int i, int side) const {
    const TrackPoint& q = pts[i];
    if (side == 0) {
        float si = i * step; if (si > length * 0.5f) si -= length;
        if (si > PIT_FROM && si < PIT_TO) return q.p.z;      // pit area: flat
    }
    float sg = side == 0 ? 1.0f : -1.0f;
    V3 o = q.p + q.n * (sg * (q.hw + (side == 0 ? q.outL : q.outR)));
    float dz = terrainZ(o.x, o.y) - q.p.z;
    return q.p.z + clampf(dz * 0.35f, -1.5f, 2.5f);
}

float Track::groundAt(float s, float d, float* slope) const {
    s = wrap(s);
    float f = s / step; int i = (int)f; float t = f - i; i %= n; int j = (i + 1) % n;
    int side = d >= 0 ? 0 : 1;
    float ad = fabsf(d);
    float z = 0, k = 0;
    for (int m = 0; m < 2; m++) {
        const TrackPoint& q = pts[m ? j : i];
        float w = side == 0 ? q.outL : q.outR;
        float in = q.hw + (q.kerb ? KERB_W : 0), out = q.hw + w;
        float zi = 0, ki = 0;
        if (ad > in && out > in + 0.1f) {
            float dz = vergeEdgeZ(m ? j : i, side) - q.p.z;
            ki = dz / (out - in);
            zi = ki * (fminf(ad, out) - in);
        }
        float wt = m ? t : 1 - t;
        z += zi * wt; k += ki * wt;
    }
    if (slope) *slope = ad > 0 ? k : 0;
    return z;
}

V3 Track::world(float s, float d, float lift) const {
    V3 p, t, l; float hw;
    frame(s, p, t, l, hw);
    return p + l * d + V3(0, 0, lift);
}

float Track::terrainZ(float x, float y) const {
    if (gnx < 2 || gny < 2) return 0;
    float fx = (x - gx0) / gstep, fy = (y - gy0) / gstep;
    fx = clampf(fx, 0, gnx - 1.001f); fy = clampf(fy, 0, gny - 1.001f);
    int ix = (int)fx, iy = (int)fy; float tx = fx - ix, ty = fy - iy;
    float a = gz[iy * gnx + ix], b = gz[iy * gnx + ix + 1], c = gz[(iy + 1) * gnx + ix], d = gz[(iy + 1) * gnx + ix + 1];
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
}

void Track::addQuad(Chunk& c, int mat, const V3& a, const V3& b, const V3& cc, const V3& d,
                    float u0, float v0, float u1, float v1, u32 col, std::vector<WorldVertex>* bins) {
    // a-b is the near edge, d-cc the far edge. Stored as 4 corners; turned into strips later.
    V3 o = c.origin;
    auto& v = bins[mat];
    v.push_back(WorldVertex{u0, v0, col, a.x - o.x, a.y - o.y, a.z - o.z});
    v.push_back(WorldVertex{u1, v0, col, b.x - o.x, b.y - o.y, b.z - o.z});
    v.push_back(WorldVertex{u1, v1, col, cc.x - o.x, cc.y - o.y, cc.z - o.z});
    v.push_back(WorldVertex{u0, v1, col, d.x - o.x, d.y - o.y, d.z - o.z});
}

static bool sameV(const WorldVertex& a, const WorldVertex& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.u == b.u && a.v == b.v && a.color == b.color;
}

// Quads (4 corners each) -> one triangle strip. Quads are chained so that each one continues
// the previous (its near edge is the previous far edge): such a quad costs 2 vertices instead of 6.
// Chains are joined with degenerate (zero-area) triangles. Back-face culling is off, so the
// alternating winding of a strip does not matter.
static void quadsToStrip(const std::vector<WorldVertex>& q, std::vector<WorldVertex>& out) {
    out.clear();
    int nq = (int)q.size() / 4;
    std::vector<char> used(nq, 0);
    // near edge -> quad (only exact matches chain, so colours / uvs never get blended)
    auto key = [](const WorldVertex& a, const WorldVertex& b) {
        uint32_t h = 2166136261u;
        const uint32_t* p = (const uint32_t*)&a; for (int i = 0; i < 6; i++) h = (h ^ p[i]) * 16777619u;
        p = (const uint32_t*)&b; for (int i = 0; i < 6; i++) h = (h ^ p[i]) * 16777619u;
        return h;
    };
    std::vector<std::pair<uint32_t, int>> near(nq);
    for (int i = 0; i < nq; i++) near[i] = {key(q[i * 4], q[i * 4 + 1]), i};
    std::sort(near.begin(), near.end());
    auto findNext = [&](const WorldVertex& d, const WorldVertex& c) {
        uint32_t k = key(d, c);
        auto it = std::lower_bound(near.begin(), near.end(), std::make_pair(k, -1));
        for (; it != near.end() && it->first == k; ++it)
            if (!used[it->second] && sameV(q[it->second * 4], d) && sameV(q[it->second * 4 + 1], c)) return it->second;
        return -1;
    };
    for (int s = 0; s < nq; s++) {
        if (used[s]) continue;
        int i = s;
        const WorldVertex& a = q[i * 4];
        if (!out.empty()) { WorldVertex last = out.back(); out.push_back(last); out.push_back(a); }
        out.push_back(a); out.push_back(q[i * 4 + 1]);
        while (i >= 0) {
            used[i] = 1;
            const WorldVertex &c = q[i * 4 + 2], &d = q[i * 4 + 3];
            out.push_back(d); out.push_back(c);
            i = findNext(d, c);
        }
    }
}

// street circuits: city blocks behind the walls, one every ~40 m each side where there is room
void Track::addBuildings(Chunk& ch, int c0, int c1, std::vector<WorldVertex>* bins) {
    uint32_t sd = 7771u + (uint32_t)c0 * 131u + (uint32_t)(length);
    auto rr = [&]() { sd = sd * 1664525u + 1013904223u; return (sd >> 8) * (1.0f / 16777216.0f); };
    bool tall = !strcmp(def->id, "singapore") || !strcmp(def->id, "lasvegas") || !strcmp(def->id, "monaco") || !strcmp(def->id, "baku");
    static const u32 COL[8] = {RGBA(214, 204, 186), RGBA(232, 228, 218), RGBA(176, 170, 160), RGBA(120, 150, 176),
                               RGBA(196, 170, 140), RGBA(150, 160, 170), RGBA(222, 196, 160), RGBA(90, 120, 150)};
    for (int i = c0; i < c1; i += 8) {
        const TrackPoint& q = pts[i];
        for (int side = 0; side < 2; side++) {
            if (rr() < 0.25f) continue;
            float sg = side == 0 ? 1.0f : -1.0f;
            float s = i * step; if (s > length * 0.5f) s -= length;
            if (side == 0 && s > PIT_FROM - 60 && s < PIT_TO + 60) continue;      // the pits
            float gap = 7 + rr() * 10, depth = 14 + rr() * 16, along = 18 + rr() * 16;
            float h = (tall ? 14 + rr() * 50 : 9 + rr() * 16);
            float off = q.hw + (side == 0 ? q.outL : q.outR) + gap;
            V3 c = q.p + q.n * (sg * (off + depth * 0.5f));
            // clear of every part of the track
            bool clear = true;
            float rad = 0.5f * sqrtf(depth * depth + along * along);
            for (int j = 0; j < n && clear; j += 3) {
                float dx = pts[j].p.x - c.x, dy = pts[j].p.y - c.y;
                float need = pts[j].hw + fmaxf(pts[j].outL, pts[j].outR) + rad + 4;
                if (dx * dx + dy * dy < need * need) clear = false;
            }
            if (!clear) continue;
            float z0 = q.p.z - 0.6f;
            V3 ax = q.t * (along * 0.5f), ay = q.n * (depth * 0.5f);
            ax.z = 0; ay.z = 0;
            V3 p00 = c - ax - ay, p10 = c + ax - ay, p11 = c + ax + ay, p01 = c - ax + ay;
            p00.z = p10.z = p11.z = p01.z = z0;
            V3 up(0, 0, h);
            u32 col = COL[(int)(rr() * 8) & 7];
            auto sh = [&](float k) { return RGBA((int)((col & 255) * k), (int)(((col >> 8) & 255) * k), (int)(((col >> 16) & 255) * k)); };
            addQuad(ch, MAT_WALL, p00, p10, p10 + up, p00 + up, 0, 0, 1, 1, sh(0.86f), bins);
            addQuad(ch, MAT_WALL, p10, p11, p11 + up, p10 + up, 0, 0, 1, 1, sh(0.72f), bins);
            addQuad(ch, MAT_WALL, p11, p01, p01 + up, p11 + up, 0, 0, 1, 1, sh(0.80f), bins);
            addQuad(ch, MAT_WALL, p01, p00, p00 + up, p01 + up, 0, 0, 1, 1, sh(0.66f), bins);
            addQuad(ch, MAT_WALL, p00 + up, p10 + up, p11 + up, p01 + up, 0, 0, 1, 1, sh(1.0f), bins);
            // rows of windows on the faces towards the track (dark bands, every 3.5 m)
            V3 face0 = sg > 0 ? p00 : p01, face1 = sg > 0 ? p10 : p11;
            V3 outn = q.n * (-sg * 0.06f); outn.z = 0;
            for (float z = 2.5f; z < h - 1.5f; z += 3.5f)
                addQuad(ch, MAT_WALL, face0 + outn + V3(0, 0, z), face1 + outn + V3(0, 0, z), face1 + outn + V3(0, 0, z + 1.4f), face0 + outn + V3(0, 0, z + 1.4f), 0, 0, 1, 1, RGBA(40, 52, 66), bins);
        }
    }
}

void Track::buildMesh() {
    verts.clear(); chunks.clear();
    // where the verge meets the land
    auto edgeZ = [&](const V3& base, const V3& outer) {
        float dz = terrainZ(outer.x, outer.y) - base.z;
        return base.z + clampf(dz * 0.35f, -1.5f, 2.5f);      // same as vergeEdgeZ (the cars drive on this)
    };
    std::vector<WorldVertex> bins[MAT_COUNT];
    for (int c0 = 0; c0 < n; c0 += CHUNK_PTS) {
        Chunk ch;
        ch.origin = pts[c0].p;
        for (auto& b : bins) b.clear();
        int c1 = c0 + CHUNK_PTS; if (c1 > n) c1 = n;
        for (int i = c0; i < c1; i++) {
            int j = (i + 1) % n;
            const TrackPoint &A = pts[i], &B = pts[j];
            float sA = i * step, sB = (i + 1) * step;
            V3 lA = A.p + A.n * A.hw, rA = A.p - A.n * A.hw, lB = B.p + B.n * B.hw, rB = B.p - B.n * B.hw;
            u32 roadColA = shade(0xFFFFFFFF, 0.92f + 0.08f * clampf(A.grade * 5, -1, 1));
            u32 roadColB = shade(0xFFFFFFFF, 0.92f + 0.08f * clampf(B.grade * 5, -1, 1));
            addQuad(ch, MAT_ROAD, lA, rA, rB, lB, 0, sA / 14.0f, 1, sB / 14.0f, roadColA, bins);
            { auto& rv = bins[MAT_ROAD]; rv[rv.size() - 1].color = roadColB; rv[rv.size() - 2].color = roadColB; }
            // start/finish line
            if (i == 0) {
                V3 l2 = lA + A.t * 1.0f, r2 = rA + A.t * 1.0f, lift(0, 0, 0.05f);
                addQuad(ch, MAT_LINE, lA + lift, rA + lift, r2 + lift, l2 + lift, 0, 0, 1, 1, RGBA(240, 240, 240), bins);
            }
            // painted edge lines
            {
                V3 lift(0, 0, 0.04f);   // lifted clear of the tarmac (the PSP's depth buffer is 16-bit)
                addQuad(ch, MAT_LINE, lA + lift, lA - A.n * 0.25f + lift, lB - B.n * 0.25f + lift, lB + lift, 0, 0, 1, 1, RGBA(225, 225, 225), bins);
                addQuad(ch, MAT_LINE, rA + A.n * 0.25f + lift, rA + lift, rB + lift, rB + B.n * 0.25f + lift, 0, 0, 1, 1, RGBA(225, 225, 225), bins);
            }
            for (int side = 0; side < 2; side++) {
                float sg = side == 0 ? 1.0f : -1.0f;
                V3 eA = side == 0 ? lA : rA, eB = side == 0 ? lB : rB;
                V3 nA = A.n * sg, nB = B.n * sg;
                bool kerb = A.kerb || B.kerb;
                V3 kA = eA, kB = eB;
                if (kerb) {
                    V3 up(0, 0, 0.06f);
                    kA = eA + nA * KERB_W; kB = eB + nB * KERB_W;
                    if (side == 0) addQuad(ch, MAT_KERB, kA + up, eA + up, eB + up, kB + up, 0, sA / 6.0f, 1, sB / 6.0f, 0xFFFFFFFF, bins);
                    else addQuad(ch, MAT_KERB, eA + up, kA + up, kB + up, eB + up, 0, sA / 6.0f, 1, sB / 6.0f, 0xFFFFFFFF, bins);
                }
                float wA = side == 0 ? A.outL : A.outR, wB = side == 0 ? B.outL : B.outR;
                bool gravel = side == 0 ? (A.gravelL || B.gravelL) : (A.gravelR || B.gravelR);
                V3 oA = eA + nA * wA, oB = eB + nB * wB;
                oA.z = edgeZ(A.p, oA); oB.z = edgeZ(B.p, oB);
                if (side == 0) {
                    float sa = sA > length * 0.5f ? sA - length : sA, sb = sB > length * 0.5f ? sB - length : sB;
                    if (sa > PIT_FROM && sa < PIT_TO) oA.z = A.p.z;
                    if (sb > PIT_FROM && sb < PIT_TO) oB.z = B.p.z;
                }
                int mat = gravel ? MAT_GRAVEL : MAT_GRASS;
                u32 col = gravel ? RGBA(235, 225, 205) : grassTint;
                if (street) { mat = MAT_ROAD; col = RGBA(150, 150, 152); }     // pavement up to the wall
                float s = 1.0f / 10.0f;
                if (side == 0) addQuad(ch, mat, oA, kA, kB, oB, 0, 0, 0, 0, col, bins);
                else addQuad(ch, mat, kA, oA, oB, kB, 0, 0, 0, 0, col, bins);
                // world-space texture coordinates (no stretching on wide run-offs)
                auto& v = bins[mat];
                for (int k = (int)v.size() - 4; k < (int)v.size(); k++) { v[k].u = (v[k].x + ch.origin.x) * s; v[k].v = (v[k].y + ch.origin.y) * s; }
                // barrier at the edge of the run-off (two-sided, sunk into the ground a little)
                V3 dn(0, 0, -0.8f), up(0, 0, street ? 1.25f : WALL_H);
                bool stripe = ((i / 4) & 1) != 0;
                u32 wc = stripe ? RGBA(205, 210, 220) : RGBA(170, 178, 192);
                if (street) wc = stripe ? RGBA(230, 230, 232) : RGBA(200, 40, 40);    // painted concrete blocks
                addQuad(ch, MAT_WALL, oA + dn, oA + up, oB + up, oB + dn, 0, 0, 1, 1, wc, bins);
                if (street) {
                    // debris fence above the wall
                    V3 f0(0, 0, 1.25f), f1(0, 0, 3.6f);
                    if (((i / 2) & 1) == 0) addQuad(ch, MAT_WALL, oA + f0, oA + f1, oA + f1 + nA * 0.05f, oA + f0 + nA * 0.05f, 0, 0, 1, 1, RGBA(90, 95, 100), bins);
                    addQuad(ch, MAT_WALL, oA + f1, oA + f1 + V3(0, 0, 0.08f), oB + f1 + V3(0, 0, 0.08f), oB + f1, 0, 0, 1, 1, RGBA(90, 95, 100), bins);
                }
            }
        }
        if (street) addBuildings(ch, c0, c1, bins);
        V3 mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
        std::vector<WorldVertex> strip;
        for (int m = 0; m < MAT_COUNT; m++) {
            quadsToStrip(bins[m], strip);
            ch.first[m] = (int)verts.size(); ch.count[m] = (int)strip.size();
            for (auto& w : strip) {
                verts.push_back(w);
                mn = V3(fminf(mn.x, w.x), fminf(mn.y, w.y), fminf(mn.z, w.z));
                mx = V3(fmaxf(mx.x, w.x), fmaxf(mx.y, w.y), fmaxf(mx.z, w.z));
            }
        }
        ch.centre = ch.origin + (mn + mx) * 0.5f; ch.radius = len(mx - mn) * 0.5f;
        chunks.push_back(ch);
    }

    // ---------------- terrain: the elevation grid, 4x finer, lowered under the circuit
    const int SUB = 4;
    float sub = gstep / SUB;
    // nearest track point lookup through a coarse bucket grid
    const float B = 60.0f;
    float bx0 = gx0 - 100, by0 = gy0 - 100;
    int bnx = (int)((gnx * gstep + 200) / B) + 1, bny = (int)((gny * gstep + 200) / B) + 1;
    std::vector<std::vector<int>> buckets(bnx * bny);
    for (int i = 0; i < n; i++) {
        int bx = (int)((pts[i].p.x - bx0) / B), by = (int)((pts[i].p.y - by0) / B);
        if (bx >= 0 && by >= 0 && bx < bnx && by < bny) buckets[by * bnx + bx].push_back(i);
    }
    auto groundZ = [&](float x, float y) {
        float dem = terrainZ(x, y);
        int bx = (int)((x - bx0) / B), by = (int)((y - by0) / B);
        float best = 1e9f; int bi = -1;
        for (int yy = by - 2; yy <= by + 2; yy++) for (int xx = bx - 2; xx <= bx + 2; xx++) {
            if (xx < 0 || yy < 0 || xx >= bnx || yy >= bny) continue;
            for (int i : buckets[yy * bnx + xx]) {
                float dx = pts[i].p.x - x, dy = pts[i].p.y - y, d = dx * dx + dy * dy;
                if (d < best) { best = d; bi = i; }
            }
        }
        if (bi < 0) return dem;
        float d = sqrtf(best);
        float under = pts[bi].p.z - 1.2f;
        if (d < 45) return fminf(dem, under) * 0.0f + under;
        float t = clampf((d - 45) / 90.0f, 0, 1);
        return lerpf(under, dem, t * t * (3 - 2 * t));
    };
    int tnx = (gnx - 1) * SUB + 1, tny = (gny - 1) * SUB + 1;
    std::vector<float> tz(tnx * tny);
    for (int y = 0; y < tny; y++) for (int x = 0; x < tnx; x++) tz[y * tnx + x] = groundZ(gx0 + x * sub, gy0 + y * sub);
    // The mesh is linear over 50 m cells, so a corner set from its nearest track point can still poke
    // up through the road where the road dips inside the cell, or where another part of the lap passes
    // lower. So every corner of a cell that holds a bit of road (or verge) is kept below that bit.
    if (!noGroundCap) {
        auto cap = [&](float x, float y, float z) {
            float fx = (x - gx0) / sub, fy = (y - gy0) / sub;
            if (fx < 0 || fy < 0 || fx > tnx - 1.001f || fy > tny - 1.001f) return;
            int ix = (int)fx, iy = (int)fy;
            for (int yy = iy; yy <= iy + 1; yy++) for (int xx = ix; xx <= ix + 1; xx++) {
                float& c = tz[yy * tnx + xx];
                if (c > z) c = z;
            }
        };
        for (int i = 0; i < n; i++) {
            const TrackPoint& q = pts[i];
            float si = i * step; if (si > length * 0.5f) si -= length;
            bool pit = si > PIT_FROM && si < PIT_TO;
            for (int side = 0; side < 2; side++) {
                float sg = side == 0 ? 1.0f : -1.0f;
                V3 nn = q.n * sg;
                for (float d = 0; d < q.hw + 0.5f; d += 3.0f) { V3 p = q.p + nn * d; cap(p.x, p.y, q.p.z - 1.0f); }
                V3 e = q.p + nn * q.hw; cap(e.x, e.y, q.p.z - 1.0f);
                float w = side == 0 ? q.outL : q.outR;
                V3 o = e + nn * w;
                float oz = (side == 0 && pit) ? q.p.z : edgeZ(q.p, o);
                for (float t = 0.25f; t <= 1.001f; t += 0.25f) {
                    V3 p = e + nn * (w * t);
                    cap(p.x, p.y, q.p.z + (oz - q.p.z) * t - 0.5f);
                }
            }
        }
    }
    if (keepGround) { groundTz = tz; gtnx = tnx; gtny = tny; gsub = sub; }
    // light per terrain corner from the slope (neighbouring cells share corners -> strips)
    auto tcol = [&](int x, int y) {
        int x0 = x > 0 ? x - 1 : x, x1 = x < tnx - 1 ? x + 1 : x, y0 = y > 0 ? y - 1 : y, y1 = y < tny - 1 ? y + 1 : y;
        V3 dx((x1 - x0) * sub, 0, tz[y * tnx + x1] - tz[y * tnx + x0]), dy(0, (y1 - y0) * sub, tz[y1 * tnx + x] - tz[y0 * tnx + x]);
        V3 nrm = norm(cross(dx, dy));
        float k = 0.78f + 0.22f * clampf(dot(nrm, norm(V3(0.4f, 0.3f, 0.85f))), 0, 1);
        return shade(terrainTint, k);
    };
    std::vector<WorldVertex> quads, strip;
    for (int cy = 0; cy < gny - 1; cy++) for (int cx = 0; cx < gnx - 1; cx++) {
        Chunk ch;
        ch.origin = V3(gx0 + cx * gstep, gy0 + cy * gstep, tz[(cy * SUB) * tnx + cx * SUB]);
        for (int m = 0; m < MAT_COUNT; m++) { ch.first[m] = (int)verts.size(); ch.count[m] = 0; }
        float zmin = 1e9f, zmax = -1e9f;
        quads.clear();
        V3 o = ch.origin;
        const float s = 1.0f / 25.0f;
        auto vert = [&](int x, int y) {
            V3 p(gx0 + x * sub, gy0 + y * sub, tz[y * tnx + x]);
            zmin = fminf(zmin, p.z); zmax = fmaxf(zmax, p.z);
            return WorldVertex{p.x * s, p.y * s, tcol(x, y), p.x - o.x, p.y - o.y, p.z - o.z};
        };
        for (int sy = 0; sy < SUB; sy++) for (int sx = 0; sx < SUB; sx++) {
            int x = cx * SUB + sx, y = cy * SUB + sy;
            // near edge (x, y)-(x, y+1), far edge (x+1, y)-(x+1, y+1): cells in a row chain into one strip
            quads.push_back(vert(x, y)); quads.push_back(vert(x, y + 1));
            quads.push_back(vert(x + 1, y + 1)); quads.push_back(vert(x + 1, y));
        }
        quadsToStrip(quads, strip);
        ch.first[MAT_TERRAIN] = (int)verts.size(); ch.count[MAT_TERRAIN] = (int)strip.size();
        verts.insert(verts.end(), strip.begin(), strip.end());
        ch.centre = V3(ch.origin.x + gstep * 0.5f, ch.origin.y + gstep * 0.5f, (zmin + zmax) * 0.5f);
        ch.radius = gstep * 0.75f + (zmax - zmin) * 0.5f;
        chunks.push_back(ch);
    }
}

// distance boards (300 / 200 / 100 m) before every sharp corner that has a straight leading into it
void Track::buildBoards() {
    boards.clear();
    const float SHARP = 1.0f / 75, BEND = 1.0f / 400, STRAIGHT = 1.0f / 220;
    int lastEntry = -1;
    for (int i = 0; i < n; i++) {
        float c = fabsf(pts[i].curv);
        if (c < SHARP) continue;
        // the tightest point of this corner (nothing tighter within 50 m either way)
        bool peak = true;
        for (int k = -10; k <= 10 && peak; k++) if (k && fabsf(pts[(i + k + n) % n].curv) > c) peak = false;
        if (!peak) continue;
        // turn-in: walk back to where the bend starts
        int e = i, back = 0;
        while (fabsf(pts[(e - 1 + n) % n].curv) > BEND && back < 80) { e = (e - 1 + n) % n; back++; }
        if (e == lastEntry) continue;
        lastEntry = e;
        // how much straight comes before it
        float straight = 0;
        for (int k = 1; k * step <= 330; k++) {
            if (fabsf(pts[(e - k + n) % n].curv) > STRAIGHT) break;
            straight = k * step;
        }
        float sEntry = e * step;
        bool left = pts[i].curv > 0;
        for (int m = 100; m <= 300; m += 100) {
            if (m + 10 > straight) break;
            float s = wrap(sEntry - m);
            const TrackPoint& q = pts[index(s)];
            float sg = left ? -1.0f : 1.0f;           // on the outside of the coming corner
            // the pit lane runs on the left of the main straight: board on the right there
            float fromLine = s > length * 0.5f ? s - length : s;
            if (sg > 0 && fromLine > PIT_FROM - 40 && fromLine < PIT_TO + 40) sg = -1;
            float out = sg > 0 ? q.outL : q.outR;
            V3 at = q.p + q.n * (sg * (q.hw + fminf(out, 6.0f) + 1.5f));
            at.z = q.p.z + clampf((terrainZ(at.x, at.y) - q.p.z) * 0.35f, -1.5f, 2.5f);
            boards.push_back({at, atan2f(-q.t.y, -q.t.x), m});
        }
    }
}

void Track::placeSpeakers() {
    speakers.clear();
    // on the grandstands (8 m up)
    for (auto& p : props) if (p.model == PROP_GRANDSTAND) speakers.push_back(p.p + V3(0, 0, 8));
    // on poles every ~300 m on the outside of the run-off (4 m up)
    for (int i = 0; i < n; i += (int)(300 / step)) {
        const TrackPoint& q = pts[i];
        bool leftSide = (i / (int)(300 / step)) % 2 == 0;
        V3 at = q.p + q.n * ((leftSide ? 1 : -1) * (q.hw + (leftSide ? q.outL : q.outR) + 2));
        at.z = q.p.z + 4;
        speakers.push_back(at);
    }
}

void Track::groupProps() {
    const float CELL = 200.0f;
    std::sort(props.begin(), props.end(), [&](const PropInst& a, const PropInst& b) {
        int ax = (int)floorf(a.p.x / CELL), ay = (int)floorf(a.p.y / CELL), bx = (int)floorf(b.p.x / CELL), by = (int)floorf(b.p.y / CELL);
        return ay != by ? ay < by : ax < bx;
    });
    propCells.clear();
    for (int i = 0; i < (int)props.size();) {
        int cx = (int)floorf(props[i].p.x / CELL), cy = (int)floorf(props[i].p.y / CELL);
        int j = i;
        V3 mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
        while (j < (int)props.size() && (int)floorf(props[j].p.x / CELL) == cx && (int)floorf(props[j].p.y / CELL) == cy) {
            const V3& p = props[j].p;
            mn = V3(fminf(mn.x, p.x), fminf(mn.y, p.y), fminf(mn.z, p.z)); mx = V3(fmaxf(mx.x, p.x), fmaxf(mx.y, p.y), fmaxf(mx.z, p.z));
            j++;
        }
        // +30 m: the biggest prop (grandstand) around its base point
        propCells.push_back({(mn + mx) * 0.5f + V3(0, 0, 8), len(mx - mn) * 0.5f + 30.0f, i, j - i});
        i = j;
    }
}

// ---- the trackside objects made in Blender (tools/blender/trackside.py): +X of each model faces the track
void Track::addTrackside(std::vector<V3>& keepClear) {
    // nothing may stand on another part of the lap (circuits fold back on themselves)
    auto clearOfTrack = [&](const V3& at, float margin) {
        for (int j = 0; j < n; j += 2) {
            float dx = pts[j].p.x - at.x, dy = pts[j].p.y - at.y;
            float need = pts[j].hw + fmaxf(pts[j].outL, pts[j].outR) + margin;
            if (dx * dx + dy * dy < need * need) return false;
        }
        return true;
    };
    auto ground = [&](const TrackPoint& q, const V3& at) { return q.p.z + clampf((terrainZ(at.x, at.y) - q.p.z) * 0.35f, -1.5f, 2.5f); };
    auto awayFrom = [&](const std::vector<float>& used, float s, float gap) {
        for (float u : used) { float d = fabsf(s - u); d = fminf(d, length - d); if (d < gap) return false; }
        return true;
    };
    auto inPits = [&](float s) { return s < 260 || s > length - 420; };
    // the start gantry 8 m past the line, its pillars just off the road (the start lights are drawn on its panel)
    {
        V3 p, t, l; float hw; frame(8.0f, p, t, l, hw);
        gantryScale = (pts[index(8)].hw + 0.8f) / 10.0f;
        props.push_back({PROP_GANTRY, p, atan2f(l.y, l.x), gantryScale});
    }
    // corners, tightest first: local peaks of curvature
    std::vector<int> peaks;
    for (int i = 0; i < n; i++) {
        float c = fabsf(pts[i].curv);
        if (c > 1.0f / 400 && c >= fabsf(pts[(i + n - 1) % n].curv) && c >= fabsf(pts[(i + 1) % n].curv)) peaks.push_back(i);
    }
    std::sort(peaks.begin(), peaks.end(), [&](int a, int b) { return fabsf(pts[a].curv) > fabsf(pts[b].curv); });
    // fans: two bleachers and a tent behind the run-off of up to five slow corners
    std::vector<float> fans;
    for (int i : peaks) {
        if ((int)fans.size() >= 5 || fabsf(pts[i].curv) < 1.0f / 160) break;
        float s = i * step;
        if (inPits(s) || !awayFrom(fans, s, 380)) continue;
        const TrackPoint& q = pts[i];
        float sg = q.curv > 0 ? -1.0f : 1.0f;                // outside of the bend
        float w = q.curv > 0 ? q.outR : q.outL;
        V3 out = q.n * sg;
        float yaw = atan2f(-out.y, -out.x);                  // facing the corner
        V3 at[3] = {q.p + out * (q.hw + w + 14) - q.t * 5.6f, q.p + out * (q.hw + w + 14) + q.t * 5.6f,
                    q.p + out * (q.hw + w + 22) + q.t * 15.0f};
        bool ok = true;
        for (const V3& a : at) if (!clearOfTrack(a, 8)) ok = false;
        if (!ok) continue;
        for (int k = 0; k < 3; k++) {
            V3 a = at[k]; a.z = ground(q, a);
            props.push_back({k < 2 ? PROP_BLEACHER : PROP_TENT, a, yaw, 1.0f});
            keepClear.push_back(a);
        }
        fans.push_back(s);
    }
    // TecPro barriers lining the outside of medium-speed corners (tight ones already have tyre walls)
    int barriers = 0;
    std::vector<float> lined;
    for (int i : peaks) {
        float c = fabsf(pts[i].curv);
        if (c >= 1.0f / 160) continue;
        if (barriers >= 8) break;
        float s0 = i * step;
        if (inPits(s0) || !awayFrom(lined, s0, 150)) continue;
        for (float ds = -30; ds <= 30; ds += 6) {
            const TrackPoint& q = pts[index(s0 + ds)];
            float sg = pts[i].curv > 0 ? -1.0f : 1.0f;
            float w = pts[i].curv > 0 ? q.outR : q.outL;
            V3 out = q.n * sg;
            V3 a = q.p + out * (q.hw + w + 0.5f);
            a.z = ground(q, a);
            props.push_back({PROP_TECPRO, a, atan2f(-out.y, -out.x), 1.0f});
        }
        lined.push_back(s0);
        barriers++;
    }
    // recovery cranes behind the run-off of three corners
    std::vector<float> cranes;
    for (int i : peaks) {
        if ((int)cranes.size() >= 3) break;
        float s = i * step;
        if (inPits(s) || !awayFrom(cranes, s, length * 0.2f) || !awayFrom(fans, s, 60)) continue;
        const TrackPoint& q = pts[i];
        float sg = q.curv > 0 ? -1.0f : 1.0f;
        float w = q.curv > 0 ? q.outR : q.outL;
        V3 out = q.n * sg;
        V3 a = q.p + out * (q.hw + w + 6) + q.t * 18.0f;
        if (!clearOfTrack(a, 4)) continue;
        a.z = ground(q, a);
        props.push_back({PROP_CRANE, a, atan2f(-out.y, -out.x), 1.0f});
        keepClear.push_back(a);
        cranes.push_back(s);
    }
    // marshal light panels every ~300 m, sides taking turns, not inside tight corners
    float last = -1000;
    bool leftSide = true;
    for (int i = 0; i < n; i++) {
        float s = i * step;
        const TrackPoint& q = pts[i];
        if (s - last < 300 || inPits(s) || fabsf(q.curv) > 1.0f / 300) continue;
        float sg = leftSide ? 1.0f : -1.0f;
        V3 out = q.n * sg;
        V3 a = q.p + out * (q.hw + (leftSide ? q.outL : q.outR) + 1.0f);
        a.z = ground(q, a);
        props.push_back({PROP_FLAGPANEL, a, atan2f(-out.y, -out.x), 1.0f});
        last = s; leftSide = !leftSide;
    }
}

void Track::buildProps() {
    props.clear();
    rngState = 12345;
    for (const char* c = def->id; *c; c++) rngState = rngState * 31 + (u8)*c;
    // grandstands along the start/finish straight, on the outside, facing the track
    for (float s = -260; s <= 160; s += 34) {
        float ss = wrap(s);
        int i = index(ss);
        if (fabsf(pts[i].curv) > 1.0f / 300) continue;
        V3 p, t, l; float hw; frame(ss, p, t, l, hw);
        float off = hw + pts[i].outR + 12;
        V3 at = p - l * off;
        float yaw = atan2f(l.y, l.x);    // face the track (+X of the model towards the circuit)
        props.push_back({PROP_GRANDSTAND, at, yaw, 1.0f});
    }
    // tyre walls on the outside of tight corners, marshal posts every ~400 m, billboards on straights
    float lastMarshal = -1000, lastBoard = -1000;
    for (int i = 0; i < n; i += 2) {
        const TrackPoint& q = pts[i];
        float s = i * step;
        bool left = q.curv > 0;
        if (fabsf(q.curv) > 1.0f / 160 && (i % 4) == 0) {
            float sg = left ? -1.0f : 1.0f;  // outside
            float w = left ? q.outR : q.outL;
            V3 at = q.p + q.n * (sg * (q.hw + w - 1.0f));
            at.z = terrainZ(at.x, at.y) * 0.0f + q.p.z + clampf((terrainZ(at.x, at.y) - q.p.z) * 0.35f, -1.5f, 2.5f);
            props.push_back({PROP_TYRES, at, rnd() * 6.28f, 1.0f});
        }
        if (s - lastMarshal > 420 && fabsf(q.curv) < 1.0f / 500) {
            lastMarshal = s;
            V3 at = q.p + q.n * (q.hw + q.outL + 3);
            at.z = q.p.z + clampf((terrainZ(at.x, at.y) - q.p.z) * 0.35f, -1.5f, 2.5f);
            props.push_back({PROP_MARSHAL, at, atan2f(-q.n.y, -q.n.x), 1.0f});
        }
        if (s - lastBoard > 260 && fabsf(q.curv) < 1.0f / 800 && s > 300) {
            lastBoard = s;
            V3 at = q.p - q.n * (q.hw + q.outR + 2);
            at.z = q.p.z + clampf((terrainZ(at.x, at.y) - q.p.z) * 0.35f, -1.5f, 2.5f);
            props.push_back({PROP_BILLBOARD, at, atan2f(q.t.y, q.t.x), 1.0f});
        }
    }
    // ---- buildings made with Unity AI (drawn where the model exists: the Vita)
    std::vector<V3> keepClear;   // trees stay away from these
    {
        // race control: just after the last pit garage (garages end ~105 m after the line), left of the straight
        V3 p, t, l; float hw; frame(wrap(140), p, t, l, hw);
        V3 at = p + l * (pts[0].hw + 22); at.z = p.z;
        props.push_back({PROP_CONTROL, at, atan2f(-l.y, -l.x), 1.0f});
        keepClear.push_back(at);
        // team hospitality units in a row behind the garages (pit lane: 350 m before to 250 m after the line)
        for (float s0 = -185; s0 <= 95; s0 += 31) {
            frame(wrap(s0), p, t, l, hw);
            at = p + l * (pts[0].hw + 36); at.z = p.z;
            props.push_back({PROP_HOSPITALITY, at, atan2f(-l.y, -l.x), 1.0f});
            keepClear.push_back(at);
        }
        // advertising footbridges over two long straights, well away from the pits
        float lastBridge = -1e9f;
        int bridges = 0;
        for (int i = 0; i < n && bridges < 2; i++) {
            float s = i * step;
            if (s < 500 || s > length - 600 || s - lastBridge < length * 0.3f) continue;
            bool straight = true;
            for (float k = -45; k <= 45 && straight; k += 5) if (fabsf(pts[index(s + k)].curv) > 1.0f / 900) straight = false;
            if (!straight) continue;
            const TrackPoint& q = pts[i];
            float span = 2 * (q.hw + fmaxf(q.outL, q.outR) + 3);
            props.push_back({PROP_BRIDGE, V3(q.p.x, q.p.y, q.p.z), atan2f(q.n.y, q.n.x), span / 30.0f});
            keepClear.push_back(q.p);
            lastBridge = s; bridges++;
        }
        // TV camera towers on the outside of the tightest corners
        std::vector<int> sharp;
        for (int i = 0; i < n; i++)
            if (fabsf(pts[i].curv) > 1.0f / 110 && fabsf(pts[i].curv) >= fabsf(pts[(i + n - 1) % n].curv) && fabsf(pts[i].curv) >= fabsf(pts[(i + 1) % n].curv)) sharp.push_back(i);
        std::sort(sharp.begin(), sharp.end(), [&](int a, int b) { return fabsf(pts[a].curv) > fabsf(pts[b].curv); });
        std::vector<float> used;
        for (int i : sharp) {
            if ((int)used.size() >= 6) break;
            float s = i * step;
            bool far = true;
            for (float u : used) { float d = fabsf(s - u); d = fminf(d, length - d); if (d < 450) far = false; }
            if (!far || s < 300 || s > length - 400) continue;
            const TrackPoint& q = pts[i];
            float sg = q.curv > 0 ? -1.0f : 1.0f;   // outside of the bend
            float w = q.curv > 0 ? q.outR : q.outL;
            V3 at = q.p + q.n * (sg * (q.hw + w + 3));
            at.z = q.p.z + clampf((terrainZ(at.x, at.y) - q.p.z) * 0.35f, -1.5f, 2.5f);
            props.push_back({PROP_TVTOWER, at, atan2f(-q.n.y * sg, -q.n.x * sg), 1.0f});   // looking back at the corner
            keepClear.push_back(at);
            used.push_back(s);
        }
        addTrackside(keepClear);
    }
    // trees in the land around the circuit (not in the desert; few in the city)
    if (!desert) {
        int want = (int)(length / (street ? 40 : 9));
        for (int k = 0; k < want; k++) {
            int i = (int)(rnd() * n) % n;
            const TrackPoint& q = pts[i];
            float side = rnd() < 0.5f ? 1.0f : -1.0f;
            float dist = q.hw + (side > 0 ? q.outL : q.outR) + 10 + rnd() * 140;
            V3 at = q.p + q.n * (side * dist) + q.t * ((rnd() - 0.5f) * 20);
            // keep clear of every part of the track (circuits fold back on themselves)
            bool clear = true;
            for (int j = 0; j < n && clear; j += 3) {
                float dx = pts[j].p.x - at.x, dy = pts[j].p.y - at.y;
                float need = pts[j].hw + fmaxf(pts[j].outL, pts[j].outR) + 8;
                if (dx * dx + dy * dy < need * need) clear = false;
            }
            for (const V3& b : keepClear) { float dx = b.x - at.x, dy = b.y - at.y; if (dx * dx + dy * dy < 22.0f * 22.0f) clear = false; }
            if (!clear) continue;
            float dz = 0;
            {
                // same ground height the terrain mesh uses near the track
                float dem = terrainZ(at.x, at.y);
                float d = dist;
                float under = q.p.z - 1.2f;
                float t = clampf((d - 45) / 90.0f, 0, 1);
                dz = d < 45 ? under : lerpf(under, dem, t * t * (3 - 2 * t));
            }
            at.z = dz;
            props.push_back({rnd() < 0.55f ? PROP_TREE_ROUND : PROP_TREE_PINE, at, rnd() * 6.28f, 0.75f + rnd() * 0.6f});
        }
    }
}
