// Car setups remembered per circuit and class (Hypercar / GT3): saved to setups.sav when the
// player leaves a setup screen, put back when that circuit and class come round again.
#pragma once
#include "race.h"
#include "data.h"

struct SetupStore {
    static const int MAX_TRACKS = 16;            // >= TRACK_COUNT
    bool has[MAX_TRACKS][2] = {};
    CarSetup su[MAX_TRACKS][2];
    bool get(int track, int cls, CarSetup& out) const;    // false (out untouched) if none saved yet
    void put(int track, int cls, const CarSetup& s);      // remembers and saves
    void load();
    void save() const;
};
extern SetupStore setups;
