#include <malloc.h>
// PC test platform: a small software renderer that follows the same rules as the PSP's GU
// (8-bit palette textures, vertex colour x texture, linear fog, 480x272), so screenshots taken
// on a PC show what the PSP draws. Input is scripted by the test program; sound is mixed on demand.
#include "platform.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <vector>
#include <string>
#include <chrono>

namespace pc {
u32 fb[SCREEN_W * SCREEN_H];
float zb[SCREEN_W * SCREEN_H];
Pad pad;
bool touchUi = getenv("EMR_TOUCH") != nullptr;   // tests: draw the Vita's touch targets
audio::MixFn mixFn = nullptr;
long trisDrawn = 0, drawCalls = 0, vertsSent = 0;
}
using namespace pc;

namespace {
M4 proj = M4::identity(), view = M4::identity();
bool fogOn = false; float fogNear = 0, fogFar = 1; u32 fogCol = 0;
const Texture* tex = nullptr;
bool blendOn = false, depthTest = true, depthWrite = true;
std::vector<u8> scratch(4 << 20);
size_t scratchUsed = 0;

struct CV { float x, y, z, w; float u, v; float r, g, b, a; float fogd; };
float vpX = 0, vpY = 0, vpW = SCREEN_W, vpH = SCREEN_H;    // viewport (beginInset)

inline u32 texelAt(const Texture* t, int x, int y) {
    x &= t->w - 1; y &= t->h - 1;
    int bw = t->w / 16;
    int off = ((y >> 3) * bw + (x >> 4)) * 128 + (y & 7) * 16 + (x & 15);
    return t->palette[t->pixels[off]];
}
// bilinear, like the PSP (GU_LINEAR) and the Vita (GL_LINEAR), so test pictures show what the
// consoles show
inline u32 texel(const Texture* t, float u, float v) {
    float fx = u * t->w - 0.5f, fy = v * t->h - 0.5f;
    int x = (int)floorf(fx), y = (int)floorf(fy);
    float ax = fx - x, ay = fy - y;
    u32 c00 = texelAt(t, x, y), c10 = texelAt(t, x + 1, y), c01 = texelAt(t, x, y + 1), c11 = texelAt(t, x + 1, y + 1);
    u32 out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        float a = ((c00 >> sh) & 255) * (1 - ax) + ((c10 >> sh) & 255) * ax;
        float b = ((c01 >> sh) & 255) * (1 - ax) + ((c11 >> sh) & 255) * ax;
        out |= (u32)(a * (1 - ay) + b * ay + 0.5f) << sh;
    }
    return out;
}

void raster(const CV* in) {
    // perspective divide + viewport
    float sx[3], sy[3], sz[3], iw[3];
    for (int k = 0; k < 3; k++) {
        iw[k] = 1.0f / in[k].w;
        sx[k] = vpX + (in[k].x * iw[k] * 0.5f + 0.5f) * vpW;
        sy[k] = vpY + (0.5f - in[k].y * iw[k] * 0.5f) * vpH;
        sz[k] = in[k].z * iw[k];
    }
    float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (fabsf(area) < 1e-6f) return;
    int x0 = (int)fmaxf(vpX, floorf(fminf(sx[0], fminf(sx[1], sx[2])))), x1 = (int)fminf(vpX + vpW - 1, ceilf(fmaxf(sx[0], fmaxf(sx[1], sx[2]))));
    int y0 = (int)fmaxf(vpY, floorf(fminf(sy[0], fminf(sy[1], sy[2])))), y1 = (int)fminf(vpY + vpH - 1, ceilf(fmaxf(sy[0], fmaxf(sy[1], sy[2]))));
    if (x0 > x1 || y0 > y1) return;
    trisDrawn++;
    float ia = 1.0f / area;
    for (int y = y0; y <= y1; y++) {
        float py = y + 0.5f;
        for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f;
            float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) * ia;
            float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) * ia;
            float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
            int o = y * SCREEN_W + x;
            if (depthTest && z >= zb[o]) continue;
            float q0 = w0 * iw[0], q1 = w1 * iw[1], q2 = w2 * iw[2], qs = 1.0f / (q0 + q1 + q2);
            q0 *= qs; q1 *= qs; q2 *= qs;
            float r = q0 * in[0].r + q1 * in[1].r + q2 * in[2].r;
            float g = q0 * in[0].g + q1 * in[1].g + q2 * in[2].g;
            float b = q0 * in[0].b + q1 * in[1].b + q2 * in[2].b;
            float a = q0 * in[0].a + q1 * in[1].a + q2 * in[2].a;
            if (tex) {
                u32 t = texel(tex, q0 * in[0].u + q1 * in[1].u + q2 * in[2].u, q0 * in[0].v + q1 * in[1].v + q2 * in[2].v);
                r *= (t & 255) / 255.0f; g *= ((t >> 8) & 255) / 255.0f; b *= ((t >> 16) & 255) / 255.0f; a *= (t >> 24) / 255.0f;
            }
            if (a < 0.03f) continue;   // alpha test
            if (fogOn) {
                float d = q0 * in[0].fogd + q1 * in[1].fogd + q2 * in[2].fogd;
                float f = clampf((fogFar - d) / (fogFar - fogNear), 0, 1);
                r = r * f + (fogCol & 255) / 255.0f * (1 - f);
                g = g * f + ((fogCol >> 8) & 255) / 255.0f * (1 - f);
                b = b * f + ((fogCol >> 16) & 255) / 255.0f * (1 - f);
            }
            if (blendOn && a < 0.999f) {
                u32 d = fb[o];
                r = r * a + (d & 255) / 255.0f * (1 - a);
                g = g * a + ((d >> 8) & 255) / 255.0f * (1 - a);
                b = b * a + ((d >> 16) & 255) / 255.0f * (1 - a);
            }
            fb[o] = RGBA((int)(clampf(r, 0, 1) * 255), (int)(clampf(g, 0, 1) * 255), (int)(clampf(b, 0, 1) * 255));
            if (depthWrite) zb[o] = z;
        }
    }
}

CV lerpCV(const CV& a, const CV& b, float t) {
    CV r;
    r.x = a.x + (b.x - a.x) * t; r.y = a.y + (b.y - a.y) * t; r.z = a.z + (b.z - a.z) * t; r.w = a.w + (b.w - a.w) * t;
    r.u = a.u + (b.u - a.u) * t; r.v = a.v + (b.v - a.v) * t;
    r.r = a.r + (b.r - a.r) * t; r.g = a.g + (b.g - a.g) * t; r.b = a.b + (b.b - a.b) * t; r.a = a.a + (b.a - a.a) * t;
    r.fogd = a.fogd + (b.fogd - a.fogd) * t;
    return r;
}

void triangle(const CV* v) {
    // clip against the near plane (z > -w)
    CV poly[4]; int n = 0;
    for (int i = 0; i < 3; i++) {
        const CV &a = v[i], &b = v[(i + 1) % 3];
        float da = a.z + a.w, db = b.z + b.w;
        if (da >= 0) poly[n++] = a;
        if ((da >= 0) != (db >= 0)) poly[n++] = lerpCV(a, b, da / (da - db));
    }
    if (n < 3) return;
    for (int i = 1; i + 1 < n; i++) { CV t[3] = {poly[0], poly[i], poly[i + 1]}; raster(t); }
}

CV project(const M4& mvp, const M4& mv, float x, float y, float z, float u, float v, u32 c) {
    CV o;
    o.x = mvp.m[0] * x + mvp.m[4] * y + mvp.m[8] * z + mvp.m[12];
    o.y = mvp.m[1] * x + mvp.m[5] * y + mvp.m[9] * z + mvp.m[13];
    o.z = mvp.m[2] * x + mvp.m[6] * y + mvp.m[10] * z + mvp.m[14];
    o.w = mvp.m[3] * x + mvp.m[7] * y + mvp.m[11] * z + mvp.m[15];
    o.fogd = -(mv.m[2] * x + mv.m[6] * y + mv.m[10] * z + mv.m[14]);
    o.u = u; o.v = v;
    o.r = (c & 255) / 255.0f; o.g = ((c >> 8) & 255) / 255.0f; o.b = ((c >> 16) & 255) / 255.0f; o.a = (c >> 24) / 255.0f;
    return o;
}

void blendPixel(int x, int y, u32 c) {
    if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    float a = (c >> 24) / 255.0f;
    if (a < 0.03f) return;
    u32& d = fb[y * SCREEN_W + x];
    if (!blendOn || a > 0.999f) { d = c | 0xFF000000; return; }
    int r = (int)((c & 255) * a + (d & 255) * (1 - a)), g = (int)(((c >> 8) & 255) * a + ((d >> 8) & 255) * (1 - a)), b = (int)(((c >> 16) & 255) * a + ((d >> 16) & 255) * (1 - a));
    d = RGBA(r, g, b);
}
}

namespace pc { float fakeWait = 0; }   // tests: pretend the frame waited this long for the screen
namespace gfx {
bool gpuClips() { return false; }
void beginFrame(u32 clear) {
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) { fb[i] = clear | 0xFF000000; zb[i] = 1e9f; }
    scratchUsed = 0; trisDrawn = 0; drawCalls = 0; vertsSent = 0;
}
void endFrame() {}
void setSync(bool, int) {}
void clearDepth() { for (int i = 0; i < SCREEN_W * SCREEN_H; i++) zb[i] = 1e9f; }
bool beginInset(float x, float y, float w, float h, u32 c) {
    vpX = floorf(x); vpY = floorf(y); vpW = floorf(w); vpH = floorf(h);
    for (int yy = (int)vpY; yy < (int)(vpY + vpH); yy++)
        for (int xx = (int)vpX; xx < (int)(vpX + vpW); xx++) { fb[yy * SCREEN_W + xx] = c | 0xFF000000u; zb[yy * SCREEN_W + xx] = 1e9f; }
    return true;
}
void endInset() { vpX = 0; vpY = 0; vpW = SCREEN_W; vpH = SCREEN_H; }
void setProjection(const M4& p) { proj = p; }
void setView(const M4& v) { view = v; }
void setFog(bool on, float n, float f, u32 c) { fogOn = on; fogNear = n; fogFar = f; fogCol = c; }
void bindTexture(const Texture* t) { tex = (t && t->pixels) ? t : nullptr; }
void setBlend(bool on) { blendOn = on; }
void setDepthTest(bool on) { depthTest = on; }
void setDepthWrite(bool on) { depthWrite = on; }
void drawModel(const ModelVertex* v, int count, const M4& model) {
    drawCalls++; vertsSent += count;
    M4 mv = view * model, mvp = proj * mv;
    const float k = 1.0f / 32768.0f;
    for (int i = 0; i + 2 < count; i += 3) {
        CV c[3];
        for (int j = 0; j < 3; j++) {
            const ModelVertex& q = v[i + j];
            c[j] = project(mvp, mv, q.x * k, q.y * k, q.z * k, q.u * k, q.v * k, q.color);
        }
        triangle(c);
    }
}
void drawWorld(const WorldVertex* v, int count, Prim prim, const M4& model) {
    drawCalls++; vertsSent += count;
    M4 mv = view * model, mvp = proj * mv;
    if (prim == PRIM_TRIANGLES) {
        for (int i = 0; i + 2 < count; i += 3) {
            CV c[3];
            for (int j = 0; j < 3; j++) c[j] = project(mvp, mv, v[i + j].x, v[i + j].y, v[i + j].z, v[i + j].u, v[i + j].v, v[i + j].color);
            triangle(c);
        }
    } else if (prim == PRIM_TRIANGLE_STRIP) {
        for (int i = 0; i + 2 < count; i++) {
            CV c[3];
            for (int j = 0; j < 3; j++) c[j] = project(mvp, mv, v[i + j].x, v[i + j].y, v[i + j].z, v[i + j].u, v[i + j].v, v[i + j].color);
            triangle(c);
        }
    }
}
void drawRect2D(float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 color) {
    drawCalls++; vertsSent += 2;
    int X0 = (int)floorf(x), Y0 = (int)floorf(y), X1 = (int)ceilf(x + w), Y1 = (int)ceilf(y + h);
    for (int py = Y0; py < Y1; py++) for (int px = X0; px < X1; px++) {
        u32 c = color;
        if (tex) {
            float u = u0 + (u1 - u0) * ((px + 0.5f - x) / w), v = v0 + (v1 - v0) * ((py + 0.5f - y) / h);
            u32 t = texel(tex, u, v);
            int r = (t & 255) * (color & 255) / 255, g = ((t >> 8) & 255) * ((color >> 8) & 255) / 255, b = ((t >> 16) & 255) * ((color >> 16) & 255) / 255, a = (t >> 24) * (color >> 24) / 255;
            c = RGBA(r, g, b, a);
        }
        blendPixel(px, py, c);
    }
}
void drawSprites2D(const Sprite2D* s, int n) {
    long dc = drawCalls;
    for (int i = 0; i < n; i++) drawRect2D(s[i].x, s[i].y, s[i].w, s[i].h, s[i].u0, s[i].v0, s[i].u1, s[i].v1, s[i].color);
    drawCalls = dc + 1;
}
void drawFill2D(float x, float y, float w, float h, u32 color) {
    const Texture* t = tex; tex = nullptr;
    drawRect2D(x, y, w, h, 0, 0, 0, 0, color);
    tex = t;
}
void* frameAlloc(int bytes) {
    bytes = (bytes + 15) & ~15;
    if (scratchUsed + bytes > scratch.size()) return nullptr;
    void* p = &scratch[scratchUsed]; scratchUsed += bytes; return p;
}
void frameTrim(void* p, int keepBytes) {
    u8* b = (u8*)p;
    if (!b || b < scratch.data() || b > scratch.data() + scratchUsed) return;
    size_t end = (size_t)(b - scratch.data()) + ((keepBytes + 15) & ~15);
    if (end < scratchUsed) scratchUsed = end;
}
void flushCache(const void*, int) {}
bool moveToFastMemory(Texture&) { return true; }
int fastMark() { return 0; }
int vramUsedKB() { return 0; }
float lastWaitSec() { return pc::fakeWait; }
float lastGpuWaitSec() { return 0; }
void fastRelease(int) {}
}

namespace input { void poll(Pad& p) { p = pc::pad; } bool hasTouch() { return pc::touchUi; } }

namespace audio {
void start(MixFn fn) { mixFn = fn; }
void stop() { mixFn = nullptr; }
}

namespace sys {
static auto t0 = std::chrono::steady_clock::now();
bool init() { return true; }
void shutdown() {}
bool running() { return true; }
void quit() {}
int memUsedKB() { struct mallinfo2 mi = mallinfo2(); return (int)(mi.uordblks / 1024); }
double timeSec() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }
void* loadFile(const char* path, int* size) {
    // EMR_HD=1: like the Vita, a model in HD/ replaces the one in DATA/
    FILE* f = nullptr;
    if (!getenv("FL_LOWRES") && !strncmp(path, "DATA/", 5)) { std::string hd = std::string("HD/") + (path + 5); f = fopen(hd.c_str(), "rb"); }
    if (!f) f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void* p = aligned_alloc(16, (n + 16) & ~15L);
    if (fread(p, 1, n, f) != (size_t)n) { fclose(f); free(p); return nullptr; }
    fclose(f);
    if (size) *size = (int)n;
    return p;
}
void freeFile(void* p) { free(p); }
void log(const char* fmt, ...) { va_list a; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a); }
}

namespace pc {
void saveShot(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) { u8 c[3] = {(u8)(fb[i] & 255), (u8)((fb[i] >> 8) & 255), (u8)((fb[i] >> 16) & 255)}; fwrite(c, 1, 3, f); }
    fclose(f);
}
}
