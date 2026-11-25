#include "platform.h"
#include "settings.h"
#include "sound.h"
#include "render.h"
#include <cstdio>
#include <cstring>

Config cfg;
const char* BUTTON_NAME[BTN_COUNT] = {"X", "O", "Square", "Triangle", "Select", "D-pad up", "D-pad down"};
const char* ACTION_NAME[ACT_COUNT] = {"Accelerate", "Brake", "Gear up", "Gear down", "Change camera", "Look behind", "Pit request", "Engine map", "DRS"};
const char* buttonName(int b) { return b >= 0 && b < BTN_COUNT ? BUTTON_NAME[b] : "None"; }

bool held(const Pad& p, int b) {
    switch (b) {
        case BTN_CROSS: return p.cross; case BTN_CIRCLE: return p.circle; case BTN_SQUARE: return p.square;
        case BTN_TRIANGLE: return p.triangle;
        case BTN_SELECT: return p.select; case BTN_UP: return p.up; case BTN_DOWN: return p.down;
    }
    return false;
}

int firstHeld(const Pad& p) {
    for (int b = 0; b < BTN_COUNT; b++) if (held(p, b)) return b;
    return -1;
}

void clearBind(int action) { if (action >= 0 && action < ACT_COUNT) cfg.bind[action] = BTN_NONE; }
void Config::resetBinds() { Config d; memcpy(bind, d.bind, sizeof bind); }

void Config::rebind(int action, int button) {
    if (button != BTN_NONE) for (int a = 0; a < ACT_COUNT; a++) if (a != action && bind[a] == button) bind[a] = bind[action];
    bind[action] = button;
}

void Config::setPreset(int p) {
    preset = p;
    if (p == 0) { fps = 0; distance = 0; scenery = true; effects3d = true; }
    if (p == 1) { fps = 0; distance = 1; scenery = true; effects3d = false; }
    if (p == 2) { fps = 1; distance = 2; scenery = false; effects3d = false; }
}

void Config::apply(int autoLevel) const {
    sound::setMaster(master / 10.0f * 1.1f);
    sound::setVolumes(music / 10.0f, engine / 10.0f, effects / 10.0f, radio / 10.0f);
    static const float DIST[3] = {1.0f, 0.75f, 0.55f}, LOD[3] = {1.0f, 0.8f, 0.6f};
    // automatic quality: 1 effects reduced, 2 scenery reduced, 3-4 draw distance one / two steps nearer
    int d = distance + (autoLevel > 2 ? autoLevel - 2 : 0); if (d > 2) d = 2;
    perf.distance = DIST[d]; perf.lodBias = LOD[d];
    perf.scenery = scenery && autoLevel < 2; perf.effects = effects3d && autoLevel < 1;
    gfx::setSync(vsync, fps == 1 ? 2 : 1);
}

static const int VERSION = 7;
const char* MECH_NAME[MF_COUNT] = {"Engine failure", "Gearbox and clutch failure", "Turbo failure", "Suspension failure",
    "Puncture", "Lose wheel", "Oil and water leak", "Throttle and brake problems", "Broken steering rod"};

void Config::save() const {
    FILE* f = fopen("settings.cfg", "wb");
    if (!f) return;
    fprintf(f, "v %d\nsound %d %d %d %d %d\nperf %d %d %d %d %d %d %d %d %d %d\nbind", VERSION, master, music, engine, effects, radio,
            preset, (int)vsync, fps, distance, (int)scenery, (int)effects3d, (int)showFps, attract, pitCam, (int)autoQuality);
    for (int a = 0; a < ACT_COUNT; a++) fprintf(f, " %d", bind[a]);
    fprintf(f, "\ndrive %d\nvita %d %d %d %d\ncockpit %d %d\nsteering %d %d %d\n", (int)spins, tilt, (int)tiltInvert, (int)mirror, (int)rearLook, (int)hideWheel, (int)cockpitHud, 1, steerHelp, 1);
    fprintf(f, "realism %d %d %d %d %d %d %d %d %d %d", (int)real.full, (int)real.opponents, (int)real.tyres, (int)real.brakes,
            (int)real.cooling, (int)real.slipstream, (int)real.blowTyre, (int)real.ground, (int)real.fuel, real.damagePct);
    for (bool m : real.mech) fprintf(f, " %d", (int)m);
    fprintf(f, "\n");
    fclose(f);
}

void Config::load() {
    FILE* f = fopen("settings.cfg", "rb");
    if (!f) return;
    Config c; int v = 0, vs = 1, sc = 1, ef = 1, sf = 0, aq = 1;
    bool ok = fscanf(f, "v %d\n", &v) == 1 && (v == VERSION || v == 6 || v == 5 || v == 4)
           && fscanf(f, "sound %d %d %d %d %d\n", &c.master, &c.music, &c.engine, &c.effects, &c.radio) == 5
           && (v == 4 ? fscanf(f, "perf %d %d %d %d %d %d %d %d %d\n", &c.preset, &vs, &c.fps, &c.distance, &sc, &ef, &sf, &c.attract, &c.pitCam) == 9
                      : fscanf(f, "perf %d %d %d %d %d %d %d %d %d %d\n", &c.preset, &vs, &c.fps, &c.distance, &sc, &ef, &sf, &c.attract, &c.pitCam, &aq) == 10)
           && fscanf(f, "bind") == 0;
    int nb = v < 7 ? ACT_DRS : ACT_COUNT;    // files before v7 have no DRS bind
    for (int a = 0; ok && a < nb; a++) ok = fscanf(f, " %d", &c.bind[a]) == 1 && c.bind[a] >= BTN_NONE && c.bind[a] < BTN_COUNT;
    int sp = 1;
    if (ok && v >= 6) ok = fscanf(f, "\ndrive %d", &sp) == 1;     // (older files: spins on)
    c.spins = sp != 0;
    int tl = 0, ti = 0, mi = 1, rl = 0;
    if (ok && fscanf(f, "\nvita %d %d %d %d", &tl, &ti, &mi, &rl) >= 2) { c.tilt = tl < 0 ? 0 : (tl > 3 ? 3 : tl); c.tiltInvert = ti != 0; c.mirror = mi != 0; c.rearLook = rl != 0; }
    int hw = 0, ch = 0;
    if (ok && fscanf(f, "\ncockpit %d %d", &hw, &ch) == 2) { c.hideWheel = hw != 0; c.cockpitHud = ch != 0; }   // (optional line)
    int scv = -1, shp = -1, hdl = 1;
    if (ok && fscanf(f, "\nsteering %d %d %d", &scv, &shp, &hdl) >= 2) {
        (void)scv; (void)hdl; c.steerHelp = shp < -1 ? -1 : (shp > 2 ? 2 : shp); }   // (optional line)
    {   // (optional line) car realism
        int r[10 + MF_COUNT];
        if (ok && fscanf(f, "\nrealism %d %d %d %d %d %d %d %d %d %d", &r[0], &r[1], &r[2], &r[3], &r[4], &r[5], &r[6], &r[7], &r[8], &r[9]) == 10) {
            Realism& R = c.real;
            R.full = r[0]; R.opponents = r[1]; R.tyres = r[2]; R.brakes = r[3]; R.cooling = r[4]; R.slipstream = r[5];
            R.blowTyre = r[6]; R.ground = r[7]; R.fuel = r[8]; R.damagePct = r[9] < 0 ? 0 : (r[9] > 200 ? 200 : r[9] / 25 * 25);
            for (int k = 0; k < MF_COUNT; k++) if (fscanf(f, " %d", &r[10 + k]) == 1) R.mech[k] = r[10 + k] != 0;
        }
    }
    fclose(f);
    if (!ok) return;
    auto in = [](int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); };
    c.master = in(c.master, 0, 10); c.music = in(c.music, 0, 10); c.engine = in(c.engine, 0, 10); c.effects = in(c.effects, 0, 10); c.radio = in(c.radio, 0, 10);
    c.attract = in(c.attract / 5 * 5, 5, 30);
    c.pitCam = in(c.pitCam, 0, 1);
    c.preset = in(c.preset, 0, 3); c.fps = in(c.fps, 0, 1); c.distance = in(c.distance, 0, 2);
    c.vsync = vs != 0; c.scenery = sc != 0; c.effects3d = ef != 0; c.showFps = sf != 0; c.autoQuality = aq != 0;
    // two actions on one button would make one of them unusable
    for (int a = 0; a < ACT_COUNT; a++) for (int b2 = a + 1; b2 < ACT_COUNT; b2++) if (c.bind[a] != BTN_NONE && c.bind[a] == c.bind[b2]) { c.resetBinds(); }
    *this = c;
}

int Config::help() const { return steerHelp >= 0 ? steerHelp : 1; }   // light by default everywhere
