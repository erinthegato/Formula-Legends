#include "sound.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

// The recordings are steady high-rev loops (22050 Hz mono). Like the Unity game, each is treated
// as being at 72 % of the way from idle to the rev limit, and played faster or slower to follow the revs.
namespace {
// data: the file as loaded (16-bit samples, or an 'ADP1' file: then adp points at its IMA ADPCM codes,
// kept packed and decoded as it plays -- a quarter of the memory). step: source samples per output sample.
// ('ULW1': 8-bit mu-law, for voice and sharp sounds -- half the memory)
struct Clip { const char* name = nullptr; int16_t* data = nullptr; int len = 0; const uint8_t* adp = nullptr, *ulaw = nullptr; float step = 0.5f; };
int16_t ULAW[256];                      // mu-law code -> sample (built once)
void ulawTable() {
    for (int i = 0; i < 256; i++) {
        int c = ~i & 0xFF, e = (c >> 4) & 7, m = c & 15;
        int v = (((m << 3) + 0x84) << e) - 0x84;
        ULAW[i] = (int16_t)((c & 0x80) ? -v : v);
    }
}
const int MAX_CLIPS = 16;   // engine loops (the 2025 grid has one per team)
Clip clips[MAX_CLIPS];
int clipCount = 0;

struct Voice {
    volatile int clip = -1;
    volatile float rate = 1;      // source samples per output sample
    volatile float vol = 0, pan = 0;
    volatile bool muffled = false;
    float pos = 0, curVol = 0, lp = 0;
    volatile int clipOff = -1;    // the off-throttle (overrun) loop, if the car has one
    volatile float offMix = 0;    // 0 on the throttle .. 1 off it
    float posOff = 0, curOff = 0;
};
Voice voices[4];
volatile float master = 0.9f;
volatile float volMusic = 0.7f, volEngine = 0.8f, volSfx = 0.8f, volRadio = 0.9f;

// ---------------- streamed menu music (IMA ADPCM, 22050 Hz stereo, see tools/music/adpcm_enc.c)
const int RING = 65536;                 // frames (about 3 s)
int16_t ring[RING * 2];
volatile int ringW = 0, ringR = 0;      // writer: main thread, reader: audio thread
float ringFrac = 0;
volatile float musicGain = 0, musicTarget = 0;
#define MUSIC_VOL (volMusic * 1.07f)
// PA speakers: the music (mono, speaker-tone) goes into a 1 s history; each speaker reads it back delayed
const int HIST = 65536;              // 1.48 s at 44100 Hz
float hist[HIST]; int histW = 0;
float hpX = 0, hpY = 0, lpY = 0;     // speaker tone filters
struct Tap { volatile float gain = 0, pan = 0, delay = 0.01f, dull = 0; float g = 0, d = 0.01f, p = 0, lp = 0; };
Tap taps[4];
std::vector<std::string> mFiles, mNames;
std::vector<int> mEra;                  // per track: the era it belongs to (-1: every era)
int mCurEra = -1;                       // the era picked in the menus (-1: not known yet)
std::vector<int> mOrder; int mPos = -1, mLast = -1;
FILE* mFile = nullptr; uint32_t mFrames = 0, mDone = 0;
int mCh = 2;                             // 1 = mono file (playlist), 2 = stereo (podium / intro)
int mState[2][2];                       // predictor, index per channel
double mStart = 0;
bool mOn = false, mLoaded = false;
std::string mForce;
bool mOnce = false;                     // the forced piece plays once (intro)                     // play this file (podium) instead of the shuffled playlist
volatile bool mDirect = false;          // straight to the speakers / headphones, no PA effect

// ---------------- one-shot sounds (pit crew, thunder, radio) and the rain loop, DATA/sfx/*.raw
const int MAX_SFX = 80;
Clip sfx[MAX_SFX]; int sfxCount = 0;
struct Shot { volatile int clip = -1; volatile float vol = 0, pan = 0; volatile bool loop = false; float pos = 0, g = 0;
              int decAt = -1, decClip = -1, dec[2] = {0, 0}, cur = 0; };   // ADPCM: decoded up to sample decAt (audio thread)
Shot shots[7];                          // 0..3 effects, 4 radio, 5 rain loop, 6 crowd loop
// freeing the effects while the audio thread mixes: it skips them while sfxHold is set, and
// mixBusy says it is in the middle of reading them
volatile bool sfxHold = false;
volatile int mixBusy = 0;

const int STEP[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
const int IDXT[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

inline int decodeNibble(int* st, int code) {
    int step = STEP[st[1]], vd = step >> 3;
    if (code & 4) vd += step;
    if (code & 2) vd += step >> 1;
    if (code & 1) vd += step >> 2;
    st[0] += (code & 8) ? -vd : vd;
    if (st[0] > 32767) st[0] = 32767;
    if (st[0] < -32768) st[0] = -32768;
    st[1] += IDXT[code]; if (st[1] < 0) st[1] = 0; if (st[1] > 88) st[1] = 88;
    return st[0];
}

void loadPlaylist() {
    mLoaded = true;
    int size = 0;
    char* p = (char*)sys::loadFile("DATA/music/playlist.txt", &size);
    if (!p) return;
    std::string all(p, size);
    sys::freeFile(p);
    size_t a = 0;
    while (a < all.size()) {
        size_t e = all.find('\n', a); if (e == std::string::npos) e = all.size();
        std::string line = all.substr(a, e - a);
        size_t t = line.find('\t');
        // file <tab> name [<tab> era: 90s, v8, hybrid or ge]: a song with an era plays only in that era
        if (t != std::string::npos) {
            std::string rest = line.substr(t + 1), era;
            size_t t2 = rest.find('\t');
            if (t2 != std::string::npos) { era = rest.substr(t2 + 1); rest = rest.substr(0, t2); }
            while (!era.empty() && (era.back() == '\r' || era.back() == ' ')) era.pop_back();
            while (!rest.empty() && rest.back() == '\r') rest.pop_back();
            static const char* ERA_TAG[4] = {"90s", "v8", "hybrid", "ge"};
            int e = -1; for (int k = 0; k < 4; k++) if (era == ERA_TAG[k]) e = k;
            mFiles.push_back(line.substr(0, t)); mNames.push_back(rest); mEra.push_back(e);
        }
        a = e + 1;
    }
    srand((unsigned)(sys::timeSec() * 1000003.0));
}

// next track: shuffled order, every track once before any repeats, never the same one twice in a row
void openNext() {
    if (mFile) { fclose(mFile); mFile = nullptr; }
    int n = (int)mFiles.size();
    if (!n) return;
    if (!mForce.empty()) {
        std::string path = "DATA/music/" + mForce;
        mFile = fopen(path.c_str(), "rb");
        uint32_t h[8];
        if (mFile && (fread(h, 4, 8, mFile) != 8 || h[0] != 0x31554D45u)) { fclose(mFile); mFile = nullptr; }
        if (mFile && h[2] != 1 && h[2] != 2) { fclose(mFile); mFile = nullptr; }
        if (mFile) { mFrames = h[3]; mCh = (int)h[2]; mDone = 0; mStart = sys::timeSec(); }
        return;
    }
    if (mOrder.empty() || mPos + 1 >= (int)mOrder.size()) {
        // the songs for every era, plus those of the era picked in the menus
        mOrder.clear();
        for (int i = 0; i < n; i++) if (mEra[i] < 0 || mEra[i] == mCurEra) mOrder.push_back(i);
        if (mOrder.empty()) for (int i = 0; i < n; i++) mOrder.push_back(i);
        int m = (int)mOrder.size();
        for (int i = m - 1; i > 0; i--) { int j = rand() % (i + 1); int t = mOrder[i]; mOrder[i] = mOrder[j]; mOrder[j] = t; }
        if (m > 1 && mOrder[0] == mLast) { int t = mOrder[0]; mOrder[0] = mOrder[1]; mOrder[1] = t; }
        mPos = 0;
    } else mPos++;
    int k = mOrder[mPos];
    mLast = k;
    std::string path = "DATA/music/" + mFiles[k];
    mFile = fopen(path.c_str(), "rb");
    if (!mFile) return;
    uint32_t h[8];
    if (fread(h, 4, 8, mFile) != 8 || h[0] != 0x31554D45u || (h[2] != 1 && h[2] != 2)) { fclose(mFile); mFile = nullptr; return; }
    mFrames = h[3]; mCh = (int)h[2]; mDone = 0;
    mStart = sys::timeSec();
}


int sfxFor(const char* name) {
    for (int i = 0; i < sfxCount; i++) if (!strcmp(sfx[i].name, name)) return i;
    if (sfxCount >= MAX_SFX) return -1;
    char path[96];
    snprintf(path, sizeof path, "DATA/sfx/%s.raw", name);
    int size = 0;
    void* p = sys::loadFile(path, &size);
    if (!p) return -1;
    Clip& c = sfx[sfxCount];
    c.name = name; c.data = (int16_t*)p; c.len = size / 2; c.adp = c.ulaw = nullptr; c.step = 0.5f;
    bool adp = size >= 12 && !memcmp(p, "ADP1", 4), mu = size >= 12 && !memcmp(p, "ULW1", 4);
    if (adp || mu) {
        uint32_t rate, n; memcpy(&rate, (char*)p + 4, 4); memcpy(&n, (char*)p + 8, 4);
        if ((int)(adp ? (n + 1) / 2 : n) > size - 12 || rate < 4000 || rate > 44100) { sys::freeFile(p); return -1; }
        (adp ? c.adp : c.ulaw) = (const uint8_t*)p + 12; c.len = (int)n; c.step = rate / 44100.0f;
        if (mu && ULAW[0] == 0) ulawTable();
    }
    return sfxCount++;
}

int clipFor(const char* name) {
    for (int i = 0; i < clipCount; i++) if (!strcmp(clips[i].name, name)) return i;
    if (clipCount >= MAX_CLIPS) return -1;
    char path[96];
    snprintf(path, sizeof path, "DATA/audio/%s.raw", name);
    int size = 0;
    void* p = sys::loadFile(path, &size);
    if (!p) return -1;
    clips[clipCount].name = name; clips[clipCount].data = (int16_t*)p; clips[clipCount].len = size / 2;
    return clipCount++;
}

void mix(int16_t* out, int frames) {
    static float buf[2048 * 2];
    if (frames > 2048) frames = 2048;
    memset(buf, 0, frames * 2 * sizeof(float));
    for (auto& v : voices) {
        int ci = v.clip;
        if (ci < 0 || ci >= clipCount) continue;
        const Clip& c = clips[ci];
        float rate = v.rate, target = v.vol, pan = v.pan;
        if (target < 0.0005f && v.curVol < 0.0005f) { v.curVol = 0; continue; }   // silent: skip the work
        bool muff = v.muffled;
        float eng = volEngine * 1.25f;
        float gl = 1 - (pan > 0 ? pan : 0), gr = 1 + (pan < 0 ? pan : 0);
        for (int i = 0; i < frames; i++) {
            v.curVol += (target - v.curVol) * 0.002f;
            int a = (int)v.pos; float t = v.pos - a;
            int b = a + 1; if (b >= c.len) b = 0;
            float s = (c.data[a] * (1 - t) + c.data[b] * t) * (1.0f / 32768.0f);
            int co = v.clipOff;
            if (co >= 0 && co < clipCount) {
                // the overrun layer, crossfaded in as the throttle closes (equal power)
                v.curOff += (v.offMix - v.curOff) * 0.0015f;
                const Clip& o = clips[co];
                int a2 = (int)v.posOff; float t2 = v.posOff - a2;
                int b2 = a2 + 1; if (b2 >= o.len) b2 = 0;
                float s2 = (o.data[a2] * (1 - t2) + o.data[b2] * t2) * (1.0f / 32768.0f);
                float m = v.curOff;
                s = s * sqrtf(1 - m) + s2 * sqrtf(m);
                v.posOff += rate;
                if (v.posOff >= o.len) v.posOff -= o.len;
            }
            // a muffled engine (heard from inside the cockpit / far away) loses its fizz
            if (muff) { v.lp += (s - v.lp) * 0.25f; s = v.lp; }
            s *= v.curVol * eng;
            buf[i * 2] += s * gl; buf[i * 2 + 1] += s * gr;
            v.pos += rate;
            if (v.pos >= c.len) v.pos -= c.len;
        }
    }
    // one-shots and loops (DATA/sfx: ADP1 files, packed in memory -- radio 11025 Hz, the rest 22050 Hz)
    mixBusy = 1;
    for (int si = 0; si < 7 && !sfxHold; si++) {
        Shot& sh = shots[si];
        int ci = sh.clip;
        if (ci < 0 || ci >= sfxCount) continue;
        const Clip& c = sfx[ci];
        float target = sh.vol, pan = sh.pan;
        float gl = sqrtf(0.5f * (1 - pan)), gr = sqrtf(0.5f * (1 + pan));
        float shotVol = (si == 4 ? volRadio : volSfx) * 1.2f;
        for (int i = 0; i < frames; i++) {
            sh.g += (target - sh.g) * 0.003f;
            int a = (int)sh.pos;
            if (a >= c.len) {
                if (sh.loop) { sh.pos -= c.len; a = (int)sh.pos; }
                else { sh.clip = -1; break; }
            }
            int smp;
            if (c.adp) {
                // packed: decode forward to sample a (from the start again after a loop or a new clip)
                if (sh.decClip != ci || a < sh.decAt) { sh.decClip = ci; sh.decAt = -1; sh.dec[0] = sh.dec[1] = 0; sh.cur = 0; }
                while (sh.decAt < a) { sh.decAt++; sh.cur = decodeNibble(sh.dec, (c.adp[sh.decAt >> 1] >> ((sh.decAt & 1) * 4)) & 15); }
                smp = sh.cur;
            } else smp = c.ulaw ? ULAW[c.ulaw[a]] : c.data[a];
            float x = smp * (1.0f / 32768.0f) * sh.g * shotVol;
            buf[i * 2] += x * gl; buf[i * 2 + 1] += x * gr;
            sh.pos += c.step;
        }
    }
    // menu / session music: 22050 Hz ring -> 44100 Hz, made mono and speaker-toned, then played back
    // through up to 4 PA speakers, each with its own delay (distance), level and direction
    {
        float tgt = musicTarget;
        bool live = !(musicGain < 0.001f && tgt == 0);
        for (int i = 0; i < frames && live; i++) {
            musicGain += (tgt - musicGain) * 0.00005f;
            float mono = 0;
            int r = ringR;
            int avail = (ringW - r + RING) % RING;
            if (avail >= 2) {
                int r2 = (r + 1) % RING;
                float l = ring[r * 2] * (1 - ringFrac) + ring[r2 * 2] * ringFrac;
                float rr = ring[r * 2 + 1] * (1 - ringFrac) + ring[r2 * 2 + 1] * ringFrac;
                mono = (l + rr) * 0.5f * (1.0f / 32768.0f);
                ringFrac += 0.5f;
                if (ringFrac >= 1) { ringFrac -= 1; ringR = r2; }
            }
            // speaker tone: high-pass ~170 Hz, low-pass ~5 kHz
            hpY = 0.976f * (hpY + mono - hpX); hpX = mono;
            lpY += (hpY - lpY) * 0.51f;
            if (mDirect) {
                // podium: the music straight to the speakers, in stereo
                int r0 = (ringR + RING - 1) % RING;
                buf[i * 2] += ring[r0 * 2] * (1.0f / 32768.0f) * musicGain * MUSIC_VOL;
                buf[i * 2 + 1] += ring[r0 * 2 + 1] * (1.0f / 32768.0f) * musicGain * MUSIC_VOL;
            }
            hist[histW] = mDirect ? 0 : lpY * musicGain * MUSIC_VOL * 1.6f;
            for (auto& t : taps) {
                t.g += (t.gain - t.g) * 0.0003f;
                if (t.g < 0.0005f && t.gain == 0) continue;
                t.d += clampf(t.delay - t.d, -0.00002f, 0.00002f);    // distance changes glide (a little Doppler)
                t.p += (t.pan - t.p) * 0.0003f;
                float ds = t.d * 44100.0f;
                float pos = (float)histW - ds;
                while (pos < 0) pos += HIST;
                int a = (int)pos; float f = pos - a; int b = (a + 1) % HIST;
                float x = hist[a] * (1 - f) + hist[b] * f;
                float k = 0.9f - 0.8f * t.dull;                   // far / muffled speakers lose more treble
                t.lp += (x - t.lp) * k;
                float gl = sqrtf(0.5f * (1 - t.p)), gr = sqrtf(0.5f * (1 + t.p));
                buf[i * 2] += t.lp * t.g * gl; buf[i * 2 + 1] += t.lp * t.g * gr;
            }
            histW = (histW + 1) % HIST;
        }
    }
    mixBusy = 0;
    float m = master;
    for (int i = 0; i < frames * 2; i++) {
        float x = buf[i] * m;
        // soft limiter
        x = x / (1 + fabsf(x) * 0.6f);
        out[i] = (int16_t)(clampf(x, -1, 1) * 32000);
    }
}
}

namespace sound {
void init() { audio::start(mix); }
void shutdown() {
    audio::stop();
    for (int i = 0; i < clipCount; i++) sys::freeFile(clips[i].data);
    clipCount = 0;
    for (int i = 0; i < sfxCount; i++) sys::freeFile(sfx[i].data);
    sfxCount = 0;
}
void setVoice(int i, const CarDef* car, float rpm, float volume, float pan, bool muffled, float throttle) {
    if (i < 0 || i > 3) return;
    Voice& v = voices[i];
    if (!car) { v.vol = 0; return; }
    int c = clipFor(car->sound);
    if (c != v.clip) { v.pos = 0; v.clip = c; }
    // "<sound>_off": the overrun loop (kept names: the clip table holds the pointer)
    {
        static char offNames[8][40]; static const char* offOf[8]; static int nOff = 0;
        const char* key = nullptr;
        for (int k = 0; k < nOff; k++) if (offOf[k] == car->sound) key = offNames[k];
        if (!key && nOff < 8) { offOf[nOff] = car->sound; snprintf(offNames[nOff], 40, "%s_off", car->sound); key = offNames[nOff++]; }
        static const char* missing[8]; static int nMiss = 0;
        bool known = false; for (int k = 0; k < nMiss; k++) if (missing[k] == key) known = true;
        int co = -1;
        if (key && !known) { co = clipFor(key); if (co < 0 && nMiss < 8) missing[nMiss++] = key; }
        if (co != v.clipOff) { v.posOff = 0; v.clipOff = co; }
        v.offMix = clampf(1 - throttle * 1.6f, 0, 1);
    }
    float ref = car->idleRpm + (car->maxRpm - car->idleRpm) * 0.72f;
    v.rate = clampf(rpm / ref, 0.3f, 1.6f) * (22050.0f / 44100.0f);
    v.vol = volume; v.pan = clampf(pan, -1, 1); v.muffled = muffled;
}
void silence() { for (auto& v : voices) v.vol = 0; }

void musicEnable(bool on) {
    if (!mLoaded) loadPlaylist();
    if (on && !mOn) {
        // back on: empty the buffer and start a fresh random track
        ringW = ringR;
        openNext();
        musicGain = 0;
    }
    mOn = on;
    musicTarget = on ? 1.0f : 0.0f;
}

void update() {
    if (!mOn || mFiles.empty()) return;
    // keep ~3 s decoded ahead; each block is 2048 frames
    static uint8_t blk[8 + 2048];
    for (int guard = 0; guard < 8; guard++) {
        int free = RING - 1 - (ringW - ringR + RING) % RING;
        if (free < 2048) break;
        size_t bsz = (size_t)mCh * (4 + 1024);
        if (!mFile || mDone >= mFrames || fread(blk, 1, bsz, mFile) != bsz) {
            if (mOnce && !mForce.empty()) { if (mFile) { fclose(mFile); mFile = nullptr; } mDone = mFrames; return; }
            openNext(); if (!mFile) return; continue;
        }
        int ch = mCh;
        for (int c = 0; c < ch; c++) { int16_t p; memcpy(&p, blk + c * 4, 2); mState[c][0] = p; mState[c][1] = blk[c * 4 + 2] > 88 ? 88 : blk[c * 4 + 2]; }
        int n = (int)(mFrames - mDone < 2048 ? mFrames - mDone : 2048);
        int w = ringW;
        const uint8_t* data = blk + ch * 4;
        for (int i = 0; i < n; i++) {
            for (int c = 0; c < ch; c++) {
                uint8_t b = data[c * 1024 + i / 2];
                ring[w * 2 + c] = (int16_t)decodeNibble(mState[c], (i & 1) ? (b >> 4) : (b & 15));
            }
            if (ch == 1) ring[w * 2 + 1] = ring[w * 2];      // mono: same sample both sides
            w = (w + 1) % RING;
        }
        ringW = w;
        mDone += n;
    }
}

const char* nowPlaying() {
    if (!mFile && mLast < 0) return "";
    return mLast >= 0 && mLast < (int)mNames.size() ? mNames[mLast].c_str() : "";
}
float trackTime() { return (float)(sys::timeSec() - mStart); }

void setSpeaker(int i, float gain, float pan, float delaySec, float dull) {
    if (i < 0 || i > 3) return;
    taps[i].gain = clampf(gain, 0, 1.2f); taps[i].pan = clampf(pan, -1, 1);
    taps[i].delay = clampf(delaySec, 0.001f, 1.4f); taps[i].dull = clampf(dull, 0, 1);
}

float musicLevel() { return musicGain; }

void paddockSpeakers() {
    // standing in the paddock: a speaker close on the left, one further away on the right,
    // and the main straight's PA echoing back from across the circuit
    setSpeaker(0, 0.85f, -0.55f, 12.0f / 343, 0.0f);
    setSpeaker(1, 0.55f, 0.7f, 30.0f / 343, 0.15f);
    setSpeaker(2, 0.28f, 0.1f, 95.0f / 343, 0.45f);
    setSpeaker(3, 0.14f, -0.3f, 190.0f / 343, 0.7f);
}
void setMaster(float m) { master = m; }
void setVolumes(float music, float engine, float sfx, float radio) { volMusic = music; volEngine = engine; volSfx = sfx; volRadio = radio; }

void play(const char* name, float vol, float pan) {
    int c = sfxFor(name);
    if (c < 0) return;
    // the free slot, or the one closest to its end
    int best = 0; float most = -1;
    for (int i = 0; i < 4; i++) {
        if (shots[i].clip < 0) { best = i; break; }
        float done = shots[i].pos / (float)sfx[shots[i].clip].len;
        if (done > most) { most = done; best = i; }
    }
    Shot& s = shots[best];
    s.clip = -1; s.pos = 0; s.g = vol; s.vol = vol; s.pan = clampf(pan, -1, 1); s.loop = false;
    s.clip = c;
}
void radio(const char* name) {
    char full[48]; snprintf(full, sizeof full, "radio_%s", name);
    static char keep[48][48]; static int nk = 0;
    // the clip table keeps the name pointer: hold the strings
    const char* key = nullptr;
    for (int i = 0; i < nk; i++) if (!strcmp(keep[i], full)) key = keep[i];
    if (!key && nk < 48) { strcpy(keep[nk], full); key = keep[nk++]; }
    if (!key) return;
    int c = sfxFor(key);
    if (c < 0) return;
    Shot& s = shots[4];
    s.clip = -1; s.pos = 0; s.g = 1; s.vol = 1.0f; s.pan = 0; s.loop = false;
    s.clip = c;
}
bool radioBusy() { return shots[4].clip >= 0; }
void loop(int slot, const char* name, float vol) {
    Shot& s = shots[slot == 0 ? 5 : 6];
    if (!name || vol <= 0.001f) { s.vol = 0; if (s.g < 0.001f) s.clip = -1; return; }
    int c = sfxFor(name);
    if (c < 0) return;
    if (s.clip != c) { s.clip = -1; s.pos = 0; s.g = 0; s.loop = true; s.clip = c; }
    s.vol = vol; s.pan = 0;
}
void musicDirect(bool on) { mDirect = on; }
void musicFile(const char* file, bool once) {
    mOnce = once;
    // a fixed piece (intro / podium) instead of the playlist; nullptr = back to the playlist
    if (!mLoaded) loadPlaylist();
    mForce = file ? file : "";
    ringW = ringR;
    mOn = false;
    musicEnable(true);
    if (file) musicGain = 1;        // no fade-in: the intro is timed to the music
}
float musicPos() {
    int avail = (ringW - ringR + RING) % RING;
    return ((float)mDone - avail) / 22050.0f;
}
void musicEra(int era) {
    if (era == mCurEra) return;
    mCurEra = era;
    mOrder.clear(); mPos = -1;          // a new shuffle with this era's songs from the next song on
}
void musicSkip(int dir) {
    if (!mOn || mFiles.empty() || !mForce.empty()) return;
    if (dir < 0) mPos = mPos >= 2 ? mPos - 2 : -1;     // back one (openNext steps forward again)
    if (dir < 0 && mPos < 0) { mPos = (int)mOrder.size() - 2; if (mPos < -1) mPos = -1; }
    ringW = ringR;
    openNext();
}
void stopEffects() { for (auto& s : shots) { s.vol = 0; s.clip = -1; } }
int freeEffects() {
    sfxHold = true;
    double t0 = sys::timeSec();
    while (mixBusy && sys::timeSec() - t0 < 0.25) {}        // let a mix in progress finish (one buffer, ~10 ms)
    int kb = 0;
    for (auto& s : shots) { s.vol = 0; s.g = 0; s.clip = -1; }
    for (int i = 0; i < sfxCount; i++) { kb += (sfx[i].adp ? sfx[i].len / 2 : (sfx[i].ulaw ? sfx[i].len : sfx[i].len * 2)) / 1024; sys::freeFile(sfx[i].data); sfx[i] = Clip(); }
    for (auto& s : shots) { s.decClip = -1; s.decAt = -1; }
    sfxCount = 0;
    sfxHold = false;
    return kb;
}
void musicPodium(bool on) {
    if (!mLoaded) loadPlaylist();
    mForce = on ? "podium.emu" : "";
    mOnce = false;
    mDirect = on;
    ringW = ringR;
    mOn = false;
    musicEnable(true);
}
}
