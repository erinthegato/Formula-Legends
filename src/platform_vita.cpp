// PS Vita platform: vitaGL (OpenGL fixed function on the Vita's GXM chip), buttons, sound, files.
// Same rules as the PSP layer, so the whole game runs unchanged:
//  * the game still lays out its screens at 480 x 272; 2D is drawn through an orthographic
//    480 x 272 projection onto the 960 x 544 screen, 3D renders at the full 960 x 544 with 2x MSAA;
//  * static vertex arrays (models, circuit, pit lane) live in GPU buffers after their first draw;
//  * the PSP's 8-bit swizzled palette textures are turned into RGBA textures the first time they
//    are bound, and cached. gfx::flushCache (which the game calls after loading or changing any
//    pixels, because the PSP needs it) tells this layer to forget the old copy;
//  * relative paths are mapped: "DATA/..." is read from the app (app0:), anything else (saves,
//    settings, logs, ghosts) lives in ux0:data/FormulaLegends/. A model in "HD/" overrides the
//    PSP one in "DATA/" (higher detail for the Vita).
#include <vitasdk.h>
#include <vitaGL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <malloc.h>
#include <vector>
#include <unordered_map>
#include <cstddef>
#include <initializer_list>
#include "platform.h"
#include <zlib.h>
#include "game.h"
#include "data.h"

// All of the common dialog memory (vitaGL's own value, from its private mem_utils.h; vglInitExtended
// passes it so that none is taken). The SDK headers don't define it.
#ifndef SCE_KERNEL_MAX_MAIN_CDIALOG_MEM_SIZE
#define SCE_KERNEL_MAX_MAIN_CDIALOG_MEM_SIZE 0x8C6000
#endif

extern "C" {
// The game's own heap (models, circuits, sounds): a full 20-car race peaks at ~15 MB on the PC test
// (FL_MEM=1) and ~29 MB on the Vita (with vitaGL's own allocations), so 48 MB still leaves ~19 MB spare;
// what it doesn't take goes to vitaGL. (It was 64 MB; before that 200 MB.)
int _newlib_heap_size_user = 48 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 2 * 1024 * 1024;
}

// vita-elf-create appends ~3.5 KB of module data after the code segment and fails ("Cannot allocate
// ... bytes for SCE data at end of segment 0; segment 1 overlaps") when the code happens to end just
// short of the data segment. This read-only block moves the end of the code segment; if that error
// ever comes back after a code change, change VITA_PAD (any multiple of 1 KB).
#ifndef VITA_PAD
#define VITA_PAD 6144
#endif
extern "C" __attribute__((used, aligned(16))) const unsigned char emr_segment_pad[VITA_PAD] = {1};

#define SAVE_DIR "ux0:data/FormulaLegends"
static const int VITA_W = 960, VITA_H = 544;

// ---------------------------------------------------------------- paths
// The game opens files by relative path (next to the EBOOT on the PSP). The linker sends every
// fopen / remove here (-Wl,--wrap=fopen,--wrap=remove).
extern "C" FILE* __real_fopen(const char* path, const char* mode);
extern "C" int __real_remove(const char* path);

static void mapPath(const char* in, char* out, int n, bool hdFirst) {
    if (strchr(in, ':')) { snprintf(out, n, "%s", in); return; }   // already a device path
    if (!strncmp(in, "DATA/", 5)) {
        if (hdFirst) snprintf(out, n, "app0:HD/%s", in + 5);
        else snprintf(out, n, "app0:%s", in);
        return;
    }
    snprintf(out, n, SAVE_DIR "/%s", in);
}

extern "C" FILE* __wrap_fopen(const char* path, const char* mode) {
    char p[512];
    bool reading = mode && mode[0] == 'r';
    if (reading && !strncmp(path, "DATA/", 5)) {
        mapPath(path, p, sizeof p, true);
        if (FILE* f = __real_fopen(p, mode)) return f;
    }
    mapPath(path, p, sizeof p, false);
    return __real_fopen(p, mode);
}
extern "C" int __wrap_remove(const char* path) {
    char p[512]; mapPath(path, p, sizeof p, false);
    return __real_remove(p);
}

// ---------------------------------------------------------------- graphics
namespace {
u8* arena = nullptr;
const int ARENA_SIZE = 16 << 20;
int arenaUsed = 0;

// RGBA copies of the game's palette textures
struct GpuTex { const u8* pix; const u32* pal; int w, h; GLuint id; };
std::vector<GpuTex> texCache;
int lastHit = -1;
const Texture* curTex = nullptr;
bool texOn = false;

// what the game asked for (2D drawing changes some of it for a moment)
M4 projM = M4::identity(), viewM = M4::identity();
bool fogWanted = false, depthWanted = true, in2D = false;

inline bool overlaps(const void* a, int an, const void* b, int bn) {
    const u8 *x = (const u8*)a, *y = (const u8*)b;
    return x < y + bn && y < x + an;
}

GLuint upload(const Texture* t) {
    for (int i = (lastHit >= 0 ? lastHit : 0), k = 0; k < (int)texCache.size(); k++, i = (i + 1) % (int)texCache.size()) {
        const GpuTex& g = texCache[i];
        if (g.pix == t->pixels && g.pal == t->palette && g.w == t->w && g.h == t->h) { lastHit = i; return g.id; }
    }
    // unswizzle (16 x 8 blocks) and look up the palette
    int w = t->w, h = t->h, bw = w / 16 > 0 ? w / 16 : 1;
    std::vector<u32> rgba((size_t)w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int off = ((y >> 3) * bw + (x >> 4)) * 128 + (y & 7) * 16 + (x & 15);
            rgba[(size_t)y * w + x] = t->palette[t->pixels[off]];
        }
    // Fully opaque textures go to the GPU as 16-bit colour (half the memory
    // and bandwidth); anything with see-through parts stays 32-bit.
    bool opaque = true;
    for (size_t i = 0; i < rgba.size() && opaque; i++) opaque = (rgba[i] >> 24) == 0xFF;
    // Big opaque textures (road, grass, car paint...) get mipmaps so they stay
    // smooth in the distance instead of shimmering. HUD/font/cut-out ones don't.
    bool pow2 = !(w & (w - 1)) && !(h & (h - 1));
    bool mip = opaque && pow2 && w >= 64 && h >= 64;
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mip ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (opaque) {
        std::vector<u16> c16((size_t)w * h);
        for (size_t i = 0; i < c16.size(); i++) {
            u32 c = rgba[i];   // A B G R in memory order R,G,B,A
            u32 r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
            c16[i] = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, c16.data());
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    }
    if (mip) glGenerateMipmap(GL_TEXTURE_2D);
    texCache.push_back(GpuTex{t->pixels, t->palette, w, h, id});
    lastHit = (int)texCache.size() - 1;
    return id;
}

// forget GPU copies made from memory in [p, p + n)
void forget(const void* p, int n) {
    for (int i = (int)texCache.size() - 1; i >= 0; i--) {
        GpuTex& g = texCache[i];
        if (overlaps(p, n, g.pix, g.w * g.h) || overlaps(p, n, g.pal, 1024)) {
            glDeleteTextures(1, &g.id);
            texCache[i] = texCache.back(); texCache.pop_back();
        }
    }
    lastHit = -1;
    curTex = nullptr;
}

// GPU copies of static vertex arrays (models, circuit chunks, pit lane): made on first draw, kept
// until the game rewrites that memory (gfx::flushCache) or frees it. Per-frame arrays (frame memory)
// are sent the slow way every time.
struct Vbo { const u8* ptr; int bytes; GLuint id; };
std::unordered_map<uint64_t, Vbo> vbos;
inline bool inArena(const void* p) { return arena && (const u8*)p >= arena && (const u8*)p < arena + ARENA_SIZE; }
inline uint64_t vkey(const void* p, int count, bool model) { return ((uint64_t)(uintptr_t)p << 1) ^ ((uint64_t)count << 40) ^ (model ? 1 : 0); }

void forgetVbos(const void* p, int n) {
    for (auto it = vbos.begin(); it != vbos.end();) {
        if (overlaps(p, n, it->second.ptr, it->second.bytes)) { glDeleteBuffers(1, &it->second.id); it = vbos.erase(it); }
        else ++it;
    }
}

GLuint vboFor(const void* src, int count, bool model) {
    if (inArena(src)) return 0;
    uint64_t k = vkey(src, count, model);
    auto it = vbos.find(k);
    if (it != vbos.end()) return it->second.id;
    std::vector<WorldVertex> tmp;
    const WorldVertex* data = (const WorldVertex*)src;
    if (model) {
        tmp.resize(count);
        const ModelVertex* v = (const ModelVertex*)src;
        const float kk = 1.0f / 32768.0f;
        for (int i = 0; i < count; i++) tmp[i] = WorldVertex{v[i].u * kk, v[i].v * kk, v[i].color, v[i].x * kk, v[i].y * kk, v[i].z * kk};
        data = tmp.data();
    }
    GLuint id = 0;
    glGenBuffers(1, &id);
    glBindBuffer(GL_ARRAY_BUFFER, id);
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(WorldVertex), data, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    vbos[k] = Vbo{(const u8*)src, count * (int)(model ? sizeof(ModelVertex) : sizeof(WorldVertex)), id};
    return id;
}

void applyTex() {
    if (curTex && curTex->pixels && curTex->palette) {
        glEnable(GL_TEXTURE_2D);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glBindTexture(GL_TEXTURE_2D, upload(curTex));
        texOn = true;
    } else {
        glDisable(GL_TEXTURE_2D);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        texOn = false;
    }
}

void enter3D() {
    if (!in2D) return;
    in2D = false;
    glMatrixMode(GL_PROJECTION); glLoadMatrixf(projM.m);
    if (fogWanted) glEnable(GL_FOG);
    if (depthWanted) glEnable(GL_DEPTH_TEST);
}
void enter2D() {
    if (in2D) return;
    in2D = true;
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0, SCREEN_W, SCREEN_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_FOG);
    glDisable(GL_DEPTH_TEST);
}

void drawVbo(GLuint id, int count, GLenum mode) {
    glBindBuffer(GL_ARRAY_BUFFER, id);
    glVertexPointer(3, GL_FLOAT, sizeof(WorldVertex), (void*)offsetof(WorldVertex, x));
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(WorldVertex), (void*)offsetof(WorldVertex, color));
    if (texOn) glTexCoordPointer(2, GL_FLOAT, sizeof(WorldVertex), (void*)offsetof(WorldVertex, u));
    glDrawArrays(mode, 0, count);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void drawArrays(const WorldVertex* v, int count, GLenum mode) {
    glVertexPointer(3, GL_FLOAT, sizeof(WorldVertex), &v->x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(WorldVertex), &v->color);
    if (texOn) glTexCoordPointer(2, GL_FLOAT, sizeof(WorldVertex), &v->u);
    glDrawArrays(mode, 0, count);
}

void setModel(const M4& model) {
    M4 mv = viewM * model;
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(mv.m);
}
}

namespace gfx {
static bool syncOn = true; static int syncEvery = 1;
static float waitSec = 0, gpuWaitSec = 0;
float lastWaitSec() { return waitSec; }
float lastGpuWaitSec() { return gpuWaitSec; }
void setSync(bool vsync, int every) {
    syncOn = vsync || every > 1; syncEvery = every < 1 ? 1 : every;
    vglWaitVblankStart(syncOn ? GL_TRUE : GL_FALSE);
}
void beginFrame(u32 clear) {
    arenaUsed = 0;
    glViewport(0, 0, VITA_W, VITA_H);
    glClearColor((clear & 255) / 255.0f, ((clear >> 8) & 255) / 255.0f, ((clear >> 16) & 255) / 255.0f, 1);
    glClearDepthf(1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    in2D = true; enter3D();
    curTex = nullptr; applyTex();
}
void endFrame() {
    SceUInt64 t0 = sceKernelGetProcessTimeWide();
    // 30 fps: wait one extra screen refresh (vitaGL waits for the second one when vsync is on)
    if (syncOn && syncEvery > 1) for (int i = 1; i < syncEvery; i++) sceDisplayWaitVblankStart();
    vglSwapBuffers(GL_FALSE);
    waitSec = (sceKernelGetProcessTimeWide() - t0) / 1000000.0f;
    gpuWaitSec = 0;
}
void clearDepth() { glDepthMask(GL_TRUE); glClear(GL_DEPTH_BUFFER_BIT); }
bool beginInset(float x, float y, float w, float h, u32 c) {
    int sx = (int)(x * 2), sy = (int)((SCREEN_H - y - h) * 2), sw = (int)(w * 2), sh = (int)(h * 2);
    glViewport(sx, sy, sw, sh);
    glEnable(GL_SCISSOR_TEST); glScissor(sx, sy, sw, sh);
    glClearColor((c & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, ((c >> 16) & 255) / 255.0f, 1);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return true;
}
void endInset() { glDisable(GL_SCISSOR_TEST); glViewport(0, 0, VITA_W, VITA_H); }
void setProjection(const M4& p) { projM = p; if (!in2D) { glMatrixMode(GL_PROJECTION); glLoadMatrixf(p.m); } }
void setView(const M4& v) { viewM = v; }
void setFog(bool on, float n, float f, u32 c) {
    fogWanted = on;
    if (on) {
        GLfloat col[4] = {(c & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, ((c >> 16) & 255) / 255.0f, 1};
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogf(GL_FOG_START, n); glFogf(GL_FOG_END, f);
        glFogfv(GL_FOG_COLOR, col);
    }
    if (!in2D) { if (on) glEnable(GL_FOG); else glDisable(GL_FOG); }
}
void bindTexture(const Texture* t) {
    const Texture* want = (t && t->pixels && t->palette) ? t : nullptr;
    if (want == curTex && (want != nullptr) == texOn) return;
    curTex = want; applyTex();
}
void setBlend(bool on) {
    if (on) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    else glDisable(GL_BLEND);
}
void setDepthTest(bool on) { depthWanted = on; if (!in2D) { if (on) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST); } }
void setDepthWrite(bool on) { glDepthMask(on ? GL_TRUE : GL_FALSE); }

void drawModel(const ModelVertex* v, int count, const M4& model) {
    if (count <= 0) return;
    enter3D();
    if (GLuint id = vboFor(v, count, true)) { setModel(model); drawVbo(id, count, GL_TRIANGLES); return; }
    // 16-bit model vertices -> floats (positions and texture coordinates are value / 32768)
    WorldVertex* w = (WorldVertex*)frameAlloc(count * (int)sizeof(WorldVertex));
    if (!w) return;
    const float k = 1.0f / 32768.0f;
    for (int i = 0; i < count; i++) {
        const ModelVertex& q = v[i];
        w[i] = WorldVertex{q.u * k, q.v * k, q.color, q.x * k, q.y * k, q.z * k};
    }
    setModel(model);
    drawArrays(w, count, GL_TRIANGLES);
}
void drawWorld(const WorldVertex* v, int count, Prim prim, const M4& model) {
    if (count <= 0) return;
    static const GLenum P[4] = {GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_LINES, GL_LINE_STRIP};
    enter3D();
    setModel(model);
    if (GLuint id = vboFor(v, count, false)) { drawVbo(id, count, P[prim]); return; }
    drawArrays(v, count, P[prim]);
}
void drawSprites2D(const Sprite2D* s, int n) {
    if (n <= 0) return;
    enter2D();
    WorldVertex* w = (WorldVertex*)frameAlloc(n * 6 * (int)sizeof(WorldVertex));
    if (!w) return;
    for (int i = 0; i < n; i++) {
        const Sprite2D& r = s[i];
        float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
        WorldVertex a{r.u0, r.v0, r.color, x0, y0, 0}, b{r.u1, r.v0, r.color, x1, y0, 0};
        WorldVertex c{r.u1, r.v1, r.color, x1, y1, 0}, d{r.u0, r.v1, r.color, x0, y1, 0};
        WorldVertex* q = w + i * 6;
        q[0] = a; q[1] = b; q[2] = c; q[3] = a; q[4] = c; q[5] = d;
    }
    drawArrays(w, n * 6, GL_TRIANGLES);
}
void drawRect2D(float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 color) {
    Sprite2D s{x, y, w, h, u0, v0, u1, v1, color};
    drawSprites2D(&s, 1);
}
void drawFill2D(float x, float y, float w, float h, u32 color) {
    const Texture* t = curTex;
    bindTexture(nullptr);
    drawRect2D(x, y, w, h, 0, 0, 0, 0, color);
    bindTexture(t);
}
void* frameAlloc(int bytes) {
    bytes = (bytes + 15) & ~15;
    if (!arena || arenaUsed + bytes > ARENA_SIZE) return nullptr;
    void* p = arena + arenaUsed; arenaUsed += bytes; return p;
}
// The game calls this after writing pixels or palettes: the RGBA copy made from them is stale.
void frameTrim(void* p, int keepBytes) {
    u8* b = (u8*)p;
    if (!b || b < arena || b > arena + arenaUsed) return;
    int end = (int)(b - arena) + ((keepBytes + 15) & ~15);
    if (end < arenaUsed) arenaUsed = end;   // (only shrinks; the block was the last one handed out)
}
void flushCache(const void* p, int bytes) { if (p && bytes > 0 && !inArena(p)) { forget(p, bytes); forgetVbos(p, bytes); } }
bool gpuClips() { return true; }
// The Vita has plenty of video memory; textures already live on the GPU once bound.
bool moveToFastMemory(Texture&) { return false; }
int fastMark() { return 0; }
int vramUsedKB() { return 0; }
void fastRelease(int) {}
}

// ---------------------------------------------------------------- input
namespace input {
static float tiltRoll() {
    SceMotionState m;
    if (sceMotionGetState(&m) < 0) return 0;
    // gravity in the console's frame: x across the screen, y up the screen. Held like a steering
    // wheel, turning it right moves gravity towards +x.
    return atan2f(m.acceleration.x, -m.acceleration.y);
}
bool hasTouch() { return true; }
void poll(Pad& p) {
    SceCtrlData d;
    memset(&d, 0, sizeof d);
    sceCtrlPeekBufferPositive(0, &d, 1);
    p.stickX = (d.lx - 128) / 127.0f; p.stickY = (d.ly - 128) / 127.0f;
    if (fabsf(p.stickX) < 0.12f) p.stickX = 0;
    if (fabsf(p.stickY) < 0.12f) p.stickY = 0;
    p.rstickX = (d.rx - 128) / 127.0f; p.rstickY = (d.ry - 128) / 127.0f;
    if (fabsf(p.rstickX) < 0.10f) p.rstickX = 0;
    if (fabsf(p.rstickY) < 0.10f) p.rstickY = 0;
    p.up = d.buttons & SCE_CTRL_UP; p.down = d.buttons & SCE_CTRL_DOWN;
    p.left = d.buttons & SCE_CTRL_LEFT; p.right = d.buttons & SCE_CTRL_RIGHT;
    p.cross = d.buttons & SCE_CTRL_CROSS; p.circle = d.buttons & SCE_CTRL_CIRCLE;
    p.square = d.buttons & SCE_CTRL_SQUARE; p.triangle = d.buttons & SCE_CTRL_TRIANGLE;
    p.l = d.buttons & SCE_CTRL_LTRIGGER; p.r = d.buttons & SCE_CTRL_RTRIGGER;
    p.start = d.buttons & SCE_CTRL_START; p.select = d.buttons & SCE_CTRL_SELECT;
    // front touch screen: 1920 x 1088 units -> the game's 480 x 272 layout
    SceTouchData t;
    p.touches = 0;
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1) > 0)
        for (int i = 0; i < (int)t.reportNum && p.touches < 2; i++) {
            p.tx[p.touches] = t.report[i].x / 4.0f; p.ty[p.touches] = t.report[i].y / 4.0f; p.touches++;
        }
    p.backTouch = sceTouchPeek(SCE_TOUCH_PORT_BACK, &t, 1) > 0 && t.reportNum > 0;
    p.backX = p.backTouch ? t.report[0].x / 4.0f : 0;
    p.hasTilt = true; p.tilt = tiltRoll();
}
}

// ---------------------------------------------------------------- audio
namespace audio {
static MixFn mixFn = nullptr;
static int port = -1;
static volatile bool audioRun = false;
static SceUID audioThread = -1;
static const int FRAMES = 1024;
static int audioMain(SceSize, void*) {
    static int16_t buf[2][FRAMES * 2] __attribute__((aligned(64)));
    int k = 0;
    while (audioRun) {
        MixFn f = mixFn;
        if (f) f(buf[k], FRAMES); else memset(buf[k], 0, sizeof buf[k]);
        sceAudioOutOutput(port, buf[k]);
        k ^= 1;
    }
    return 0;
}
void start(MixFn fn) {
    mixFn = fn;
    if (audioRun) return;
    port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, FRAMES, 44100, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) { sys::log("audio port failed %08x\n", port); return; }
    int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
    sceAudioOutSetVolume(port, (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), vol);
    audioRun = true;
    audioThread = sceKernelCreateThread("audio", audioMain, 0x10000100 - 20, 0x10000, 0, 0, nullptr);
    if (audioThread >= 0) sceKernelStartThread(audioThread, 0, nullptr);
}
void stop() {
    if (!audioRun) return;
    audioRun = false;
    sceKernelWaitThreadEnd(audioThread, nullptr, nullptr);
    sceKernelDeleteThread(audioThread);
    sceAudioOutReleasePort(port);
}
}

// ---------------------------------------------------------------- system
// A plain message on screen without the GPU (used when vitaGL can't start): the game's own font,
// drawn by the CPU straight into a frame buffer. Waits for a button, then closes the app.
static void messageScreen(std::initializer_list<const char*> lines) {
    const int W = 960, H = 544, PITCH = 960;
    int size = (W * H * 4 + 0x3FFFF) & ~0x3FFFF;
    SceUID blk = sceKernelAllocMemBlock("emr_msg_fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, size, nullptr);
    if (blk < 0) { sceKernelExitProcess(0); return; }
    u32* fb = nullptr; sceKernelGetMemBlockBase(blk, (void**)&fb);
    for (int i = 0; i < PITCH * H; i++) fb[i] = 0xFF402010;      // dark blue (A B G R)
    Font f;
    bool haveFont = f.load("DATA/font.bin");
    auto texel = [&](int x, int y) -> u32 {
        const Texture& t = f.tex;
        if (x < 0 || y < 0 || x >= t.w || y >= t.h) return 0;
        int bw = t.w / 16, off = ((y >> 3) * bw + (x >> 4)) * 128 + (y & 7) * 16 + (x & 15);
        return t.palette[t.pixels[off]];
    };
    const int S = 2;                                   // screen pixels per font layout pixel
    int y = 60, k = 0;
    for (const char* line : lines) {
        u32 col = k == 0 ? 0xFF10A0FF : 0xFFFFFFFF;    // title in orange
        if (haveFont) {
            int x = 60;
            for (const char* c = line; *c; c++) {
                int ch = (unsigned char)*c; if (ch < 32 || ch > 126) ch = '?';
                const Font::Glyph& q = f.g[ch - 32];
                for (int oy = 0; oy < q.h * S; oy++)
                    for (int ox = 0; ox < q.w * S; ox++) {
                        int sx = x + q.xoff * S + ox, sy = y + oy;
                        if (sx < 0 || sx >= W || sy < 0 || sy >= H) continue;
                        u32 t = texel(q.x * f.texScale + ox * f.texScale / S, q.y * f.texScale + oy * f.texScale / S);
                        u32 a = t >> 24;
                        if (!a) continue;
                        u32 d = fb[sy * PITCH + sx], o = 0xFF000000;
                        for (int sh = 0; sh < 24; sh += 8)
                            o |= ((((col >> sh) & 255) * a + ((d >> sh) & 255) * (255 - a)) / 255) << sh;
                        fb[sy * PITCH + sx] = o;
                    }
                x += q.adv * S;
            }
        }
        y += (haveFont ? f.lineH : 16) * S + 4; k++;
    }
    SceDisplayFrameBuf d{};
    d.size = sizeof d; d.base = fb; d.pitch = PITCH; d.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8; d.width = W; d.height = H;
    sceDisplaySetFrameBuf(&d, SCE_DISPLAY_SETBUF_NEXTFRAME);
    SceCtrlData pad{};
    for (int i = 0; i < 30; i++) sceDisplayWaitVblankStart();       // ignore a button still held from the LiveArea
    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_SQUARE | SCE_CTRL_TRIANGLE | SCE_CTRL_START | SCE_CTRL_SELECT)) break;
        sceDisplayWaitVblankStart();
    }
    sceKernelExitProcess(0);
}

namespace sys {
static bool runningFlag = true;
static bool fileExists(const char* p) { SceIoStat st; return sceIoGetstat(p, &st) >= 0; }
bool init() {
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
    sceMotionStartSampling();
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir(SAVE_DIR, 0777);
    arena = (u8*)memalign(64, ARENA_SIZE);

    // vitaGL compiles its shaders with the system's shader compiler (libshacccg.suprx)
    if (!fileExists("ur0:data/libshacccg.suprx") && !fileExists("ur0:data/external/libshacccg.suprx")) {
        log("libshacccg.suprx not found in ur0:data - install it (VitaDB does it for you)\n");
        messageScreen({"Formula Legends can't start yet", "",
                       "The PS Vita's shader compiler (libshacccg.suprx) is missing.", "",
                       "To install it:", "1. Open VitaDB (or download it from vitadb.rinnegatamante.it)",
                       "2. VitaDB installs libshacccg.suprx by itself on first start",
                       "3. Start this game again", "", "Press any button to close"});
    }

    // vitaGL takes the memory left over for textures and draw buffers. Leave some of each kind free for
    // plugins - a screen recorder (Vita-Recorder) needs ~2 MB of video memory (or 2 MB of physically
    // contiguous RAM) plus a little ordinary RAM while it records. vglInitExtended left none of the
    // first two.
    vglInitWithCustomThreshold(0, VITA_W, VITA_H,
                               16 * 1024 * 1024,      // ordinary RAM
                               8 * 1024 * 1024,       // video memory (CDRAM)
                               4 * 1024 * 1024,       // physically contiguous RAM
                               SCE_KERNEL_MAX_MAIN_CDIALOG_MEM_SIZE,   // all dialog memory stays free (as before)
                               SCE_GXM_MULTISAMPLE_2X);
    vglWaitVblankStart(GL_TRUE);
    glViewport(0, 0, VITA_W, VITA_H);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);          // the AI models are not consistently wound
    glShadeModel(GL_SMOOTH);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 8.0f / 255.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    return true;
}
void shutdown() {}
bool running() { return runningFlag; }
void quit() { runningFlag = false; }
int memUsedKB() { struct mallinfo mi = mallinfo(); return (int)(mi.uordblks / 1024); }
double timeSec() { return sceKernelGetProcessTimeWide() / 1000000.0; }
void* loadFile(const char* path, int* size) {
    FILE* f = fopen(path, "rb");    // wrapped: DATA/ -> app0:HD/ then app0:DATA/
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void* p = memalign(64, (n + 63) & ~63);
    if (!p) { fclose(f); return nullptr; }
    if (fread(p, 1, n, f) != (size_t)n) { fclose(f); free(p); return nullptr; }
    fclose(f);
    // 'FLZ1' u32 size, then a zlib stream: packed on the memory card (tools/pack_files.py), unpacked here
    if (n >= 8 && !memcmp(p, "FLZ1", 4)) {
        uint32_t raw; memcpy(&raw, (char*)p + 4, 4);
        void* q = memalign(64, (raw + 63) & ~63);
        uLongf got = raw;
        if (!q || uncompress((Bytef*)q, &got, (const Bytef*)p + 8, (uLong)(n - 8)) != Z_OK || got != raw) { free(p); free(q); return nullptr; }
        free(p); p = q; n = (long)raw;
    }
    if (size) *size = (int)n;
    gfx::flushCache(p, (int)n);   // memory reused from a freed file: drop old texture copies
    return p;
}
void freeFile(void* p) {
    if (!p) return;
    gfx::flushCache(p, (int)malloc_usable_size(p));
    free(p);
}
void log(const char* fmt, ...) {
    va_list a; va_start(a, fmt);
    FILE* f = fopen("log.txt", "a");
    if (f) { vfprintf(f, fmt, a); fclose(f); }
    va_end(a);
}
}

// ---------------------------------------------------------------- main
static Game game;
int main(int, char**) {
    sys::init();
    remove("log.txt");
    if (!game.init()) sys::log("DATA folder missing or incomplete\n");
    double last = sys::timeSec();
    while (sys::running()) {
        double now = sys::timeSec();
        float dt = (float)(now - last);
        last = now;
        Pad p;
        input::poll(p);
        game.frame(dt, p);
    }
    game.shutdown();
    audio::stop();
    sys::shutdown();
    sceKernelExitProcess(0);
    return 0;
}

// vita-elf-create needs a few KB free at the end of the code segment for its own tables; when the
// code happens to end right at the segment boundary it fails ("segment 1 overlaps"). This read-only
// filler moves the boundary. If that error ever comes back, change its size by a few KB.
extern "C" __attribute__((used, aligned(16))) const char fl_segment_pad[6144] = {1};
