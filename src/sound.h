// Engine sound: the Unity project's recorded engine loops, pitched with the revs.
#pragma once
#include "platform.h"
#include "data.h"

namespace sound {
void init();
void shutdown();
// voice 0 = the player's car, 1..3 = the nearest opponents
void setVoice(int voice, const CarDef* car, float rpm, float volume, float pan, bool muffled, float throttle = 1);   // engines with a "<sound>_off" loop blend it in off the throttle
void silence();
void setMaster(float v);
void setVolumes(float music, float engine, float sfx, float radio);   // each 0..1
// Menu music: the tracks in DATA/music (listed in playlist.txt) in shuffled order.
void musicEnable(bool on);     // fades in / out; a new random track starts each time it comes back on
void update();                 // call once per frame: reads ahead from the memory stick
const char* nowPlaying();      // "Artist - Title" of the current track, or ""
float trackTime();             // seconds since the current track started
// The music plays through PA speakers: up to 4 at once. gain 0..1, pan -1 (left) .. 1 (right),
// delay = distance / speed of sound, dull = 0..1 extra muffling (far away / heard from a cockpit).
void setSpeaker(int i, float gain, float pan, float delaySec, float dull);
void paddockSpeakers();        // the fixed set used in the menus
float musicLevel();            // current music fade level 0..1 (tests)
// Effects from DATA/sfx: one-shots (pan -1..1), the engineer's radio, and two loops (0 rain, 1 crowd)
void play(const char* name, float vol, float pan);
void radio(const char* name);  // e.g. "box" plays radio_box.raw (a new call cuts the last one off)
bool radioBusy();
void loop(int slot, const char* name, float vol);
void stopEffects();
int freeEffects();             // unload every cached effect (they load again when next played); returns KB freed
void musicDirect(bool on);     // true: plain stereo (menus); false: through the PA speakers (trackside)
void musicSkip(int dir);       // +1 next song, -1 the one before
void musicEra(int era);        // the era picked in the menus: its own songs join the shuffle (playlist.txt 3rd column)
void musicPodium(bool on);
bool musicPackMissing();      // the music pack (Vita: ux0:data/FormulaLegends/DATA/music) isn't installed
void musicFile(const char* file, bool once = false);   // play DATA/music/<file> instead of the playlist; nullptr = playlist
float musicPos();              // seconds of the current piece actually heard     // the podium music, straight (not through the PA); off = back to the playlist
}
