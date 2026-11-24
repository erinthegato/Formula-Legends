#include "trophies.h"
#include "render.h"
#include <cstdio>

Trophies trophies;

const TrophyDef TROPHIES[TR_COUNT] = {
    {"Green Flag",        "Finish a race",                                   TIER_BRONZE},
    {"On the Podium",     "Finish in the top 3 of your class",               TIER_BRONZE},
    {"Class Winner",      "Win your class in a race",                        TIER_SILVER},
    {"Overall Victory",   "Take the chequered flag first overall",           TIER_GOLD},
    {"Comeback Drive",    "Gain 5 places from where you started",            TIER_BRONZE},
    {"Clean Sheet",       "Finish a race of 20+ min with no penalty or damage", TIER_SILVER},
    {"Rain Master",       "Win your class with rain falling at the flag",    TIER_SILVER},
    {"Through the Night", "Finish a race that ran through the night",        TIER_BRONZE},
    {"Quick Hands",       "Manual pit stop with tyres in under 9 seconds",   TIER_BRONZE},
    {"Perfect Stop",      "Hit every prompt PERFECT in a manual pit stop",   TIER_SILVER},
    {"Ghost Buster",      "Set a new time trial record",                     TIER_BRONZE},
    {"Double Stint",      "Drive 2 hours of race time without a break",      TIER_SILVER},
    {"Twice Around",      "Finish the 24 Hours of the Sablons",              TIER_GOLD},
    {"Sablons Legend",    "Win your class at the 24 Hours of the Sablons",   TIER_GOLD},
    {"World Champion",    "Win the championship in your class",              TIER_GOLD},
    {"Formula Legend",  "Win every other trophy",                          TIER_PLATINUM},
};

void Trophies::award(int id) {
    if (id < 0 || id >= TR_COUNT || has(id)) return;
    won |= 1u << id;
    if (queued < 8) queue[queued++] = id;
    bool all = true;
    for (int i = 0; i < TR_COUNT; i++) if (i != TR_PLATINUM && !has(i)) all = false;
    if (all && !has(TR_PLATINUM)) { won |= 1u << TR_PLATINUM; if (queued < 8) queue[queued++] = TR_PLATINUM; }
    save();
}

void Trophies::load() {
    FILE* f = fopen("trophies.sav", "rb");
    if (!f) return;
    unsigned w = 0;
    if (fscanf(f, "EMT1 %x", &w) == 1) won = w & ((1u << TR_COUNT) - 1);
    fclose(f);
}

void Trophies::save() const {
    FILE* f = fopen("trophies.sav", "wb");
    if (!f) return;
    fprintf(f, "EMT1 %x\n", won);
    fclose(f);
}

void Trophies::update(float dt) {
    if (!queued) return;
    showT += dt;
    if (showT > 4.0f) {               // next one
        for (int i = 1; i < queued; i++) queue[i - 1] = queue[i];
        queued--; showT = 0;
    }
}

static const u32 TIER_COL[4] = {RGBA(205, 127, 50), RGBA(200, 205, 215), RGBA(250, 205, 60), RGBA(150, 210, 255)};
static const char* TIER_NAME[4] = {"BRONZE", "SILVER", "GOLD", "PLATINUM"};

void Trophies::draw(Renderer& R) const {
    if (!queued) return;
    const TrophyDef& t = TROPHIES[queue[0]];
    // slides down, waits, slides back up
    float k = showT < 0.35f ? showT / 0.35f : (showT > 3.6f ? (4.0f - showT) / 0.4f : 1);
    if (k < 0) k = 0;
    float w = 220, h = 34, x = SCREEN_W / 2 - w / 2, y = -h + (h + 40) * k;
    gfx::setDepthTest(false); gfx::setBlend(true); gfx::setFog(false, 0, 1, 0);
    gfx::bindTexture(nullptr);
    gfx::drawFill2D(x, y, w, h, RGBA(8, 12, 20, 235));
    gfx::drawFill2D(x, y, 4, h, TIER_COL[t.tier]);
    gfx::drawFill2D(x, y, w, 1, TIER_COL[t.tier]); gfx::drawFill2D(x, y + h - 1, w, 1, TIER_COL[t.tier]);
    gfx::drawFill2D(x + w - 1, y, 1, h, TIER_COL[t.tier]);
    gfx::drawFill2D(x + 10, y + 7, 20, 20, TIER_COL[t.tier]);
    gfx::drawFill2D(x + 15, y + 12, 10, 10, RGBA(8, 12, 20, 235));
    char b[64]; snprintf(b, sizeof b, "TROPHY - %s", TIER_NAME[t.tier]);
    R.font.draw(x + 38, y + 3, b, TIER_COL[t.tier], 0.62f);
    R.font.draw(x + 38, y + 15, t.name, RGBA(255, 255, 255), 0.85f);
    gfx::setBlend(false); gfx::setDepthTest(true);
}
