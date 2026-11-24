#include "data.h"
#include <cstdlib>
#include <cstring>

static u32 C(float r, float g, float b) { return RGBA((int)(r * 255), (int)(g * 255), (int)(b * 255)); }

// The teams of each era (fictional names in the style of the real ones). 10 per era, in the era's
// pecking order, then the career cars (your own team, one per era).
CarDef CARS[] = {
    // id                make        model   team                      numbers  class        era         engine                         sound                  idle  max     kg   kW    gain    aero   grip   len    colour                    wheels
    {"m98_marchetti", "Marchetti", "F98", "Scuderia Rosso Marchetti", {3, 4}, CLASS_HYPER, ERA_90S, "3.0 V10 naturally aspirated", "engine_v12", 4000, 17500, 600, 571, 0.000f, 1.005f, 1.007f, 4.45f, C(0.80f, 0.03f, 0.03f), false},
    {"m98_wexford", "Wexford", "W20", "Wexford Grand Prix", {1, 2}, CLASS_HYPER, ERA_90S, "3.0 V10 naturally aspirated", "engine_v12", 4000, 17500, 600, 566, 0.000f, 0.999f, 1.008f, 4.45f, C(0.06f, 0.12f, 0.42f), false},
    {"m98_woking", "Woking", "WK-13", "Woking Silver Racing", {7, 8}, CLASS_HYPER, ERA_90S, "3.0 V10 naturally aspirated", "engine_v12", 4000, 17500, 600, 568, 0.000f, 0.989f, 1.007f, 4.45f, C(0.72f, 0.74f, 0.78f), false},
    {"m98_benedetto", "Benedetto", "B198", "Benedetto Formula", {5, 6}, CLASS_HYPER, ERA_90S, "3.0 V10 naturally aspirated", "engine_v12", 4000, 17500, 600, 562, 0.000f, 1.000f, 1.001f, 4.45f, C(0.20f, 0.45f, 0.85f), false},
    {"m98_shamrock", "Shamrock", "S198", "Shamrock Grand Prix", {9, 10}, CLASS_HYPER, ERA_90S, "3.0 V10 naturally aspirated", "engine_v12", 4000, 17500, 600, 562, 0.000f, 0.992f, 1.001f, 4.45f, C(0.98f, 0.82f, 0.05f), false},
    {"m98_bleu", "Bleu", "AP01", "Equipe Bleu", {11, 12}, CLASS_HYPER, ERA_90S, "3.0 V10 naturally aspirated", "engine_v12", 4000, 17500, 600, 556, 0.000f, 1.014f, 0.999f, 4.45f, C(0.10f, 0.28f, 0.75f), false},
    {"m98_arrowfield", "Arrowfield", "A19", "Arrowfield Racing", {16, 17}, CLASS_HYPER, ERA_90S, "3.0 V10 naturally aspirated", "engine_v12", 4000, 17500, 600, 557, 0.000f, 1.006f, 0.996f, 4.45f, C(0.08f, 0.08f, 0.09f), false},
    {"m98_tartan", "Tartan", "TG2", "Tartan Grand Prix", {18, 19}, CLASS_HYPER, ERA_90S, "3.0 V10 naturally aspirated", "engine_v12", 4000, 17500, 600, 557, 0.000f, 0.986f, 0.997f, 4.45f, C(0.95f, 0.95f, 0.95f), false},
    {"m98_hinwil", "Hinwil", "C17", "Hinwil Sauberwerk", {14, 15}, CLASS_HYPER, ERA_90S, "3.0 V10 naturally aspirated", "engine_v12", 4000, 17500, 600, 553, 0.000f, 1.009f, 0.996f, 4.45f, C(0.12f, 0.22f, 0.55f), false},
    {"m98_faenza", "Faenza", "M198", "Faenza Corse", {22, 23}, CLASS_HYPER, ERA_90S, "3.0 V10 naturally aspirated", "engine_v12", 4000, 17500, 600, 554, 0.000f, 0.990f, 0.992f, 4.45f, C(0.06f, 0.06f, 0.07f), false},

    {"m10_marchetti", "Marchetti", "F10", "Scuderia Rosso Marchetti", {7, 8}, CLASS_HYPER, ERA_V8, "2.4 V8 naturally aspirated", "engine_v8_flatplane", 4500, 18000, 620, 571, 0.000f, 0.987f, 1.012f, 4.95f, C(0.82f, 0.03f, 0.03f), false},
    {"m10_woking", "Woking", "WK-25", "Woking Silver Racing", {1, 2}, CLASS_HYPER, ERA_V8, "2.4 V8 naturally aspirated", "engine_v8_flatplane", 4500, 18000, 620, 569, 0.000f, 0.988f, 1.011f, 4.95f, C(0.76f, 0.78f, 0.82f), false},
    {"m10_taurus", "Taurus", "TR6", "Taurus Energy Racing", {5, 6}, CLASS_HYPER, ERA_V8, "2.4 V8 naturally aspirated", "engine_v8_flatplane", 4500, 18000, 620, 567, 0.000f, 0.997f, 1.005f, 4.95f, C(0.07f, 0.10f, 0.30f), false},
    {"m10_sterne", "Sterne", "S01", "Sterne Grand Prix", {3, 4}, CLASS_HYPER, ERA_V8, "2.4 V8 naturally aspirated", "engine_v8_flatplane", 4500, 18000, 620, 564, 0.000f, 0.997f, 1.004f, 4.95f, C(0.75f, 0.77f, 0.80f), false},
    {"m10_wexford", "Wexford", "W32", "Wexford Grand Prix", {9, 10}, CLASS_HYPER, ERA_V8, "2.4 V8 naturally aspirated", "engine_v8_flatplane", 4500, 18000, 620, 562, 0.000f, 0.987f, 1.003f, 4.95f, C(0.08f, 0.14f, 0.40f), false},
    {"m10_enstone", "Enstone", "E30", "Enstone Jaune", {11, 12}, CLASS_HYPER, ERA_V8, "2.4 V8 naturally aspirated", "engine_v8_flatplane", 4500, 18000, 620, 560, 0.000f, 1.013f, 1.001f, 4.95f, C(0.98f, 0.84f, 0.05f), false},
    {"m10_tiger", "Tiger", "T03", "Silverstone Tiger Racing", {14, 15}, CLASS_HYPER, ERA_V8, "2.4 V8 naturally aspirated", "engine_v8_flatplane", 4500, 18000, 620, 558, 0.000f, 1.007f, 0.994f, 4.95f, C(0.95f, 0.95f, 0.95f), false},
    {"m10_faenza", "Faenza", "STR5", "Faenza Corse", {16, 17}, CLASS_HYPER, ERA_V8, "2.4 V8 naturally aspirated", "engine_v8_flatplane", 4500, 18000, 620, 555, 0.000f, 1.012f, 0.998f, 4.95f, C(0.10f, 0.12f, 0.35f), false},
    {"m10_hinwil", "Hinwil", "C29", "Hinwil Sauberwerk", {22, 23}, CLASS_HYPER, ERA_V8, "2.4 V8 naturally aspirated", "engine_v8_flatplane", 4500, 18000, 620, 550, 0.000f, 1.011f, 0.993f, 4.95f, C(0.92f, 0.92f, 0.92f), false},
    {"m10_norfolk", "Norfolk", "N127", "Norfolk Racing", {18, 19}, CLASS_HYPER, ERA_V8, "2.4 V8 naturally aspirated", "engine_v8_flatplane", 4500, 18000, 620, 550, 0.000f, 0.989f, 0.993f, 4.95f, C(0.05f, 0.30f, 0.15f), false},

    {"m16_sterne", "Sterne", "S07", "Sterne Grand Prix", {44, 6}, CLASS_HYPER, ERA_HYBRID, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 15000, 702, 609, 0.000f, 0.998f, 1.008f, 5.25f, C(0.74f, 0.76f, 0.79f), false},
    {"m16_marchetti", "Marchetti", "SF16", "Scuderia Rosso Marchetti", {5, 7}, CLASS_HYPER, ERA_HYBRID, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 15000, 702, 605, 0.000f, 1.000f, 1.009f, 5.25f, C(0.85f, 0.04f, 0.04f), false},
    {"m16_taurus", "Taurus", "TR12", "Taurus Energy Racing", {3, 33}, CLASS_HYPER, ERA_HYBRID, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 15000, 702, 606, 0.000f, 1.006f, 1.005f, 5.25f, C(0.10f, 0.12f, 0.28f), false},
    {"m16_wexford", "Wexford", "W38", "Wexford Grand Prix", {19, 77}, CLASS_HYPER, ERA_HYBRID, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 15000, 702, 600, 0.000f, 0.992f, 1.006f, 5.25f, C(0.96f, 0.96f, 0.96f), false},
    {"m16_tiger", "Tiger", "T09", "Silverstone Tiger Racing", {11, 27}, CLASS_HYPER, ERA_HYBRID, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 15000, 702, 601, 0.000f, 1.002f, 1.003f, 5.25f, C(0.10f, 0.10f, 0.11f), false},
    {"m16_enstone", "Enstone", "RS16", "Enstone Jaune", {20, 30}, CLASS_HYPER, ERA_HYBRID, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 15000, 702, 601, 0.000f, 0.990f, 0.999f, 5.25f, C(0.98f, 0.85f, 0.05f), false},
    {"m16_faenza", "Faenza", "STR11", "Faenza Corse", {55, 26}, CLASS_HYPER, ERA_HYBRID, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 15000, 702, 598, 0.000f, 1.013f, 0.995f, 5.25f, C(0.10f, 0.14f, 0.45f), false},
    {"m16_woking", "Woking", "WK-31", "Woking Racing", {14, 22}, CLASS_HYPER, ERA_HYBRID, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 15000, 702, 597, 0.000f, 1.010f, 0.995f, 5.25f, C(0.22f, 0.23f, 0.25f), false},
    {"m16_hinwil", "Hinwil", "C35", "Hinwil Sauberwerk", {9, 12}, CLASS_HYPER, ERA_HYBRID, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 15000, 702, 593, 0.000f, 1.013f, 0.992f, 5.25f, C(0.10f, 0.30f, 0.75f), false},
    {"m16_kannapolis", "Kannapolis", "K16", "Kannapolis Racing", {8, 21}, CLASS_HYPER, ERA_HYBRID, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 15000, 702, 590, 0.000f, 1.009f, 0.987f, 5.25f, C(0.42f, 0.44f, 0.46f), false},

    {"m25_papaya", "Woking", "WK-39", "Papaya Woking", {4, 81}, CLASS_HYPER, ERA_GE, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 13000, 800, 609, 0.000f, 0.996f, 1.012f, 5.62f, C(1.00f, 0.50f, 0.05f), false},
    {"m25_taurus", "Taurus", "TR21", "Taurus Energy Racing", {1, 22}, CLASS_HYPER, ERA_GE, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 13000, 800, 607, 0.000f, 0.999f, 1.004f, 5.62f, C(0.08f, 0.10f, 0.30f), false},
    {"m25_marchetti", "Marchetti", "SF25", "Scuderia Rosso Marchetti", {16, 44}, CLASS_HYPER, ERA_GE, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 13000, 800, 604, 0.000f, 0.999f, 1.005f, 5.62f, C(0.86f, 0.03f, 0.03f), false},
    {"m25_sterne", "Sterne", "S16", "Sterne Grand Prix", {63, 12}, CLASS_HYPER, ERA_GE, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 13000, 800, 601, 0.000f, 0.994f, 1.002f, 5.62f, C(0.10f, 0.10f, 0.11f), false},
    {"m25_banbury", "Banbury", "B25", "Banbury Green Racing", {14, 18}, CLASS_HYPER, ERA_GE, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 13000, 800, 603, 0.000f, 1.007f, 1.004f, 5.62f, C(0.02f, 0.32f, 0.22f), false},
    {"m25_enstone", "Enstone", "A525", "Enstone Alpine", {10, 43}, CLASS_HYPER, ERA_GE, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 13000, 800, 596, 0.000f, 0.992f, 0.999f, 5.62f, C(0.10f, 0.30f, 0.80f), false},
    {"m25_kannapolis", "Kannapolis", "K25", "Kannapolis Racing", {31, 87}, CLASS_HYPER, ERA_GE, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 13000, 800, 594, 0.000f, 0.992f, 0.997f, 5.62f, C(0.95f, 0.95f, 0.96f), false},
    {"m25_faenza", "Faenza", "VR02", "Faenza Racing", {6, 30}, CLASS_HYPER, ERA_GE, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 13000, 800, 592, 0.000f, 0.997f, 0.991f, 5.62f, C(0.95f, 0.95f, 0.97f), false},
    {"m25_hinwil", "Hinwil", "C45", "Hinwil Kick", {27, 5}, CLASS_HYPER, ERA_GE, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 13000, 800, 591, 0.000f, 1.012f, 0.991f, 5.62f, C(0.10f, 0.10f, 0.10f), false},
    {"m25_wexford", "Wexford", "W47", "Wexford Grand Prix", {23, 55}, CLASS_HYPER, ERA_GE, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 13000, 800, 590, 0.000f, 1.012f, 0.993f, 5.62f, C(0.05f, 0.18f, 0.55f), false},

    // career mode: your own team's car, one per era (name, number, livery and upgrades set by the career)
    {"career_90s", "Your", "Grand Prix car", "Your Team", {0, 0}, CLASS_HYPER, ERA_90S, "3.0 V10 naturally aspirated", "engine_v12", 4000, 17500, 604, 554, 0.000f, 1.000f, 0.995f, 4.45f, C(0.90f, 0.45f, 0.05f), false, 1.0f, true},
    {"career_v8", "Your", "Grand Prix car", "Your Team", {0, 0}, CLASS_HYPER, ERA_V8, "2.4 V8 naturally aspirated", "engine_v8_flatplane", 4500, 18000, 624, 554, 0.000f, 1.000f, 0.995f, 4.95f, C(0.90f, 0.45f, 0.05f), false, 1.0f, true},
    {"career_hybrid", "Your", "Grand Prix car", "Your Team", {0, 0}, CLASS_HYPER, ERA_HYBRID, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 15000, 706, 594, 0.000f, 1.000f, 0.995f, 5.25f, C(0.90f, 0.45f, 0.05f), false, 1.0f, true},
    {"career_ge", "Your", "Grand Prix car", "Your Team", {0, 0}, CLASS_HYPER, ERA_GE, "1.6 V6 turbo hybrid", "engine_hyper_v6", 4000, 13000, 804, 594, 0.000f, 1.000f, 0.995f, 5.62f, C(0.90f, 0.45f, 0.05f), false, 1.0f, true},
};
const int CAR_COUNT = sizeof(CARS) / sizeof(CARS[0]);
const int CAR_PICK = CAR_COUNT - ERA_COUNT;
int careerCarIndex(int era) { return CAR_PICK + (era < 0 ? 0 : era % ERA_COUNT); }
int eraFirstCar(int era) { return era * TEAMS_PER_ERA; }

// The real circuits (tools/build_tracks.py): the true layouts and heights, scaled to the official lap.
//  id             name                                   location                     start  hours  laps  street floodlit
const TrackDef TRACKS[] = {
    {"interlagos",  "Interlagos",                         "Sao Paulo, Brazil",           14.0f, 1.5f, 71, false, false},
    {"imola",       "Imola",                              "Emilia-Romagna, Italy",       14.0f, 1.5f, 63, false, false},
    {"monaco",      "Monaco",                             "Monte Carlo, Monaco",         15.0f, 1.8f, 78, true,  false},
    {"barcelona",   "Circuit de Barcelona-Catalunya",     "Montmelo, Spain",             15.0f, 1.5f, 66, false, false},
    {"montreal",    "Circuit Gilles Villeneuve",          "Montreal, Canada",            14.0f, 1.5f, 70, true,  false},
    {"magnycours",  "Magny-Cours",                        "Nievre, France",              14.0f, 1.5f, 72, false, false},
    {"hungaroring", "Hungaroring",                        "Budapest, Hungary",           15.0f, 1.6f, 70, false, false},
    {"spa",         "Spa-Francorchamps",                  "Ardennes, Belgium",           15.0f, 1.4f, 44, false, false},
    {"monza",       "Monza",                              "Lombardy, Italy",             15.0f, 1.3f, 53, false, false},
    {"suzuka",      "Suzuka",                             "Mie, Japan",                  14.0f, 1.5f, 53, false, false},
    {"bahrain",     "Bahrain International Circuit",      "Sakhir, Bahrain",             15.0f, 1.5f, 57, false, false},
    {"melbourne",   "Albert Park",                        "Melbourne, Australia",        15.0f, 1.5f, 58, true,  false},
    {"sepang",      "Sepang",                             "Selangor, Malaysia",          16.0f, 1.6f, 56, false, false},
    {"shanghai",    "Shanghai International Circuit",     "Shanghai, China",             15.0f, 1.6f, 56, false, false},
    {"istanbul",    "Istanbul Park",                      "Istanbul, Turkey",            14.0f, 1.5f, 58, false, false},
    {"singapore",   "Marina Bay",                         "Singapore",                   20.0f, 1.9f, 62, true,  true},
    {"abudhabi",    "Yas Marina",                         "Abu Dhabi, UAE",              17.0f, 1.6f, 58, false, true},
    {"sochi",       "Sochi Autodrom",                     "Sochi, Russia",               15.0f, 1.5f, 53, true,  false},
    {"baku",        "Baku City Circuit",                  "Baku, Azerbaijan",            15.0f, 1.6f, 51, true,  false},
    {"redbullring", "Red Bull Ring",                      "Spielberg, Austria",          15.0f, 1.3f, 71, false, false},
    {"silverstone", "Silverstone",                        "Northamptonshire, UK",        15.0f, 1.4f, 52, false, false},
    {"hockenheim",  "Hockenheim",                         "Baden-Wurttemberg, Germany",  14.0f, 1.5f, 67, false, false},
    {"cota",        "Circuit of the Americas",            "Austin, Texas, USA",          14.0f, 1.6f, 56, false, false},
    {"mexico",      "Autodromo Hermanos Rodriguez",       "Mexico City, Mexico",         14.0f, 1.6f, 71, false, false},
    {"jeddah",      "Jeddah Corniche Circuit",            "Jeddah, Saudi Arabia",        20.0f, 1.5f, 50, true,  true},
    {"miami",       "Miami International Autodrome",      "Miami, Florida, USA",         16.0f, 1.6f, 57, true,  false},
    {"zandvoort",   "Zandvoort",                          "North Holland, Netherlands",  15.0f, 1.6f, 72, false, false},
    {"lasvegas",    "Las Vegas Strip Circuit",            "Las Vegas, Nevada, USA",      22.0f, 1.4f, 50, true,  true},
    {"lusail",      "Lusail International Circuit",       "Lusail, Qatar",               19.0f, 1.5f, 57, false, true},
};
const int TRACK_COUNT = sizeof(TRACKS) / sizeof(TRACKS[0]);

const char* ERA_NAME[ERA_COUNT] = {"1990s V10 era", "2010 V8 era", "2016 hybrid era", "2025 ground-effect era"};
const char* ERA_SHORT[ERA_COUNT] = {"1998", "2010", "2016", "2025"};
const char* ERA_ID[ERA_COUNT] = {"90s", "v8", "hybrid", "ge"};

static int T(const char* id) { for (int i = 0; i < TRACK_COUNT; i++) if (!strcmp(TRACKS[i].id, id)) return i; return 0; }
// each era's 10-round calendar, from the circuits it really raced at
const int ERA_CAL[ERA_COUNT][ERA_ROUNDS] = {
    {T("interlagos"), T("imola"), T("barcelona"), T("monaco"), T("montreal"), T("magnycours"), T("hungaroring"), T("spa"), T("monza"), T("suzuka")},
    {T("bahrain"), T("melbourne"), T("sepang"), T("shanghai"), T("istanbul"), T("monaco"), T("spa"), T("monza"), T("singapore"), T("abudhabi")},
    {T("melbourne"), T("sochi"), T("baku"), T("redbullring"), T("silverstone"), T("hockenheim"), T("sepang"), T("cota"), T("mexico"), T("interlagos")},
    {T("bahrain"), T("jeddah"), T("suzuka"), T("miami"), T("imola"), T("monaco"), T("silverstone"), T("zandvoort"), T("lasvegas"), T("lusail")},
};
// start times: Bahrain ran in daylight in 2010 and under lights by 2025; the rest use the circuit's own
const float ERA_HOUR[ERA_COUNT][ERA_ROUNDS] = {
    {14, 14, 14, 14.5f, 13, 14, 14, 14, 14, 13},
    {15, 17, 16, 15, 15, 14, 14, 14, 20, 17},
    {16, 15, 17, 14, 13, 14, 15, 14, 13, 14},
    {18, 20, 14, 16, 15, 15, 15, 15, 22, 19},
};
bool trackInEra(int track, int era) { for (int k = 0; k < ERA_ROUNDS; k++) if (ERA_CAL[era][k] == track) return true; return false; }
int eraTrackCount(int) { return ERA_ROUNDS; }
int eraTrack(int era, int k) { return ERA_CAL[era][((k % ERA_ROUNDS) + ERA_ROUNDS) % ERA_ROUNDS]; }

// ---------------------------------------------------------------- loaders
bool Model::load(const char* path) {
    int size = 0;
    blob = sys::loadFile(path, &size);
    if (!blob) return false;
    const u8* p = (const u8*)blob;
    if (memcmp(p, "EMM1", 4) != 0) { unload(); return false; }
    p += 4;
    axleF = axleR = wheelR = 0; wheelY = 0.86f; ownWheels = false; flags = 0;
    uint32_t n; memcpy(&n, p, 4); p += 4;
    memcpy(scale, p, 12); p += 12;
    memcpy(offset, p, 12); p += 12;
    lodCount = (int)(n > 4 ? 4 : n);
    for (int i = 0; i < lodCount; i++) {
        uint32_t vc, ts, gl; memcpy(&vc, p, 4); memcpy(&ts, p + 4, 4); memcpy(&gl, p + 8, 4);
        if (i == 0) memcpy(&cabX, p + 12, 4);
        if (i == 1) memcpy(&panelY, p + 12, 4);
        if (i == 2) memcpy(&panelX, p + 12, 4);
        lod[i].glass = gl <= vc ? (int)gl : 0;
        p += 16;
        lod[i].tex.w = lod[i].tex.h = (int)ts;
        if (ts) { lod[i].tex.palette = (const u32*)p; p += 1024; lod[i].tex.pixels = p; p += ts * ts; }
        else { lod[i].tex.palette = nullptr; lod[i].tex.pixels = nullptr; }   // no texture: borrows another model's
        lod[i].verts = (const ModelVertex*)p; lod[i].count = (int)vc; p += vc * sizeof(ModelVertex);
    }
    // extras (Vita models): 'EMX1', f32 axleF, axleR, wheelY, wheelR, u32 flags (bit 0: own wheels)
    if (p + 24 <= (const u8*)blob + size && !memcmp(p, "EMX1", 4)) {
        memcpy(&axleF, p + 4, 4); memcpy(&axleR, p + 8, 4); memcpy(&wheelY, p + 12, 4); memcpy(&wheelR, p + 16, 4);
        uint32_t fl; memcpy(&fl, p + 20, 4); ownWheels = fl & 1; flags = fl;
    }
    gfx::flushCache(blob, size);
    return true;
}
void Model::unload() { if (blob) sys::freeFile(blob); blob = nullptr; lodCount = 0; }

bool TexFile::load(const char* path) {
    int size = 0;
    blob = sys::loadFile(path, &size);
    if (!blob) return false;
    const u8* p = (const u8*)blob;
    if (memcmp(p, "TEX1", 4) != 0) { unload(); return false; }
    uint32_t s; memcpy(&s, p + 4, 4);
    tex.w = tex.h = (int)s; tex.palette = (const u32*)(p + 16); tex.pixels = p + 16 + 1024;
    gfx::flushCache(blob, size);
    return true;
}
void TexFile::unload() { if (blob) sys::freeFile(blob); blob = nullptr; }

bool Font::load(const char* path) {
    int size = 0;
    blob = sys::loadFile(path, &size);
    if (!blob) return false;
    const u8* p = (const u8*)blob;
    bool v2 = memcmp(p, "FNT2", 4) == 0;
    if (!v2 && memcmp(p, "FNT1", 4) != 0) return false;
    uint32_t w, h, lh; memcpy(&w, p + 4, 4); memcpy(&h, p + 8, 4); memcpy(&lh, p + 12, 4);
    texScale = v2 ? (int)(lh >> 16) : 1;
    if (texScale < 1) texScale = 1;
    lineH = (int)(lh & 0xFFFF);
    tex.w = (int)w; tex.h = (int)h; tex.palette = (const u32*)(p + 16); tex.pixels = p + 16 + 1024;
    memcpy(g, p + 16 + 1024 + w * h, sizeof g);
    gfx::flushCache(blob, size);
    return true;
}

int Font::width(const char* s, float scale) const {
    float x = 0;
    for (; *s; s++) { int c = (unsigned char)*s; if (c < 32 || c > 126) c = '?'; x += g[c - 32].adv * scale; }
    return (int)(x + 0.5f);
}

void Font::draw(float x, float y, const char* s, u32 color, float scale) const {
    if (!blob) return;
    gfx::Sprite2D buf[96];
    int n = 0;
    for (; *s; s++) {
        int c = (unsigned char)*s; if (c < 32 || c > 126) c = '?';
        const Glyph& q = g[c - 32];
        if (c != ' ') {
            buf[n++] = gfx::Sprite2D{x + q.xoff * scale, y, q.w * scale, q.h * scale, u(q.x), v(q.y),
                                     u(q.x + q.w), v(q.y + q.h), color};
            if (n == 96) { gfx::bindTexture(&tex); gfx::drawSprites2D(buf, n); n = 0; }
        }
        x += q.adv * scale;
    }
    gfx::bindTexture(&tex);
    gfx::drawSprites2D(buf, n);   // the whole string in one draw call
}
