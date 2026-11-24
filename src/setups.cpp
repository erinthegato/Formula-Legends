#include "setups.h"
#include <cstdio>
#include <cstring>

SetupStore setups;

bool SetupStore::get(int track, int cls, CarSetup& out) const {
    if (track < 0 || track >= TRACK_COUNT || track >= MAX_TRACKS || cls < 0 || cls >= ERA_COUNT || !has[track][cls]) return false;
    out = su[track][cls];
    return true;
}

void SetupStore::put(int track, int cls, const CarSetup& s) {
    if (track < 0 || track >= TRACK_COUNT || track >= MAX_TRACKS || cls < 0 || cls >= ERA_COUNT) return;
    has[track][cls] = true; su[track][cls] = s;
    save();
}

void SetupStore::load() {
    FILE* f = fopen("setups.sav", "rb");
    if (!f) return;
    char id[32]; int cls, w, sp, g, bf, br, mp, ty, fu;
    if (fscanf(f, "EMSU1") == 0)
        while (fscanf(f, " %31s %d %d %d %d %d %d %d %d %d", id, &cls, &w, &sp, &g, &bf, &br, &mp, &ty, &fu) == 10) {
            int t = -1;
            for (int i = 0; i < TRACK_COUNT; i++) if (!strcmp(TRACKS[i].id, id)) t = i;
            if (t < 0 || t >= MAX_TRACKS || cls < 0 || cls >= ERA_COUNT) continue;
            auto in = [](int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); };
            CarSetup& s = su[t][cls];
            s.wing = in(w, 1, 5); s.susp = in(sp, 0, 2); s.gearing = in(g, 0, 2); s.brakeF = in(bf, 30, 100); s.brakeR = in(br, 20, 100);
            s.map = in(mp, 0, MAP_COUNT - 1); s.tyre = in(ty, TY_SOFT, TY_HARD); s.fuel = in(fu, 10, 100);
            has[t][cls] = true;
        }
    fclose(f);
}

void SetupStore::save() const {
    FILE* f = fopen("setups.sav", "wb");
    if (!f) return;
    fprintf(f, "EMSU1\n");
    for (int t = 0; t < TRACK_COUNT && t < MAX_TRACKS; t++)
        for (int c = 0; c < 2; c++)
            if (has[t][c]) {
                const CarSetup& s = su[t][c];
                fprintf(f, "%s %d %d %d %d %d %d %d %d %d\n", TRACKS[t].id, c, s.wing, s.susp, s.gearing, s.brakeF, s.brakeR, s.map, s.tyre, s.fuel);
            }
    fclose(f);
}
