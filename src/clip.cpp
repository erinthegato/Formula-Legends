#include "clip.h"

namespace {
M4 P = M4::identity(), V = M4::identity();
// guard band: keep corners within 3x the screen half-size (the chip allows about 8x)
const float K = 3.0f;
const float NEAR_EPS = 0.02f;

struct CV { float c[4]; WorldVertex w; };

inline void toClip(const M4& mvp, const WorldVertex& w, float* o) {
    o[0] = mvp.m[0] * w.x + mvp.m[4] * w.y + mvp.m[8] * w.z + mvp.m[12];
    o[1] = mvp.m[1] * w.x + mvp.m[5] * w.y + mvp.m[9] * w.z + mvp.m[13];
    o[2] = mvp.m[2] * w.x + mvp.m[6] * w.y + mvp.m[10] * w.z + mvp.m[14];
    o[3] = mvp.m[3] * w.x + mvp.m[7] * w.y + mvp.m[11] * w.z + mvp.m[15];
}

// signed distance to plane p (>= 0 inside)
inline float planeD(const float* c, int p) {
    switch (p) {
    case 0: return c[2] + c[3] - NEAR_EPS;   // near
    case 1: return K * c[3] - c[0];           // right
    case 2: return K * c[3] + c[0];           // left
    case 3: return K * c[3] - c[1];           // top
    default: return K * c[3] + c[1];          // bottom
    }
}

inline u32 lerpCol(u32 a, u32 b, float t) {
    u32 r = 0;
    for (int s = 0; s < 32; s += 8) {
        float x = ((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t;
        r |= ((u32)(x + 0.5f) & 255) << s;
    }
    return r;
}

inline CV lerp(const CV& a, const CV& b, float t) {
    CV r;
    for (int i = 0; i < 4; i++) r.c[i] = a.c[i] + (b.c[i] - a.c[i]) * t;
    r.w.x = a.w.x + (b.w.x - a.w.x) * t; r.w.y = a.w.y + (b.w.y - a.w.y) * t; r.w.z = a.w.z + (b.w.z - a.w.z) * t;
    r.w.u = a.w.u + (b.w.u - a.w.u) * t; r.w.v = a.w.v + (b.w.v - a.w.v) * t;
    r.w.color = lerpCol(a.w.color, b.w.color, t);
    return r;
}

// clips a list of triangles; writes the result to frame memory and draws it
void run(const WorldVertex* in, int count, const M4& model, bool fromModel, const ModelVertex* min, bool strip) {
    if (gfx::gpuClips()) {
        // the Vita's GPU clips (no small guard band): draw as is, so static geometry stays in GPU buffers
        if (fromModel && !strip) gfx::drawModel(min, count, model);
        else if (!fromModel) gfx::drawWorld(in, count, strip ? PRIM_TRIANGLE_STRIP : PRIM_TRIANGLES, model);
        else gfx::drawModel(min, count, model);
        return;
    }
    M4 mvp = P * V * model;
    int cap = (strip ? count * 3 : count) * 2 + 96;   // most triangles pass unchanged; cut ones add a few corners
    WorldVertex* out = (WorldVertex*)gfx::frameAlloc(cap * (int)sizeof(WorldVertex));
    if (!out) return;
    int n = 0;
    const float k = 1.0f / 32768.0f;
    int step = strip ? 1 : 3;
    for (int i = 0; i + 2 < count; i += step) {
        CV poly[12], tmp[12];
        int pn = 3;
        if (strip) {
            // skip the zero-area triangles that join strip pieces
            if (fromModel) {
                const ModelVertex *a = &min[i], *b = &min[i + 1], *cc = &min[i + 2];
                if ((a->x == b->x && a->y == b->y && a->z == b->z) || (b->x == cc->x && b->y == cc->y && b->z == cc->z) || (a->x == cc->x && a->y == cc->y && a->z == cc->z)) continue;
            } else {
                const WorldVertex *a = &in[i], *b = &in[i + 1], *cc = &in[i + 2];
                if ((a->x == b->x && a->y == b->y && a->z == b->z) || (b->x == cc->x && b->y == cc->y && b->z == cc->z) || (a->x == cc->x && a->y == cc->y && a->z == cc->z)) continue;
            }
        }
        for (int j = 0; j < 3; j++) {
            if (fromModel) {
                const ModelVertex& q = min[i + j];
                poly[j].w = WorldVertex{q.u * k, q.v * k, q.color, q.x * k, q.y * k, q.z * k};
            } else poly[j].w = in[i + j];
            toClip(mvp, poly[j].w, poly[j].c);
        }
        // quick accept / reject
        bool allIn = true, out1 = false;
        for (int p = 0; p < 5 && !out1; p++) {
            float d0 = planeD(poly[0].c, p), d1 = planeD(poly[1].c, p), d2 = planeD(poly[2].c, p);
            if (d0 < 0 && d1 < 0 && d2 < 0) out1 = true;
            if (d0 < 0 || d1 < 0 || d2 < 0) allIn = false;
        }
        if (out1) continue;
        if (!allIn) {
            for (int p = 0; p < 5 && pn >= 3; p++) {
                int tn = 0;
                for (int j = 0; j < pn; j++) {
                    const CV &a = poly[j], &b = poly[(j + 1) % pn];
                    float da = planeD(a.c, p), db = planeD(b.c, p);
                    if (da >= 0) tmp[tn++] = a;
                    if ((da >= 0) != (db >= 0) && tn < 12) tmp[tn++] = lerp(a, b, da / (da - db));
                }
                for (int j = 0; j < tn; j++) poly[j] = tmp[j];
                pn = tn;
            }
        }
        for (int j = 1; j + 1 < pn; j++) {
            if (n + 3 > cap) break;
            out[n++] = poly[0].w; out[n++] = poly[j].w; out[n++] = poly[j + 1].w;
        }
    }
    gfx::frameTrim(out, n * (int)sizeof(WorldVertex));   // most of the room asked for is not needed
    if (n) { gfx::flushCache(out, n * (int)sizeof(WorldVertex)); gfx::drawWorld(out, n, PRIM_TRIANGLES, model); }
}
}

namespace clip {
void setMatrices(const M4& proj, const M4& view) { P = proj; V = view; }
void drawWorld(const WorldVertex* v, int count, const M4& model, bool strip) { run(v, count, model, false, nullptr, strip); }
void drawModel(const ModelVertex* v, int count, const M4& model, bool strip) { run(nullptr, count, model, true, v, strip); }
}
