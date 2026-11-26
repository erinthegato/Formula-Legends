// A real-world circuit: centre line every ~5 m with real elevation, plus the 3D scenery
// (road, kerbs, grass, gravel, barriers, terrain, props) built when the track loads.
#pragma once
#include <vector>
#include "platform.h"
#include "data.h"

struct TrackPoint {
    V3 p;          // centre (metres, x east, y north, z up)
    V3 t;          // unit tangent (direction of travel, includes slope)
    V3 n;          // unit left normal (horizontal)
    float hw;      // half width of the tarmac
    float curv;    // curvature (1/m, + = left bend), smoothed
    float grade;   // dz/ds
    bool kerb;     // kerbs on both sides here
    float outL, outR;  // run-off width left / right (grass or gravel); cut short where another part of the lap is close
    bool gravelL, gravelR;
};

enum { MAT_ROAD, MAT_KERB, MAT_GRASS, MAT_GRAVEL, MAT_WALL, MAT_TERRAIN, MAT_LINE, MAT_COUNT };

struct Chunk {
    V3 origin;              // vertices are stored relative to this
    V3 centre; float radius;
    int first[MAT_COUNT], count[MAT_COUNT];
};

struct PropInst { int model; V3 p; float yaw; float scale; float s = 0; };   // s: metres round the lap (flag panels)
// distance boards before the sharp corners (300 / 200 / 100 m to the turn-in); yaw = the way the face looks
struct BrakeBoard { V3 p; float yaw; int metres; };
// props grouped into 200 m squares so whole groups can be skipped when off-screen
struct PropCell { V3 centre; float radius; int first, count; };

enum { PROP_GRANDSTAND, PROP_TREE_PINE, PROP_TREE_ROUND, PROP_TYRES, PROP_BILLBOARD, PROP_MARSHAL,
       // Unity AI buildings made for the Vita (HD/models); the PSP has no model for them and skips them
       PROP_CONTROL, PROP_BRIDGE, PROP_TVTOWER, PROP_HOSPITALITY,
       // trackside objects made in Blender (tools/blender/trackside.py), one 64 px texture each
       PROP_GANTRY, PROP_BLEACHER, PROP_TENT, PROP_FLAGPANEL, PROP_CRANE, PROP_TECPRO, PROP_COUNT };

// pit area (distances from the line) kept flat at road level on the left of the straight
// the pit lane: entry PIT_IN_BEFORE m before the line, PIT_LANE_LEN long; garages from PIT_BOX0 m after
// the entry, 16 m each (10 teams of the era + the career team). PIT_FROM..PIT_TO: the flat pit area
const float PIT_IN_BEFORE = 290, PIT_LANE_LEN = 480, PIT_BOX0 = 105;
const float PIT_FROM = -320, PIT_TO = 215, PIT_W = 32;
// a car's garage slot in its era's pit lane: 0..9 the teams, 10 the career team
inline int pitSlot(const CarDef* d) { int i = (int)(d - CARS); return i >= CAR_PICK ? TEAMS_PER_ERA : i % TEAMS_PER_ERA; }

struct Track {
    const TrackDef* def = nullptr;
    int n = 0;
    float step = 5, length = 0;
    float gantryScale = 1;   // the start gantry model (20 m between pillars) scaled to the road
    float elevMin = 0, elevMax = 0;
    std::vector<TrackPoint> pts;
    // terrain from the elevation model
    float gx0 = 0, gy0 = 0, gstep = 200; int gnx = 0, gny = 0;
    std::vector<float> gz;
    // scenery
    std::vector<WorldVertex> verts;
    std::vector<Chunk> chunks;
    std::vector<PropInst> props;
    // footprints on the ground (an oriented box: centre, unit axis along its length, half sizes): the
    // city blocks, so nothing else is put inside them
    struct Footprint { float x, y, ax, ay, hx, hy; };
    std::vector<Footprint> blocks;
    int propsDropped = 0;                // (log) trackside objects left out: on a road or inside another one
    std::vector<PropCell> propCells;
    std::vector<BrakeBoard> boards;
    std::vector<V3> speakers;       // trackside PA speakers (on grandstands and poles round the lap)
    // ground look
    u32 grassTint = 0xFFFFFFFF, terrainTint = 0xFFFFFFFF;
    bool desert = false;
    bool street = false;     // street circuit: walls by the road, buildings behind
    int closeRunoff = 0;     // track points whose run-off was cut short by a nearby part of the lap
    static bool noRunoffCut; // tests only: skip that (to compare before / after)
    static bool noGroundCap; // tests only: the terrain as it was (to compare before / after)
    static bool keepGround;  // tests only: keep the terrain mesh heights (groundTz) after the build
    std::vector<float> groundTz; int gtnx = 0, gtny = 0; float gsub = 0;
    // map outline for the HUD
    float mapMinX = 0, mapMinY = 0, mapScale = 1;

    bool load(const TrackDef* d, bool scenery = true);   // scenery=false: centre line only (menu map)
    void free();
    void buildBoards();

    int index(float s) const { int i = (int)(s / step); i %= n; if (i < 0) i += n; return i; }
    float wrap(float s) const { while (s >= length) s -= length; while (s < 0) s += length; return s; }
    // interpolated centre-line data at distance s
    void frame(float s, V3& pos, V3& tangent, V3& left, float& hw) const;
    float curvAt(float s) const;
    float gradeAt(float s) const;
    float terrainZ(float x, float y) const;
    // ground under (s, d) off the tarmac: height above the centre line there and the sideways
    // slope (metres up per metre further from the centre line), from the same verge the mesh draws
    float groundAt(float s, float d, float* slope = nullptr) const;
    float vergeEdgeZ(int i, int side) const;   // height of the verge's outer edge (barrier foot)
    // world position of (s, d): d = metres to the left of the centre line
    V3 world(float s, float d, float lift = 0) const;

    static float terrainCell;            // terrain cell size, metres (33; 50 on the Performance preset)
    float meshZ(float x, float y) const;      // the terrain mesh's own height
private:
    void buildMesh();
    void addBuildings(Chunk& ch, int c0, int c1, std::vector<WorldVertex>* bins);
    void buildProps();
    void addTrackside(std::vector<V3>& keepClear);
    void tidyProps();
    static constexpr float GROUND_FLAT = 25.0f, GROUND_BLEND = 50.0f;   // ground flat under the circuit, then back to the hills                    // drop objects that stand on a road, in another's run-off or in each other
    bool onAnyRoad(const Footprint& f, int ownIndex, bool allowOwnRunoff) const;
    void groupProps();
    void placeSpeakers();
    void addQuad(Chunk& c, int mat, const V3& a, const V3& b, const V3& cc, const V3& d,
                 float u0, float v0, float u1, float v1, u32 col, std::vector<WorldVertex>* bins);
};
