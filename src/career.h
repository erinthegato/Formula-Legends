// Career mode: you run your own endurance team. Your car (a fictional make, CARS[careerCarIndex(cls)])
// takes the place of one entry on the grid. Between races you manage the budget, sign sponsors, hire
// the two team-mates who share the car with you, buy upgrades and paint the livery. Each season is the
// 8-round championship; the career is saved to career.sav after every change.
#pragma once
#include "champ.h"

enum Upgrade { UP_ENGINE = 0, UP_AERO, UP_WEIGHT, UP_PIT, UP_COUNT };
static const int UP_MAX = 5;
static const int LIVERY_STYLES = 6;
static const int LIVERY_COLOURS_N = 16;

extern const char* TEAM_NAMES[];        // full team name
extern const char* TEAM_SHORT[];        // the make shown on the car lists
extern const int TEAM_NAME_COUNT;
extern const char* SPONSOR_NAMES[];
extern const int SPONSOR_NAME_COUNT;
extern const u32 LIVERY_COLOURS[LIVERY_COLOURS_N];
extern const char* LIVERY_COLOUR_NAMES[LIVERY_COLOURS_N];
extern const char* LIVERY_STYLE_NAMES[LIVERY_STYLES];
extern const char* UPGRADE_NAMES[UP_COUNT];

// money is kept in thousands of dollars ($k)
struct Sponsor { int name = -1; int perRace = 0; int bonus = 0; int goal = 0; bool title = false; };   // bonus if we finish <= goal in class
struct Hire { Driver d; int salary = 0; bool free = true; };                                        // salary per race

struct Career {
    int magic = 0x52414343;     // 'CCAR'
    int version = 1;
    bool active = false;
    int name = 0, cls = CLASS_GT3, number = 0;
    int money = 0, season = 1, reputation = 0;
    int style = 0, colA = 0, colB = 1, colC = 2;     // livery
    int up[UP_COUNT] = {};
    Hire crew[2];               // your two team-mates (you are the third driver)
    Hire market[6];             // drivers looking for a seat
    Sponsor sponsor[2];         // [0] title sponsor, [1] partner
    Sponsor offers[3];
    int skipCar = -1, skipNum = 0;     // the entry our car replaced
    int difficulty = 1, raceMin = 20;
    Champ champ;
    // the last race, for the report on the team screen
    int lastPos = 0, inPrize = 0, inSponsor = 0, inBonus = 0, outRunning = 0, outSalaries = 0, outRepairs = 0;
    bool lastValid = false;
    int seasonEndBonus = 0;

    void begin(int name, int cls, int difficulty, int raceMin);
    void apply() const;                      // our car's name, number, colour and upgrades into CARS
    RaceSettings roundSettings() const;
    void afterRace(const Race& r, const std::vector<int>& qualiOrder);
    void newSeason();
    void newOffers();
    void newMarket();
    int upgradeCost(int k) const;
    bool buyUpgrade(int k);
    bool hire(int marketIdx, int seat);
    bool sign(int offerIdx);
    int runningCost(int round) const;        // entry, crew, tyres and fuel for one round
    int salaries() const;
    int classPos() const;                    // our place in the class standings
    int carIndex() const { return careerCarIndex(cls); }
    bool broke() const { return money < -1000; }
    bool save(const char* path) const;
    bool load(const char* path);
};

// a driver's rating 60..99 from their pace, for the menus
int driverRating(const Driver& d);
