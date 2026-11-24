#include "career.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

const char* TEAM_NAMES[] = {"Code 4 Racing", "Gato Motorsport", "Midnight Stint Racing", "Apex Endurance", "Northlight Racing",
    "Blue Hour Motorsport", "Redline Collective", "Sunrise Racing Team", "Ironclad Endurance", "Velvet Comet Racing",
    "Harbour Lights Racing", "Erin Racing"};
const char* TEAM_SHORT[] = {"Code 4", "Gato", "Midnight", "Apex", "Northlight", "Blue Hour", "Redline", "Sunrise", "Ironclad",
    "Velvet Comet", "Harbour", "Erin"};
const int TEAM_NAME_COUNT = sizeof(TEAM_NAMES) / sizeof(TEAM_NAMES[0]);
const char* SPONSOR_NAMES[] = {"Volta Energy", "Kestrel Watches", "Polaris Tyres", "Bluefin Logistics", "Orbit Telecom",
    "Ember Coffee", "Granite Bank", "Lumen Optics", "Tidal Drinks", "Ridgeway Oil", "Nimbus Cloud", "Atlas Freight",
    "Juniper Gin", "Cobalt Tools", "Saffron Air", "Vector Software"};
const int SPONSOR_NAME_COUNT = sizeof(SPONSOR_NAMES) / sizeof(SPONSOR_NAMES[0]);
const u32 LIVERY_COLOURS[LIVERY_COLOURS_N] = {
    RGBA(240, 120, 20), RGBA(20, 20, 24), RGBA(245, 245, 248), RGBA(210, 20, 30), RGBA(20, 60, 190), RGBA(60, 170, 255),
    RGBA(20, 140, 70), RGBA(150, 230, 40), RGBA(255, 210, 20), RGBA(140, 40, 200), RGBA(255, 80, 160), RGBA(150, 155, 165),
    RGBA(10, 40, 90), RGBA(120, 20, 30), RGBA(30, 200, 190), RGBA(190, 150, 70)};
const char* LIVERY_COLOUR_NAMES[LIVERY_COLOURS_N] = {"Orange", "Black", "White", "Red", "Blue", "Sky blue", "Green", "Lime",
    "Yellow", "Purple", "Pink", "Silver", "Navy", "Maroon", "Teal", "Gold"};
const char* LIVERY_STYLE_NAMES[LIVERY_STYLES] = {"Stripes", "Two-tone", "Slashes", "Bands", "Split", "Arrow"};
const char* UPGRADE_NAMES[UP_COUNT] = {"Engine", "Aero", "Weight", "Pit crew"};

static uint32_t rs = 99;
static float rnd() { rs = rs * 1664525u + 1013904223u; return (rs >> 8) * (1.0f / 16777216.0f); }
static int irnd(int n) { return (int)(rnd() * n) % n; }

int driverRating(const Driver& d) { return (int)(60 + (d.pace - 0.986f) / 0.014f * 39 + 0.5f); }

static bool hyper(const Career& c) { return c.cls == CLASS_HYPER; }

static Hire makeDriver(const Career& c, float quality) {
    Hire h;
    h.d.first = (uint8_t)irnd(32); h.d.last = (uint8_t)irnd(40);
    float q = std::min(1.0f, std::max(0.0f, quality + (rnd() - 0.5f) * 0.6f));
    h.d.pace = 0.986f + q * 0.014f;
    h.d.stamina = 0.8f + rnd() * 0.45f;
    h.salary = (int)((8 + q * q * 70 + (h.d.stamina - 0.8f) * 25) * (hyper(c) ? 1.6f : 1.0f));
    h.free = false;
    return h;
}

void Career::newMarket() {
    // better drivers want to join a team with a reputation
    float base = std::min(0.75f, 0.25f + reputation * 0.02f);
    for (auto& m : market) m = makeDriver(*this, base + rnd() * 0.3f);
}

void Career::newOffers() {
    float rep = 1 + reputation * 0.05f, k = hyper(*this) ? 1.8f : 1.0f;
    for (int i = 0; i < 3; i++) {
        Sponsor& o = offers[i];
        o.name = irnd(SPONSOR_NAME_COUNT);
        o.title = i == 0;
        o.goal = 1 + irnd(7);                                  // finish this high in class for the bonus
        float hard = 1.0f - (o.goal - 1) / 7.0f;               // tighter targets pay more
        o.perRace = (int)((25 + rnd() * 45 + hard * 35) * k * rep * (o.title ? 1.8f : 1.0f));
        o.bonus = (int)((40 + hard * 140) * k * rep);
        for (auto& s : sponsor) if (s.name == o.name) o.name = (o.name + 3) % SPONSOR_NAME_COUNT;
    }
}

void Career::begin(int nm, int cl, int diff, int mins) {
    *this = Career();
    rs = 12345u + (uint32_t)nm * 977u + (uint32_t)cl * 31u + (uint32_t)rand();
    active = true; name = nm; cls = cl; difficulty = diff; raceMin = mins;
    money = hyper(*this) ? 4000 : 1500;
    season = 1; reputation = 0;
    style = 0; colA = 0; colB = 1; colC = 2;
    // a free race number
    for (int tries = 0; tries < 200; tries++) {
        int n = 2 + irnd(97); bool used = false;
        for (int i = 0; i < CAR_PICK; i++) if (CARS[i].numbers[0] == n || CARS[i].numbers[1] == n) used = true;
        if (!used) { number = n; break; }
    }
    // the entry we take over: one of the first cars of our class on a normal grid
    int seen = 0, pick = irnd(4);
    for (int i = 0; i < CAR_PICK; i++)
        if ((int)CARS[i].cls == cls) { if (seen == pick) { skipCar = i; skipNum = CARS[i].numbers[0]; } seen++; }
    // two rookie team-mates on cheap deals, a first look at the market and the sponsors
    crew[0] = makeDriver(*this, 0.25f); crew[1] = makeDriver(*this, 0.2f);
    newMarket(); newOffers();
    sponsor[0] = offers[0]; sponsor[0].perRace = sponsor[0].perRace * 3 / 4; sponsor[0].title = true;   // a starter deal
    newOffers();
    RaceSettings base;
    base.car = carIndex(); base.difficulty = difficulty; base.raceMin = raceMin; base.opponents = 11;
    base.manualPits = true; base.weekend = true;
    champ.begin(base);
    lastValid = false;
}

void Career::apply() const {
    static CarDef stock[2]; static bool have = false;
    if (!have) { stock[0] = CARS[careerCarIndex(CLASS_HYPER)]; stock[1] = CARS[careerCarIndex(CLASS_GT3)]; have = true; }
    if (!active) return;
    int idx = carIndex();
    CarDef& d = CARS[idx];
    d = stock[cls == CLASS_HYPER ? 0 : 1];
    int n = name % TEAM_NAME_COUNT;
    d.make = TEAM_SHORT[n]; d.team = TEAM_NAMES[n];
    d.model = cls == CLASS_HYPER ? "Hypercar" : "GT3";
    d.numbers[0] = number; d.numbers[1] = 0;
    d.colour = LIVERY_COLOURS[colA % LIVERY_COLOURS_N];
    d.powerKW *= 1 + 0.015f * up[UP_ENGINE];
    d.grip *= 1 + 0.008f * up[UP_AERO];
    d.cornerMul = 1 + 0.006f * up[UP_AERO];
    d.massKg -= 7.0f * up[UP_WEIGHT];
}

RaceSettings Career::roundSettings() const {
    RaceSettings s = champ.roundSettings();
    s.career = true;
    s.skipCar = skipCar; s.skipNum = skipNum;
    s.crew[1] = crew[0].d; s.crew[2] = crew[1].d;
    s.pitCrewLevel = up[UP_PIT];
    // every round is a full race weekend, like a quick race: practice, qualifying (sets the grid), race
    // (older careers were saved with race-only rounds)
    s.weekend = true;
    s.sessionMin = raceMin >= 30 ? 10 : 5;
    return s;
}

int Career::runningCost(int round) const {
    float mult = CHAMP_ROUNDS[std::min(round, CHAMP_ROUND_COUNT - 1)].mult;
    return (int)((hyper(*this) ? 300 : 120) * mult);
}

int Career::salaries() const { return crew[0].salary + crew[1].salary; }

int Career::upgradeCost(int k) const {
    static const int COST[UP_MAX] = {120, 200, 320, 480, 700};
    if (up[k] >= UP_MAX) return 0;
    return (int)(COST[up[k]] * (hyper(*this) ? 2.2f : 1.0f));
}

bool Career::buyUpgrade(int k) {
    int c = upgradeCost(k);
    if (!c || money < c) return false;
    money -= c; up[k]++;
    return true;
}

bool Career::hire(int m, int seat) {
    if (m < 0 || m > 5 || seat < 0 || seat > 1 || market[m].free) return false;
    // signing-on fee: one race of salary
    if (money < market[m].salary) return false;
    money -= market[m].salary;
    Hire old = crew[seat];
    crew[seat] = market[m];
    market[m] = old;                      // the released driver goes back on the market
    return true;
}

bool Career::sign(int o) {
    if (o < 0 || o > 2 || offers[o].name < 0) return false;
    int slot = offers[o].title ? 0 : 1;
    sponsor[slot] = offers[o];
    offers[o].name = -1;
    return true;
}

int Career::classPos() const {
    int idx[48]; int n = champ.standings((CarClass)cls, idx, 48);
    for (int k = 0; k < n; k++) if (champ.e[idx[k]].number == number) return k + 1;
    return 0;
}

void Career::afterRace(const Race& r, const std::vector<int>& qualiOrder) {
    int rd = champ.round;
    champ.award(r, qualiOrder);
    // where we finished in class
    const Car& me = r.player();
    int pos = 1;
    for (auto& c : r.cars) if (c.def->cls == me.def->cls && c.position < me.position) pos++;
    float mult = CHAMP_ROUNDS[std::min(rd, CHAMP_ROUND_COUNT - 1)].mult;
    static const int PRIZE[10] = {250, 180, 140, 110, 90, 70, 55, 40, 30, 20};
    lastPos = pos;
    inPrize = pos <= 10 ? (int)(PRIZE[pos - 1] * mult * (hyper(*this) ? 2.4f : 1.0f)) : 0;
    inSponsor = 0; inBonus = 0;
    for (auto& s : sponsor) if (s.name >= 0) { inSponsor += s.perRace; if (pos <= s.goal) inBonus += s.bonus; }
    outRunning = runningCost(rd);
    outSalaries = salaries();
    outRepairs = (int)(me.damage() * (hyper(*this) ? 400 : 150));
    money += inPrize + inSponsor + inBonus - outRunning - outSalaries - outRepairs;
    reputation += pos <= 3 ? 2 : (pos <= 6 ? 1 : 0);
    lastValid = true;
    seasonEndBonus = 0;
    newOffers();
    if (rd % 2 == 1) newMarket();
    if (champ.over()) {
        // prize money for the final standings, then a new season
        int cp = classPos();
        static const int SEASON[6] = {1200, 800, 550, 350, 200, 100};
        seasonEndBonus = cp >= 1 && cp <= 6 ? (int)(SEASON[cp - 1] * (hyper(*this) ? 2.0f : 1.0f)) : 0;
        money += seasonEndBonus;
        reputation += cp == 1 ? 6 : (cp <= 3 ? 3 : 1);
    }
}

void Career::newSeason() {
    season++;
    RaceSettings base = champ.base;
    champ.begin(base);
    for (auto& s : sponsor) s = Sponsor();          // contracts run for one season
    newOffers(); newMarket();
    lastValid = false;
}

bool Career::save(const char* path) const {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(this, sizeof *this, 1, f) == 1;
    fclose(f);
    return ok;
}

bool Career::load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    Career c;
    bool ok = fread(&c, sizeof c, 1, f) == 1 && c.magic == 0x52414343 && c.version == 1 && c.active;
    fclose(f);
    if (!ok) return false;
    *this = c;
    return true;
}
