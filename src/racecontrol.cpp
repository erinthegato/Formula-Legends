#include <cstddef>
// Race control and the team: starts (rolling / standing / Le Mans), the safety car, penalties,
// weather, tyres, fuel, pit stops, damage, flying parts, smoke and spray, and saving a race.
#include "race.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>

const char* TYRE_NAME[TY_COUNT] = {"Soft", "Medium", "Hard", "Inter", "Wet"};
const char* MAP_NAME[MAP_COUNT] = {"Safety car", "Normal", "Attack"};
const char* RADIO_FILE[RADIO_COUNT] = {"bestlap", "box", "box_ack", "contact", "damage", "drivethrough", "finish",
    "formation", "fuel", "giveback", "go", "green", "lastlap", "lemans", "limits", "overtake", "penalty", "sc", "sc_in", "tyres"};
const char* SFX_FILE[SFX_COUNT] = {"pit_airjack_up", "pit_jack_drop", "pit_refuel", "pit_wheelgun", "podium_champagne",
    "podium_crowd", "thunder_close", "thunder_mid", "thunder_far1", "thunder_far2"};

static uint32_t rseed = 99;
static float rnd() { rseed = rseed * 1664525u + 1013904223u; return (rseed >> 8) * (1.0f / 16777216.0f); }

static const float PIT_LIMIT = 22.22f;      // 80 km/h (Formula 1)
static const float LANE_LEN = PIT_LANE_LEN; // pit entry .. pit exit (track.h)

// ------------------------------------------------------------------ clock and conditions
float Race::clockHour() const {
    float race = (time - greenTime) * timeScale;
    if (race < 0) race = 0;
    return fmodf(startHour + race / 3600.0f, 24.0f);
}

float Race::nightLevel() const {
    float n = sunLevel();
    // Formula 1 races after dark run under floodlights: a dark sky over a lit circuit
    return fminf(n, 0.32f);
}

float Race::sunLevel() const {
    float h = clockHour();
    // sunrise 5:45-7:00, sunset 18:15-19:15 (the night races are near the equator or in autumn:
    // Singapore at 20:00 is fully dark; it used to read as dusk)
    if (h >= 7.0f && h <= 18.25f) return 0;
    if (h > 5.75f && h < 7.0f) return 1 - (h - 5.75f) / 1.25f;
    if (h > 18.25f && h < 19.25f) return (h - 18.25f) / 1.0f;
    return 1;
}

float Race::raceLeft() const {
    if (session != SES_RACE || set.raceMin <= 0) return -1;
    if (formation || countdown > 0) return set.raceMin * 60.0f;
    return fmaxf(0, set.raceMin * 60.0f - (time - greenTime));
}

int Race::bestTyreFor(float w) const { return w < 0.22f ? TY_MEDIUM : (w < 0.62f ? TY_INTER : TY_WET); }

float Race::tyreGrip(const Car& c) const {
    float w = wet, g;
    switch (c.tyre) {
        case TY_SOFT:   g = 1.03f * (1 - 0.6f * w); break;
        case TY_MEDIUM: g = 1.00f * (1 - 0.6f * w); break;
        case TY_HARD:   g = 0.975f * (1 - 0.6f * w); break;
        case TY_INTER:  g = 0.90f * (1 - 0.12f * w) - 0.3f * fmaxf(0, w - 0.6f); break;
        default:        g = 0.84f * (1 - 0.05f * w); break;
    }
    g *= 1 - 0.10f * c.wear - 0.25f * fmaxf(0, c.wear - 0.85f) / 0.15f;
    float f = c.drv[c.driver].fatigue;
    g *= 1 - 0.05f * f * f;                      // a tired driver can't use all the grip
    // temperature: full grip between 80 and 105 deg C, up to 8% less when cold, a little less when cooked
    float T = c.tyreTemp();
    if (T < 80) g *= 1 - fminf(0.08f, (80 - T) * 0.0025f);
    else if (T > 105) g *= 1 - fminf(0.06f, (T - 105) * 0.003f);
    return fmaxf(g, 0.3f);
}

// ------------------------------------------------------------------ drivers: three per car, fatigue, stint rules
static const char* FIRST[] = {"Anna", "Luca", "Mika", "Sofia", "Kenji", "Elena", "Mateo", "Nadia", "Tom", "Ines",
    "Rafael", "Yuki", "Oskar", "Lea", "Diego", "Freya", "Jonas", "Amara", "Pierre", "Hana", "Marco", "Clara", "Andre", "Zoe",
    "Felix", "Maya", "Bruno", "Ida", "Kai", "Lena", "Hugo", "Nora"};
static const char* LAST[] = {"Marchetti", "Lindqvist", "Okafor", "Brandt", "Sato", "Moreau", "Vidal", "Kowalski", "Hart",
    "Ferreira", "Tanaka", "Novak", "Duval", "Rossi", "Becker", "Ortega", "Nakamura", "Holm", "Laurent", "Costa", "Weber",
    "Ivanova", "Delacroix", "Fischer", "Mendes", "Varga", "Kaur", "Albers", "Silva", "Mori", "Janssen", "Reyes", "Strand",
    "Keller", "Bianchi", "Ahmadi", "Pereira", "Lund", "Okoye", "Castell"};
static const int NFIRST = sizeof(FIRST) / sizeof(FIRST[0]), NLAST = sizeof(LAST) / sizeof(LAST[0]);

void Race::crewDrivers(Car& c) {
    // seeded by the car number, so a car keeps the same three drivers in every session and race
    uint32_t h = (uint32_t)c.number * 2654435761u + 12345u;
    auto next = [&]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return h; };
    for (int k = 0; k < DRIVERS_PER_CAR; k++) {
        Driver& d = c.drv[k];
        d.first = (uint8_t)(next() % NFIRST);
        d.last = (uint8_t)((c.number * 7 + k * 13 + next() % 3) % NLAST);
        d.pace = 0.988f + (next() % 1000) / 1000.0f * 0.012f;       // up to ~1.2 % apart
        d.stamina = 0.8f + (next() % 1000) / 1000.0f * 0.45f;       // how long they last before tiring
        d.fatigue = 0; d.hours = 0;
    }
    if (c.player) { c.drv[0].pace = 1; c.drv[0].stamina = 1.0f; }
    c.driver = 0; c.stint = 0;
}

void Race::driverName(const Car& c, int k, char* out, int n, bool full) {
    if (c.player && k == 0) { snprintf(out, n, "You"); return; }
    const Driver& d = c.drv[k];
    if (full) snprintf(out, n, "%s %s", FIRST[d.first % NFIRST], LAST[d.last % NLAST]);
    else snprintf(out, n, "%c. %s", FIRST[d.first % NFIRST][0], LAST[d.last % NLAST]);
}

int Race::freshestDriver(const Car& c, int except) const {
    int best = -1; float bf = 1e9f;
    for (int k = 0; k < DRIVERS_PER_CAR; k++) {
        if (k == except || c.drv[k].hours >= MAX_DRIVE_H - 0.5f) continue;
        float score = c.drv[k].fatigue + 0.08f * c.drv[k].hours;   // fresh, and share the driving out
        if (score < bf) { bf = score; best = k; }
    }
    return best < 0 ? (except + 1) % DRIVERS_PER_CAR : best;
}

void Race::driverUpdate(Car& c, float dt) {
    // Formula 1: one driver per car, no fatigue or driving-time limits. An AI driver still makes the
    // odd slip under pressure late in a race or in the wet: runs wide, loses a second.
    if (session != SES_RACE || countdown > 0) return;
    if (!c.human && c.pit == PIT_NONE && c.mistake <= 0 && !formation && !sc.state) {
        float rate = 0.0012f * (1 + 2.5f * wet);
        if (rnd() < rate * dt) { c.mistake = 0.8f + rnd() * 1.2f; c.mistakeSide = rnd() < 0.5f ? -1.0f : 1.0f; }
    }
    if (c.mistake > 0) c.mistake -= dt;
}

static void swapDriver(Car& c) {
    if (c.plan.nextDriver == c.driver) return;
    c.driver = c.plan.nextDriver; c.stint = 0; c.stintWarn = 0; c.tiredWarn = 0;
}

void Race::message(const char* text, u32 color, int radio) {
    RaceMsg m;
    snprintf(m.text, sizeof(m.text), "%s", text);
    m.time = 4.5f; m.color = color;
    msgs.insert(msgs.begin(), m);
    if (msgs.size() > 4) msgs.pop_back();
    if (radio >= 0) radioPending = radio;
}

// ------------------------------------------------------------------ pit lane layout
void Race::setupPit() {
    // the pit lane runs down the left of the main straight: in 290 m before the line, out 190 m after
    // (Formula 1 pit lanes are short: the 11 garages of one era fill 176 m)
    pitIn = track.wrap(-PIT_IN_BEFORE);
    pitOut = track.wrap(LANE_LEN - PIT_IN_BEFORE);
    pitWallFrom = track.wrap(-PIT_IN_BEFORE + 80.0f);
    pitWallTo = track.wrap(LANE_LEN - PIT_IN_BEFORE - 70.0f);
    float hw = track.pts[0].hw;
    // the wall at hw + 2.3; the teams' pit wall stands behind it (to hw + 4.9); the fast lane, then the boxes
    laneD = hw + 8.8f;
    boxD = hw + 12.3f;
}

float Race::pitRel(float s) const {
    float r = track.wrap(s - pitIn);
    if (r > track.length - 300) r -= track.length;    // a little before the entry reads negative
    return r;
}

bool Race::inPitLane(const Car& c) const {
    if (c.pit == PIT_NONE) return false;
    float r = pitRel(c.s);
    return r > -20 && r < LANE_LEN + 10;
}

float Race::gap(const Car& c) const {
    if (c.lastMark < 0 || c.lastMark >= (int)markFirst.size() || markFirst[c.lastMark] < 0) return -1;
    return fmaxf(0, c.markTime - markFirst[c.lastMark]);
}

int Race::lapsDown(const Car& c) const {
    float lead = c.total;
    for (auto& o : cars) lead = fmaxf(lead, o.total);
    return (int)((lead - c.total) / track.length);
}

float Race::lapFuel(const Car& c) const {
    float tankKm = c.def->cls == CLASS_HYPER ? 180.0f : 190.0f;
    return track.length / (tankKm * 1000) * consumeScale;
}

// ------------------------------------------------------------------ blue flags and multi-class traffic
bool Race::faster(const Car& a, const Car& b) const {
    if (a.def->cls == CLASS_HYPER && b.def->cls == CLASS_GT3) return true;
    return session == SES_RACE && a.total - b.total > track.length * 0.5f;     // lapping it
}

float Race::nextBend(float s, float range) const {
    float best = 0;
    for (float x = 20; x < range; x += 10) {
        float k = track.curvAt(s + x);
        if (fabsf(k) > fabsf(best)) best = k;
        if (fabsf(best) > 1.0f / 120 && fabsf(k) < fabsf(best) * 0.5f) break;   // past the first real corner
    }
    return best;
}

bool Race::blueOff = false;
bool Race::hybridOff = false;

void Race::blueFlags(float dt) {
    bool off = formation || sc.state || countdown > 0 || blueOff;
    for (auto& o : cars) {
        o.blueMsgT = fmaxf(0, o.blueMsgT - dt);
        float was = o.blue;
        o.blue = fmaxf(0, o.blue - dt); o.yieldSide = 0;
        if (off || o.pit != PIT_NONE || o.finished) { o.blue = 0; continue; }
        // the closest faster car behind, within ~2 s
        int by = -1; float bg = 1e9f;
        for (int i = 0; i < (int)cars.size(); i++) {
            const Car& c = cars[i];
            if (&c == &o || c.pit != PIT_NONE || c.finished) continue;
            float g = lapGap(c.s, o.s);                  // o is g metres ahead of c on the lap
            if (g <= 0 || g > fmaxf(60.0f, c.v * 2.0f) || g > bg) continue;
            if (!faster(c, o) || c.v < o.v - 3) continue;
            by = i; bg = g;
        }
        if (by < 0) { if (o.player) o.heldUp = fmaxf(0, o.heldUp - dt * 0.5f); continue; }
        o.blue = 1.0f; o.blueBy = by; o.blueGap = bg;
        const Car& c = cars[by];
        if (o.player && was <= 0 && o.blueMsgT <= 0) {
            o.blueMsgT = 25;
            message(c.def->cls != o.def->cls ? "Blue flag: Hypercar behind, let it by" : "Blue flag: the leaders are lapping you", RGBA(80, 150, 255));
        }
        // the player holding a faster car up: close behind and not getting by
        if (o.player && o.human && bg < 25 && c.v < o.v + 2) {
            o.heldUp += dt;
            if (o.heldUp > 12) {
                o.heldUp = 0; o.blueWarn++;
                if (o.blueWarn >= 3) { o.blueWarn = 0; o.driveThrough = true; o.dtLaps = 3; message("Blue flags ignored: drive-through", RGBA(255, 90, 60), RADIO_DRIVETHROUGH); }
                else { char b[64]; snprintf(b, sizeof b, "Blue flags ignored: warning %d/2", o.blueWarn); message(b, RGBA(255, 200, 60)); }
            }
        }
        // an AI car makes room in a predictable way: keeps its line through corners, and on the
        // straight before a corner moves over to the inside of it, so the faster car keeps the racing line
        if (!o.human) {
            float k = nextBend(o.s, 260);
            bool straightNow = fabsf(track.curvAt(o.s)) < 1.0f / 350 && fabsf(track.curvAt(o.s + 30)) < 1.0f / 300;
            if (straightNow) {
                float side = fabsf(k) > 1.0f / 600 ? (k > 0 ? 1.0f : -1.0f) : (c.d > o.d ? -1.0f : 1.0f);
                // only if that side is clear: no car alongside or just ahead / behind over there
                for (auto& x : cars) {
                    if (&x == &o || x.pit != PIT_NONE) continue;
                    float g = lapGap(o.s, x.s);
                    if (g < -14 || g > 18) continue;
                    if ((x.d - o.d) * side > -1.0f && fabsf(x.d - o.d) < 4.5f && &x != &c) { side = 0; break; }
                    if (&x == &c && fabsf(g) < 8 && (x.d - o.d) * side > 0) { side = 0; break; }   // it is already alongside on that side
                }
                o.yieldSide = side;
            }
        }
    }
}

float Race::boxS(const Car& c) const {
    return track.wrap(pitIn + PIT_BOX0 + pitSlot(c.def) * 16.0f + (c.number % 2) * 6.0f);
}

bool Race::playerAtBox() const { return session == SES_RACE && player().pit == PIT_STOP && player().pitPhase == -2; }

void Race::releasePlayer() {
    Car& c = player();
    if (c.pit != PIT_STOP || c.pitPhase != -2) return;
    c.pitPhase = 0; c.pitTimer = 0;
    qte = PitQte();
    radioPending = RADIO_BOX_ACK;
    if (c.plan.aiDrives) message("The team takes over: manage the race", RGBA(120, 200, 255));
}

// ------------------------------------------------------------------ garage (practice / qualifying), engine map
void Race::garageCar(Car& c, int phase, float delay) {
    float L = track.length;
    c.s = boxS(c); c.d = garageD(); c.v = 0; c.yaw = -1.5708f; c.drift = 0; c.yawRate = 0; c.gear = 1; c.steer = 0;
    // keep the distance covered in step with the new place on the lap
    float cand = floorf(c.total / L) * L + c.s;
    if (cand - c.total > L * 0.5f) cand -= L;
    if (c.total - cand > L * 0.5f) cand += L;
    c.total = cand;
    c.pit = PIT_STOP; c.pitPhase = phase; c.pitTimer = -delay; c.wantPit = false; c.outLap = true;
    c.lift = 0.12f; c.engineOn = false; c.rpm = 0;
    pose(c);
}

void Race::toGarage() {
    if (session == SES_RACE) return;
    garageCar(player(), -2, 0);
}

static void applySetup(Race& r, Car& c, const CarSetup& su) {
    c.su = su;
    c.tyre = r.wet > 0.22f ? r.bestTyreFor(r.wet) : (su.tyre <= TY_HARD ? su.tyre : TY_MEDIUM);
    c.wear = 0; c.tyreLaps = 0; c.coldTyres(); c.fuel = clampf(su.fuel / 100.0f, 0.1f, 1.0f);
    for (float& d : c.dmg) d = 0;
    c.noseOff = c.wingOff = false;
}

void Race::leaveGarage(const CarSetup& su) {
    Car& c = player();
    if (c.pit != PIT_STOP || c.pitPhase != -2) return;
    applySetup(*this, c, su);
    c.pitPhase = -3; c.pitTimer = 0;
}

void Race::flyingLap(const CarSetup& su) {
    Car& c = player();
    if (c.pit != PIT_STOP || c.pitPhase != -2) return;
    applySetup(*this, c, su);
    // 900 m before the line at racing speed: the next crossing starts a timed lap
    float L = track.length;
    float back = fminf(900.0f, L * 0.25f);
    c.s = track.wrap(-back);
    c.total = (floorf(c.total / L) + 1) * L - back;
    int i = track.index(c.s);
    c.d = line[i]; c.yaw = 0; c.drift = 0; c.lift = 0;
    c.v = vmax[c.def->cls][i] * 0.85f; c.gear = 6;
    c.pit = PIT_NONE; c.pitPhase = 0; c.outLap = true; c.engineOn = true;
    pose(c);
}

// garage moves: in from the box, and out: off the jacks, engine on, pushed to the box
static bool garageMove(Race& r, Car& c, float dt, std::vector<int>& sfx) {
    if (c.pitPhase == -2) { c.lift = 0.12f; c.engineOn = false; c.rpm = 0; return true; }
    if (c.pitPhase == -4) {
        c.d = fminf(r.garageD(), c.d + 2.2f * dt);
        float k = clampf((c.d - r.boxD) / (r.garageD() - r.boxD), 0, 1);
        c.yaw = -1.5708f * k;
        c.rpm = c.def->idleRpm;
        if (k >= 1) {
            c.engineOn = false; c.lift = 0.12f;
            if (c.player) sfx.push_back(SFX_AIRJACK);
            c.pitPhase = c.player && !r.autoPlayer ? -2 : -3; c.pitTimer = -5;
        }
        return true;
    }
    if (c.pitPhase != -3) return false;
    c.pitTimer += dt;
    float t = c.pitTimer;
    if (t < 0) { c.lift = 0.12f; return true; }                    // waiting for its turn
    if (t < 1.0f) {                                                // down off the jacks
        if (t - dt < 0 && c.player) sfx.push_back(SFX_JACKDROP);
        c.lift = 0.12f * (1 - t);
        return true;
    }
    c.lift = 0;
    if (t < 2.6f) {                                                // engine on
        c.engineOn = true;
        float blip = t < 1.9f ? (t - 1.0f) / 0.9f : 1 - (t - 1.9f) / 0.7f;
        c.rpm = c.def->idleRpm + (c.def->maxRpm - c.def->idleRpm) * 0.45f * clampf(blip, 0, 1);
        return true;
    }
    // pushed out by the crew, turning into the lane
    c.rpm = c.def->idleRpm;
    c.d = fmaxf(r.boxD, c.d - 1.6f * dt);
    float k = clampf((c.d - r.boxD) / (r.garageD() - r.boxD), 0, 1);
    c.yaw = -1.5708f * k * k;
    if (c.d <= r.boxD + 0.01f) { c.yaw = 0; c.pit = PIT_OUT; c.pitPhase = 0; c.v = 2; c.outLap = true; }
    return true;
}

void Race::cycleMap() {
    Car& c = player();
    c.su.map = (c.su.map + 1) % MAP_COUNT;
    char b[48]; snprintf(b, sizeof b, "Engine map: %s", MAP_NAME[c.su.map]);
    message(b, c.su.map == MAP_ATTACK ? RGBA(255, 120, 60) : (c.su.map == MAP_SC ? RGBA(255, 215, 40) : RGBA(255, 255, 255)));
}

// ------------------------------------------------------------------ starts
void Race::setupStart() {
    rseed = 1777 + set.track * 31 + (uint32_t)time;
    int n = (int)cars.size();
    float hw = track.pts[0].hw;
    formation = false; countdown = 0; greenTime = 0;
    for (auto& c : cars) { c.startDelay = 0; c.v = 0; c.yaw = 0; c.lastPos = 0; }
    if (session != SES_RACE) {
        // practice / qualifying: the field spread round the lap, the player just before the line
        // practice / qualifying: every car starts in its garage and the crews push them out in turn
        std::swap(cars[0], cars[playerIdx]);
        playerIdx = 0;
        for (int k = 0; k < n; k++) { cars[k].total = 0; garageCar(cars[k], -3, k == 0 ? 0 : 3 + rnd() * (session == SES_QUALI ? 40 : 70)); }
        return;
    }
    int type = set.startType;
    if (type == START_EVENT || type == START_LEMANS) type = START_STANDING;      // Formula 1: lights out from the grid
    if (type == START_ROLLING) {
        // two by two behind the pace car, 1 km before the line; green when the leader gets there
        formation = true;
        for (int k = 0; k < n; k++) {
            float back = 1000.0f + 14.0f * k;
            cars[k].s = track.wrap(-back); cars[k].total = -back;
            cars[k].d = (k % 2 == 0 ? 1.0f : -1.0f) * hw * 0.42f;
            cars[k].v = 24; cars[k].gear = 3;
        }
        sc = SafetyCar();
        sc.state = 3;                       // pace car
        sc.total = cars[0].total + 30; sc.s = track.wrap(sc.total); sc.v = 24;
        message("Formation lap - hold your position", RGBA(255, 220, 80), RADIO_FORMATION);
    } else if (type == START_LEMANS) {
        // the classic start: cars angled along the pit wall, drivers run across the track to them
        countdown = 4;
        for (int k = 0; k < n; k++) {
            float back = 40.0f + 7.0f * k;
            cars[k].s = track.wrap(-back); cars[k].total = -back;
            cars[k].d = hw - 2.2f; cars[k].yaw = 0.5f;
            cars[k].startDelay = 2.2f + rnd() * 1.6f + k * 0.03f;
        }
        cars[playerIdx].startDelay = 2.6f;
        message("Le Mans start! Drivers run to their cars", RGBA(255, 220, 80), RADIO_LEMANS);
    } else {
        countdown = 4;
        for (int k = 0; k < n; k++) {
            // F1 grid: 8 m between grid slots, staggered left and right
            float back = 10.0f + 8.0f * k;
            cars[k].s = track.wrap(-back); cars[k].total = -back;
            cars[k].d = (k % 2 == 0 ? 1.0f : -1.0f) * hw * 0.34f;
        }
    }
}

// ------------------------------------------------------------------ weather
// rubber on the racing line: up to +3 % grip on it, marbles cost up to 5 % more than 4.5 m off it
float Race::rubberGrip(const Car& c) const {
    if (rubber <= 0.01f) return 1.0f;
    float off = fabsf(c.d - line[track.index(c.s)]);
    float dryK = 1 - clampf(wet * 2.5f, 0, 1);
    if (off < 3.0f) return 1 + 0.03f * rubber * (1 - off / 3.0f) * dryK;
    if (off > 4.5f) return 1 - 0.05f * rubber * clampf((off - 4.5f) / 2.0f, 0, 1) * dryK;
    return 1.0f;
}

void Race::weather(float dt) {
    // rubber: every km driven lays a little (about 1200 car-km from green to fully rubbered in);
    // a wet track washes it off over a few minutes
    {
        float km = 0;
        for (const Car& c : cars) if (c.pit == PIT_NONE) km += c.v * dt * 0.001f;
        rubber = clampf(rubber + km / 1200.0f * (1 - clampf(wet * 3, 0, 1)) - wet * 0.004f * dt * fminf(timeScale, 8.0f), 0, 1);
        int step = (int)(rubber * 4);
        if (step > rubberMsg && session == SES_RACE) message(step >= 3 ? "Track fully rubbered in: more grip on the line" : "The track is rubbering in", RGBA(200, 200, 200));
        if (step != rubberMsg) rubberMsg = step;
    }
    if (set.weather == WX_SIMULATED) {
        rainTimer -= dt * fminf(timeScale, 8.0f);
        if (rainTimer <= 0) {
            float r = rnd();
            rainTarget = r < 0.62f ? 0 : (r < 0.88f ? 0.2f + rnd() * 0.35f : 0.6f + rnd() * 0.4f);
            rainTimer = 900 + rnd() * 2700;      // race seconds
            if (rainTarget > 0 && rain < 0.05f && session == SES_RACE) message("Rain expected in a few minutes", RGBA(140, 190, 255));
        }
        storm = rainTarget > 0.75f;
    } else if (set.weather == WX_DRY) rainTarget = 0;
    else if (set.weather == WX_WET) rainTarget = 0.6f;
    else {
        // thunderstorm: waves of heavy rain, now and then torrential
        storm = true;
        rainTimer -= dt * fminf(timeScale, 8.0f);
        if (rainTimer <= 0) { rainTarget = 0.55f + rnd() * 0.42f; rainTimer = 300 + rnd() * 900; }
        if (rainTarget < 0.5f) rainTarget = 0.7f;
    }
    // the clouds take a while to arrive or clear
    float rate = 0.015f * dt * fminf(1 + timeScale * 0.05f, 3.0f);
    if (rain < rainTarget) rain = fminf(rainTarget, rain + rate); else rain = fmaxf(rainTarget, rain - rate);
    // the track gets wet quickly and dries slowly (faster with a dry line in a long race)
    if (rain > wet) wet = fminf(1, wet + (rain - wet) * 0.05f * dt * fminf(1 + timeScale * 0.1f, 4.0f));
    else wet = fmaxf(rain, wet - 0.0025f * dt * fminf(1 + timeScale * 0.1f, 4.0f));
    // lightning and thunder (the sound arrives later: 343 m/s)
    lightning = fmaxf(0, lightning - dt * 5);
    if (storm && rain > 0.5f) {
        nextStrike -= dt;
        if (nextStrike <= 0) {
            float dist = 250 + rnd() * 3800;
            if (dist < 1400) lightning = 1;
            else lightning = fmaxf(lightning, 0.5f);
            nextStrike = 5 + rnd() * 16;
            thunderDelay = dist / 343.0f;
            thunderPending = dist < 700 ? SFX_THUNDER_CLOSE : dist < 1800 ? SFX_THUNDER_MID : (rnd() < 0.5f ? SFX_THUNDER_FAR1 : SFX_THUNDER_FAR2);
        }
    }
    if (thunderPending >= 0) {
        thunderDelay -= dt;
        if (thunderDelay <= 0) { sfxEvents.push_back(thunderPending); thunderPending = -1; }
    }
}

// ------------------------------------------------------------------ safety car / pace car
void Race::moveSafetyCar(float dt) {
    if (!sc.state) return;
    // the leader on track (not in the pits)
    const Car* lead = nullptr;
    for (auto& c : cars) if (c.pit == PIT_NONE && c.total < sc.total && (!lead || c.total > lead->total)) lead = &c;
    int i = track.index(sc.s);
    float vt = sc.state == 3 ? 24.0f : fminf(vmax[0][i] * 0.62f, 46.0f);
    if (lead) {
        float gap = sc.total - lead->total;
        if (sc.state != 3 && gap > 150) vt = fminf(vt, 18);     // let the queue catch up
        if (gap < 12) vt = fmaxf(vt, lead->v + 2);
    }
    sc.v += clampf(vt - sc.v, -8 * dt, 5 * dt);
    sc.s = track.wrap(sc.s + sc.v * dt);
    float lapBefore = floorf(sc.total / track.length);
    sc.total += sc.v * dt;
    sc.d += (line[i] * 0.5f - sc.d) * fminf(1, dt);
    if (floorf(sc.total / track.length) > lapBefore && sc.state == 1) {
        sc.lapsLeft--;
        bool clear = true;
        for (auto& c : cars) if (c.stuck > 1) clear = false;
        if (sc.rain && rain > 0.7f) clear = false;
        if (sc.lapsLeft <= 0 && clear) { sc.state = 2; message("Safety car in this lap", RGBA(255, 220, 60), RADIO_SC_IN); }
    }
    // pulls off into the pit lane
    float r = pitRel(sc.s);
    if ((sc.state == 2 || sc.state == 3) && r >= 0 && r < 40 && sc.total > -900) {
        if (sc.state == 2) { message("GREEN FLAG - racing resumes", RGBA(60, 255, 90), RADIO_GREEN); scCooldown = 150; }
        sc.state = 0;
    }
    V3 p, t, l; float hw;
    track.frame(sc.s, p, t, l, hw);
    sc.fwd = t; sc.left = l; sc.up = cross(t, l);
    sc.pos = p + l * sc.d;
}

static void deploySC(Race& r, bool rain);
void Race::testSafetyCar() { deploySC(*this, false); }   // (the PC test: fl_test screel)
static void deploySC(Race& r, bool rain) {
    if (r.sc.state || r.session != SES_RACE || r.scCooldown > 0 || r.formation) return;
    if (r.era == ERA_90S) {
        // 1980: no safety car in Formula 1 (it came in 1993); the marshals wave double yellows and the
        // race goes on (the scCooldown keeps the message from repeating)
        r.scCooldown = 40;
        r.message(rain ? "Heavy rain - race continues" : "Double yellow flags", RGBA(255, 220, 60));
        return;
    }
    const Car* lead = nullptr;
    for (auto& c : r.cars) if (c.pit == PIT_NONE && (!lead || c.total > lead->total)) lead = &c;
    if (!lead) return;
    r.sc = SafetyCar();
    r.sc.state = 1; r.sc.rain = rain;
    r.sc.total = lead->total + 180; r.sc.s = r.track.wrap(r.sc.total); r.sc.v = 30;
    r.sc.lapsLeft = rain ? 2 : 1;
    r.message(rain ? "SAFETY CAR - torrential rain" : "SAFETY CAR deployed", RGBA(255, 220, 60), RADIO_SC);
}

// ------------------------------------------------------------------ FCY and slow zones
bool Race::zoneAt(float s) const {
    for (const SlowZone& z : zones) if (track.wrap(s - z.from) <= track.wrap(z.to - z.from)) return true;
    return false;
}
bool Race::slowAt(float s) const { return (fcy > 0 && fcyWarn <= 0) || zoneAt(s); }

void Race::callFcy(float seconds) {
    if (session != SES_RACE || formation || sc.state || fcy > 0 || fcyWarn > 0 || scCooldown > 0) return;
    if (!ep().vsc) { deploySC(*this, false); return; }      // before 2015: the safety car
    fcyWarn = 3; fcy = seconds;       // fcy counts down only once the warning is over
    message("VIRTUAL SAFETY CAR deployed", RGBA(255, 215, 40), RADIO_SC);
}

// a slow zone from ~300 m before the spot to just after it
void Race::addSlowZone(float s, float seconds) {
    if (session != SES_RACE || formation || sc.state || fcy > 0) return;
    (void)seconds;
    return;                           // Formula 1: no slow zones (double yellows only)
    float from = track.wrap(s - 320), to = track.wrap(s + 80);
    for (SlowZone& z : zones) if (track.wrap(s - z.from) <= track.wrap(z.to - z.from) + 50) { z.left = fmaxf(z.left, seconds); return; }
    if (zones.size() >= 3) return;
    zones.push_back({from, to, seconds});
    const Car& me = player();
    float ahead = track.wrap(from - me.s);
    char t[64];
    if (ahead < track.length * 0.5f) snprintf(t, sizeof t, "SLOW ZONE %d m ahead - 80 km/h", ((int)ahead / 100) * 100 + 100);
    else snprintf(t, sizeof t, "SLOW ZONE on track - 80 km/h");
    message(t, RGBA(255, 215, 40));
}

// ------------------------------------------------------------------ race control
void Race::raceControl(float dt) {
    for (auto& m : msgs) m.time -= dt;
    while (!msgs.empty() && msgs.back().time <= 0) msgs.pop_back();
    scCooldown -= dt;
    bool wasFormation = formation;
    moveSafetyCar(dt);
    // rolling start: green when the leader reaches the line
    if (formation) {
        const Car* lead = nullptr;
        for (auto& c : cars) if (!lead || c.total > lead->total) lead = &c;
        if (lead && lead->total >= 0) {
            formation = false; greenTime = time;
            if (sc.state == 3) sc.state = 0;
            message("GREEN FLAG - GO GO GO", RGBA(60, 255, 90), RADIO_GO);
        }
    }
    if (wasFormation && !formation) {
        for (auto& c : cars) c.lapStart = time;
    }
    if (session != SES_RACE) return;
    // FCY: warning, then the clock; slow zones run out
    if (fcyWarn > 0) {
        fcyWarn -= dt;
        if (fcyWarn <= 0) { zones.clear(); message("VSC - keep under 120 km/h, no overtaking", RGBA(255, 215, 40)); }
    } else if (fcy > 0) {
        float before = fcy;
        fcy -= dt;
        if (before > 5 && fcy <= 5) message("VSC ENDING", RGBA(255, 215, 40));
        if (fcy <= 0) { fcy = 0; scCooldown = 30; message("GREEN FLAG", RGBA(60, 255, 90), RADIO_GREEN); }
    }
    for (size_t k = 0; k < zones.size();) {
        zones[k].left -= dt;
        if (zones[k].left <= 0) { zones.erase(zones.begin() + k); if (zones.empty()) message("Slow zone over - green", RGBA(60, 255, 90)); }
        else k++;
    }
    if (sc.state) { fcy = 0; fcyWarn = 0; zones.clear(); }
    // stopped cars: a car stopped on the racing surface brings out the FCY (the safety car if it is a
    // wreck); one in the run-off gets a slow zone round it; the marshals push them back after a while
    for (int i = 0; i < (int)cars.size(); i++) {
        Car& c = cars[i];
        bool stopped = c.v < 2 && c.pit == PIT_NONE && !c.finished && c.startDelay <= 0 && !formation && time > 8;
        c.stuck = stopped ? c.stuck + dt : 0;
        bool onTrack = fabsf(c.d) < track.pts[track.index(c.s)].hw + 0.5f;
        if (c.stuck > 8 && onTrack) { if (c.damage() > 0.8f) deploySC(*this, false); else callFcy(50 + rnd() * 25); }
        if (c.stuck > 3 && !onTrack) addSlowZone(c.s, 40);
        if (c.stuck > (onTrack ? 14 : 7) && (!c.player || c.offTrack)) {
            c.d *= 0.3f; c.yaw = 0; c.v = 8; c.stuck = 0;
            if (c.player) message("Marshals put you back on track", RGBA(255, 220, 60));
        }
    }
    if (rain > 0.85f) deploySC(*this, true);
    if (sc.state == 1 && sc.rain) sc.lapsLeft = rain > 0.7f ? 1 : sc.lapsLeft;
}

void Race::penaltyCheck(Car& c, float dt) {
    if (session != SES_RACE || c.finished || formation) return;
    // track limits: the whole car past the kerb
    int i = track.index(c.s);
    const TrackPoint& tp = track.pts[i];
    c.limitCool -= dt;
    if (c.pit == PIT_NONE && fabsf(c.d) > tp.hw + (tp.kerb ? 1.3f : 0.3f) + (c.player && !autoPlayer ? 1.0f : 2.2f) && c.v > 15 && c.limitCool <= 0) {
        c.limitCool = 5;
        if (getenv("EMR_DBG")) fprintf(stderr, "limits #%d s %.0f d %.1f hw %.1f v %.1f wear %.2f slip %.2f wet %.2f off %d\n", c.number, c.s, c.d, tp.hw, c.v, c.wear, c.slip, wet, c.offTrack);
        c.limitWarn++;
        if (c.limitWarn > 5) {
            c.penalty += 5; c.limitWarn = 3;
            if (c.player) message("Track limits: +5 s penalty", RGBA(255, 120, 60), RADIO_PENALTY);
        } else if (c.player) {
            char t[64]; snprintf(t, sizeof t, "Track limits warning %d of 5", c.limitWarn);
            message(t, c.limitWarn == 5 ? RGBA(255, 255, 255) : RGBA(255, 200, 60), RADIO_LIMITS);
        }
    }
    if (!c.player || autoPlayer || c.plan.aiDrives) return;
    // FCY / slow zone: over 85 km/h for more than 2.5 s is a warning, the next time +5 s
    if (c.pit == PIT_NONE && slowAt(c.s) && c.v > slowV + 1.4f) {
        c.slowOver += dt;
        if (c.slowOver > 2.5f) {
            c.slowOver = 0;
            if (++c.slowWarn >= 2) { c.penalty += 5; c.slowWarn = 0; message("Speeding under the VSC: +5 s", RGBA(255, 120, 60), RADIO_PENALTY); }
            else message("Slow down - VSC 120 km/h! Next time: +5 s", RGBA(255, 200, 60), RADIO_LIMITS);
        }
    } else c.slowOver = fmaxf(0, c.slowOver - dt * 0.5f);
    // overtaking under the safety car: give the place back or take a drive-through
    if (c.lastPos == 0) c.lastPos = c.position;
    if (c.position < c.lastPos && c.pit == PIT_NONE) {
        if (sc.state == 1 || sc.state == 2 || formation || (fcy > 0 && fcyWarn <= 0)) {
            // only counts when the car passed was out on track
            bool fair = false;
            for (auto& o : cars) if (o.position == c.position + 1 && (o.pit != PIT_NONE || o.stuck > 0)) fair = true;
            if (!fair && c.giveBack <= 0) {
                c.giveBack = 25; c.giveBackPos = c.lastPos;
                message(sc.state || formation ? "Overtake under the safety car: give the place back" : "Overtake under the VSC: give the place back", RGBA(255, 200, 60), RADIO_GIVEBACK);
            }
        } else {
            static float lastCall = -100;
            if (time - lastCall > 40) { lastCall = time; radioPending = RADIO_OVERTAKE; }
        }
    }
    c.lastPos = c.position;
    if (c.giveBack > 0) {
        c.giveBack -= dt;
        if (c.position >= c.giveBackPos) { c.giveBack = 0; message("Place given back - thank you", RGBA(60, 255, 90)); }
        else if (c.giveBack <= 0) {
            c.driveThrough = true; c.dtLaps = 3;
            message("Drive-through penalty: serve within 3 laps", RGBA(255, 90, 60), RADIO_DRIVETHROUGH);
        }
    }
}

// ------------------------------------------------------------------ strategy (each lap)
void Race::strategy(Car& c) {
    if (session != SES_RACE || c.finished || c.pit != PIT_NONE) return;
    float lapFuel = track.length / (tankKm * 1000) * consumeScale;
    bool slick = c.tyre <= TY_HARD;
    int want = bestTyreFor(wet);
    // a margin either side of the crossover, so the call doesn't flip-flop
    bool wrongTyre = (slick && wet > 0.3f) || (!slick && wet < 0.12f) ||
                     (c.tyre == TY_INTER && wet > 0.75f) || (c.tyre == TY_WET && wet < 0.5f);
    // decided at the line: the car still has a whole lap to cover before it reaches the pit entry
    bool fuelLow = c.fuel < lapFuel * 2.15f;
    bool worn = c.wear > 0.72f;
    bool broken = c.damage() > 0.55f || c.wingOff || c.noseOff;
    float left = raceLeft();
    float lapTime = c.lastLap > 0 ? c.lastLap : 120;
    bool lastLaps = (left >= 0 && left < lapTime * 1.2f) || (left < 0 && c.lap >= set.laps - 1);
    bool driverDue = false;                  // (Formula 1: one driver per car)
    bool box = c.driveThrough || ((fuelLow || wrongTyre || worn || broken || driverDue) && !(lastLaps && !fuelLow));
    if (c.player && !autoPlayer) {
        // the engineer tells the player (they choose to come in with Select)
        if (c.driveThrough) return;
        if (fuelLow) message("Fuel is low - box this lap", RGBA(255, 200, 60), RADIO_FUEL);
        else if (wrongTyre) message(want == TY_MEDIUM ? "Track is drying - slicks now" : "It's raining - box for wet tyres", RGBA(140, 190, 255), RADIO_TYRES);
        else if (worn) message("Tyres are finished - box this lap", RGBA(255, 200, 60), RADIO_BOX);
        else if (broken) message("Damage - box for repairs", RGBA(255, 120, 60), RADIO_DAMAGE);
        else if (driverDue && !lastLaps) message("Driving time limit - box this lap for a driver change", RGBA(255, 200, 60), RADIO_BOX);
        return;
    }
    if (!box) return;
    c.wantPit = true;
}

// what the crew does at the stop, decided when the car arrives (the weather may have changed)
void Race::choosePlan(Car& c) {
    float lapFuel = track.length / (tankKm * 1000) * consumeScale;
    int want = bestTyreFor(wet);
    bool slick = c.tyre <= TY_HARD;
    bool wrongTyre = (want == TY_MEDIUM) != slick || (!slick && want != c.tyre);
    float left = raceLeft();
    float lapTime = c.lastLap > 0 ? c.lastLap : 120;
    c.plan.tyres = wrongTyre || c.wear > 0.4f;
    // dry: the compound that lasts the rest of the race: soft for a short final stint, then medium,
    // hard for a long one (tyre life is sized to the race distance, Race::tyreKm)
    if (want == TY_MEDIUM) {
        float lapsLeft = left >= 0 ? left / lapTime : (float)(set.laps - c.lap);
        float km = lapsLeft * track.length / 1000.0f;
        if (km < tyreKm[TY_SOFT] * 0.9f) c.plan.tyre = TY_SOFT;
        else if (km < tyreKm[TY_MEDIUM] * 0.9f) c.plan.tyre = TY_MEDIUM;
        else c.plan.tyre = TY_HARD;
    } else c.plan.tyre = want;
    // fuel: enough to finish if that is less than a tank
    float need;
    if (left >= 0) need = fminf(1, (left / lapTime + 1.5f) * lapFuel);
    else need = fminf(1, ((set.laps - c.lap) + 1.5f) * lapFuel);
    c.plan.fuel = ep().refuel ? fmaxf(need, c.fuel) : c.fuel;     // refuelling was banned from 2010
    c.plan.repair = c.damage() > 0.15f || c.wingOff || c.noseOff;
    c.plan.nextDriver = c.driver; c.plan.swapDriver = false;
    if (c.player) c.plan.aiDrives = false;
    return;
    // drivers: change after ~2.5 h, when the driver is getting tired, or before the time limits bite
    const Driver& d = c.drv[c.driver];
    // how long the next stint will be (race hours): the fuel going in, at this lap time
    float nextH = (lapFuel > 0 ? c.plan.fuel / lapFuel : 10) * lapTime * consumeScale / 3600.0f;
    if (left >= 0) nextH = fminf(nextH, left * consumeScale / 3600.0f);
    bool change = c.stint / 3600.0f + nextH > MAX_STINT_H - 0.2f || d.hours + nextH > MAX_DRIVE_H - 0.2f ||
                  d.fatigue > 0.55f || d.fatigue + nextH / (2.75f * d.stamina) > 0.85f;
    if (left >= 0 && left * consumeScale < 1800 && c.stint < (MAX_STINT_H - 0.6f) * 3600 && d.hours < MAX_DRIVE_H - 0.6f &&
        d.fatigue + nextH / (2.75f * d.stamina) < 0.9f) change = false;   // nearly over: whoever is in finishes
    c.plan.nextDriver = change ? freshestDriver(c, c.driver) : c.driver;
    c.plan.swapDriver = c.plan.nextDriver != c.driver;
    if (c.player) c.plan.aiDrives = c.plan.nextDriver != 0;   // a team-mate in the car: the AI drives, you manage
}

// ------------------------------------------------------------------ pit lane driving / stops
bool Race::pitDrive(Car& c, float dt) {
    (void)dt;
    float r = pitRel(c.s);
    if (c.pit == PIT_NONE) {
        if (!c.wantPit) return false;
        if (r >= -5 && r < 40) {
            c.pit = PIT_IN; c.wantPit = false;
            c.pitPhase = c.driveThrough ? -1 : 0;
            if (c.player) radioPending = RADIO_BOX_ACK;
        } else return false;
    }
    if (session == SES_RACE && c.v > PIT_LIMIT + 1.2f && r > 60 && r < LANE_LEN - 40 && !c.driveThrough) {
        c.driveThrough = true; c.dtLaps = 3;
        if (c.player) message("Pit lane speeding: drive-through", RGBA(255, 90, 60), RADIO_DRIVETHROUGH);
    }
    if (c.pit == PIT_IN) {
        float rb = pitRel(boxS(c));
        if (c.pitPhase == -1) {
            if (r > rb) {
                c.driveThrough = false; c.pit = PIT_OUT;
                if (c.player) message("Drive-through served", RGBA(60, 255, 90));
            }
        } else if (r >= rb - 0.6f) {
            c.pit = PIT_STOP; c.v = 0; c.pitTimer = 0; c.pitTotal = 0;
            if (session != SES_RACE) { c.pitPhase = -4; return true; }     // practice / qualifying: into the garage
            c.pitPhase = c.player ? -2 : 0;     // the player's team screen opens
            if (!c.player) choosePlan(c);
            c.stops++;
            if (c.player) { choosePlan(c); c.plan.tyres = true; }
        }
    }
    if (c.pit == PIT_OUT && r > LANE_LEN) { c.pit = PIT_NONE; c.d = fminf(c.d, track.pts[track.index(c.s)].hw - 1); }
    if (c.pit == PIT_OUT && r < 0) c.pit = PIT_NONE;   // safety: wrapped past
    return c.pit != PIT_NONE;
}

void Race::pitStop(Car& c, float dt) {
    c.v = 0; c.throttle = 0; c.brake = 1;
    if (session != SES_RACE) { garageMove(*this, c, dt, sfxEvents); pose(c); return; }
    c.rpm = c.def->idleRpm;
    // the car stays where it stopped (the crew line up on it); only our car, going into the garage,
    // is first put square on the box
    if (c.pitPhase <= -2) c.d += (boxD - c.d) * fminf(1, dt * 3);
    pose(c);
    if (c.pitPhase == -2) return;                 // the team screen is open
    // crews work at real speed, but a long race shown in a short time squeezes the stop
    float su = clampf(sqrtf(consumeScale), 1.0f, 3.0f);
    if (c.player && set.career) su *= 1 + 0.06f * set.pitCrewLevel;   // career: a better-drilled crew
    float t = dt * su;
    c.pitTimer += t; c.pitTotal += dt;
    bool me = c.player;
    if (manualStop(c)) { c.pitTimer -= t; c.pitTotal -= dt; pitStopManual(c, dt); return; }
    switch (c.pitPhase) {
        case 0:                                    // refuel (1990s; the tyres are changed meanwhile, see case 1)
            if (c.pitTimer <= t && me && ep().refuel && c.plan.fuel > c.fuel + 0.01f) sfxEvents.push_back(SFX_REFUEL);
            c.fuel = fminf(c.plan.fuel, c.fuel + t / 11.0f);       // ~12 litres a second
            if (c.fuel >= c.plan.fuel - 1e-3f && c.pitTimer > (ep().refuel ? 1.2f : 0.25f)) {
                if (c.plan.swapDriver) swapDriver(c);
                c.pitPhase = 1; c.pitTimer = 0;
                if (me && c.plan.tyres) sfxEvents.push_back(SFX_AIRJACK);
            }
            break;
        case 1: {                                  // up on the jacks, four wheels
            if (!c.plan.tyres) { c.pitPhase = 2; c.pitTimer = 0; break; }
            c.lift = fminf(0.12f, c.lift + dt * 2.5f);
            // Formula 1: four wheel crews at once. 1980 ~11 s (stops were rare and unrehearsed: hand
            // jacks, big nuts), 2010 ~3.2 s, 2016 ~2.6 s, 2025 ~2.3 s
            static const float SWAP[ERA_COUNT] = {11.0f, 3.2f, 2.6f, 2.3f};
            float sw = SWAP[era];
            static const float GUN[2] = {0.25f, 0.45f};
            for (float g : GUN) if (c.pitTimer >= g * sw && c.pitTimer - t < g * sw && me) sfxEvents.push_back(SFX_WHEELGUN);
            if (c.pitTimer > sw) { c.tyre = c.plan.tyre; c.wear = 0; c.tyreLaps = 0; c.coldTyres(); c.pitPhase = 2; c.pitTimer = 0; }
            break;
        }
        case 2: {                                  // repairs: bodywork first, then the rest
            if (!c.plan.repair) { c.pitPhase = 3; c.pitTimer = 0; if (me && c.lift > 0) sfxEvents.push_back(SFX_JACKDROP); break; }
            if (me && c.pitTimer <= t && c.lift == 0) sfxEvents.push_back(SFX_AIRJACK);
            c.lift = fminf(0.12f, c.lift + dt * 0.8f);
            bool done = true;
            for (float& d : c.dmg) { d = fmaxf(0, d - t / 45.0f); if (d > 0) done = false; }
            if (done && c.pitTimer > 4) {
                c.wingOff = c.noseOff = false;
                c.pitPhase = 3; c.pitTimer = 0;
                if (me) sfxEvents.push_back(SFX_JACKDROP);
            }
            break;
        }
        default:                                   // drop and go
            c.lift = fmaxf(0, c.lift - dt * 3.0f);
            if (c.pitTimer > 0.4f) {
                c.pit = PIT_OUT; c.pitPhase = 0; c.lift = 0; c.v = 2;
                if (me) { char m[64]; snprintf(m, sizeof m, "Stop %d: %.1f s - go go go", c.stops, c.pitTotal); message(m, RGBA(60, 255, 90)); }
            }
            break;
    }
}

// ------------------------------------------------------------------ manual pit stop (QTE)
bool Race::manualStop(const Car& c) const { return c.player && set.manualPits && !autoPlayer && !c.plan.aiDrives; }

void Race::qteNext(Car& c) {
    PitQte& q = qte;
    if (q.idx >= q.n) {
        c.pit = PIT_OUT; c.pitPhase = 0; c.lift = 0; c.v = 2; q.on = false;
        char m[64]; snprintf(m, sizeof m, "Stop %d: %.1f s  (%d perfect, %d fumbled)", c.stops, c.pitTotal, q.perfect, q.miss);
        message(m, q.miss ? RGBA(255, 200, 60) : RGBA(60, 255, 90));
        return;
    }
    int st = q.seq[q.idx];
    c.pitPhase = st == QS_FUEL ? 0 : (st == QS_REPAIR ? 2 : (st == QS_DROP ? 3 : 1));
    if (st == QS_REPAIR) {
        // the mechanics work alone: as long as the automatic stop
        float worst = 0; for (float d : c.dmg) worst = fmaxf(worst, d);
        q.waiting = false; q.work = q.workTotal = fmaxf(4.0f, worst * 45.0f);
        return;
    }
    q.waiting = true; q.pos = 0; q.result = -1;
    q.speed = 1.0f / 1.1f;
    q.button = (int)(rnd() * 4) & 3;
    q.z0 = 0.40f + rnd() * 0.34f; q.z1 = q.z0 + 0.18f;
    q.p0 = q.z0 + 0.06f; q.p1 = q.z0 + 0.12f;
}

void Race::qtePress(int b) {
    if (!qte.on || !qte.waiting) return;     // presses between prompts do nothing (no mashing)
    bool hit = b == qte.button && qte.pos >= qte.z0 && qte.pos <= qte.z1;
    bool perfect = hit && qte.pos >= qte.p0 && qte.pos <= qte.p1;
    qte.result = perfect ? 2 : (hit ? 1 : 0);
    qte.waiting = false;                     // one press per prompt
}

void Race::pitStopManual(Car& c, float dt) {
    PitQte& q = qte;
    float su = clampf(sqrtf(consumeScale), 1.0f, 3.0f);
    c.pitTotal += dt;
    if (!q.on) {
        // the jobs this stop needs, in order
        q = PitQte(); q.on = true;
        if (c.plan.fuel > c.fuel + 0.01f || c.plan.swapDriver) q.seq[q.n++] = QS_FUEL;
        if (c.plan.tyres) { q.seq[q.n++] = QS_JACK; q.seq[q.n++] = QS_FL; q.seq[q.n++] = QS_FR; q.seq[q.n++] = QS_RL; q.seq[q.n++] = QS_RR; }
        if (c.plan.repair && c.damage() > 0) { if (!c.plan.tyres) q.seq[q.n++] = QS_JACK; q.seq[q.n++] = QS_REPAIR; }
        q.seq[q.n++] = QS_DROP;
        qteNext(c);
    }
    q.flash = fmaxf(0, q.flash - dt);
    int st = q.idx < q.n ? q.seq[q.idx] : QS_DROP;
    if (q.waiting) {
        q.pos += dt * q.speed;
        if (q.pos > 1) { q.result = 0; q.waiting = false; }
        if (st >= QS_FL && st <= QS_RR) c.lift = fminf(0.12f, c.lift + dt * 0.8f);
        if (q.waiting) return;
    }
    if (q.result >= 0) {
        // answered: how long the job takes depends on the timing (miss = fumble, slower than the crew alone)
        int r = q.result; q.result = -1; q.flash = 0.8f;
        q.lastShown = r;
        if (r == 2) q.perfect++; else if (r == 1) q.good++; else q.miss++;
        static const float FUEL_K[3] = {1.25f, 0.85f, 0.7f}, JACK[3] = {0.9f, 0.25f, 0.12f}, WHEEL[3] = {1.4f, 0.5f, 0.3f}, DROP[3] = {1.0f, 0.3f, 0.1f};
        if (st == QS_FUEL) {
            float fill = (c.plan.fuel - c.fuel) * 11.0f;
            q.work = fmaxf(fill, c.plan.swapDriver ? 6.0f : 0.5f) * FUEL_K[r];
            q.fuelFrom = c.fuel;
            sfxEvents.push_back(SFX_REFUEL);
        } else if (st == QS_JACK) { q.work = JACK[r]; sfxEvents.push_back(SFX_AIRJACK); }
        else if (st == QS_DROP) q.work = DROP[r];
        else { q.work = WHEEL[r]; if (r) sfxEvents.push_back(SFX_WHEELGUN); }
        q.workTotal = q.work;
    }
    // the job in progress
    q.work -= dt * su;
    float k = q.workTotal > 0 ? clampf(1 - q.work / q.workTotal, 0, 1) : 1;
    if (st == QS_FUEL) c.fuel = q.fuelFrom + (fmaxf(c.plan.fuel, q.fuelFrom) - q.fuelFrom) * k;
    if (st == QS_JACK || (st >= QS_FL && st <= QS_RR) || st == QS_REPAIR) c.lift = fminf(0.12f, c.lift + dt * 0.8f);
    if (st == QS_REPAIR) for (float& d : c.dmg) d = fmaxf(0, d - dt * su / 45.0f);
    if (st == QS_DROP) c.lift = fmaxf(0, c.lift - dt * 1.5f);
    if (q.work > 0) return;
    // job done
    if (st == QS_FUEL && c.plan.swapDriver) swapDriver(c);
    if (st == QS_RR) { c.tyre = c.plan.tyre; c.wear = 0; c.tyreLaps = 0; c.coldTyres(); }
    if (st == QS_REPAIR) { for (float& d : c.dmg) d = 0; c.wingOff = c.noseOff = false; }
    if (st == QS_DROP) { c.lift = 0; sfxEvents.push_back(SFX_JACKDROP); }
    q.idx++;
    qteNext(c);
}

// ------------------------------------------------------------------ damage and parts
void Race::addDamage(Car& c, int part, float amount) {
    if (amount <= 0.002f) return;
    float before = c.dmg[part];
    c.dmg[part] = fminf(1, c.dmg[part] + amount);
    if (amount > 0.12f) c.dmg[DM_ENGINE] = fminf(1, c.dmg[DM_ENGINE] + amount * 0.25f);
    // shards of carbon
    if (amount > 0.03f && parts.size() < 200)
        for (int k = 0; k < 4; k++) {
            Particle p; p.p = c.pos + V3(0, 0, 0.5f);
            p.v = c.fwd * (c.v * 0.5f) + V3(rnd() - 0.5f, rnd() - 0.5f, rnd() * 0.8f) * 6.0f;
            p.life = 1.2f; p.size = 0.12f; p.color = RGBA(40, 40, 40); p.kind = 2;
            parts.push_back(p);
        }
    if (part == DM_NOSE && c.dmg[part] > 0.75f && !c.noseOff) detach(c, DM_NOSE);
    if (part == DM_WING && c.dmg[part] > 0.7f && !c.wingOff) detach(c, DM_WING);
    if (c.player && amount > 0.06f && before < 0.95f) {
        static float lastCall = -100;
        if (time - lastCall > 15) { lastCall = time; message("Damage! Check the car", RGBA(255, 120, 60), RADIO_DAMAGE); }
    }
}

void Race::detach(Car& c, int part) {
    if (part == DM_NOSE) c.noseOff = true; else c.wingOff = true;
    Debris d;
    d.p = c.pos + c.fwd * (part == DM_NOSE ? 2.2f : -2.2f) + c.up * 0.8f;
    d.v = c.fwd * (c.v * 0.55f) + c.up * (3 + rnd() * 4) + c.left * ((rnd() - 0.5f) * 8);
    d.yaw = atan2f(c.fwd.y, c.fwd.x); d.pitch = 0; d.spin = (rnd() - 0.5f) * 14;
    d.life = 25; d.ground = c.pos.z - c.lift; d.carIdx = (int)(&c - &cars[0]); d.part = part;
    debris.push_back(d);
    if (debris.size() > 8) debris.erase(debris.begin());
    if (c.player) message(part == DM_NOSE ? "You've lost the nose!" : "The rear wing is gone!", RGBA(255, 90, 60), RADIO_DAMAGE);
    if (session == SES_RACE) {
        // bits on the track: a slow zone while the marshals clear them, now and then an FCY or the safety car
        float p = rnd();
        if (p < 0.12f) deploySC(*this, false);
        else if (p < 0.3f) callFcy(45 + rnd() * 20);
        else addSlowZone(c.s, 45);
    }
}

void Race::particles(float dt) {
    for (auto& d : debris) {
        d.life -= dt;
        d.v.z -= 9.81f * dt;
        d.p = d.p + d.v * dt;
        if (d.p.z < d.ground + 0.1f) {
            d.p.z = d.ground + 0.1f;
            d.v.z = -d.v.z * 0.35f;
            d.v.x *= 0.7f; d.v.y *= 0.7f; d.spin *= 0.6f;
        }
        d.yaw += d.spin * dt; d.pitch += d.spin * 0.6f * dt;
    }
    debris.erase(std::remove_if(debris.begin(), debris.end(), [](const Debris& d) { return d.life <= 0; }), debris.end());
    for (auto& p : parts) {
        p.life -= dt;
        p.p = p.p + p.v * dt;
        if (p.kind == 2) p.v.z -= 9.81f * dt;
        else { p.v = p.v * (1 - 1.5f * dt); p.v.z += 0.6f * dt; p.size += dt * (p.kind == 0 ? 1.6f : 2.2f); }
    }
    parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle& p) { return p.life <= 0; }), parts.end());
    // new smoke and spray only near the player (that's where the camera is)
    const V3 cam = player().pos;
    for (auto& c : cars) {
        if (parts.size() >= 220) break;
        V3 dv = c.pos - cam;
        if (dv.x * dv.x + dv.y * dv.y > 150 * 150) continue;
        float smoke = fmaxf(c.dmg[DM_ENGINE] - 0.3f, 0) + (c.slip > 0.5f && c.v > 8 && wet < 0.3f ? 0.5f : 0);
        if (smoke > 0 && rnd() < smoke * 0.9f) {
            Particle p; p.p = c.pos - c.fwd * 2.0f + V3(0, 0, 0.5f);
            p.v = c.fwd * (c.v * 0.3f) + V3(rnd() - 0.5f, rnd() - 0.5f, 1.2f);
            p.life = 1.6f; p.size = 0.6f; p.kind = 0;
            p.color = c.dmg[DM_ENGINE] > 0.3f ? RGBA(60, 60, 60, 150) : RGBA(220, 220, 220, 110);
            parts.push_back(p);
        }
        if (wet > 0.25f && c.v > 25 && rnd() < wet * 0.8f) {
            Particle p; p.p = c.pos - c.fwd * 2.6f + c.left * ((rnd() - 0.5f) * 1.6f) + V3(0, 0, 0.3f);
            p.v = c.fwd * (c.v * 0.55f) + V3(0, 0, 1.0f + rnd());
            p.life = 0.9f; p.size = 0.7f; p.kind = 1; p.color = RGBA(200, 205, 215, 70);
            parts.push_back(p);
        }
    }
}

// ------------------------------------------------------------------ saving (3 slots, saved in the pits)
struct SaveHead {
    char magic[4]; int version;
    RaceSettings set; int session, playerIdx, carCount;
    float time, countdown, timeScale, consumeScale, startHour, timeAccel, greenTime;
    float rain, wet, rainTarget, rainTimer, scCooldown; int storm, formation, chequered;
    SafetyCar sc;
};

bool Race::save(const char* path) const {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    SaveHead h; memset((void*)&h, 0, sizeof h);
    memcpy(h.magic, "EMS1", 4); h.version = 7;   // v7: the body on its springs (CarBody) at the end of the Car record; v6: tyre temperatures at the end of the Car record; v5: spins, slow zones, hybrid
    h.set = set; h.session = session; h.playerIdx = playerIdx; h.carCount = (int)cars.size();
    h.time = time; h.countdown = countdown; h.timeScale = timeScale; h.consumeScale = consumeScale; h.startHour = startHour; h.timeAccel = timeAccel; h.greenTime = greenTime;
    h.rain = rain; h.wet = wet; h.rainTarget = rainTarget; h.rainTimer = rainTimer; h.scCooldown = scCooldown;
    h.storm = storm; h.formation = formation; h.chequered = chequered; h.sc = sc;
    fwrite(&h, sizeof h, 1, f);
    for (auto& c : cars) {
        int def = (int)(c.def - CARS);
        fwrite(&def, 4, 1, f);
        fwrite(&c, sizeof(Car), 1, f);
    }
    fclose(f);
    return true;
}

bool Race::load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    SaveHead h;
    if (fread(&h, sizeof h, 1, f) != 1 || memcmp(h.magic, "EMS1", 4) || (h.version < 5 || h.version > 7) || h.carCount <= 0 || h.carCount > 64) { fclose(f); return false; }
    std::vector<Car> cs(h.carCount);
    for (auto& c : cs) {
        int def = 0;
        // version 5: no tyre temperatures; 6: no CarBody (it starts on the road)
        size_t recSize = h.version == 7 ? sizeof(Car) : (h.version == 6 ? offsetof(Car, body) : offsetof(Car, tyreT));
        if (fread(&def, 4, 1, f) != 1 || fread(&c, recSize, 1, f) != 1 || def < 0 || def >= CAR_COUNT) { fclose(f); return false; }
        if (h.version == 5) for (float& t : c.tyreT) t = 80;
        if (h.version < 7) c.body = CarBody();
        c.def = &CARS[def];
    }
    fclose(f);
    set = h.set; session = (Session)h.session;
    if (!track.load(&TRACKS[set.track])) return false;
    plan(); setupPit();
    cars = cs; playerIdx = h.playerIdx;
    time = h.time; countdown = h.countdown; timeScale = h.timeScale; consumeScale = h.consumeScale; startHour = h.startHour; timeAccel = h.timeAccel; greenTime = h.greenTime;
    rain = h.rain; wet = h.wet; rainTarget = h.rainTarget; rainTimer = h.rainTimer; scCooldown = h.scCooldown;
    storm = h.storm; formation = h.formation; chequered = h.chequered; sc = h.sc;
    sessionLength = set.sessionMin * 60.0f;
    over = false; msgs.clear(); debris.clear(); parts.clear(); sfxEvents.clear(); radioPending = -1;
    lightning = 0; thunderPending = -1; nextStrike = 10;
    markFirst.clear();
    for (auto& c : cars) { c.lastMark = -1; pose(c); }   // the live gaps start again from here
    rank();
    return true;
}

bool Race::peek(const char* path, RaceSettings& s, int& ses, float& raceTime, int& lap) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    SaveHead h;
    bool ok = fread(&h, sizeof h, 1, f) == 1 && !memcmp(h.magic, "EMS1", 4) && h.version == 4 && h.carCount > 0 && h.carCount <= 64;
    lap = 0;
    if (ok) {
        // the player's car record
        fseek(f, (long)(sizeof h + h.playerIdx * (4 + sizeof(Car))), SEEK_SET);
        int def; Car c;
        if (fread(&def, 4, 1, f) == 1 && fread((void*)&c, sizeof(Car), 1, f) == 1) lap = c.lap;
    }
    fclose(f);
    if (!ok) return false;
    s = h.set; ses = h.session; raceTime = (h.time - h.greenTime) * h.timeScale;
    return true;
}
