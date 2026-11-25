// The race: cars move in track coordinates (distance along the lap s, metres left of the centre
// line d, heading relative to the track yaw). Hills come from the track (gravity on slopes).
// Race control (starts, safety car, penalties), pit stops, tyres, fuel, damage and weather live
// in racecontrol.cpp.
#pragma once
#include <vector>
#include "track.h"
#include "realism.h"

struct ClassPhys {
    float topKmh;        // reference top speed (the AI's flat-out limit)
    float muBase;        // tyre grip (g) at low speed
    float downforceV;    // speed (m/s) at which aero grip equals mechanical grip
    float brakeG;        // braking at low speed (g)
    float cdA;           // drag area
    float aeroCap;       // most the downforce can multiply the grip by
    float fuelKg;        // a full tank
    float ersKW;         // hybrid boost (0: no hybrid)
    bool drs;            // the rear-wing flap opens on the straights (2011 on)
    bool refuel;         // pit stops refuel (1994-2009)
    bool vsc;            // virtual safety car (2015 on)
};

enum Tyre { TY_SOFT = 0, TY_MEDIUM, TY_HARD, TY_INTER, TY_WET, TY_COUNT };
extern const char* TYRE_NAME[TY_COUNT];

enum DmgPart { DM_NOSE = 0, DM_WING, DM_LEFT, DM_RIGHT, DM_SUSP, DM_ENGINE, DM_COUNT };

// engineer radio clips (DATA/sfx/radio_*.raw) and other one-shot sounds (DATA/sfx/*.raw)
enum Radio { RADIO_BESTLAP = 0, RADIO_BOX, RADIO_BOX_ACK, RADIO_CONTACT, RADIO_DAMAGE, RADIO_DRIVETHROUGH,
             RADIO_FINISH, RADIO_FORMATION, RADIO_FUEL, RADIO_GIVEBACK, RADIO_GO, RADIO_GREEN, RADIO_LASTLAP,
             RADIO_LEMANS, RADIO_LIMITS, RADIO_OVERTAKE, RADIO_PENALTY, RADIO_SC, RADIO_SC_IN, RADIO_TYRES,
             RADIO_ENGINE, RADIO_GEARBOX, RADIO_TURBO, RADIO_SUSPENSION, RADIO_PUNCTURE, RADIO_WHEEL, RADIO_LEAK,
             RADIO_THROTTLE, RADIO_BRAKES, RADIO_STEERING, RADIO_OVERHEAT, RADIO_COUNT };
enum Sfx { SFX_AIRJACK = 0, SFX_JACKDROP, SFX_REFUEL, SFX_WHEELGUN, SFX_CHAMPAGNE, SFX_CROWD,
           SFX_THUNDER_CLOSE, SFX_THUNDER_MID, SFX_THUNDER_FAR1, SFX_THUNDER_FAR2, SFX_COUNT };
extern const char* RADIO_FILE[RADIO_COUNT];
extern const char* SFX_FILE[SFX_COUNT];

enum PitState { PIT_NONE = 0, PIT_IN, PIT_STOP, PIT_OUT };

// The player's car setup (garage / diagnostics computer). Wing, suspension and gearing are locked
// from qualifying on (parc ferme); brakes, engine map, tyres and fuel are not.
enum EngineMap { MAP_SC = 0, MAP_NORMAL, MAP_ATTACK, MAP_COUNT };
extern const char* MAP_NAME[MAP_COUNT];
struct CarSetup {
    int wing = 3;            // 1 (low drag) .. 5 (high downforce)
    int susp = 1;            // 0 soft, 1 medium, 2 stiff
    int gearing = 1;         // 0 short, 1 medium, 2 long
    int brakeF = 75, brakeR = 40;   // brake pressure front / rear, percent
    int map = MAP_NORMAL;
    int tyre = 1;            // TY_MEDIUM
    int fuel = 100;          // percent of the tank at the start
    float bias() const { return brakeF / (float)(brakeF + brakeR); }
};

struct PitPlan { int tyre = TY_MEDIUM; bool tyres = true; float fuel = 1; bool repair = true; bool swapDriver = false; bool aiDrives = false;
                 int nextDriver = 0; };   // who drives the next stint (the player's car: 0 = you, 1-2 = team-mates, driven by the AI)

// One of the three drivers who share a car. Names are made up from tables (Race::driverName), seeded by the car number.
// fatigue 0 (fresh) .. 1 (exhausted): rises while driving (faster at night and in the wet), falls while resting.
struct Driver { uint8_t first = 0, last = 0, pad0 = 0, pad1 = 0; float pace = 1, stamina = 1, fatigue = 0, hours = 0; };
static const int DRIVERS_PER_CAR = 3;
static const float MAX_STINT_H = 4.0f;      // longest stint one driver may do (hours of race time)
static const float MAX_DRIVE_H = 14.0f;     // most one driver may drive in the whole race

// the body on its springs (save version 7): height above the road, vertical speed, pitch and roll
// relative to the road, how much of its weight + downforce the tyres carry, time in the air
struct alignas(alignof(void*)) CarBody {
    float hOff = 0, vz = 0;            // m above the road's surface (0 = normal ride height), m/s up (world)
    float pitch = 0, pitchV = 0;       // rad nose up, relative to the road
    float roll = 0, rollV = 0;         // rad left side up, relative to the road
    float load = 1;                    // tyre load / (weight + downforce): 1 normal, 0 in the air
    float air = 0;                     // seconds with the wheels off the ground
    float zPrev = 0, psPrev = 0, rsPrev = 0;   // the road under the car last step: height, pitch, roll
    bool init = false;
    V3 gPos, gUp;                      // (pose) the road under the car: a point and its normal (wheels stay on it)
};

struct Car {
    const CarDef* def = nullptr;
    int number = 0;
    bool player = false;
    // state
    float s = 0, d = 0, yaw = 0, v = 0;
    float steer = 0, throttle = 0, brake = 0;
    float yawRate = 0;
    int gear = 1; float rpm = 0;
    float wheelSpin = 0;          // wheel rotation (radians) for drawing
    float lift = 0;               // raised on the pit jacks (m)
    float drift = 0;              // human-driven cars: angle between where the nose points and where the car goes
    bool human = false;           // driven by the pad this frame
    bool spinning = false;        // the rear has gone and the car is spinning (human-driven only)
    CarSetup su;                  // setup (AI cars keep the default)
    bool outLap = false;          // this lap does not count (left the garage)
    bool engineOn = true;
    // race
    int lap = 0;                  // laps completed
    float lapStart = 0, lastLap = 0, bestLap = 0;
    float total = 0;              // distance covered (for positions)
    bool finished = false; float finishTime = 0;
    int position = 0, lastPos = 0;
    float startDelay = 0;         // Le Mans start: driver running to the car
    // tyres, fuel, drivers
    int tyre = TY_MEDIUM; float wear = 0;
    void coldTyres() { for (float& t : tyreT) t = 45; rel.flat = -1; }   // a fresh set straight from the rack
    float tyreTemp() const { return (tyreT[0] + tyreT[1] + tyreT[2] + tyreT[3]) * 0.25f; }
    float fuel = 1;               // 0..1 of the tank
    int driver = 0; float stint = 0;   // who is driving, and for how long (race seconds) since the last change
    Driver drv[DRIVERS_PER_CAR];
    float mistake = 0, mistakeSide = 0;  // a tired AI driver's slip: seconds left running wide / off line
    int stintWarn = 0, tiredWarn = 0;
    int tyreLaps = 0;                    // laps on this set of tyres
    // blue flags: a faster car (the other class, or one lapping us) is close behind
    float blue = 0;                      // > 0 while the blue flag is out for this car
    int blueBy = -1; float blueGap = 0;  // which car, how far behind (m)
    float yieldSide = 0;                 // AI: which side it moves over to (+ left), 0 = keeps its line
    float heldUp = 0, blueMsgT = 0; int blueWarn = 0;   // the player: holding a faster car up
    int lastMark = -1; float markTime = 0;   // timing: last 200 m timing point passed, and when
    // damage 0..1 per part
    float dmg[DM_COUNT] = {0, 0, 0, 0, 0, 0};
    bool wingOff = false, noseOff = false;
    // pits
    int pit = PIT_NONE; bool wantPit = false; float pitTimer = 0, pitTotal = 0; int stops = 0;
    PitPlan plan;
    int pitPhase = 0;             // -2 waiting for the team screen, -1 drive-through, 0 refuel, 1 jacks + wheels, 2 repair, 3 drop
    // penalties
    int limitWarn = 0; float limitClean = 0, limitCool = 0;
    float slowOver = 0; int slowWarn = 0;   // the player: time over 80 km/h in a slow zone / FCY
    bool slowLim = false;                   // the 80 km/h limiter holds (engages once you are down to 80 in a zone)
    // hybrid (Hypercars): the battery charges under braking and gives extra power out of the corners
    float ers = 1; bool boosting = false;
    int ersState = 0;             // 0 idle, 1 deploying, 2 harvesting (for the HUD)
    float ersBurst = 0;           // seconds left of a button (DRS) deployment
    float yawCmd = 0;             // the player's steering after the car's turn-in lag (heavier eras turn in slower)
    int contactWarn = 0;
    float penalty = 0;            // seconds added to the result
    bool driveThrough = false; int dtLaps = 0;
    float giveBack = 0; int giveBackPos = 0;
    // AI
    float skill = 1, lineBias = 0, avoid = 0;
    float slip = 0;               // 0..1 how much the tyres are sliding (sound/smoke)
    bool offTrack = false;
    float stuck = 0;              // seconds stopped on track (safety car trigger)
    // world pose (worked out each frame for drawing / sound / camera)
    V3 pos, fwd, left, up;
    // (added in save version 6: kept last so version 5 saves still load)
    float tyreT[4] = {75, 75, 75, 75};   // tyre temperatures, deg C: front left, front right, rear left, rear right
    // DRS: allowed in the zone it was detected for (within a second of the car ahead), open while
    // in that zone and not braking; drsFlap animates the flap 0 (shut) .. 1 (open)
    int drsZone = -1; bool drs = false; float drsFlap = 0;
    bool drsReq = false;          // the player pressed DRS in this zone (closes on the brakes)
    bool hold = false;            // the intro: this car stands still where it was put (not driven)
    CarBody body;                 // (save version 7) suspension and the car in the air
    CarRel rel;                   // (save version 8) temperatures, slipstream, mechanical problems, retirement

    float damage() const { float m = 0; for (float x : dmg) m = m > x ? m : x; return m; }
};

enum StartType { START_EVENT = 0, START_ROLLING, START_STANDING, START_LEMANS };
enum WeatherMode { WX_SIMULATED = 0, WX_DRY, WX_WET, WX_STORM };

struct RaceSettings {
    int track = 0;
    int car = 0;
    int laps = 3;
    int raceMin = 0;        // > 0: timed race of this many real minutes (overrides laps)
    int opponents = 11;
    int difficulty = 1;     // 0 easy 1 normal 2 hard
    int startPos = 1;       // 0 pole, 1 middle, 2 back
    bool manualGears = false;
    bool weekend = false;   // practice + qualifying before the race
    int sessionMin = 5;     // practice / qualifying length (minutes)
    int startType = START_EVENT;
    int weather = WX_SIMULATED;
    float eventHours = 0;   // real race hours this event stands for (0 = the event's own)
    CarSetup setup;         // the player's car
    bool manualPits = true; // the player's pit stops: timing prompts (QTE) instead of the crew working alone
    // career mode: our team's car takes the place of one entry, with our hired drivers and pit crew
    bool career = false;
    int skipCar = -1, skipNum = 0;      // the entry our car replaces
    Driver crew[3];                     // [1], [2]: our hired team-mates
    int pitCrewLevel = 0;               // 0..5: faster stops
    bool timeTrial = false;             // time trial: the player alone, dry, against a ghost of the best lap
    float rubber = 0.1f;                // how rubbered-in the track is at the start (carried through a weekend)
};

// time trial ghost: where the car was every GHOST_DT seconds of its lap
struct GhostPt { float s, d, yaw; };
static const float GHOST_DT = 0.1f;

enum Session { SES_PRACTICE = 0, SES_QUALI, SES_RACE };

struct RaceMsg { char text[64]; float time; u32 color; };
struct Debris { V3 p, v; float yaw, pitch, spin, life, ground; int carIdx, part; };
struct Particle { V3 p, v; float life, size; u32 color; int kind; };   // 0 smoke / dust, 1 spray, 2 shard, 3 spark, 4 flame

// Manual pit stop: one prompt per job (fuel, jacks, each wheel, release). A marker sweeps a bar;
// press the shown button once while it is in the green zone. No button mashing: only the first press
// of a prompt counts, a wrong button or a press outside the zone is a fumble.
enum QteStep { QS_FUEL = 0, QS_JACK, QS_FL, QS_FR, QS_RL, QS_RR, QS_REPAIR, QS_DROP };
enum QteButton { QB_CROSS = 0, QB_CIRCLE, QB_SQUARE, QB_TRIANGLE };
struct PitQte {
    bool on = false;
    int seq[8]; int n = 0, idx = 0;
    bool waiting = false;       // a prompt is on screen
    int button = 0;
    float pos = 0, speed = 1;   // marker 0..1
    float z0 = 0.5f, z1 = 0.68f, p0 = 0.56f, p1 = 0.62f;   // green zone, perfect band
    float work = 0, workTotal = 0, fuelFrom = 0;
    int result = -1, lastShown = -1; float flash = 0;   // last result: 0 fumble, 1 good, 2 perfect
    int perfect = 0, good = 0, miss = 0;
};

struct SafetyCar {
    int state = 0;                // 0 none, 1 out, 2 coming in this lap
    float s = 0, total = 0, v = 0, d = 0;
    int lapsLeft = 0; float timer = 0;
    bool rain = false;
    V3 pos, fwd, left, up;
};

struct Race {
    friend struct Game;           // (the intro stages its shots with the race's own tools)
    Track track;
    RaceSettings set;
    std::vector<Car> cars;
    int playerIdx = 0;
    float time = 0;               // seconds since green (real seconds)
    float countdown = 4;          // > 0 before a standing start
    bool formation = false;       // rolling start: field behind the pace car until the line
    bool over = false;
    float greenTime = 0;          // race time when the green flag fell (rolling / Le Mans starts)
    // per-class racing line + speed plan along the lap (one value per track point)
    std::vector<float> line;
    std::vector<float> vmax[2];

    Session session = SES_RACE;
    float sessionLength = 0;      // seconds (practice / qualifying)
    bool chequered = false;       // time is up: cars finish when they next cross the line
    // time: the race clock runs `timeScale` times faster than real time (a 6 h race in 30 min = 12x)
    float timeScale = 1;
    float consumeScale = 1;       // fuel / tyre use speed-up: the clock's, but a tank always lasts 5+ laps
    float startHour = 12;
    float timeAccel = 1;
    bool autoPlayer = false;      // the AI drives the player's car (tests, highlights)          // 1..16x speed-up while the AI drives (manage mode)
    // weather
    float rain = 0, wet = 0, rainTarget = 0, rainTimer = 0;
    // track rubber 0 (green) .. 1 (fully rubbered in): the cars lay rubber on the racing line, which
    // grips more; off the line the marbles (bits of rubber) grip less. Rain washes it away.
    float rubber = 0.1f; int rubberMsg = 0;
    float rubberGrip(const Car& c) const;
    bool storm = false; float lightning = 0, nextStrike = 20;
    int thunderPending = -1; float thunderDelay = 0;
    // race control
    SafetyCar sc;
    // WEC-style neutralisations before a safety car: a full-course yellow (FCY: the whole track at
    // 80 km/h, no overtaking) and slow zones (80 km/h through one part of the lap, round an incident)
    float slowV = 120 / 3.6f;                 // the VSC speed limit (F1: a simplified "delta": 120 km/h)
    float fcy = 0, fcyWarn = 0;               // seconds of FCY left / countdown to it starting
    struct SlowZone { float from, to, left; };
    std::vector<SlowZone> zones;
    bool slowAt(float s) const;               // 80 km/h here (FCY or a slow zone)
    bool zoneAt(float s) const;               // a slow zone here (not the FCY)
    void callFcy(float seconds);
    void testSafetyCar();
    void sparks(const V3& at, const V3& vel, int n, float spread);   // a burst of sparks (near the camera only)
    void addSlowZone(float s, float seconds);
    PitQte qte;                   // the player's manual pit stop
    float scCooldown = 0;
    std::vector<RaceMsg> msgs;
    int radioPending = -1;        // engineer radio clip for the player (RADIO_* in racecontrol.cpp)
    std::vector<int> sfxEvents;   // pit / thunder sounds to play this frame
    std::vector<Debris> debris;
    std::vector<Particle> parts;
    std::vector<float> fireT;     // seconds each car (by its index in cars) keeps burning after a big crash (not saved)
    void crashFire(Car& c, float amount);
    // car realism (settings menu, copied from cfg at the start) and mechanical problems (reliability.cpp)
    Realism real;
    bool realFor(const Car& c) const { return c.player || real.opponents || real.full; }
    void reliability(Car& c, float dt);
    void failure(Car& c, int kind);
    void retire(Car& c, int cause);
    void retiredDrive(Car& c, float dt);
    void slipstream(float dt);
    // pit lane (distances along the lap, wrapping)
    float pitIn = 0, pitOut = 0, pitWallFrom = 0, pitWallTo = 0;
    float laneD = 0, boxD = 0;

    // gridOrder: car numbers in starting order (from qualifying); empty = the usual random grid
    bool start(const RaceSettings& s, Session ses = SES_RACE, const std::vector<int>* gridOrder = nullptr);
    std::vector<int> qualifyingOrder() const;   // car numbers, hypercars first, each class by best lap
    void endSession();                          // stop a practice / qualifying session now
    void end();
    // controls for the player (steer -1..1 right positive, throttle/brake 0..1, gear +-1)
    void update(float dt, float steer, float throttle, float brake, int shift, bool aiPlayer);
    Car& player() { return cars[playerIdx]; }
    const Car& player() const { return cars[playerIdx]; }
    void pose(Car& c) const;
    void surfaceFrame(const Car& c, V3& pos, V3& fwd, V3& left) const;   // the road (or verge) under the car
    void bodyUpdate(Car& c, float dt, float gEff, float accLong, float accLat);
    float wallHit(Car& c, float sg);
    static const ClassPhys& phys(int era);
    int era = 0;                                // the generation of car racing (all cars the same)
    const ClassPhys& ep() const { return phys(era); }
    // DRS zones (worked out from the circuit's long straights)
    struct DrsZone { float detect, from, to; };
    std::vector<DrsZone> drsZones;
    bool drsPress = false;        // the DRS button went down this frame (set by the game)
    // the standing-start launch (launch.cpp): the player keeps the revs in the green band while the
    // lights come on, then drops the clutch (DRS or gear-up button) once when they go out
    enum LaunchState { LS_OFF = 0, LS_REV, LS_WAIT, LS_RUN };
    struct Launch {
        int state = LS_OFF;
        float rev = 0.05f;        // 0 idle .. 1 limiter (the bar)
        float lo = 0, hi = 0;     // the green band
        float hold = 0;           // the lights stay on this much longer (random) before going out
        float inBand = 0, sampled = 0;   // time in the band over the last part of the countdown
        float revAtDrop = 0, reaction = 0, quality = 0;
        bool jumped = false, dropped = false;
        int effect = 0; float effectT = 0;   // 1 boost, 2 wheelspin, 3 bog-down
        u32 seed = 1;
    } launch;
    void launchArm();
    float launchCountdown(float dt, float throttle, bool clutch);   // returns how much the countdown runs
    void launchBefore(Car& c, float dt, bool clutch);   // player control while the clutch is in / just after
    void launchAfter(Car& c, float dt);                 // the launch's effect, after the physics step
    std::vector<unsigned char> ersSec;   // per track point: 0 corner, 1 corner exit, 2 straight
    void setupErs();
    int ersSecAt(float s) const { return ersSec.empty() ? 0 : ersSec[track.index(s)]; }
    bool drsOn = false;                         // DRS enabled now (race: from lap 3, dry, no safety car)
    void setupDrs();
    void updateDrs(float dt);
    int drsZoneAt(float s) const;
    // race distance: fuel tank and tyre life are sized to it (F1: lap races)
    float tankKm = 300, tyreKm[TY_COUNT] = {100, 160, 230, 150, 150};

    // race control / strategy (racecontrol.cpp)
    float clockHour() const;                    // time of day at the track
    float nightLevel() const;                   // 0 day .. 1 night (F1: floodlit, at most 0.32)
    float sunLevel() const;                     // the sun alone: 0 day .. 1 night
    float raceLeft() const;                     // seconds of a timed race left (or -1)
    float pitRel(float s) const;                // metres from pit entry along the lap (wrapping)
    bool inPitLane(const Car& c) const;
    float boxS(const Car& c) const;
    int bestTyreFor(float wetness) const;
    void message(const char* text, u32 color = 0xFFFFFFFF, int radio = -1);
    bool spinsOn = true;          // the player car can step out and spin (Controls setting)
    bool playerAtBox() const;
    void releasePlayer();                       // start the player's stop with the chosen plan
    // practice / qualifying garage. The car sits in its garage (pitPhase -2 waiting, -3 going out:
    // off the jacks, engine on, pushed to the box by the crew, then the pit-lane AI drives it out;
    // -4 driving in from the box)
    void toGarage();                            // put the player's car straight back in its garage
    void leaveGarage(const CarSetup& su);       // ... and send it out (pushed out, then the pit lane)
    void flyingLap(const CarSetup& su);         // ... or straight onto a flying lap
    void garageCar(Car& c, int phase, float delay);
    float garageD() const { return boxD + 9.0f; }
    bool inGarage() const { return session != SES_RACE && player().pit == PIT_STOP && player().pitPhase == -2; }
    void cycleMap();                            // the player's engine map button
    // drivers (racecontrol.cpp)
    static void driverName(const Car& c, int k, char* out, int n, bool full = false);   // "You", "A. Rossi" / "Anna Rossi"
    static void crewDrivers(Car& c);                 // names, pace and stamina of a car's three drivers
    int freshestDriver(const Car& c, int except) const;
    // live timing (pit wall): gaps measured at timing points every 200 m round the lap
    static constexpr float MARK_M = 200.0f;
    std::vector<float> markFirst;              // when the first car passed each timing point (race time)
    float gap(const Car& c) const;             // seconds behind the leader (-1 unknown); see lapsDown
    int lapsDown(const Car& c) const;          // whole laps behind the leader
    float lapFuel(const Car& c) const;         // share of the tank one lap uses
    bool faster(const Car& a, const Car& b) const;   // a gets a blue flag shown to b (other class, or lapping it)
    float nextBend(float s, float range) const;      // sign / size of the next real corner within range (+ left)
    float lapGap(float sFrom, float sTo) const { float g = track.wrap(sTo - sFrom); return g > track.length * 0.5f ? g - track.length : g; }
    void qtePress(int button);                  // a face button during a manual stop
    bool manualStop(const Car& c) const;
    bool save(const char* path) const;
    bool load(const char* path);
    // time trial (set.timeTrial): no other cars, always dry, a lap that leaves the track or spins
    // doesn't count, fuel / tyres / damage are made good at the line, a ghost drives the record lap
    std::vector<GhostPt> ghost, ghostRec;    // the record lap, the lap being driven
    float ghostLap = 0;                      // the record's lap time (0 = none yet)
    bool lapInvalid = false;                 // this lap went off the track: no time
    float gripScale = 1.11f;                 // all cars' cornering grip (the F1 06-style handling: planted)
    float surfaceGrip() const { return track.def && track.def->street ? 0.92f : 1.0f; }   // dusty public roads
    float playerThrPrev = 0, playerStab = 0; // (the throttle stab detector)
    // the tyres and the chassis between frames (not saved: it settles again in a moment)
    struct Handling {
        float wt = 0;            // weight transfer: + nose loaded (braking), - tail loaded (accelerating)
        float accPrev = 0;       // last frame's acceleration along the car, m/s^2
        float latUse = 0, uT = 0, uB = 0;   // how much of the cornering / traction / braking grip is in use (0..1+)
        float boost = 1;         // turbo boost built up (1980 turbo: lag)
        float cut = 0;           // gear change: drive cut for this long
        float lock = 0;          // wheels locked under braking (0..1)
        bool frontLock = true;   // the fronts lock (else the rears)
        float rearLock = 0;      // rears locked by a downshift (s)
        float skirt = 0;         // 1980 wing car: the skirts have lost their seal (s)
        float dirty = 0;         // 0..1 in another car's turbulent air
        float aqua = 0;          // 0..1 aquaplaning
        float flatSpot = 0;      // from lockups (vibration, a little grip)
        int lastGear = 1;
    };
    std::vector<Handling> hand;
    float standingWater() const;
    float aquaSpeed(const Car& c, int i) const;
    Handling& handOf(const Car& c) { size_t i = (size_t)(&c - cars.data()); if (hand.size() != cars.size()) hand.assign(cars.size(), Handling()); return hand[i < hand.size() ? i : 0]; }
    int steerHelp = 0;                       // the player's steering help: 0 off, 1 light, 2 strong (set by the game)
    // the steering lock at speed v (rad at full wheel), and how much of the wheel gives a lateral pull
    static float steerLock(float v) { return 0.30f / (1 + v / 13.0f) + 0.014f; }
    float steerFor(float v, float lat) const { return v < 3 ? 1 : clampf(atanf(lat * 2.9f / (v * v)) / steerLock(v), 0, 1); }
    float playerLatMax = 0;                  // the player's cornering grip last step, m/s^2 (for the steering help)
    bool newRecord = false;                  // set when a lap beats the record (the game saves the ghost)
    float ttDelta = 0; bool ttDeltaOn = false;   // the live gap to the record (+ slower)
    bool ghostAt(float t, GhostPt& out) const;   // the ghost's place t seconds into its lap
    bool saveGhost(const char* path) const;
    bool loadGhost(const char* path);
    static bool peek(const char* path, RaceSettings& s, int& session, float& raceTime, int& lap);   // for the load menu
    void setupPit();
    void setupStart();

private:
    void plan();
    void physics(Car& c, float dt);
    void drive(Car& c, float dt);
    void collide(float dt);
    void rank();
    // racecontrol.cpp
    void weather(float dt);
    void raceControl(float dt);
    void strategy(Car& c);
    void choosePlan(Car& c);
    bool pitDrive(Car& c, float dt);
    void pitStop(Car& c, float dt);
    void pitStopManual(Car& c, float dt);
    void qteNext(Car& c);
    void addDamage(Car& c, int part, float amount);
    void detach(Car& c, int part);
    void particles(float dt);
    void moveSafetyCar(float dt);
    float tyreGrip(const Car& c) const;
    void penaltyCheck(Car& c, float dt);
    void driverUpdate(Car& c, float dt);
    void blueFlags(float dt);
public:
    static bool blueOff;     // tests: the old traffic behaviour, to compare
    static bool hybridOff;   // tests: no hybrid boost, to compare
private:
};
