// Drawing the race: circuit, scenery, cars, the cockpit (AI-made model with a turning wheel,
// driver's arms and live displays) and the heads-up display.
#pragma once
#include "race.h"
#include <vector>

enum CamMode { CAM_CHASE = 0, CAM_FAR, CAM_COCKPIT, CAM_COUNT, CAM_TV = 10 };

// performance options (settings menu)
struct Perf { float distance = 1; float lodBias = 1; bool scenery = true; bool effects = true; };
extern Perf perf;

struct Camera { V3 eye, fwd, up; float fov = 60; };

struct Renderer {
    float sceneAspect = (float)SCREEN_W / SCREEN_H;   // drawScene's width / height (the mirror is wider)
    bool sceneMirrorX = false;                       // drawScene flipped left-right (a mirror)
    bool sceneLite = false;                          // drawScene: road and cars only (the mirror)
    Model carModel[48];
    bool carLoaded[48] = {};
    Model wheel, prop[PROP_COUNT], cockpit[2], steering[ERA_COUNT], pitCrew, fuelRig;
    Model eraWheel[ERA_COUNT], flap[ERA_COUNT];      // each era's road wheel and DRS flap
    void drawFlap(const Car& c, const M4& place, bool near);
    // the F1 rear light (render_fx.cpp): where it sits on each era's car, which mode it is in, and
    // whether a flashing mode is lit at time t
    enum { RL_OFF, RL_RAIN_STEADY, RL_RAIN, RL_HARVEST, RL_LIMITER };
    static const V3 REAR_LIGHT[ERA_COUNT];
    int rearLightMode(const Race& r, const Car& c) const;
    static bool rearLightLit(int mode, float t);
    bool eraPartsLoaded[ERA_COUNT] = {};
    Model safetyCar[ERA_COUNT];
    // where each car last stood in its box (the crew go back in from there after it has gone)
    struct StopSpot { V3 pos, fwd, left; bool valid = false; };
    StopSpot stopSpot[64];        // each era's safety car (tools/gen_safety_cars.py; tyres on the model)
    void loadEraParts(int era);
    // crew / driver poses made in Blender (tools/blender/pit_crew.py -> tools/gen_crew.py): DATA/models/crew_<anim>_<frame>.emm
    enum { ANIM_IDLE = 0, ANIM_WHEELGUN, ANIM_PUSH, ANIM_CELEBRATE, ANIM_COUNT };
    static const int POSE_FRAMES = 4;
    Model pose[ANIM_COUNT][POSE_FRAMES]; int poseCount[ANIM_COUNT] = {};
    // a person (crew member / driver) at `place` (metres, feet on z 0, facing +X), playing `anim` at time t,
    // in a team's colours (suit and helmet: `main`, flashes and helmet stripe: `second`; 0 0 = the default)
    void drawPerson(const M4& place, int anim, float t, bool clipped = false, u32 main = 0, u32 second = 0);
    void teamColours(const CarDef* d, u32& main, u32& second) const;
    // the crew texture with each team's palette (8 KB on the GPU each; the pixels are shared)
    // (keyed by the pixels too: the crew and the driver have different textures)
    struct CrewPal { const void* pixels; u32 main, second; Texture tex; u32 pal[256] __attribute__((aligned(16))); };
    CrewPal crewPal[48]; int crewPalCount = 0;      // (emptied with the cars: each race fills its own)
    const Texture* teamTexture(const Texture* tex, u32 main, u32 second);
    // the driver in the car, made in Blender (tools/blender/cockpit_parts.py -> tools/gen_cockpit.py):
    // helmet, HANS, shoulders and arms in the team's colours; not drawn from his own seat
    Model driver;
    void drawDriver(const Car& c, const M4& place, int lod, bool near);
    int lvyLoadedStyle[ERA_COUNT] = {-1, -1, -1, -1};   // the one career livery style kept in lvyBlob
    // car models re-ordered into body / nose / rear wing (the nose and wing can come off)
    struct PartSplit { int body = 0, noseFirst = 0, noseCount = 0, wingFirst = 0, wingCount = 0, glassFirst = 0, glassCount = 0; V3 noseC, wingC; };
    PartSplit split[48][4];
    std::vector<WorldVertex> pitVerts; V3 pitOrigin;
    int scModel = -1;
    Texture puff;                    // soft round blob for smoke, spray and lights
    alignas(16) u8 puffPix[16 * 16]; alignas(16) u32 puffPal[256];
    // TV camera
    V3 tvPos; float tvS = 0; bool tvInit = false; int tvFocus = -1, tvCut = 0, tvBlocked = 0; float tvCheck = 0;
    bool sightBlocked(const Track& t, const V3& a, const V3& b) const;
    TexFile tex[4];          // asphalt, grass, gravel, kerb
    Font font;
    // smoothed chase camera
    V3 camPos, camFwd;
    bool camInit = false;
    float speedFeel = 0, shakeAmp = 0;   // the camera's sense of speed: smoothed speed share, shake strength
    void speedShake(const Race& r, const Car& c, float dt, float& sx, float& sy);
    V3 camRight, camUp;             // camera axes for this frame
    std::vector<int> visChunks;     // chunks on screen this frame
    int raceMark = 0;               // video memory used before any car was loaded

    bool loadCommon();
    void loadCar(int idx);
    void freeCars();

    Camera camera(const Race& r, int mode, float dt, bool lookBack);
    void drawScene(const Race& r, const Camera& cam, bool hidePlayer);
    void drawCockpit(const Race& r, const Camera& cam);
    void drawCabin(const Car& c, const Camera& cam, M4& car, M4& wheelM, const Race* race = nullptr);
    // windscreen wiper and rain drops on the glass (cockpit view)
    struct Screen { bool done = false, ok = false; V3 base, top; float halfW = 0, h = 0; };
    Screen screenOf[48];
    const Screen& windscreen(int idx);
    struct Drop { float u, v, s; };
    Drop drops[240]; int dropCount = 0;     // the PSP uses up to 96, the Vita 240
    float wipeAngle = 0, wipeT = 0, wipeLastT = -1; u32 dropSeed = 1;
    void drawWindscreen(const Car& c, const Camera& cam, const Race* race);
    void drawRain(const Race& r, bool cockpit);
    V3 cockpitEye(int carIdx) const;        // in car space (the car must be loaded)
    void drawHUD(const Race& r, int mode);
    void drawCarShowroom(int carIdx, float angle, float x0, float y0, float w, float h);
    void drawMap(const Track& t, const Race* r, float x, float y, float size, u32 col);
    void drawCarAt(const Car& c, const V3& eye, const V3& fwd, float tanH, float tanV) { drawCar(c, eye, fwd, tanH, tanV); }   // (set camRight / camUp first)
    // render_fx.cpp
    void splitCar(int idx);
    void buildPit(const Race& r);
    void drawPit(const Race& r, const Camera& cam);
    void drawEffects(const Race& r, const Camera& cam, bool ownLights = true);
    u32 skyColour(const Race& r) const;
    void drawNight(const Race& r, float darkLater = 0);   // darkLater: darkness still to come over everything (the cockpit at night)
    float cabinDark(const Race& r) const;
    void drawBeam(const Race& r, const Camera& cam);
    void drawBeamFor(const Car& c, const Camera& cam, float k, bool pool);
    float brakeHeat[64] = {}; float heatLastT = -1;   // glowing brake discs (per car, 0..1)
    void drawWeather(const Race& r, bool cockpit);
    Camera tvCamera(const Race& r, int focus, float dt);
    Camera cinematicCamera(const Race& r, const Car& c, float t);
    void drawRaceInfo(const Race& r);
    void drawQte(const Race& r);
    void drawCockpitHud(const Race& r);
    void drawSpeedBox(const Car& c);
    void drawRelative(const Race& r);      // PS Vita HUD: gaps ahead / behind, stint, fuel laps
    // the instruments made for the Unity AI interiors (Vita): wheel screen with shift lights, dash display(s)
    void drawNewInstruments(const Race& r, const Car& c, const M4& car, const M4& wheelM, float rpmF, u32 flagCol, const char* flagWord);
    void drawMarshalFlags(const Race& r, const Camera& cam);   // blue flags waved at the marshal posts ahead of the player
    // career livery: the team car is repainted from a set of painted styles (DATA/models/livery_<id>.lvy),
    // its palette built from the three chosen colours
    void setLivery(int carIdx, int style, u32 a, u32 b, u32 c);
    void applyLivery(int carIdx);
    void* lvyBlob[ERA_COUNT] = {};
    int lvyStyle[ERA_COUNT] = {-1, -1, -1, -1}; u32 lvyCol[ERA_COUNT][3] = {};
    void drawDamage(const Car& c, float cx, float cy, float time);

    // 3D text and coloured quads in some local space (used on the wheel display and dash)
    void text3D(const M4& place, const char* s, float height, u32 color, bool centre);
    void quad3D(const M4& place, float x0, float y0, float x1, float y1, u32 color);
    void box3D(const M4& place, const V3& lo, const V3& hi, u32 color);
    // the marshal light panels (yellow at incidents, the VSC and the safety car; blue flags) and
    // the fans' flags waving over the grandstands and bleachers (render_fx.cpp)
    void drawTrackLife(const Race& r, const Camera& cam);
    void drawBoards(const Race& r, const Camera& cam, float tanH, float tanV);
    void drawGhost(const Race& r, const Camera& cam, float tanH, float tanV);
    void drawTimeTrialHud(const Race& r);
    std::vector<ModelVertex> ghostVerts; int ghostFor = -1;

private:
    void drawCar(const Car& c, const V3& eye, const V3& fwd, float tanH, float tanV, bool inside = false);
    void drawWheels(const CarDef& d, const M4& place, int wl, float steer, float spin, bool frontOnly = false, const Car* car = nullptr, const V3* eye = nullptr);
    bool visible(const V3& rel, float radius, const Camera& cam, float tanH, float tanV, float maxDist) const;
};

void formatTime(char* out, float t);
