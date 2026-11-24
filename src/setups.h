// Car setups remembered per circuit and era: saved to setups.sav when the
// player leaves a setup screen, put back when that circuit and class come round again.
#pragma once
#include "race.h"
#include "data.h"

struct SetupStore {
    static const int MAX_TRACKS = 32;            // >= TRACK_COUNT
    bool has[MAX_TRACKS][ERA_COUNT] = {};
    CarSetup su[MAX_TRACKS][ERA_COUNT];
    bool get(int track, int cls, CarSetup& out) const;    // false (out untouched) if none saved yet
    void put(int track, int cls, const CarSetup& s);      // remembers and saves
    void load();
    void save() const;
};
extern SetupStore setups;
