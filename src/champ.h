// Championship mode: a 10-round season of one era on its real calendar, that era's points system,
// saved after every round.
#pragma once
#include "race.h"

struct ChampRound { const char* track; float hours; float mult; };   // mult: 1, 1.5 or 2 (Le Mans)
extern const ChampRound CHAMP_ROUNDS[];
extern const int CHAMP_ROUND_COUNT;

struct ChampEntry { int number = 0, car = 0; float pts = 0; int wins = 0, poles = 0, best = 99; };

struct Champ {
    bool active = false;
    int round = 0;                // next round to run (== CHAMP_ROUND_COUNT when the season is over)
    RaceSettings base;            // the player's car, difficulty, race length, opponents...
    ChampEntry e[48]; int count = 0;
    int lastPts[48] = {};         // points scored by each entry in the last round (results screen)

    void begin(const RaceSettings& s);
    RaceSettings roundSettings() const;            // settings for the next round
    int trackIndex(int round) const;
    int era() const;                              // the era of the player's car
    // after the race: points for every class finisher, +1 for each class pole
    void award(const Race& r, const std::vector<int>& qualiOrder);
    // entries of one class sorted by points
    int standings(CarClass cls, int* out, int max) const;
    ChampEntry* find(int number);
    bool save(const char* path) const;
    bool load(const char* path);
    bool over() const { return round >= CHAMP_ROUND_COUNT; }
};
