// Cars and circuits (copied from the Unity game's CarDatabase / TrackDatabase) and the loaders
// for the model packs (.emm) and texture files made by the tools/ scripts.
#pragma once
#include "platform.h"

enum CarClass { CLASS_HYPER = 0, CLASS_GT3 = 1 };   // (Formula Legends: every car is CLASS_HYPER, one class)
// the four generations of Formula 1 car in the game
enum Era { ERA_90S = 0, ERA_V8 = 1, ERA_HYBRID = 2, ERA_GE = 3, ERA_COUNT = 4 };
extern const char* ERA_NAME[ERA_COUNT];     // "1990s V10", ...
extern const char* ERA_SHORT[ERA_COUNT];    // "1998", "2010", "2016", "2025"
extern const char* ERA_ID[ERA_COUNT];       // "90s", "v8", "hybrid", "ge" (files)

struct CarDef {
    const char* id;
    const char* make;
    const char* model;
    const char* team;
    int numbers[2];
    CarClass cls;
    int era;                 // Era
    const char* engine;      // label shown in the menu
    const char* sound;       // engine recording (DATA/audio/<sound>.raw)
    float idleRpm, maxRpm;
    // performance: mass with the driver, engine power, top speed gain, drag factor, grip factor
    float massKg, powerKW, topGain, aero, grip;
    float lengthM;
    u32 colour;              // main paint colour (menus, map dots)
    bool hasWheels;          // the AI model already has wheels
    float cornerMul = 1;     // AI corner speed factor (career car upgrades)
    bool career = false;     // the player's own team car in career mode (not in the normal car lists)
};

struct TrackDef {
    const char* id;
    const char* name;
    const char* location;
    float startHour;         // local time of the Grand Prix start
    float hours;             // a full Grand Prix's length in hours (about 1.5)
    int gpLaps;              // laps of the real Grand Prix
    bool street;             // street circuit: walls close to the road, buildings behind
    bool floodlit;           // night race under floodlights
};
// Each era's championship calendar (indices into TRACKS) and which eras race at a circuit
static const int ERA_ROUNDS = 10;
extern const int ERA_CAL[ERA_COUNT][ERA_ROUNDS];
extern const float ERA_HOUR[ERA_COUNT][ERA_ROUNDS];   // start time of each round (night races)
bool trackInEra(int track, int era);
int eraTrackCount(int era);
int eraTrack(int era, int k);                          // k-th circuit of an era's calendar

extern CarDef CARS[];          // not const: the career team car takes the player's name, livery and upgrades
extern const int CAR_COUNT;     // every car, the career cars included (they are last)
extern const int CAR_PICK;      // the 40 team cars (10 per era)
int careerCarIndex(int era);    // CARS index of the career car of that era
int eraFirstCar(int era);       // CARS index of an era's first team (10 in a row)
static const int TEAMS_PER_ERA = 10;
extern const TrackDef TRACKS[];
extern const int TRACK_COUNT;

struct ModelLod {
    int count = 0;
    int glass = 0;           // vertices at the end of the list that are window glass
    const ModelVertex* verts = nullptr;
    Texture tex;
};

struct Model {
    int lodCount = 0;
    ModelLod lod[4];
    float scale[3] = {1, 1, 1};
    float offset[3] = {0, 0, 0};
    float cabX = 0;          // cars: where the cabin sits along the car (the interior is placed there)
    float panelY = 1, panelX = 0;   // cars: where the race number goes on the side
    // optional 'EMX1' block after the levels of detail (the Vita's Unity AI cars): where this body's
    // wheel arches are. axleF == 0: use the class defaults. ownWheels: the model has its wheels on.
    float axleF = 0, axleR = 0, wheelY = 0.86f, wheelR = 0;
    bool ownWheels = false;
    uint32_t flags = 0;    // EMX1 flags: bit 0 own wheels, bit 1 Unity AI interior (new instrument layout)
    // levels of detail stored without a texture of their own use level 0's (the GPU's mipmaps already
    // make it smaller in the distance): bit i set = lod[i] borrows lod[0]'s texture
    uint8_t sharedTex = 0;
    void reshare() { for (int i = 1; i < lodCount; i++) if (sharedTex & (1u << i)) lod[i].tex = lod[0].tex; }
    void* blob = nullptr;
    bool load(const char* path);
    void unload();
    // model matrix that turns the stored -1..1 positions into metres placed by `place`
    M4 matrix(const M4& place) const {
        return place * M4::translate(V3(offset[0], offset[1], offset[2])) * M4::scale(scale[0], scale[1], scale[2]);
    }
};

struct TexFile {
    Texture tex;
    void* blob = nullptr;
    bool load(const char* path);
    void unload();
};

struct Font {
    Texture tex;
    int lineH = 16;
    int texScale = 1;     // 'FNT2' (the Vita's font): the atlas is drawn this many times larger than the layout
    float u(int px) const { return (float)(px * texScale) / tex.w; }
    float v(int px) const { return (float)(px * texScale) / tex.h; }
    struct Glyph { int16_t x, y, w, h, adv, xoff; } g[95];
    void* blob = nullptr;
    bool load(const char* path);
    int width(const char* s, float scale = 1) const;
    void draw(float x, float y, const char* s, u32 color, float scale = 1) const;
    void drawRight(float x, float y, const char* s, u32 color, float scale = 1) const { draw(x - width(s, scale), y, s, color, scale); }
    void drawCentre(float x, float y, const char* s, u32 color, float scale = 1) const { draw(x - width(s, scale) * 0.5f, y, s, color, scale); }
};
