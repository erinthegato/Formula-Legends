#include "../src/trophies.h"
#include <algorithm>
// PC test program: runs the real game code with the software renderer.
//
//   fl_test menus <outdir>
//   fl_test race <track id> <car index> <laps> <opponents> <outdir> [shot times...]
//
// "race" lets the AI drive your car too, takes screenshots (chase, far, cockpit) at the given
// race times, writes 8 s of the engine mix to race_audio.wav and prints the lap times.
#include "../src/game.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

namespace pc { extern float fakeWait; }
namespace pc { extern Pad pad; extern audio::MixFn mixFn; extern long trisDrawn, drawCalls, vertsSent; void saveShot(const char*); }

extern long statVerts[6];
static void shot(Game& g, const std::string& path, float dt = 1.0f / 60) {
    Pad p{};
    for (long& s : statVerts) s = 0;
    g.frame(dt, p);
    pc::saveShot(path.c_str());
    fprintf(stderr, "    vertices: track %ld  terrain %ld  props %ld  cars %ld  wheels %ld\n", statVerts[0], statVerts[1], statVerts[2], statVerts[3], statVerts[4]);
    fprintf(stderr, "  shot %s  (%ld triangles, %ld vertices sent, %ld draw calls)\n", path.c_str(), pc::trisDrawn, pc::vertsSent, pc::drawCalls);
}

static void press(Game& g, bool Pad::*b) {
    Pad p{}; p.*b = true; g.frame(1.0f / 60, p);
    Pad q{}; g.frame(1.0f / 60, q);
}

static void writeWav(const char* path, const std::vector<int16_t>& s) {
    FILE* f = fopen(path, "wb");
    int n = (int)s.size() * 2;
    int hdr[] = {0x46464952, 36 + n, 0x45564157, 0x20746d66, 16, 0x00020001, 44100, 44100 * 4, 0x00100004, 0x61746164, n};
    fwrite(hdr, 4, 11, f);
    fwrite(s.data(), 2, s.size(), f);
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: see source\n"); return 1; }
    sys::init();
    Game g;
    if (!g.init()) { fprintf(stderr, "init failed (run from the folder that holds DATA/)\n"); return 1; }
    g.autoAllowed = false;          // fixed time steps here: the automatic quality would react to them
    std::string mode = argv[1];
    if (mode != "intro") { sound::musicFile(nullptr); g.screen = SCR_TITLE; }
    if (mode == "intro") {
        // play the intro with the real audio clock and grab frames at the sync points
        std::string out = argv[2];
        std::vector<int16_t> wav;
        float marks[] = {2.6f, 9.0f, 10.5f, 13.6f, 15.6f, 17.8f, 19.4f, 21.0f, 23.4f, 26.8f, 28.4f, 30.2f, 32.0f, 34.5f, 38.0f, 40.2f, 41.6f,
                         43.0f, 44.6f, 46.4f, 47.6f, 49.8f, 52.0f, 54.4f, 55.6f, 57.2f, 58.6f, 60.0f};
        const int NM = sizeof(marks) / sizeof(marks[0]);
        int mi = 0;
        for (int f = 0; f < 60 * 70 && g.screen == SCR_INTRO; f++) {
            Pad q{}; g.frame(1.0f / 60, q);
            if (pc::mixFn) { std::vector<int16_t> b(735 * 2); pc::mixFn(b.data(), 735); if (f % 2 == 0) wav.insert(wav.end(), b.begin(), b.end()); }
            if (f % 120 == 0) fprintf(stderr, "f %d pos %.2f t %.2f screen %d\n", f, g.introPos, g.t, (int)g.screen);
            if (mi < NM && g.introPos >= marks[mi]) { char b[48]; snprintf(b, sizeof b, "/intro_%03d.ppm", (int)(marks[mi] * 10)); pc::saveShot((out + b).c_str()); mi++; }
        }
        fprintf(stderr, "intro ended: screen %d at %.1f s of music\n", (int)g.screen, g.introPos);
        return 0;
    }
    if (mode == "attract") {
        // attract <outdir>: idle on the title until the attract movie starts, a frame from each shot,
        // then past the end of the song to see it loop, and a button press back to the title
        std::string out = argv[2];
        sound::musicFile(nullptr); g.screen = SCR_TITLE; g.idle = 0;
        int f = 0;
        for (; f < 60 * 40 && g.screen == SCR_TITLE; f++) { Pad q{}; g.frame(1.0f / 60, q); }
        fprintf(stderr, "attract started after %.1f s idle (screen %d)\n", f / 60.0f, (int)g.screen);
        int lastShot = -2, shotFrames = 0, n = 0, loops = 0; float lastPos = 0;
        for (f = 0; f < 60 * 225 && g.screen == SCR_INTRO; f++) {
            Pad q{}; g.frame(1.0f / 60, q);
            if (pc::mixFn) { std::vector<int16_t> b(735 * 2); pc::mixFn(b.data(), 735); }
            if (g.introPos < lastPos - 5) { loops++; fprintf(stderr, "looped at frame %d (music %.1f -> %.1f)\n", f, lastPos, g.introPos); }
            lastPos = g.introPos;
            if (g.introShot != lastShot) { lastShot = g.introShot; shotFrames = 0; }
            if (getenv("FL_VIDEO") && f < 60 * atoi(getenv("FL_VIDEO")) && f % 2 == 0) {
                char b[64]; snprintf(b, sizeof b, "/v_%05d.ppm", f / 2); pc::saveShot((out + b).c_str());
            }
            if (++shotFrames == 50 && loops == 0) {
                char b[64]; snprintf(b, sizeof b, "/attract_%03d_%05.1f.ppm", n++, g.introPos); pc::saveShot((out + b).c_str());
            }
        }
        fprintf(stderr, "after %.1f s: screen %d, loops %d, shot %d, music %.1f\n", f / 60.0f, (int)g.screen, loops, g.introShot, g.introPos);
        press(g, &Pad::cross);
        for (int i = 0; i < 30; i++) { Pad q{}; g.frame(1.0f / 60, q); }
        fprintf(stderr, "after a button: screen %d (title %d)\n", (int)g.screen, (int)SCR_TITLE);
        return 0;
    }
    if (mode == "physlap") {
        // physlap <track id> <era 0-3> <laps>: the AI drives the era's first car alone (time trial
        // rules, dry), no drawing; lap times and how often the car went light / left the ground
        std::string tid = argv[2]; int era = atoi(argv[3]), laps = atoi(argv[4]);
        int tr = 0; for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) tr = i;
        g.ttMode = true; g.set.track = tr; g.set.car = eraFirstCar(era); g.aiDrivesPlayer = true; g.set.weather = WX_DRY;
        g.startSession(SES_PRACTICE);
        g.race.toGarage(); g.race.flyingLap(g.set.setup);
        Race& r = g.race;
        float simT = 0, light = 0, minLoad = 9, maxAir = 0, maxLift = 0; int lastLap = 0; float best = 1e9f;
        while (r.player().lap < laps && simT < 1500) {
            r.update(1.0f / 60, 0, 0, 0, 0, true); simT += 1.0f / 60;
            r.sfxEvents.clear();
            Car& me = r.player();
            if (me.v > 20) { if (me.body.load < 0.5f) light += 1.0f / 60; minLoad = fminf(minLoad, me.body.load); maxAir = fmaxf(maxAir, me.body.air); maxLift = fmaxf(maxLift, me.body.hOff); }
            if (getenv("FL_TRACE") && me.lap == 2) {
                float a = 0, b = 0; sscanf(getenv("FL_TRACE"), "%f %f", &a, &b);
                if (me.s >= a && me.s <= b) printf("trace %.1f %.3f %.4f %.4f\n", me.s, me.body.load, me.body.hOff, me.body.pitch);
            }
            if (getenv("FL_DBG") && me.v > 20 && me.body.load < 0.6f && me.lap == 2) {
                int i = r.track.index(me.s);
                fprintf(stderr, "s %6.0f d %5.1f hw %4.1f kerb %d v %3.0f load %.2f hOff %+.3f vz %+.2f grade %+.3f\n", me.s, me.d, r.track.pts[i].hw, (int)r.track.pts[i].kerb, me.v * 3.6f, me.body.load, me.body.hOff, me.body.vz, r.track.gradeAt(me.s));
            }
            if (me.lap != lastLap) { lastLap = me.lap; if (me.lap >= 2) best = fminf(best, me.lastLap); }
        }
        printf("%-12s era %d  best %.3f  light %.2f s  min load %.2f  max air %.2f s  max rise %.2f m\n", tid.c_str(), era, best, light, minLoad, maxAir, maxLift);
        return 0;
    }
    if (mode == "barriers") {   // barrier lines of a circuit (x y per point, left then right), with / without the run-off cut
        std::string tid = argv[2]; FILE* f = fopen(argv[3], "w");
        for (int cut = 0; cut < 2; cut++) {
            Track::noRunoffCut = cut == 0;
            Track t; for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) t.load(&TRACKS[i]);
            for (auto& q : t.pts) {
                V3 l = q.p + q.n * (q.hw + q.outL), r = q.p - q.n * (q.hw + q.outR);
                fprintf(f, "%d %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n", cut, q.p.x, q.p.y, q.hw, l.x, l.y, r.x, r.y);
            }
        }
        fclose(f); return 0;
    }
    if (mode == "terrain") {    // where the terrain mesh comes up through the road or the verges: fl_test terrain <track|all>
        std::string tid = argv[2];
        Track::keepGround = true;
        for (int k = 0; k < TRACK_COUNT; k++) {
            if (tid != "all" && tid != TRACKS[k].id) continue;
            Track t; t.load(&TRACKS[k]);
            auto meshZ = [&](float x, float y) {       // the two triangles of a cell, as the strips draw them
                float fx = (x - t.gx0) / t.gsub, fy = (y - t.gy0) / t.gsub;
                fx = clampf(fx, 0, t.gtnx - 1.001f); fy = clampf(fy, 0, t.gtny - 1.001f);
                int ix = (int)fx, iy = (int)fy; float u = fx - ix, v = fy - iy;
                float a = t.groundTz[iy * t.gtnx + ix], b = t.groundTz[iy * t.gtnx + ix + 1];
                float c = t.groundTz[(iy + 1) * t.gtnx + ix], d = t.groundTz[(iy + 1) * t.gtnx + ix + 1];
                // quad (x,y) (x,y+1) (x+1,y+1) (x+1,y): split along (x,y+1)-(x+1,y)
                return u + v <= 1 ? a + (b - a) * u + (c - a) * v : d + (c - d) * (1 - u) + (b - d) * (1 - v);
            };
            int bad = 0; float worst = 0; float worstS = 0;
            for (int i = 0; i < t.n; i++) {
                const TrackPoint& q = t.pts[i];
                for (float f = -1; f <= 1.001f; f += 0.25f) {
                    float d = f * (q.hw + 2);
                    V3 p = q.p + q.n * d;
                    float over = meshZ(p.x, p.y) - p.z;
                    if (over > 0.0f) { bad++; if (over > worst) { worst = over; worstS = i * t.step; } }
                }
            }
            fprintf(stderr, "%-9s terrain above the road at %d of %d samples, worst %.2f m at %.0f m\n", TRACKS[k].id, bad, t.n * 9, worst, worstS);
        }
        return 0;
    }
    if (mode == "verge") {      // the player's car parked on the steepest verges of a circuit: fl_test verge <track#> <out>
        std::string out = argv[3];
        Track::noGroundCap = getenv("EMR_NOCAP") != nullptr;
        g.set.track = atoi(argv[2]); g.set.car = 1; g.set.raceMin = 30; g.aiDrivesPlayer = true;
        g.startRace();
        for (int i = 0; i < 120; i++) { Pad q{}; g.step(1.0f / 30, q); }
        Track& t = g.race.track;
        std::vector<std::pair<float, int>> steep;
        for (int i = 0; i < t.n; i += 4) for (int side = 0; side < 2; side++) {
            float sg = side ? -1.0f : 1.0f, w = side ? t.pts[i].outR : t.pts[i].outL;
            if (w < 6) continue;
            steep.push_back({-fabsf(t.groundAt(i * t.step, sg * (t.pts[i].hw + w - 1))), i * 2 + side});
        }
        std::sort(steep.begin(), steep.end());
        if (getenv("EMR_AT")) { steep.clear(); steep.push_back({0, (int)(atof(getenv("EMR_AT")) / t.step) * 2}); }
        for (int k = 0; k < 3 && k < (int)steep.size(); k++) {
            int i = steep[k].second / 2, side = steep[k].second % 2;
            float sg = side ? -1.0f : 1.0f, w = side ? t.pts[i].outR : t.pts[i].outL;
            Car& me = g.race.player();
            me.s = i * t.step; me.d = getenv("EMR_AT") ? 0 : sg * (t.pts[i].hw + w * 0.6f); me.v = 0; me.yaw = 0.35f * sg; me.lift = 0;
            g.race.pose(me);
            g.cam = CAM_CHASE; g.R.camInit = false;
            float sl = 0; float z = t.groundAt(me.s, me.d, &sl);
            fprintf(stderr, "verge at %.0f m (%s): car %.2f m above the road, slope %.2f\n", me.s, side ? "right" : "left", z, sl);
            char bf[64]; snprintf(bf, sizeof bf, "/verge_%d.ppm", k);
            g.race.pose(me); shot(g, out + bf, 0.0001f);
        }
        return 0;
    }
    if (mode == "autoq") {      // automatic quality: slow frames lower the detail, fast ones bring it back
        g.autoAllowed = true; cfg.autoQuality = true; cfg.setPreset(0); cfg.apply();
        g.set.track = 1; g.set.car = 1; g.set.raceMin = 30; g.aiDrivesPlayer = true;
        g.startRace();
        auto run = [&](float secs, float dt, float wait) {
            pc::fakeWait = wait;
            for (float t = 0; t < secs; t += dt) { Pad q{}; g.step(dt, q); g.autoQualityStep(dt); }
            fprintf(stderr, "  %.0f s at %.0f fps (waiting %.1f ms a frame): level %d  distance %.2f scenery %d effects %d\n",
                    secs, 1 / dt, wait * 1000, g.autoLevel, perf.distance, (int)perf.scenery, (int)perf.effects);
        };
        run(6, 1.0f / 45, 0); run(6, 1.0f / 45, 0); run(6, 1.0f / 58, 0);
        run(14, 1.0f / 60, 0.010f); run(14, 1.0f / 60, 0.010f); run(30, 1.0f / 60, 0.010f);
        fprintf(stderr, "messages: %s\n", g.race.msgs.empty() ? "" : g.race.msgs[0].text);
        return 0;
    }
    if (mode == "bench") {      // the benchmark scenes (what bench.txt starts on the PSP), perf.log written in the current folder
        g.bench = true; remove("perf.log");
        float simT = 0;
        while (!g.benchDone && simT < 400) { Pad q{}; g.frame(1.0f / 30, q); simT += 1.0f / 30; }
        fprintf(stderr, "bench done %d after %.0f s\n", (int)g.benchDone, simT);
        return 0;
    }
    if (mode == "career") {     // a new team: setup, livery, the team screens, one race, the report
        std::string out = argv[2];
        remove("career.sav");
        auto settle = [&](int n = 50) { for (int i = 0; i < n; i++) { Pad q{}; g.frame(1.0f / 30, q); } };
        settle(30);
        g.sel = 2; press(g, &Pad::cross); settle(40); shot(g, out + "/c_new.ppm");
        press(g, &Pad::right);                                   // team name
        g.sel = 3; press(g, &Pad::left);                         // race length 10 min
        g.sel = 4; press(g, &Pad::cross); settle(60); shot(g, out + "/c_livery0.ppm");
        press(g, &Pad::right); press(g, &Pad::right);           // style
        g.sel = 1; press(g, &Pad::right); press(g, &Pad::right); press(g, &Pad::right); press(g, &Pad::right);   // main colour
        g.sel = 2; press(g, &Pad::right); press(g, &Pad::right);
        settle(30); shot(g, out + "/c_livery1.ppm"); settle(60); shot(g, out + "/c_livery2.ppm");
        g.sel = 5; press(g, &Pad::cross); settle(50); shot(g, out + "/c_team.ppm");
        g.sel = 1; press(g, &Pad::cross); settle(5); shot(g, out + "/c_sponsors.ppm");
        g.sel = 1; press(g, &Pad::cross); settle(5); press(g, &Pad::circle);
        g.sel = 2; press(g, &Pad::cross); settle(5); shot(g, out + "/c_drivers.ppm"); press(g, &Pad::circle);
        g.sel = 3; press(g, &Pad::cross); g.sel = 0; press(g, &Pad::cross); g.sel = 1; press(g, &Pad::cross); settle(5); shot(g, out + "/c_upgrades.ppm"); press(g, &Pad::circle);
        fprintf(stderr, "career: %s #%d money %d skip #%d (%s) car idx %d\n", TEAM_NAMES[g.career.name], g.career.number, g.career.money, g.career.skipNum, CARS[g.career.skipCar].team, g.career.carIndex());
        // one race, driven by the AI
        g.aiDrivesPlayer = true;
        g.sel = 0; press(g, &Pad::cross);
        fprintf(stderr, "race started: screen %d, player car %s %s #%d\n", (int)g.screen, g.race.player().def->make, g.race.player().def->model, g.race.player().number);
        bool skipSeen = false; for (auto& c : g.race.cars) if (c.number == g.career.skipNum && (int)(c.def - CARS) == g.career.skipCar) skipSeen = true;
        fprintf(stderr, "replaced entry still on the grid: %s; field %d cars\n", skipSeen ? "YES (bug)" : "no", (int)g.race.cars.size());
        g.cam = CAM_CHASE; float simT = 0; bool shotTaken = false;
        // a career round is a full weekend: practice and qualifying first, then the race
        for (;;) {
            while (g.screen == SCR_RACE && g.race.session != SES_RACE && simT < 3000) { Pad q{}; g.step(1.0f / 30, q); simT += 1.0f / 30; }
            if (g.race.session == SES_RACE || g.screen != SCR_RESULTS) break;
            fprintf(stderr, "session %d done at %.0f s (rubber %.2f, P1 best %.3f), on to the next\n", (int)g.race.session, simT, g.race.rubber, g.race.cars[0].bestLap);
            press(g, &Pad::cross);
        }
        fprintf(stderr, "race session: screen %d, grid from qualifying %d cars\n", (int)g.screen, (int)g.qualiGrid.size());
        simT = 0;
        while (g.screen == SCR_RACE && simT < 3000) {
            Pad q{}; g.step(1.0f / 30, q); simT += 1.0f / 30;
            if (!shotTaken && simT > 70) {
                for (int k = 0; k < 2; k++) { g.cam = k ? CAM_COCKPIT : CAM_CHASE; g.R.camInit = false; for (int f = 0; f < 20; f++) { Pad z{}; g.frame(1.0f / 30, z); } shot(g, out + (k ? "/c_race_cockpit.ppm" : "/c_race.ppm"), 1.0f / 60); }
                g.cam = CAM_CHASE; shotTaken = true;
            }
        }
        fprintf(stderr, "rubber at the end %.2f\n", g.race.rubber);
        fprintf(stderr, "after race: screen %d  money %d  last pos %d  prize %d sponsor %d bonus %d running %d salaries %d repairs %d\n", (int)g.screen,
                g.career.money, g.career.lastPos, g.career.inPrize, g.career.inSponsor, g.career.inBonus, g.career.outRunning, g.career.outSalaries, g.career.outRepairs);
        press(g, &Pad::cross); settle(40); press(g, &Pad::cross); settle(40);
        fprintf(stderr, "back at screen %d, round %d\n", (int)g.screen, g.career.champ.round);
        shot(g, out + "/c_report.ppm");
        Career c2; bool okLoad = c2.load("career.sav"); fprintf(stderr, "saved career loads: %d round %d\n", (int)okLoad, c2.champ.round);
        return 0;
    }
    if (mode == "timetrial") {   // timetrial <track> <car> <laps> <outdir>: AI laps against its own ghost, saved and reloaded
        int tr = atoi(argv[2]), car = atoi(argv[3]), laps = atoi(argv[4]); std::string out = argv[5];
        g.ttMode = true; g.set.track = tr; g.set.car = car; g.aiDrivesPlayer = true;
        if (getenv("EMR_NOHYBRID")) Race::hybridOff = true;
        char gp[64]; g.ghostPath(gp, sizeof gp); remove(gp);
        g.startSession(SES_PRACTICE);
        g.race.toGarage(); g.race.flyingLap(g.set.setup); g.screen = SCR_RACE;
        fprintf(stderr, "time trial: %s, %s, %d cars, session %d, dry %.2f\n", TRACKS[tr].name, CARS[car].id, (int)g.race.cars.size(), (int)g.race.session, g.race.rain);
        float simT = 0; int lastLap = 0; bool shot1 = false, shot2 = false;
        while (g.race.player().lap < laps && simT < 3000) {
            Pad q{}; g.step(1.0f / 30, q); simT += 1.0f / 30;
            Car& me = g.race.player();
            if (me.lap != lastLap) {
                lastLap = me.lap;
                fprintf(stderr, "lap %d done: last %.3f best %.3f record %.3f ghost pts %d delta-on %d\n", me.lap, me.lastLap, me.bestLap, g.race.ghostLap, (int)g.race.ghost.size(), (int)g.race.ttDeltaOn);
                if (me.lap == 2) me.skill *= 0.95f;          // lap 3 slower: the ghost pulls away
            }
            if (me.lap == 2 && !shot1 && simT - 0 > 0 && g.race.time - me.lapStart > 8) {
                shot1 = true;
                fprintf(stderr, "  delta at 8 s: %+.3f\n", g.race.ttDelta);
                g.cam = CAM_CHASE; g.R.camInit = false; shot(g, out + "/tt_chase.ppm", 0);
            }
            if (me.lap == 2 && !shot2 && g.race.time - me.lapStart > 14) {
                shot2 = true; fprintf(stderr, "  delta at 14 s: %+.3f\n", g.race.ttDelta);
                g.cam = CAM_COCKPIT; g.R.camInit = false; shot(g, out + "/tt_cockpit.ppm", 0); g.cam = CAM_CHASE;
            }
        }
        FILE* f = fopen(gp, "rb"); fprintf(stderr, "ghost file %s: %s\n", gp, f ? "saved" : "MISSING"); if (f) fclose(f);
        Race r2; r2.track = g.race.track; bool ok = r2.loadGhost(gp);
        fprintf(stderr, "reload: %d  record %.3f  points %d\n", (int)ok, r2.ghostLap, (int)r2.ghost.size());
        g.aiDrivesPlayer = false; g.screen = SCR_PAUSE; g.sel = 6; press(g, &Pad::cross); g.sel = 6; g.idle = 0;
        g.frame(1.0f / 30, *(new Pad{})); shot(g, out + "/tt_title.ppm");
        remove(gp);
        return 0;
    }
    if (mode == "crew") {      // the crew on the title screen, 12 frames 1/15 s apart (do the poses blend?)
        std::string out = argv[2];
        for (int i = 0; i < 80; i++) { Pad q{}; g.frame(1.0f / 30, q); }
        for (int k = 0; k < 12; k++) { char fn[64]; snprintf(fn, sizeof fn, "/crew_%02d.ppm", k); shot(g, out + fn); Pad q{}; g.frame(1.0f / 30, q); g.frame(1.0f / 30, q); }
        return 0;
    }
    if (mode == "menus") {
        std::string out = argv[2];
        auto settle = [&](int n = 80) { for (int i = 0; i < n; i++) { Pad q{}; g.frame(1.0f / 30, q); } };
        settle(); shot(g, out + "/menu_title.ppm");
        press(g, &Pad::cross); settle(8); shot(g, out + "/menu_walk.ppm"); settle(); shot(g, out + "/menu_track.ppm");
        press(g, &Pad::down); settle(5); shot(g, out + "/menu_track2.ppm");
        press(g, &Pad::cross); settle(); shot(g, out + "/menu_car.ppm");
        for (int i = 0; i < 9; i++) press(g, &Pad::down);
        settle(20); shot(g, out + "/menu_car2.ppm");
        press(g, &Pad::cross); settle(); shot(g, out + "/menu_settings.ppm");
        g.sel = 14; press(g, &Pad::cross); settle(40); press(g, &Pad::down); press(g, &Pad::down); press(g, &Pad::down); press(g, &Pad::left); settle(5); shot(g, out + "/menu_setup.ppm");
        { int keep = g.set.car; g.set.car = 1; g.menuCamInit = false; settle(30); shot(g, out + "/menu_setup_hy.ppm"); press(g, &Pad::circle); settle(30); shot(g, out + "/menu_settings_hy.ppm"); press(g, &Pad::down); g.set.car = keep; g.sel = 14; press(g, &Pad::cross); g.menuCamInit = false; settle(30); }
        press(g, &Pad::circle); settle(30);
        press(g, &Pad::circle); press(g, &Pad::circle); press(g, &Pad::circle);      // back to the title
        g.sel = 4; press(g, &Pad::cross); settle(); shot(g, out + "/menu_options.ppm");
        g.sel = 5; press(g, &Pad::right); g.sel = 15; press(g, &Pad::cross); settle(5);
        g.sel = 6; press(g, &Pad::cross); press(g, &Pad::up); press(g, &Pad::up); g.sel = 2; press(g, &Pad::square); settle(4); shot(g, out + "/menu_controls.ppm");
        fprintf(stderr, "pit request is now on: %s\n", BUTTON_NAME[cfg.bind[ACT_PIT]]);
        { int ids[24]; g.sel = g.controlRows(ids) - 2; } press(g, &Pad::cross);       // reset buttons
        press(g, &Pad::circle); press(g, &Pad::circle); settle(5);
        g.sel = 5; press(g, &Pad::cross); settle(); shot(g, out + "/menu_help.ppm");
        press(g, &Pad::circle);
        g.sel = 7; press(g, &Pad::cross); settle(); shot(g, out + "/menu_trophies.ppm");
        press(g, &Pad::circle);
        { unsigned keep = trophies.won; trophies.won &= ~(1u << TR_PODIUM);
          trophies.award(TR_PODIUM); for (int i = 0; i < 30; i++) g.frame(1.0f / 60, Pad{}); shot(g, out + "/menu_trophy_popup.ppm");
          trophies.won = keep; trophies.queued = 0; trophies.save(); }
        // 6 s of menu audio (the soundtrack)
        std::vector<int16_t> wav;
        for (int f = 0; f < 360; f++) {
            Pad q{}; g.step(1.0f / 60, q); sound::musicEnable(true); sound::update();
            if (pc::mixFn) { std::vector<int16_t> b(735 * 2); pc::mixFn(b.data(), 735); wav.insert(wav.end(), b.begin(), b.end()); }
        }
        writeWav((out + "/menu_audio.wav").c_str(), wav);
        fprintf(stderr, "now playing: %s\n", sound::nowPlaying());
        return 0;
    }
    if (mode == "garage") {
        std::string out = argv[2];
        auto settle = [&](int n) { for (int i = 0; i < n; i++) { Pad q{}; g.frame(1.0f / 30, q); } };
        g.set.track = 4; g.set.car = getenv("EMR_CAR") ? atoi(getenv("EMR_CAR")) : 9; g.set.weekend = true; g.set.sessionMin = 10; g.set.opponents = 11;
        g.startRace();
        settle(40); shot(g, out + "/g_1_garage.ppm");
        int inG = 0; for (auto& c : g.race.cars) if (c.pit == PIT_STOP) inG++;
        fprintf(stderr, "practice start: screen %d, player at the computer %d, cars in garages %d/%d\n", (int)g.screen, g.race.inGarage(), inG, (int)g.race.cars.size());
        g.setupSel = 0; press(g, &Pad::right); press(g, &Pad::right);          // wing 3 -> 5
        g.setupSel = 8; press(g, &Pad::cross);                                // drive out
        settle(60); shot(g, out + "/g_2_pushed.ppm");
        settle(55); shot(g, out + "/g_2b_pushing.ppm"); settle(8); shot(g, out + "/g_2c_pushing.ppm");
        settle(87);
        const Car& me = g.race.player();
        fprintf(stderr, "drive out: screen %d pit %d (0 = on track) v %.1f wing %d\n", (int)g.screen, me.pit, me.v, me.su.wing);
        settle(120); shot(g, out + "/g_3_out.ppm");
        int onTrack = 0; for (auto& c : g.race.cars) if (c.pit == PIT_NONE) onTrack++;
        fprintf(stderr, "cars out on track after %.0f s: %d\n", g.race.time, onTrack);
        // the AI brings our car back in (pit request -> pit lane -> garage)
        g.aiDrivesPlayer = true; g.race.player().wantPit = true;
        bool sawIn = false; int overlaps = 0;
        for (int f = 0; f < 30 * 400 && !sawIn; f++) {
            Pad q{}; g.frame(1.0f / 30, q);
            if (g.race.player().pit == PIT_STOP && g.race.player().pitPhase == -4) { sawIn = true; pc::saveShot((out + "/g_4_in.ppm").c_str()); }
            for (size_t a = 0; a < g.race.cars.size(); a++) for (size_t b = a + 1; b < g.race.cars.size(); b++) {
                const Car &A = g.race.cars[a], &B = g.race.cars[b];
                if (A.pit || B.pit) continue;
                if (fabsf(g.race.lapGap(A.s, B.s)) < 3.0f && fabsf(A.d - B.d) < 1.2f) overlaps++;
            }
        }
        fprintf(stderr, "pit request -> garage: %s at %.0f s; frames with cars inside each other: %d\n", sawIn ? "yes" : "NO", g.race.time, overlaps);
        g.aiDrivesPlayer = false;
        g.race.toGarage(); g.screen = SCR_GARAGE;
        g.setupSel = 10; press(g, &Pad::right); press(g, &Pad::right);        // time x16
        float t0 = g.race.time; settle(30);
        fprintf(stderr, "time x%d: %.1f s of session in 1 s\n", (int)g.race.timeAccel, g.race.time - t0);
        g.setupSel = 9; press(g, &Pad::cross); settle(5);                      // flying lap
        fprintf(stderr, "flying lap: pit %d v %.0f km/h outLap %d\n", g.race.player().pit, g.race.player().v * 3.6f, (int)g.race.player().outLap);
        shot(g, out + "/g_5_flying.ppm");
        g.race.toGarage(); g.screen = SCR_GARAGE; settle(3);
        g.setupSel = 11; press(g, &Pad::cross); settle(5);                     // skip to qualifying
        fprintf(stderr, "skip: screen %d (7 = results)\n", (int)g.screen);
        press(g, &Pad::cross); settle(40);
        fprintf(stderr, "quali: screen %d at the computer %d\n", (int)g.screen, g.race.inGarage());
        g.setupSel = 0; press(g, &Pad::left); g.setupSel = 3; press(g, &Pad::right);
        settle(3); shot(g, out + "/g_6_quali.ppm");
        fprintf(stderr, "quali setup: wing %d (sealed at 5), brake front %d\n", g.set.setup.wing, g.set.setup.brakeF);
        return 0;
    }
    if (mode == "reel") {
        // highlights: how often the TV camera's view of the followed car is blocked by scenery
        std::string out = argv[2];
        int blocked = 0, frames = 0, shots = 0;
        for (int run = 0; run < 3; run++) {
            g.startReel();
            for (int f = 0; f < 30 * 60 && g.screen == SCR_REEL; f++) {
                Pad q{}; g.step(1.0f / 30, q);
                Camera c = g.R.tvCamera(g.race, g.reelFocus, 1.0f / 30);
                const Car& car = g.race.cars[g.reelFocus];
                if (g.R.sightBlocked(g.race.track, c.eye, car.pos + V3(0, 0, 0.8f))) blocked++;
                frames++;
                if (f % 450 == 200 && shots < 8) { g.frame(0, q); char b[64]; snprintf(b, sizeof b, "/reel_%d.ppm", shots++); pc::saveShot((out + b).c_str()); }
            }
            fprintf(stderr, "reel %d: %s\n", run, TRACKS[g.race.set.track].name);
        }
        fprintf(stderr, "view blocked in %d of %d frames (%.1f%%)\n", blocked, frames, 100.0 * blocked / frames);
        return 0;
    }
    if (mode == "podium") {
        g.set.track = 8; g.set.car = 1; g.set.laps = 1; g.set.opponents = 11; g.aiDrivesPlayer = true;
        g.startRace();
        for (int i = 0; i < 30 * 400 && g.screen == SCR_RACE; i++) { Pad q{}; g.frame(1.0f / 30, q); }
        press(g, &Pad::cross);
        for (int i = 0; i < 120; i++) { Pad q{}; g.frame(1.0f / 30, q); }
        shot(g, std::string(argv[2]) + "/podium.ppm");
        return 0;
    }
    if (mode == "handling") {
        // scripted driver: accelerate, then steer <amount> for 1.5 s, then let go
        RaceSettings rs; rs.track = 2; rs.car = 1; rs.opponents = 5; rs.weather = WX_DRY;
        float amount = (float)atof(argv[2]); float brake = argc > 3 ? (float)atof(argv[3]) : 0;
        g.race.start(rs, SES_PRACTICE);
        Race& r = g.race; float t0 = -1;
        for (int i = 0; i < 60 * 40; i++) {
            Car& c = r.player();
            float st = 0, th = 1, bk = 0;
            if (t0 < 0 && c.v > (argc > 4 ? atof(argv[4]) : 55)) t0 = r.time;
            if (t0 >= 0) { float tt = r.time - t0; if (tt < 0.8f) { st = amount; th = brake > 0 ? 0 : 0.6f; bk = brake; } else th = 0.5f; if (tt > 4) break;
                if (i % 12 == 0) printf("t %.1f  steer %.1f  v %5.1f  nose %6.1f deg  slide %6.1f deg  across %5.1f m  slip %.2f\n", tt, st, c.v, c.yaw * 57.3f, c.drift * 57.3f, c.d, c.slip); }
            r.update(1.0f / 60, st, th, bk, 0, false);
        }
        return 0;
    }
    if (mode == "cars") {
        // every car on the turntable from two sides
        std::string out = argv[2];
        for (int i = 0; i < CAR_COUNT; i++) for (int v = 0; v < 2; v++) {
            gfx::beginFrame(RGBA(70, 80, 95));
            g.R.drawCarShowroom(i, v ? 2.6f : -0.5f, 0, -104, 480, 480);
            gfx::endFrame();
            char b[64]; snprintf(b, sizeof b, "/car_%02d_%d.ppm", i, v);
            pc::saveShot((out + b).c_str());
        }
        return 0;
    }
    if (mode == "screens") {
        // walks through the new screens: championship, manage mode, team screen + save, podium,
        // championship standings, load menu, highlights reel
        std::string out = argv[2];
        auto run = [&](float secs, const Pad& p) { int n = (int)(secs * 30); for (int i = 0; i < n; i++) { Pad q = p; g.frame(1.0f / 30, q); Pad z{}; (void)z; } };
        Pad none{};
        g.t = 2.5f; shot(g, out + "/s_title.ppm");
        remove("champ.sav");
        press(g, &Pad::down); press(g, &Pad::cross);                     // championship
        fprintf(stderr, "screen %d\n", (int)g.screen);
        if (g.screen == SCR_CHAMP) { press(g, &Pad::down); press(g, &Pad::cross); }   // new season
        press(g, &Pad::cross);                                            // car
        g.set.raceMin = getenv("EMR_MIN") ? atoi(getenv("EMR_MIN")) : 5; g.set.opponents = 11;
        shot(g, out + "/s_settings.ppm");
        g.sel = 13; press(g, &Pad::cross);                                // start season
        shot(g, out + "/s_champ0.ppm");
        g.sel = 0; press(g, &Pad::cross);                                 // round 1
        fprintf(stderr, "round start screen %d track %s timeScale %.1f\n", (int)g.screen, TRACKS[g.race.set.track].id, g.race.timeScale);
        // manage mode: the team drives our car, x16, and we call it in
        g.race.player().plan.aiDrives = true;
        g.race.timeAccel = 16;
        float simT = 0;
        while (g.screen == SCR_RACE && simT < 200) { g.frame(1.0f / 30, none); simT += 1.0f / 30; if (simT > 6 && simT < 6.04f) { pc::saveShot((out + "/s_manage.ppm").c_str()); g.wallView = false; } if (simT > 7 && simT < 7.04f) { pc::saveShot((out + "/s_manage_tv.ppm").c_str()); g.wallView = true; } if (simT > 30 && simT < 30.04f) { pc::saveShot((out + "/s_wall2.ppm").c_str()); g.race.player().wantPit = true; } }
        fprintf(stderr, "after manage: screen %d t %.0f\n", (int)g.screen, g.race.time);
        if (g.screen == SCR_TEAM) {
            shot(g, out + "/s_team.ppm");
            g.sel = 4; press(g, &Pad::cross);                             // save to slot 1
            shot(g, out + "/s_team_saved.ppm");
            // manual pit stop: we work the prompts (perfect timing on the first ones, a fumble on one)
            g.race.player().plan.aiDrives = false; g.race.player().plan.nextDriver = 0; g.race.player().plan.swapDriver = g.race.player().driver != 0;
            g.sel = 5; press(g, &Pad::cross);
            int prompts = 0; bool shotTaken = false;
            for (int f = 0; f < 3000 && g.race.player().pit == PIT_STOP; f++) {
                Pad q{};
                const PitQte& qt = g.race.qte;
                if (qt.waiting && qt.pos > (qt.p0 + qt.p1) * 0.5f) {
                    if (!shotTaken && prompts == 2) { pc::saveShot((out + "/s_qte.ppm").c_str()); shotTaken = true; }
                    bool fumble = prompts == 3;
                    int b = fumble ? (qt.button + 1) % 4 : qt.button;
                    if (b == QB_CROSS) q.cross = true;
                    if (b == QB_CIRCLE) q.circle = true;
                    if (b == QB_SQUARE) q.square = true;
                    if (b == QB_TRIANGLE) q.triangle = true;
                    prompts++;
                }
                g.frame(1.0f / 30, q);
                if (f % 2 == 1 && !qt.waiting && qt.flash > 0.6f && prompts == 4) pc::saveShot((out + "/s_qte_result.ppm").c_str());
            }
            fprintf(stderr, "manual stop: %d prompts, perfect %d good %d fumbled %d, last message: %s\n", prompts, g.race.qte.perfect, g.race.qte.good, g.race.qte.miss, g.race.msgs.empty() ? "" : g.race.msgs[0].text);
            g.race.player().plan.aiDrives = true;
        }
        simT = 0;
        while (g.screen == SCR_RACE && simT < 2000) { g.frame(1.0f / 30, none); simT += 1.0f / 30; if (g.screen == SCR_TEAM) { g.sel = 5; press(g, &Pad::cross); } }
        fprintf(stderr, "race done: screen %d time %.0f\n", (int)g.screen, g.race.time);
        shot(g, out + "/s_results.ppm");
        press(g, &Pad::cross);
        run(4, none); shot(g, out + "/s_podium.ppm");
        press(g, &Pad::cross);
        shot(g, out + "/s_champ1.ppm");
        press(g, &Pad::circle);                                           // title
        g.sel = 2; press(g, &Pad::cross); shot(g, out + "/s_load.ppm");
        g.sel = 0; press(g, &Pad::cross);
        fprintf(stderr, "loaded: screen %d atBox %d\n", (int)g.screen, g.race.playerAtBox());
        g.frame(1.0f / 30, none);
        shot(g, out + "/s_loaded.ppm");
        g.sel = 7; press(g, &Pad::cross);                                 // retire to menu
        g.idle = 30; g.frame(1.0f / 30, none);
        run(10, none); shot(g, out + "/s_reel.ppm");
        fprintf(stderr, "reel screen %d\n", (int)g.screen);
        return 0;
    }
    if (mode == "weekend") {
        // practice -> qualifying -> race with 1-minute sessions; checks music and the grid
        std::string tid = argv[2];
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        g.set.car = atoi(argv[3]); g.set.laps = atoi(argv[4]); g.set.opponents = atoi(argv[5]);
        std::string out = argv[6];
        g.set.weekend = true; g.set.sessionMin = getenv("EMR_SESS") ? atoi(getenv("EMR_SESS")) : 1;
        g.aiDrivesPlayer = true;
        g.startRace();
        const char* names[3] = {"practice", "qualifying", "race"};
        for (int ses = 0; ses < 3; ses++) {
            std::vector<int16_t> wav;
            float simT = 0, musicSum = 0; int musicN = 0;
            if (ses == 2) {
                fprintf(stdout, "race grid:");
                std::vector<const Car*> grid(g.race.cars.size());
                for (auto& c : g.race.cars) fprintf(stdout, " #%d", c.number);
                fprintf(stdout, "\n");
            }
            while (g.screen == SCR_RACE && simT < 1200) {
                Pad q{};
                g.frame(1.0f / 30, q);
                if (pc::mixFn && simT > 3 && simT < 13) {
                    std::vector<int16_t> b(1470 * 2); pc::mixFn(b.data(), 1470); wav.insert(wav.end(), b.begin(), b.end());
                    musicSum += sound::musicLevel(); musicN++;
                }
                if (simT > 20 && simT < 20.04f) { pc::saveShot((out + "/weekend_" + names[ses] + ".ppm").c_str()); }
                simT += 1.0f / 30;
            }
            writeWav((out + "/weekend_" + names[ses] + ".wav").c_str(), wav);
            fprintf(stdout, "%s: %.0f s, music level %.2f, results:\n", names[ses], g.race.time, musicN ? musicSum / musicN : 0);
            std::vector<const Car*> order(g.race.cars.size());
            for (auto& c : g.race.cars) order[c.position - 1] = &c;
            for (auto* c : order) { char b[16]; formatTime(b, c->bestLap); fprintf(stdout, "  P%-2d #%-3d %-9s %s %s%s\n", c->position, c->number, c->def->make, b, c->def->cls == CLASS_HYPER ? "HY" : "GT", c->player ? " <- player" : ""); }
            if (ses == 1) { fprintf(stdout, "qualifying order:"); for (int n : g.race.qualifyingOrder()) fprintf(stdout, " #%d", n); fprintf(stdout, "\n"); }
            Pad x{}; x.cross = true; g.frame(1.0f / 30, x);
            Pad z{}; g.frame(1.0f / 30, z);
        }
        return 0;
    }
    if (mode == "props") {
        // fl_test props <track id> <outdir>: a picture of every Unity AI building on the circuit
        std::string tid = argv[2], out = argv[3];
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        g.set.car = 1; g.set.laps = 1; g.set.opponents = 3; g.set.weather = 1;
        g.aiDrivesPlayer = true;
        g.startRace();
        if (g.screen != SCR_RACE) { fprintf(stderr, "race did not start\n"); return 1; }
        static const char* NM[PROP_COUNT] = {"", "", "", "", "", "", "control", "bridge", "tvtower", "hospitality"};
        int k = 0;
        for (const PropInst& p : g.race.track.props) {
            if (p.model < PROP_CONTROL) continue;
            // stand on the track near it, looking at it
            int bi = 0; float bd = 1e18f;
            for (int i = 0; i < g.race.track.n; i++) { V3 d = g.race.track.pts[i].p - p.p; float dd = dot(d, d); if (dd < bd) { bd = dd; bi = i; } }
            const TrackPoint& q = g.race.track.pts[(bi + g.race.track.n - (p.model == PROP_BRIDGE ? 12 : 6)) % g.race.track.n];
            Camera cam; cam.eye = q.p + V3(0, 0, getenv("EMR_CAMZ") ? (float)atof(getenv("EMR_CAMZ")) : 3.0f); cam.up = V3(0, 0, 1); cam.fov = 60;
            cam.fwd = norm(p.p + V3(0, 0, p.model == PROP_BRIDGE ? 4.0f : 5.0f) - cam.eye);
            gfx::beginFrame(RGBA(150, 180, 220));
            g.R.camRight = norm(cross(cam.fwd, cam.up)); g.R.camUp = cross(g.R.camRight, cam.fwd);
            g.R.drawScene(g.race, cam, false);
            gfx::endFrame();
            char b[128]; snprintf(b, sizeof b, "%s/prop_%s_%02d.ppm", out.c_str(), NM[p.model], k++);
            pc::saveShot(b);
            fprintf(stderr, "%s at %.0f %.0f (%.0f m from the track)\n", NM[p.model], p.p.x, p.p.y, sqrtf(bd));
        }
        return 0;
    }
    if (mode == "steer") {      // steer <track> <help 0-2>: AI drives 30 s, then full throttle + full stick lock for 3 s
        std::string tid = argv[2]; int help = atoi(argv[3]);
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        g.set.car = 3; g.set.laps = 3; g.set.opponents = 0;
        cfg.steerHelp = help;
        g.aiDrivesPlayer = true;
        g.startRace();
        for (float simT = 0; simT < 30; simT += 1.0f / 60) { Pad z{}; g.step(1.0f / 60, z); }
        g.aiDrivesPlayer = false;
        const Car& me = g.race.player();
        float v0 = me.v * 3.6f; bool spun = false; float maxSlip = 0, maxDrift = 0;
        for (float simT = 0; simT < 3; simT += 1.0f / 60) {
            Pad pp{}; if (getenv("FL_DPAD")) pp.right = true; else pp.stickX = 1.0f; pp.r = true;
            g.step(1.0f / 60, pp);
            spun |= me.spinning; maxSlip = fmaxf(maxSlip, me.slip); maxDrift = fmaxf(maxDrift, fabsf(me.drift));
        }
        printf("help %d: from %.0f km/h  spun %s  max slide %.2f rad  now %.0f km/h\n", help, v0, spun ? "YES" : "no", maxDrift, me.v * 3.6f);
        return 0;
    }
    if (mode == "replay") {     // replay <track> <outdir>: 40 s of racing, then the replay (TV + cockpit), then photo mode
        std::string tid = argv[2], out = argv[3];
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        g.set.car = 3; g.set.laps = 2; g.set.opponents = 11;
        g.aiDrivesPlayer = true;
        g.startRace();
        for (float simT = 0; simT < 44 && g.screen == SCR_RACE; simT += 1.0f / 30) { Pad z{}; g.step(1.0f / 30, z); }
        fprintf(stderr, "recorded %d frames of %d cars\n", g.repCount, g.repCars);
        Pad z{};
        g.screen = SCR_PAUSE; g.sel = 7; press(g, &Pad::cross);
        if (g.screen != SCR_REPLAY) { fprintf(stderr, "replay did not start\n"); return 1; }
        for (int f = 0; f < 150; f++) g.frame(1.0f / 30, z);        // 5 s in
        shot(g, out + "/replay_tv.ppm", 1.0f / 30);
        press(g, &Pad::triangle); press(g, &Pad::triangle); press(g, &Pad::triangle);   // cockpit
        for (int f = 0; f < 30; f++) g.frame(1.0f / 30, z);
        shot(g, out + "/replay_cockpit.ppm", 1.0f / 30);
        press(g, &Pad::right);                                     // another car
        press(g, &Pad::triangle);                                  // TV again
        for (int f = 0; f < 30; f++) g.frame(1.0f / 30, z);
        shot(g, out + "/replay_tv2.ppm", 1.0f / 30);
        float before = g.race.cars[g.repLivePlayer].s;
        press(g, &Pad::square);
        if (g.screen != SCR_PHOTO) { fprintf(stderr, "photo mode did not start\n"); return 1; }
        Pad turn{}; turn.stickX = 1; turn.stickY = -0.6f;
        for (int f = 0; f < 25; f++) g.frame(1.0f / 30, turn);
        press(g, &Pad::cross);                                     // hide the help
        shot(g, out + "/photo.ppm", 1.0f / 30);
        press(g, &Pad::circle); press(g, &Pad::circle);            // photo -> replay -> pause
        fprintf(stderr, "back on screen %d; player restored: %s\n", (int)g.screen, g.race.playerIdx == g.repLivePlayer && g.repLive.empty() ? "yes" : "no");
        (void)before;
        return 0;
    }
    if (mode == "race") {
        std::string tid = argv[2];
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        g.set.car = atoi(argv[3]);
        g.set.laps = atoi(argv[4]);
        g.set.opponents = atoi(argv[5]);
        std::string out = argv[6];
        std::vector<float> shots;
        for (int i = 7; i < argc; i++) shots.push_back((float)atof(argv[i]));
        if (getenv("EMR_DIFF")) g.set.difficulty = atoi(getenv("EMR_DIFF"));
        if (getenv("EMR_MIN")) g.set.raceMin = atoi(getenv("EMR_MIN"));       // timed race (real minutes)
        if (getenv("EMR_HOURS")) g.set.eventHours = (float)atof(getenv("EMR_HOURS"));
        if (getenv("EMR_WX")) g.set.weather = atoi(getenv("EMR_WX"));
        if (getenv("EMR_BLUEOFF") && *getenv("EMR_BLUEOFF")) Race::blueOff = true;
        if (getenv("EMR_NOHYBRID")) Race::hybridOff = true;
        if (getenv("EMR_START")) g.set.startType = atoi(getenv("EMR_START"));
        g.aiDrivesPlayer = true;
        g.startRace();
        if (g.screen != SCR_RACE) { fprintf(stderr, "race did not start\n"); return 1; }
        if (getenv("EMR_CLOCK")) g.race.startHour = (float)atof(getenv("EMR_CLOCK"));   // time of day at the start (22 = night)
        size_t si = 0;
        std::vector<int16_t> wav;
        float simT = -4;
        double updTime = 0; long updN = 0;
        const float dt = 1.0f / 30;
        while (g.screen == SCR_RACE && simT < 3600) {
            Pad p{};
            bool want = si < shots.size() && simT >= shots[si];
            if (want) {
                const char* names[3] = {"chase", "far", "cockpit"};
                for (int cm = 0; cm < 3; cm++) {
                    g.cam = cm;
                    g.R.camInit = false;
                    // EMR_WARM=n: n frames drawn before the shot (wiper, drops on the glass)
                    if (getenv("EMR_WARM") && cm == 2) for (int f = 0; f < atoi(getenv("EMR_WARM")); f++) { Pad z{}; g.frame(1.0f / 30, z); }
                    char b[64]; snprintf(b, sizeof b, "/race_%s_t%03d_%s.ppm", tid.c_str(), (int)shots[si], names[cm]);
                    shot(g, out + b, 0);
                }
                g.cam = CAM_CHASE;
                si++;
            }
            // update without drawing (fast)
            g.screen = SCR_RACE;
            { double t0 = sys::timeSec(); g.step(dt, p); updTime += sys::timeSec() - t0; updN++; }
            if (simT > 5 && simT < 13 && pc::mixFn) {
                int frames = (int)(44100 * dt);
                std::vector<int16_t> b(frames * 2);
                pc::mixFn(b.data(), frames);
                wav.insert(wav.end(), b.begin(), b.end());
            }
            simT += dt;
            // race control messages as they happen
            static std::string lastMsg;
            if (!g.race.msgs.empty() && lastMsg != g.race.msgs[0].text) {
                lastMsg = g.race.msgs[0].text;
                fprintf(stdout, "  [%6.1f s  clock %05.2f h  rain %.2f wet %.2f] %s\n", g.race.time, g.race.clockHour(), g.race.rain, g.race.wet, lastMsg.c_str());
            }
            if (getenv("EMR_SPOT")) {
                const Car& c = g.race.cars[1];
                float s0 = atof(getenv("EMR_SPOT"));
                if (c.s > s0 && c.s < s0 + 200 && c.lap < 2)
                    fprintf(stdout, "    s %.0f d %.2f hw %.1f v %.1f yaw %.2f slip %.2f curv %.4f line %.2f thr %.2f brk %.2f\n", c.s, c.d, g.race.track.pts[g.race.track.index(c.s)].hw, c.v, c.yaw, c.slip, g.race.track.curvAt(c.s), g.race.line[g.race.track.index(c.s)], c.throttle, c.brake);
            }
            if (getenv("EMR_TRACE")) {
                static int lastPit[64], lastLap[64];
                for (size_t i = 0; i < g.race.cars.size(); i++) {
                    const Car& c = g.race.cars[i];
                    if (c.number != atoi(getenv("EMR_TRACE"))) continue;
                    if (getenv("EMR_T0") && g.race.time > atof(getenv("EMR_T0")) && fmodf(g.race.time, 2.0f) < 1.0f / 30)
                        fprintf(stdout, "      t %.0f rel %.1f box %.1f v %.1f d %.2f yaw %.2f thr %.2f brk %.2f phase %d stuck %.1f\n", g.race.time, g.race.pitRel(c.s), g.race.pitRel(g.race.boxS(c)), c.v, c.d, c.yaw, c.throttle, c.brake, c.pitPhase, c.stuck);
                    if (c.pit != lastPit[i] || c.lap != lastLap[i])
                        fprintf(stdout, "    #%d t %.0f lap %d pit %d want %d tyre %s wear %.2f fuel %.2f wet %.2f plan %s/%d\n", c.number, g.race.time, c.lap, c.pit, c.wantPit, TYRE_NAME[c.tyre], c.wear, c.fuel, g.race.wet, TYRE_NAME[c.plan.tyre], c.plan.tyres);
                    lastPit[i] = c.pit; lastLap[i] = c.lap;
                }
            }
            if (getenv("EMR_YELLOW")) {   // 1: FCY at 30 s; 2: a slow zone 500 m ahead of the player at 30 s
                static bool called = false; static float maxIn = 0; static int zoneShots = 0; static float inT = 0;
                int mode = atoi(getenv("EMR_YELLOW"));
                if (!called && simT > 100 && !g.race.formation) { called = true; g.race.scCooldown = 0; if (mode == 1) g.race.callFcy(40); else g.race.addSlowZone(g.race.player().s + 500, 60); }
                bool any = false;
                for (auto& c : g.race.cars) if (c.pit == PIT_NONE && g.race.slowAt(c.s) && !c.player) { any = true; if (inT > 4) maxIn = fmaxf(maxIn, c.v * 3.6f); }
                inT = any ? inT + 1.0f / 30 : 0;
                if (called && zoneShots < 2 && g.race.slowAt(g.race.player().s) && inT > 6) {
                    for (int cm = 0; cm < 3; cm += 2) { g.cam = cm; g.R.camInit = false; char b[64]; snprintf(b, sizeof b, "/yellow%d_%s.ppm", mode, cm ? "cockpit" : "chase"); shot(g, out + b, 0); }
                    g.cam = CAM_CHASE; zoneShots = 2;
                }
                static int lastLog = -1;
                if ((int)simT % 10 == 0 && (int)simT != lastLog) { lastLog = (int)simT; fprintf(stderr, "t %3d fcy %.0f warn %.1f zones %d  AI max in zone %.0f km/h  player %.0f km/h pen %.0f\n", (int)simT, g.race.fcy, g.race.fcyWarn, (int)g.race.zones.size(), maxIn, g.race.player().v * 3.6f, g.race.player().penalty); }
            }
            if (getenv("EMR_BOARDSHOT") && simT > 3) {   // screenshots on the approach to the first few distance boards
                static int taken = 0; static float last = -100;
                const Car& me = g.race.player();
                for (const BrakeBoard& bd : g.race.track.boards) {
                    if (bd.metres != 300 || taken >= 3 || simT - last < 10) continue;
                    V3 rel = bd.p - me.pos; float ahead = dot(rel, me.fwd);
                    if (ahead > 28 && ahead < 40 && fabsf(dot(rel, me.left)) < 25) {
                        for (int cm = 0; cm < 3; cm += 2) {
                            g.cam = cm; g.R.camInit = false;
                            char b[64]; snprintf(b, sizeof b, "/board_%d_%s.ppm", taken, cm ? "cockpit" : "chase");
                            shot(g, out + b, 0);
                        }
                        g.cam = CAM_CHASE; taken++; last = simT;
                    }
                }
            }
            if (getenv("EMR_BLUESHOT")) {    // screenshots the first time the player's car is shown blue flags
                static int taken = 0; static float last = -99;
                const Car& me = g.race.player();
                if (taken < 3 && me.blue > 0 && me.blueGap < 70 && simT - last > 20 && me.pit == PIT_NONE) {
                    for (int cm = 0; cm < 2; cm++) {
                        g.cam = cm == 0 ? CAM_CHASE : CAM_COCKPIT; g.R.camInit = false;
                        char bf[64]; snprintf(bf, sizeof bf, "/blue_%d_%s.ppm", taken, cm ? "cockpit" : "chase");
                        shot(g, out + bf, 0);
                    }
                    g.cam = CAM_CHASE; taken++; last = simT;
                }
            }
            if (getenv("EMR_TRAFFIC")) {      // seconds a faster-class car spends stuck right behind a GT
                static double stuck = 0, frames = 0;
                for (auto& c : g.race.cars) {
                    if (c.def->cls != CLASS_HYPER || c.pit != PIT_NONE) continue;
                    for (auto& o : g.race.cars)
                        if (o.def->cls == CLASS_GT3 && o.pit == PIT_NONE) { float gp = g.race.lapGap(c.s, o.s); if (gp > 0 && gp < 12 && fabsf(o.d - c.d) < 2.5f) { stuck += dt; break; } }
                }
                frames += dt;
                if (g.race.over || simT + dt >= 3600) {}
                g.trafficStuck = stuck;
            }
            if (g.screen == SCR_RESULTS) break;
        }
        if (getenv("EMR_TRAFFIC")) {
            float dmg = 0; for (auto& c : g.race.cars) dmg += c.damage();
            fprintf(stdout, "traffic: hypercars stuck behind GTs %.1f car-seconds, total damage %.2f\n", g.trafficStuck, dmg);
        }
        writeWav((out + "/race_audio.wav").c_str(), wav);
        fprintf(stderr, "update cost: %.3f ms per 1/30 s frame (PC)\n", updTime * 1000 / (updN ? updN : 1));
        // results
        const Race& r = g.race;
        fprintf(stdout, "track %s  %.0f m  race time %.1f s\n", tid.c_str(), r.track.length, r.time);
        fprintf(stdout, "track rubber at the end %.2f (wet %.2f)\n", r.rubber, r.wet);
        std::vector<const Car*> order(r.cars.size());
        for (auto& c : r.cars) order[c.position - 1] = &c;
        for (auto* c : order) {
            char best[16]; formatTime(best, c->bestLap);
            fprintf(stdout, "P%-2d #%-3d %-10s %-12s laps %d  best %s  %s  stops %d tyre %s wear %.2f fuel %.2f dmg %.2f%s%s pen %.0f%s\n", c->position, c->number, c->def->make, c->def->model, c->lap, best,
                    c->def->cls == CLASS_HYPER ? "HY" : "GT", c->stops, TYRE_NAME[c->tyre], c->wear, c->fuel, c->damage(), c->noseOff ? " NOSE-OFF" : "", c->wingOff ? " WING-OFF" : "", c->penalty, c->player ? "  <- player" : "");
        }
        if (getenv("EMR_DRIVERS"))
            for (auto* c : order) {
                fprintf(stdout, "    #%-3d", c->number);
                for (int k = 0; k < DRIVERS_PER_CAR; k++) {
                    char nm[24]; Race::driverName(*c, k, nm, sizeof nm, true);
                    fprintf(stdout, "  %-18s %4.1f h tired %3d%%%s", nm, c->drv[k].hours, (int)(c->drv[k].fatigue * 100), k == c->driver ? "*" : " ");
                }
                fprintf(stdout, "\n");
            }
        return 0;
    }
    return 1;
}
