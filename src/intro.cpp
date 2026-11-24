// The Formula Legends intro, cut to the music (Motley Crue - Kickstart My Heart, DATA/music/intro.emu):
// the bike revs under a flashing "CODE 4 STUDIOS", then from the first beat a highlight reel of the
// four eras, a block of shots per era on one circuit: empty-track crane shots, DRS trains, overtakes,
// pit stops, crashes and a thunderstorm, era title cards between them, and the game's title at the end.
// Everything is the real game: the era's cars and circuit, driven by the race code, staged per shot.
#include "game.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {
enum ShotType { SH_EMPTY, SH_DRS, SH_PIT, SH_OVERTAKE, SH_CRASH, SH_STORM, SH_START, SH_CARD, SH_FINAL };
struct Block { int era; const char* track; int weather; float hour; };
// the four era blocks (each loads its circuit while its title card shows)
const Block BLOCKS[4] = {
    {ERA_GE, "suzuka", WX_DRY, 14.5f},
    {ERA_90S, "monaco", WX_DRY, 15.0f},
    {ERA_V8, "spa", WX_STORM, 14.0f},
    {ERA_HYBRID, "redbullring", WX_DRY, 17.6f},
};
// the shots: first bar (bars of 4 beats from the band's first beat), block, type
struct Shot { int bar; int block; int type; };
const Shot SHOTS[] = {
    {0, 0, SH_EMPTY}, {2, 0, SH_DRS}, {4, 0, SH_PIT}, {6, 0, SH_OVERTAKE},
    {8, 1, SH_CARD}, {9, 1, SH_EMPTY}, {10, 1, SH_OVERTAKE}, {12, 1, SH_CRASH}, {14, 1, SH_PIT},
    {16, 2, SH_CARD}, {17, 2, SH_STORM}, {20, 2, SH_OVERTAKE}, {22, 2, SH_CRASH},
    {24, 3, SH_CARD}, {25, 3, SH_EMPTY}, {26, 3, SH_DRS}, {28, 3, SH_PIT}, {30, 3, SH_OVERTAKE}, {32, 3, SH_START}, {34, 3, SH_DRS}, {35, 3, SH_EMPTY},
    {36, 3, SH_FINAL},
};
const int SHOT_N = sizeof(SHOTS) / sizeof(SHOTS[0]);
const int FINAL_BARS = 4;

float sstep(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
float hash1(int k) { uint32_t h = (uint32_t)k * 2654435761u; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return (h & 0xFFFFFF) / 16777216.0f; }
}

float Game::introBarTime(int bar) const { return introBeat0 + bar * 4 * introPeriod; }

void Game::introBegin() {
    // DATA/music/intro.txt: the band's first beat, the beat length, the intro's length
    int sz = 0;
    char* txt = (char*)sys::loadFile("DATA/music/intro.txt", &sz);
    if (!txt) return;
    std::string all(txt, sz); sys::freeFile(txt);
    sscanf(all.c_str(), "beat0 %f\nperiod %f\nlength %f", &introBeat0, &introPeriod, &introLength);
    screen = SCR_INTRO; t = 0;
    introShot = -1; introBlock = -1; introPendingBlock = -1; introCardFrames = 0;
    introLoadBlock(0);                         // the first circuit loads before the music starts
    sound::musicFile("intro.emu", true);
}

void Game::introEnd() {
    sound::musicFile(nullptr);
    leaveRace();
    introBlock = -1;
    screen = SCR_TITLE; sel = 0; t = 0; idle = 0;
}

void Game::introLoadBlock(int b) {
    const Block& B = BLOCKS[b];
    RaceSettings s;
    for (int i = 0; i < TRACK_COUNT; i++) if (!strcmp(TRACKS[i].id, B.track)) s.track = i;
    s.car = eraFirstCar(B.era);
    s.laps = 50; s.opponents = 7; s.difficulty = 2; s.startPos = 0;
    s.startType = START_STANDING; s.weather = B.weather; s.sessionMin = 600; s.rubber = 0.5f;
    if (raceLoaded) leaveRace();
    // practice: no race control (no safety cars), DRS free in its zones
    raceLoaded = race.start(s, SES_PRACTICE);
    if (!raceLoaded) return;
    afterLoad();
    race.startHour = B.hour; race.time = 50; race.greenTime = 0; race.countdown = 0;
    for (auto& c : race.cars) { c.pit = PIT_NONE; c.pitPhase = 0; c.lift = 0; c.hold = true; c.engineOn = true; c.outLap = false; }
    if (B.weather == WX_STORM) { race.rain = race.rainTarget = 0.85f; race.wet = 0.95f; race.storm = true; race.nextStrike = 1.5f; }
    else { race.rain = race.rainTarget = 0; race.wet = 0; race.storm = false; race.rainTimer = 1e6f; }
    introBlock = b;
}

// put a car on the circuit at s (metres round the lap), d (metres left of the centre line), moving at v
void Game::introPlace(Car& c, float s, float d, float v) {
    c.s = race.track.wrap(s); c.d = d; c.v = v; c.yaw = 0; c.drift = 0; c.spinning = false; c.yawRate = 0;
    c.total = c.s; c.lap = 0; c.pit = PIT_NONE; c.pitPhase = 0; c.lift = 0; c.wantPit = false;
    c.hold = false; c.throttle = v > 1 ? 1 : 0; c.brake = 0; c.avoid = 0; c.mistake = 0;
    c.drs = false; c.drsZone = -1; c.drsFlap = 0; c.slip = 0; c.offTrack = false;
    for (float& x : c.dmg) x = 0;
    c.noseOff = c.wingOff = false; c.fuel = 0.6f; c.wear = 0.2f; c.tyre = race.wet > 0.4f ? TY_WET : TY_MEDIUM;
    c.gear = (int)clampf(v / 13.0f + 1, 1, 7);
    race.pose(c);
}

// the hardest corner within a stretch of the lap (biggest curvature), and the longest straight
static float tightestCorner(const Track& t, float from, float len) {
    float best = from, bk = 0;
    for (float s = from; s < from + len; s += t.step) { float k = fabsf(t.curvAt(t.wrap(s))); if (k > bk) { bk = k; best = s; } }
    return t.wrap(best);
}

void Game::introStage(int k) {
    const Shot& S = SHOTS[k];
    if (S.type == SH_CARD || S.type == SH_FINAL || !raceLoaded) return;
    Race& r = race;
    const Track& tr = r.track;
    float L = tr.length;
    for (auto& c : r.cars) { c.hold = true; introPlace(c, 0, 0, 0); c.hold = true; c.s = tr.wrap(L * 0.5f + c.number * 9.0f); c.d = 0; c.v = 0; r.pose(c); }
    for (auto& d : r.debris) d.life = 0;
    r.parts.clear();
    introShotT0 = introBarTime(S.bar);
    R.tvInit = false; R.tvCut = k * 3;
    introCamS = 0; introFocus = 0; introFocus2 = 1;
    int n = (int)r.cars.size();
    auto car = [&](int i) -> Car& { return r.cars[i % n]; };
    uint32_t seedish = (uint32_t)(k * 7 + introBlock * 13);
    switch (S.type) {
    case SH_EMPTY: {
        // a crane sweep along a curving stretch with nobody on it
        float s0 = tightestCorner(tr, L * (0.15f + 0.4f * hash1(seedish)), L * 0.3f) - 140;
        introCamS = s0;
        break;
    }
    case SH_DRS: {
        // two cars into a DRS zone a second apart: the one behind opens its flap and pulls out to pass
        float s0 = 0;
        if (!r.drsZones.empty()) s0 = r.drsZones[(k / 2) % r.drsZones.size()].detect - 40;
        else s0 = L - 420;
        float v = 68;
        introPlace(car(0), s0 + 16, tr.pts[tr.index(s0)].hw * 0.15f, v + 1);
        introPlace(car(1), s0, -tr.pts[tr.index(s0)].hw * 0.1f, v + 2);
        introPlace(car(2), s0 + 60, 0, v);
        introFocus = 1; introFocus2 = 0;
        car(1).skill = 1.03f; car(0).skill = 0.95f;
        break;
    }
    case SH_PIT: {
        // a stop in the box: the crew at work, the car dropped, then away
        Car& c = car(2);
        introPlace(c, r.boxS(c), r.boxD, 0);
        c.pit = PIT_STOP; c.pitPhase = r.ep().refuel ? 0 : 1; c.pitTimer = 0; c.lift = 0.12f; c.hold = true;
        c.plan.tyres = true; c.plan.fuel = 1; c.plan.repair = false; c.plan.swapDriver = false; c.plan.tyre = TY_SOFT;
        introFocus = 2;
        // a car going by in the fast lane
        introPlace(car(5), r.pitIn + 40, r.laneD, 21);
        car(5).pit = PIT_IN; car(5).pitPhase = -1;
        break;
    }
    case SH_OVERTAKE: {
        // side by side into a corner: the car on the inside brakes later and takes the place
        float c0 = tightestCorner(tr, L * (0.1f + 0.5f * hash1(seedish + 3)), L * 0.35f);
        float s0 = c0 - 260;
        float side = tr.curvAt(c0) > 0 ? 1.0f : -1.0f;        // inside of the corner
        float hw = tr.pts[tr.index(s0)].hw;
        float v = r.wet > 0.4f ? 52 : 64;
        introPlace(car(3), s0 + 9, -side * hw * 0.25f, v);
        introPlace(car(4), s0, side * hw * 0.45f, v + 3);
        introPlace(car(6), s0 + 70, 0, v - 4);
        car(4).skill = 1.05f; car(4).lineBias = side * 1.5f; car(3).skill = 0.9f;
        introFocus = 4; introFocus2 = 3;
        introCamS = c0;
        break;
    }
    case SH_CRASH: {
        // too fast into a corner: the rear goes, the car spins into the wall and loses its wing
        float c0 = tightestCorner(tr, L * (0.2f + 0.5f * hash1(seedish + 5)), L * 0.3f);
        float side = tr.curvAt(c0) > 0 ? -1.0f : 1.0f;        // outside of the corner
        float s0 = c0 - 95;
        Car& c = car(5);
        introPlace(c, s0, side * 1.0f, r.wet > 0.4f ? 55 : 62);
        c.hold = false;
        introPlace(car(6), s0 - 60, 0, 58);
        introFocus = 5; introFocus2 = 6;
        introCamS = c0;
        introCrashDone = false;
        break;
    }
    case SH_STORM: {
        // a train of cars through the spray, lightning overhead
        float s0 = L * 0.30f;
        for (int i = 0; i < 6 && i < n; i++) introPlace(car(i), s0 - i * 22.0f, (i % 2 ? 1 : -1) * 0.8f, 50);
        introFocus = 5; introFocus2 = 0;
        r.lightning = 0; r.nextStrike = 0.9f;
        break;
    }
    case SH_START: {
        // the grid: lights out and away
        for (int i = 0; i < n; i++) {
            Car& c = car(i);
            float back = 10.0f + 8.0f * i;
            introPlace(c, -back, (i % 2 == 0 ? 1.0f : -1.0f) * tr.pts[0].hw * 0.34f, 0);
            c.total = -back; c.hold = true;
        }
        introFocus = 0;
        break;
    }
    }
    (void)seedish;
}

// during a shot: the scripted touches (the crash, the end of the pit stop, the start)
void Game::introDirect(int k, float st) {
    const Shot& S = SHOTS[k];
    Race& r = race;
    int n = (int)r.cars.size();
    auto car = [&](int i) -> Car& { return r.cars[i % n]; };
    if (S.type == SH_PIT) {
        Car& c = car(2);
        float stop = r.ep().refuel ? 1.9f : 1.25f;
        if (st < stop) { c.pitPhase = st < (r.ep().refuel ? 0.6f : 0.0f) ? 0 : 1; c.lift = 0.12f; }
        else if (c.hold) { c.pitPhase = 3; c.lift = fmaxf(0, 0.12f - (st - stop) * 0.6f); if (st > stop + 0.25f) { c.hold = false; c.pit = PIT_OUT; c.lift = 0; c.v = 3; } }
    }
    if (S.type == SH_CRASH && !introCrashDone && st > 0.35f) {
        Car& c = car(5);
        float side = c.d > 0 ? 1.0f : -1.0f;
        c.spinning = true; c.drift = 0.55f * side; c.yaw = 0.95f * side;
        introCrashDone = true;
    }
    if (S.type == SH_CRASH && introCrashDone) {
        Car& c = car(5);
        if (!c.wingOff && c.v < 45 && fabsf(c.d) > r.track.pts[r.track.index(c.s)].hw) { r.addDamage(c, DM_WING, 0.8f); r.addDamage(c, DM_NOSE, 0.4f); }
    }
    if (S.type == SH_START && st > 0.45f) for (int i = 0; i < n; i++) if (car(i).hold) { car(i).hold = false; car(i).v = 0.5f; }
}

void Game::introUpdate(float dt, bool skip) {
    introPos = sound::musicPos();
    if (skip || introPos >= introLength - 0.05f || t > 90) { introEnd(); return; }
    // a pending circuit: loaded once its title card has been on screen
    if (introPendingBlock >= 0 && introCardFrames >= 2) {
        int b = introPendingBlock; introPendingBlock = -1;
        introLoadBlock(b);
        if (introShot >= 0) introStage(introShot);
        return;
    }
    if (introPos < introBeat0 - 0.02f) return;           // the Code 4 flash: nothing moves yet
    // which shot is on
    int k = 0;
    for (int i = 0; i < SHOT_N; i++) if (introPos >= introBarTime(SHOTS[i].bar) - 0.01f) k = i;
    if (k != introShot) {
        introShot = k;
        const Shot& S = SHOTS[k];
        if (S.type == SH_CARD && S.block != introBlock) { introPendingBlock = S.block; introCardFrames = 0; return; }
        if (S.block != introBlock && S.type != SH_FINAL) { introLoadBlock(S.block); }
        introStage(k);
    }
    if (!raceLoaded || SHOTS[introShot].type == SH_CARD || SHOTS[introShot].type == SH_FINAL) return;
    float st = introPos - introShotT0;
    introDirect(introShot, st);
    // the race runs at real speed in small steps
    accum += fminf(dt, 0.1f);
    const float STEP = 1.0f / 60;
    int guard = 0;
    while (accum >= STEP && guard++ < 8) { race.update(STEP, 0, 0, 0, 0, true); accum -= STEP; introDirect(introShot, st); }
    race.sfxEvents.clear();
    sound::silence();
}

// the camera for the shot on screen
Camera Game::introCamera(float st) {
    const Shot& S = SHOTS[introShot];
    const Race& r = race;
    const Track& tr = r.track;
    int n = (int)r.cars.size();
    const Car& a = r.cars[introFocus % n];
    const Car& b = r.cars[introFocus2 % n];
    Camera c; c.up = V3(0, 0, 1); c.fov = 48;
    V3 p, tg, l; float hw;
    switch (S.type) {
    case SH_EMPTY: {
        // a crane rising and gliding along the road, looking ahead and down
        float s = introCamS + st * 34.0f;
        tr.frame(s, p, tg, l, hw);
        float h = 5.0f + st * 3.2f;
        c.eye = p + l * (hw + 9.0f) + V3(0, 0, h);
        V3 look = tr.world(s + 70, 0) + V3(0, 0, 0.5f);
        c.fwd = norm(look - c.eye); c.fov = 56;
        break;
    }
    case SH_DRS: {
        // low behind the car with DRS: the flap opening, then from beside as it draws alongside
        if (st < 1.5f) {
            c.eye = a.pos - a.fwd * 4.6f + a.up * 1.15f + a.left * 0.25f;
            c.fwd = norm(a.pos + a.fwd * 7.0f + a.up * 0.55f - c.eye); c.fov = 44;
        } else {
            c.eye = a.pos + a.left * 3.4f - a.fwd * 1.0f + V3(0, 0, 0.9f);
            c.fwd = norm((a.pos + b.pos) * 0.5f + V3(0, 0, 0.5f) - c.eye); c.fov = 58;
        }
        break;
    }
    case SH_PIT: {
        Camera cc = R.cinematicCamera(r, a, r.time);
        c = cc; c.fov = 54;
        if (a.pit == PIT_NONE || a.pit == PIT_OUT) {
            // follow it out of the box
            c.eye = a.pos + a.left * -5.0f - a.fwd * 7.0f + V3(0, 0, 2.2f);
            c.fwd = norm(a.pos + V3(0, 0, 0.6f) - c.eye);
        }
        break;
    }
    case SH_OVERTAKE: {
        // from above and behind the pair, swinging round to the inside as the move is made
        V3 flat = norm(V3(a.fwd.x, a.fwd.y, 0));
        V3 side = V3(-flat.y, flat.x, 0);
        float sw = sinf(st * 0.9f) * 3.0f;
        c.eye = (a.pos + b.pos) * 0.5f - flat * 13.0f + side * sw + V3(0, 0, 5.2f);
        c.fwd = norm((a.pos + b.pos) * 0.5f + flat * 10.0f - c.eye); c.fov = 50;
        break;
    }
    case SH_CRASH: {
        // following the car into the corner (it never comes out the other side)
        V3 flat = norm(V3(tg.x, tg.y, 0));
        tr.frame(a.s, p, tg, l, hw);
        flat = norm(V3(tg.x, tg.y, 0));
        V3 side = V3(-flat.y, flat.x, 0);
        float back = 15.0f - fminf(st, 2.0f) * 3.0f;
        c.eye = a.pos - flat * back + side * 2.0f + V3(0, 0, 4.2f);
        c.fwd = norm(a.pos + V3(0, 0, 0.4f) - c.eye); c.fov = 52;
        (void)b;
        break;
    }
    case SH_STORM: {
        // chasing the train through the spray
        const Car& lead = r.cars[introFocus % n];
        c.eye = lead.pos - lead.fwd * 9.5f + V3(0, 0, 2.3f) + lead.left * (1.2f * sinf(st * 0.6f));
        c.fwd = norm(lead.pos + lead.fwd * 20.0f - c.eye); c.fov = 50;
        break;
    }
    case SH_START: {
        // high behind the grid, looking down the straight
        tr.frame(-150 + st * 12, p, tg, l, hw);
        c.eye = p + V3(0, 0, 9.0f) + l * 4.0f;
        c.fwd = norm(tr.world(40 + st * 40, 0) - c.eye); c.fov = 52;
        break;
    }
    default:
        c.eye = V3(0, 0, 10); c.fwd = V3(1, 0, 0);
    }
    return c;
}

static void bigText(Renderer& R, float cx, float y, const char* s, u32 col, float scale) { R.font.drawCentre(cx, y, s, col, scale); }

void Game::drawIntro() {
    float p = introPos;
    const u32 BLACK = RGBA(0, 0, 0);
    // ---- 0 .. first beat: CODE 4 STUDIOS, flashing with the bike's revs
    if (p < introBeat0 - 0.02f || introShot < 0) {
        gfx::beginFrame(BLACK);
        gfx::setDepthTest(false); gfx::setBlend(true);
        // the revs: a strobe that comes faster as the bike screams up, then the name holds and burns
        static const float REV[] = {0.10f, 0.65f, 0.85f, 2.45f, 2.90f, 3.10f, 6.05f, 6.25f, 7.05f, 8.70f, 8.85f, 9.00f};
        float flash = 0;
        for (float r0 : REV) { float d = p - r0; if (d >= 0 && d < 0.55f) flash = fmaxf(flash, 1 - d / 0.55f); }
        float hold = sstep(8.6f, 9.4f, p);
        float a = fmaxf(flash, hold);
        float glow = 0.35f + 0.65f * a;
        int A = (int)(255 * clampf(0.12f + a, 0, 1));
        // scan line and a flash of the whole screen on the biggest revs
        if (flash > 0.85f) gfx::drawFill2D(0, 0, SCREEN_W, SCREEN_H, RGBA(255, 255, 255, (int)(90 * (flash - 0.85f) / 0.15f)));
        float jitter = flash > 0.5f ? (hash1((int)(p * 60)) - 0.5f) * 4 : 0;
        bigText(R, SCREEN_W / 2 + jitter, 96, "CODE 4", RGBA(255, (int)(70 + 150 * glow), (int)(40 + 60 * glow), A), 3.6f);
        bigText(R, SCREEN_W / 2 - jitter, 150, "S T U D I O S", RGBA(255, 255, 255, A), 1.5f);
        gfx::drawFill2D(SCREEN_W / 2 - 120 * glow, 140, 240 * glow, 2, RGBA(255, 70, 40, A));
        if (p > 9.3f) gfx::drawFill2D(0, 0, SCREEN_W, SCREEN_H, RGBA(255, 255, 255, (int)(255 * sstep(9.3f, introBeat0, p))));
        gfx::setBlend(false); gfx::setDepthTest(true);
        finishFrame();
        return;
    }
    const Shot& S = SHOTS[introShot];
    float st = p - introBarTime(S.bar);
    float bar = 4 * introPeriod;
    // ---- era title card (the circuit loads behind it)
    if (S.type == SH_CARD || introPendingBlock >= 0) {
        const Block& B = BLOCKS[S.block];
        gfx::beginFrame(BLACK);
        gfx::setDepthTest(false); gfx::setBlend(true);
        float beatPh = fmodf(st, introPeriod) / introPeriod;
        int bg = (int)(60 * (1 - beatPh));
        gfx::drawFill2D(0, 0, SCREEN_W, SCREEN_H, RGBA(bg + 20, 6, 4));
        gfx::drawFill2D(0, 118, SCREEN_W, 4, RGBA(255, 70, 40));
        bigText(R, SCREEN_W / 2, 52, ERA_SHORT[B.era], RGBA(255, 255, 255), 3.4f);
        static const char* CAPS[ERA_COUNT] = {"THE V10 ERA", "THE V8 ERA", "THE HYBRID ERA", "THE GROUND EFFECT ERA"};
        bigText(R, SCREEN_W / 2, 136, CAPS[B.era], RGBA(255, 200, 150), 1.2f);
        for (int i = 0; i < TRACK_COUNT; i++) if (!strcmp(TRACKS[i].id, B.track)) bigText(R, SCREEN_W / 2, 170, TRACKS[i].name, RGBA(200, 200, 210), 0.9f);
        gfx::setBlend(false); gfx::setDepthTest(true);
        introCardFrames++;
        finishFrame();
        return;
    }
    // ---- the title at the end
    if (S.type == SH_FINAL) {
        gfx::beginFrame(BLACK);
        gfx::setDepthTest(false); gfx::setBlend(true);
        float in = sstep(0, 0.35f, st), out = 1 - sstep(FINAL_BARS * bar - 1.4f, FINAL_BARS * bar - 0.2f, st);
        float beatPh = fmodf(st, introPeriod) / introPeriod;
        int A = (int)(255 * in * out);
        if (st < 0.25f) gfx::drawFill2D(0, 0, SCREEN_W, SCREEN_H, RGBA(255, 255, 255, (int)(255 * (1 - st / 0.25f))));
        gfx::drawFill2D(0, 132, SCREEN_W * in, 3, RGBA(255, 70, 40, A));
        bigText(R, SCREEN_W / 2, 64, "FORMULA", RGBA(255, 255, 255, A), 3.0f);
        bigText(R, SCREEN_W / 2, 100 - 2 * (1 - beatPh), "LEGENDS", RGBA(255, 70, 40, A), 2.6f);
        // the four eras, one per beat
        int lit = (int)(st / introPeriod);
        for (int e = 0; e < ERA_COUNT; e++) {
            u32 col = e < lit ? RGBA(255, 255, 255, A) : RGBA(90, 90, 100, A);
            bigText(R, SCREEN_W / 2 + (e - 1.5f) * 84, 150, ERA_SHORT[e], col, 1.1f);
        }
        bigText(R, SCREEN_W / 2, 230, "a CODE 4 STUDIOS game", RGBA(170, 170, 180, A), 0.75f);
        gfx::setBlend(false); gfx::setDepthTest(true);
        finishFrame();
        return;
    }
    // ---- a shot: the race drawn through the shot's camera
    if (!raceLoaded) { gfx::beginFrame(BLACK); finishFrame(); return; }
    Camera c = introCamera(st);
    gfx::beginFrame(R.skyColour(race));
    R.drawScene(race, c, false);
    R.drawNight(race, 0);
    M4 proj = M4::perspective(c.fov, (float)SCREEN_W / SCREEN_H, 0.8f, 1600.0f), view = M4::lookDir(V3(0, 0, 0), c.fwd, c.up);
    gfx::setProjection(proj); gfx::setView(view);
    gfx::setFog(false, 0, 1, 0);
    R.drawBeam(race, c);
    R.drawEffects(race, c);
    R.drawWeather(race, false);
    // the frame: letterbox bars, a flash on each cut, the era and circuit, what the shot is
    gfx::setDepthTest(false); gfx::setBlend(true);
    gfx::drawFill2D(0, 0, SCREEN_W, 22, RGBA(0, 0, 0, 255));
    gfx::drawFill2D(0, SCREEN_H - 22, SCREEN_W, 22, RGBA(0, 0, 0, 255));
    if (st < 0.12f) gfx::drawFill2D(0, 0, SCREEN_W, SCREEN_H, RGBA(255, 255, 255, (int)(200 * (1 - st / 0.12f))));
    const Block& B = BLOCKS[introBlock < 0 ? 0 : introBlock];
    char b[64];
    const char* tname = TRACKS[race.set.track].name;
    snprintf(b, sizeof b, "%s  %s", ERA_SHORT[B.era], tname);
    R.font.draw(10, SCREEN_H - 19, b, RGBA(255, 255, 255, 220), 0.8f);
    static const char* LABEL[] = {"", "DRS", "PIT STOP", "OVERTAKE", "", "", "LIGHTS OUT", "", ""};
    const char* lab = LABEL[S.type];
    if (S.type == SH_DRS && race.cars[introFocus % race.cars.size()].drsFlap < 0.5f) lab = "";
    if (lab[0]) {
        float w = R.font.width(lab, 1.0f) + 14;
        gfx::drawFill2D(SCREEN_W - w - 10, SCREEN_H - 20, w, 18, S.type == SH_DRS ? RGBA(40, 200, 90, 230) : RGBA(255, 70, 40, 230));
        R.font.draw(SCREEN_W - w - 3, SCREEN_H - 19, lab, RGBA(255, 255, 255), 1.0f);
    }
    gfx::setBlend(false); gfx::setDepthTest(true);
    finishFrame();
}
