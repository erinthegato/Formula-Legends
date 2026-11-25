#include "trophies.h"
#include "setups.h"
#include "game.h"
#include "clip.h"
#include <cstdio>
#include <string>
#include <cstring>
#include <cstdlib>
#include <algorithm>

static const char* CAMS[CAM_COUNT] = {"Chase", "Far chase", "Cockpit"};
// race length: > 0 laps, < 0 minutes
// race length: laps (999 = the full Grand Prix distance), negative = timed race in minutes
static const int LENGTHS[] = {1, 2, 3, 5, 8, 10, 15, 20, 30, 999, -10, -20, -30};
static const int LENGTH_N = sizeof(LENGTHS) / sizeof(LENGTHS[0]);
static const float EVENTH[] = {0, 1, 2, 4, 6, 8, 10, 12, 24};
static const int OPP[] = {5, 9, 13, 19};      // a full Formula 1 grid is 20 cars
static const int ACCEL[] = {1, 2, 4, 8, 16};

static u32 WHITE = RGBA(255, 255, 255), DIM = RGBA(175, 185, 200), ACC = RGBA(255, 150, 40), GOLD = RGBA(255, 215, 70);
#define bindPressed(p, a) (held(p, a) && !held(prev, a))

static void slotPath(char* out, int slot, const char* ext) { snprintf(out, 32, "save%d.%s", slot + 1, ext); }

bool Game::init() {
    bool ok = R.loadCommon();
    { FILE* f = fopen("bench.txt", "r"); if (f) { fclose(f); bench = true; remove("perf.log"); } }
    sound::init();
    cfg.load(); cfg.apply();
    trophies.load();
    setups.load();
    set.car = 1; set.track = 1;
    // the intro: Code 4 Studios, then the eras' highlight reel, cut to its music (intro.cpp)
    if (!bench) introBegin();
    return ok;
}

void Game::shutdown() {
    sound::shutdown();
    if (raceLoaded) race.end();
}

void Game::startRace() { startSession(set.weekend ? SES_PRACTICE : SES_RACE); }

void Game::afterLoad() {
    for (auto& c : race.cars) {
        R.loadCar((int)(c.def - CARS));
        sound::setVoice(1, c.def, c.def->idleRpm, 0, 0, false);
    }
    R.buildPit(race);
    sound::silence();
    R.camInit = false; R.tvInit = false;
    t = 0; accum = 0; steer = 0;
}

// time trial ghost / record file for this circuit and car, next to the game
void Game::ghostPath(char* out, int n) const { snprintf(out, n, "tt_%s_%s.gst", TRACKS[set.track].id, CARS[set.car].id); }

void Game::startSession(Session ses) {
    if (raceLoaded) race.end();
    if (!ttMode && (ses == SES_PRACTICE || (ses == SES_RACE && !set.weekend))) setups.get(set.track, CARS[set.car].era, set.setup);   // a new weekend: the remembered setup
    std::vector<int> grid;
    if (ses == SES_RACE && set.weekend && !ttMode) grid = qualiGrid;
    RaceSettings s = set;
    s.timeTrial = ttMode;
    // track rubber: green for practice, carried on through the weekend; a race without a weekend
    // starts part rubbered (support races ran before), a time trial on a well-used track
    if (ttMode) s.rubber = 0.6f;
    else if (ses == SES_PRACTICE) s.rubber = 0.1f;
    else s.rubber = set.weekend ? weekendRubber : 0.35f;
    if (ttMode) { s.career = false; ses = SES_PRACTICE; }
    raceLoaded = race.start(s, ses, grid.empty() ? nullptr : &grid);
    if (!raceLoaded) { screen = SCR_TRACK; return; }
    afterLoad();
    if (ttMode) { char gp[64]; ghostPath(gp, sizeof gp); race.loadGhost(gp); }
    screen = SCR_RACE;
    if (ses != SES_RACE && !aiDrivesPlayer) {
        // practice and qualifying start in the garage, at the diagnostics computer
        race.toGarage();
        screen = SCR_GARAGE; setupPage = true; look = 1; setupSel = 8;
    }
}

void Game::leaveRace() {
    menuCamInit = false;
    sound::silence(); sound::stopEffects();
    sound::loop(0, nullptr, 0); sound::loop(1, nullptr, 0);
    sound::freeEffects();         // radio calls, thunder, rain, pit sounds: loaded again when needed
    if (raceLoaded) race.end();
    raceLoaded = false;
    R.freeCars();
}

void Game::startReel() {
    // an AI race on a random circuit, shown through trackside TV cameras
    RaceSettings s;
    reelCount++;
    s.track = (int)((sys::timeSec() * 7 + reelCount * 3)) % TRACK_COUNT;
    s.car = (int)(sys::timeSec() * 13 + reelCount) % CAR_PICK;
    s.laps = 3; s.opponents = 11; s.difficulty = 2; s.startPos = 1;
    s.startType = START_EVENT;
    s.weather = reelCount % 3 == 2 ? WX_WET : WX_DRY;
    if (raceLoaded) leaveRace();
    raceLoaded = race.start(s, SES_RACE);
    if (!raceLoaded) { screen = SCR_TITLE; return; }
    afterLoad();
    reelFocus = 0; reelSwitch = 0;
    screen = SCR_REEL;
}

bool Game::loadSlot(int slot) {
    char p[32];
    slotPath(p, slot, "sav");
    if (raceLoaded) leaveRace();
    if (!race.load(p)) return false;
    raceLoaded = true;
    set = race.set;
    slotPath(p, slot, "chp");
    champMode = champ.load(p);
    careerMode = race.set.career && career.load("career.sav");
    if (careerMode) { champMode = false; career.apply(); careerLivery(); }
    afterLoad();
    screen = SCR_RACE;
    return true;
}

void Game::frame(float dt, const Pad& pad) {
    fps += (1.0f / fmaxf(dt, 0.001f) - fps) * 0.1f;
    if (dt > 0.1f) dt = 0.1f;
    lastDt = dt;
    if (bench) benchUpdate(dt);
    double tu0 = sys::timeSec();
    update(dt, pad);
    trophies.update(dt);
    double tu1 = sys::timeSec();
    // music: menus, results, podium (its own tune), the highlights reel and practice / qualifying
    // (through the trackside speakers); off in the race
    bool pausedOpt = (screen == SCR_OPTIONS || screen == SCR_CONTROLS) && optionsFrom == SCR_PAUSE;
    bool racing = raceLoaded && race.session == SES_RACE && (screen == SCR_RACE || screen == SCR_PAUSE || screen == SCR_TEAM || pausedOpt);
    if (screen == SCR_GARAGE) racing = false;
    if (screen != SCR_PODIUM && screen != SCR_INTRO) sound::musicEnable(!racing && screen != SCR_PAUSE && !pausedOpt);
    bool trackside = raceLoaded && race.session != SES_RACE && (screen == SCR_RACE || screen == SCR_GARAGE);
    sound::musicDirect(!trackside && screen != SCR_REEL);
    if (trackside) trackSpeakers();
    else sound::paddockSpeakers();
    // L / R skip songs in the menus
    bool menu = (!raceLoaded || screen == SCR_RESULTS || screen == SCR_PODIUM) && screen != SCR_INTRO;
    if (menu && screen != SCR_PODIUM && rebinding < 0) { if (pressed(pad, &Pad::r)) sound::musicSkip(1); if (pressed(pad, &Pad::l)) sound::musicSkip(-1); }
    sound::update();
    prev = pad;
    draw();
    double tu2 = sys::timeSec();
    perfFrame(dt, (tu1 - tu0) * 1000, (tu2 - tu1) * 1000);
    autoQualityStep(dt);
}

void Game::autoQualityStep(float dt) {
    bool racing = raceLoaded && (screen == SCR_RACE || screen == SCR_GARAGE);
    if (!cfg.autoQuality || !autoAllowed || !racing) { aqT = 0; aqFrames = 0; aqBusy = 0; return; }
    aqT += dt; aqFrames++;
    aqBusy += fmaxf(0, dt - gfx::lastWaitSec());          // time spent working, not waiting for the screen
    if (aqT < 2.5f) return;
    float target = cfg.fps ? 30.0f : 60.0f, budget = 1.0f / target;
    float fpsAvg = aqFrames / aqT, busy = aqBusy / aqFrames;
    // what a level actually changes for these settings (levels that change nothing are skipped)
    auto look = [&](int lv) {
        int d = cfg.distance + (lv > 2 ? lv - 2 : 0); if (d > 2) d = 2;
        return d * 4 + ((cfg.scenery && lv < 2) ? 2 : 0) + ((cfg.effects3d && lv < 1) ? 1 : 0);
    };
    if (fpsAvg < target * 0.9f && autoLevel < 4) {
        int lv = autoLevel;
        while (lv < 4 && look(lv + 1) == look(autoLevel)) lv++;
        if (lv < 4) lv++;
        if (look(lv) != look(autoLevel)) {
            autoLevel = lv; cfg.apply(autoLevel);
            race.message("Detail lowered automatically to keep it smooth", RGBA(150, 160, 180));
        }
        aqCalm = 0;
    } else if (autoLevel > 0 && fpsAvg >= target * 0.97f && busy < budget * 0.6f) {
        aqCalm += aqT;
        if (aqCalm > 10) {                                   // plenty of spare time for 10 s: one step back up
            int lv = autoLevel - 1;
            while (lv > 0 && look(lv) == look(autoLevel)) lv--;
            autoLevel = lv; cfg.apply(autoLevel); aqCalm = 0;
        }
    } else aqCalm = 0;
    aqT = 0; aqFrames = 0; aqBusy = 0;
}

// ------------------------------------------------------------------ performance log and benchmark
static const char* screenName(int s) {
    static const char* N[] = {"title", "track", "car", "settings", "credits", "race", "pause", "results", "team", "podium",
        "champ", "load", "reel", "options", "controls", "garage", "intro", "career", "career-new", "livery"};
    return s >= 0 && s < (int)(sizeof N / sizeof N[0]) ? N[s] : "?";
}

void Game::perfFrame(float dt, double updMs, double drawMs) {
    int mem = sys::memUsedKB();
    if (mem > memPeak) memPeak = mem;
    if (!cfg.showFps && !bench) return;
    perfUpd += updMs; perfDraw += drawMs; perfN++; perfT += dt;
    perfGpu += gfx::lastGpuWaitSec() * 1000; perfWait += gfx::lastWaitSec() * 1000;
    if (dt > perfWorst) perfWorst = dt;
    if (perfT < 2.0f) return;
    FILE* f = fopen("perf.log", "a");
    if (f) {
        const char* where = raceLoaded && (screen == SCR_RACE || screen == SCR_GARAGE) ? TRACKS[race.set.track].id : "-";
        const char* cams[4] = {"chase", "far", "cockpit", "tv"};
        bool manage = raceLoaded && race.player().plan.aiDrives;
        // frame time split: update (CPU), draw (CPU, includes waiting for the GPU), GPU wait, vsync wait
        fprintf(f, "%-10s %-9s %-8s fps %5.1f  frame %5.2f ms  worst %5.1f ms  update %5.2f  draw %5.2f  gpu-wait %5.2f  vsync-wait %5.2f ms  heap %5d KB (peak %5d)  vram %4d KB  cars %2d  rain %.2f  auto %d\n",
                bench && benchStep >= 0 ? "bench" : "play", screenName(screen), where[0] != '-' ? where : "-",
                perfN / perfT, perfT * 1000 / perfN, perfWorst * 1000, perfUpd / perfN, perfDraw / perfN, perfGpu / perfN, perfWait / perfN,
                mem, memPeak, gfx::vramUsedKB(), raceLoaded ? (int)race.cars.size() : 0, raceLoaded ? race.rain : 0.0f, autoLevel);
        (void)cams; (void)manage;
        fclose(f);
    }
    perfUpd = perfDraw = perfGpu = perfWait = 0; perfN = 0; perfT = 0; perfWorst = 0;
}

// the benchmark: the same scenes every time, the AI driving our car, then quit
void Game::benchUpdate(float dt) {
    struct Scene { const char* what; float secs; };
    static const Scene S[] = {
        {"menus", 10}, {"ardennes chase", 40}, {"ardennes cockpit", 25}, {"sablons night", 35},
        {"kagami storm", 35}, {"santerno pitwall", 25}, {"livery", 8}, {"end", 0}};
    const int N = sizeof S / sizeof S[0];
    benchT += dt; idle = 0;
    if (benchStep >= 0 && benchT < S[benchStep].secs) return;
    if (benchStep >= 0) { FILE* f = fopen("perf.log", "a"); if (f) { fprintf(f, "---- end of %s\n", S[benchStep].what); fclose(f); } }
    benchStep++; benchT = 0;
    if (benchStep >= N - 1) {
        FILE* f = fopen("perf.log", "a"); if (f) { fprintf(f, "==== benchmark done, heap peak %d KB\n", memPeak); fclose(f); }
        cfg.apply(); autoAllowed = true;
        if (raceLoaded) leaveRace();
        benchDone = true;
        sys::quit();
        return;
    }
    if (benchStep == 0) {
        // run flat out: no waiting for the screen, fixed detail, so the frame times show the real headroom
        autoAllowed = false; autoLevel = 0; cfg.apply(0);
        gfx::setSync(false, 1);
        FILE* f = fopen("perf.log", "a"); if (f) { fprintf(f, "==== benchmark: vsync off, detail preset %d, distance %d, scenery %d, effects %d\n", cfg.preset, cfg.distance, (int)cfg.scenery, (int)cfg.effects3d); fclose(f); }
    }
    { FILE* f = fopen("perf.log", "a"); if (f) { fprintf(f, "---- %s\n", S[benchStep].what); fclose(f); } }
    const char* w = S[benchStep].what;
    auto track = [](const char* id) { for (int i = 0; i < TRACK_COUNT; i++) if (!strcmp(TRACKS[i].id, id)) return i; return 0; };
    if (!strcmp(w, "menus")) { screen = SCR_TITLE; sel = 0; return; }
    if (!strcmp(w, "livery")) {
        if (raceLoaded) leaveRace();
        if (!career.active) { career.begin(0, ERA_GE, 1, 5); }
        career.apply(); careerLivery(); careerMode = true;
        screen = SCR_LIVERY; sel = 0; t = 0;
        return;
    }
    if (!strcmp(w, "ardennes cockpit")) { cam = CAM_COCKPIT; R.camInit = false; return; }   // same race, inside the car
    if (raceLoaded) leaveRace();
    RaceSettings s; s.car = 1; s.opponents = 11; s.raceMin = 30; s.weekend = false; s.difficulty = 1;
    if (!strcmp(w, "ardennes chase")) { s.track = track("ardennes"); s.weather = WX_DRY; }
    if (!strcmp(w, "sablons night")) { s.track = track("sablons"); s.weather = WX_DRY; s.startType = START_ROLLING; s.eventHours = 24; }
    if (!strcmp(w, "kagami storm")) { s.track = track("kagami"); s.weather = WX_STORM; s.car = 9; }
    if (!strcmp(w, "santerno pitwall")) { s.track = track("santerno"); s.weather = WX_DRY; }
    set = s; champMode = false; careerMode = false;
    aiDrivesPlayer = strcmp(w, "santerno pitwall") != 0;
    cam = CAM_CHASE;
    startRace();
    if (!strcmp(w, "sablons night")) race.startHour = 22.5f;                      // the sun is down
    if (!strcmp(w, "santerno pitwall")) { race.player().plan.aiDrives = true; wallView = true; }   // a team-mate drives, we watch from the pit wall
    if (!aiDrivesPlayer) {}
}

void Game::raceAudio() {
    if (!raceLoaded) return;
    const Car& me = race.player();
    bool quiet = screen == SCR_PAUSE || screen == SCR_TEAM || screen == SCR_REEL;
    bool manage = me.plan.aiDrives && !aiDrivesPlayer;
    float vol = 0.45f + 0.45f * me.throttle;
    if (race.countdown > 0) vol = 0.5f;
    if (manage) vol *= 0.4f;
    if (!me.engineOn) vol = 0;
    sound::setVoice(0, me.def, me.rpm, quiet ? 0 : vol, 0, false);
    // the three nearest opponents
    int best[3] = {-1, -1, -1}; float bd[3] = {1e9f, 1e9f, 1e9f};
    for (int i = 0; i < (int)race.cars.size(); i++) {
        if (i == race.playerIdx) continue;
        float d = len(race.cars[i].pos - me.pos);
        for (int k = 0; k < 3; k++) if (d < bd[k]) {
            for (int m = 2; m > k; m--) { bd[m] = bd[m - 1]; best[m] = best[m - 1]; }
            bd[k] = d; best[k] = i; break;
        }
    }
    for (int k = 0; k < 3; k++) {
        if (best[k] < 0 || quiet) { sound::setVoice(k + 1, nullptr, 0, 0, 0, false); continue; }
        const Car& o = race.cars[best[k]];
        V3 rel = o.pos - me.pos;
        float pan = dot(norm(rel), me.left * -1.0f) * 0.8f;
        float v = o.engineOn ? 0.55f / (1 + bd[k] / 10.0f) : 0;
        sound::setVoice(k + 1, o.def, o.rpm, v, pan, bd[k] > 50);
    }
    // rain on the car (quieter inside the cockpit)
    float rv = race.rain * (cam == CAM_COCKPIT ? 0.4f : 0.65f);
    sound::loop(0, rv > 0.02f && screen != SCR_PAUSE ? "weather_rain" : nullptr, rv);
    // the crowd in the grandstands and bleachers: louder the nearer the stand (muffled in the cockpit)
    {
        static const Track* stTrack = nullptr; static std::vector<V3> stands;
        if (stTrack != &race.track) {
            stTrack = &race.track; stands.clear();
            for (const PropInst& p : race.track.props) if (p.model == PROP_GRANDSTAND || p.model == PROP_BLEACHER) stands.push_back(p.p);
        }
        float dmin = 1e9f;
        for (const V3& s : stands) { float d = len(s - me.pos); if (d < dmin) dmin = d; }
        float cv = dmin < 260 ? 0.42f / (1 + dmin / 30.0f) * (1 - dmin / 260.0f) : 0;
        if (cam == CAM_COCKPIT) cv *= 0.6f;
        sound::loop(1, cv > 0.01f && screen != SCR_PAUSE ? "crowd_stands" : nullptr, cv);
    }
}

// pit sounds, thunder and the engineer's radio
void Game::raceEvents() {
    for (int e : race.sfxEvents) if (e >= 0 && e < SFX_COUNT) {
        float v = e >= SFX_THUNDER_CLOSE ? 1.0f : 0.8f;
        sound::play(SFX_FILE[e], v, e >= SFX_THUNDER_CLOSE ? ((rand() % 100) - 50) / 100.0f : 0.3f);
    }
    race.sfxEvents.clear();
    // other teams' stops near us: the air jacks, the wheel guns, the drop and the crew's shout as the
    // car goes (our own stop's sounds come from the race, above; the shout is ours too)
    {
        static float lastT = 1e9f;
        static int prevPit[64], prevPh[64]; static float prevTm[64];
        int n = (int)race.cars.size() < 64 ? (int)race.cars.size() : 64;
        if (race.time < lastT) for (int i = 0; i < 64; i++) { prevPit[i] = -1; prevPh[i] = -99; prevTm[i] = 0; }
        lastT = race.time;
        const Car& me = race.player();
        static const float SWAP[ERA_COUNT] = {1.6f, 3.2f, 2.6f, 2.3f};   // (as Race::pitStop)
        for (int i = 0; i < n; i++) {
            const Car& c = race.cars[i];
            V3 rel = c.pos - me.pos;
            float d = len(rel);
            float vol = 0.85f / (1 + d / 8.0f), pan = d > 0.5f ? dot(rel * (1.0f / d), me.left * -1.0f) * 0.8f : 0;
            if (race.session == SES_RACE && d < 60 && prevPit[i] == PIT_STOP && c.pit == PIT_OUT && prevPh[i] >= 0)
                sound::play("pit_crew_go", c.player ? 0.7f : vol, c.player ? 0 : pan);
            if (!c.player && d < 45 && c.pit == PIT_STOP && prevPit[i] == PIT_STOP) {
                if (c.pitPhase == 1 && prevPh[i] != 1) sound::play("pit_airjack_up", vol, pan);
                if (c.pitPhase == 3 && prevPh[i] != 3) sound::play("pit_jack_drop", vol, pan);
                if (c.pitPhase == 1 && prevPh[i] == 1) {
                    float sw = SWAP[c.def->era % ERA_COUNT];
                    for (float g : {0.25f, 0.45f}) if (prevTm[i] < g * sw && c.pitTimer >= g * sw) sound::play("pit_wheelgun", vol, pan);
                }
            }
            prevPit[i] = c.pit; prevPh[i] = c.pitPhase; prevTm[i] = c.pitTimer;
        }
    }
    if (race.radioPending >= 0 && race.radioPending < RADIO_COUNT) {
        if (screen != SCR_REEL && !aiDrivesPlayer) sound::radio(RADIO_FILE[race.radioPending]);
        race.radioPending = -1;
    }
}

// practice / qualifying: the soundtrack comes from the nearest trackside PA speakers
void Game::trackSpeakers() {
    const Car& me = race.player();
    V3 ear = cam == CAM_COCKPIT ? me.pos + V3(0, 0, 1) : R.camPos;
    V3 right = me.left * -1.0f;
    const auto& sp = race.track.speakers;
    int best[4] = {-1, -1, -1, -1}; float bd[4] = {1e9f, 1e9f, 1e9f, 1e9f};
    for (int i = 0; i < (int)sp.size(); i++) {
        float d = len(sp[i] - ear);
        for (int k = 0; k < 4; k++) if (d < bd[k]) {
            for (int m = 3; m > k; m--) { bd[m] = bd[m - 1]; best[m] = best[m - 1]; }
            bd[k] = d; best[k] = i; break;
        }
    }
    bool inside = cam == CAM_COCKPIT;
    for (int k = 0; k < 4; k++) {
        if (best[k] < 0) { sound::setSpeaker(k, 0, 0, 0.01f, 0); continue; }
        float d = bd[k];
        float gain = 1.3f / (1 + d / 22.0f) * (inside ? 0.55f : 1.0f);
        if (d > 450) gain = 0;
        float pan = dot(norm(sp[best[k]] - ear), right);
        float dull = clampf(d / 300.0f, 0, 1) * 0.8f + (inside ? 0.35f : 0);
        sound::setSpeaker(k, gain, pan, d / 343.0f, dull);
    }
}

// ------------------------------------------------------------------ the race itself
void Game::updateRace(float dt, const Pad& p) {
    Car& me = race.player();
    bool manage = me.plan.aiDrives && !aiDrivesPlayer;
    if (pressed(p, &Pad::start) || tapIn(p, SCREEN_W - 130, 0, SCREEN_W, 50)) { screen = SCR_PAUSE; sel = 0; raceAudio(); return; }
    if (tapIn(p, 0, SCREEN_H - 90, 92, SCREEN_H) && !race.qte.on) race.cycleMap();      // tap the track map
    if (manage) {
        // managing: the team's driver races; Triangle changes the speed of time
        if (pressed(p, &Pad::triangle)) {
            int k = 0; for (int i = 0; i < 5; i++) if (ACCEL[i] == (int)race.timeAccel) k = i;
            race.timeAccel = (float)ACCEL[(k + 1) % 5];
        }
        if (pressed(p, &Pad::circle)) wallView = !wallView;      // pit wall <-> TV pictures
    } else {
        race.timeAccel = 1;
        if (race.qte.on) {
            // manual pit stop: the face buttons answer the prompts
            if (pressed(p, &Pad::cross)) race.qtePress(QB_CROSS);
            if (pressed(p, &Pad::circle)) race.qtePress(QB_CIRCLE);
            if (pressed(p, &Pad::square)) race.qtePress(QB_SQUARE);
            if (pressed(p, &Pad::triangle)) race.qtePress(QB_TRIANGLE);
            // touch: the four button pads under the prompt (see Renderer::drawQte)
            for (int b = 0; b < 4; b++)
                if (input::hasTouch() && tapIn(p, SCREEN_W / 2 - 130 + b * 65, 240, SCREEN_W / 2 - 130 + b * 65 + 60, 272)) race.qtePress(b);
        } else if (bindPressed(p, ACT_CAMERA)) cam = (cam + 1) % CAM_COUNT;
    }
    if (bindPressed(p, ACT_MAP) && !manage && !race.qte.on) race.cycleMap();
    if (bindPressed(p, ACT_DRS) && !manage && !race.qte.on) race.drsPress = true;
    // pit request: box this lap (press again to cancel)
    if (bindPressed(p, ACT_PIT) && me.pit == PIT_NONE && !me.finished) {
        me.wantPit = !me.wantPit;
        race.message(me.wantPit ? (race.session == SES_RACE ? "Box this lap - copy" : "Come in - to the garage") : "Stay out", RGBA(255, 220, 60), me.wantPit ? RADIO_BOX_ACK : -1);
    }
    // steering: analogue stick, or the d-pad with a little smoothing
    float target = 0;
    bool digital = false;
    if (!p.right && !p.left) padDir = 0;
    // the stick: a small dead zone, full lock just before the edge (small sticks rarely reach it),
    // and a curve so small moves turn gently
    const float DZ = 0.12f, FULL = 0.90f;
    if (fabsf(p.stickX) > DZ) {
        static const float EXPO[3] = {1.0f, 1.6f, 2.2f};
        float m = clampf((fabsf(p.stickX) - DZ) / (FULL - DZ), 0, 1);
        target = (p.stickX > 0 ? 1.0f : -1.0f) * powf(m, 2.0f);       // (less twitchy: a softer centre)
        (void)EXPO;
    }
    else if (p.right || p.left) {
        // the d-pad: holding it winds the wheel on gradually (a tap is a small correction), and at
        // speed it stops near the grip limit instead of slamming on full lock - gentle with no help
        int dir = (p.right ? 1 : 0) - (p.left ? 1 : 0);
        if (dir != padDir) { padDir = dir; padHeld = 0; }
        padHeld += dt;
        float reach = 1.0f;
        if (raceLoaded) {
            const Car& me = race.player();
            float lat = race.playerLatMax > 0 ? race.playerLatMax : 30.0f;
            reach = fmaxf(race.steerFor(me.v, lat * (cfg.help() == 0 ? 1.12f : 1.0f)), 0.12f);
        }
        float wind = clampf(0.35f + padHeld * 1.6f, 0, 1);              // 35 % at once, all of it after 0.4 s
        target = dir * reach * wind;
        digital = true;
    }
    else if (cfg.tilt && p.hasTilt) {
        // tilt steering: turn the console like a wheel; the angle when driving began is straight ahead
        if (!tiltCal) { tiltBase = p.tilt; tiltCal = true; }
        static const float RANGE[4] = {1, 0.62f, 0.44f, 0.30f};       // radians for full lock
        float a = (p.tilt - tiltBase) * (cfg.tiltInvert ? -1.0f : 1.0f);
        a = fabsf(a) < 0.03f ? 0 : a - (a > 0 ? 0.03f : -0.03f);
        target = clampf(a / RANGE[cfg.tilt], -1, 1);
    }
    // how fast the wheel follows the input: quick at low speed, calmer as the speed builds (an F1 car
    // at 300 km/h needs only a touch of lock), always quicker back to the centre
    float vk = raceLoaded ? clampf(race.player().v / 85.0f, 0, 1) : 0;
    float rate = (fabsf(target) > fabsf(steer) && target * steer >= 0) ? 4.2f - 2.0f * vk : 6.5f - 2.0f * vk;
    if (digital && fabsf(target) > fabsf(steer) && target * steer >= 0) rate = 3.0f - 1.6f * vk;    // the d-pad winds on more slowly
    steer += clampf(target - steer, -rate * dt, rate * dt);
    // R accelerates and L brakes as well as the bound buttons (only while driving)
    float thr = held(p, ACT_ACCEL) || p.r ? 1.0f : (p.stickY < -0.4f ? -p.stickY : 0);
    float brk = held(p, ACT_BRAKE) || p.l ? 1.0f : (p.stickY > 0.4f ? p.stickY : 0);
    // PS Vita right stick: analogue throttle (up) and brake (down), from a light touch to full
    if (p.rstickY < 0) thr = fmaxf(thr, clampf((-p.rstickY - 0.10f) / 0.85f, 0, 1));
    if (p.rstickY > 0) brk = fmaxf(brk, clampf((p.rstickY - 0.10f) / 0.85f, 0, 1));
    int shift = (bindPressed(p, ACT_GEAR_UP) ? 1 : 0) - (bindPressed(p, ACT_GEAR_DOWN) ? 1 : 0);
    race.steerHelp = cfg.help();
    if (race.qte.on) { thr = 0; brk = 0; shift = 0; }
    // time: 60 steps a second; sped up by taking more (and at 4x+ longer) steps per frame
    int accel = (int)race.timeAccel;
    float STEP = accel >= 4 ? 1.0f / 30.0f : 1.0f / 60.0f;
    accum += dt * accel;
    int guard = 0;
    std::vector<int> events;
    while (accum >= STEP && guard++ < 40) {
        race.spinsOn = cfg.help() < 2;            // steering help Strong: no slides or spins at all
        race.update(STEP, steer, thr, brk, shift, aiDrivesPlayer);
        if (race.newRecord) { race.newRecord = false; char gp[64]; ghostPath(gp, sizeof gp); race.saveGhost(gp); if (race.set.timeTrial) trophies.award(TR_TT_RECORD); }
        events.insert(events.end(), race.sfxEvents.begin(), race.sfxEvents.end());
        shift = 0; accum -= STEP;
        if (race.playerAtBox() || race.inGarage() || race.over) { accum = 0; break; }
    }
    race.sfxEvents = events;
    replayRecord();
    // trophies during the race
    if (race.session == SES_RACE) {
        if (race.time < 2 || gridPos == 0) gridPos = me.position;
        if (race.nightLevel() > 0.25f || TRACKS[race.set.track].floodlit) sawNight = true;

        if (race.qte.on) qteWasOn = true;
        else if (qteWasOn) {
            // a manual stop just ended
            qteWasOn = false;
            if (race.qte.miss == 0 && race.qte.good == 0 && race.qte.perfect > 0) trophies.award(TR_PERFECT_STOP);
            if (me.pitTotal > 0 && me.pitTotal < 4.0f && me.wear < 0.01f) trophies.award(TR_QUICK_STOP);
        }
    }
    raceEvents();
    raceAudio();
    if (race.playerAtBox()) {
        if (aiDrivesPlayer) race.releasePlayer();
        else { screen = SCR_TEAM; sel = 6; race.timeAccel = 1; sound::silence(); sound::radio("box"); }
    }
    if (race.inGarage()) { screen = SCR_GARAGE; setupSel = 8; race.timeAccel = 1; sound::silence(); }
    if (race.over) finishRace();
}

void Game::checkRaceTrophies() {
    if (race.session != SES_RACE || race.set.timeTrial || !raceLoaded) return;
    const Car& me = race.player();
    if (me.finished) {
        trophies.award(TR_FIRST_RACE);
        int classPos = me.position;
        if (classPos <= 3) trophies.award(TR_PODIUM);
        if (classPos <= (me.def->era == ERA_90S ? 6 : 10)) trophies.award(TR_CLASS_WIN);      // (the points places)
        if (me.position == 1) trophies.award(TR_OVERALL_WIN);
        if (gridPos > 0 && gridPos - me.position >= 5) trophies.award(TR_COMEBACK);
        bool long20 = race.set.laps >= 10 || race.set.raceMin >= 20;
        if (long20 && me.penalty <= 0 && !me.driveThrough && me.damage() < 0.1f) trophies.award(TR_CLEAN);
        if (classPos == 1 && race.rain > 0.3f) trophies.award(TR_RAIN_WIN);
        if (sawNight) trophies.award(TR_NIGHT);
        const TrackDef& td = TRACKS[race.set.track];
        if (race.set.raceMin <= 0 && race.set.laps >= td.gpLaps) trophies.award(TR_DOUBLE_STINT);      // the full distance
        bool monaco = !strcmp(td.id, "monaco") && race.set.raceMin <= 0 && race.set.laps >= 10;
        if (monaco) trophies.award(TR_LE_MANS);
        if (monaco && classPos == 1) trophies.award(TR_LE_MANS_WIN);
    }
    gridPos = 0; sawNight = false; qteWasOn = false;
}

void Game::finishRace() {
    checkRaceTrophies();
    screen = SCR_RESULTS; sel = 0; t = 0;
    sound::silence(); sound::stopEffects(); sound::loop(0, nullptr, 0);
    if (race.session == SES_RACE && careerMode && career.active) {
        career.afterRace(race, set.weekend ? qualiGrid : std::vector<int>());
        career.save("career.sav");
    }
    if (race.session == SES_RACE && champMode && champ.active) {
        champ.award(race, set.weekend ? qualiGrid : std::vector<int>());   // pole point only after a qualifying session
        champ.save("champ.sav");
        if (champ.over()) {
            int order[48]; int n = champ.standings(race.player().def->cls, order, 48);
            if (n > 0 && champ.e[order[0]].number == race.player().number) trophies.award(TR_CHAMPION);
        }
    }
}

int Game::tapRow(const Pad& p, float x0, float x1, float y0, float step, int n) const {
    if (!(p.touches > 0 && prev.touches == 0)) return -1;
    if (p.tx[0] < x0 || p.tx[0] >= x1) return -1;
    int r = (int)floorf((p.ty[0] - y0) / step);
    return r >= 0 && r < n ? r : -1;
}

void Game::update(float dt, const Pad& p) {
    t += dt;
    if (screen != SCR_RACE) tiltCal = false;          // re-centre tilt steering whenever driving starts again
    noteT -= dt;
    bool any = p.cross || p.circle || p.square || p.triangle || p.start || p.select || p.up || p.down || p.left || p.right || p.l || p.r;
    idle = any ? 0 : idle + dt;
    int dy = (pressed(p, &Pad::down) ? 1 : 0) - (pressed(p, &Pad::up) ? 1 : 0);
    int dx = (pressed(p, &Pad::right) ? 1 : 0) - (pressed(p, &Pad::left) ? 1 : 0);
    bool ok = pressed(p, &Pad::cross), back = pressed(p, &Pad::circle);
    switch (screen) {
    case SCR_INTRO:
        introUpdate(dt, any && t > 0.6f && !(prev.cross || prev.circle || prev.start));
        break;
    case SCR_TITLE:
        if (dy) {
            // TIME TRIAL (id 6) is shown second; the other items keep their ids
            int pos = 0; for (int i = 0; i < 8; i++) if (TITLE_ORDER[i] == sel) pos = i;
            sel = TITLE_ORDER[(pos + dy + 8) % 8];
        }
        if (idle > cfg.attract && !aiDrivesPlayer) { idle = 0; introBegin(1); break; }
        if (ok || pressed(p, &Pad::start)) {
            t = 0;
            ttMode = sel == 6;
            if (sel == 0 || sel == 6) { champMode = false; careerMode = false; screen = SCR_TRACK; sel = set.track; }
            else if (sel == 1) {
                champMode = true;
                if (champ.load("champ.sav")) { screen = SCR_CHAMP; sel = 0; }
                else { champ.active = false; screen = SCR_CAR; sel = set.car; }
            } else if (sel == 2) {
                champMode = false; careerMode = true; careerPage = 0;
                if (career.load("career.sav")) { career.apply(); careerLivery(); screen = SCR_CAREER; sel = 0; }
                else { screen = SCR_CAREER_NEW; sel = 0; }
            } else if (sel == 3) { screen = SCR_LOAD; sel = 0; }
            else if (sel == 4) { screen = SCR_OPTIONS; optionsFrom = SCR_TITLE; sel = 0; }
            else if (sel == 5) screen = SCR_HELP;
            else if (sel == 7) { screen = SCR_TROPHIES; sel = 0; }
        }
        break;
    case SCR_REEL:
        reelSwitch -= dt;
        if (reelSwitch <= 0) {
            // follow a different car every 12 s: usually one in a fight for position
            reelSwitch = 12;
            int best = rand() % (int)race.cars.size(); float gap = 1e9f;
            for (int k = 0; k < 6; k++) {
                int i = rand() % (int)race.cars.size();
                for (auto& o : race.cars) {
                    float g = o.total - race.cars[i].total;
                    if (&o != &race.cars[i] && g > 0 && g < gap) { gap = g; best = i; }
                }
            }
            reelFocus = best;
        }
        accum += dt;
        while (accum >= 1.0f / 60) { race.update(1.0f / 60, 0, 0, 0, 0, true); if (race.playerAtBox()) race.releasePlayer(); accum -= 1.0f / 60; }
        race.sfxEvents.clear(); race.radioPending = -1;
        if (any && t > 0.5f) { leaveRace(); screen = SCR_TITLE; sel = 0; t = 0; idle = 0; break; }
        if (race.over || t > 150) { startReel(); t = 0; }
        break;
    case SCR_HELP:
        if (ok || back) { screen = SCR_TITLE; sel = 5; t = 0; }
        break;
    case SCR_REPLAY: updateReplay(dt, p); break;
    case SCR_PHOTO: updatePhoto(dt, p); break;
    case SCR_TROPHIES:
        if (ok || back) { screen = SCR_TITLE; sel = 7; t = 0; }
        break;
    case SCR_LOAD:
        sel = (sel + dy + 4) % 4;
        if (back || (ok && sel == 3)) { screen = SCR_TITLE; sel = 3; t = 0; break; }
        if (ok) {
            if (!loadSlot(sel)) { snprintf(note, sizeof note, "Slot %d is empty", sel + 1); noteT = 2; }
        }
        break;
    case SCR_CHAMP:
        sel = (sel + dy + 3) % 3;
        if (back || (ok && sel == 2)) { screen = SCR_TITLE; sel = 1; t = 0; break; }
        if (ok && sel == 1) { champ.active = false; screen = SCR_CAR; sel = set.car; t = 0; break; }
        if (ok && sel == 0) {
            if (champ.over()) { champ.active = false; screen = SCR_CAR; sel = set.car; t = 0; break; }
            set = champ.roundSettings();
            qualiGrid.clear();
            startRace();
        }
        break;
    case SCR_CAREER: case SCR_CAREER_NEW: case SCR_LIVERY:
        updateCareer(dx, dy, ok, back, pressed(p, &Pad::square));
        break;
    case SCR_TRACK:
        // the era's own ten circuits; L / R or left / right changes the era
        if (dx || pressed(p, &Pad::l) || pressed(p, &Pad::r)) { int d = dx ? dx : (pressed(p, &Pad::l) ? -1 : 1); era = (era + d + ERA_COUNT) % ERA_COUNT; t = 0; }
        sel = (sel + dy + ERA_ROUNDS) % ERA_ROUNDS;
        if (ok) {
            set.track = eraTrack(era, sel);
            if (CARS[set.car].era != era) set.car = eraFirstCar(era);
            screen = SCR_CAR; sel = set.car; t = 0;
        }
        if (back) { screen = SCR_TITLE; sel = ttMode ? 6 : 0; t = 0; }
        break;
    case SCR_CAR: {
        // the era's ten teams (sel is the CARS index); in a championship left / right changes the era
        int e = CARS[sel % CAR_PICK].era, k = sel % TEAMS_PER_ERA;
        if (champMode && (dx || pressed(p, &Pad::l) || pressed(p, &Pad::r))) { int d = dx ? dx : (pressed(p, &Pad::l) ? -1 : 1); e = (e + d + ERA_COUNT) % ERA_COUNT; t = 0; }
        k = (k + dy + TEAMS_PER_ERA) % TEAMS_PER_ERA;
        sel = eraFirstCar(e) + k; era = e;
        if (dy) t = 0;
        if (ok && ttMode) { set.car = sel; t = 0; startSession(SES_PRACTICE); break; }   // time trial: straight to the garage
        if (ok) {
            set.car = sel; screen = SCR_SETTINGS; sel = 13; t = 0; setupPage = false;
            if (setups.get(set.track, CARS[set.car].era, set.setup)) { snprintf(note, sizeof note, "Your %s setup is loaded", TRACKS[set.track].name); noteT = 2.5f; }
        }
        if (back) {
            if (champMode) { screen = SCR_TITLE; sel = 1; }
            else { screen = SCR_TRACK; sel = 0; for (int k2 = 0; k2 < ERA_ROUNDS; k2++) if (eraTrack(era, k2) == set.track) sel = k2; }
            t = 0;
        }
        break;
    }
    case SCR_SETTINGS: {
        if (setupPage) {
            // the diagnostics computer: car setup
            if (updateSetup(dx, dy, ok, back, false)) { setupPage = false; sel = 14; }
            break;
        }
        const int ROWS = 15;
        sel = (sel + dy + ROWS) % ROWS;
        if (ok && sel == 14) { setupPage = true; setupSel = 0; break; }
        auto cyc = [&](int v, int n) { return (v + dx + n) % n; };
        int li = 0, oi = 0, ei = 0;
        for (int i = 0; i < LENGTH_N; i++) if ((set.raceMin > 0 && LENGTHS[i] == -set.raceMin) || (set.raceMin <= 0 && LENGTHS[i] == set.laps)) li = i;
        for (int i = 0; i < 4; i++) if (OPP[i] == set.opponents) oi = i;
        for (int i = 0; i < 9; i++) if (EVENTH[i] == set.eventHours) ei = i;
        if (dx) {
            if (sel == 0) { int v = LENGTHS[cyc(li, LENGTH_N)]; if (v > 0) { set.laps = v; set.raceMin = 0; } else set.raceMin = -v; }
            if (sel == 1) { int e = (CARS[set.car].era + dx + ERA_COUNT) % ERA_COUNT; set.car = eraFirstCar(e) + (set.car % TEAMS_PER_ERA); era = e; }
            if (sel == 2) set.opponents = OPP[cyc(oi, 4)];
            if (sel == 3) set.difficulty = cyc(set.difficulty, 3);
            if (sel == 4) set.startPos = cyc(set.startPos, 3);
            if (sel == 5) set.startType = set.startType == START_ROLLING ? START_STANDING : START_ROLLING;
            if (sel == 6) set.weather = cyc(set.weather, 4);
            if (sel == 7) set.manualPits = !set.manualPits;
            if (sel == 8) set.manualGears = !set.manualGears;
            if (sel == 9) cam = cyc(cam, CAM_COUNT);
            if (sel == 10) set.weekend = !set.weekend;
            if (sel == 11) { static const int M[4] = {3, 5, 10, 20}; int mi = 0; for (int i = 0; i < 4; i++) if (M[i] == set.sessionMin) mi = i; set.sessionMin = M[cyc(mi, 4)]; }
        }
        if (ok && sel == 13) {
            if (champMode && !champ.active) {
                // a new season with these settings (lap races)
                set.raceMin = 0;
                champ.begin(set); champ.save("champ.sav");
                screen = SCR_CHAMP; sel = 0; t = 0;
            } else startRace();
        }
        if (ok && sel == 12) { screen = SCR_CAR; sel = set.car; }
        if (back) { screen = SCR_CAR; sel = set.car; t = 0; }
        break;
    }
    case SCR_RACE:
        updateRace(dt, p);
        break;
    case SCR_GARAGE: {
        // practice / qualifying: the car is in its garage; the session carries on outside
        {
            int accel = (int)race.timeAccel;
            float STEP = accel >= 4 ? 1.0f / 30 : 1.0f / 60;
            accum += dt * accel;
            int guard = 0;
            while (accum >= STEP && guard++ < 40) { race.update(STEP, 0, 0, 0, 0, false); accum -= STEP; }
        }
        race.sfxEvents.clear();
        sound::silence();
        if (pressed(p, &Pad::start)) { screen = SCR_PAUSE; sel = 0; break; }
        int act = updateSetup(dx, dy, ok, back, true);
        if (act == 1) { race.leaveGarage(set.setup); race.timeAccel = 1; screen = SCR_RACE; R.camInit = false; }
        if (act == 2) { race.flyingLap(set.setup); race.timeAccel = 1; screen = SCR_RACE; R.camInit = false; }
        if (act == 3) { race.timeAccel = 1; race.endSession(); }
        if (race.over) finishRace();
        break;
    }
    case SCR_TEAM: {
        // the pit-stop team screen: the race waits while the plan is made
        Car& me = race.player();
        PitPlan& pl = me.plan;
        // touch: tap a row to pick it; tap the picked row's left / right half to change it, or to do it
        int tr = tapRow(p, 12, 254, 48, 22, 7);
        if (tr >= 0) { if (tr != sel) { sel = tr; dy = 0; } else if (tr <= 4) dx = p.tx[0] < 133 ? -1 : 1; else ok = true; }
        sel = (sel + dy + 7) % 7;
        if (dx) {
            if (sel == 0) { int v = pl.tyres ? pl.tyre + 1 : 0; v = (v + dx + TY_COUNT + 1) % (TY_COUNT + 1); pl.tyres = v > 0; if (v) pl.tyre = v - 1; }
            if (sel == 1 && race.ep().refuel) pl.fuel = clampf(pl.fuel + dx * 0.1f, fmaxf(me.fuel, 0.1f), 1.0f);
            if (sel == 2) pl.repair = !pl.repair;
            if (sel == 3) pl.repair = !pl.repair;   // (Formula 1: one driver per car; a new nose is part of the repairs)
            if (sel == 4) saveSlot = (saveSlot + dx + 3) % 3;
        }
        if (ok && sel == 4) {
            char pth[32]; slotPath(pth, saveSlot, "sav");
            bool okSave = race.save(pth);
            if (champMode && champ.active) { slotPath(pth, saveSlot, "chp"); champ.save(pth); }
            snprintf(note, sizeof note, okSave ? "Saved to slot %d" : "Could not save to slot %d", saveSlot + 1); noteT = 2.5f;
        }
        if (ok && sel == 6) { leaveRace(); screen = SCR_TITLE; sel = 0; t = 0; break; }
        if ((ok && sel == 5) || pressed(p, &Pad::start)) {
            race.releasePlayer();
            if (!pl.aiDrives) race.timeAccel = 1;
            screen = SCR_RACE;
        }
        break;
    }
    case SCR_PAUSE: {
        // shown in PAUSE_ORDER; sel holds the item id (7 replay, 8 photo mode)
        int pos = 0; for (int i = 0; i < 9; i++) if (PAUSE_ORDER[i] == sel) pos = i;
        int tr = tapRow(p, SCREEN_W / 2 - 100, SCREEN_W / 2 + 100, 80, 16, 9);
        if (tr >= 0) { if (tr != pos) { pos = tr; dy = 0; } else if (PAUSE_ORDER[tr] == 2) dx = p.tx[0] < SCREEN_W / 2 ? -1 : 1; else ok = true; }
        sel = PAUSE_ORDER[(pos + dy + 9) % 9];
        if (ok && sel == 7) { replayStart(); break; }
        if (ok && sel == 8) { phFrom = SCR_PAUSE; phYaw = 2.4f; phPitch = 0.25f; phDist = 7; phFov = 50; phHelp = true; screen = SCR_PHOTO; break; }
        if (dx && sel == 2) cam = (cam + dx + CAM_COUNT) % CAM_COUNT;
        if (pressed(p, &Pad::start) || (ok && sel == 0)) { screen = SCR_RACE; }
        else if (ok && sel == 1) startSession(race.session);
        else if (ok && sel == 3) { screen = SCR_OPTIONS; optionsFrom = SCR_PAUSE; sel = 0; }
        else if (ok && sel == 4 && race.session != SES_RACE) { race.toGarage(); screen = SCR_GARAGE; setupPage = true; setupSel = 0; sound::silence(); }
        else if (ok && sel == 5 && race.session != SES_RACE) { race.endSession(); screen = SCR_RESULTS; sel = 0; t = 0; sound::silence(); }
        else if (ok && sel == 6) { screen = SCR_TITLE; sel = 0; t = 0; leaveRace(); }
        break;
    }
    case SCR_OPTIONS:
    case SCR_CONTROLS:
        updateOptions(p, dx, dy, ok, back);
        break;
    case SCR_RESULTS:
        if (ok || pressed(p, &Pad::start)) {
            if (ttMode) { leaveRace(); screen = SCR_TITLE; sel = 6; t = 0; break; }
            if (race.session != SES_RACE) weekendRubber = race.rubber;
            if (race.session == SES_PRACTICE) { startSession(SES_QUALI); break; }
            if (race.session == SES_QUALI) { qualiGrid = race.qualifyingOrder(); startSession(SES_RACE); break; }
            // the podium: the top three of the player's class
            screen = SCR_PODIUM; t = 0; bits.clear();
            sound::musicPodium(true);
            sound::loop(1, "podium_crowd", 0.5f);
        }
        break;
    case SCR_PODIUM: {
        // champagne from the top three, confetti from above
        if (t > 2.5f && t < 9) {
            static bool popped = false;
            if (!popped) { sound::play("podium_champagne", 0.9f, 0); popped = true; }
            if (t > 8.9f) popped = false;
            for (int k = 0; k < 3; k++) for (int j = 0; j < 3; j++) {
                float dir = k == 1 ? 1.0f : (k == 2 ? -1.0f : (j & 1 ? 1.0f : -1.0f));
                Bit b{podiumScr[k][0], podiumScr[k][1], dir * (float)(40 + rand() % 90), -(float)(120 + rand() % 120), 1.6f, RGBA(255, 245, 200, 200), 0};
                bits.push_back(b);
            }
        }
        for (int j = 0; j < 3; j++) {
            static const u32 CC[5] = {RGBA(255, 80, 80), RGBA(255, 220, 60), RGBA(80, 200, 255), RGBA(120, 255, 120), RGBA(255, 140, 220)};
            Bit b{(float)(rand() % SCREEN_W), -6, (float)(rand() % 40 - 20), (float)(30 + rand() % 50), 9, CC[rand() % 5], 1};
            bits.push_back(b);
        }
        for (auto& b : bits) {
            b.life -= dt; b.x += b.vx * dt; b.y += b.vy * dt;
            if (b.kind == 0) b.vy += 260 * dt; else b.vx += sinf(t * 3 + b.y * 0.05f) * 30 * dt;
        }
        bits.erase(std::remove_if(bits.begin(), bits.end(), [](const Bit& b) { return b.life <= 0 || b.y > SCREEN_H + 10; }), bits.end());
        if (bits.size() > 700) bits.erase(bits.begin(), bits.begin() + 100);
        if ((ok || pressed(p, &Pad::start)) && t > 1.0f) {
            sound::loop(1, nullptr, 0);
            sound::musicPodium(false);
            qualiGrid.clear();
            leaveRace();
            if (careerMode && career.active) { screen = SCR_CAREER; careerPage = 0; sel = 0; }
            else if (champMode && champ.active) { screen = SCR_CHAMP; sel = 0; }
            else { screen = SCR_TITLE; sel = 0; }
            t = 0;
        }
        break;
    }
    }
}

// ------------------------------------------------------------------ drawing
void Game::drawMenuBack(const char* title) {
    gfx::setDepthTest(false); gfx::setBlend(true);
    gfx::drawFill2D(0, 0, SCREEN_W, 34, RGBA(0, 0, 0, 150));
    gfx::drawFill2D(0, 34, SCREEN_W, 2, ACC);
    R.font.draw(14, 6, title, WHITE, 1.4f);
    R.font.drawRight(SCREEN_W - 12, 12, "FORMULA LEGENDS", DIM, 0.75f);
    gfx::drawFill2D(0, SCREEN_H - 20, SCREEN_W, 20, RGBA(0, 0, 0, 150));
}

void Game::drawRaceView() {
    const Car& me = race.player();
    bool manage = (me.plan.aiDrives && !aiDrivesPlayer && screen != SCR_RESULTS) || screen == SCR_REEL;
    gfx::beginFrame(R.skyColour(race));
    Camera c;
    int mode = manage ? CAM_TV : cam;
    bool wall = manage && wallView && screen != SCR_REEL;
    // pit stops and the garage moves: the cinematic camera (Settings)
    bool cine = !manage && screen != SCR_REEL && screen != SCR_RESULTS && cfg.pitCam == 0 && me.pit == PIT_STOP && !aiDrivesPlayer;
    if (cine) { mode = CAM_TV + 1; c = R.cinematicCamera(race, me, race.time); }
    else if (wall) c = pitWallCamera();
    else if (mode == CAM_TV) c = R.tvCamera(race, screen == SCR_REEL ? reelFocus : race.playerIdx, 1.0f / 60);
    else c = R.camera(race, mode, screen == SCR_RACE ? 1.0f / 60 : 0, (held(prev, ACT_LOOK_BACK) || (cfg.rearLook && prev.backTouch)) && screen == SCR_RACE && !race.qte.on);
    if (screen == SCR_REPLAY || screen == SCR_PHOTO) { c = viewCam; mode = viewMode; cine = false; wall = false; }
    bool cockpit = mode == CAM_COCKPIT;
    R.drawScene(race, c, cockpit);
    R.drawMarshalFlags(race, c);
    // in the car the cabin's night darkness goes over the whole screen, so the outside gets less here
    R.drawNight(race, cockpit ? R.cabinDark(race) : 0);
    if (!cockpit) {
        // lights after the darkness, so they glow
        M4 proj = M4::perspective(c.fov, (float)SCREEN_W / SCREEN_H, 0.8f, 1600.0f), view = M4::lookDir(V3(0, 0, 0), c.fwd, c.up);
        gfx::setProjection(proj); gfx::setView(view);
        gfx::setFog(false, 0, 1, 0);
        R.drawBeam(race, c);
        R.drawEffects(race, c);
    }
    if (wall && !cine) drawPitWall(c);
    if (cockpit) {
        R.drawRain(race, true);                // behind the cabin: rain only outside the car
        R.drawCockpit(race, c);
        if (race.nightLevel() > 0.05f || race.rain > 0.3f) {
            // the headlight beam and the other cars' lamps after all the darkness, so they glow;
            // the cabin's depth (near 0.05) hides them behind the bodywork and the dash
            M4 proj = M4::perspective(c.fov, (float)SCREEN_W / SCREEN_H, 0.05f, 1600.0f), view = M4::lookDir(V3(0, 0, 0), c.fwd, c.up);
            gfx::setProjection(proj); gfx::setView(view); clip::setMatrices(proj, view);
            gfx::setFog(false, 0, 1, 0);
            gfx::setDepthTest(true);
            R.drawBeam(race, c);
            R.drawEffects(race, c, false);
        }
    }
    R.drawWeather(race, cockpit);
    // PS Vita: rear-view mirror strip at the top (a second, lighter view of the scene); off while the
    // start lights show, in the pit-stop cinematics, and when automatic quality has had to step in
    if (input::hasTouch() && cfg.mirror && autoLevel < 2 && !manage && !cine && !wall && (screen == SCR_RACE || screen == SCR_PAUSE) && race.countdown <= 0 && race.time > 1.5f) {
        const float MX = SCREEN_W / 2 - 72, MY = 4, MW = 144, MH = 32;
        if (gfx::beginInset(MX, MY, MW, MH, R.skyColour(race))) {
            Camera m;
            float half = me.def->lengthM * 0.5f;
            m.eye = me.pos + me.up * 1.25f - me.fwd * (half - 0.4f);
            m.fwd = norm(me.fwd * -1.0f - me.up * 0.06f);
            m.up = me.up; m.fov = 17;
            Perf keep = perf;
            perf.distance *= 0.22f; perf.lodBias *= 0.5f; perf.scenery = false; perf.effects = false;
            R.sceneAspect = MW / MH; R.sceneMirrorX = true; R.sceneLite = true;
            R.drawScene(race, m, true);
            R.sceneMirrorX = false; R.sceneLite = false;
            perf = keep;
            // the night (and the gloom of rain) over the mirror too, then the lamps of the cars behind
            float dark = clampf(race.nightLevel() * 0.72f + race.rain * 0.18f, 0, 0.95f);
            if (dark > 0.02f) {
                gfx::setDepthTest(false); gfx::setBlend(true); gfx::bindTexture(nullptr);
                gfx::drawFill2D(MX, MY, MW, MH, RGBA(4, 6, 16, (int)(dark * 255)));
                gfx::setDepthTest(true); gfx::setBlend(false);
                M4 proj = M4::perspective(m.fov, MW / MH, 0.8f, 1600.0f), view = M4::lookDir(V3(0, 0, 0), m.fwd, m.up);
                proj.m[0] = -proj.m[0];
                gfx::setProjection(proj); gfx::setView(view); clip::setMatrices(proj, view);
                gfx::setFog(false, 0, 1, 0);
                R.drawEffects(race, m, false);
            }
            R.sceneAspect = (float)SCREEN_W / SCREEN_H;
            gfx::endInset();
            gfx::setDepthTest(false); gfx::setBlend(true); gfx::setFog(false, 0, 1, 0);
            u32 fr = RGBA(15, 15, 18, 255);
            gfx::drawFill2D(MX - 2, MY - 2, MW + 4, 2, fr); gfx::drawFill2D(MX - 2, MY + MH, MW + 4, 2, fr);
            gfx::drawFill2D(MX - 2, MY, 2, MH, fr); gfx::drawFill2D(MX + MW, MY, 2, MH, fr);
            gfx::setBlend(false); gfx::setDepthTest(true);
        }
    }
}

void Game::drawManage(bool wall) {
    // team view: the order, our car's status, controls (on the pit wall the screens show all that)
    gfx::setDepthTest(false); gfx::setBlend(true);
    const Car& me = race.player();
    char b[96];
    if (wall) {
        gfx::drawFill2D(0, SCREEN_H - 18, SCREEN_W, 18, RGBA(0, 0, 0, 150));
        snprintf(b, sizeof b, "TRIANGLE time x%d   O  TV view   %s  %s   START pause", (int)race.timeAccel, BUTTON_NAME[cfg.bind[ACT_PIT]], me.wantPit ? "cancel pit" : "pit (take over)");
        R.font.drawCentre(SCREEN_W / 2, SCREEN_H - 17, b, DIM, 0.68f);
        gfx::setBlend(false);
        return;
    }
    gfx::drawFill2D(SCREEN_W - 150, 72, 144, 150, RGBA(0, 0, 0, 150));
    R.font.draw(SCREEN_W - 144, 74, "TEAM VIEW", ACC, 0.8f);
    std::vector<const Car*> order(race.cars.size());
    for (auto& c : race.cars) order[c.position - 1] = &c;
    int first = me.position - 4; if (first < 0) first = 0;
    if (first > (int)order.size() - 8) first = (int)order.size() - 8;
    if (first < 0) first = 0;
    for (int k = 0; k < 8 && first + k < (int)order.size(); k++) {
        const Car& c = *order[first + k];
        snprintf(b, sizeof b, "%2d #%-3d %s%s", c.position, c.number, c.def->make, c.pit != PIT_NONE ? " PIT" : "");
        R.font.draw(SCREEN_W - 144, 90 + k * 15, b, c.player ? GOLD : (c.def->cls == CLASS_HYPER ? WHITE : RGBA(140, 200, 255)), 0.68f);
    }
    { char nm[24]; Race::driverName(me, me.driver, nm, sizeof nm);
      snprintf(b, sizeof b, "%s %d%%  stint %d:%02d", nm, (int)(me.drv[me.driver].fatigue * 100), (int)me.stint / 3600, ((int)me.stint / 60) % 60); }
    R.font.draw(SCREEN_W - 144, 212 - 2, b, DIM, 0.62f);
    gfx::drawFill2D(0, SCREEN_H - 18, SCREEN_W, 18, RGBA(0, 0, 0, 150));
    snprintf(b, sizeof b, "TRIANGLE time x%d   O  pit wall   %s  %s   START pause", (int)race.timeAccel, BUTTON_NAME[cfg.bind[ACT_PIT]], me.wantPit ? "cancel pit" : "pit (take over)");
    R.font.drawCentre(SCREEN_W / 2, SCREEN_H - 17, b, DIM, 0.72f);
    gfx::setBlend(false);
}

void Game::drawTeam() {
    const Car& me = race.player();
    const PitPlan& pl = me.plan;
    gfx::setDepthTest(false); gfx::setBlend(true);
    gfx::drawFill2D(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, 110));
    drawMenuBack("PIT STOP - TEAM");
    gfx::drawFill2D(8, 42, 250, 206, RGBA(5, 10, 18, 220));
    char v[7][48];
    char nm[24]; Race::driverName(me, pl.nextDriver, nm, sizeof nm);
    snprintf(v[0], 48, "Tyres:  < %s >", pl.tyres ? TYRE_NAME[pl.tyre] : "No change");
    if (race.ep().refuel) snprintf(v[1], 48, "Fuel to:  < %d%% >", (int)(pl.fuel * 100 + 0.5f));
    else snprintf(v[1], 48, "Fuel:  no refuelling (%d%%)", (int)(me.fuel * 100 + 0.5f));
    snprintf(v[2], 48, "Repairs:  < %s >", pl.repair ? "Yes" : "No");
    snprintf(v[3], 48, "Front wing:  < %s >", pl.repair ? "new nose" : "no change"); (void)nm;
    snprintf(v[4], 48, "Save race:  < Slot %d >", saveSlot + 1);
    snprintf(v[5], 48, "RELEASE THE CAR");
    snprintf(v[6], 48, "Retire to menu");
    for (int i = 0; i < 7; i++) {
        float y = 50 + i * 22;
        if (i == sel) gfx::drawFill2D(12, y - 2, 242, 19, RGBA(255, 150, 40, 200));
        R.font.draw(20, y, v[i], i == sel ? RGBA(20, 20, 20) : (i == 5 ? GOLD : WHITE), 0.82f);
    }
    // (Formula 1: one driver per car, no stints to manage)
    // what the car needs
    gfx::drawFill2D(266, 42, 206, 206, RGBA(5, 10, 18, 220));
    char b[64];
    float y = 48;
    snprintf(b, sizeof b, "P%d   lap %d   stop %d", me.position, me.lap + 1, me.stops); R.font.draw(274, y, b, WHITE, 0.8f); y += 18;
    snprintf(b, sizeof b, "Tyres: %s, %d%% left", TYRE_NAME[me.tyre], (int)((1 - me.wear) * 100)); R.font.draw(274, y, b, DIM, 0.72f); y += 15;
    snprintf(b, sizeof b, "Fuel: %d%%", (int)(me.fuel * 100)); R.font.draw(274, y, b, DIM, 0.72f); y += 15;
    snprintf(b, sizeof b, "Damage: %d%%%s%s", (int)(me.damage() * 100), me.noseOff ? " no nose" : "", me.wingOff ? " no wing" : ""); R.font.draw(274, y, b, me.damage() > 0.3f ? RGBA(255, 120, 60) : DIM, 0.72f); y += 15;
    snprintf(b, sizeof b, "Lap %d of %d", me.lap + 1, race.set.laps); R.font.draw(274, y, b, DIM, 0.72f); y += 15;
    y += 15;
    const char* wx = race.rain < 0.05f ? (race.wet > 0.2f ? "Dry now, track still wet" : "Dry") : (race.rain < 0.5f ? "Light rain" : (race.storm ? "Thunderstorm" : "Heavy rain"));
    snprintf(b, sizeof b, "Weather: %s", wx); R.font.draw(274, y, b, RGBA(140, 190, 255), 0.72f); y += 15;
    snprintf(b, sizeof b, "Engineer: %s tyres", TYRE_NAME[race.bestTyreFor(race.wet) == TY_MEDIUM ? TY_MEDIUM : race.bestTyreFor(race.wet)]); R.font.draw(274, y, b, RGBA(140, 190, 255), 0.72f); y += 15;
    float left = race.raceLeft();
    if (left >= 0) {
        int s = (int)(left * race.timeScale);
        snprintf(b, sizeof b, "Race left: %d:%02d   (clock %02d:%02d)", s / 3600, (s / 60) % 60, (int)race.clockHour(), (int)(race.clockHour() * 60) % 60);
        R.font.draw(274, y, b, DIM, 0.68f); y += 15;
    }
    if (me.driveThrough) { R.font.draw(274, y, "Drive-through still to serve!", RGBA(255, 90, 60), 0.72f); y += 15; }
    if (race.set.manualPits && !pl.aiDrives) R.font.draw(274, y, "Manual stop: hit the prompts!", GOLD, 0.68f);
    if (noteT > 0) R.font.drawCentre(SCREEN_W / 2, 230, note, GOLD, 0.8f);
    R.font.drawCentre(SCREEN_W / 2, SCREEN_H - 18, "UP/DOWN choose   LEFT/RIGHT change   X select", DIM, 0.75f);
}


void Game::draw() {
    if (screen == SCR_INTRO) { drawIntro(); return; }
    if (screen == SCR_PODIUM) { if (raceLoaded) drawPodium(); else { gfx::beginFrame(0); finishFrame(); } return; }
    if (screen == SCR_GARAGE) { if (raceLoaded) drawGarageView(); else { gfx::beginFrame(0); finishFrame(); } return; }
    bool pausedOptions = (screen == SCR_OPTIONS || screen == SCR_CONTROLS) && optionsFrom == SCR_PAUSE && raceLoaded;
    if (pausedOptions) { drawRaceView(); drawOptionsFlat(); finishFrame(); return; }
    if (screen == SCR_RACE || screen == SCR_PAUSE || screen == SCR_RESULTS || screen == SCR_TEAM || screen == SCR_REEL || screen == SCR_REPLAY || screen == SCR_PHOTO) {
        if (!raceLoaded) { gfx::beginFrame(0); finishFrame(); return; }
        drawRaceView();
        if (screen == SCR_REPLAY) drawReplayHud();
        if (screen == SCR_PHOTO) drawPhotoHud();
        const Car& me = race.player();
        bool manage = me.plan.aiDrives && !aiDrivesPlayer;
        bool cineNow = cfg.pitCam == 0 && me.pit == PIT_STOP && !aiDrivesPlayer;
        bool wallNow = manage && wallView && !cineNow;
        if (screen == SCR_RACE) {
            if (!wallNow) R.drawHUD(race, manage ? CAM_TV : (cineNow ? CAM_COCKPIT : this->cam));   // the pit wall has its own screens
            if (manage) drawManage(wallNow);
            if (cfg.showFps) drawFps();
        }
        if (screen == SCR_REEL) {
            gfx::setDepthTest(false); gfx::setBlend(true);
            const Car& f = race.cars[reelFocus];
            gfx::drawFill2D(10, SCREEN_H - 44, 230, 34, RGBA(0, 0, 0, 160));
            gfx::drawFill2D(10, SCREEN_H - 44, 4, 34, f.def->colour | 0xFF000000u);
            char b[64]; snprintf(b, sizeof b, "P%d  #%d %s %s", f.position, f.number, f.def->make, f.def->model);
            R.font.draw(20, SCREEN_H - 42, b, WHITE, 0.8f);
            R.font.draw(20, SCREEN_H - 26, f.def->team, DIM, 0.66f);
            R.font.drawRight(SCREEN_W - 12, 10, "HIGHLIGHTS", ACC, 1.0f);
            R.font.drawRight(SCREEN_W - 12, 28, TRACKS[race.set.track].name, DIM, 0.7f);
            if ((int)(t * 1.5f) & 1) R.font.drawCentre(SCREEN_W / 2, 12, "PRESS ANY BUTTON", WHITE, 0.85f);
        }
        if (screen == SCR_TEAM) drawTeam();
        if (screen == SCR_PAUSE) {
            gfx::setDepthTest(false); gfx::setBlend(true);
            gfx::drawFill2D(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, 120));
            gfx::drawFill2D(SCREEN_W / 2 - 100, 56, 200, 174, RGBA(10, 14, 22, 230));
            R.font.drawCentre(SCREEN_W / 2, 62, "PAUSED", WHITE, 1.3f);
            const char* items[9] = {"Resume", "Restart", "", "Settings", "Back to garage", "End session", "Quit to menu", "Replay (last minute)", "Photo mode"};
            char cb[40]; snprintf(cb, sizeof cb, "Camera: < %s >", CAMS[this->cam]);
            items[2] = cb;
            for (int k = 0; k < 9; k++) {
                int i = PAUSE_ORDER[k];
                u32 col = i == sel ? ACC : ((i == 4 || i == 5) && race.session == SES_RACE ? DIM : WHITE);
                R.font.drawCentre(SCREEN_W / 2, 82 + k * 16, items[i], col, 0.86f);
            }
            if (noteT > 0) R.font.drawCentre(SCREEN_W / 2, 236, note, GOLD, 0.7f);
        }
        if (screen == SCR_RESULTS) {
            gfx::setDepthTest(false); gfx::setBlend(true);
            gfx::drawFill2D(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, 150));
            static const char* TITLES[3] = {"PRACTICE RESULTS", "QUALIFYING RESULTS", "RACE RESULTS"};
            const char* const* titles = TITLES;
            static const char* TT_TITLES[3] = {"TIME TRIAL", "TIME TRIAL", "TIME TRIAL"};
            if (race.set.timeTrial) titles = TT_TITLES;
            drawMenuBack(titles[race.session]);
            std::vector<const Car*> order(race.cars.size());
            for (auto& c : race.cars) order[c.position - 1] = &c;
            const Car* lead = order[0];
            int rows = (int)order.size() < 10 ? (int)order.size() : 10;
            int myRow = race.player().position - 1;
            int first = myRow >= rows ? myRow - rows + 1 : 0;
            R.font.draw(16, 42, "POS  No   CAR                      TIME / GAP        BEST", DIM, 0.75f);
            for (int k = 0; k < rows; k++) {
                const Car& c = *order[first + k];
                char b[96], tm[24], best[16];
                formatTime(best, c.bestLap);
                if (race.session != SES_RACE) {
                    if (c.bestLap <= 0) strcpy(tm, "no time");
                    else if (&c == lead) formatTime(tm, c.bestLap);
                    else snprintf(tm, sizeof tm, "+%.3f", c.bestLap - lead->bestLap);
                } else if (&c == lead) formatTime(tm, c.finishTime - race.greenTime);
                else if (c.finished && c.lap == lead->lap) { float g = c.finishTime + c.penalty - lead->finishTime - lead->penalty; snprintf(tm, sizeof tm, "+%.3f", g); }
                else snprintf(tm, sizeof tm, "+%d lap%s", lead->lap - c.lap, lead->lap - c.lap == 1 ? "" : "s");
                u32 col = c.player ? GOLD : WHITE;
                float y = 58 + k * 18;
                snprintf(b, sizeof b, "%d", c.position); R.font.draw(18, y, b, col, 0.9f);
                snprintf(b, sizeof b, "#%d", c.number); R.font.draw(52, y, b, col, 0.9f);
                snprintf(b, sizeof b, "%s %s", c.def->make, c.def->model); R.font.draw(94, y, b, col, 0.9f);
                R.font.draw(268, y, tm, col, 0.9f);
                R.font.draw(380, y, best, col, 0.9f);
                if (c.penalty > 0 && race.session == SES_RACE) { snprintf(b, sizeof b, "+%.0fs", c.penalty); R.font.draw(450, y + 2, b, RGBA(255, 120, 60), 0.65f); }
            }
            R.font.drawCentre(SCREEN_W / 2, SCREEN_H - 18, race.set.timeTrial ? "X  back to the title" : race.session == SES_PRACTICE ? "X  go to qualifying" : (race.session == SES_QUALI ? "X  go to the race (grid set by qualifying)" : "X  podium"), DIM, 0.8f);
        }
        finishFrame();
        return;
    }

    drawMenus(lastDt);
}

void Game::updateOptions(const Pad& p, int dx, int dy, bool ok, bool back) {
    if (screen == SCR_CONTROLS) {
        if (rebinding >= 0) {
            // wait for the buttons to be let go, then take the next one pressed
            int b = firstHeld(p);
            if (!rebindArmed) { if (b < 0) rebindArmed = true; }
            else if (b >= 0) { cfg.rebind(rebinding, b); cfg.save(); rebinding = -1; }
            return;
        }
        int ids[24]; int n = controlRows(ids);
        sel = (sel + dy + n) % n;
        int id = ids[sel];
        if (ok && id < ACT_COUNT) { rebinding = id; rebindArmed = false; }
        else if (pressed(p, &Pad::square) && id < ACT_COUNT) { clearBind(id); cfg.save(); }
        else if (id == CR_HELP && (ok || dx)) { cfg.steerHelp = (cfg.help() + (dx ? dx : 1) + 3) % 3; cfg.save(); }
        else if (id == CR_TILT && (ok || dx)) { cfg.tilt = (cfg.tilt + (dx ? dx : 1) + 4) % 4; cfg.save(); }
        else if (id == CR_TILTDIR && (ok || dx)) { cfg.tiltInvert = !cfg.tiltInvert; cfg.save(); }
        else if (id == CR_REAR && (ok || dx)) { cfg.rearLook = !cfg.rearLook; cfg.save(); }
        else if (ok && id == CR_RESET) { cfg.resetBinds(); cfg.save(); }
        else if (back || (ok && id == CR_BACK)) { screen = SCR_OPTIONS; sel = 15; }
        return;
    }
    int optRows = input::hasTouch() ? 20 : 19;              // 19: rear-view mirror (Vita)
    sel = (sel + dy + optRows) % optRows;
    if ((dx || ok) && sel == 19) { cfg.mirror = !cfg.mirror; cfg.save(); return; }
    if ((dx || ok) && sel == 17) { cfg.hideWheel = !cfg.hideWheel; cfg.save(); return; }
    if ((dx || ok) && sel == 18) {
        if (cfg.hideWheel) { cfg.cockpitHud = !cfg.cockpitHud; cfg.save(); }
        else { snprintf(note, sizeof note, "Hide the steering wheel first"); noteT = 1.5f; }
        return;
    }
    if (dx) {
        auto step = [&](int& v, int lo, int hi) { v += dx; if (v < lo) v = lo; if (v > hi) v = hi; };
        bool custom = false;
        switch (sel) {
            case 0: step(cfg.master, 0, 10); break;
            case 1: step(cfg.music, 0, 10); break;
            case 2: step(cfg.engine, 0, 10); break;
            case 3: step(cfg.effects, 0, 10); break;
            case 4: step(cfg.radio, 0, 10); break;
            case 5: cfg.setPreset((cfg.preset + dx + 3) % 3); break;
            case 6: cfg.vsync = !cfg.vsync; break;
            case 7: cfg.fps ^= 1; custom = true; break;
            case 8: cfg.distance = (cfg.distance + dx + 3) % 3; custom = true; break;
            case 9: cfg.scenery = !cfg.scenery; custom = true; break;
            case 10: cfg.effects3d = !cfg.effects3d; custom = true; break;
            case 11: cfg.showFps = !cfg.showFps; break;
            case 12: cfg.autoQuality = !cfg.autoQuality; if (!cfg.autoQuality) autoLevel = 0; break;
            case 14: cfg.pitCam ^= 1; break;
            case 13: cfg.attract += dx * 5; if (cfg.attract < 5) cfg.attract = 5; if (cfg.attract > 30) cfg.attract = 30; break;
        }
        if (custom) { cfg.preset = 3; autoLevel = 0; }      // the player's own choice: start again from it
        if (sel == 5) autoLevel = 0;
        cfg.apply(autoLevel);
        if (sel == 4) sound::radio("box");          // hear the new level
        if (sel == 3) sound::play("pit_wheelgun", 0.8f, 0);
    }
    if (ok && sel == 15) { screen = SCR_CONTROLS; sel = 0; rebinding = -1; }
    else if (back || (ok && sel == 16)) { cfg.save(); sel = optionsFrom == SCR_TITLE ? 4 : 3; screen = optionsFrom; t = 0; }
}

// ------------------------------------------------------------------ career mode: menus
void Game::careerLivery() {
    R.setLivery(career.carIndex(), career.style, LIVERY_COLOURS[career.colA], LIVERY_COLOURS[career.colB], LIVERY_COLOURS[career.colC]);
}

void Game::updateCareer(int dx, int dy, bool ok, bool back, bool square) {
    auto cyc = [&](int v, int n) { return (v + dx + n) % n; };
    auto saveIt = [&]() { career.save("career.sav"); };
    if (screen == SCR_CAREER_NEW) {
        sel = (sel + dy + 6) % 6;
        if (dx) {
            if (sel == 0) newName = cyc(newName, TEAM_NAME_COUNT);
            if (sel == 1) newCls = (newCls + (dx ? dx : 1) + ERA_COUNT) % ERA_COUNT;
            if (sel == 2) newDiff = cyc(newDiff, 3);
            if (sel == 3) { static const int M[4] = {3, 5, 10, 20}; int mi = 0; for (int i = 0; i < 4; i++) if (M[i] == newMin) mi = i; newMin = M[cyc(mi, 4)]; }
        }
        if (back || (ok && sel == 5)) { screen = SCR_TITLE; sel = 2; t = 0; return; }
        if (ok && sel == 4) {
            career.begin(newName, newCls, newDiff, newMin);
            career.apply(); careerLivery(); saveIt();
            screen = SCR_LIVERY; sel = 0; t = 0;
            snprintf(note, sizeof note, "Your team takes over car #%d of %s", career.skipNum, CARS[career.skipCar].team); noteT = 5;
        }
        return;
    }
    if (screen == SCR_LIVERY) {
        sel = (sel + dy + 6) % 6;
        if (dx) {
            if (sel == 0) career.style = cyc(career.style, LIVERY_STYLES);
            if (sel == 1) career.colA = cyc(career.colA, LIVERY_COLOURS_N);
            if (sel == 2) career.colB = cyc(career.colB, LIVERY_COLOURS_N);
            if (sel == 3) career.colC = cyc(career.colC, LIVERY_COLOURS_N);
            if (sel == 4) {
                // the next free race number
                int n = career.number;
                for (int tries = 0; tries < 99; tries++) {
                    n = (n - 1 + dx + 99) % 99 + 1;
                    bool used = false;
                    for (int i = 0; i < CAR_PICK; i++) if (CARS[i].numbers[0] == n || CARS[i].numbers[1] == n) used = true;
                    if (!used) break;
                }
                career.number = n;
            }
            career.apply(); careerLivery();
        }
        if (back || (ok && sel == 5)) { saveIt(); screen = SCR_CAREER; careerPage = 0; sel = 4; t = 0; }
        return;
    }
    // the team screen
    if (careerPage == 0) {
        sel = (sel + dy + 7) % 7;
        if (back || (ok && sel == 6)) { saveIt(); screen = SCR_TITLE; sel = 2; t = 0; return; }
        if (!ok) return;
        if (sel == 0) {
            if (career.broke()) { remove("career.sav"); career.active = false; screen = SCR_CAREER_NEW; sel = 0; return; }
            if (career.champ.over()) { career.newSeason(); saveIt(); snprintf(note, sizeof note, "Season %d: new contracts on offer", career.season); noteT = 3; return; }
            career.apply();
            set = career.roundSettings();
            qualiGrid.clear();
            startRace();
            return;
        }
        if (sel == 4) { screen = SCR_LIVERY; sel = 0; t = 0; return; }
        careerPage = sel; sel = 0;
        return;
    }
    int rows = careerPage == 1 ? 4 : careerPage == 2 ? 7 : careerPage == 3 ? 5 : 1;
    sel = (sel + dy + rows) % rows;
    if (back || (ok && sel == rows - 1)) { int pg = careerPage; careerPage = 0; sel = pg; return; }
    if (careerPage == 1 && ok) {
        if (career.sign(sel)) { saveIt(); snprintf(note, sizeof note, "Signed: %s", SPONSOR_NAMES[career.sponsor[career.offers[sel].title ? 0 : 1].name % SPONSOR_NAME_COUNT]); }
        else snprintf(note, sizeof note, "That offer has gone");
        noteT = 2.5f;
    }
    if (careerPage == 2 && (ok || square)) {
        int seat = ok ? 0 : 1;
        if (career.hire(sel, seat)) { saveIt(); snprintf(note, sizeof note, "Hired for seat %d", seat + 2); }
        else snprintf(note, sizeof note, "Not enough money for the signing fee");
        noteT = 2.5f;
    }
    if (careerPage == 3 && ok) {
        int cost = career.upgradeCost(sel);
        if (career.buyUpgrade(sel)) { career.apply(); saveIt(); snprintf(note, sizeof note, "%s upgraded to level %d", UPGRADE_NAMES[sel], career.up[sel]); }
        else snprintf(note, sizeof note, cost ? "Not enough money" : "Already at the top level");
        noteT = 2.5f;
    }
}

// ------------------------------------------------------------------ replay (last minute) and photo mode
void Game::replayRecord() {
    if (race.cars.empty()) return;
    int n = (int)race.cars.size();
    if (n != repCars || race.time + 1.0f < repNext - 0.1f) {        // a new race (or the field changed): start again
        repCars = n; rep.assign((size_t)REP_N * n, RepCar{}); repHead = 0; repCount = 0; repNext = race.time;
    }
    if (race.time - repNext > 2.0f) repNext = race.time;            // time sped up: don't fill the minute with copies
    while (race.time >= repNext) {
        RepCar* f = &rep[(size_t)repHead * n];
        for (int i = 0; i < n; i++) {
            const Car& c = race.cars[i];
            f[i] = RepCar{c.s, c.d, c.yaw, c.lift, c.steer, c.wheelSpin, c.v, c.noseOff, c.wingOff, c.body.hOff, c.body.pitch, c.body.roll};
        }
        repHead = (repHead + 1) % REP_N; if (repCount < REP_N) repCount++;
        repNext += 0.1f;
    }
}

void Game::replayStart() {
    if (repCount < 30) { snprintf(note, sizeof note, "Nothing to replay yet - drive a little first"); noteT = 2.5f; return; }
    repLive = race.cars; repLivePlayer = race.playerIdx;
    repT = 0; repPlay = true; repFocus = race.playerIdx; repCam = 0;
    screen = SCR_REPLAY; R.camInit = false;
    sound::silence();
}

void Game::replayApply() {
    int n = repCars;
    float ft = clampf(repT, 0, (float)(repCount - 1));
    int i0 = (int)ft, i1 = i0 + 1 < repCount ? i0 + 1 : i0;
    float a = ft - i0;
    int oldest = (repHead - repCount + REP_N) % REP_N;
    const RepCar* f0 = &rep[(size_t)((oldest + i0) % REP_N) * n];
    const RepCar* f1 = &rep[(size_t)((oldest + i1) % REP_N) * n];
    float L = race.track.length;
    for (int i = 0; i < n && i < (int)race.cars.size(); i++) {
        Car& c = race.cars[i];
        float ds = f1[i].s - f0[i].s;
        if (ds > L * 0.5f) ds -= L; if (ds < -L * 0.5f) ds += L;   // over the line
        float dy = f1[i].yaw - f0[i].yaw;
        if (dy > 3.14159f) dy -= 6.28318f; if (dy < -3.14159f) dy += 6.28318f;
        c.s = race.track.wrap(f0[i].s + ds * a); c.d = f0[i].d + (f1[i].d - f0[i].d) * a;
        c.yaw = f0[i].yaw + dy * a; c.lift = f0[i].lift + (f1[i].lift - f0[i].lift) * a;
        c.steer = f0[i].steer + (f1[i].steer - f0[i].steer) * a; c.wheelSpin = f0[i].spin + (f1[i].spin - f0[i].spin) * a;
        c.v = f0[i].v + (f1[i].v - f0[i].v) * a;
        c.noseOff = f0[i].nose; c.wingOff = f0[i].wing;
        c.body.hOff = f0[i].hOff + (f1[i].hOff - f0[i].hOff) * a;
        c.body.pitch = f0[i].pitch + (f1[i].pitch - f0[i].pitch) * a; c.body.roll = f0[i].roll + (f1[i].roll - f0[i].roll) * a;
        race.pose(c);
    }
}

void Game::replayEnd() {
    race.cars = repLive; race.playerIdx = repLivePlayer;
    for (Car& c : race.cars) race.pose(c);
    repLive.clear();
    screen = SCR_PAUSE; sel = 7; R.camInit = false;
}

void Game::updateReplay(float dt, const Pad& p) {
    if (pressed(p, &Pad::circle) || pressed(p, &Pad::start)) { replayEnd(); return; }
    if (pressed(p, &Pad::cross)) repPlay = !repPlay;
    if (pressed(p, &Pad::triangle)) repCam = (repCam + 1) % 4;           // TV, chase, far, cockpit
    int n = repCars;
    if (pressed(p, &Pad::right)) repFocus = (repFocus + 1) % n;
    if (pressed(p, &Pad::left)) repFocus = (repFocus + n - 1) % n;
    if (pressed(p, &Pad::square)) { phFrom = SCR_REPLAY; phYaw = 2.4f; phPitch = 0.25f; phDist = 7; phFov = 50; phHelp = true; screen = SCR_PHOTO; return; }
    // touch: tap the left / right of the timeline bar to jump, anywhere else to pause / play
    if (p.touches > 0 && prev.touches == 0) {
        if (p.ty[0] > SCREEN_H - 30) repT = clampf((p.tx[0] - 20) / (SCREEN_W - 40), 0, 1) * (repCount - 1);
        else repPlay = !repPlay;
    }
    float speed = p.r ? 4.0f : (p.l ? -2.0f : (repPlay ? 1.0f : 0.0f));
    repT += dt * 10 * speed;
    if (repT >= repCount - 1) { repT = (float)(repCount - 1); if (!p.l) repPlay = false; }
    if (repT < 0) repT = 0;
    race.playerIdx = repFocus;
    replayApply();
    static const int MODES[4] = {CAM_TV, CAM_CHASE, CAM_FAR, CAM_COCKPIT};
    viewMode = MODES[repCam];
    float cdt = speed != 0 ? 1.0f / 60 : 0;
    viewCam = viewMode == CAM_TV ? R.tvCamera(race, repFocus, cdt) : R.camera(race, viewMode, cdt, false);
}

void Game::updatePhoto(float dt, const Pad& p) {
    if (pressed(p, &Pad::circle) || pressed(p, &Pad::start)) { screen = phFrom; if (phFrom == SCR_PAUSE) sel = 8; R.camInit = false; return; }
    if (pressed(p, &Pad::cross) || pressed(p, &Pad::triangle) || (p.touches > 0 && prev.touches == 0)) phHelp = !phHelp;
    // left stick / D-pad: around and up / down; right stick or L / R: closer / further; SQUARE + up/down: zoom lens
    float sx = p.stickX + (p.right ? 1.0f : 0) - (p.left ? 1.0f : 0);
    float sy = p.stickY + (p.down ? 1.0f : 0) - (p.up ? 1.0f : 0);
    if (p.square) phFov = clampf(phFov + sy * 30 * dt, 15, 90);
    else { phYaw += sx * 1.6f * dt; phPitch = clampf(phPitch - sy * 0.9f * dt, -0.05f, 1.35f); }
    float zoom = p.rstickY + (p.l ? 1.0f : 0) - (p.r ? 1.0f : 0);
    phDist = clampf(phDist + zoom * 6 * dt, 2.5f, 40);
    const Car& c = race.cars[race.playerIdx];
    V3 target = c.pos + c.up * 0.7f;
    float h = atan2f(c.fwd.y, c.fwd.x) + phYaw;
    V3 off(cosf(h) * cosf(phPitch), sinf(h) * cosf(phPitch), sinf(phPitch));
    viewCam.eye = target + off * phDist;
    if (viewCam.eye.z < c.pos.z + 0.15f) viewCam.eye.z = c.pos.z + 0.15f;
    viewCam.fwd = norm(target - viewCam.eye);
    viewCam.up = V3(0, 0, 1); viewCam.fov = phFov;
    viewMode = CAM_FAR;
}

void Game::drawReplayHud() {
    gfx::setDepthTest(false); gfx::setBlend(true); gfx::setFog(false, 0, 1, 0);
    gfx::bindTexture(nullptr);
    const Car& f = race.cars[repFocus];
    gfx::drawFill2D(8, 8, 96, 22, RGBA(0, 0, 0, 150));
    if (((int)(t * 2) & 1) || !repPlay) gfx::drawFill2D(14, 15, 8, 8, RGBA(230, 40, 40));
    R.font.draw(28, 9, repPlay ? "REPLAY" : "PAUSED", WHITE, 0.95f);
    static const char* CN[4] = {"TV", "Chase", "Far", "Cockpit"};
    char b[80];
    snprintf(b, sizeof b, "#%d %s %s   camera: %s", f.number, f.def->make, f.def->model, CN[repCam]);
    gfx::drawFill2D(110, 8, SCREEN_W - 118, 22, RGBA(0, 0, 0, 150));
    gfx::drawFill2D(110, 8, 4, 22, f.def->colour | 0xFF000000u);
    R.font.draw(120, 11, b, WHITE, 0.78f);
    // timeline
    float k = repCount > 1 ? repT / (repCount - 1) : 0;
    gfx::drawFill2D(20, SCREEN_H - 22, SCREEN_W - 40, 6, RGBA(60, 60, 70, 200));
    gfx::drawFill2D(20, SCREEN_H - 22, (SCREEN_W - 40) * k, 6, RGBA(230, 40, 40));
    snprintf(b, sizeof b, "-%.0f s", (repCount - 1 - repT) / 10.0f);
    R.font.drawRight(SCREEN_W - 20, SCREEN_H - 38, b, DIM, 0.7f);
    R.font.draw(20, SCREEN_H - 38, "X play/pause  R fast  L rewind  < > car  TRIANGLE camera  SQUARE photo  O back", DIM, 0.56f);
    gfx::setBlend(false); gfx::setDepthTest(true);
}

void Game::drawPhotoHud() {
    if (!phHelp) return;
    gfx::setDepthTest(false); gfx::setBlend(true); gfx::setFog(false, 0, 1, 0);
    gfx::bindTexture(nullptr);
    gfx::drawFill2D(SCREEN_W / 2 - 170, SCREEN_H - 62, 340, 54, RGBA(0, 0, 0, 160));
    R.font.drawCentre(SCREEN_W / 2, SCREEN_H - 60, "PHOTO MODE", ACC, 0.9f);
    R.font.drawCentre(SCREEN_W / 2, SCREEN_H - 44, "Stick: move around   R stick or L / R: closer / further   SQUARE + up/down: zoom", DIM, 0.56f);
    R.font.drawCentre(SCREEN_W / 2, SCREEN_H - 32, input::hasTouch() ? "X: hide this, then PS + START saves a screenshot   O: back"
                                                                       : "X: hide this, then take your screenshot   O: back", WHITE, 0.6f);
    char b[32]; snprintf(b, sizeof b, "lens %.0f", phFov);
    R.font.drawCentre(SCREEN_W / 2, SCREEN_H - 20, b, DIM, 0.55f);
    gfx::setBlend(false); gfx::setDepthTest(true);
}

// the Controls page: the buttons, then the driving options (tilt direction only while tilt is on;
// tilt and the rear pad only on the Vita), then reset and back
int Game::controlRows(int* ids) const {
    int n = 0;
    for (int i = 0; i < ACT_COUNT; i++) ids[n++] = i;
    ids[n++] = CR_HELP;
    if (input::hasTouch()) {
        ids[n++] = CR_TILT;
        if (cfg.tilt) ids[n++] = CR_TILTDIR;
        ids[n++] = CR_REAR;
    }
    ids[n++] = CR_RESET; ids[n++] = CR_BACK;
    return n;
}
