#include "race.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static const float G = 9.81f;
static const float WHEELBASE = 2.9f;
static const float CAR_LEN = 4.8f, CAR_W = 1.95f;
static const float GEAR[8] = {0, 0.36f, 0.49f, 0.60f, 0.70f, 0.79f, 0.89f, 1.0f};

const ClassPhys& Race::phys(int era) {
    // tuned so the AI on Hard laps close to each era's real pole times (see tests: Monza, Spa, Suzuka)
    //                                  top   mu     dfV   brake  cdA    cap   fuel  ers   drs    refuel vsc
    static ClassPhys E[ERA_COUNT] = {{350, 1.32f, 52, 1.95f, 1.08f, 3.0f, 95,  0,   false, true,  false},   // 1998 V10
                                     {335, 1.41f, 48, 2.10f, 1.15f, 3.1f, 160, 0,   false, false, false},   // 2010 V8
                                     {345, 1.42f, 50, 2.10f, 1.18f, 3.2f, 100, 120, true,  false, true},    // 2016 hybrid
                                     {345, 1.52f, 47, 2.20f, 1.12f, 3.4f, 100, 120, true,  false, true}};   // 2025 ground effect
    // AI on Hard, dry, best of 3 laps (Monza / Spa / Monaco / Suzuka) against the real pole times:
    //   1998 1:23.9 1:49.5 1:15.7 1:36.4 (real 1:25.3 1:48.7 1:19.8 1:36.3)
    //   2010 1:23.0 1:45.8 1:13.0 1:31.9 (real 1:21.9 1:45.8 1:13.8 1:30.8)
    //   2016 1:21.3 1:45.0 1:12.0 1:31.2 (real 1:21.1 1:46.7 1:13.6 1:30.6)
    //   2025 1:19.4 1:40.3 1:09.5 1:27.2 (real 1:18.8 1:40.5 1:10.0 1:27.0)
#ifndef PLATFORM_VITA
    // tests: FL_PHYS="era mu dfV cdA cap" overrides one era's numbers (lap-time tuning)
    static bool once = false;
    if (!once) {
        once = true;
        if (const char* o = getenv("FL_PHYS")) {
            int e; float mu, df, cd, cap;
            if (sscanf(o, "%d %f %f %f %f", &e, &mu, &df, &cd, &cap) == 5 && e >= 0 && e < ERA_COUNT) { E[e].muBase = mu; E[e].downforceV = df; E[e].cdA = cd; E[e].aeroCap = cap; }
        }
    }
#endif
    return E[era < 0 ? 0 : era % ERA_COUNT];
}

static uint32_t seed = 7;
static float frand() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) * (1.0f / 16777216.0f); }

static float topSpeed(const CarDef* d) {
    const ClassPhys& p = Race::phys(d->era);
    // power = drag * v  ->  v = cbrt(P / (0.5 rho CdA))
    float P = d->powerKW * 1000 * (1 + d->topGain);
    return cbrtf(P / (0.5f * 1.2f * p.cdA * d->aero));
}

bool Race::start(const RaceSettings& s0, Session ses, const std::vector<int>* gridOrder) {
    RaceSettings s = s0;
    if (s.laps >= 999 && s.track >= 0 && s.track < TRACK_COUNT) s.laps = TRACKS[s.track].gpLaps;   // the full Grand Prix distance
    if (s.timeTrial) { s.opponents = 0; s.weather = WX_DRY; s.weekend = false; ses = SES_PRACTICE; }
    set = s;
    session = ses;
    chequered = false;
    sessionLength = s.timeTrial ? 1e8f : s.sessionMin * 60.0f;
    ghost.clear(); ghostRec.clear(); ghostLap = 0; lapInvalid = false; newRecord = false; ttDeltaOn = false;
    if (!track.load(&TRACKS[s.track])) return false;
    era = CARS[s.car].era;
    plan();
    setupPit();
    setupDrs();
    cars.clear(); msgs.clear(); debris.clear(); parts.clear(); sfxEvents.clear(); markFirst.clear();
    sc = SafetyCar(); scCooldown = 60; fcy = 0; fcyWarn = 0; zones.clear();
    rubber = clampf(s.rubber, 0, 1); rubberMsg = (int)(rubber * 4); radioPending = -1; formation = false; timeAccel = 1;
    seed = 1234 + s.track * 77 + s.car * 13;
    const CarDef* me = &CARS[s.car];
    // the field: the player's team-mate first, then alternate the other cars in both classes
    std::vector<std::pair<const CarDef*, int>> entries;
    entries.push_back({me, me->numbers[0]});
    std::vector<std::pair<const CarDef*, int>> pool;
    if (me->numbers[1]) pool.push_back({me, me->numbers[1]});   // (the career team runs one car)
    era = me->era;
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < CAR_PICK; i++)
            if (&CARS[i] != me && CARS[i].era == me->era && !(i == s.skipCar && (s.career || CARS[i].numbers[k] == s.skipNum))) pool.push_back({&CARS[i], CARS[i].numbers[k]});
    std::vector<std::pair<const CarDef*, int>> hy, gt;
    for (auto& e : pool) (e.first->cls == CLASS_HYPER ? hy : gt).push_back(e);
    size_t a = 0, b = 0;
    if (me->cls == CLASS_HYPER && !hy.empty()) { entries.push_back(hy[0]); a = 1; }
    if (me->cls == CLASS_GT3 && !gt.empty()) { entries.push_back(gt[0]); b = 1; }
    while ((int)entries.size() < s.opponents + 1 && (a < hy.size() || b < gt.size())) {
        if (a < hy.size()) entries.push_back(hy[a++]);
        if ((int)entries.size() < s.opponents + 1 && b < gt.size()) entries.push_back(gt[b++]);
    }
    if (s.opponents <= 0) entries.resize(1);        // time trial: the player alone
    // grid: hypercars ahead of GTs, shuffled inside each class, player placed by the setting
    std::vector<int> order;
    for (int cls = 0; cls < 2; cls++) {
        std::vector<int> grp;
        for (int i = 0; i < (int)entries.size(); i++) if ((int)entries[i].first->cls == cls) grp.push_back(i);
        for (int i = (int)grp.size() - 1; i > 0; i--) { int j = (int)(frand() * (i + 1)) % (i + 1); std::swap(grp[i], grp[j]); }
        if ((int)me->cls == cls) {
            grp.erase(std::find(grp.begin(), grp.end(), 0));
            int at = s.startPos == 0 ? 0 : (s.startPos == 1 ? (int)grp.size() / 2 : (int)grp.size());
            grp.insert(grp.begin() + at, 0);
        }
        order.insert(order.end(), grp.begin(), grp.end());
    }
    if (gridOrder && !gridOrder->empty()) {
        std::vector<int> o2;
        for (int num : *gridOrder)
            for (int i = 0; i < (int)entries.size(); i++) if (entries[i].second == num) o2.push_back(i);
        if (o2.size() == entries.size()) order = o2;
    }
    seed = seed * 31 + (uint32_t)ses * 7919u + (uint32_t)(sys::timeSec() * 1000);   // drivers' form differs per session
    static const float SKILL[3] = {0.88f, 0.94f, 0.985f};
    for (int k = 0; k < (int)order.size(); k++) {
        Car c;
        c.def = entries[order[k]].first;
        c.number = entries[order[k]].second;
        c.player = order[k] == 0;
        c.skill = SKILL[s.difficulty] * (0.985f + frand() * 0.03f);
        c.lineBias = (frand() - 0.5f) * 0.6f;
        c.gear = 1; c.rpm = c.def->idleRpm;
        crewDrivers(c);
        if (c.player && s.career) { c.drv[1] = s.crew[1]; c.drv[2] = s.crew[2]; c.drv[1].fatigue = c.drv[2].fatigue = 0; c.drv[1].hours = c.drv[2].hours = 0; }
        // career: the team's second car is driven by your first hired driver
        if (!c.player && s.career && c.def == me) { c.drv[0] = s.crew[1]; c.drv[0].fatigue = 0; c.driver = 0; }
        if (c.player) playerIdx = k;
        cars.push_back(c);
    }
    // race clock: a timed race stands for the event's real length (e.g. 6 h in 30 min = 12x)
    const TrackDef& td = TRACKS[s.track];
    float hours = s.eventHours > 0 ? s.eventHours : td.hours;
    timeScale = (ses == SES_RACE && s.raceMin > 0) ? hours * 60.0f / s.raceMin : 1.0f;
    {
        float estLap = track.length / 52.0f;
        consumeScale = fmaxf(1.0f, fminf(timeScale, 3900.0f / (5.0f * estLap)));
    }
    startHour = ses == SES_RACE ? td.startHour : (ses == SES_PRACTICE ? 10.5f : 14.0f);
    // weather
    rain = 0; wet = 0; storm = false; lightning = 0; nextStrike = 15; thunderPending = -1;
    if (s.weather == WX_WET) { rain = rainTarget = 0.6f; wet = 0.8f; }
    else if (s.weather == WX_STORM) { rain = rainTarget = 0.75f; wet = 0.9f; storm = true; }
    else if (s.weather == WX_SIMULATED) { rainTarget = frand() < 0.3f ? 0.2f + frand() * 0.7f : 0; rain = rainTarget * 0.5f; wet = rain; }
    rainTimer = 120 + frand() * 300;
    {
        // F1: the tank and the tyres are sized to the race. Refuelling (1990s): a tank for ~45 % of
        // the race (two stops). Otherwise the whole race on one tank. Tyres: softs ~40 %, mediums
        // ~60 %, hards ~85 % of the distance, so a race has one or two stops however long it is.
        float raceKm = fmaxf(1, s.laps) * track.length / 1000.0f;
        if (s.raceMin > 0) raceKm = s.raceMin * 60.0f * 52.0f / 1000.0f;
        if (s.laps <= 2 && s.raceMin <= 0) raceKm = fmaxf(raceKm, 3 * track.length / 1000.0f);
        tankKm = ep().refuel ? raceKm * 0.45f + track.length / 1000.0f * 0.6f : raceKm * 1.04f + track.length / 1000.0f * 0.3f;
        static const float SHARE[TY_COUNT] = {0.40f, 0.62f, 0.88f, 0.70f, 0.70f};
        for (int t = 0; t < TY_COUNT; t++) tyreKm[t] = fmaxf(raceKm * SHARE[t], track.length / 1000.0f * 1.2f);
    }
    int startTyre = bestTyreFor(wet);
    for (auto& c : cars) { c.tyre = startTyre; c.fuel = 1; }
    {
        Car& me = cars[playerIdx];
        me.su = s.setup;
        if (startTyre == TY_MEDIUM && me.su.tyre <= TY_HARD) me.tyre = me.su.tyre;      // dry: the compound chosen in the garage
        me.fuel = clampf(me.su.fuel / 100.0f, 0.1f, 1.0f);
    }
    time = 0; over = false;
    setupStart();
    for (auto& c : cars) pose(c);
    rank();
    return true;
}

void Race::end() {
    track.free();
    std::vector<Car>().swap(cars); std::vector<Debris>().swap(debris); std::vector<Particle>().swap(parts);
    std::vector<float>().swap(line); std::vector<float>().swap(vmax[0]); std::vector<float>().swap(vmax[1]); drsZones.clear();
    std::vector<float>().swap(markFirst); msgs.clear();
}

void Race::plan() {
    int n = track.n;
    line.assign(n, 0);
    std::vector<float> raw(n);
    for (int i = 0; i < n; i++) {
        float now = 0, ahead = 0;
        for (int k = -2; k <= 2; k++) now += track.pts[(i + k + n) % n].curv;
        for (int k = 3; k < 18; k++) ahead += track.pts[(i + k) % n].curv;
        now /= 5; ahead /= 15;
        float r = clampf((now * 1.0f - ahead * 0.8f) * 260.0f, -1, 1);
        raw[i] = r * (track.pts[i].hw - 1.6f);
    }
    for (int pass = 0; pass < 3; pass++) {
        std::vector<float> t = raw;
        for (int i = 0; i < n; i++) { float s = 0; for (int k = -4; k <= 4; k++) s += t[(i + k + n) % n]; raw[i] = s / 9; }
    }
    line = raw;
    for (int cls = 0; cls < 2; cls++) {
        const ClassPhys& p = ep();
        float vtop = p.topKmh / 3.6f;
        std::vector<float> v(n);
        for (int i = 0; i < n; i++) {
            float k = 0; for (int j = -3; j <= 3; j++) k += track.pts[(i + j + n) % n].curv;
            k = fabsf(k / 7) / 1.3f;
            float mu = p.muBase * gripScale * surfaceGrip() * G, aero = mu / (p.downforceV * p.downforceV);
            float vc = k > aero + 1e-6f ? sqrtf(mu / (k - aero)) : 999.0f;
            // past the downforce cap the grip stops growing with speed
            float vcap = p.downforceV * sqrtf(p.aeroCap - 1);
            if (vc > vcap && k > 1e-6f) vc = fmaxf(vcap, sqrtf(mu * p.aeroCap / k));
            v[i] = fminf(vc, vtop);
        }
        for (int pass = 0; pass < 2; pass++)
            for (int i = n - 1; i >= 0; i--) {
                int j = (i + 1) % n;
                float vv = v[j];
                float dec = p.brakeG * G * fminf(1 + (vv / p.downforceV) * (vv / p.downforceV), 2.6f) * 0.82f;
                dec += G * track.pts[i].grade;
                float lim = sqrtf(vv * vv + 2 * fmaxf(dec, 3.0f) * track.step);
                if (v[i] > lim) v[i] = lim;
            }
        vmax[cls] = v;
    }
}

void Race::pose(Car& c) const {
    V3 p, t, l; float hw;
    track.frame(c.s, p, t, l, hw);
    float th = atan2f(t.y, t.x) + c.yaw;
    float grade = track.gradeAt(c.s);
    float cp = 1.0f / sqrtf(1 + grade * grade), sp = grade * cp;
    c.fwd = V3(cosf(th) * cp, sinf(th) * cp, sp);
    c.left = V3(-sinf(th), cosf(th), 0);
    // off the tarmac the car sits on the grass / gravel, which slopes up or down to the barrier
    float k = 0, gz = inPitLane(c) ? 0 : track.groundAt(c.s, c.d, &k);
    if (gz != 0 || k != 0) {
        V3 g = l * (c.d >= 0 ? k : -k);                // uphill direction across the verge, metres up per metre
        c.fwd = norm(c.fwd + V3(0, 0, c.fwd.x * g.x + c.fwd.y * g.y));
        c.left = norm(c.left + V3(0, 0, c.left.x * g.x + c.left.y * g.y));
    }
    c.up = cross(c.fwd, c.left);
    c.pos = p + l * c.d + V3(0, 0, c.lift + gz);
    float ad = fabsf(c.d);
    if (ad > hw && ad < hw + 1.3f && track.pts[track.index(c.s)].kerb && !inPitLane(c)) c.pos.z += 0.04f;
}

void Race::physics(Car& c, float dt) {
    const ClassPhys& p = phys(c.def->era);
    float mass = c.def->massKg + c.fuel * p.fuelKg;
    static const float MAP_POWER[MAP_COUNT] = {0.72f, 1.0f, 1.04f}, MAP_FUEL[MAP_COUNT] = {0.6f, 1.0f, 1.25f};
    int map = c.su.map;
    if (!c.player && sc.state) map = MAP_SC;        // the AI saves fuel behind the safety car
    float wingD = (c.su.wing - 3) * 0.06f, wingDrag = 1 + (c.su.wing - 3) * 0.045f;
    float P = c.def->powerKW * 1000 * (1 - 0.45f * c.dmg[DM_ENGINE]) * MAP_POWER[map];
    if (c.fuel <= 0) P *= 0.08f;                         // out of fuel: crawling on the last drops
    // hybrid (2014 on): braking charges the battery, flat-out acceleration spends it: +120 kW (MGU-K)
    // for up to ~16 s. Normal map: out of corners only (under 270 km/h); Attack: whenever flat out;
    // Safety-car map: charge only.
    c.boosting = false;
    if (p.ersKW > 0 && c.pit == PIT_NONE && !hybridOff) {
        if (c.brake > 0.1f && c.v > 8) c.ers = fminf(1.0f, c.ers + c.brake * fminf(c.v / 50.0f, 1.0f) * 0.10f * dt);
        bool want = map != MAP_SC && c.throttle > 0.9f && c.brake < 0.05f && c.v > 14 && !sc.state && !formation && !slowAt(c.s);
        if (map == MAP_NORMAL && c.v > 75.0f) want = false;
        if (want && c.ers > 0.01f && c.fuel > 0) {
            c.boosting = true;
            c.ers = fmaxf(0.0f, c.ers - 0.062f * dt);
            P += p.ersKW * 1000.0f * (1 - 0.45f * c.dmg[DM_ENGINE]);
        }
    }
    int i = track.index(c.s);
    const TrackPoint& tp = track.pts[i];
    float hw = tp.hw;
    float ad = fabsf(c.d);
    bool pitLane = inPitLane(c);
    bool onKerb = tp.kerb && ad > hw && ad < hw + 1.3f;
    bool off = !pitLane && ad > hw + (tp.kerb ? 1.3f : 0.3f);
    bool leftSide = c.d > 0;
    bool gravel = off && (leftSide ? tp.gravelL : tp.gravelR);
    c.offTrack = off;

    // damage: aero parts cost downforce, body panels add drag, suspension costs grip
    float aeroLoss = 0.35f * c.dmg[DM_NOSE] + 0.3f * c.dmg[DM_WING] + (c.noseOff ? 0.15f : 0) + (c.wingOff ? 0.25f : 0);
    float aeroK = fminf(1 + (c.v / p.downforceV) * (c.v / p.downforceV) * (1 - aeroLoss) * (1 + wingD) * (c.drs ? 0.9f : 1.0f), p.aeroCap * (1 + wingD));
    // suspension: soft rides kerbs and a wet track better, stiff is sharper on smooth dry tarmac
    static const float KERB[3] = {0.96f, 0.90f, 0.84f}, DRY[3] = {0.985f, 1.0f, 1.012f}, WETK[3] = {1.03f, 1.0f, 0.97f};
    float suspK = DRY[c.su.susp] + (WETK[c.su.susp] - DRY[c.su.susp]) * wet;
    float gripMul = (off ? (gravel ? 0.42f : 0.55f) : (onKerb ? KERB[c.su.susp] : 1.0f)) * c.def->grip * tyreGrip(c) * (1 - 0.25f * c.dmg[DM_SUSP]) * suspK;
    if (!off) gripMul *= rubberGrip(c);
    float latMax = p.muBase * gripScale * surfaceGrip() * G * aeroK * gripMul;
    if (c.player) playerLatMax = latMax;

    // ---- longitudinal
    static const float RATIO[3] = {0.95f, 1.0f, 1.05f};
    float vt = topSpeed(c.def) * (c.wingOff ? 1.02f : 1.0f);
    float gearTop = vt * 1.03f * GEAR[c.gear] * RATIO[c.su.gearing];
    c.rpm = fmaxf(c.def->idleRpm, c.def->maxRpm * c.v / fmaxf(gearTop, 1));
    float rpmFrac = c.rpm / c.def->maxRpm;
    float powerFrac = rpmFrac > 1.0f ? 0.0f : clampf(0.55f + 0.6f * rpmFrac, 0, 1);
    float force = c.throttle * P * powerFrac / fmaxf(c.v, 6.0f);
    float traction = 1.25f * mass * G * aeroK * gripMul;
    if (force > traction) force = traction;
    float dragK = (1 + 0.12f * (c.dmg[DM_NOSE] + c.dmg[DM_LEFT] + c.dmg[DM_RIGHT]) - (c.wingOff ? 0.06f : 0)) * wingDrag * (c.drs ? 0.84f : 1.0f);
    float drag = 0.5f * 1.2f * p.cdA * c.def->aero * dragK * c.v * c.v + 0.015f * mass * G;
    float grade = track.gradeAt(c.s);
    float slope = mass * G * grade / sqrtf(1 + grade * grade);
    float acc = (force - drag - slope) / mass;
    // brakes: the pressures set the strength; the best split is about 63 % front
    float bias = c.su.bias();
    float brakeK = fminf(1.0f, (c.su.brakeF + c.su.brakeR) / 115.0f) * (1 - 1.4f * fabsf(bias - 0.63f));
    float brakeDec = c.brake * p.brakeG * G * fminf(aeroK, 2.6f) * (off ? 0.6f : 1.0f) * fminf(1.0f, tyreGrip(c) + 0.15f) * brakeK;
    acc -= c.v > 0.1f ? brakeDec : 0;
    if (off) acc -= (gravel ? 4.0f + 0.05f * c.v : 1.2f + 0.012f * c.v) * (c.v > 1 ? 1 : 0);
    c.v += acc * dt;
    if (c.v < 0) c.v = 0;

    // ---- steering / yaw (c.yawRate holds the wanted rate, set by player input or AI)
    float want = c.yawRate + (c.dmg[DM_SUSP] * 0.04f * c.v / 30.0f) * (c.dmg[DM_LEFT] > c.dmg[DM_RIGHT] ? 1 : -1) * (c.player ? 1 : 0);
    float maxRate = c.v > 1 ? latMax / c.v : 3.0f;
    c.slip = 0;
    float travel = c.yaw;                       // direction the car moves in (relative to the track)
    {
        float ex = fabsf(want) > maxRate ? fabsf(want) / maxRate - 1 : 0;   // how far past the grip the steering asks
        float sgW = want > 0 ? 1.0f : -1.0f;
        if (ex > 0) {
            c.slip = clampf(ex, 0, 1);
            want = sgW * maxRate * (1 + 0.12f * c.slip);
            c.v -= c.slip * 4.0f * dt * (c.v > 5 ? 1 : 0);
        }
        // Steering much harder than the tyres can take makes the rear step out. A small slide can
        // be caught with opposite lock; past ~35 degrees the car spins. The nose turns by the extra
        // drift while the car keeps going where the grip takes it (travel = yaw - drift).
        float rot = 0;
        if (c.human && !c.spinning && spinsOn) {
            const float STEP_OUT = 0.35f, SPIN = 0.62f;
            if (ex > STEP_OUT && c.v > 15) rot += sgW * fminf((ex - STEP_OUT) * 1.3f, 1.1f) * dt;
            // F1 06-style handling: the car stays planted unless the throttle is stabbed (0 -> full in a blink)
            // in a slow, loaded corner, which kicks the rear out; opposite lock catches it
            {
                float rateT = (c.throttle - playerThrPrev) / fmaxf(dt, 1e-3f);
                playerStab = rateT > 5.0f ? 1.0f : playerStab * expf(-dt * 2.5f);
                float load = maxRate > 0 ? clampf((fabsf(want) / maxRate - 0.7f) * 3.5f, 0, 1) : 0;
                float slow = clampf((42.0f - c.v) / 22.0f, 0, 1);
                float kick = playerStab * load * slow * clampf((c.throttle - 0.7f) * 3.3f, 0, 1);
                if (kick > 0 && c.v > 8) rot += sgW * kick * 0.9f * dt;
            }
            float sd = c.drift > 0 ? 1.0f : -1.0f, ad = fabsf(c.drift);
            bool counter = fabsf(c.yawRate) > 0.02f && (c.yawRate > 0 ? 1.0f : -1.0f) != sd;
            if (ad > 0.12f && !counter) rot += sd * ad * (0.9f + c.throttle * 1.6f) * dt;   // a big slide feeds itself (more on the throttle)
            float settle = counter ? 3.5f : (ad < 0.12f ? 6.0f : 0.8f);
            rot -= c.drift * (1 - expf(-dt * settle));
            if (ad > SPIN && c.v > 12) {
                c.spinning = true;
                message("Spin!", RGBA(255, 120, 60));
            }
        } else if (c.spinning) {
            // round it goes, scrubbing speed, until the car has almost stopped
            float sd = c.drift > 0 ? 1.0f : -1.0f;
            // ... until it is going backwards, then it slides on to a stop
            if (fabsf(c.drift) < 2.9f) rot += sd * fminf(clampf(c.v * 0.09f, 1.2f, 4.5f) * dt, 2.9f - fabsf(c.drift));
            c.v = fmaxf(0, c.v - (7.5f + 0.06f * c.v) * dt);
            c.throttle = 0;
            if (c.v < 4) { c.spinning = false; c.drift = 0; }      // stopped: drive on from where the nose points
        } else {
            rot -= c.drift * (1 - expf(-dt * 6));
        }
        c.drift = wrapPi(c.drift + rot);
        c.yaw += rot;
        if (fabsf(c.drift) > 0.15f) {
            c.slip = fmaxf(c.slip, clampf(fabsf(c.drift) * 1.5f, 0, 1));
            c.v = fmaxf(0, c.v - fabsf(sinf(c.drift)) * 6.0f * dt);   // sideways scrubs speed
        }
        travel = c.yaw - c.drift;
    }
    if (c.brake > 0.5f && c.v > 30 && fabsf(c.yaw) > 0.05f) c.slip = fmaxf(c.slip, 0.3f);

    // ---- move along the track
    float k = track.curvAt(c.s);
    float denom = fmaxf(1 - c.d * k, 0.5f);
    float ds = c.v * cosf(travel) / denom * dt;
    float dd = c.v * sinf(travel) * dt;
    c.yaw += (want - k * ds / fmaxf(dt, 1e-4f)) * dt;
    c.yaw = wrapPi(c.yaw);
    c.s = track.wrap(c.s + ds);
    c.total += ds;
    c.d += dd;
    c.wheelSpin += c.v * dt / 0.34f;

    // ---- tyre temperatures: speed, cornering, braking (fronts) and wheelspin (rears) heat them; a wet
    // track cools slicks. They settle over ~25 s. Grip is best between 80 and 105 deg C (tyreGrip).
    {
        float ambient = 24 - 8 * wet;
        float lat = fabsf(c.yawRate * c.v);                       // sideways load, m/s^2
        float side = clampf(c.yawRate * c.v / 18.0f, -1, 1);       // turning left loads the right-hand tyres
        float base = ambient + 52 * powf(fminf(c.v, 90.0f) / 70.0f, 0.8f) + 22 * fminf(lat / 15.0f, 1.5f);
        bool slick = c.tyre <= TY_HARD;
        float cool = slick ? 28 * wet : 6 * fmaxf(0, wet - 0.6f) - 10 * fmaxf(0, 0.4f - wet);   // wets overheat on a dry track
        float comp = c.tyre == TY_SOFT ? 5 : (c.tyre == TY_HARD ? -5 : 0);
        for (int k = 0; k < 4; k++) {
            bool front = k < 2, leftSide = (k & 1) == 0;
            float tgt = base + comp - cool + (front ? 18 * c.brake * fminf(c.v / 50.0f, 1) : 12 * c.throttle + 45 * c.slip)
                      + (leftSide ? -8 : 8) * side;
            if (c.pit == PIT_STOP || c.v < 1) tgt = fminf(tgt, c.tyreT[k]);   // standing: they only cool
            c.tyreT[k] += (tgt - c.tyreT[k]) * fminf(1.0f, dt / 25.0f);
        }
    }

    // ---- tyres and fuel (scaled with the race clock)
    if (ds > 0 && session == SES_RACE) {
        float life = tyreKm[c.tyre] * 1000;
        bool slick = c.tyre <= TY_HARD;
        float abuse = 1 + c.slip * 1.5f + (slick ? 0 : fmaxf(0, 0.5f - wet) * (c.tyre == TY_WET ? 4 : 2)) + c.dmg[DM_SUSP];
        c.wear = fminf(1, c.wear + ds / life * consumeScale * abuse);
        // the tank is sized to the race (Race::start); a little more on the Attack map
        c.fuel = fmaxf(0, c.fuel - ds / (tankKm * 1000) * consumeScale * (0.6f + 0.4f * c.throttle) * MAP_FUEL[map] / 0.95f);
        c.stint += dt * consumeScale;          // driving time: same clock as fuel and tyres (a tank ~65 min)
    }

    // ---- barriers, pit wall
    float run = c.d > 0 ? tp.outL : tp.outR;
    float wall = hw + run - 1.1f;
    float rel = pitRel(c.s);
    bool wallZone = pitRel(pitWallFrom) <= rel && rel <= pitRel(pitWallTo);
    if (c.d > 0 && pitLane) wall = boxD + 3.0f;
    if (c.d > 0 && wallZone) {
        // pit wall between the track and the pit lane
        float pw = hw + 2.3f;
        if (!pitLane && c.d > pw - 1.0f) { c.d = pw - 1.0f; if (c.yaw > 0) { c.v *= 0.7f; c.yaw *= 0.3f; addDamage(c, DM_LEFT, 0.05f + c.v * 0.002f); } }
        if (pitLane && c.d < pw + 1.0f && c.pit != PIT_NONE) c.d = pw + 1.0f;
        wall = boxD + 3.0f;
    }
    if (fabsf(c.d) > wall) {
        float sg = c.d > 0 ? 1.0f : -1.0f;
        c.d = sg * wall;
        float into = sinf(c.yaw - c.drift) * sg;
        if (into > 0) {
            float hit = c.v * into;
            if (hit > 4) addDamage(c, sg > 0 ? DM_LEFT : DM_RIGHT, hit * 0.012f);
            if (hit > 12) { addDamage(c, DM_SUSP, hit * 0.006f); addDamage(c, frand() < 0.5f ? DM_NOSE : DM_WING, hit * 0.008f); }
            c.v *= 1.0f - clampf(into * 1.6f, 0.1f, 0.7f); c.yaw *= 0.25f; c.drift *= 0.25f;
        }
    }
    if (fabsf(c.yaw) > 1.4f && !c.player) c.yaw *= 0.98f;

    // ---- gears
    if (!c.player || !set.manualGears) {
        if (rpmFrac > 0.965f && c.gear < 7) c.gear++;
        else if (c.gear > 1 && c.rpm < c.def->maxRpm * 0.6f && !c.slowLim) {
            float lower = c.def->maxRpm * c.v / (vt * 1.03f * GEAR[c.gear - 1] * RATIO[c.su.gearing]);
            if (lower < c.def->maxRpm * 0.93f) c.gear--;
        }
    }
}

void Race::drive(Car& c, float dt) {
    int cls = (int)c.def->cls;
    float la = 9 + 0.38f * c.v;
    float sAhead = track.wrap(c.s + la);
    int ia = track.index(sAhead);
    int i = track.index(c.s);
    float hw = track.pts[i].hw;
    float target = line[ia] + c.lineBias + c.avoid;
    target = clampf(target, -(hw - 1.1f), hw - 1.1f);
    int ib = track.index(c.s + c.v * 0.25f + 4);
    float vt = fminf(vmax[cls][ib], vmax[cls][i]) * c.skill * c.drv[c.driver].pace * c.def->cornerMul;
    // a tired driver's slip: off the line and off the pace for a moment
    if (c.mistake > 0) { target = clampf(target + c.mistakeSide * 2.5f, -(hw - 0.6f), hw - 0.6f); vt *= 0.86f; }
    // wet track / worn tyres / damage: the AI eases off as much as the grip has gone
    vt *= sqrtf(fmaxf(0.3f, tyreGrip(c)));
    // sliding wide: lift and let the front bite again
    if (c.slip > 0.2f) vt = fminf(vt, c.v * (1 - 0.12f * c.slip));
    if (c.offTrack) vt = fminf(vt, 30);
    if (c.fuel <= 0) vt = fminf(vt, 12);
    // FCY / slow zone: 80 km/h (a touch under), braking for a zone that starts ahead
    if (fcy > 0 || !zones.empty()) {
        if (slowAt(c.s)) vt = fminf(vt, slowV - 0.6f);
        else {
            float need = (c.v * c.v - slowV * slowV) / (2 * 5.5f) + 40;
            for (float k = 20; k < need; k += 15)
                if (slowAt(c.s + k)) { vt = fminf(vt, sqrtf(slowV * slowV + 2 * 5.5f * fmaxf(0, k - 40))); break; }
        }
    }

    // pit lane / formation / safety car override the racing speed and line
    bool pitting = pitDrive(c, dt);
    if (pitting) {
        float laneT = c.pit == PIT_STOP ? boxD : laneD;
        float relS = pitRel(c.s), relBox = pitRel(boxS(c));
        bool stopping = c.pit == PIT_IN && c.pitPhase != -1;     // (-1: drive-through, no stop)
        if (stopping && relBox - relS < 30) laneT = boxD;
        if (c.pit == PIT_OUT && relS > pitRel(pitOut) - 130) laneT = hw * 0.5f;
        target = laneT;
        vt = c.pit == PIT_STOP ? 0 : 21.4f;   // 80 km/h pit limit (a touch under)
        if (stopping) {
            float toBox = relBox - relS;
            vt = fminf(vt, sqrtf(fmaxf(0, 2 * 6.0f * (toBox - 1))));
        }
        // the entry: ease off before the lane starts
        if (relS < 0 || relS > pitRel(pitOut)) vt = fminf(vt, 26);
    }
    float block = 1e9f;
    if (formation || sc.state) {
        // single file behind the car ahead (or the pace / safety car)
        float aheadTotal = 1e9f, aheadV = 0;
        for (auto& o : cars) {
            if (&o == &c || o.pit != PIT_NONE) continue;
            float gap = o.total - c.total;
            if (gap > 0 && gap < aheadTotal - c.total) { aheadTotal = o.total; aheadV = o.v; }
        }
        float limit = formation ? 26.0f : 34.0f;
        if (sc.state && sc.total > c.total && sc.total < aheadTotal) { aheadTotal = sc.total; aheadV = sc.v; }
        float gap = aheadTotal - c.total;
        float want = formation ? 14.0f : 22.0f;
        if (gap < 400) limit = clampf(aheadV + (gap - want) * 0.4f, 0, limit + 15);
        if (!pitting) vt = fminf(vt, limit);
        if (formation) target = (c.position % 2 ? 1.0f : -1.0f) * hw * 0.42f;
    } else if (!pitting) {
        // being lapped / the other class coming through: move over to the side chosen in blueFlags,
        // gently, and lift a touch when the faster car is right there
        if (c.blue > 0 && c.yieldSide != 0) {
            float want = c.yieldSide * (hw - 2.0f);
            target = target + (want - target) * 0.85f;
            if (c.blueGap < 35) vt *= 0.975f;
        }
        for (auto& o : cars) {
            if (&o == &c || o.pit != PIT_NONE) continue;
            float gap = lapGap(c.s, o.s);
            // a slower-class or lapped car is seen from further away, so the pass is planned early
            bool lapping = o.blue > 0 && o.blueBy == (int)(&c - &cars[0]);
            if (gap < 0 || gap > (lapping ? 70.0f : 28.0f)) continue;
            if (fabsf(o.d - (c.d + c.avoid * 0.3f)) > (lapping ? 3.2f : 2.4f)) continue;
            if (o.v > c.v + 1) continue;
            float room = c.d > o.d ? (hw - 1.2f) - o.d : o.d + (hw - 1.2f);
            float side = c.d > o.d ? 1.0f : -1.0f;
            if (lapping && o.yieldSide != 0) side = -o.yieldSide;        // go the other way to the one it moves to
            else if (room < 2.6f) side = -side;
            c.avoid = clampf(c.avoid + side * 3.0f * dt, -2.8f, 2.8f);
            if (gap < 9) block = fminf(block, o.v + 0.5f);
            // closing fast on a car still in the way (a GT through a corner): ease off in time, not at the last moment
            float closing = c.v - o.v;
            if (closing > 3 && fabsf(o.d - c.d) < 2.3f && gap < 9 + closing * 1.4f) block = fminf(block, o.v + 0.5f + (gap - 6) * 0.35f);
        }
    }
    c.avoid *= expf(-0.35f * dt);
    vt = fminf(vt, block);
    if (c.startDelay > 0) vt = 0;

    if (c.v < vt - 1.0f) { c.throttle = 1; c.brake = 0; }
    else if (c.v > vt + 0.8f) { c.throttle = 0; c.brake = clampf((c.v - vt) / 5.0f, 0.25f, 1); }
    else { c.throttle = vt < 1 ? 0 : 0.55f; c.brake = vt < 1 ? 1 : 0; }
    if (countdown > 0) { c.throttle = 0; c.brake = 1; }

    float wantYaw = atan2f(target - c.d, la);
    float err = wrapPi(wantYaw - c.yaw);
    c.yawRate = track.curvAt(c.s + la * 0.5f) * c.v + err * (c.v / la) * 2.2f + err * 0.6f;
    c.steer = clampf(-c.yawRate * WHEELBASE / fmaxf(c.v, 5) * 3.0f, -1, 1);
}

void Race::collide(float dt) {
    for (size_t a = 0; a < cars.size(); a++)
        for (size_t b = a + 1; b < cars.size(); b++) {
            Car &A = cars[a], &B = cars[b];
            if ((A.pit == PIT_STOP) || (B.pit == PIT_STOP) || inPitLane(A) != inPitLane(B)) continue;
            float ds = lapGap(A.s, B.s);          // by position on the lap: lapped cars and practice traffic collide too
            if (fabsf(ds) > CAR_LEN) continue;
            float dd = B.d - A.d;
            if (fabsf(dd) > CAR_W) continue;
            float along = CAR_LEN - fabsf(ds), side = CAR_W - fabsf(dd);
            if (side < along * 0.5f) {
                float push = side * 0.5f + 0.01f, sg = dd > 0 ? 1.0f : -1.0f;
                B.d += sg * push; A.d -= sg * push;
                A.yaw *= 0.9f; B.yaw *= 0.9f;
                float rub = fabsf(sinf(A.yaw) * A.v - sinf(B.yaw) * B.v) + 1;
                if (rub > 3) { addDamage(A, sg > 0 ? DM_LEFT : DM_RIGHT, rub * 0.05f * dt); addDamage(B, sg > 0 ? DM_RIGHT : DM_LEFT, rub * 0.05f * dt); }
            } else {
                Car& rear = ds > 0 ? A : B; Car& front = ds > 0 ? B : A;
                float back = along + 0.02f;
                float closing = rear.v - front.v;
                rear.s = track.wrap(rear.s - back); rear.total -= back;
                if (closing > 0) {
                    if (closing > 4) { addDamage(rear, DM_NOSE, closing * 0.018f); addDamage(front, DM_WING, closing * 0.015f);
                        if (getenv("EMR_DBG2")) fprintf(stderr, "hit %s#%d -> %s#%d closing %.1f blue %.0f/%.0f yield %.0f/%.0f d %.1f/%.1f curv %.4f\n", rear.def->cls ? "GT" : "HY", rear.number, front.def->cls ? "GT" : "HY", front.number, closing, rear.blue, front.blue, rear.yieldSide, front.yieldSide, rear.d, front.d, track.curvAt(rear.s)); }
                    // contact penalty (race control): hitting a car from behind hard enough
                    if (session == SES_RACE && time > 60 && closing > 11 && !sc.state) {
                        rear.contactWarn++; if (getenv("EMR_DBG")) fprintf(stderr, "contact #%d -> #%d closing %.1f t %.0f d %.1f/%.1f pit %d/%d\n", rear.number, front.number, closing, time, rear.d, front.d, rear.pit, front.pit);
                        if (rear.contactWarn >= 2) { rear.penalty += 5; rear.contactWarn = 0; if (rear.player) message("Penalty: +5 s for causing a collision", RGBA(255, 120, 60), RADIO_PENALTY); }
                        else if (rear.player) message("Warning: avoidable contact", RGBA(255, 200, 60), RADIO_CONTACT);
                    }
                    float m = (rear.v + front.v) * 0.5f; rear.v = m - 0.6f; front.v = m + 0.3f;
                }
            }
        }
}

void Race::rank() {
    std::vector<int> idx(cars.size());
    for (size_t i = 0; i < idx.size(); i++) idx[i] = (int)i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        const Car &A = cars[a], &B = cars[b];
        if (session != SES_RACE) {
            if ((A.bestLap > 0) != (B.bestLap > 0)) return A.bestLap > 0;
            if (A.bestLap > 0) return A.bestLap < B.bestLap;
            return A.total > B.total;
        }
        if (A.finished != B.finished) return A.finished;
        if (A.finished) {
            if (A.lap != B.lap) return A.lap > B.lap;
            return A.finishTime + A.penalty < B.finishTime + B.penalty;   // time penalties count at the flag
        }
        return A.total > B.total;
    });
    for (size_t k = 0; k < idx.size(); k++) cars[idx[k]].position = (int)k + 1;
}

std::vector<int> Race::qualifyingOrder() const {
    std::vector<const Car*> v;
    for (auto& c : cars) v.push_back(&c);
    std::stable_sort(v.begin(), v.end(), [](const Car* a, const Car* b) {
        if (a->def->cls != b->def->cls) return a->def->cls < b->def->cls;
        if ((a->bestLap > 0) != (b->bestLap > 0)) return a->bestLap > 0;
        return a->bestLap < b->bestLap;
    });
    std::vector<int> out;
    for (auto* c : v) out.push_back(c->number);
    return out;
}

void Race::endSession() {
    if (session == SES_RACE) return;
    for (auto& c : cars) if (!c.finished) { c.finished = true; c.finishTime = time; }
    over = true;
}

void Race::update(float dt, float steer, float throttle, float brake, int shift, bool aiPlayer) {
    sfxEvents.clear();
    autoPlayer = aiPlayer;
    if (countdown > 0) {
        countdown -= dt;
        for (auto& c : cars) { c.v = 0; c.rpm = c.def->idleRpm + (c.def->maxRpm - c.def->idleRpm) * 0.35f * (c.player ? throttle : 0.5f + 0.2f * sinf(countdown * 9 + c.number)); pose(c); }
        if (countdown <= 0) message("LIGHTS OUT - GO!", RGBA(60, 255, 90), RADIO_GO);
        return;
    }
    time += dt;
    weather(dt);
    raceControl(dt);
    bool leaderDone = false;
    for (auto& c : cars) if (c.finished) leaderDone = true;
    if (session != SES_RACE && time >= sessionLength) { chequered = true; leaderDone = true; }
    if (session == SES_RACE && set.raceMin > 0 && !formation && time - greenTime >= set.raceMin * 60.0f && !chequered) { chequered = true; message("Chequered flag: the leader's next lap is the last", RGBA(255, 255, 255), RADIO_LASTLAP); }
    blueFlags(dt);
    updateDrs(dt);
    for (auto& c : cars) {
        if (c.hold) { c.v = 0; continue; }
        if (c.startDelay > 0) { c.startDelay -= dt; if (c.startDelay <= 0 && c.player) message("In the car - go!", RGBA(60, 255, 90)); }
        bool human = c.player && !aiPlayer && !c.finished && c.pit == PIT_NONE && !c.plan.aiDrives;
        // the player's car is taken into the pit lane automatically once the entry is reached
        if (human && c.wantPit && pitRel(c.s) >= -5 && pitRel(c.s) < 40) human = false;
        c.human = human && c.startDelay <= 0;
        if (human && c.startDelay <= 0) {
            c.throttle = throttle; c.brake = brake;
            // F1 06-style handling: a little more lock at medium speed for sharp turn-in
            float lock = 0.34f / (1 + c.v / 20.0f) + 0.03f;
            float delta = -steer * lock;
            c.steer = steer;
            c.yawRate = c.v * tanf(delta) / WHEELBASE;
            // steering help: never ask for (much) more turn than the tyres can hold, so a twitch of the
            // stick doesn't step the rear out. Light leaves a little slide; strong keeps it on the grip
            if (steerHelp > 0 && c.v > 8 && playerLatMax > 0) {
                float cap = playerLatMax / c.v * (steerHelp == 1 ? 1.18f : 1.0f);
                c.yawRate = clampf(c.yawRate, -cap, cap);
            }
            if (shift > 0 && c.gear < 7) c.gear++;
            if (shift < 0 && c.gear > 1) c.gear--;
        } else {
            if (c.player && c.startDelay > 0) { c.throttle = 0; c.brake = 1; }
            else drive(c, dt);
            if (c.finished && c.pit == PIT_NONE) { c.throttle = 0.2f; c.yawRate *= 0.5f; c.brake = c.v > 30 ? 0.3f : 0; }
        }
        driverUpdate(c, dt);
        if (c.pit == PIT_STOP) { pitStop(c, dt); continue; }
        int lapsBefore = (int)floorf(c.total / track.length);
        physics(c, dt);
        if (c.player) playerThrPrev = c.throttle;
        // the FCY / slow zone limiter: once down to 80 km/h in the zone, the car holds it
        if (c.player) {
            if (c.pit != PIT_NONE || !slowAt(c.s)) c.slowLim = false;
            else {
                if (c.v <= slowV + 0.3f) c.slowLim = true;
                if (c.slowLim && c.v > slowV) c.v = slowV;
                // cruising at 80: the highest gear that keeps the engine above ~35 % revs
                if (c.slowLim && !set.manualGears) {
                    float frac = c.rpm / c.def->maxRpm * GEAR[c.gear];
                    int gBest = 1;
                    for (int gg = 7; gg >= 1; gg--) if (frac / GEAR[gg] >= 0.35f) { gBest = gg; break; }
                    c.gear = gBest;
                }
            }
        }
        penaltyCheck(c, dt);
        int lapsAfter = (int)floorf(c.total / track.length);
        if (set.timeTrial && c.player && !c.outLap && c.total > 0) {
            float lt = time - c.lapStart;
            if (lapsAfter == lapsBefore) {
                while (ghostRec.size() < 9000 && ghostRec.size() * GHOST_DT <= lt) ghostRec.push_back({c.s, c.d, c.yaw});
                if ((c.offTrack || c.spinning) && !lapInvalid) { lapInvalid = true; message("Lap invalid: off the track", RGBA(255, 120, 60)); }
            }
            // live gap to the record: when was the ghost where we are now?
            ttDeltaOn = false;
            if (ghostLap > 0 && ghost.size() > 2 && lt > 1) {
                float p = track.wrap(c.s);
                int lo = 0, hi = (int)ghost.size() - 1;
                while (lo < hi) { int m = (lo + hi) / 2; if (track.wrap(ghost[m].s) < p && !(m > 10 && track.wrap(ghost[m].s) < 50)) lo = m + 1; else hi = m; }
                if (lo > 0 && lo < (int)ghost.size()) {
                    float s0 = track.wrap(ghost[lo - 1].s), s1 = track.wrap(ghost[lo].s);
                    float k = s1 > s0 ? clampf((p - s0) / (s1 - s0), 0, 1) : 0;
                    ttDelta = lt - (lo - 1 + k) * GHOST_DT;
                    ttDeltaOn = true;
                }
            }
        }
        if (c.total > 0) {                         // timing points for the live gaps
            int m = (int)(c.total / MARK_M);
            if (m > c.lastMark) {
                c.lastMark = m; c.markTime = time;
                if (m >= (int)markFirst.size()) markFirst.resize(m + 1, -1.0f);
                if (markFirst[m] < 0) markFirst[m] = time;
            }
        }
        if (lapsAfter > lapsBefore && lapsAfter >= 1) {
            if (session == SES_RACE) c.tyreLaps++;
            float lt = time - c.lapStart;
            bool pb = false;
            bool ttLap = set.timeTrial && c.player;
            if (c.outLap) c.outLap = false;           // came out of the garage this lap: no time
            else if (ttLap && lapInvalid) {
                c.lastLap = lt;                       // shown, but it doesn't count
                message("Invalid lap - not counted", RGBA(255, 120, 60));
            } else {
                c.lastLap = lt;
                pb = c.bestLap <= 0 || lt < c.bestLap;
                if (pb) c.bestLap = lt;
            }
            if (ttLap) {
                if (pb && !lapInvalid && lt > 10 && (ghostLap <= 0 || lt < ghostLap)) {
                    bool first = ghostLap <= 0;
                    ghost.swap(ghostRec); ghostLap = lt; newRecord = true;
                    char m[48], t[16]; int ms = (int)(lt * 1000 + 0.5f);
                    snprintf(t, sizeof t, "%d:%02d.%03d", ms / 60000, (ms / 1000) % 60, ms % 1000);
                    snprintf(m, sizeof m, first ? "First record: %s" : "NEW RECORD: %s", t);
                    message(m, RGBA(200, 140, 255), RADIO_BESTLAP);
                }
                // a fresh start for every lap: full fuel, new tyres, no damage, no warnings
                ghostRec.clear(); lapInvalid = false;
                c.fuel = c.su.fuel / 100.0f; c.wear = 0; c.tyreLaps = 0; c.coldTyres();
                for (float& x : c.dmg) x = 0;
                c.wingOff = c.noseOff = false;
                c.limitWarn = 0; c.penalty = 0; c.driveThrough = false;
            }
            c.lapStart = time;
            c.lap = lapsAfter;
            if (c.driveThrough && --c.dtLaps <= 0 && c.pit == PIT_NONE) {
                c.driveThrough = false; c.penalty += 30;
                if (c.player) message("Drive-through not served: +30 s", RGBA(255, 90, 60), RADIO_PENALTY);
            }
            if (c.player && pb && c.lap > 1) radioPending = RADIO_BESTLAP;
            bool done = session == SES_RACE ? (set.raceMin > 0 ? chequered : (c.lap >= set.laps || leaderDone)) : chequered;
            if (session == SES_RACE && set.raceMin > 0 && !leaderDone && c.position != 1) done = false;
            if (!c.finished && done) {
                c.finished = true; c.finishTime = time;
                if (session == SES_RACE) leaderDone = true;
                if (c.player) radioPending = RADIO_FINISH;
            }
            if (c.player && session == SES_RACE && set.raceMin <= 0 && c.lap == set.laps - 1 && set.laps > 1) radioPending = RADIO_LASTLAP;
            strategy(c);
        } else if (lapsAfter == 0 && lapsBefore < 0) {
            c.lapStart = time;
        }
    }
    // the session ends while the player sits in the garage
    if (session != SES_RACE && chequered && inGarage() && !player().finished) { player().finished = true; player().finishTime = time; }
    collide(dt);
    particles(dt);
    for (auto& c : cars) pose(c);
    rank();
    Car& me = player();
    if (session == SES_RACE) { if (me.finished && time - me.finishTime > 8 && me.pit == PIT_NONE) over = true; }
    else if (me.finished) {
        bool all = true;
        for (auto& c : cars) if (!c.finished && c.pit == PIT_NONE) all = false;
        if ((all && time - me.finishTime > 3) || time > sessionLength + 90) over = true;
    }
}

// ------------------------------------------------------------------ time trial ghost
bool Race::ghostAt(float t, GhostPt& out) const {
    if (ghost.size() < 2 || t < 0) return false;
    float f = t / GHOST_DT;
    int i = (int)f;
    if (i >= (int)ghost.size() - 1) return false;
    float k = f - i;
    const GhostPt& a = ghost[i]; const GhostPt& b = ghost[i + 1];
    float ds = b.s - a.s;
    if (ds > track.length * 0.5f) ds -= track.length; else if (ds < -track.length * 0.5f) ds += track.length;
    out.s = track.wrap(a.s + ds * k);
    out.d = a.d + (b.d - a.d) * k;
    out.yaw = a.yaw + (b.yaw - a.yaw) * k;
    return true;
}

// ghost file: 'EGH1', lap time, number of points, then the points
bool Race::saveGhost(const char* path) const {
    if (ghostLap <= 0 || ghost.empty()) return false;
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    u32 magic = 0x31484745, n = (u32)ghost.size();
    bool ok = fwrite(&magic, 4, 1, f) == 1 && fwrite(&ghostLap, 4, 1, f) == 1 && fwrite(&n, 4, 1, f) == 1 &&
              fwrite(ghost.data(), sizeof(GhostPt), n, f) == n;
    fclose(f);
    return ok;
}

bool Race::loadGhost(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    u32 magic = 0, n = 0; float lap = 0;
    bool ok = fread(&magic, 4, 1, f) == 1 && magic == 0x31484745 && fread(&lap, 4, 1, f) == 1 && fread(&n, 4, 1, f) == 1 && n > 1 && n < 20000;
    if (ok) {
        ghost.resize(n);
        ok = fread(ghost.data(), sizeof(GhostPt), n, f) == n;
    }
    fclose(f);
    if (!ok) { ghost.clear(); return false; }
    ghostLap = lap;
    return true;
}

// ------------------------------------------------------------------ DRS
// Zones on the circuit's longest straights: the flap may open from 100 m after the straight
// starts to 140 m before it ends; the detection point is 150 m before the zone. In the race a car
// gets DRS for a zone when it crosses that zone's detection point within a second of the car ahead
// (from lap 3, dry track, no safety car). In practice and qualifying it is free in the zones.
void Race::setupDrs() {
    drsZones.clear();
    if (!ep().drs) return;
    int n = track.n;
    std::vector<int> straight(n);
    for (int i = 0; i < n; i++) {
        float k = 0; for (int j = -4; j <= 4; j++) k += fabsf(track.pts[(i + j + n) % n].curv);
        straight[i] = k / 9 < 1.0f / 700.0f;
    }
    // runs of straight road (wrapping round the line)
    int start = 0; while (start < n && straight[start]) start++;
    if (start >= n) return;
    struct Run { float from, len; };
    std::vector<Run> runs;
    for (int i = 0, k = start; i < n; ) {
        int a = (k + i) % n;
        if (!straight[a]) { i++; continue; }
        int len = 0; while (i + len < n && straight[(k + i + len) % n]) len++;
        runs.push_back({a * track.step, len * track.step});
        i += len;
    }
    std::sort(runs.begin(), runs.end(), [](const Run& a, const Run& b) { return a.len > b.len; });
    int want = era == ERA_GE ? 3 : 2;
    for (auto& r : runs) {
        if ((int)drsZones.size() >= want || r.len < 450) break;
        DrsZone z;
        z.from = track.wrap(r.from + 100); z.to = track.wrap(r.from + r.len - 140); z.detect = track.wrap(z.from - 150);
        drsZones.push_back(z);
    }
}

int Race::drsZoneAt(float s) const {
    for (int k = 0; k < (int)drsZones.size(); k++) {
        float a = drsZones[k].from, len = track.wrap(drsZones[k].to - a);
        if (track.wrap(s - a) < len) return k;
    }
    return -1;
}

void Race::updateDrs(float dt) {
    if (drsZones.empty()) { for (auto& c : cars) { c.drs = false; c.drsFlap = fmaxf(0, c.drsFlap - dt * 4); } return; }
    int leadLap = 0; for (auto& c : cars) leadLap = c.lap > leadLap ? c.lap : leadLap;
    bool wasOn = drsOn;
    drsOn = ep().drs && wet < 0.25f && rain < 0.2f && !sc.state && fcy <= 0 && !formation && countdown <= 0 &&
            (session != SES_RACE || leadLap >= 2);
    if (session == SES_RACE && drsOn && !wasOn) message("DRS enabled", RGBA(80, 230, 140));
    if (session == SES_RACE && !drsOn && wasOn) message("DRS disabled", RGBA(255, 200, 60));
    for (auto& c : cars) {
        int z = drsZoneAt(c.s);
        // detection: crossing a zone's detection point
        for (int k = 0; k < (int)drsZones.size(); k++) {
            float d = track.wrap(c.s - drsZones[k].detect);
            if (d < c.v * dt + 0.5f && c.pit == PIT_NONE) {
                bool ok = session != SES_RACE;
                if (!ok) {
                    // the car ahead on the road (a lapped car counts too, as in the rules)
                    float best = 1e9f;
                    for (auto& o : cars) {
                        if (&o == &c || o.pit != PIT_NONE) continue;
                        float g = lapGap(c.s, o.s);
                        if (g > 0 && g < best) best = g;
                    }
                    ok = best < fmaxf(c.v, 20.0f) * 1.0f;
                }
                c.drsZone = ok ? k : -1;
            }
        }
        bool open = drsOn && z >= 0 && z == c.drsZone && c.brake < 0.2f && c.pit == PIT_NONE && !c.wingOff && c.v > 22;
        if (z < 0 && c.drs) c.drsZone = -1;          // used up: the next zone needs a new detection
        if (c.player && open && !c.drs) message("DRS", RGBA(80, 230, 140));
        c.drs = open;
        c.drsFlap = clampf(c.drsFlap + (open ? dt * 5 : -dt * 7), 0, 1);
    }
}
