// Everything the game needs from the machine: drawing, buttons, sound, files, time.
// platform_psp.cpp implements it with the PSP's GU graphics chip; platform_pc.cpp with a small
// software renderer so the whole game can be tested (and screenshotted) on a PC.
#pragma once
#include <cstdint>
#include "math3d.h"

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

#define SCREEN_W 480
#define SCREEN_H 272

// Colours are 0xAABBGGRR like the PSP.
inline u32 RGBA(int r, int g, int b, int a = 255) { return (u32)(r & 255) | ((u32)(g & 255) << 8) | ((u32)(b & 255) << 16) | ((u32)(a & 255) << 24); }

// 8-bit palette texture (swizzled like the PSP wants), size w x h (powers of two).
struct Texture {
    int w = 0, h = 0;
    const u32* palette = nullptr;   // 256 entries ABGR
    const u8* pixels = nullptr;     // swizzled 16x8 blocks
};

// Model vertex (16 bytes): GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT
struct ModelVertex { u16 u, v; u32 color; int16_t x, y, z, pad; };
// World vertex (24 bytes): GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF
struct WorldVertex { float u, v; u32 color; float x, y, z; };

enum Prim { PRIM_TRIANGLES = 0, PRIM_TRIANGLE_STRIP = 1, PRIM_LINES = 2, PRIM_LINE_STRIP = 3 };

namespace gfx {
void beginFrame(u32 clearColor);
// wait for the screen's refresh before showing a frame (no tearing); every = 2 halves the frame rate to 30
void setSync(bool vsync, int every);
void endFrame();
void clearDepth();
// draw the 3D scene into a small rectangle of the screen (480 x 272 units), cleared to clearColor.
// false where it isn't supported (the PSP); endInset() goes back to the whole screen
bool beginInset(float x, float y, float w, float h, u32 clearColor);
void endInset();
void setProjection(const M4& p);
void setView(const M4& v);
void setFog(bool on, float nearDist, float farDist, u32 color);
void bindTexture(const Texture* t);          // nullptr = vertex colour only
void setBlend(bool on);
void setDepthTest(bool on);
void setDepthWrite(bool on);
// 16-bit model: positions are -1..1 (value/32768); `model` must include the size scale.
void drawModel(const ModelVertex* v, int count, const M4& model);
// float vertices; `model` places them (usually just a translation relative to the camera)
void drawWorld(const WorldVertex* v, int count, Prim prim, const M4& model);
// screen-space textured rectangles (sprites); colour multiplies the texture
void drawRect2D(float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 color);
void drawFill2D(float x, float y, float w, float h, u32 color);
// many rectangles in one draw call (text, map dots); uses the bound texture (or none)
struct Sprite2D { float x, y, w, h, u0, v0, u1, v1; u32 color; };
void drawSprites2D(const Sprite2D* s, int n);
// per-frame scratch memory for vertices that are built while drawing (freed at endFrame)
void* frameAlloc(int bytes);
// give back the end of the LAST frameAlloc block: only the first keepBytes of it are used
void frameTrim(void* p, int keepBytes);
void flushCache(const void* p, int bytes);   // make data written by the CPU visible to the GPU
// true where the graphics chip clips big triangles itself (the Vita): clip.h then draws straight through
bool gpuClips();
// Copy a texture's pixels into video memory (faster for the GPU to read). Returns false when full.
// fastMark / fastRelease free everything moved after a mark (used for the cars of one race).
bool moveToFastMemory(Texture& t);
int fastMark();
int vramUsedKB();            // video memory in use (screen buffers + textures moved there)
float lastWaitSec();         // how long the last frame waited for the screen (vsync): idle time, not work
float lastGpuWaitSec();      // how long the CPU waited for the graphics chip to finish the previous frame
void fastRelease(int mark);
}

struct Pad {
    float stickX = 0, stickY = 0;   // -1..1
    bool up, down, left, right, cross, circle, square, triangle, l, r, start, select;
    // PS Vita extras (zero elsewhere)
    float rstickX = 0, rstickY = 0;     // right stick, -1..1 (dead zone applied)
    bool hasTilt = false; float tilt = 0;   // motion sensor: roll of the console in radians (+ = turned right)
    int touches = 0; float tx[2] = {0, 0}, ty[2] = {0, 0};   // front touch screen, in 480 x 272 screen units
};
namespace input {
void poll(Pad& p);
bool hasTouch();                // a touch screen (the PS Vita)
}

namespace audio {
typedef void (*MixFn)(int16_t* stereo, int frames);   // 44100 Hz, interleaved L R
void start(MixFn fn);
void stop();
}

namespace sys {
bool init();
void shutdown();
bool running();
double timeSec();
// whole file into memory (malloc'd, 16-byte aligned); nullptr if missing
void* loadFile(const char* path, int* size);
void freeFile(void* p);
void log(const char* fmt, ...);
void quit();                 // leave the game (the benchmark ends this way)
int memUsedKB();             // heap in use (malloc), for the performance log
}
