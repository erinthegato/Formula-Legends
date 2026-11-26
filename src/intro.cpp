// The Formula Legends intro and attract movie, cut to the music. The intro ('Take On The World' by All Good
// Folks on Uppbeat, DATA/music/intro.emu): "CODE 4 STUDIOS" strobes on the opening beats, then from the
// first big hit a highlight reel of the
// four eras, a block of shots per era on one circuit: empty-track crane shots, DRS trains, overtakes,
// pit stops, crashes and a thunderstorm, era title cards between them, and the game's title at the end.
// Everything is the real game: the era's cars and circuit, driven by the race code, staged per shot.
#include "game.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {
enum ShotType { SH_EMPTY, SH_DRS, SH_PIT, SH_OVERTAKE, SH_CRASH, SH_STORM, SH_START, SH_CARD, SH_FINAL, SH_TEXT };
struct Block { int era; const char* track; int weather; float hour; };
// a shot: first bar (bars of 4 beats from the music's first beat), era block, type; a text card's two lines
struct Shot { int bar; int block; int type; const char* line1; const char* line2; };

// ---- the boot intro: Take On The World (DATA/music/intro.emu, the first 70 s, 124.4 BPM). Bar 0 is the
// first big hit at 9.89 s. Sections: the beat comes in at bar 3, the drop at bar 11, the second half of
// the chorus at bar 19, a 4-bar lift at 23, the energy falls away in bar 26 and the breakdown starts at
// 27. Era cards sit on bars 3, 11 and 19, the start on 23 and the title on the breakdown at 27.
const Block INTRO_BLOCKS[] = {
    {ERA_GE, "suzuka", WX_DRY, 14.5f},
    {ERA_90S, "monaco", WX_DRY, 15.0f},
    {ERA_V8, "spa", WX_STORM, 14.0f},
    {ERA_HYBRID, "redbullring", WX_DRY, 17.6f},
};
const Shot INTRO_SHOTS[] = {
    {0, 0, SH_EMPTY}, {1, 0, SH_DRS}, {2, 0, SH_OVERTAKE},
    {3, 1, SH_CARD}, {4, 1, SH_EMPTY}, {5, 1, SH_OVERTAKE}, {7, 1, SH_CRASH}, {9, 1, SH_PIT},
    {11, 2, SH_CARD}, {12, 2, SH_STORM}, {15, 2, SH_OVERTAKE}, {17, 2, SH_CRASH},
    {19, 3, SH_CARD}, {20, 3, SH_DRS}, {21, 3, SH_PIT}, {22, 3, SH_OVERTAKE}, {23, 3, SH_START}, {25, 3, SH_DRS}, {26, 3, SH_EMPTY},
    {27, 3, SH_FINAL},
};

// ---- the attract movie: "Nocturn" (Tonebox, an original made for the game with ElevenLabs;
// DATA/music/attract.emu, the whole song, 171.7 s, 100 BPM, bar 0 at 0.292 s, bars of 2.4 s). Its
// sections: a quiet first bar, the band comes in at bar 1, the breakdown at 18, the build from 26, the
// big drop at 33, a new section at 50, the outro from 66. One era per section; the cards sit on the
// bar before so the next circuit loads while the music turns.
const Block ATTRACT_BLOCKS[] = {
    {ERA_90S, "imola", WX_DRY, 14.0f},
    {ERA_V8, "singapore", WX_DRY, 20.5f},        // the first night race
    {ERA_HYBRID, "silverstone", WX_DRY, 13.0f},
    {ERA_HYBRID, "interlagos", WX_STORM, 14.0f},
    {ERA_GE, "lasvegas", WX_DRY, 22.0f},
    {ERA_GE, "zandvoort", WX_DRY, 15.0f},
};
const Shot ATTRACT_SHOTS[] = {
    // the quiet first bar: the studio
    {0, 0, SH_TEXT, "CODE 4 STUDIOS", "PRESENTS"},
    // the band comes in: 1980 at Imola, lights out on the first beat
    {1, 0, SH_START}, {3, 0, SH_OVERTAKE}, {5, 0, SH_PIT}, {7, 0, SH_CRASH},
    // 2010 under the Singapore lights
    {9, 1, SH_CARD},
    {10, 1, SH_OVERTAKE}, {12, 1, SH_PIT}, {14, 1, SH_CRASH}, {15, 1, SH_OVERTAKE}, {17, 1, SH_EMPTY},
    // the breakdown: 2016 at Silverstone, slow crane shots and DRS
    {18, 2, SH_CARD},
    {19, 2, SH_EMPTY}, {21, 2, SH_DRS}, {23, 2, SH_EMPTY}, {24, 2, SH_TEXT, "FOUR ERAS", "ONE GRID"},
    // the build: 2016 in the rain at Interlagos
    {26, 3, SH_CARD},
    {27, 3, SH_STORM}, {29, 3, SH_OVERTAKE}, {31, 3, SH_STORM},
    // the big drop: 2025 on the Las Vegas Strip, lights out on the hit
    {32, 4, SH_CARD},
    {33, 4, SH_START}, {35, 4, SH_DRS}, {37, 4, SH_OVERTAKE}, {39, 4, SH_PIT}, {41, 4, SH_CRASH}, {43, 4, SH_DRS},
    {45, 4, SH_OVERTAKE}, {47, 4, SH_EMPTY},
    // the next section: 2025 by the sea at Zandvoort
    {49, 5, SH_CARD},
    {50, 5, SH_OVERTAKE}, {52, 5, SH_PIT}, {54, 5, SH_DRS}, {56, 5, SH_CRASH}, {58, 5, SH_OVERTAKE}, {60, 5, SH_DRS},
    {62, 5, SH_PIT}, {64, 5, SH_EMPTY},
    // the outro: the title until the song ends, then it all starts again
    {66, 5, SH_FINAL},
};

struct Script { const Block* blocks; const Shot* shots; int nshots; const char* music; const char* txt; bool logo; bool loop; };
const Script SCRIPTS[2] = {
    {INTRO_BLOCKS, INTRO_SHOTS, (int)(sizeof(INTRO_SHOTS) / sizeof(Shot)), "intro.emu", "DATA/music/intro.txt", true, false},
    {ATTRACT_BLOCKS, ATTRACT_SHOTS, (int)(sizeof(ATTRACT_SHOTS) / sizeof(Shot)), "attract.emu", "DATA/music/attract.txt", false, true},
};
int curScript = 0;
#define BLOCKS (SCRIPTS[curScript].blocks)
#define SHOTS (SCRIPTS[curScript].shots)
#define SHOT_N (SCRIPTS[curScript].nshots)
bool isCard(int type) { return type == SH_CARD || type == SH_TEXT; }

float sstep(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
float hash1(int k) { uint32_t h = (uint32_t)k * 2654435761u; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return (h & 0xFFFFFF) / 16777216.0f; }
}

float Game::introBarTime(int bar) const { return introBeat0 + bar * 4 * introPeriod; }

void Game::introBegin(int script) {
    // DATA/music/<intro|attract>.txt: the music's first beat, the beat length, the piece's length
    curScript = script; introScript = script;
    const Script& sc = SCRIPTS[script];
    int sz = 0;
    char* txt = (char*)sys::loadFile(sc.txt, &sz);
    if (!txt) { screen = script && hqChosen ? SCR_TITLE : SCR_HQ; idle = 0; return; }   // (no intro music: straight to headquarters)
    std::string all(txt, sz); sys::freeFile(txt);
    sscanf(all.c_str(), "beat0 %f\nperiod %f\nlength %f", &introBeat0, &introPeriod, &introLength);
    screen = SCR_INTRO; t = 0; introLastPos = 0;
    introShot = -1; introBlock = -1; introPendingBlock = -1; introCardFrames = 0;
    introLoadBlock(sc.shots[0].block);           // the first circuit loads before the music starts
    // the attract movie loops with its music (the file plays round again without a gap)
    sound::musicFile(sc.music, !sc.loop);
}

void Game::introEnd() {
    sound::musicFile(nullptr);
    leaveRace();
    introBlock = -1;
    // after the intro, headquarters (pick the era); after the attract movie, back where the player was
    screen = hqChosen ? SCR_TITLE : SCR_HQ; sel = 0; t = 0; idle = 0;
    hqSel = era;
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
    c.body = CarBody();
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

// the attract movie (many shots per circuit): a different corner each shot, from the circuit's
// tightest corners (curvature peaks at least 250 m apart), in turn
static float nthCorner(const Track& t, int n) {
    float ks[8], ss[8]; int m = 0;
    float L = t.length;
    for (float s = 0; s < L; s += t.step) {
        float k = fabsf(t.curvAt(s));
        if (k < 0.012f || k < fabsf(t.curvAt(t.wrap(s - t.step))) || k < fabsf(t.curvAt(t.wrap(s + t.step)))) continue;
        int near = -1;
        for (int i = 0; i < m; i++) { float d = fabsf(ss[i] - s); if (fminf(d, L - d) < 250) near = i; }
        if (near >= 0) { if (k > ks[near]) { ks[near] = k; ss[near] = s; } continue; }
        if (m < 8) { ks[m] = k; ss[m] = s; m++; continue; }
        int lo = 0; for (int i = 1; i < 8; i++) if (ks[i] < ks[lo]) lo = i;
        if (k > ks[lo]) { ks[lo] = k; ss[lo] = s; }
    }
    if (!m) return tightestCorner(t, 0, L);
    // in order round the lap, so neighbouring shots are on different parts of the circuit
    for (int i = 0; i < m; i++) for (int j = i + 1; j < m; j++) if (ss[j] < ss[i]) { float a = ss[i]; ss[i] = ss[j]; ss[j] = a; }
    return ss[(n * 3) % m];
}

void Game::introStage(int k) {
    const Shot& S = SHOTS[k];
    if (isCard(S.type) || S.type == SH_FINAL || !raceLoaded) return;
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
        float s0 = (curScript ? nthCorner(tr, k) : tightestCorner(tr, L * (0.15f + 0.4f * hash1(seedish)), L * 0.3f)) - 140;
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
        float c0 = curScript ? nthCorner(tr, k + 1) : tightestCorner(tr, L * (0.1f + 0.5f * hash1(seedish + 3)), L * 0.35f);
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
        float c0 = curScript ? nthCorner(tr, k + 2) : tightestCorner(tr, L * (0.2f + 0.5f * hash1(seedish + 5)), L * 0.3f);
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
    const Script& sc = SCRIPTS[curScript];
    if (skip || (!sc.loop && introPos >= introLength - 0.05f) || t > introLength + 30) { introEnd(); return; }
    if (sc.loop && introPos < introLastPos - 5) {
        // the music has gone round: back to the first shot (its title card covers loading the first circuit)
        t = 0; introShot = -1;
    }
    introLastPos = introPos;
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
        if (isCard(S.type) && S.block != introBlock) { introPendingBlock = S.block; introCardFrames = 0; return; }
        if (S.block != introBlock && S.type != SH_FINAL) { introLoadBlock(S.block); }
        introStage(k);
    }
    if (!raceLoaded || isCard(SHOTS[introShot].type) || SHOTS[introShot].type == SH_FINAL) return;
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
    // another car passing right by the lens gets cut open by the near plane: lift the camera over it,
    // still looking at the same spot
    if (S.type != SH_EMPTY) {
        V3 aim = c.eye + c.fwd * 20.0f;
        for (int i = 0; i < n; i++) {
            const Car& o = r.cars[i];
            // (the shot's own cars may be framed close - the DRS shot sits 4.6 m behind - so a smaller margin)
            float near = (&o == &a || &o == &b) ? 4.0f : 5.0f;
            float dist = len(c.eye - o.pos);
            if (dist < near) c.eye.z += (near - dist) * 0.9f;
        }
        c.fwd = norm(aim - c.eye);
    }
    return c;
}

static void bigText(Renderer& R, float cx, float y, const char* s, u32 col, float scale) { R.font.drawCentre(cx, y, s, col, scale); }

void Game::drawIntro() {
    float p = introPos;
    const u32 BLACK = RGBA(0, 0, 0);
    // ---- 0 .. first big hit: CODE 4 STUDIOS, flashing on the opening beats
    if ((p < introBeat0 - 0.02f || introShot < 0) && !SCRIPTS[curScript].logo) { gfx::beginFrame(BLACK); finishFrame(); return; }
    if (p < introBeat0 - 0.02f || introShot < 0) {
        gfx::beginFrame(BLACK);
        gfx::setDepthTest(false); gfx::setBlend(true);
        // a strobe on the music's beats (0.24 s + n x 0.482 s) that quickens: bars, half bars, then every
        // beat into the first big hit, where the name holds and burns out to white
        static const float REV[] = {0.24f, 2.17f, 4.10f, 5.06f, 6.03f, 6.99f, 7.96f, 8.44f, 8.92f, 9.40f};
        float flash = 0;
        for (float r0 : REV) { float d = p - r0; if (d >= 0 && d < 0.55f) flash = fmaxf(flash, 1 - d / 0.55f); }
        float hold = sstep(8.9f, 9.5f, p);
        float a = fmaxf(flash, hold);
        float glow = 0.35f + 0.65f * a;
        int A = (int)(255 * clampf(0.12f + a, 0, 1));
        // scan line and a flash of the whole screen on the strongest beats
        if (flash > 0.85f) gfx::drawFill2D(0, 0, SCREEN_W, SCREEN_H, RGBA(255, 255, 255, (int)(90 * (flash - 0.85f) / 0.15f)));
        float jitter = flash > 0.5f ? (hash1((int)(p * 60)) - 0.5f) * 4 : 0;
        bigText(R, SCREEN_W / 2 + jitter, 96, "CODE 4", RGBA(255, (int)(70 + 150 * glow), (int)(40 + 60 * glow), A), 3.6f);
        bigText(R, SCREEN_W / 2 - jitter, 150, "S T U D I O S", RGBA(255, 255, 255, A), 1.5f);
        gfx::drawFill2D(SCREEN_W / 2 - 120 * glow, 140, 240 * glow, 2, RGBA(255, 70, 40, A));
        if (p > 9.45f) gfx::drawFill2D(0, 0, SCREEN_W, SCREEN_H, RGBA(255, 255, 255, (int)(255 * sstep(9.45f, introBeat0, p))));
        gfx::setBlend(false); gfx::setDepthTest(true);
        finishFrame();
        return;
    }
    const Shot& S = SHOTS[introShot];
    float st = p - introBarTime(S.bar);
    // ---- a text card (the attract movie's opening)
    if (S.type == SH_TEXT) {
        gfx::beginFrame(BLACK);
        gfx::setDepthTest(false); gfx::setBlend(true);
        float in = sstep(0, 0.6f, st);
        float beatPh = fmodf(fmaxf(st, 0), introPeriod) / introPeriod;
        int A = (int)(255 * in);
        gfx::drawFill2D(SCREEN_W / 2 - 150 * in, 128, 300 * in, 2, RGBA(255, 70, 40, (int)(A * (0.55f + 0.45f * (1 - beatPh)))));
        bigText(R, SCREEN_W / 2, 84, S.line1, RGBA(255, 255, 255, A), 2.4f);
        if (S.line2) bigText(R, SCREEN_W / 2, 144, S.line2, RGBA(255, 200, 150, (int)(A * sstep(0.4f, 1.0f, st))), 1.1f);
        gfx::setBlend(false); gfx::setDepthTest(true);
        introCardFrames++;
        finishFrame();
        return;
    }
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
        static const char* CAPS[ERA_COUNT] = {"THE WING CAR ERA", "THE V8 ERA", "THE HYBRID ERA", "THE GROUND EFFECT ERA"};
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
        float in = sstep(0, 0.35f, st), out = 1 - sstep(introLength - 1.4f, introLength - 0.2f, p);
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
        if (SCRIPTS[curScript].loop && fmodf(p, 1.0f) < 0.6f) bigText(R, SCREEN_W / 2, 196, "PRESS ANY BUTTON", RGBA(255, 255, 255, A), 0.9f);
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
