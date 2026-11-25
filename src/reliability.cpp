// Car realism: brake and engine temperatures, the slipstream, burst tyres and mechanical problems
// (engine, gearbox, turbo, suspension, punctures, lost wheels, leaks, throttle / brakes, steering),
// and cars that retire. Which of these are on comes from the settings (Car realism, Race::real).
// 1980 cars break far more often than the modern ones, as they did.
#include "race.h"
#include <cstdio>
#include <cstring>
#include <cmath>

const char* RETIRE_SHORT[MF_COUNT + 1] = {"Engine", "Gearbox", "Turbo", "Suspension", "Puncture", "Wheel", "Leak",
                                          "Throttle", "Steering", "Accident"};

static uint32_t fseed = 4242;
static float frnd() { fseed = fseed * 1664525u + 1013904223u; return (fseed >> 8) * (1.0f / 16777216.0f); }

// failures per car per race hour (at normal stress): 1980 cars retired in droves
static const float FAIL_RATE[ERA_COUNT] = {0.55f, 0.07f, 0.06f, 0.045f};
// which part goes (weights)
static const float FAIL_W[MF_COUNT] = {0.26f, 0.18f, 0.07f, 0.10f, 0.10f, 0.03f, 0.12f, 0.09f, 0.05f};

static bool turbo(const Car& c) { return c.def->era >= ERA_HYBRID || (c.def->engine && strstr(c.def->engine, "urbo")); }

void Race::slipstream(float dt) {
    // one pass over the other cars for both: turbulent air (always on: it's the aero) and the tow
    bool on = real.on(real.slipstream);
    for (auto& c : cars) {
        float dirty = 0, want = 0;
        bool running = !c.rel.retired && c.pit == PIT_NONE;
        bool lookDirty = running && c.v > 20, lookTow = on && running && c.v > 25;
        if (lookDirty || lookTow)
            for (auto& o : cars) {
                if (&o == &c || o.rel.retired || o.pit != PIT_NONE) continue;
                float gap = o.total - c.total;
                if (gap < 3 || gap > 40) continue;
                float side = fabsf(o.d - c.d);
                // close behind another car the front loses downforce
                if (lookDirty && gap <= 30 && side <= 2.6f) dirty = fmaxf(dirty, (1 - (gap - 3) / 27.0f) * (1 - side / 2.6f * 0.5f));
                // the closest car up to 40 m ahead, nearly in line: less drag
                if (lookTow && gap >= 4 && side <= 1.6f) want = fmaxf(want, 1 - (gap - 4) / 36.0f);
            }
        Handling& H = handOf(c);
        H.dirty += (dirty - H.dirty) * fminf(1.0f, dt * 4);
        c.rel.tow += (want - c.rel.tow) * fminf(1.0f, dt * 3);
    }
}

void Race::reliability(Car& c, float dt) {
    CarRel& R = c.rel;
    bool mine = realFor(c);
    bool racing = session == SES_RACE && !formation && countdown <= 0 && !c.finished && c.pit == PIT_NONE && time - greenTime > 3;
    float rs = dt * consumeScale;                    // race seconds (temperatures settle on the car's own clock)
    float rpmFrac = c.rpm / fmaxf(c.def->maxRpm, 1);

    // ---- brake discs: braking heats them, air through the ducts cools them
    if (real.on(real.brakes) && mine) {
        float heat = c.brake * c.v * (era == ERA_90S ? 3.6f : 3.0f);
        float cool = (R.brakeT - (24 - 6 * wet)) * (0.10f + c.v * 0.0045f);
        R.brakeT = fmaxf(20, R.brakeT + (heat - cool) * dt);
    } else R.brakeT = 500;

    // ---- engine water: revs and the throttle heat it, speed cools it, sitting in a slipstream starves the radiators
    if (real.on(real.cooling) && mine) {
        float tgt = 86 + 10 * c.throttle * rpmFrac + 20 * R.tow + (c.v < 15 ? 12 : 0) - fminf(c.v, 80.0f) * 0.05f + (c.su.map == MAP_ATTACK ? 3 : 0);
        R.engT += (tgt - R.engT) * fminf(1.0f, dt / 9.0f);
        if (R.leakT > 0) R.engT += 1.8f * rs / 6.0f;   // the leak: it climbs and climbs
        if (R.engT > 122 && racing) {
            c.dmg[DM_ENGINE] = fminf(1.0f, c.dmg[DM_ENGINE] + (R.engT - 122) * 0.0012f * rs);
            if (c.dmg[DM_ENGINE] > 0.97f && !R.retired) { failure(c, MF_ENGINE); return; }
        }
        R.overheatMsg -= dt;
        if (c.player && R.engT > 114 && R.overheatMsg <= 0 && racing) {
            message("Engine temperature high - get out of the tow", RGBA(255, 150, 60), RADIO_OVERHEAT);
            R.overheatMsg = 40;
        }
    } else R.engT = 90;

    if (!racing || R.retired) return;

    // ---- things already broken
    if (R.leakT > 0) {
        R.leakT -= rs;
        if (R.leakT <= 0) { R.leakT = 0; failure(c, MF_ENGINE); return; }
    }
    if (R.cut > 0) R.cut -= dt;
    else if (R.nextCut > 0) {
        R.nextCut -= dt;
        if (R.nextCut <= 0) { R.cut = 0.3f + frnd() * 0.6f; R.nextCut = 3 + frnd() * 9; }
    }
    if (R.steerT > 0) {
        R.steerT += dt;
        if (R.steerT > 3.5f) { retire(c, MF_STEERING); return; }
    }

    // ---- worn-out tyres burst
    if (real.on(real.blowTyre) && mine && R.flat < 0 && c.wear > 0.9f && c.v > 30
        && frnd() < (c.wear - 0.9f) * 0.25f * rs) {
        R.flat = (int)(frnd() * 4) & 3;
        if (c.player) message("Tyre blown! Box, box", RGBA(255, 90, 60), RADIO_PUNCTURE);
        c.wantPit = true;
    }

    // ---- a new failure: a small chance every second, more when the car is worked hard or already hurt
    if (!mine || !real.anyMech() || c.v < 5) return;
    float stress = 1 + 2.5f * fmaxf(0.0f, rpmFrac - 0.9f) + (c.su.map == MAP_ATTACK ? 0.4f : 0) + fmaxf(0.0f, R.engT - 108) * 0.06f
                 + c.damage() * 1.5f + (c.offTrack ? 0.5f : 0);
    float hazard = FAIL_RATE[era % ERA_COUNT] / 3600.0f * stress * rs;
    if (frnd() >= hazard) return;
    float w[MF_COUNT], sum = 0;
    for (int k = 0; k < MF_COUNT; k++) {
        w[k] = real.mechOn(k) && !(R.fails & (1u << k)) ? FAIL_W[k] : 0;
        if (k == MF_TURBO && !turbo(c)) w[k] = 0;
        if (k == MF_PUNCTURE && (R.flat >= 0)) w[k] = 0;
        if (k == MF_PUNCTURE) w[k] *= 1 + 3 * c.wear;
        sum += w[k];
    }
    if (sum <= 0) return;
    float pick = frnd() * sum;
    for (int k = 0; k < MF_COUNT; k++) { pick -= w[k]; if (pick < 0 && w[k] > 0) { failure(c, k); return; } }
}

void Race::failure(Car& c, int kind) {
    CarRel& R = c.rel;
    R.fails |= 1u << kind;
    bool me = c.player;
    switch (kind) {
    case MF_ENGINE:
        c.dmg[DM_ENGINE] = 1;                                   // smoke and flames from the back (Race::particles)
        if (me) message("Engine's gone - pull over, pull over", RGBA(255, 70, 50), RADIO_ENGINE);
        retire(c, MF_ENGINE);
        break;
    case MF_GEARBOX:
        if (frnd() < 0.45f) {
            if (me) message("Gearbox failure - stop the car", RGBA(255, 70, 50), RADIO_GEARBOX);
            retire(c, MF_GEARBOX);
        } else {
            int top = c.def->era == ERA_90S ? 5 : 7;
            R.gearMax = top - 2 - (frnd() < 0.4f ? 1 : 0);
            if (me) { char m[64]; snprintf(m, sizeof m, "Gearbox problem: no gears above %d", R.gearMax); message(m, RGBA(255, 150, 60), RADIO_GEARBOX); }
        }
        break;
    case MF_TURBO:
        R.powerK = 0.62f;
        if (me) message("Turbo failure - big power loss", RGBA(255, 150, 60), RADIO_TURBO);
        break;
    case MF_SUSPENSION:
        c.dmg[DM_SUSP] = fmaxf(c.dmg[DM_SUSP], 0.85f);
        if (frnd() < 0.5f) {
            if (me) message("Suspension broken - bring it in", RGBA(255, 70, 50), RADIO_SUSPENSION);
            retire(c, MF_SUSPENSION);
        } else if (me) message("Suspension damage - car will pull to one side", RGBA(255, 150, 60), RADIO_SUSPENSION);
        break;
    case MF_PUNCTURE:
        R.flat = (int)(frnd() * 4) & 3;
        c.wantPit = true;
        if (me) message("Puncture! Box this lap", RGBA(255, 90, 60), RADIO_PUNCTURE);
        break;
    case MF_WHEEL:
        if (me) message("You've lost a wheel - stop the car", RGBA(255, 70, 50), RADIO_WHEEL);
        retire(c, MF_WHEEL);
        break;
    case MF_LEAK:
        R.leakT = 40 + frnd() * 80;
        c.dmg[DM_ENGINE] = fmaxf(c.dmg[DM_ENGINE], 0.3f);       // a trail of smoke
        if (me) message("Oil and water leak - temperatures rising", RGBA(255, 150, 60), RADIO_LEAK);
        break;
    case MF_THROTTLE:
        if (frnd() < 0.5f) {
            R.nextCut = 0.5f + frnd() * 2;
            if (me) message("Throttle is cutting out", RGBA(255, 150, 60), RADIO_THROTTLE);
        } else {
            R.brakeWeak = true;
            if (me) message("Brake problem - brake early", RGBA(255, 150, 60), RADIO_BRAKES);
        }
        break;
    case MF_STEERING:
        R.steerT = 0.01f;
        if (me) message("Steering rod broken!", RGBA(255, 70, 50), RADIO_STEERING);
        break;
    }
    if (!me && kind != MF_PUNCTURE && kind != MF_THROTTLE && kind != MF_LEAK && !R.retired) {
        // the others' problems that slow them right down
        if (kind == MF_TURBO || kind == MF_GEARBOX || kind == MF_SUSPENSION) {
            char m[64]; snprintf(m, sizeof m, "#%d %s slowing: %s problem", c.number, c.def->make, RETIRE_SHORT[kind]);
            message(m, RGBA(200, 200, 200));
        }
    }
}

void Race::retire(Car& c, int cause) {
    CarRel& R = c.rel;
    if (R.retired) return;
    R.retired = true; R.cause = cause; R.retireT = time;
    c.wantPit = false; c.drs = false; c.ersBurst = 0;
    // pull over to the side it is nearer, into the run-off
    const TrackPoint& tp = track.pts[track.index(c.s)];
    float side = c.d >= 0 ? 1.0f : -1.0f;
    float run = side > 0 ? tp.outL : tp.outR;
    R.parkD = side * (tp.hw + fminf(fmaxf(run - 1.5f, 1.0f), 6.0f));
    char m[64];
    if (c.player) snprintf(m, sizeof m, "RETIRED - %s", cause < 0 ? "accident" : MECH_NAME[cause]);
    else snprintf(m, sizeof m, "#%d %s out: %s", c.number, c.def->make, cause < 0 ? "accident" : RETIRE_SHORT[cause]);
    message(m, c.player ? RGBA(255, 70, 50) : RGBA(200, 200, 200));
}

// a retired car rolls to a stop at the side of the track and stays there
void Race::retiredDrive(Car& c, float dt) {
    CarRel& R = c.rel;
    c.throttle = 0; c.brake = 0.5f; c.boosting = false; c.human = false;
    c.engineOn = R.cause != MF_ENGINE && R.cause != MF_LEAK ? c.engineOn : false;
    float dec = R.cause == MF_WHEEL || R.cause == MF_SUSPENSION ? 9.0f : 5.0f;
    c.v = fmaxf(0, c.v - dec * dt);
    float ds = c.v * dt;
    c.s = track.wrap(c.s + ds); c.total += ds;
    float dd = clampf(R.parkD - c.d, -1, 1) * fminf(c.v * 0.12f, 3.0f) * dt;
    c.d += dd;
    c.yaw += ((c.v > 1 ? atan2f(dd, fmaxf(ds, 1e-3f)) : c.yaw) - c.yaw) * fminf(1.0f, dt * 3);
    c.rpm = c.engineOn ? c.def->idleRpm : 0;
    c.wheelSpin += c.v * dt / 0.34f;
    c.slip = 0; c.stuck = 0;
    // a 1980 car at the side gets waved yellows; the modern ones a slow zone round it
    if (c.v < 1 && !R.zoned) { R.zoned = true; if (era != ERA_90S && session == SES_RACE) addSlowZone(c.s, 30); }
}
