#include "champ.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

// Formula Legends: a 10-round season on the era's own calendar (ERA_CAL in data.cpp)
const ChampRound CHAMP_ROUNDS[] = {
    {"", 1.5f, 1}, {"", 1.5f, 1}, {"", 1.5f, 1}, {"", 1.5f, 1}, {"", 1.5f, 1},
    {"", 1.5f, 1}, {"", 1.5f, 1}, {"", 1.5f, 1}, {"", 1.5f, 1}, {"", 1.5f, 1},
};
const int CHAMP_ROUND_COUNT = sizeof(CHAMP_ROUNDS) / sizeof(CHAMP_ROUNDS[0]);

// points: in 1980 the top six scored 9-6-4-3-2-1; from 2010 the top ten 25-18-15-12-10-8-6-4-2-1
static const float PTS[2][10] = {
    {9, 6, 4, 3, 2, 1, 0, 0, 0, 0},
    {25, 18, 15, 12, 10, 8, 6, 4, 2, 1},
};

int Champ::era() const { return base.car >= 0 && base.car < CAR_COUNT ? CARS[base.car].era : 0; }
int Champ::trackIndex(int rd) const { return eraTrack(era(), rd); }

void Champ::begin(const RaceSettings& s) {
    active = true; round = 0; base = s; count = 0;
    memset(lastPts, 0, sizeof lastPts);
}

RaceSettings Champ::roundSettings() const {
    RaceSettings s = base;
    int rd = round < CHAMP_ROUND_COUNT ? round : CHAMP_ROUND_COUNT - 1;
    s.track = trackIndex(rd);
    s.eventHours = 0;
    s.startType = START_STANDING;
    s.raceMin = 0;                       // Formula 1: races over a number of laps
    if (s.laps < 1) s.laps = 5;
    return s;
}

ChampEntry* Champ::find(int number) {
    for (int i = 0; i < count; i++) if (e[i].number == number) return &e[i];
    return nullptr;
}

void Champ::award(const Race& r, const std::vector<int>& qualiOrder) {
    (void)qualiOrder;                    // (no points for pole position in Formula 1)
    const float* pts = PTS[era() == ERA_90S ? 0 : 1];
    memset(lastPts, 0, sizeof lastPts);
    std::vector<const Car*> v;
    for (auto& c : r.cars) v.push_back(&c);
    std::sort(v.begin(), v.end(), [](const Car* a, const Car* b) { return a->position < b->position; });
    if (!v.empty()) {
        int winnerLaps = v[0]->lap;
        for (int k = 0; k < (int)v.size(); k++) {
            const Car& c = *v[k];
            ChampEntry* en = find(c.number);
            if (!en && count < 48) { en = &e[count++]; en->number = c.number; en->car = (int)(c.def - CARS); }
            if (!en) continue;
            float p = 0;
            if (c.lap >= 0.9f * winnerLaps && k < 10) p = pts[k];        // classified: 90 % of the winner's laps
            if (k == 0) en->wins++;
            en->best = std::min(en->best, k + 1);
            en->pts += p;
            lastPts[en - e] += (int)(p + 0.5f);
        }
        // pole position (counted, not scored)
        if (!qualiOrder.empty()) { ChampEntry* en = find(qualiOrder[0]); if (en) en->poles++; }
    }
    round++;
}

int Champ::standings(CarClass cls, int* out, int max) const {
    int n = 0;
    for (int i = 0; i < count && n < max; i++) if (CARS[e[i].car].cls == cls || true) out[n++] = i;   // (one class)
    std::sort(out, out + n, [this](int a, int b) {
        if (e[a].pts != e[b].pts) return e[a].pts > e[b].pts;
        if (e[a].wins != e[b].wins) return e[a].wins > e[b].wins;
        return e[a].best < e[b].best;
    });
    return n;
}

struct ChampFile { char magic[4]; int version; };

bool Champ::save(const char* path) const {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    ChampFile h = {{'F', 'L', 'C', '1'}, 1};
    fwrite(&h, sizeof h, 1, f);
    fwrite(this, sizeof(Champ), 1, f);
    fclose(f);
    return true;
}

bool Champ::load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    ChampFile h; Champ c;
    bool ok = fread(&h, sizeof h, 1, f) == 1 && !memcmp(h.magic, "FLC1", 4) && h.version == 1 && fread((void*)&c, sizeof(Champ), 1, f) == 1;
    fclose(f);
    if (!ok || c.count < 0 || c.count > 48) return false;
    *this = c;
    return true;
}
