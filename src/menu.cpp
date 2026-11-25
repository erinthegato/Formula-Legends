// The menus live inside a 3D pit garage: the car sits in its box, and every menu is a screen or
// board standing in the garage (a monitor on a stand, the big wall screen, a pit board). The camera
// walks from one to the next. The same panel code draws the settings flat over a paused race.
#include "trophies.h"
#include "setups.h"
#include "game.h"
#include "clip.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

static const u32 WHITE = RGBA(255, 255, 255), DIM = RGBA(175, 185, 200), ACC = RGBA(255, 150, 40), GOLD = RGBA(255, 215, 70), INK = RGBA(20, 20, 20);
static const char* DIFF[3] = {"Easy", "Normal", "Hard"};
static const char* STARTP[3] = {"Pole", "Middle", "Back"};
static const char* STARTT[4] = {"Standing", "Rolling", "Standing", "Standing"};
static const char* WXN[4] = {"Changeable", "Dry", "Wet", "Thunderstorm"};
static const char* CAMS[CAM_COUNT] = {"Chase", "Far chase", "Cockpit"};

// ------------------------------------------------------------------ a screen in the world
struct Panel {
    Renderer* R = nullptr;
    bool flat = false;            // true: drawn straight onto the display (pause menu)
    M4 m;                         // centre of the screen; x right, y up, z towards the viewer
    float vw = 480, vh = 272;     // layout size in "pixels"
    float k = 0.0075f;            // metres per pixel
    std::vector<WorldVertex> fills, glyphs;

    void quad(std::vector<WorldVertex>& o, float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 col, float z) {
        float x0 = (x - vw * 0.5f) * k, x1 = (x + w - vw * 0.5f) * k, y1 = (vh * 0.5f - y) * k, y0 = (vh * 0.5f - y - h) * k;
        WorldVertex a{u0, v1, col, x0, y0, z}, b{u1, v1, col, x1, y0, z}, c{u1, v0, col, x1, y1, z}, d{u0, v0, col, x0, y1, z};
        o.push_back(a); o.push_back(b); o.push_back(c); o.push_back(a); o.push_back(c); o.push_back(d);
    }
    void fill(float x, float y, float w, float h, u32 col) {
        if (flat) { gfx::drawFill2D(x, y, w, h, col); return; }
        quad(fills, x, y, w, h, 0, 0, 0, 0, col, 0.004f);
    }
    float width(const char* s, float scale = 1) const { return R->font.width(s, scale); }
    void text(float x, float y, const char* s, u32 col, float scale = 1) {
        const Font& f = R->font;
        if (flat) { f.draw(x, y, s, col, scale); return; }
        for (; *s; s++) {
            int c = (unsigned char)*s; if (c < 32 || c > 126) c = '?';
            const Font::Glyph& g = f.g[c - 32];
            if (c != ' ') quad(glyphs, x + g.xoff * scale, y, g.w * scale, g.h * scale, f.u(g.x), f.v(g.y),
                               f.u(g.x + g.w), f.v(g.y + g.h), col, 0.008f);
            x += g.adv * scale;
        }
    }
    void textC(float x, float y, const char* s, u32 col, float scale = 1) { text(x - width(s, scale) * 0.5f, y, s, col, scale); }
    void textR(float x, float y, const char* s, u32 col, float scale = 1) { text(x - width(s, scale), y, s, col, scale); }
    // circuit outline (and nothing else) as dots
    void map(const Track& t, float x, float y, float size, u32 col) {
        float kk = size * t.mapScale;
        int stride = (int)(1.2f / (kk * t.step)); if (stride < 1) stride = 1;
        for (int i = 0; i < t.n; i += stride)
            fill(x + (t.pts[i].p.x - t.mapMinX) * kk - 1, y + size - (t.pts[i].p.y - t.mapMinY) * kk - 1, 2.2f, 2.2f, col);
        fill(x + (t.pts[0].p.x - t.mapMinX) * kk - 2, y + size - (t.pts[0].p.y - t.mapMinY) * kk - 2, 4, 4, RGBA(255, 150, 40));
    }
    void flush(const V3& eye) {
        if (flat) return;
        M4 mm = M4::translate(V3(0, 0, 0) - eye) * m;
        gfx::setBlend(true); gfx::setDepthWrite(false);
        for (int pass = 0; pass < 2; pass++) {
            std::vector<WorldVertex>& v = pass ? glyphs : fills;
            if (v.empty()) continue;
            int bytes = (int)(v.size() * sizeof(WorldVertex));
            WorldVertex* p = (WorldVertex*)gfx::frameAlloc(bytes);
            if (p) {
                memcpy(p, v.data(), bytes);
                gfx::flushCache(p, bytes);
                gfx::bindTexture(pass ? &R->font.tex : nullptr);
                gfx::drawWorld(p, (int)v.size(), PRIM_TRIANGLES, mm);
            }
            v.clear();
        }
        gfx::setDepthWrite(true); gfx::setBlend(false);
    }
};

static void fmtLength(char* out, const RaceSettings& s) {
    if (s.raceMin <= 0 && s.laps >= 999) { snprintf(out, 24, "Full GP (%d laps)", TRACKS[s.track].gpLaps); return; }
    if (s.raceMin <= 0) { snprintf(out, 24, "%d lap%s", s.laps, s.laps == 1 ? "" : "s"); return; }
    if (s.raceMin >= 60) snprintf(out, 24, "%d h%s", s.raceMin / 60, s.raceMin % 60 ? " 30" : "");
    else snprintf(out, 24, "%d min", s.raceMin);
}

// how fast the race clock runs: two short tip lines ("30 min real = 6 h at the track" /
// "Clock x12: 1 real min = 12 min of race time"). A lap race runs in real time.
static void fmtSpan(char* out, int n, float minutes) {
    int m = (int)(minutes + 0.5f);
    if (m >= 60) { if (m % 60) snprintf(out, n, "%d h %d min", m / 60, m % 60); else snprintf(out, n, "%d h", m / 60); }
    else snprintf(out, n, "%d min", m);
}
static void clockTip(char* a, char* b, int n, const RaceSettings& s) {
    if (s.raceMin <= 0) {
        snprintf(a, n, "TIP: a lap race runs in real time");
        snprintf(b, n, "(the clock, fuel and tyres all at x1)");
        return;
    }
    float hours = s.eventHours > 0 ? s.eventHours : TRACKS[s.track].hours;
    float scale = hours * 60.0f / s.raceMin;
    char r[24], e[24], one[24];
    fmtSpan(r, sizeof r, (float)s.raceMin); fmtSpan(e, sizeof e, hours * 60); fmtSpan(one, sizeof one, scale);
    snprintf(a, n, "TIP: %.15s real = %.15s at the track", r, e);
    snprintf(b, n, "Clock x%.0f: 1 real min = %.15s", scale, one);
}

// ------------------------------------------------------------------ the garage
namespace {
const V3 Z(0, 0, 1);
struct Station { V3 eye, target; float fov; };
// where the camera stands for each group of screens
const Station ST_TITLE = {V3(3.7f, -5.7f, 1.7f), V3(-0.4f, 0.9f, 0.85f), 46};
const Station ST_CAR = {V3(-4.7f, -4.7f, 1.7f), V3(1.1f, 0.4f, 0.6f), 46};
const Station ST_SETUP = {V3(4.8f, 3.5f, 1.7f), V3(-0.4f, -1.2f, 0.7f), 46};
const Station ST_WALL = {V3(-2.35f, 1.72f, 1.95f), V3(-2.6f, 4.9f, 2.0f), 37};

std::vector<WorldVertex> garage;
size_t standsFirst = 0;           // the monitors on stands are the last part of the garage (left out in the showroom)
Panel pTitle, pCarList, pCarInfo, pSetup, pSetupInfo, pWall;
bool built = false;

void gq(const V3& a, const V3& b, const V3& c, const V3& d, u32 ca, u32 cb) {
    WorldVertex A{0, 0, ca, a.x, a.y, a.z}, B{0, 0, ca, b.x, b.y, b.z}, C{0, 0, cb, c.x, c.y, c.z}, D{0, 0, cb, d.x, d.y, d.z};
    garage.push_back(A); garage.push_back(B); garage.push_back(C); garage.push_back(A); garage.push_back(C); garage.push_back(D);
}
u32 shade(u32 c, float k) { return RGBA((int)((c & 255) * k), (int)(((c >> 8) & 255) * k), (int)(((c >> 16) & 255) * k)); }
void gbox(const V3& lo, const V3& hi, u32 col) {
    V3 a(lo.x, lo.y, lo.z), b(hi.x, lo.y, lo.z), c(hi.x, hi.y, lo.z), d(lo.x, hi.y, lo.z);
    V3 e(lo.x, lo.y, hi.z), f(hi.x, lo.y, hi.z), g(hi.x, hi.y, hi.z), h(lo.x, hi.y, hi.z);
    gq(e, f, g, h, col, col);                                           // top
    gq(a, b, f, e, shade(col, 0.8f), shade(col, 0.8f)); gq(d, c, g, h, shade(col, 0.7f), shade(col, 0.7f));
    gq(a, d, h, e, shade(col, 0.6f), shade(col, 0.6f)); gq(b, c, g, f, shade(col, 0.9f), shade(col, 0.9f));
}

// a monitor on a stand, placed in front of a camera station (so it reads well from there)
Panel standPanel(Renderer* R, const Station& s, float dist, float rightOff, float upOff, float vw, float vh, float k, float yaw) {
    V3 f = norm(s.target - s.eye), right = norm(cross(f, Z));
    V3 pos = s.eye + f * dist + right * rightOff + Z * upOff;
    V3 n = norm(V3(-f.x, -f.y, 0));
    float c = cosf(yaw), sn = sinf(yaw);
    n = V3(n.x * c - n.y * sn, n.x * sn + n.y * c, 0);
    V3 pr = cross(Z, n);
    Panel p; p.R = R; p.vw = vw; p.vh = vh; p.k = k;
    p.m = M4::basis(pr, Z, n, pos);
    // screen, bezel and stand
    float w = vw * k * 0.5f, h = vh * k * 0.5f, e = 0.035f;
    V3 back = pos - n * 0.01f;
    gq(back - pr * (w + e) - Z * (h + e), back + pr * (w + e) - Z * (h + e), back + pr * (w + e) + Z * (h + e), back - pr * (w + e) + Z * (h + e), RGBA(30, 32, 38), RGBA(44, 46, 54));
    gq(pos - pr * w - Z * h, pos + pr * w - Z * h, pos + pr * w + Z * h, pos - pr * w + Z * h, RGBA(8, 11, 18), RGBA(14, 18, 28));
    V3 foot = pos - n * 0.05f;
    float bottom = pos.z - h - e;
    if (bottom > 0.1f) {
        gbox(V3(foot.x - 0.03f, foot.y - 0.03f, 0), V3(foot.x + 0.03f, foot.y + 0.03f, bottom), RGBA(60, 62, 70));
        gbox(V3(foot.x - 0.25f, foot.y - 0.25f, 0), V3(foot.x + 0.25f, foot.y + 0.25f, 0.04f), RGBA(50, 52, 58));
    }
    return p;
}

void buildGarage(Renderer* R) {
    built = true;
    garage.clear();
    const float W = 7, D = 5, H = 4.6f;
    u32 floorC = RGBA(96, 100, 108), wallC = RGBA(196, 200, 208), wallD = RGBA(150, 154, 162);
    // floor in strips (lighter under the lights), the painted car box
    for (int i = 0; i < 7; i++) {
        float x0 = -W + i * 2.0f, x1 = x0 + 2.0f;
        u32 c0 = shade(floorC, 0.85f + 0.15f * (i % 2));
        gq(V3(x0, -D, 0), V3(x1, -D, 0), V3(x1, D, 0), V3(x0, D, 0), shade(c0, 1.08f), shade(c0, 0.82f));
    }
    u32 yel = RGBA(240, 200, 40);
    for (int s = -1; s <= 1; s += 2) {
        gq(V3(s * 1.5f - 0.05f, -3.2f, 0.005f), V3(s * 1.5f + 0.05f, -3.2f, 0.005f), V3(s * 1.5f + 0.05f, 3.0f, 0.005f), V3(s * 1.5f - 0.05f, 3.0f, 0.005f), yel, yel);
        gq(V3(-1.5f, s * 3.1f - 0.05f, 0.005f), V3(1.5f, s * 3.1f - 0.05f, 0.005f), V3(1.5f, s * 3.1f + 0.05f, 0.005f), V3(-1.5f, s * 3.1f + 0.05f, 0.005f), yel, yel);
    }
    // walls: back, sides, front either side of the door and the lintel above it
    gq(V3(-W, D, 0), V3(W, D, 0), V3(W, D, H), V3(-W, D, H), wallD, wallC);
    gq(V3(-W, -D, 0), V3(-W, D, 0), V3(-W, D, H), V3(-W, -D, H), shade(wallD, 0.85f), shade(wallC, 0.85f));
    gq(V3(W, -D, 0), V3(W, D, 0), V3(W, D, H), V3(W, -D, H), shade(wallD, 0.92f), shade(wallC, 0.92f));
    for (int s = -1; s <= 1; s += 2)
        gq(V3(s * W, -D, 0), V3(s * 4.2f, -D, 0), V3(s * 4.2f, -D, H), V3(s * W, -D, H), shade(wallD, 0.7f), shade(wallC, 0.7f));
    gq(V3(-4.2f, -D, 3.5f), V3(4.2f, -D, 3.5f), V3(4.2f, -D, H), V3(-4.2f, -D, H), shade(wallC, 0.65f), shade(wallC, 0.7f));
    // ceiling with light strips
    gq(V3(-W, -D, H), V3(W, -D, H), V3(W, D, H), V3(-W, D, H), RGBA(58, 60, 66), RGBA(48, 50, 56));
    for (int i = -1; i <= 1; i++)
        gq(V3(i * 4.0f - 0.2f, -3.5f, H - 0.03f), V3(i * 4.0f + 0.2f, -3.5f, H - 0.03f), V3(i * 4.0f + 0.2f, 3.5f, H - 0.03f), V3(i * 4.0f - 0.2f, 3.5f, H - 0.03f), RGBA(255, 255, 245), RGBA(255, 255, 245));
    // outside the door: the pit lane, the pit wall with its fence, the track and the far bank
    gq(V3(-30, -15, -0.02f), V3(30, -15, -0.02f), V3(30, -D, -0.02f), V3(-30, -D, -0.02f), RGBA(84, 86, 90), RGBA(74, 76, 80));
    gq(V3(-30, -9.0f, -0.01f), V3(30, -9.0f, -0.01f), V3(30, -8.8f, -0.01f), V3(-30, -8.8f, -0.01f), RGBA(235, 235, 235), RGBA(235, 235, 235));
    for (int i = -6; i < 6; i++)
        gbox(V3(i * 5.0f, -15.3f, 0), V3(i * 5.0f + 5.0f, -15.0f, 1.1f), (i & 1) ? RGBA(200, 40, 40) : RGBA(225, 225, 220));
    gq(V3(-30, -32, -0.03f), V3(30, -32, -0.03f), V3(30, -15, -0.03f), V3(-30, -15, -0.03f), RGBA(56, 58, 62), RGBA(62, 64, 68));
    gq(V3(-60, -90, -0.04f), V3(60, -90, -0.04f), V3(60, -32, -0.04f), V3(-60, -32, -0.04f), RGBA(70, 120, 60), RGBA(80, 135, 66));
    gq(V3(-40, -40, 0), V3(40, -40, 0), V3(40, -44, 7), V3(-40, -44, 7), RGBA(120, 124, 132), RGBA(150, 156, 166));   // far grandstand
    // furniture: tool chests, a workbench, shelves
    gbox(V3(5.6f, 2.4f, 0), V3(6.7f, 4.4f, 1.05f), RGBA(190, 40, 36));
    gbox(V3(5.7f, 2.5f, 1.05f), V3(6.6f, 3.3f, 1.5f), RGBA(170, 34, 30));
    gbox(V3(2.2f, 4.3f, 0.85f), V3(5.2f, 4.95f, 0.92f), RGBA(120, 96, 70));
    gbox(V3(2.3f, 4.4f, 0), V3(2.4f, 4.9f, 0.85f), RGBA(70, 72, 78)); gbox(V3(5.0f, 4.4f, 0), V3(5.1f, 4.9f, 0.85f), RGBA(70, 72, 78));
    gbox(V3(-6.9f, -1.0f, 0), V3(-6.3f, 1.4f, 2.1f), RGBA(74, 78, 90));
    // the wall screen's bezel
    gbox(V3(-4.5f, 4.88f, 0.92f), V3(-0.7f, 4.99f, 3.08f), RGBA(30, 32, 38));
    gq(V3(-4.42f, 4.875f, 0.96f), V3(-0.78f, 4.875f, 0.96f), V3(-0.78f, 4.875f, 3.04f), V3(-4.42f, 4.875f, 3.04f), RGBA(8, 11, 18), RGBA(14, 18, 28));
    pWall = Panel(); pWall.R = R; pWall.vw = 480; pWall.vh = 272; pWall.k = 0.0075f;
    pWall.m = M4::basis(V3(1, 0, 0), Z, V3(0, -1, 0), V3(-2.6f, 4.87f, 2.0f));
    // monitors on stands for the other stations
    standsFirst = garage.size();
    pTitle = standPanel(R, ST_TITLE, 2.3f, 0.74f, 0.02f, 200, 176, 0.0072f, -0.22f);
    pCarList = standPanel(R, ST_CAR, 2.2f, -0.86f, 0.0f, 200, 262, 0.0068f, 0.16f);
    pCarInfo = standPanel(R, ST_CAR, 2.0f, 0.62f, -0.62f, 270, 58, 0.0052f, -0.1f);
    pSetup = standPanel(R, ST_SETUP, 2.2f, -0.74f, 0.0f, 236, 258, 0.0068f, 0.14f);
    pSetupInfo = standPanel(R, ST_SETUP, 2.0f, 0.78f, -0.62f, 250, 44, 0.0052f, -0.1f);
    gfx::flushCache(garage.data(), (int)garage.size() * (int)sizeof(WorldVertex));
}

const Station& stationFor(Screen s) {
    switch (s) {
        case SCR_TITLE: return ST_TITLE;
        case SCR_CAR: return ST_CAR;
        case SCR_SETTINGS: return ST_SETUP;
        case SCR_LIVERY: return ST_CAR;
        default: return ST_WALL;
    }
}
}   // namespace

// ------------------------------------------------------------------ drawing the garage and its screens
static void header(Panel& P, const char* title) {
    P.fill(0, 0, P.vw, 34, RGBA(0, 0, 0, 150));
    P.fill(0, 34, P.vw, 2, ACC);
    P.text(14, 6, title, WHITE, 1.4f);
    P.fill(0, P.vh - 20, P.vw, 20, RGBA(0, 0, 0, 150));
}


void Game::drawMenus(float dt) {
    if (screen == SCR_HQ) { drawHQ(dt); return; }
    if (!built) buildGarage(&R);
    // the camera walks to the station of the current screen
    // the car in its box: the highlighted car, the chosen car, or (on the title) each car in turn
    // (the main menu shows the picked era's cars in turn; the showroom the one being looked at)
    int showCar = screen == SCR_CAR ? sel : (screen == SCR_TITLE || screen == SCR_HELP ? eraFirstCar(era) + ((int)(t / 7) + set.car) % TEAMS_PER_ERA : set.car);
    if (screen == SCR_SHOWROOM) showCar = showroomCar;
    bool careerScreen = screen == SCR_CAREER || screen == SCR_LIVERY || (screen == SCR_CAREER_NEW && career.active);
    if (careerScreen && career.active) showCar = career.carIndex();
    R.loadCar(showCar);
    Car boxCar;
    {
        float a = 0.32f + (screen == SCR_CAR ? sinf(t * 0.4f) * 0.25f : 0) + (screen == SCR_LIVERY ? t * 0.45f : 0);   // the livery editor turns the car round
        boxCar.def = &CARS[showCar]; boxCar.number = CARS[showCar].numbers[0];
        boxCar.fwd = V3(sinf(a), -cosf(a), 0); boxCar.left = V3(cosf(a), sinf(a), 0); boxCar.up = Z; boxCar.pos = V3(0, 0, 0);
    }
    // race settings: sitting in the car (the tablet ahead, the diagnostics computer to the right)
    look += ((setupPage ? 1.0f : 0.0f) - look) * (1 - expf(-dt * 6));
    Station st = stationFor(screen);
    if (screen == SCR_SHOWROOM) {
        // walking round the car (kept inside the garage's walls)
        float ex = cosf(showYaw) * showDist, ey = sinf(showYaw) * showDist * 0.78f;
        st.eye = V3(ex, ey, 1.05f + 0.12f * showDist); st.target = V3(0, 0, 0.45f); st.fov = 46;
    }
    Camera inCam = inCarCamera(boxCar, look);
    if (screen == SCR_SETTINGS) { st.eye = inCam.eye; st.target = inCam.eye + inCam.fwd * 3; st.fov = inCam.fov; }
    if (!menuCamInit) { menuEye = st.eye; menuTarget = st.target; menuFov = st.fov; menuCamInit = true; }
    float kk = 1 - expf(-dt * 4.5f);
    menuEye = menuEye + (st.eye - menuEye) * kk;
    menuTarget = menuTarget + (st.target - menuTarget) * kk;
    menuFov += (st.fov - menuFov) * kk;
    bool seated = screen == SCR_SETTINGS && len(menuEye - st.eye) < 0.3f;
    if (seated) { menuEye = st.eye; menuTarget = st.target; }
    // a gentle sway so the scene is never frozen (not when seated)
    V3 eye = seated ? menuEye : menuEye + V3(sinf(t * 0.5f) * 0.03f, 0, sinf(t * 0.37f) * 0.015f);
    V3 fwd = seated ? inCam.fwd : norm(menuTarget - eye);

    gfx::beginFrame(RGBA(126, 172, 228));
    M4 proj = M4::perspective(menuFov, (float)SCREEN_W / SCREEN_H, 0.3f, 140.0f), view = M4::lookDir(V3(0, 0, 0), fwd, Z);
    gfx::setProjection(proj); gfx::setView(view);
    clip::setMatrices(proj, view);
    gfx::setFog(false, 0, 1, 0);
    gfx::setDepthTest(true); gfx::setDepthWrite(true); gfx::setBlend(false);
    gfx::bindTexture(nullptr);
    // (the showroom walks round the car: the monitors on their stands, the crew and the props are put away)
    bool showroom = screen == SCR_SHOWROOM;
    clip::drawWorld(garage.data(), (int)(showroom ? standsFirst : garage.size()), M4::translate(V3(0, 0, 0) - eye));
    R.camRight = norm(cross(fwd, Z)); R.camUp = cross(R.camRight, fwd);
    float tanV = tanf(menuFov * 3.14159f / 360.0f), tanH = tanV * SCREEN_W / SCREEN_H;

    if (!seated) R.drawCarAt(boxCar, eye, fwd, tanH, tanV);
    // team stripe along the walls in the car's colour, the name of the game above it
    {
        u32 col = CARS[showCar].colour | 0xFF000000u;
        M4 wall = M4::translate(V3(0, 0, 0) - eye) * M4::basis(V3(1, 0, 0), Z, V3(0, -1, 0), V3(0, 4.985f, 0));
        R.quad3D(wall, -7, 0.0f, 7, 0.75f, col);
        R.quad3D(wall, -7, 3.30f, 7, 3.36f, col);
        R.text3D(wall * M4::translate(V3(-0.6f, 3.85f, 0.01f)), "FORMULA LEGENDS", 0.46f, RGBA(40, 44, 54), true);
        R.text3D(wall * M4::translate(V3(3.4f, 2.2f, 0.01f)), CARS[showCar].team, 0.34f, col, true);
    }
    // crew, fuel rig, tyres, spare wheels
    auto prop = [&](const Model& md, const V3& at, float yaw, float sc) {
        if (!md.lodCount) return;
        gfx::bindTexture(&md.lod[0].tex);
        M4 place = M4::translate(at - eye) * M4::rotZ(yaw) * M4::scale(sc, sc, sc);
        clip::drawModel(md.lod[0].verts, md.lod[0].count, md.matrix(place));
    };
    if (!showroom) {
        const V3 at[4] = {V3(-5.5f, 0.9f, 0), V3(1.5f, 3.9f, 0), V3(-3.9f, -1.6f, 0), V3(-5.2f, -6.8f, 0)};
        const float yaw[4] = {-0.3f, -1.8f, 0.5f, 1.2f};
        u32 tm, ts; R.teamColours(&CARS[showCar], tm, ts);     // the garage's crew in the team's colours
        for (int k = 0; k < 4; k++) R.drawPerson(M4::translate(at[k] - eye) * M4::rotZ(yaw[k]), Renderer::ANIM_IDLE, t + k * 0.6f, true, tm, ts);
    }
    if (!showroom) prop(R.fuelRig, V3(-5.0f, -2.8f, 0), 0.4f, 1);
    prop(R.prop[PROP_TYRES], V3(-5.9f, 3.9f, 0), 0, 1); prop(R.prop[PROP_TYRES], V3(-5.9f, 2.5f, 0), 0.5f, 1); prop(R.prop[PROP_TYRES], V3(6.0f, -3.9f, 0), 0.2f, 1);
    for (int i = 0; i < 4; i++) prop(R.wheel, V3(3.0f + i * 0.5f, 4.55f, 1.28f), 1.5708f, 1);

    // ---- the screens
    switch (screen) {
        case SCR_TITLE: screenTitle(pTitle); break;
        case SCR_CAR: screenCar(pCarList, pCarInfo); break;
        case SCR_TRACK: screenTrack(pWall); break;
        case SCR_CHAMP: screenChamp(pWall); break;
        case SCR_CAREER: screenCareer(pWall); break;
        case SCR_CAREER_NEW: screenCareerNew(pWall); break;
        case SCR_LIVERY: screenLivery(pCarList, pCarInfo); break;
        case SCR_LOAD: screenLoad(pWall); break;
        case SCR_HELP: screenHelp(pWall); break;
        case SCR_TROPHIES: screenTrophies(pWall); break;
        case SCR_OPTIONS: screenOptions(pWall); break;
        case SCR_CONTROLS: screenControls(pWall); break;
        case SCR_REALISM: screenRealism(pWall); break;
        default: break;
    }
    // screens that are not in use show a standby picture
    Panel* all[6] = {&pTitle, &pCarList, &pCarInfo, &pSetup, &pSetupInfo, &pWall};
    for (Panel* p : all) {
        if (showroom && p != &pWall) { p->fills.clear(); p->glyphs.clear(); continue; }
        if (p->fills.empty() && p->glyphs.empty()) {
            if (p == &pWall) {
                p->textC(240, 96, TRACKS[set.track].name, WHITE, 1.5f);
                p->textC(240, 132, TRACKS[set.track].location, DIM, 0.9f);
                p->fill(40, 170, 400, 2, ACC);
            } else if (p == &pTitle) p->textC(p->vw / 2, p->vh / 2 - 10, "MAIN MENU", DIM, 1.0f);
            else p->fill(p->vw * 0.5f - 20, p->vh * 0.5f - 1, 40, 2, ACC);
        }
        p->flush(eye);
    }
    if (seated) {
        Camera c; c.eye = eye; c.fwd = fwd; c.up = Z; c.fov = menuFov;
        M4 carRel, wheelM;
        R.drawCabin(boxCar, c, carRel, wheelM);
        drawInCarPanels(boxCar, c, carRel, false);
    }
    if (screen == SCR_SHOWROOM) drawShowroomInfo();
    nowPlayingBar();
    if (cfg.showFps) drawFps();
    finishFrame();
}

// music: what's playing and the L / R skip (flat, in the corner)
void Game::nowPlayingBar() {
    const char* np = sound::nowPlaying();
    if (!np[0] || sound::trackTime() > 6.0f) return;
    char b[96]; snprintf(b, sizeof b, "%s   rear pad: skip", np);
    float w = R.font.width(b, 0.66f) + 14;
    gfx::setDepthTest(false); gfx::setBlend(true);
    gfx::drawFill2D(SCREEN_W - w - 4, SCREEN_H - 20, w, 16, RGBA(0, 0, 0, 160));
    R.font.draw(SCREEN_W - w + 3, SCREEN_H - 19, b, RGBA(255, 200, 120), 0.66f);
    gfx::setBlend(false); gfx::setDepthTest(true);
}

// ------------------------------------------------------------------ in the car: tablet and diagnostics computer
static const float SETUP_LOOK = 0.80f;          // how far the driver turns to the right to see the setup screen (radians)

Camera Game::inCarCamera(const Car& c, float lk) const {
    Camera cam;
    int idx = (int)(c.def - CARS);
    V3 e = R.carLoaded[idx] ? R.cockpitEye(idx) : V3(0, 0, 1);
    M4 place = M4::basis(c.fwd, c.left, c.up, c.pos);
    cam.eye = place.point(e);
    float th = lk * SETUP_LOOK;                 // the setup screen sits ahead and to the right, inside the car
    cam.fwd = norm(c.fwd * cosf(th) - c.left * sinf(th) + c.up * (-0.10f - 0.04f * lk));
    cam.up = c.up; cam.fov = 62;
    return cam;
}

bool Game::parcFerme() const { return raceLoaded && (race.session == SES_QUALI || race.session == SES_RACE); }

void Game::drawInCarPanels(const Car& c, const Camera& cam, const M4& carRel, bool trackGarage) {
    int idx = (int)(c.def - CARS);
    V3 eL = R.cockpitEye(idx) - V3(R.carModel[idx].cabX, 0, 0);       // eye in cabin space
    M4 carW = M4::translate(cam.eye) * carRel;                        // cabin space -> world
    M4 rel = M4::translate(V3(0, 0, 0) - cam.eye);
    gfx::setFog(false, 0, 1, 0);
    if (!trackGarage) {
        // the race-settings tablet clipped above the dash
        Panel P; P.R = &R; P.vw = 236; P.vh = 258; P.k = 0.00135f;
        P.m = carW * M4::translate(eL + V3(0.38f, 0, -0.045f)) * M4::basis(V3(0, -1, 0), V3(0, 0, 1), V3(-1, 0, 0), V3(0, 0, 0));
        float w = P.vw * P.k * 0.5f, h = P.vh * P.k * 0.5f;
        // drawn on top of the cabin: the hypercars' wheel sits high enough to cover its lower half
        gfx::setDepthTest(false);
        R.quad3D(rel * P.m * M4::translate(V3(0, 0, -0.006f)), -0.02f, -h - 0.25f, 0.02f, -h, RGBA(40, 42, 48));   // mount
        R.quad3D(rel * P.m * M4::translate(V3(0, 0, -0.004f)), -w - 0.014f, -h - 0.014f, w + 0.014f, h + 0.014f, RGBA(24, 26, 30));
        R.quad3D(rel * P.m * M4::translate(V3(0, 0, -0.002f)), -w, -h, w, h, RGBA(8, 11, 18));
        screenSetupTablet(P);
        P.flush(cam.eye);
        gfx::setDepthTest(true);
    }
    // the diagnostics computer: a screen on an arm inside the cabin, ahead and to the right of the driver,
    // closer than the roll cage and the door so nothing of the car gets in the way. Drawn on top of the cabin.
    {
        Panel P; P.R = &R; P.vw = 300; P.vh = 230; P.k = 0.00165f;
        const float dist = 0.50f, a = SETUP_LOOK, down = 0.13f;
        V3 at = eL + V3(dist * cosf(a), -dist * sinf(a), -dist * down);
        V3 right(-sinf(a), -cosf(a), 0), up(0, 0, 1), toEye(-cosf(a), sinf(a), 0);
        P.m = carW * M4::translate(at) * M4::basis(right, up, toEye, V3(0, 0, 0));
        float w = P.vw * P.k * 0.5f, h = P.vh * P.k * 0.5f;
        M4 m = rel * P.m;
        gfx::setDepthTest(false);
        R.quad3D(m * M4::translate(V3(0, 0, -0.03f)), -0.012f, -h - 0.16f, 0.012f, -h, RGBA(52, 54, 60));        // arm
        R.quad3D(m * M4::translate(V3(0, 0, -0.01f)), -w - 0.012f, -h - 0.012f, w + 0.012f, h + 0.012f, RGBA(28, 30, 36));
        R.quad3D(m * M4::translate(V3(0, 0, -0.005f)), -w, -h, w, h, RGBA(8, 11, 18));
        screenSetupComputer(P, trackGarage);
        P.flush(cam.eye);
        gfx::setDepthTest(true);
    }
}

// the race settings, on the tablet in the car
void Game::screenSetupTablet(Panel& P) {
    P.fill(0, 0, P.vw, 22, RGBA(0, 0, 0, 150));
    P.fill(0, 22, P.vw, 2, ACC);
    P.text(8, 3, champMode ? "SEASON SETTINGS" : "RACE SETTINGS", WHITE, 0.9f);
    char v[15][48], len_[24];
    fmtLength(len_, set);
    snprintf(v[0], 48, "Length:  < %s >", len_);
    snprintf(v[1], 48, "Era:  < %s >", ERA_NAME[CARS[set.car].era]);
    snprintf(v[2], 48, "Opponents:  < %d >", set.opponents);
    snprintf(v[3], 48, "AI:  < %s >", DIFF[set.difficulty]);
    snprintf(v[4], 48, "Grid:  < %s >", STARTP[set.startPos]);
    snprintf(v[5], 48, "Start:  < %s >", STARTT[set.startType]);
    snprintf(v[6], 48, "Weather:  < %s >", WXN[set.weather]);
    snprintf(v[7], 48, "Pit stops:  < %s >", set.manualPits ? "Manual (QTE)" : "Auto crew");
    snprintf(v[8], 48, "Gears:  < %s >", set.manualGears ? "Manual" : "Auto");
    snprintf(v[9], 48, "Camera:  < %s >", CAMS[cam]);
    snprintf(v[10], 48, "Weekend:  < %s >", set.weekend ? "Practice+Quali" : "Race only");
    snprintf(v[11], 48, "Sessions:  < %d min >", set.sessionMin);
    snprintf(v[12], 48, "Change car");
    snprintf(v[13], 48, champMode && !champ.active ? "START SEASON" : (set.weekend ? "START PRACTICE" : "START RACE"));
    snprintf(v[14], 48, "Car setup  (computer) >");
    for (int i = 0; i < 15; i++) {
        float y = 27 + i * 13.2f;
        if (i == sel && !setupPage) P.fill(4, y - 1, P.vw - 8, 14, RGBA(255, 150, 40, 230));
        bool off = (i == 11 && !set.weekend);
        P.text(10, y, v[i], i == sel && !setupPage ? INK : (i == 13 ? GOLD : (i == 14 ? RGBA(140, 200, 255) : (off ? DIM : WHITE))), 0.76f);
    }
    {
        char a[64], b2[64];
        clockTip(a, b2, sizeof a, set);
        P.text(10, 228, a, RGBA(140, 200, 255), 0.62f);
        if (set.weekend && set.raceMin > 0) { size_t l = strlen(b2); snprintf(b2 + l, sizeof b2 - l, "  (P+Q x1)"); }
        P.text(10, 238, b2, RGBA(140, 200, 255), 0.62f);
    }
    P.textC(P.vw / 2, 250, "LEFT/RIGHT change  X select  O back", DIM, 0.6f);
}

// car setup on the diagnostics computer (and the session's times when in the garage at the track)
void Game::screenSetupComputer(Panel& P, bool trackGarage) {
    static const char* SOFT3[3] = {"Soft", "Medium", "Stiff"};
    static const char* GEAR3[3] = {"Short", "Medium", "Long"};
    CarSetup& su = set.setup;
    bool pf = parcFerme();
    char b[64];
    P.fill(0, 0, P.vw, 20, RGBA(0, 0, 0, 160));
    P.fill(0, 20, P.vw, 2, RGBA(80, 180, 255));
    snprintf(b, sizeof b, "DIAGNOSTICS - %s %s", CARS[set.car].make, CARS[set.car].model);
    P.text(6, 2, b, WHITE, 0.8f);
    char v[12][40];
    snprintf(v[0], 40, "Rear wing  < %d >", su.wing);
    snprintf(v[1], 40, "Springs  < %s >", SOFT3[su.susp]);
    snprintf(v[2], 40, "Gearing  < %s >", GEAR3[su.gearing]);
    snprintf(v[3], 40, "Brake front  < %d%% >", su.brakeF);
    snprintf(v[4], 40, "Brake rear  < %d%% >", su.brakeR);
    snprintf(v[5], 40, "Engine map  < %s >", MAP_NAME[su.map]);
    snprintf(v[6], 40, "Tyres  < %s >", TYRE_NAME[su.tyre]);
    snprintf(v[7], 40, "Fuel  < %d%% >", su.fuel);
    snprintf(v[8], 40, trackGarage ? "DRIVE OUT" : "< Race settings");
    snprintf(v[9], 40, "FLYING LAP");
    snprintf(v[10], 40, "Time  < x%d >", (int)(raceLoaded ? race.timeAccel : 1));
    snprintf(v[11], 40, raceLoaded && race.set.timeTrial ? "End time trial" : (raceLoaded && race.session == SES_PRACTICE ? "Skip to qualifying" : "Skip to the race"));
    int rows = trackGarage ? 12 : 9;
    bool active = setupPage || trackGarage;
    float rowH = trackGarage ? 13.6f : 18;
    for (int i = 0; i < rows; i++) {
        float y = 25 + i * rowH;
        bool sealed = pf && i < 3;
        bool timeOff = i == 10 && raceLoaded && race.set.sessionMin <= 5;
        if (i == setupSel && active) P.fill(3, y - 2, 170, rowH - 1, sealed || timeOff ? RGBA(120, 120, 130, 230) : RGBA(80, 180, 255, 230));
        u32 col = i == setupSel && active ? INK : (sealed || timeOff ? DIM : (i == 8 || i == 9 ? GOLD : WHITE));
        P.text(8, y, v[i], col, trackGarage ? 0.68f : 0.74f);
        if (sealed) P.textR(170, y + 1, "SEALED", i == setupSel && active ? INK : RGBA(255, 120, 60), 0.55f);
    }
    // read-outs on the right
    float x = 182, y = 28;
    float bias = su.bias();
    P.text(x, y, "BRAKE BALANCE", DIM, 0.58f); y += 12;
    P.fill(x, y, 110, 8, RGBA(40, 44, 54));
    P.fill(x, y, 110 * bias, 8, RGBA(80, 180, 255));
    P.fill(x + 110 * 0.63f - 1, y - 2, 2, 12, RGBA(120, 255, 140));
    snprintf(b, sizeof b, "%d%% front", (int)(bias * 100 + 0.5f)); P.text(x, y + 10, b, WHITE, 0.62f); y += 26;
    const ClassPhys& ph = Race::phys(CARS[set.car].era);
    static const float RATIO[3] = {0.95f, 1.0f, 1.05f};
    float top = ph.topKmh * (1 - (su.wing - 3) * 0.015f) * fminf(1.0f, RATIO[su.gearing] + 0.03f);
    snprintf(b, sizeof b, "Top speed ~%d km/h", (int)top); P.text(x, y, b, WHITE, 0.62f); y += 13;
    float grip = 1 + (su.wing - 3) * 0.05f;
    P.text(x, y, "Downforce", DIM, 0.58f);
    P.fill(x + 56, y + 3, 54, 6, RGBA(40, 44, 54)); P.fill(x + 56, y + 3, 54 * clampf((grip - 0.85f) / 0.3f, 0, 1), 6, RGBA(255, 190, 60)); y += 13;
    const char* bal = su.bias() < 0.6f ? "Rotates on the brakes" : (su.bias() > 0.68f ? "Stable on the brakes" : "Balanced");
    P.text(x, y, bal, RGBA(140, 200, 255), 0.6f); y += 13;
    const char* ride = su.susp == 0 ? "Soft: kerbs and rain" : (su.susp == 2 ? "Stiff: sharp, dry" : "Medium springs");
    P.text(x, y, ride, RGBA(140, 200, 255), 0.6f); y += 16;
    float trackKm = (raceLoaded ? race.track.length : (previewIdx == set.track && preview.n ? preview.length : 5000.0f)) / 1000.0f;
    static const float MAPF[MAP_COUNT] = {0.6f, 1.0f, 1.25f};
    float laps = su.fuel / 100.0f * (CARS[set.car].cls == CLASS_HYPER ? 180.0f : 190.0f) / (trackKm * MAPF[su.map]);
    snprintf(b, sizeof b, "Fuel: ~%d laps", (int)laps); P.text(x, y, b, WHITE, 0.62f); y += 16;
    if (trackGarage && raceLoaded) {
        const Car& me = race.player();
        P.fill(x - 4, y - 2, 118, 1, RGBA(80, 180, 255));
        float left = fmaxf(0, race.sessionLength - race.time);
        snprintf(b, sizeof b, "%s  %d:%02d", race.session == SES_PRACTICE ? "PRACTICE" : "QUALIFYING", (int)left / 60, (int)left % 60);
        if (race.set.timeTrial) {
            if (race.ghostLap > 0) { char t3[16]; formatTime(t3, race.ghostLap); snprintf(b, sizeof b, "TIME TRIAL  record %s", t3); }
            else snprintf(b, sizeof b, "TIME TRIAL  no record yet");
        }
        P.text(x, y + 2, b, GOLD, 0.62f); y += 15;
        char t2[16]; formatTime(t2, me.bestLap);
        snprintf(b, sizeof b, "P%d  best %s", me.position, t2); P.text(x, y, b, WHITE, 0.62f); y += 13;
        formatTime(t2, me.lastLap); snprintf(b, sizeof b, "last %s", t2); P.text(x, y, b, DIM, 0.62f); y += 13;
        const char* gr = race.rubber < 0.25f ? "green" : (race.rubber < 0.55f ? "rubbering in" : "rubbered in");
        snprintf(b, sizeof b, "Track: %s %d%%", gr, (int)(race.rubber * 100)); P.text(x, y, b, RGBA(140, 200, 255), 0.6f);
    }
    if (pf) P.textC(P.vw / 2, 194, "PARC FERME: wing, springs and gearing are sealed", RGBA(255, 120, 60), 0.6f);
    P.textC(P.vw / 2, 212, trackGarage ? "X drive out / flying lap / skip   L R music   START pause" : "LEFT/RIGHT change   O back to the race settings", DIM, 0.58f);
}

int Game::updateSetup(int dx, int dy, bool ok, bool back, bool trackGarage) {
    int rows = trackGarage ? 12 : 9;
    setupSel = (setupSel + dy + rows) % rows;
    CarSetup& su = set.setup;
    bool pf = parcFerme();
    if (dx) {
        auto step = [&](int& v, int d, int lo, int hi) { v += d; if (v < lo) v = lo; if (v > hi) v = hi; };
        switch (setupSel) {
            case 0: if (!pf) step(su.wing, dx, 1, 5); break;
            case 1: if (!pf) step(su.susp, dx, 0, 2); break;
            case 2: if (!pf) step(su.gearing, dx, 0, 2); break;
            case 3: step(su.brakeF, dx * 5, 30, 100); break;
            case 4: step(su.brakeR, dx * 5, 20, 100); break;
            case 5: step(su.map, dx, 0, MAP_COUNT - 1); break;
            case 6: step(su.tyre, dx, TY_SOFT, TY_HARD); break;
            case 7: step(su.fuel, dx * 10, 10, 100); break;
            case 10:
                if (trackGarage && race.set.sessionMin > 5) { static const int A[3] = {1, 4, 16}; int k = race.timeAccel >= 16 ? 2 : (race.timeAccel >= 4 ? 1 : 0); k = (k + dx + 3) % 3; race.timeAccel = (float)A[k]; }
                break;
        }
        if (setupSel < 3 && pf) { snprintf(note, sizeof note, "Sealed: parc ferme"); noteT = 1.5f; }
    }
    int leave = 0;
    if (ok && setupSel == 8) leave = 1;
    else if (ok && trackGarage && setupSel == 9) leave = 2;
    else if (ok && trackGarage && setupSel == 11) leave = 3;
    else if (back && !trackGarage) leave = 1;
    if (leave && !ttMode) setups.put(set.track, CARS[set.car].era, su);   // remembered for this circuit and era
    return leave;
}

// practice / qualifying: in the garage at the circuit, sitting in the car at the computer
void Game::drawGarageView() {
    Car& me = race.player();
    look += (1.0f - look) * (1 - expf(-lastDt * 6));
    gfx::beginFrame(R.skyColour(race));
    Camera c = inCarCamera(me, look);
    R.drawScene(race, c, true);
    R.drawNight(race);
    M4 carRel, wheelM;
    R.drawCabin(me, c, carRel, wheelM);
    drawInCarPanels(me, c, carRel, true);
    if (cfg.showFps) drawFps();
    finishFrame();
}

// ------------------------------------------------------------------ podium: the drivers on the steps
static bool project(const M4& pv, const V3& p, float& sx, float& sy) {
    const float* m = pv.m;
    float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (w <= 0.01f) return false;
    sx = (x / w * 0.5f + 0.5f) * SCREEN_W; sy = (0.5f - y / w * 0.5f) * SCREEN_H;
    return true;
}

void Game::drawPodium() {
    gfx::beginFrame(RGBA(60, 90, 140));
    std::vector<const Car*> cls;
    CarClass pc = race.player().def->cls;
    for (auto& c : race.cars) if (c.def->cls == pc) cls.push_back(&c);
    std::sort(cls.begin(), cls.end(), [](const Car* a, const Car* b) { return a->position < b->position; });
    V3 eye(sinf(t * 0.15f) * 0.8f, -7.6f, 2.3f), target(0, 0.8f, 2.25f);
    V3 fwd = norm(target - eye);
    M4 proj = M4::perspective(42, (float)SCREEN_W / SCREEN_H, 0.3f, 200.0f), view = M4::lookDir(V3(0, 0, 0), fwd, Z);
    gfx::setProjection(proj); gfx::setView(view); clip::setMatrices(proj, view);
    gfx::setFog(false, 0, 1, 0); gfx::setDepthTest(true); gfx::setDepthWrite(true); gfx::setBlend(false);
    M4 rel = M4::translate(V3(0, 0, 0) - eye);
    auto box = [&](V3 lo, V3 hi, u32 col) {
        M4 front = rel * M4::basis(V3(1, 0, 0), Z, V3(0, -1, 0), V3(0, lo.y, 0));
        R.quad3D(front, lo.x, lo.z, hi.x, hi.z, col);
        M4 top = rel * M4::basis(V3(1, 0, 0), V3(0, 1, 0), Z, V3(0, 0, hi.z));
        R.quad3D(top, lo.x, lo.y, hi.x, hi.y, RGBA((col & 255) * 4 / 5 + 40, ((col >> 8) & 255) * 4 / 5 + 40, ((col >> 16) & 255) * 4 / 5 + 40));
        for (int sd = 0; sd < 2; sd++) {
            float x = sd ? hi.x : lo.x;
            M4 side = rel * M4::basis(V3(0, 1, 0), Z, V3(sd ? 1.0f : -1.0f, 0, 0), V3(x, 0, 0));
            R.quad3D(side, lo.y, lo.z, hi.y, hi.z, RGBA((col & 255) * 3 / 5, ((col >> 8) & 255) * 3 / 5, ((col >> 16) & 255) * 3 / 5));
        }
    };
    // pit straight, the podium balcony and its backdrop
    R.quad3D(rel * M4::basis(V3(1, 0, 0), V3(0, 1, 0), Z, V3(0, 0, 0)), -60, -40, 60, 2, RGBA(70, 72, 78));
    R.quad3D(rel * M4::basis(V3(1, 0, 0), V3(0, 1, 0), Z, V3(0, 0, 0.004f)), -60, -6.2f, 60, -6.0f, RGBA(230, 230, 230));
    box(V3(-6.5f, 0.0f, 0), V3(6.5f, 2.4f, 0.4f), RGBA(40, 46, 60));
    R.quad3D(rel * M4::basis(V3(1, 0, 0), Z, V3(0, -1, 0), V3(0, 1.6f, 0)), -6.5f, 0.4f, 6.5f, 6.0f, RGBA(18, 30, 70));
    R.quad3D(rel * M4::basis(V3(1, 0, 0), Z, V3(0, -1, 0), V3(0, 1.59f, 0)), -6.5f, 4.9f, 6.5f, 5.05f, RGBA(255, 150, 40));
    char b[96];
    snprintf(b, sizeof b, "%s  -  %s", TRACKS[race.set.track].name, ERA_SHORT[CARS[race.set.car].era]); (void)pc;
    R.text3D(rel * M4::basis(V3(1, 0, 0), Z, V3(0, -1, 0), V3(0, 1.58f, 5.45f)), b, 0.42f, RGBA(255, 255, 255), true);
    R.text3D(rel * M4::basis(V3(1, 0, 0), Z, V3(0, -1, 0), V3(0, 1.58f, 4.45f)), "FORMULA LEGENDS", 0.24f, RGBA(150, 170, 210), true);
    static const float SX[3] = {0, -2.9f, 2.9f}, SH[3] = {1.45f, 1.0f, 0.75f};
    static const u32 SC[3] = {RGBA(210, 170, 50), RGBA(180, 185, 195), RGBA(170, 110, 60)};
    M4 pv = proj * view;
    for (int k = 0; k < 3 && k < (int)cls.size(); k++) {
        box(V3(SX[k] - 1.35f, 0.1f, 0.4f), V3(SX[k] + 1.35f, 1.5f, 0.4f + SH[k]), SC[k]);
        char n[12]; snprintf(n, sizeof n, "%d", k + 1);
        M4 face = rel * M4::basis(V3(1, 0, 0), Z, V3(0, -1, 0), V3(0, 0.09f, 0));
        R.text3D(face * M4::translate(V3(SX[k], 0.4f + SH[k] * 0.62f, 0)), n, 0.42f, RGBA(20, 20, 20), true);
        snprintf(b, sizeof b, "#%d %s", cls[k]->number, cls[k]->def->team);
        R.text3D(face * M4::translate(V3(SX[k], 0.4f + SH[k] * 0.22f, 0)), b, 0.13f, RGBA(20, 20, 20), true);
        // Formula 1: the driver on his step, in his team's suit and helmet. Trophies wait on the step beside them, then
        // go up over their heads as the celebration starts (with the March of the Toreadors playing).
        float topZ = 0.4f + SH[k];
        bool party = t > 2.5f;
        u32 tm, ts; R.teamColours(cls[k]->def, tm, ts);
        int people = 1;
        for (int d = 0; d < people; d++) {
            float x = SX[k];
            float hop = (party && t < 9 && !R.poseCount[Renderer::ANIM_CELEBRATE]) ? fmaxf(0, sinf(t * 7 + d * 2 + k)) * 0.12f : 0;
            V3 at(x, 0.8f, topZ + hop);
            M4 place = M4::translate(at - eye) * M4::rotZ(-1.5708f + (d ? 0.25f : 0));
            bool rep = d == 1;          // the team's representative: celebrates a beat later, more calmly
            bool cheer = party && (!rep || t > 3.4f);
            R.drawPerson(place, cheer ? Renderer::ANIM_CELEBRATE : Renderer::ANIM_IDLE, t * (rep ? 0.7f : 1.0f) + k * 0.21f, true, tm, ts);
            // the trophy: on its stand, then held up (riding the jump of the celebration)
            float tw = (k == 0 && !rep) ? 0.11f : 0.08f, th = (k == 0 && !rep) ? 0.34f : 0.26f;
            V3 cup;
            if (!cheer) cup = V3(x + 0.42f, 0.45f, topZ);                  // on the step, beside him
            else {
                float ph = fmodf(t * (rep ? 0.7f : 1.0f) * 5.0f + k * 1.05f, 6.0f);   // (the celebration's poses: 5 a second, there and back)
                float up = ph < 3 ? ph / 3 : (6 - ph) / 3;
                cup = V3(x, 0.78f, topZ + 1.95f + up * 0.32f);
            }
            M4 cm = M4::translate(cup - eye);
            u32 gold = rep ? RGBA(200, 205, 215) : RGBA(235, 190, 60);
            R.box3D(cm, V3(-tw * 0.7f, -tw * 0.7f, 0), V3(tw * 0.7f, tw * 0.7f, 0.05f), RGBA(40, 40, 44));      // base
            R.box3D(cm, V3(-0.025f, -0.025f, 0.05f), V3(0.025f, 0.025f, th * 0.45f), gold);                  // stem
            R.box3D(cm, V3(-tw, -tw, th * 0.45f), V3(tw, tw, th), gold);                                     // cup
        }
        float sx, sy;
        if (project(pv, V3(SX[k], 0.8f, topZ + 1.75f), sx, sy)) { podiumScr[k][0] = sx; podiumScr[k][1] = sy; }
    }
    // the three cars parked on the pit straight below
    float tanV = tanf(42 * 3.14159f / 360.0f), tanH = tanV * SCREEN_W / SCREEN_H;
    R.camRight = norm(cross(fwd, Z)); R.camUp = cross(R.camRight, fwd);
    static const float CX[3] = {0, -6.2f, 6.2f};
    for (int k = 0; k < 3 && k < (int)cls.size(); k++) {
        float a = 3.14159f + (k == 1 ? -0.55f : (k == 2 ? 0.55f : 0));
        Car tmp = *cls[k];
        tmp.fwd = V3(sinf(a), -cosf(a), 0); tmp.left = V3(cosf(a), sinf(a), 0); tmp.up = Z; tmp.pos = V3(CX[k], k ? -3.4f : -3.0f, -0.25f);
        tmp.noseOff = tmp.wingOff = false;
        R.drawCarAt(tmp, eye, fwd, tanH, tanV);
    }
    // champagne and confetti
    static gfx::Sprite2D sp[800];
    int n = 0;
    for (auto& bt : bits) if (n < 800) sp[n++] = gfx::Sprite2D{bt.x, bt.y, bt.kind ? 3.0f : 2.0f, bt.kind ? 4.0f : 2.0f, 0, 0, 0, 0, bt.col};
    gfx::setDepthTest(false); gfx::setBlend(true);
    gfx::bindTexture(nullptr);
    gfx::drawSprites2D(sp, n);
    if (!cls.empty()) {     // the winner and his team
        char d0[24];
        Race::driverName(*cls[0], 0, d0, sizeof d0);
        snprintf(b, sizeof b, "Winner: %s  -  %s", d0, cls[0]->def->team);
        float w = R.font.width(b, 0.75f) + 16;
        gfx::drawFill2D(SCREEN_W / 2 - w / 2, SCREEN_H - 62, w, 17, RGBA(0, 0, 0, 150));
        R.font.drawCentre(SCREEN_W / 2, SCREEN_H - 61, b, WHITE, 0.75f);
    }
    int mine = (int)(std::find(cls.begin(), cls.end(), &race.player()) - cls.begin()) + 1;
    snprintf(b, sizeof b, "You finished P%d", mine);
    gfx::drawFill2D(SCREEN_W / 2 - 100, SCREEN_H - 40, 200, 18, RGBA(0, 0, 0, 150));
    R.font.drawCentre(SCREEN_W / 2, SCREEN_H - 39, b, GOLD, 0.85f);
    R.font.drawCentre(SCREEN_W / 2, SCREEN_H - 16, "X  continue", DIM, 0.8f);
    finishFrame();
}

void Game::drawFps() {
    char b[48]; snprintf(b, sizeof b, "%d fps  %.1f MB", (int)(fps + 0.5f), sys::memUsedKB() / 1024.0f);
    gfx::setDepthTest(false); gfx::setBlend(true);
    gfx::drawFill2D(SCREEN_W / 2 - 46, 0, 92, 13, RGBA(0, 0, 0, 140));
    R.font.drawCentre(SCREEN_W / 2, 0, b, RGBA(120, 255, 140), 0.7f);
    gfx::setBlend(false); gfx::setDepthTest(true);
}

// the settings menus over a paused race
void Game::drawOptionsFlat() {
    gfx::setDepthTest(false); gfx::setBlend(true); gfx::setFog(false, 0, 1, 0);
    gfx::drawFill2D(0, 0, SCREEN_W, SCREEN_H, RGBA(6, 9, 16, 215));
    Panel P; P.R = &R; P.flat = true;
    if (screen == SCR_OPTIONS) screenOptions(P); else if (screen == SCR_REALISM) screenRealism(P); else screenControls(P);
    gfx::setBlend(false); gfx::setDepthTest(true);
}

// ------------------------------------------------------------------ what each screen shows
void Game::screenTitle(Panel& P) {
    P.fill(0, 0, P.vw, 22, RGBA(0, 0, 0, 150));
    P.fill(0, 22, P.vw, 2, ACC);
    char b[48]; snprintf(b, sizeof b, "MAIN MENU  %s", ERA_SHORT[era]);
    P.text(8, 3, b, WHITE, 0.9f);
    const char* items[TITLE_N] = {"QUICK RACE", "CHAMPIONSHIP", "CAREER", "LOAD RACE", "SETTINGS", "CREDITS", "TIME TRIAL",
                                  "TROPHIES", "SHOWROOM", "HEADQUARTERS"};
    for (int i = 0; i < TITLE_N; i++) {
        int id = TITLE_ORDER[i];
        float y = 26 + i * 12.6f;
        if (id == sel) P.fill(8, y - 1.5f, P.vw - 16, 12.6f, RGBA(255, 150, 40, 230));
        P.textC(P.vw / 2, y, items[id], id == sel ? INK : WHITE, 0.74f);
    }
    P.textC(P.vw / 2, 158, "O  headquarters (change era)", DIM, 0.58f);
}

void Game::screenTrack(Panel& P) {
    char b[96];
    header(P, "SELECT CIRCUIT");
    // era tabs
    for (int e = 0; e < ERA_COUNT; e++) {
        float x = 8 + e * 50;
        P.fill(x, 26, 47, 14, e == era ? ACC : RGBA(0, 0, 0, 140));
        P.textC(x + 23.5f, 27, ERA_SHORT[e], e == era ? INK : DIM, 0.66f);
    }
    for (int i = 0; i < ERA_ROUNDS; i++) {
        int ti = eraTrack(era, i);
        float y = 46 + i * 19.5f;
        if (i == sel) P.fill(8, y - 1, 196, 18.5f, RGBA(255, 150, 40, 230));
        snprintf(b, sizeof b, "%d  %s", i + 1, TRACKS[ti].name);
        P.text(14, y, b, i == sel ? INK : WHITE, 0.74f);
    }
    int ti = eraTrack(era, sel);
    if (previewIdx != ti) { preview.load(&TRACKS[ti], false); previewIdx = ti; }
    P.fill(214, 42, 258, 206, RGBA(0, 0, 0, 120));
    if (preview.n) {
        P.map(preview, 226, 48, 130, WHITE);
        P.text(366, 52, "Lap", DIM, 0.75f);
        snprintf(b, sizeof b, "%.3f km", preview.length / 1000.0f); P.text(366, 66, b, WHITE, 0.9f);
        P.text(366, 90, "Height range", DIM, 0.75f);
        snprintf(b, sizeof b, "%.0f m", preview.elevMax - preview.elevMin); P.text(366, 104, b, WHITE, 0.9f);
        P.text(366, 128, "Grand Prix", DIM, 0.75f);
        snprintf(b, sizeof b, "%d laps", TRACKS[ti].gpLaps); P.text(366, 142, b, WHITE, 0.9f);
        if (TRACKS[ti].street || TRACKS[ti].floodlit) P.text(366, 160, TRACKS[ti].floodlit ? (TRACKS[ti].street ? "Street, at night" : "Night race") : "Street circuit", ACC, 0.7f);
    } else P.text(240, 120, "circuit data missing", ACC, 0.9f);
    snprintf(b, sizeof b, "%s  -  %s", TRACKS[ti].location, ERA_NAME[era]);
    P.text(224, 190, b, WHITE, 0.72f);
    P.text(224, 208, "Real layout and heights, today's track", DIM, 0.66f);
    P.text(224, 222, "Outline: bacinger/f1-circuits  Heights: EU-DEM/SRTM", DIM, 0.58f);
    P.textC(240, 254, "L/R  era     X  choose     O  back", DIM, 0.8f);
}

void Game::screenCar(Panel& P, Panel& I) {
    char b[96];
    P.fill(0, 0, P.vw, 22, RGBA(0, 0, 0, 150));
    P.fill(0, 22, P.vw, 2, ACC);
    int e = CARS[sel % CAR_PICK].era;
    snprintf(b, sizeof b, "%s  %s", champMode ? "SEASON CAR" : "SELECT TEAM", ERA_SHORT[e]);
    P.text(8, 3, b, WHITE, 0.9f);
    for (int k = 0; k < TEAMS_PER_ERA; k++) {
        int i = eraFirstCar(e) + k;
        float y = 30 + k * 20.0f;
        if (i == sel) P.fill(4, y - 1, P.vw - 8, 19, RGBA(255, 150, 40, 230));
        P.fill(8, y + 4, 9, 9, CARS[i].colour | 0xFF000000u);
        snprintf(b, sizeof b, "%s", CARS[i].team);
        P.text(22, y, b, i == sel ? INK : WHITE, 0.72f);
    }
    P.textC(P.vw / 2, 249, champMode ? "L/R era  X choose  O back" : "X choose   O back", DIM, 0.62f);
    const CarDef& d = CARS[sel % CAR_PICK];
    snprintf(b, sizeof b, "%s %s  (%s)", d.make, d.model, ERA_SHORT[d.era]);
    I.text(8, 3, b, WHITE, 0.9f);
    snprintf(b, sizeof b, "%s  #%d & #%d", d.team, d.numbers[0], d.numbers[1]);
    I.text(8, 21, b, DIM, 0.76f);
    snprintf(b, sizeof b, "%s   %.0f kW   %.0f kg", d.engine, d.powerKW + Race::phys(d.era).ersKW, d.massKg);
    I.text(8, 38, b, DIM, 0.76f);
}


void Game::screenChamp(Panel& P) {
    header(P, "CHAMPIONSHIP");
    char b[96];
    P.fill(8, 40, 200, 208, RGBA(0, 0, 0, 120));
    for (int i = 0; i < CHAMP_ROUND_COUNT; i++) {
        const ChampRound& r = CHAMP_ROUNDS[i];
        u32 col = i < champ.round ? DIM : (i == champ.round ? GOLD : WHITE);
        snprintf(b, sizeof b, "%d  %s", i + 1, TRACKS[champ.trackIndex(i)].name); (void)r;
        P.text(14, 44 + i * 14, b, col, 0.64f);
    }
    const char* items[3] = {champ.over() ? "SEASON OVER - new season" : "START NEXT ROUND", "New season", "Back"};
    for (int i = 0; i < 3; i++) {
        float y = 186 + i * 19;
        if (i == sel) P.fill(12, y - 2, 192, 17, RGBA(255, 150, 40, 230));
        P.text(18, y, items[i], i == sel ? INK : (i == 0 ? GOLD : WHITE), 0.8f);
    }
    CarClass pc = CARS[champ.base.car].cls;
    int idx[48]; int n = champ.standings(pc, idx, 48);
    P.fill(214, 40, 258, 208, RGBA(0, 0, 0, 120));
    snprintf(b, sizeof b, "%s STANDINGS  (after %d of %d)", ERA_SHORT[champ.era()], champ.round, CHAMP_ROUND_COUNT);
    P.text(220, 44, b, ACC, 0.7f);
    int me = -1;
    for (int k = 0; k < n; k++) { const ChampEntry& e = champ.e[idx[k]]; if (e.number == CARS[champ.base.car].numbers[0]) me = k; }
    int first = me > 9 ? me - 9 : 0;
    for (int k = 0; k < 10 && first + k < n; k++) {
        const ChampEntry& e = champ.e[idx[first + k]];
        snprintf(b, sizeof b, "%2d  #%-3d %-9s %5.1f  W%d", first + k + 1, e.number, CARS[e.car].make, e.pts, e.wins);
        P.text(222, 62 + k * 17, b, first + k == me ? GOLD : WHITE, 0.72f);
        if (champ.lastPts[idx[first + k]] > 0) { snprintf(b, sizeof b, "+%d", champ.lastPts[idx[first + k]]); P.textR(468, 62 + k * 17, b, RGBA(120, 230, 120), 0.66f); }
    }
    if (!n) { snprintf(b, sizeof b, "No points yet - the season opens at %s", TRACKS[champ.trackIndex(0)].name); P.text(222, 70, b, DIM, 0.64f); }
    if (!champ.over()) {
        char a[64], b2[64];
        clockTip(a, b2, sizeof a, champ.roundSettings());
        P.text(220, 226, a, RGBA(140, 200, 255), 0.6f);
        P.text(220, 237, b2, RGBA(140, 200, 255), 0.6f);
    }
    fmtLength(b, champ.base);
    char b2[160]; snprintf(b2, sizeof b2, "%s %s   races %s (Sablons twice as long)", CARS[champ.base.car].make, CARS[champ.base.car].model, b);
    P.textC(240, 255, b2, DIM, 0.66f);
}

void Game::screenLoad(Panel& P) {
    header(P, "LOAD RACE");
    P.fill(30, 50, 420, 180, RGBA(0, 0, 0, 120));
    for (int i = 0; i < 4; i++) {
        float y = 62 + i * 40;
        if (i == sel) P.fill(36, y - 4, 408, 34, RGBA(255, 150, 40, 230));
        u32 col = i == sel ? INK : WHITE;
        if (i == 3) { P.text(46, y + 4, "Back", col, 0.9f); continue; }
        char p[32], b[96]; snprintf(p, sizeof p, "save%d.sav", i + 1);
        RaceSettings s; int ses; float rt; int lap;
        if (Race::peek(p, s, ses, rt, lap)) {
            snprintf(b, sizeof b, "Slot %d:  %s", i + 1, TRACKS[s.track].name);
            P.text(46, y, b, col, 0.85f);
            int sec = (int)rt;
            snprintf(b, sizeof b, "%s %s   lap %d   %d:%02d into the race", CARS[s.car].make, CARS[s.car].model, lap + 1, sec / 3600, (sec / 60) % 60);
            P.text(46, y + 15, b, i == sel ? RGBA(40, 40, 40) : DIM, 0.7f);
        } else {
            snprintf(b, sizeof b, "Slot %d:  empty", i + 1);
            P.text(46, y + 4, b, col, 0.85f);
        }
    }
    if (noteT > 0) P.textC(240, 234, note, GOLD, 0.8f);
    P.textC(240, 255, "Races are saved at a pit stop (team screen)", DIM, 0.75f);
}

void Game::screenHelp(Panel& P) {
    // credits (how to play is in the README): they roll up the panel and start again
    header(P, "CREDITS");
    struct Line { const char* role; const char* who; };
    static const Line L[] = {
        {"Producer / CEO / Engineer", "Erin"},
        {"Code", "Erin"},
        {"Studio", "CODE 4 Studios"},
        {"Engine", "Endurance Motor Racing 3D"},
        {"", ""},
        {"Intro music", "Take On The World - All Good Folks (Uppbeat)"},
        {"1980 era music", "Originals, Epidemic Sound, Uppbeat"},
        {"2016 era music", "Uppbeat, Epidemic Sound"},
        {"2025 era music", "Uppbeat (uppbeat.io)"},
        {"", ""},
        {"Music from Epidemic Sound", "(epidemicsound.com)"},
        {"Epidemic Sound", "Ruzer - Fever Drone"},
        {"Epidemic Sound", "Ruzer - Summit"},
        {"Epidemic Sound", "Ruzer - Turn Down"},
        {"Epidemic Sound", "Ruzer - 1988"},
        {"Epidemic Sound", "Ben Elson - 2AM"},
        {"Epidemic Sound", "Lupus Nocte - Inferadise"},
        {"Epidemic Sound", "Velveteen - Do You Wanna Follow"},
        {"", ""},
        {"Team radio voice", "ElevenLabs"},
        {"Cars, wheels, cockpits", "Modelled in Blender"},
        {"Pit crew, safety cars, props", "Modelled in Blender"},
        {"Circuit outlines", "bacinger/f1-circuits (MIT)"},
        {"Circuit heights", "EU-DEM 25 m, SRTM 30 m"},
        {"", ""},
        {"Thank you for playing", "Formula Legends"},
    };
    const float top = 44, bottom = 238, row = 16.2f;
    P.fill(30, 42, 420, 200, RGBA(0, 0, 0, 120));
    int n = sizeof(L) / sizeof(L[0]);
    float total = n * row + (bottom - top);               // all the lines, then a screen's gap before they come round again
    float roll = fmodf(t * 16.0f, total);
    for (int i = 0; i < n; i++) {
        float y = bottom - roll + i * row;
        if (y < top - row) y += total;                    // (the start of the next pass)
        if (y < top || y > bottom - 10 || !L[i].who[0]) continue;
        // fade in at the bottom, out at the top
        float edge = fminf(y - top, bottom - 10 - y);
        int a = (int)(255 * fminf(1.0f, edge / 18.0f));
        bool head = i < 2 || !strcmp(L[i].role, "Music from Epidemic Sound") || !strcmp(L[i].role, "Thank you for playing");
        u32 rc = head ? ACC : DIM, wc = head ? GOLD : WHITE;
        rc = (rc & 0x00FFFFFFu) | ((u32)a << 24); wc = (wc & 0x00FFFFFFu) | ((u32)a << 24);
        P.textR(214, y, L[i].role, rc, 0.74f);
        P.text(226, y, L[i].who, wc, head ? 0.86f : 0.76f);
    }
    P.textC(240, 255, "X or O  back", DIM, 0.75f);
}

void Game::screenTrophies(Panel& P) {
    char b[48]; snprintf(b, sizeof b, "TROPHIES  %d / %d", trophies.count(), (int)TR_COUNT);
    header(P, b);
    P.fill(14, 40, 452, 208, RGBA(0, 0, 0, 120));
    static const u32 TC[4] = {RGBA(205, 127, 50), RGBA(200, 205, 215), RGBA(250, 205, 60), RGBA(150, 210, 255)};
    for (int i = 0; i < TR_COUNT; i++) {
        int col = i / 8, row = i % 8;
        float x = 20 + col * 226, y = 45 + row * 25.5f;
        bool got = trophies.has(i);
        const TrophyDef& t = TROPHIES[i];
        P.fill(x, y + 2, 14, 14, got ? TC[t.tier] : RGBA(60, 60, 66));
        if (got) P.fill(x + 4, y + 6, 6, 6, RGBA(10, 12, 18));
        P.text(x + 20, y, t.name, got ? WHITE : DIM, 0.72f);
        P.text(x + 20, y + 11, t.how, got ? GOLD : RGBA(120, 125, 135), 0.54f);
    }
    P.textC(240, 255, "X or O  back", DIM, 0.75f);
}

void Game::screenOptions(Panel& P) {
    header(P, "SETTINGS");
    static const char* PRESET[4] = {"Quality", "Balanced", "Performance", "Custom"};
    static const char* DISTN[3] = {"Far", "Medium", "Near"};
    char v[20][56];
    auto vol = [](char* o, const char* name, int x) {
        char bar[12]; for (int i = 0; i < 10; i++) bar[i] = i < x ? '|' : '.'; bar[10] = 0;
        snprintf(o, 56, "%-9s < %s >  %d", name, bar, x);
    };
    vol(v[0], "Master", cfg.master); vol(v[1], "Music", cfg.music); vol(v[2], "Engines", cfg.engine); vol(v[3], "Effects", cfg.effects); vol(v[4], "Radio", cfg.radio);
    snprintf(v[5], 56, "Optimisation:  < %s >", PRESET[cfg.preset]);
    snprintf(v[6], 56, "VSync:  < %s >", cfg.vsync ? "On" : "Off");
    snprintf(v[7], 56, "Frame rate:  < %s >", cfg.fps ? "30" : "60");
    snprintf(v[8], 56, "Draw distance:  < %s >", DISTN[cfg.distance]);
    snprintf(v[9], 56, "Scenery:  < %s >", cfg.scenery ? "Full" : "Reduced");
    snprintf(v[10], 56, "Smoke, spray, rain:  < %s >", cfg.effects3d ? "Full" : "Reduced");
    snprintf(v[11], 56, "Show frame rate:  < %s >", cfg.showFps ? "On" : "Off");
    snprintf(v[12], 56, "Auto quality:  < %s >", cfg.autoQuality ? (autoLevel ? "On (lowered)" : "On") : "Off");
    snprintf(v[13], 56, "Attract screen after:  < %d s >", cfg.attract);
    snprintf(v[14], 56, "Pit stop camera:  < %s >", cfg.pitCam ? "Your camera" : "Cinematic");
    snprintf(v[15], 56, "Controls...");   // (not a row now: a category)
    snprintf(v[16], 56, "Back");
    snprintf(v[17], 56, "Steering wheel:  < %s >", cfg.hideWheel ? "Hidden" : "Shown");
    snprintf(v[18], 56, "HUD in cockpit:  < %s >", !cfg.hideWheel ? "Wheel shown" : (cfg.cockpitHud ? "On" : "Off"));
    snprintf(v[19], 56, "Rear-view mirror:  < %s >", cfg.mirror ? "On" : "Off");
    if (optCat < 0) {
        // the categories
        static const char* SUB[OPT_CATS] = {"Master, music, engines, effects and radio volume",
            "Optimisation, frame rate, draw distance, scenery, effects", "Menus, pit stop camera, steering wheel, HUD, mirror",
            "Buttons, steering help, tilt steering", "Temperatures, slipstream, damage, mechanical problems", ""};
        for (int i = 0; i < OPT_CATS; i++) {
            float y = 46 + i * 33;
            bool on = i == sel;
            P.fill(40, y - 3, 400, 29, on ? RGBA(255, 150, 40, 230) : RGBA(0, 0, 0, 120));
            P.text(52, y, OPT_CAT_NAME[i], on ? INK : WHITE, 0.9f);
            if (SUB[i][0]) P.text(52, y + 14, SUB[i], on ? INK : DIM, 0.6f);
        }
        P.textC(240, 255, "X open     O back     Rear touch pad: next / previous song", DIM, 0.7f);
        return;
    }
    int ids[12]; int n = optRows(ids);
    P.text(20, 40, OPT_CAT_NAME[optCat], ACC, 0.7f);
    for (int k = 0; k < n; k++) {
        int i = ids[k];
        float y = 56 + k * 19 + (i == 16 ? 6 : 0);
        if (k == sel) P.fill(40, y - 2, 400, 17, RGBA(255, 150, 40, 230));
        P.text(52, y, v[i], k == sel ? INK : ((i == 18 && !cfg.hideWheel) ? DIM : WHITE), 0.82f);
    }
    P.textC(240, 255, "LEFT/RIGHT change     X select     O back", DIM, 0.75f);
}

void Game::screenControls(Panel& P) {
    header(P, "CONTROLS");
    int ids[24]; int n = controlRows(ids);
    static const char* TILT[4] = {"Off", "Gentle", "Normal", "Strong"};
    static const char* HELP[3] = {"Off", "Light", "Strong"};
    // two columns: the buttons on the left, the driving options (then reset / back) on the right
    P.fill(14, 40, 222, 178, RGBA(0, 0, 0, 120));
    P.fill(244, 40, 222, 178, RGBA(0, 0, 0, 120));
    P.text(22, 44, "BUTTONS", ACC, 0.66f);
    P.text(252, 44, "DRIVING", ACC, 0.66f);
    int right = 0;
    for (int k = 0; k < n; k++) {
        int id = ids[k];
        bool left = id < ACT_COUNT;
        float x = left ? 18 : 248, y = left ? 58 + id * 17.5f : 58 + right++ * 19.5f;
        if (id == CR_RESET) y += 8;                       // a little gap before reset / back
        if (id == CR_BACK) y += 8;
        bool on = k == sel;
        if (on) P.fill(x, y - 2, 214, 17, RGBA(255, 150, 40, 230));
        u32 col = on ? INK : WHITE, val = on ? INK : GOLD;
        char b[32];
        if (left) {
            P.text(x + 6, y, ACTION_NAME[id], col, 0.78f);
            if (rebinding == id) P.textR(x + 208, y, ((int)(t * 3) & 1) ? "press a button" : "", col, 0.78f);
            else P.textR(x + 208, y, buttonName(cfg.bind[id]), on ? INK : (cfg.bind[id] < 0 ? DIM : GOLD), 0.78f);
        } else if (id == CR_HELP) {
            snprintf(b, sizeof b, "< %s >", HELP[cfg.help()]);
            P.text(x + 6, y, "Steering help", col, 0.78f); P.textR(x + 208, y, b, val, 0.78f);
        } else if (id == CR_TILT) {
            snprintf(b, sizeof b, "< %s >", TILT[cfg.tilt]);
            P.text(x + 6, y, "Tilt steering", col, 0.78f); P.textR(x + 208, y, b, val, 0.78f);
        } else if (id == CR_TILTDIR) {
            P.text(x + 6, y, "Tilt direction", col, 0.78f); P.textR(x + 208, y, cfg.tiltInvert ? "< Reversed >" : "< Normal >", val, 0.78f);
        } else P.text(x + 6, y, id == CR_RESET ? "Reset buttons" : "Back", col, 0.78f);
    }
    // one line about what the selected option does
    int id = ids[sel];
    const char* tip = id < ACT_COUNT ? "X: press a new button     SQUARE: no button" :
        id == CR_HELP ? "Light: no spins from steering. Strong: no slides at all" :
        id == CR_TILT ? "Turn the Vita like a wheel (the stick still works)" :
        id == CR_TILTDIR ? "Reversed if turning right steers left" :
        "";
    P.textC(240, 224, tip, DIM, 0.66f);
    P.textC(240, 238, input::hasTouch() ? "Always: left stick steers, right stick gas / brake, R gas, L brake" : "Always: stick / D-pad steer, R gas, L brake, START pause", DIM, 0.6f);
    P.textC(240, 255, rebinding >= 0 ? "Press the new button for this action" : "X  change     O  back", DIM, 0.72f);
}

// (the intro is in intro.cpp)

// ------------------------------------------------------------------ the pit wall (managing the race)
// A stand on the pit wall next to our box, looking up the track at the cars coming towards it, with two
// screens on the desk: live timing for the whole field, and our car's numbers.
Camera Game::pitWallCamera() const {
    const Car& me = race.player();
    float sb = race.boxS(me);
    float hw = race.track.pts[race.track.index(sb)].hw;
    Camera c;
    c.eye = race.track.world(sb, hw + 2.9f, 3.4f);
    V3 tgt = race.track.world(sb - 230, -3.0f, -9.0f);      // up the track, so it shows above the screens
    c.fwd = norm(tgt - c.eye); c.up = V3(0, 0, 1); c.fov = 60;
    return c;
}

static void gapText(char* b, int n, const Race& r, const Car& c) {
    int down = r.lapsDown(c);
    float g = r.gap(c);
    if (c.position == 1) snprintf(b, n, "LEADER");
    else if (down >= 1) snprintf(b, n, "+%d LAP%s", down, down > 1 ? "S" : "");
    else if (g < 0) snprintf(b, n, "-");
    else if (g >= 60) snprintf(b, n, "+%d:%04.1f", (int)g / 60, fmodf(g, 60));
    else snprintf(b, n, "+%.1f", g);
}

void Game::drawPitWall(const Camera& cam) {
    const Race& r = race;
    const Car& me = r.player();
    gfx::setFog(false, 0, 1, 0);
    gfx::setProjection(M4::perspective(cam.fov, (float)SCREEN_W / SCREEN_H, 0.05f, 50.0f));
    gfx::setView(M4::lookDir(V3(0, 0, 0), cam.fwd, cam.up));
    gfx::setDepthTest(false);
    V3 f = cam.fwd, rt = norm(cross(f, V3(0, 0, 1))), up = cross(rt, f);
    M4 rel = M4::translate(V3(0, 0, 0) - cam.eye);
    auto facing = [&](float x, float y, float dist, float toe) {
        V3 at = cam.eye + f * dist + rt * x + up * y;
        V3 z = norm(cam.eye - at + rt * toe);
        V3 xx = norm(cross(V3(0, 0, 1), z)), yy = cross(z, xx);
        return M4::basis(xx, yy, z, at);
    };
    // the desk front, under the screens
    R.quad3D(rel * facing(0, -0.62f, 0.9f, 0), -1.2f, -0.25f, 1.2f, 0.12f, RGBA(28, 30, 36));
    R.quad3D(rel * facing(0, -0.505f, 0.89f, 0), -1.2f, -0.006f, 1.2f, 0.006f, RGBA(255, 150, 40));
    const u32 W = RGBA(235, 240, 250), D = RGBA(140, 150, 170), A = RGBA(255, 150, 40), G = RGBA(255, 215, 70);
    static const u32 TYC[TY_COUNT] = {RGBA(235, 40, 40), RGBA(250, 210, 30), RGBA(240, 240, 240), RGBA(40, 200, 60), RGBA(40, 120, 255)};
    char b[64];
    std::vector<const Car*> order(r.cars.size());
    for (auto& c : r.cars) order[c.position - 1] = &c;

    // ---- screen 1: live timing (the 8 cars around ours; leader's gap, interval, tyres, stops)
    {
        Panel P; P.R = &R; P.vw = 252; P.vh = 172; P.k = 0.0025f;
        P.m = facing(-0.318f, -0.16f, 0.86f, -0.03f);
        float w = P.vw * P.k * 0.5f, h = P.vh * P.k * 0.5f;
        R.quad3D(rel * P.m * M4::translate(V3(0, 0, -0.006f)), -w - 0.012f, -h - 0.012f, w + 0.012f, h + 0.012f, RGBA(20, 21, 24));
        R.quad3D(rel * P.m * M4::translate(V3(0, 0, -0.003f)), -w, -h, w, h, RGBA(6, 9, 16));
        P.fill(0, 0, P.vw, 15, RGBA(30, 34, 46));
        P.text(4, 1, "LIVE TIMING", A, 0.74f);
        float left = r.raceLeft();
        if (left >= 0) { int s = (int)(left * r.timeScale); snprintf(b, sizeof b, "%d:%02d:%02d left", s / 3600, (s / 60) % 60, s % 60); }
        else snprintf(b, sizeof b, "lap %d/%d", order[0]->lap + 1, r.set.laps);
        P.textR(P.vw - 4, 1, b, W, 0.72f);
        float y = 17;
        P.text(4, y, "P", D, 0.64f); P.text(30, y, "DRIVER", D, 0.64f);
        P.text(114, y, "GAP", D, 0.64f); P.text(176, y, "INT", D, 0.64f); P.text(206, y, "TYRE", D, 0.64f); P.text(240, y, "S", D, 0.64f);
        y += 12;
        int first = me.position - 4; if (first > (int)order.size() - 8) first = (int)order.size() - 8; if (first < 0) first = 0;
        for (int k = 0; k < 8 && first + k < (int)order.size(); k++) {
            const Car& c = *order[first + k];
            const Car* ahead = first + k > 0 ? order[first + k - 1] : nullptr;
            if (c.player) P.fill(1, y - 1, P.vw - 2, 15, RGBA(95, 64, 12));
            u32 col = c.player ? G : W;
            snprintf(b, sizeof b, "%d", c.position); P.text(4, y, b, col, 0.74f);
            P.fill(22, y + 1, 3, 11, c.def->cls == CLASS_HYPER ? RGBA(230, 40, 40) : RGBA(40, 160, 255));
            Race::driverName(c, c.driver, b, sizeof b); P.text(30, y, b, col, 0.74f);
            if (c.pit != PIT_NONE) P.text(114, y, c.pit == PIT_STOP ? "IN PIT" : "PIT", A, 0.74f);
            else { gapText(b, sizeof b, r, c); P.text(114, y, b, col, 0.74f); }
            if (ahead) {
                float g1 = r.gap(c), g0 = r.gap(*ahead);
                int dl = r.lapsDown(c) - r.lapsDown(*ahead);
                if (dl >= 1) snprintf(b, sizeof b, "+%dL", dl);
                else if (g1 >= 0 && g0 >= 0) snprintf(b, sizeof b, "%.1f", fmaxf(0, g1 - g0));
                else snprintf(b, sizeof b, "-");
                P.text(176, y, b, D, 0.72f);
            }
            P.fill(206, y + 2, 6, 10, TYC[c.tyre]);
            snprintf(b, sizeof b, "%d", c.tyreLaps); P.text(215, y, b, D, 0.72f);
            snprintf(b, sizeof b, "%d", c.stops); P.text(240, y, b, D, 0.72f);
            y += 15;
        }
        // race control: the latest message as a ticker
        P.fill(0, P.vh - 14, P.vw, 14, RGBA(30, 34, 46));
        if (!r.msgs.empty()) P.text(4, P.vh - 13, r.msgs[0].text, (r.msgs[0].color & 0xFFFFFFu) | 0xFF000000u, 0.6f);
        else P.text(4, P.vh - 13, "Race control", D, 0.6f);
        P.flush(cam.eye);
    }

    // ---- screen 2: our car
    {
        Panel P; P.R = &R; P.vw = 236; P.vh = 172; P.k = 0.0025f;
        P.m = facing(0.333f, -0.16f, 0.86f, 0.03f);
        float w = P.vw * P.k * 0.5f, h = P.vh * P.k * 0.5f;
        R.quad3D(rel * P.m * M4::translate(V3(0, 0, -0.006f)), -w - 0.012f, -h - 0.012f, w + 0.012f, h + 0.012f, RGBA(20, 21, 24));
        R.quad3D(rel * P.m * M4::translate(V3(0, 0, -0.003f)), -w, -h, w, h, RGBA(6, 9, 16));
        int clsPos = 1; for (auto& o : r.cars) if (o.def->cls == me.def->cls && o.position < me.position) clsPos++;
        P.fill(0, 0, P.vw, 15, RGBA(30, 34, 46));
        snprintf(b, sizeof b, "#%d %s", me.number, me.def->model); P.text(4, 1, b, G, 0.74f);
        snprintf(b, sizeof b, "P%d  class P%d", me.position, clsPos); P.textR(P.vw - 4, 1, b, W, 0.72f);
        float y = 18;
        auto bar = [&](float x, float yy, float ww, float v, u32 c) { P.fill(x, yy, ww, 7, RGBA(40, 44, 54)); P.fill(x, yy, ww * clampf(v, 0, 1), 7, c); };
        float lf = r.lapFuel(me);
        int lapsFuel = lf > 0 ? (int)(me.fuel / lf) : 0;
        P.text(4, y, "FUEL", D, 0.72f);
        bar(48, y + 3, 70, me.fuel, me.fuel > 0.15f ? RGBA(80, 180, 255) : RGBA(250, 60, 40));
        snprintf(b, sizeof b, "%d%%  %d laps", (int)(me.fuel * 100), lapsFuel); P.text(124, y, b, W, 0.72f);
        y += 15;
        float life = 1 - me.wear;
        P.text(4, y, "TYRES", D, 0.72f);
        bar(48, y + 3, 70, life, life > 0.4f ? RGBA(80, 220, 90) : (life > 0.15f ? RGBA(250, 200, 40) : RGBA(250, 60, 40)));
        P.fill(124, y + 2, 6, 10, TYC[me.tyre]);
        snprintf(b, sizeof b, "%s %d laps", TYRE_NAME[me.tyre], me.tyreLaps); P.text(134, y, b, W, 0.72f);
        y += 15;
        snprintf(b, sizeof b, "DAMAGE %d%%%s", (int)(me.damage() * 100), me.noseOff ? " nose" : (me.wingOff ? " wing" : ""));
        P.text(4, y, b, me.damage() > 0.3f ? RGBA(255, 120, 60) : D, 0.72f);
        static const char* MP[MAP_COUNT] = {"SC", "NORMAL", "ATTACK"};
        snprintf(b, sizeof b, "MAP %s", MP[me.su.map]); P.textR(P.vw - 4, y, b, D, 0.64f);
        y += 15;
        P.fill(3, y - 2, P.vw - 6, 1, RGBA(60, 66, 80));
        for (int k = 0; k < DRIVERS_PER_CAR; k++) {
            const Driver& d = me.drv[k];
            char nm[24]; Race::driverName(me, k, nm, sizeof nm);
            snprintf(b, sizeof b, "%s%s", k == me.driver ? ">" : " ", nm);
            P.text(4, y, b, k == me.driver ? W : D, 0.72f);
            bar(104, y + 3, 64, d.fatigue, d.fatigue < 0.45f ? RGBA(80, 220, 90) : (d.fatigue < 0.7f ? RGBA(250, 200, 40) : RGBA(250, 60, 40)));
            if (k == me.driver) snprintf(b, sizeof b, "%d:%02d", (int)me.stint / 3600, ((int)me.stint / 60) % 60);
            else snprintf(b, sizeof b, "%.1fh", d.hours);
            P.textR(P.vw - 4, y, b, k == me.driver && me.stint > (MAX_STINT_H - 0.5f) * 3600 ? RGBA(255, 120, 60) : D, 0.72f);
            y += 15;
        }
        P.fill(3, y - 1, P.vw - 6, 1, RGBA(60, 66, 80));
        y += 2;
        const char* wx = r.rain < 0.05f ? "Dry" : (r.rain < 0.5f ? "Light rain" : (r.storm ? "Storm" : "Heavy rain"));
        if (r.wet > 0.05f) snprintf(b, sizeof b, "%s, track %d%% wet", wx, (int)(r.wet * 100));
        else snprintf(b, sizeof b, "%s, rubber %d%%", wx, (int)(r.rubber * 100));
        P.text(4, y, b, RGBA(140, 190, 255), 0.72f);
        float h0 = r.clockHour(); snprintf(b, sizeof b, "%02d:%02d", (int)h0, (int)(h0 * 60) % 60); P.textR(P.vw - 4, y, b, D, 0.72f);
        y += 15;
        const char* call;
        if (me.pit != PIT_NONE) call = "In the pits";
        else if (me.wantPit) call = "BOX THIS LAP";
        else if (lapsFuel <= 2) call = "Box soon: fuel";
        else if (life < 0.3f) call = "Box soon: tyres";
        else if (me.drv[me.driver].fatigue > 0.7f) call = "Driver tired: change him";
        else call = nullptr;
        if (call) P.text(4, y, call, A, 0.74f);
        else { snprintf(b, sizeof b, "Next stop in ~%d laps", lapsFuel > 1 ? lapsFuel - 1 : 1); P.text(4, y, b, D, 0.72f); }
        P.fill(0, P.vh - 14, P.vw, 14, RGBA(30, 34, 46));
        snprintf(b, sizeof b, "x%d", (int)r.timeAccel); P.text(4, P.vh - 13, b, RGBA(120, 200, 255), 0.66f);
        if (me.lastLap > 0) { char t1[16], t2[16]; formatTime(t1, me.lastLap); formatTime(t2, me.bestLap); snprintf(b, sizeof b, "last %s  best %s", t1, t2); P.textR(P.vw - 4, P.vh - 13, b, W, 0.64f); }
        P.flush(cam.eye);
    }
    gfx::setDepthTest(true);
}

// ------------------------------------------------------------------ career mode screens
static void money(char* b, int n, int k) {        // $k -> "$1.52M" / "$520k"
    int a = k < 0 ? -k : k;
    if (a >= 1000) snprintf(b, n, "%s$%d.%02dM", k < 0 ? "-" : "", a / 1000, (a % 1000) / 10);
    else snprintf(b, n, "%s$%dk", k < 0 ? "-" : "", a);
}

void Game::screenCareerNew(Panel& P) {
    header(P, "NEW CAREER");
    char b[96];
    static const char* DIFFN[3] = {"Easy", "Normal", "Hard"};
    char v[6][64];
    snprintf(v[0], 64, "Team:  < %s >", TEAM_NAMES[newName]);
    snprintf(v[1], 64, "Era:  < %s >", ERA_NAME[newCls % ERA_COUNT]);
    snprintf(v[2], 64, "Opponents:  < %s >", DIFFN[newDiff]);
    snprintf(v[3], 64, "Race length:  < %d laps >", newMin);
    snprintf(v[4], 64, "START THE TEAM");
    snprintf(v[5], 64, "Back");
    P.fill(8, 42, 250, 206, RGBA(0, 0, 0, 120));
    for (int i = 0; i < 6; i++) {
        float y = 52 + i * 30;
        if (i == sel) P.fill(12, y - 3, 242, 21, RGBA(255, 150, 40, 230));
        P.text(20, y, v[i], i == sel ? INK : (i == 4 ? GOLD : WHITE), 0.78f);
    }
    P.fill(266, 42, 206, 206, RGBA(0, 0, 0, 120));
    float y = 50;
    P.text(274, y, "YOUR OWN TEAM", ACC, 0.8f); y += 20;
    const char* lines[] = {"Your two cars take the", "place of a team on the grid.", "", "Between races: sign", "sponsors, hire your team-", "mate and a reserve, buy", "upgrades, paint the livery.", ""};
    for (const char* l : lines) { P.text(274, y, l, WHITE, 0.66f); y += 14; }
    money(b, sizeof b, newCls >= ERA_HYBRID ? 4000 : 1500);
    char b2[128]; snprintf(b2, sizeof b2, "Budget to start: %s", b); P.text(274, y, b2, GOLD, 0.7f); y += 15;
    P.text(274, y, newCls >= ERA_HYBRID ? "Hybrid-era cars cost far" : "The older eras: the", DIM, 0.64f); y += 13;
    P.text(274, y, newCls >= ERA_HYBRID ? "more to run - and pay more." : "cheaper way in.", DIM, 0.64f); y += 15;
    {
        char one[24]; fmtSpan(one, sizeof one, 6 * 60.0f / newMin);
        snprintf(b2, sizeof b2, "TIP: a 6 h round in %d min:", newMin); P.text(274, y, b2, RGBA(140, 200, 255), 0.6f); y += 11;
        snprintf(b2, sizeof b2, "1 real min = %s", one); P.text(274, y, b2, RGBA(140, 200, 255), 0.6f);
    }
    P.textC(240, 254, "UP/DOWN choose   LEFT/RIGHT change   X select", DIM, 0.72f);
}

void Game::screenLivery(Panel& P, Panel& I) {
    char b[96];
    P.fill(0, 0, P.vw, 22, RGBA(0, 0, 0, 150));
    P.fill(0, 22, P.vw, 2, ACC);
    P.text(8, 3, "LIVERY", WHITE, 0.9f);
    const char* label[6] = {"Style", "Main colour", "Second colour", "Accent", "Race number", "DONE"};
    int col[3] = {career.colA, career.colB, career.colC};
    for (int i = 0; i < 6; i++) {
        float y = 34 + i * 36;
        if (i == sel) P.fill(4, y - 3, P.vw - 8, 33, RGBA(255, 150, 40, 230));
        u32 tc = i == sel ? INK : WHITE;
        P.text(12, y, label[i], i == 5 ? (i == sel ? INK : GOLD) : (i == sel ? INK : DIM), 0.72f);
        if (i == 0) P.text(24, y + 14, LIVERY_STYLE_NAMES[career.style], tc, 0.8f);
        if (i >= 1 && i <= 3) {
            P.fill(24, y + 15, 14, 12, LIVERY_COLOURS[col[i - 1]] | 0xFF000000u);
            P.text(44, y + 14, LIVERY_COLOUR_NAMES[col[i - 1]], tc, 0.8f);
        }
        if (i == 4) { snprintf(b, sizeof b, "#%d", career.number); P.text(24, y + 14, b, tc, 0.8f); }
        if (i < 5) { P.text(P.vw - 26, y + 12, "<", tc, 0.8f); P.text(P.vw - 14, y + 12, ">", tc, 0.8f); }
    }
    P.textC(P.vw / 2, 249, "LEFT/RIGHT change   X done", DIM, 0.62f);
    const CarDef& d = CARS[career.carIndex()];
    snprintf(b, sizeof b, "%s %s  #%d", d.make, d.model, career.number);
    I.text(8, 3, b, WHITE, 0.9f);
    I.text(8, 21, d.team, DIM, 0.76f);
    if (noteT > 0) I.text(8, 38, note, GOLD, 0.66f);
    else { snprintf(b, sizeof b, "%s   %.0f kW   %.0f kg", d.engine, d.powerKW, d.massKg); I.text(8, 38, b, DIM, 0.76f); }
}

void Game::screenCareer(Panel& P) {
    char b[128], m[32];
    const Career& c = career;
    snprintf(b, sizeof b, "%s", TEAM_NAMES[c.name % TEAM_NAME_COUNT]);
    header(P, b);
    snprintf(b, sizeof b, "SEASON %d", c.season); P.textR(470, 12, b, DIM, 0.75f);
    // left: the menu of this page
    P.fill(8, 42, 200, 206, RGBA(0, 0, 0, 120));
    auto row = [&](int i, const char* s, u32 col) {
        float y = 50 + i * 27;
        if (i == sel) P.fill(12, y - 3, 192, 21, RGBA(255, 150, 40, 230));
        P.text(20, y, s, i == sel ? INK : col, 0.74f);
    };
    // right: details
    P.fill(214, 42, 258, 206, RGBA(0, 0, 0, 120));
    float y = 48;
    auto line = [&](const char* s, u32 col, float sc = 0.66f) { P.text(222, y, s, col, sc); y += sc > 0.7f ? 17 : 14; };
    int rd = c.champ.round;
    if (careerPage == 0) {
        if (c.broke()) snprintf(b, sizeof b, "BANKRUPT - start again");
        else if (c.champ.over()) snprintf(b, sizeof b, "START SEASON %d", c.season + 1);
        else snprintf(b, sizeof b, "NEXT: R%d %s", rd + 1, TRACKS[c.champ.trackIndex(rd)].name);
        row(0, b, GOLD);
        const char* items[6] = {"Sponsors", "Drivers", "Upgrades", "Livery", "Standings", "Save and quit"};
        for (int i = 0; i < 6; i++) row(i + 1, items[i], WHITE);
        money(m, sizeof m, c.money);
        snprintf(b, sizeof b, "Budget %s", m); P.text(222, y, b, c.money < 0 ? RGBA(255, 90, 60) : GOLD, 1.0f); y += 22;
        int cp = c.classPos();
        ChampEntry* me = nullptr; for (int i = 0; i < c.champ.count; i++) if (c.champ.e[i].number == c.number) me = (ChampEntry*)&c.champ.e[i];
        snprintf(b, sizeof b, "%s  round %d/%d  %s P%d  %.0f pts", ERA_SHORT[c.cls % ERA_COUNT], rd < CHAMP_ROUND_COUNT ? rd + 1 : CHAMP_ROUND_COUNT, CHAMP_ROUND_COUNT,
                 "champ.", cp, me ? me->pts : 0.0f);
        if (!cp) snprintf(b, sizeof b, "%s  round %d/%d  no points yet", ERA_SHORT[c.cls % ERA_COUNT], rd + 1, CHAMP_ROUND_COUNT);
        line(b, WHITE);
        snprintf(b, sizeof b, "#%d  Eng %d  Aero %d  Wt %d  Pit %d", c.number, c.up[0], c.up[1], c.up[2], c.up[3]); line(b, DIM);
        char n1[24], n2[24];
        Car tmp; tmp.drv[1] = c.crew[0].d; tmp.drv[2] = c.crew[1].d;
        Race::driverName(tmp, 1, n1, sizeof n1); Race::driverName(tmp, 2, n2, sizeof n2);
        snprintf(b, sizeof b, "You + %s (%d), reserve %s (%d)", n1, driverRating(c.crew[0].d), n2, driverRating(c.crew[1].d)); line(b, DIM);
        snprintf(b, sizeof b, "Sponsors: %s%s%s", c.sponsor[0].name >= 0 ? SPONSOR_NAMES[c.sponsor[0].name] : "none",
                 c.sponsor[1].name >= 0 ? ", " : "", c.sponsor[1].name >= 0 ? SPONSOR_NAMES[c.sponsor[1].name] : "");
        line(b, DIM);
        if (!c.champ.over() && !c.broke()) {
            char a[64], b2[64];
            clockTip(a, b2, sizeof a, c.roundSettings());
            line(a, RGBA(140, 200, 255), 0.6f); line(b2, RGBA(140, 200, 255), 0.6f);
        }
        y += 4;
        if (c.lastValid) {
            snprintf(b, sizeof b, "LAST RACE: P%d in class", c.lastPos); line(b, ACC, 0.7f);
            int in = c.inPrize + c.inSponsor + c.inBonus, out = c.outRunning + c.outSalaries + c.outRepairs;
            snprintf(b, sizeof b, "Prize +%dk  sponsors +%dk  bonus +%dk", c.inPrize, c.inSponsor, c.inBonus); line(b, RGBA(120, 230, 120));
            snprintf(b, sizeof b, "Running -%dk  salaries -%dk  repairs -%dk", c.outRunning, c.outSalaries, c.outRepairs); line(b, RGBA(255, 140, 110));
            money(m, sizeof m, in - out); snprintf(b, sizeof b, "Net %s%s", in - out >= 0 ? "+" : "", m); line(b, in - out >= 0 ? RGBA(120, 230, 120) : RGBA(255, 140, 110));
            if (c.seasonEndBonus) { money(m, sizeof m, c.seasonEndBonus); snprintf(b, sizeof b, "Season prize money +%s", m); line(b, GOLD); }
        } else {
            line("Each race costs entry, crew,", DIM); line("tyres and fuel, and salaries.", DIM);
            line("Prize money, sponsors and", DIM); line("bonuses pay for it all.", DIM);
        }
        if (!c.champ.over() && rd < CHAMP_ROUND_COUNT) {
            money(m, sizeof m, c.runningCost(rd) + c.salaries());
            snprintf(b, sizeof b, "Next race costs about %s", m); P.text(222, 232, b, DIM, 0.62f);
        }
        if (c.broke()) P.text(222, 232, "The team owes over $1M: it folds.", RGBA(255, 90, 60), 0.62f);
    } else if (careerPage == 1) {
        P.text(20, 48, "OFFERS (to season end)", ACC, 0.62f);
        for (int i = 0; i < 3; i++) {
            const Sponsor& o = c.offers[i];
            if (o.name >= 0) snprintf(b, sizeof b, "%s%s", o.title ? "Title: " : "", SPONSOR_NAMES[o.name]);
            else snprintf(b, sizeof b, "(signed)");
            float yy = 66 + i * 40;
            if (i == sel) P.fill(12, yy - 3, 192, 36, RGBA(255, 150, 40, 230));
            P.text(18, yy, b, i == sel ? INK : WHITE, 0.68f);
            if (o.name >= 0) { snprintf(b, sizeof b, "%dk/race +%dk if top %d", o.perRace, o.bonus, o.goal); P.text(18, yy + 15, b, i == sel ? INK : DIM, 0.6f); }
        }
        { float yy = 66 + 3 * 40; if (sel == 3) P.fill(12, yy - 3, 192, 21, RGBA(255, 150, 40, 230)); P.text(20, yy, "Back", sel == 3 ? INK : WHITE, 0.74f); }
        line("YOUR SPONSORS", ACC, 0.7f);
        for (int s = 0; s < 2; s++) {
            const Sponsor& sp = c.sponsor[s];
            if (sp.name >= 0) { snprintf(b, sizeof b, "%s: %s", s ? "Partner" : "Title", SPONSOR_NAMES[sp.name]); line(b, WHITE);
                snprintf(b, sizeof b, "  %dk a race, +%dk if top %d in class", sp.perRace, sp.bonus, sp.goal); line(b, DIM); }
            else { snprintf(b, sizeof b, "%s: (empty)", s ? "Partner" : "Title"); line(b, DIM); y += 14; }
        }
        y += 6;
        line("Signing an offer replaces the", DIM); line("sponsor in that slot. Offers", DIM); line("change after every race.", DIM);
    } else if (careerPage == 2) {
        P.text(20, 48, "DRIVER MARKET", ACC, 0.62f);
        for (int i = 0; i < 6; i++) {
            const Hire& h = c.market[i];
            Car tmp; tmp.drv[1] = h.d; char nm[24]; Race::driverName(tmp, 1, nm, sizeof nm);
            float yy = 64 + i * 25;
            if (i == sel) P.fill(12, yy - 3, 192, 23, RGBA(255, 150, 40, 230));
            snprintf(b, sizeof b, "%s  %d", nm, driverRating(h.d)); P.text(18, yy, b, i == sel ? INK : WHITE, 0.66f);
            snprintf(b, sizeof b, "stamina %d  %dk/race", (int)(h.d.stamina * 100), h.salary); P.text(18, yy + 11, b, i == sel ? INK : DIM, 0.56f);
        }
        { float yy = 64 + 6 * 25; if (sel == 6) P.fill(12, yy - 3, 192, 21, RGBA(255, 150, 40, 230)); P.text(20, yy, "Back", sel == 6 ? INK : WHITE, 0.74f); }
        line("YOUR TEAM-MATES", ACC, 0.7f);
        for (int s = 0; s < 2; s++) {
            Car tmp; tmp.drv[1] = c.crew[s].d; char nm[24]; Race::driverName(tmp, 1, nm, sizeof nm);
            snprintf(b, sizeof b, "Seat %d: %s  rating %d", s + 2, nm, driverRating(c.crew[s].d)); line(b, WHITE);
            snprintf(b, sizeof b, "  stamina %d   %dk a race", (int)(c.crew[s].d.stamina * 100), c.crew[s].salary); line(b, DIM);
        }
        y += 6;
        line("X: hire for seat 2", GOLD); line("SQUARE: hire for seat 3", GOLD);
        line("Signing fee: one race's pay.", DIM);
        line("Rating = speed; stamina = how", DIM); line("long before they tire.", DIM);
    } else if (careerPage == 3) {
        P.text(20, 48, "UPGRADES", ACC, 0.62f);
        static const char* WHAT[UP_COUNT] = {"+1.5% power a level", "more grip, faster corners", "-7 kg a level", "6% faster pit stops a level"};
        for (int i = 0; i < UP_COUNT; i++) {
            float yy = 66 + i * 36;
            if (i == sel) P.fill(12, yy - 3, 192, 33, RGBA(255, 150, 40, 230));
            P.text(18, yy, UPGRADE_NAMES[i], i == sel ? INK : WHITE, 0.72f);
            for (int k = 0; k < UP_MAX; k++) P.fill(110 + k * 18, yy + 3, 14, 8, k < c.up[i] ? RGBA(80, 220, 90) : RGBA(60, 64, 74));
            int cost = c.upgradeCost(i);
            if (cost) { money(m, sizeof m, cost); snprintf(b, sizeof b, "next level %s", m); } else snprintf(b, sizeof b, "top level");
            P.text(18, yy + 15, b, i == sel ? INK : DIM, 0.6f);
        }
        { float yy = 66 + 4 * 36; if (sel == 4) P.fill(12, yy - 3, 192, 21, RGBA(255, 150, 40, 230)); P.text(20, yy, "Back", sel == 4 ? INK : WHITE, 0.74f); }
        money(m, sizeof m, c.money); snprintf(b, sizeof b, "Budget %s", m); line(b, GOLD, 0.9f); y += 4;
        if (sel < UP_COUNT) { line(UPGRADE_NAMES[sel], ACC, 0.7f); line(WHAT[sel], WHITE); }
        y += 6;
        const CarDef& d = CARS[c.carIndex()];
        snprintf(b, sizeof b, "Power %.0f kW   Mass %.0f kg", d.powerKW, d.massKg); line(b, DIM);
        snprintf(b, sizeof b, "Grip x%.3f", d.grip); line(b, DIM);
        line("Upgrades last the whole career.", DIM);
    } else if (careerPage == 5) {
        row(0, "Back", WHITE);
        int idx[48]; int n = c.champ.standings((CarClass)c.cls, idx, 48);
        snprintf(b, sizeof b, "%s STANDINGS  (after %d of %d)", ERA_SHORT[c.cls % ERA_COUNT], rd, CHAMP_ROUND_COUNT); line(b, ACC, 0.7f);
        for (int k = 0; k < 11 && k < n; k++) {
            const ChampEntry& e = c.champ.e[idx[k]];
            snprintf(b, sizeof b, "%2d  #%-3d %-10s %5.1f", k + 1, e.number, CARS[e.car].make, e.pts);
            line(b, e.number == c.number ? GOLD : WHITE);
        }
        if (!n) line("No races run yet this season", DIM);
    }
    if (noteT > 0) { P.fill(0, P.vh - 20, P.vw, 20, RGBA(0, 0, 0, 200)); P.textC(240, P.vh - 17, note, GOLD, 0.72f); }
    else P.textC(240, P.vh - 17, careerPage == 2 ? "X / SQUARE hire   O back" : "UP/DOWN choose   X select   O back", DIM, 0.68f);
}

// ------------------------------------------------------------------ settings > controls > car realism
void Game::screenRealism(Panel& P) {
    Realism& R = cfg.real;
    header(P, mechPage ? "MECHANICAL PROBLEMS" : "CAR REALISM");
    P.fill(14, 40, 452, 196, RGBA(0, 0, 0, 120));
    char v[12][56]; const char* tips[12] = {};
    int n;
    auto yn = [&](bool b, bool forced) { return forced ? "On (full)" : (b ? "On" : "Off"); };
    if (!mechPage) {
        bool f = R.full;
        snprintf(v[0], 56, "Full authentic experience"); tips[0] = "Everything on, damage at least 100 %";
        snprintf(v[1], 56, "Apply on opponents"); tips[1] = "The AI cars overheat, break down and blow tyres too";
        snprintf(v[2], 56, "Tyre temperature and wear"); tips[2] = "Off: the tyres never wear and are always warm";
        snprintf(v[3], 56, "Brake temperature"); tips[3] = "Cold brakes bite less; overheated ones fade (1980 steel discs early)";
        snprintf(v[4], 56, "Engine cooling and damage"); tips[4] = "Sitting in another car's tow heats the engine";
        snprintf(v[5], 56, "Slipstreaming"); tips[5] = "Less drag right behind another car";
        snprintf(v[6], 56, "Blow tyre when worn"); tips[6] = "A worn-out tyre can burst";
        snprintf(v[7], 56, "Ground contact"); tips[7] = "Kerbs and gravel knock the floor and suspension";
        snprintf(v[8], 56, "Consume fuel"); tips[8] = "Off: the tank never runs down";
        snprintf(v[9], 56, "Damage sensitivity"); tips[9] = "How much a hit hurts (the 1980 cars take 50 % more)";
        snprintf(v[10], 56, "Mechanical problems..."); tips[10] = "Which failures can happen (1980 cars break far more often)";
        snprintf(v[11], 56, "Back");
        n = 12;
        const char* val[12] = {R.full ? "On" : "Off", yn(R.opponents, f), yn(R.tyres, f), yn(R.brakes, f), yn(R.cooling, f),
                               yn(R.slipstream, f), yn(R.blowTyre, f), yn(R.ground, f), yn(R.fuel, f), nullptr, nullptr, nullptr};
        char dmg[16]; snprintf(dmg, sizeof dmg, "%d %%", (int)(R.damageK() * 100 + 0.5f)); val[9] = dmg;
        for (int i = 0; i < n; i++) {
            float y = 46 + i * 15.6f + (i >= 10 ? 4 : 0);
            bool on = i == sel;
            if (on) P.fill(20, y - 2, 440, 15, RGBA(255, 150, 40, 230));
            bool dim = f && i >= 1 && i <= 8;
            P.text(28, y, v[i], on ? INK : (dim ? DIM : WHITE), 0.76f);
            if (val[i]) { char b[24]; snprintf(b, sizeof b, "< %s >", val[i]); P.textR(452, y, b, on ? INK : (dim ? DIM : GOLD), 0.76f); }
        }
    } else {
        for (int k = 0; k < MF_COUNT; k++) snprintf(v[k], 56, "%s", MECH_NAME[k]);
        snprintf(v[MF_COUNT], 56, "Back");
        tips[MF_TURBO] = "Turbo cars only: the 1980 Viry and every car from 2014";
        tips[MF_PUNCTURE] = "A flat tyre: box for a new set";
        tips[MF_LEAK] = "The engine overheats until it lets go";
        tips[MF_THROTTLE] = "The throttle cuts out, or the brakes go soft";
        n = MF_COUNT + 1;
        for (int i = 0; i < n; i++) {
            float y = 50 + i * 17.5f + (i == MF_COUNT ? 6 : 0);
            bool on = i == sel;
            if (on) P.fill(20, y - 2, 440, 16, RGBA(255, 150, 40, 230));
            P.text(28, y, v[i], on ? INK : (R.full && i < MF_COUNT ? DIM : WHITE), 0.78f);
            if (i < MF_COUNT) P.textR(452, y, R.mechOn(i) ? (R.full ? "< On (full) >" : "< On >") : "< Off >", on ? INK : (R.full ? DIM : GOLD), 0.78f);
        }
    }
    if (sel >= 0 && sel < n && tips[sel]) P.textC(240, 240, tips[sel], DIM, 0.64f);
    P.textC(240, 255, "LEFT/RIGHT or X change     O back", DIM, 0.72f);
}

// ------------------------------------------------------------------ team headquarters
// The team principal's office: a desk with a model of each era's car on a plinth, the team's posters on
// the wall behind, a trophy cabinet, a window over the factory. Left / right walks along the desk,
// X picks the era (then the main menu). Built once, drawn relative to the camera like the garage.
namespace {
std::vector<WorldVertex> hq;
bool hqBuilt = false;
const float HQ_CAR_X[ERA_COUNT] = {-1.2f, -0.4f, 0.4f, 1.2f};
const float HQ_DESK_Y = 0.95f, HQ_TOP = 0.76f, HQ_PLINTH = 0.055f, HQ_SCALE = 0.12f;
const char* HQ_LINE1[ERA_COUNT] = {"Ground-effect wing cars: sliding skirts, 3.0 litre V8s",
                                    "Screaming 2.4 litre V8s at 18,000 rpm, high noses",
                                    "1.6 litre turbo hybrids: ERS boost and DRS",
                                    "Ground effect returns: wide low noses, 18-inch wheels"};
const char* HQ_LINE2[ERA_COUNT] = {"No safety car, no refuelling: double yellows and raw grip",
                                    "Shark fins, and the first season without refuelling",
                                    "Low finger noses, muted engines, huge torque",
                                    "The halo, ERS and DRS on the closest grid yet"};

void hqQuad(const V3& a, const V3& b, const V3& c, const V3& d, u32 ca, u32 cb) {
    WorldVertex A{0, 0, ca, a.x, a.y, a.z}, B{0, 0, ca, b.x, b.y, b.z}, C{0, 0, cb, c.x, c.y, c.z}, D{0, 0, cb, d.x, d.y, d.z};
    hq.push_back(A); hq.push_back(B); hq.push_back(C); hq.push_back(A); hq.push_back(C); hq.push_back(D);
}
void hqBox(const V3& lo, const V3& hi, u32 col) {
    V3 a(lo.x, lo.y, lo.z), b(hi.x, lo.y, lo.z), c(hi.x, hi.y, lo.z), d(lo.x, hi.y, lo.z);
    V3 e(lo.x, lo.y, hi.z), f(hi.x, lo.y, hi.z), g(hi.x, hi.y, hi.z), h(lo.x, hi.y, hi.z);
    hqQuad(e, f, g, h, col, col);
    hqQuad(a, b, f, e, shade(col, 0.8f), shade(col, 0.8f)); hqQuad(d, c, g, h, shade(col, 0.7f), shade(col, 0.7f));
    hqQuad(a, d, h, e, shade(col, 0.62f), shade(col, 0.62f)); hqQuad(b, c, g, f, shade(col, 0.9f), shade(col, 0.9f));
}

void buildHQ() {
    hqBuilt = true;
    hq.clear();
    const float W = 5, D = 4, H = 3.2f;
    // a dark wood floor in planks, a rug under the desk
    for (int i = 0; i < 20; i++) {
        float x0 = -W + i * 0.5f;
        u32 c = (i % 3 == 0) ? RGBA(92, 62, 40) : (i % 3 == 1 ? RGBA(104, 70, 44) : RGBA(86, 58, 38));
        hqQuad(V3(x0, -D, 0), V3(x0 + 0.5f, -D, 0), V3(x0 + 0.5f, D, 0), V3(x0, D, 0), shade(c, 0.8f), c);
    }
    hqQuad(V3(-2.4f, -1.2f, 0.004f), V3(2.4f, -1.2f, 0.004f), V3(2.4f, 2.8f, 0.004f), V3(-2.4f, 2.8f, 0.004f), RGBA(40, 44, 58), RGBA(52, 56, 72));
    // walls: deep blue-grey, a wood panel band below the dado rail
    u32 wall = RGBA(54, 62, 80), wallHi = RGBA(70, 80, 100), panel = RGBA(70, 48, 32);
    hqQuad(V3(-W, D, 0), V3(W, D, 0), V3(W, D, H), V3(-W, D, H), wall, wallHi);                 // behind the desk
    hqQuad(V3(-W, D - 0.01f, 0), V3(W, D - 0.01f, 0), V3(W, D - 0.01f, 1.0f), V3(-W, D - 0.01f, 1.0f), shade(panel, 0.8f), panel);
    hqQuad(V3(W, -D, 0), V3(W, D, 0), V3(W, D, H), V3(W, -D, H), shade(wall, 0.85f), shade(wallHi, 0.85f));   // right
    hqQuad(V3(W - 0.01f, -D, 0), V3(W - 0.01f, D, 0), V3(W - 0.01f, D, 1.0f), V3(W - 0.01f, -D, 1.0f), shade(panel, 0.7f), shade(panel, 0.85f));
    hqQuad(V3(-W, -D, 0), V3(W, -D, 0), V3(W, -D, H), V3(-W, -D, H), shade(wall, 0.6f), shade(wall, 0.7f));   // behind the camera
    // the left wall with a big window over the factory: wall round the opening, the frame, the view
    const float wy0 = -2.6f, wy1 = 2.8f, wz0 = 0.9f, wz1 = 2.7f;
    u32 lw = shade(wall, 0.75f), lwh = shade(wallHi, 0.75f);
    hqQuad(V3(-W, -D, 0), V3(-W, D, 0), V3(-W, D, wz0), V3(-W, -D, wz0), lw, lw);
    hqQuad(V3(-W, -D, wz1), V3(-W, D, wz1), V3(-W, D, H), V3(-W, -D, H), lwh, lwh);
    hqQuad(V3(-W, -D, wz0), V3(-W, wy0, wz0), V3(-W, wy0, wz1), V3(-W, -D, wz1), lw, lwh);
    hqQuad(V3(-W, wy1, wz0), V3(-W, D, wz0), V3(-W, D, wz1), V3(-W, wy1, wz1), lw, lwh);
    for (float y = wy0; y <= wy1 + 0.01f; y += (wy1 - wy0) / 3) hqBox(V3(-W - 0.05f, y - 0.04f, wz0), V3(-W + 0.05f, y + 0.04f, wz1), RGBA(30, 32, 36));
    hqBox(V3(-W - 0.05f, wy0, wz0 - 0.06f), V3(-W + 0.12f, wy1, wz0), RGBA(36, 38, 44));
    hqBox(V3(-W - 0.05f, wy0, wz1), V3(-W + 0.05f, wy1, wz1 + 0.06f), RGBA(30, 32, 36));
    // outside: an evening sky, the factory and the test track's grass
    hqQuad(V3(-40, -40, -2), V3(-40, 40, -2), V3(-40, 40, 30), V3(-40, -40, 30), RGBA(250, 170, 110), RGBA(60, 90, 160));
    hqQuad(V3(-40, -40, -0.5f), V3(-W, -40, -0.5f), V3(-W, 40, -0.5f), V3(-40, 40, -0.5f), RGBA(60, 100, 56), RGBA(70, 112, 62));
    hqBox(V3(-30, -14, -0.5f), V3(-16, 6, 5.5f), RGBA(200, 204, 214));                        // the factory
    hqBox(V3(-30, 7, -0.5f), V3(-20, 18, 3.5f), RGBA(170, 176, 190));
    hqBox(V3(-16.1f, -13, 3.2f), V3(-16.0f, 5, 4.4f), RGBA(120, 170, 220));                  // its glass band
    hqQuad(V3(-15.95f, -11, 4.6f), V3(-15.95f, 3, 4.6f), V3(-15.95f, 3, 5.3f), V3(-15.95f, -11, 5.3f), RGBA(255, 150, 40), RGBA(255, 150, 40));
    for (int i = 0; i < 6; i++) hqBox(V3(-34, -26 + i * 9.0f, -0.5f), V3(-31, -23 + i * 9.0f, 4 + (i % 3) * 2.0f), RGBA(40, 64, 40));   // trees
    // ceiling and its lights
    hqQuad(V3(-W, -D, H), V3(W, -D, H), V3(W, D, H), V3(-W, D, H), RGBA(44, 46, 54), RGBA(40, 42, 50));
    for (int i = -1; i <= 1; i++) hqQuad(V3(i * 2.5f - 0.5f, -0.4f, H - 0.02f), V3(i * 2.5f + 0.5f, -0.4f, H - 0.02f), V3(i * 2.5f + 0.5f, 0.4f, H - 0.02f), V3(i * 2.5f - 0.5f, 0.4f, H - 0.02f), RGBA(255, 250, 235), RGBA(255, 250, 235));
    // the desk: a long walnut top on two panelled ends, a modesty panel at the back
    u32 walnut = RGBA(110, 68, 40);
    hqBox(V3(-1.98f, 0.50f, HQ_TOP - 0.05f), V3(1.98f, 1.40f, HQ_TOP), walnut);
    hqBox(V3(-1.93f, 0.55f, 0), V3(-1.81f, 1.35f, HQ_TOP - 0.05f), shade(walnut, 0.85f));
    hqBox(V3(1.81f, 0.55f, 0), V3(1.93f, 1.35f, HQ_TOP - 0.05f), shade(walnut, 0.85f));
    hqBox(V3(-1.81f, 1.20f, 0.18f), V3(1.81f, 1.26f, HQ_TOP - 0.05f), shade(walnut, 0.7f));
    hqQuad(V3(-1.98f, 0.50f, HQ_TOP + 0.001f), V3(1.98f, 0.50f, HQ_TOP + 0.001f), V3(1.98f, 0.53f, HQ_TOP + 0.001f), V3(-1.98f, 0.53f, HQ_TOP + 0.001f), RGBA(200, 170, 90), RGBA(200, 170, 90));   // brass edge
    // the plinths for the model cars
    for (int e = 0; e < ERA_COUNT; e++) {
        float x = HQ_CAR_X[e];
        hqBox(V3(x - 0.33f, HQ_DESK_Y - 0.17f, HQ_TOP), V3(x + 0.33f, HQ_DESK_Y + 0.17f, HQ_TOP + HQ_PLINTH), RGBA(22, 22, 26));
    }
    // on the desk: a laptop (left), a lamp (right)
    hqBox(V3(-1.94f, 0.62f, HQ_TOP), V3(-1.66f, 0.84f, HQ_TOP + 0.015f), RGBA(60, 62, 70));
    hqQuad(V3(-1.94f, 0.84f, HQ_TOP + 0.015f), V3(-1.66f, 0.84f, HQ_TOP + 0.015f), V3(-1.66f, 0.90f, HQ_TOP + 0.20f), V3(-1.94f, 0.90f, HQ_TOP + 0.20f), RGBA(40, 70, 110), RGBA(80, 130, 190));
    hqBox(V3(1.75f, 1.12f, HQ_TOP), V3(1.87f, 1.24f, HQ_TOP + 0.02f), RGBA(40, 40, 44));
    hqBox(V3(1.80f, 1.17f, HQ_TOP + 0.02f), V3(1.82f, 1.19f, HQ_TOP + 0.42f), RGBA(180, 160, 90));
    hqBox(V3(1.70f, 1.08f, HQ_TOP + 0.40f), V3(1.92f, 1.28f, HQ_TOP + 0.48f), RGBA(30, 60, 40));
    // the principal's chair behind the desk
    hqBox(V3(-0.28f, 2.55f, 0.42f), V3(0.28f, 3.05f, 0.50f), RGBA(96, 54, 34));
    hqBox(V3(-0.28f, 2.98f, 0.50f), V3(0.28f, 3.08f, 1.05f), RGBA(96, 54, 34));
    hqBox(V3(-0.04f, 2.76f, 0), V3(0.04f, 2.84f, 0.42f), RGBA(90, 92, 98));
    hqBox(V3(-0.28f, 2.52f, 0), V3(0.28f, 3.08f, 0.05f), RGBA(60, 62, 68));
    // the trophy cabinet on the right wall: glass-fronted shelves of gold cups
    hqBox(V3(W - 0.55f, -2.6f, 0), V3(W - 0.02f, 1.2f, 2.4f), RGBA(46, 32, 24));
    for (int s = 0; s < 4; s++) {
        float z = 0.3f + s * 0.52f;
        hqBox(V3(W - 0.56f, -2.5f, z), V3(W - 0.08f, 1.1f, z + 0.03f), RGBA(150, 150, 160));
        for (int k = 0; k < 6; k++) {
            float y = -2.25f + k * 0.58f, hh = 0.18f + ((k + s) % 3) * 0.06f;
            hqBox(V3(W - 0.40f, y - 0.06f, z + 0.03f), V3(W - 0.28f, y + 0.06f, z + 0.07f), RGBA(60, 50, 40));
            hqBox(V3(W - 0.38f, y - 0.03f, z + 0.07f), V3(W - 0.30f, y + 0.03f, z + 0.07f + hh), RGBA(230, 185, 60));
            hqBox(V3(W - 0.41f, y - 0.07f, z + 0.03f + hh), V3(W - 0.27f, y + 0.07f, z + 0.10f + hh), RGBA(250, 205, 80));
        }
    }
    // the four era posters behind the desk: frames (the game prints the years on them)
    for (int e = 0; e < ERA_COUNT; e++) {
        float x = -3.3f + e * 2.2f;
        hqBox(V3(x - 0.60f, D - 0.06f, 1.05f), V3(x + 0.60f, D - 0.01f, 2.25f), RGBA(20, 20, 22));
    }
}
}   // namespace

void Game::chooseEra(int e) {
    era = e; hqChosen = true;
    if (set.car < 0 || set.car >= CAR_PICK || CARS[set.car].era != e) set.car = eraFirstCar(e) + ((set.car >= 0 ? set.car : 0) % TEAMS_PER_ERA);
    bool inCal = false;
    for (int k = 0; k < ERA_ROUNDS; k++) if (eraTrack(e, k) == set.track) inCal = true;
    if (!inCal) set.track = eraTrack(e, 0);
}

void Game::drawHQ(float dt) {
    if (!hqBuilt) buildHQ();
    // the camera follows along the desk to the model being looked at
    hqLook += (HQ_CAR_X[hqSel] - hqLook) * (1 - expf(-dt * 4.0f));
    V3 eye(hqLook * 0.85f + sinf(t * 0.4f) * 0.02f, -0.30f, 1.16f + sinf(t * 0.31f) * 0.008f);
    V3 target(hqLook, HQ_DESK_Y + 0.25f, HQ_TOP + 0.20f);
    V3 fwd = norm(target - eye);
    float fov = 42;
    gfx::beginFrame(RGBA(30, 34, 44));
    M4 proj = M4::perspective(fov, (float)SCREEN_W / SCREEN_H, 0.05f, 120.0f), view = M4::lookDir(V3(0, 0, 0), fwd, Z);
    gfx::setProjection(proj); gfx::setView(view);
    clip::setMatrices(proj, view);
    gfx::setFog(false, 0, 1, 0);
    gfx::setDepthTest(true); gfx::setDepthWrite(true); gfx::setBlend(false);
    gfx::bindTexture(nullptr);
    clip::drawWorld(hq.data(), (int)hq.size(), M4::translate(V3(0, 0, 0) - eye));
    R.camRight = norm(cross(fwd, Z)); R.camUp = cross(R.camRight, fwd);
    M4 rel = M4::translate(V3(0, 0, 0) - eye);
    // the posters: the team's colours and the era's year
    for (int e = 0; e < ERA_COUNT; e++) {
        float x = -3.3f + e * 2.2f;
        u32 mn, sc; R.teamColours(&CARS[eraFirstCar(e) + (set.car % TEAMS_PER_ERA)], mn, sc);
        M4 face = rel * M4::basis(V3(1, 0, 0), Z, V3(0, -1, 0), V3(x, 3.93f, 1.65f));
        R.quad3D(face, -0.55f, -0.55f, 0.55f, 0.55f, e == hqSel ? RGBA(250, 250, 245) : RGBA(200, 200, 196));
        R.quad3D(face * M4::translate(V3(0, 0, 0.004f)), -0.55f, -0.55f, 0.55f, -0.15f, mn | 0xFF000000u);
        R.quad3D(face * M4::translate(V3(0, 0, 0.006f)), -0.55f, -0.19f, 0.55f, -0.15f, sc | 0xFF000000u);
        R.text3D(face * M4::translate(V3(0, 0.08f, 0.008f)), ERA_SHORT[e], 0.34f, RGBA(30, 30, 34), true);
    }
    R.text3D(rel * M4::basis(V3(1, 0, 0), Z, V3(0, -1, 0), V3(0, 3.985f, 2.55f)), "FORMULA LEGENDS RACING", 0.20f, RGBA(255, 150, 40), true);
    // the model cars: each era's car of the player's team slot, the chosen one turning on its plinth
    for (int e = 0; e < ERA_COUNT; e++) {
        int idx = eraFirstCar(e) + (set.car % TEAMS_PER_ERA);
        float yaw = e == hqSel ? -0.6f + t * 0.5f : -0.6f;
        V3 at(HQ_CAR_X[e], HQ_DESK_Y, HQ_TOP + HQ_PLINTH);
        M4 place = M4::translate(at - eye) * M4::rotZ(yaw) * M4::scale(HQ_SCALE, HQ_SCALE, HQ_SCALE);
        R.drawCarModel(idx, place, true);
        // the plaque in front of the plinth
        M4 plaque = M4::translate(V3(HQ_CAR_X[e], HQ_DESK_Y - 0.20f, HQ_TOP + 0.03f) - eye) * M4::basis(V3(1, 0, 0), norm(V3(0, 0.45f, 1)), norm(V3(0, -1, 0.45f)), V3(0, 0, 0));
        R.quad3D(plaque, -0.11f, -0.035f, 0.11f, 0.035f, e == hqSel ? RGBA(255, 150, 40) : RGBA(190, 160, 80));
        R.text3D(plaque * M4::translate(V3(0, -0.022f, 0.002f)), ERA_SHORT[e], 0.045f, RGBA(20, 20, 20), true);
    }
    // the words: what the era is, how to pick it
    gfx::setDepthTest(false); gfx::setBlend(true);
    Panel P; P.R = &R; P.flat = true;
    P.fill(0, 0, SCREEN_W, 26, RGBA(0, 0, 0, 160));
    P.fill(0, 26, SCREEN_W, 2, ACC);
    P.text(10, 5, "TEAM HEADQUARTERS", WHITE, 1.0f);
    P.textR(SCREEN_W - 10, 7, "choose your era", DIM, 0.75f);
    P.fill(0, SCREEN_H - 74, SCREEN_W, 74, RGBA(0, 0, 0, 170));
    char b[64]; snprintf(b, sizeof b, "%s", ERA_NAME[hqSel]);
    for (char* c = b; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    P.textC(SCREEN_W / 2, SCREEN_H - 70, b, ACC, 1.15f);
    P.textC(SCREEN_W / 2, SCREEN_H - 46, HQ_LINE1[hqSel], WHITE, 0.72f);
    P.textC(SCREEN_W / 2, SCREEN_H - 32, HQ_LINE2[hqSel], DIM, 0.72f);
    P.text(10, SCREEN_H - 16, hqChosen ? "LEFT / RIGHT  era     X  choose     O  back" : "LEFT / RIGHT  era     X  choose", DIM, 0.64f);
    for (int e = 0; e < ERA_COUNT; e++) P.fill(SCREEN_W / 2 - 30 + e * 16, SCREEN_H - 82, 12, 4, e == hqSel ? ACC : RGBA(255, 255, 255, 90));
    gfx::setBlend(false); gfx::setDepthTest(true);
    nowPlayingBar();
    if (cfg.showFps) drawFps();
    finishFrame();
}

// the showroom: the car's team, model and numbers over the garage view
void Game::drawShowroomInfo() {
    const CarDef& d = CARS[showroomCar];
    gfx::setDepthTest(false); gfx::setBlend(true);
    Panel P; P.R = &R; P.flat = true;
    P.fill(0, 0, SCREEN_W, 26, RGBA(0, 0, 0, 160));
    P.fill(0, 26, SCREEN_W, 2, ACC);
    char b[96];
    snprintf(b, sizeof b, "SHOWROOM  %s", ERA_SHORT[d.era]);
    P.text(10, 5, b, WHITE, 1.0f);
    snprintf(b, sizeof b, "%d / %d", showroomCar - eraFirstCar(d.era) + 1, TEAMS_PER_ERA);
    P.textR(SCREEN_W - 10, 7, b, DIM, 0.75f);
    P.fill(8, 36, 170, 112, RGBA(0, 0, 0, 150));
    P.fill(8, 36, 4, 112, d.colour | 0xFF000000u);
    P.text(18, 40, d.team, WHITE, 1.05f);
    snprintf(b, sizeof b, "%s %s", d.make, d.model);
    P.text(18, 60, b, ACC, 0.8f);
    snprintf(b, sizeof b, "Engine  %s", d.engine);
    P.text(18, 80, b, DIM, 0.66f);
    snprintf(b, sizeof b, "Power  %.0f hp", d.powerKW * 1.341f);
    P.text(18, 95, b, DIM, 0.66f);
    snprintf(b, sizeof b, "Weight  %.0f kg with driver", d.massKg);
    P.text(18, 110, b, DIM, 0.66f);
    snprintf(b, sizeof b, "Cars  #%d  #%d", d.numbers[0], d.numbers[1]);
    P.text(18, 125, b, DIM, 0.66f);
    P.fill(0, SCREEN_H - 22, SCREEN_W, 22, RGBA(0, 0, 0, 160));
    P.text(8, SCREEN_H - 18, "LEFT/RIGHT team  L/R era  stick turn  UP/DOWN zoom  /\\ spin  O back", DIM, 0.56f);
    gfx::setBlend(false); gfx::setDepthTest(true);
}
