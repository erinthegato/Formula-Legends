// Trophies: in-game achievements with a pop-up when one is won and a list on the title menu
// (TROPHIES). Saved to trophies.sav. (Real PS Vita trophies need Sony-signed trophy packs,
// which homebrew can't make, so the game keeps its own.)
#pragma once

enum TrophyTier { TIER_BRONZE = 0, TIER_SILVER, TIER_GOLD, TIER_PLATINUM };
enum TrophyId {
    TR_FIRST_RACE = 0, TR_PODIUM, TR_CLASS_WIN, TR_OVERALL_WIN, TR_COMEBACK, TR_CLEAN, TR_RAIN_WIN, TR_NIGHT,
    TR_QUICK_STOP, TR_PERFECT_STOP, TR_TT_RECORD, TR_DOUBLE_STINT, TR_LE_MANS, TR_LE_MANS_WIN, TR_CHAMPION,
    TR_PLATINUM, TR_COUNT
};
struct TrophyDef { const char* name; const char* how; int tier; };
extern const TrophyDef TROPHIES[TR_COUNT];

struct Trophies {
    unsigned won = 0;                 // bit per trophy
    int queue[8] = {}; int queued = 0; float showT = 0;   // pop-ups waiting to be shown
    bool has(int id) const { return (won >> id) & 1u; }
    int count() const { int n = 0; for (int i = 0; i < TR_COUNT; i++) n += has(i); return n; }
    void award(int id);               // marks it, queues the pop-up, saves; the platinum follows the rest
    void load();
    void save() const;
    void update(float dt);
    void draw(struct Renderer& R) const;   // the pop-up (2D, top of the screen)
};
extern Trophies trophies;
