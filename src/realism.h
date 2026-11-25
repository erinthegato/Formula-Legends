// Car realism settings (settings menu > Car realism) and the mechanical problems a car can have.
#pragma once

enum MechFail { MF_ENGINE = 0, MF_GEARBOX, MF_TURBO, MF_SUSPENSION, MF_PUNCTURE, MF_WHEEL, MF_LEAK, MF_THROTTLE,
                MF_STEERING, MF_COUNT };
extern const char* MECH_NAME[MF_COUNT];
extern const char* RETIRE_SHORT[MF_COUNT + 1];   // results: "DNF Engine" (the last: an accident)

struct Realism {
    bool full = false;        // full authentic experience: everything below on, sensitivity at least 100 %
    bool opponents = true;    // the AI cars get it too (temperatures, failures, blow-outs)
    bool tyres = true;        // tyre temperature and wear (off: no wear, always warm)
    bool brakes = true;       // brake temperature: cold brakes bite less, hot ones fade
    bool cooling = true;      // engine temperature: following closely heats it, too hot damages it
    bool slipstream = true;   // less drag right behind another car
    bool blowTyre = true;     // a worn-out tyre can burst
    bool ground = true;       // kerbs and heavy landings knock the floor and suspension
    bool fuel = true;         // fuel is used (off: the tank never runs down)
    int damagePct = 100;      // contact damage: 0 .. 200 %
    bool mech[MF_COUNT] = {true, true, true, true, true, true, true, true, true};

    bool on(bool flag) const { return full || flag; }
    float damageK() const { return (full && damagePct < 100 ? 100 : damagePct) / 100.0f; }
    bool anyMech() const { for (bool m : mech) if (m) return true; return full; }
    bool mechOn(int f) const { return full || mech[f]; }
};

// per car: temperatures, slipstream and what has gone wrong (kept at the end of the Car record)
struct CarRel {
    float brakeT = 200;       // brake discs, deg C
    float engT = 90;          // engine water, deg C
    float tow = 0;            // 0..1 in another car's slipstream
    unsigned fails = 0;       // 1 << MF_* that have happened
    int flat = -1;            // punctured / burst tyre (0 FL, 1 FR, 2 RL, 3 RR), -1 none
    int gearMax = 0;          // gearbox: highest gear still working (0 = all)
    float cut = 0, nextCut = 0;   // throttle: cutting out now (s left) / until the next cut
    bool brakeWeak = false;   // brake problem: much less braking
    float steerT = 0;         // broken steering rod: seconds since it went
    float leakT = 0;          // oil / water leak: seconds until the engine lets go
    float powerK = 1;         // turbo failure: power left
    bool retired = false; int cause = -1; float retireT = 0; bool zoned = false;
    float parkD = 0;          // where it pulls over to (m from the centre line)
    float overheatMsg = 0;
};
