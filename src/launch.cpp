// The standing-start launch (player only). While the five lights come on, keep the revs in the green
// band with the throttle: holding it climbs, letting go drops, the right stick holds a revs level.
// 2016 and 2025 cars have launch control, so a held throttle sits near the band by itself. Once the
// lights go out, press the clutch (the DRS button or gear up) ONCE. Only the first press counts.
// Reaction time and how close the revs were give a launch quality 0..1:
//   good revs and a quick reaction: a short boost;  too many revs: wheelspin;  too few: bogs down;
//   dropping the clutch before the lights go out: jump start, +5 s.
#include "race.h"
#include "render.h"
#include <cmath>
#include <cstdio>

struct Band { float mid, half, climb, fall, wobble; bool lc; };
// 1980: narrow and low, twitchy revs;  2010: a little wider;  2016 / 2025: launch control
static const Band BAND[ERA_COUNT] = {
    {0.50f, 0.060f, 0.95f, 0.75f, 0.030f, false},
    {0.60f, 0.080f, 0.80f, 0.65f, 0.020f, false},
    {0.68f, 0.100f, 0.80f, 0.65f, 0.010f, true},
    {0.70f, 0.110f, 0.80f, 0.65f, 0.008f, true},
};

static float lrand(u32& s) { s = s * 1664525u + 1013904223u; return (s >> 8) * (1.0f / 16777216.0f); }
static float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

void Race::launchArm() {
    launch = Launch();
    if (session != SES_RACE) return;
    const Band& b = BAND[era % ERA_COUNT];
    launch.state = LS_REV;
    launch.lo = b.mid - b.half; launch.hi = b.mid + b.half;
    launch.seed = 9127u + (u32)set.track * 131u + (u32)(time * 1000.0f);
    launch.hold = 0.2f + lrand(launch.seed) * 1.4f;     // like a real start: the lights hold on a random time
}

float Race::launchCountdown(float dt, float throttle, bool clutch) {
    if (launch.state != LS_REV) return dt;
    if (autoPlayer || formation || player().startDelay > 0) { launch.state = LS_OFF; return dt; }
    const Band& b = BAND[era % ERA_COUNT];
    Launch& L = launch;
    // the revs follow the throttle: analogue (stick) holds a level, the button climbs / falls
    float tgt;
    if (throttle > 0.02f && throttle < 0.98f) tgt = 0.05f + 0.95f * throttle;
    else if (throttle >= 0.98f) tgt = b.lc ? b.mid + 0.02f : 1.0f;
    else tgt = 0.05f;
    float rate = (throttle > 0.02f && throttle < 0.98f) ? 3.0f : (tgt > L.rev ? b.climb : b.fall);
    if (b.lc && throttle >= 0.98f) rate = 2.0f;
    L.rev += fmaxf(-rate * dt, fminf(rate * dt, tgt - L.rev));
    L.rev = clamp01(L.rev + (lrand(L.seed) - 0.5f) * b.wobble * (throttle > 0.02f ? 1.0f : 0.2f));
    // judged once the lights start coming on (the last 4 s)
    if (countdown < 4.0f) { L.sampled += dt; if (L.rev >= L.lo && L.rev <= L.hi) L.inBand += dt; }
    // the clutch dropped before the lights went out: jump start
    if (clutch && !L.dropped && countdown < 4.0f) {
        L.dropped = true; L.jumped = true; L.revAtDrop = L.rev;
        Car& c = player();
        c.penalty += 5;
        message("Jump start! +5 s", RGBA(255, 120, 60), RADIO_PENALTY);
    }
    // all five lights on: they stay on a random hold before going out
    if (countdown - dt <= 0 && L.hold > 0) { L.hold -= dt; return fmaxf(0, countdown - 0.001f); }
    return dt;
}

void Race::launchBefore(Car& c, float dt, bool clutch) {
    Launch& L = launch;
    if (L.state == LS_REV && countdown <= 0) L.state = LS_WAIT;     // lights out
    if (L.state == LS_WAIT) {
        const Band& b = BAND[era % ERA_COUNT];
        // still revving against the clutch
        float tgt = c.throttle >= 0.98f ? (b.lc ? b.mid + 0.02f : 1.0f) : 0.05f + 0.95f * c.throttle;
        float rate = tgt > L.rev ? b.climb : b.fall;
        L.rev += fmaxf(-rate * dt, fminf(rate * dt, tgt - L.rev));
        if (!L.jumped) L.reaction += dt;
        bool go = L.jumped || clutch || L.reaction >= 1.2f;      // (too slow: the car pulls away by itself)
        if (!go) { c.throttle = 0; c.brake = 1; c.rpm = c.def->idleRpm + (c.def->maxRpm - c.def->idleRpm) * L.rev; return; }
        if (!L.jumped) L.revAtDrop = L.rev;
        float mid = (L.lo + L.hi) * 0.5f, half = (L.hi - L.lo) * 0.5f;
        float atDrop = clamp01(1 - fabsf(L.revAtDrop - mid) / (half * 2.0f));
        float held = L.sampled > 0 ? L.inBand / L.sampled : 0;
        float revQ = 0.6f * atDrop + 0.4f * held;
        float reacQ = L.jumped ? 0.5f : clamp01(1 - (L.reaction - 0.15f) / 0.6f);
        L.quality = clamp01(0.55f * revQ + 0.45f * reacQ);
        char m[64]; const char* verdict; u32 col = RGBA(60, 255, 90);
        if (L.revAtDrop > L.hi + 0.04f) { L.effect = 2; L.effectT = 1.6f + (L.revAtDrop - L.hi) * 4; verdict = "Wheelspin!"; col = RGBA(255, 160, 60); }
        else if (L.revAtDrop < L.lo - 0.04f) { L.effect = 3; L.effectT = 1.5f; verdict = "Bogged down"; col = RGBA(255, 160, 60); }
        else if (L.quality > 0.72f) { L.effect = 1; L.effectT = 2.0f; verdict = "Great start!"; }
        else { L.effect = 0; L.effectT = 0; verdict = L.quality > 0.5f ? "Good start" : "Slow start"; col = RGBA(255, 220, 60); }
        if (L.jumped) snprintf(m, sizeof m, "Jump start - %s", verdict);
        else if (L.reaction >= 1.2f) snprintf(m, sizeof m, "Missed the clutch - slow getaway");
        else snprintf(m, sizeof m, "Reaction %.2f s - %s", L.reaction, verdict);
        if (!L.jumped && L.reaction >= 1.2f) { L.effect = 3; L.effectT = 1.0f; }
        message(m, col);
        L.state = LS_RUN;
    }
    if (L.state == LS_RUN) {
        if (L.effect == 2) c.throttle *= 0.6f;            // the rears spinning up: less drive
        if (L.effect == 3) c.throttle *= 0.3f;            // the engine bogs down
    }
}

void Race::launchAfter(Car& c, float dt) {
    Launch& L = launch;
    if (L.state != LS_RUN) return;
    if (L.effect == 1 && c.v < 60) c.v += 2.2f * L.quality * dt;              // a clean getaway
    if (L.effect == 2) { c.slip = fmaxf(c.slip, 0.85f); c.v = fmaxf(0, c.v - 1.5f * dt); }   // smoke, scrubbing
    L.effectT -= dt;
    if (L.effectT <= 0) { L.state = LS_OFF; L.effect = 0; }
}

// the rev bar under the start lights
void Renderer::drawLaunch(const Race& r) {
    const Race::Launch& L = r.launch;
    if (L.state != Race::LS_REV && L.state != Race::LS_WAIT) return;
    if (r.countdown > 6.0f) return;                   // (the grid girls are still there)
    gfx::setDepthTest(false); gfx::setBlend(true);
    float w = 200, h = 12, x = SCREEN_W / 2 - w / 2, y = 64;
    gfx::drawFill2D(x - 4, y - 4, w + 8, h + 26, RGBA(0, 0, 0, 170));
    gfx::drawFill2D(x, y, w, h, RGBA(50, 50, 55, 230));
    gfx::drawFill2D(x + w * L.lo, y, w * (L.hi - L.lo), h, RGBA(40, 200, 70, 230));
    gfx::drawFill2D(x + w * 0.92f, y, w * 0.08f, h, RGBA(200, 40, 30, 230));
    bool in = L.rev >= L.lo && L.rev <= L.hi;
    gfx::drawFill2D(x + w * L.rev - 2, y - 3, 4, h + 6, in ? RGBA(255, 255, 255) : RGBA(255, 160, 60));
    const char* hint = L.state == Race::LS_WAIT ? "CLUTCH!  (DRS / gear up)" : "Keep revs in the green - clutch (DRS) at lights out";
    font.drawCentre(SCREEN_W / 2, y + h + 3, hint, L.state == Race::LS_WAIT ? RGBA(60, 255, 90) : RGBA(220, 220, 220), 0.6f);
    gfx::setBlend(false); gfx::setDepthTest(true);
}
