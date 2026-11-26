// Screens and flow: title -> track -> car -> race settings -> race -> results -> podium,
// plus the championship hub, the team screen at pit stops, save slots and the highlights reel.
#pragma once
#include "trophies.h"
#include "render.h"
#include "sound.h"
#include "champ.h"
#include "career.h"
#include "settings.h"

struct Panel;

// title menu: item ids (0 quick race .. 5 credits, 6 time trial) in the order they are shown
enum { CR_HELP = 100, CR_PADSTEER, CR_TILT, CR_TILTDIR, CR_RESET, CR_BACK };   // Controls page rows (0..ACT_COUNT-1: buttons)
// settings categories
enum { OC_SOUND = 0, OC_DISPLAY, OC_CAMERA, OC_CONTROLS, OC_REALISM, OC_BACK, OPT_CATS };
static const char* const OPT_CAT_NAME[OPT_CATS] = {"Sound", "Display and performance", "Camera and cockpit", "Controls", "Car realism", "Back"};
//   // Controls page rows (0..ACT_COUNT-1: buttons)
// the main menu (after the era is picked at headquarters): ids 8 showroom, 9 back to headquarters
static const int TITLE_N = 10;
static const int TITLE_ORDER[TITLE_N] = {0, 6, 1, 2, 8, 3, 7, 4, 5, 9};   // 7: trophies
// pause menu: item ids in the order shown (7 replay, 8 photo mode)
static const int PAUSE_ORDER[9] = {0, 1, 2, 7, 8, 3, 4, 5, 6};
const char* eraSave(const char* kind, int era);   // "career_2016.sav": one save per era

enum Screen { SCR_TITLE, SCR_TRACK, SCR_CAR, SCR_SETTINGS, SCR_HELP, SCR_RACE, SCR_PAUSE, SCR_RESULTS,
              SCR_TEAM, SCR_PODIUM, SCR_CHAMP, SCR_LOAD, SCR_REEL, SCR_OPTIONS, SCR_CONTROLS, SCR_GARAGE, SCR_INTRO,
              SCR_CAREER, SCR_CAREER_NEW, SCR_LIVERY, SCR_TROPHIES, SCR_REPLAY, SCR_PHOTO, SCR_REALISM,
              SCR_HQ, SCR_SHOWROOM };

struct Game {
    Screen screen = SCR_TITLE;
    Renderer R;
    Race race;
    RaceSettings set;
    Champ champ;
    bool champMode = false;
    // career mode
    Career career;
    bool careerMode = false;
    bool ttMode = false;          // time trial (its own mode on the title menu)
    float weekendRubber = 0.1f;   // the track rubber carried from practice to qualifying to the race
    void ghostPath(char* out, int n) const;
    int careerPage = 0;           // team screen page: 0 main, 1 sponsors, 2 drivers, 3 upgrades, 5 standings
    int newName = 0, newCls = ERA_GE, newDiff = 1, newMin = 5;      // newCls: the era; newMin: laps per race
    void careerLivery();          // push the career car's livery to the renderer
    // performance: with "Show FPS" on (or in the benchmark) a line every 2 s goes to perf.log
    // benchmark: if bench.txt is in the game folder, the game plays a fixed set of scenes and quits
    bool bench = false, benchDone = false; int benchStep = -1; float benchT = 0;
    double perfUpd = 0, perfDraw = 0, perfGpu = 0, perfWait = 0; float perfWorst = 0, perfT = 0; int perfN = 0, memPeak = 0;
    void perfFrame(float dt, double updMs, double drawMs);
    // automatic quality: in a race, if the frame rate stays low the detail steps down (effects, scenery,
    // then draw distance); when there is plenty of spare time again it steps back up, slowly
    int autoLevel = 0;            // 0 = the player's settings ... 4 = everything lowered
    bool autoAllowed = true;      // off in the PC tests (fixed time steps)
    float aqT = 0, aqBusy = 0, aqCalm = 0; int aqFrames = 0, aqBusyRuns = 0;   // aqBusyRuns: 2.5 s windows in a row with the work near the frame budget
    void autoQualityStep(float dt);
    void benchUpdate(float dt);
    bool raceLoaded = false;
    int cam = CAM_CHASE;
    // replay: every car's place 10 times a second for the last minute (a ring of REP_N frames)
    struct RepCar { float s, d, yaw, lift, steer, spin, v; bool nose, wing; float hOff, pitch, roll; };
    static const int REP_N = 600;
    std::vector<RepCar> rep; int repCars = 0, repHead = 0, repCount = 0; float repNext = 0;
    float repT = 0; bool repPlay = true; int repCam = 0, repFocus = 0, repLivePlayer = 0;
    std::vector<Car> repLive;                 // the race as it was when the replay started (put back after)
    // photo mode: the race stands still, a free camera circles a car
    float phYaw = 2.4f, phPitch = 0.25f, phDist = 7, phFov = 50; bool phHelp = true; Screen phFrom = SCR_PAUSE;
    // the camera the replay / photo mode wants (drawRaceView uses it on those screens)
    Camera viewCam; int viewMode = CAM_CHASE;
    void replayRecord();
    void replayStart();
    void replayApply();       // put the cars where they were at repT
    void replayEnd();
    void updateReplay(float dt, const Pad& p);
    void updatePhoto(float dt, const Pad& p);
    void drawReplayHud();
    void drawPhotoHud();
    int sel = 0;                  // highlighted row on the current screen
    int era = ERA_GE;             // the era being picked on the circuit / team screens
    float t = 0;                  // time on this screen
    float idle = 0;               // time without a button press (title -> highlights reel)
    int padDir = 0; float padHeld = 0; bool padLast = false;   // the d-pad steering: which way, held how long
    float steer = 0;
    Pad prev;
    float tiltBase = 0; bool tiltCal = false;          // PS Vita tilt steering: the console's angle when driving began
    int tapRow(const Pad& p, float x0, float x1, float y0, float step, int n) const;   // a new tap on a list row, or -1
    bool tapIn(const Pad& p, float x0, float y0, float x1, float y1) const {
        return p.touches > 0 && prev.touches == 0 && p.tx[0] >= x0 && p.tx[0] < x1 && p.ty[0] >= y0 && p.ty[0] < y1;
    }
    bool aiDrivesPlayer = false;  // used by the PC test build
    double trafficStuck = 0;      // tests
    bool wallView = true;         // managing: Circle swaps between the pit wall and the TV pictures
    float accum = 0;
    int saveSlot = 0;
    char note[64] = "";           // short confirmation shown on the team / load screens
    float noteT = 0;
    // settings menu
    Screen optionsFrom = SCR_TITLE;   // where the settings were opened from (title or pause)
    int rebinding = -1; bool rebindArmed = false;
    float fps = 60, lastDt = 1.0f / 60;
    // in-car menus: race settings on the tablet, car setup on the diagnostics computer to the right
    bool setupPage = false; float look = 0; int setupSel = 0;
    float podiumScr[3][2] = {{240, 120}, {150, 140}, {330, 150}};   // the winners' heads on screen (champagne)
    // garage camera
    V3 menuEye, menuTarget; float menuFov = 46; bool menuCamInit = false;
    // highlights reel
    int reelFocus = 0; float reelSwitch = 0; int reelCount = 0;
    // podium
    struct Bit { float x, y, vx, vy, life; u32 col; int kind; };
    std::vector<Bit> bits;
    // previews for the track menu (loaded lazily)
    Track preview; int previewIdx = -1;

    bool init();
    void shutdown();
    void frame(float dt, const Pad& pad);
    void step(float dt, const Pad& pad) { update(dt, pad); prev = pad; }   // update without drawing
    void startRace();
    void startSession(Session ses);
    void startReel();
    bool loadSlot(int slot);
    std::vector<int> qualiGrid;   // car numbers in qualifying order
    // the intro (intro.cpp): music position, the beat grid, the shot and era block on screen
    float introPos = 0, introBeat0 = 9.887f, introPeriod = 0.48234f, introLength = 70.0f;
    int introScript = 0; float introLastPos = 0;
    int introShot = -1, introBlock = -1, introPendingBlock = -1, introCardFrames = 0;
    float introShotT0 = 0, introCamS = 0; int introFocus = 0, introFocus2 = 1; bool introCrashDone = false;
    void introBegin(int script = 0);     // 0: the boot intro, 1: the attract movie (title screen left alone)
    void introEnd();
    void introUpdate(float dt, bool skip);
    void introLoadBlock(int block);
    void introPlace(Car& c, float s, float d, float v);
    void introStage(int shot);
    void introDirect(int shot, float st);
    Camera introCamera(float st);
    float introBarTime(int bar) const;

    int controlRows(int* ids) const;
    int optCat = -1;                     // settings: the open category (-1: the list of categories)
    int optRows(int* ids) const;         // the rows of the open category (ids as in screenOptions)
    bool mechPage = false;               // Car realism: the mechanical problems page
    void screenRealism(Panel& P);
    void updateRealism(int dx, int dy, bool ok, bool back);     // the rows the Controls page shows (CR_* ids), in order
    void chooseEra(int e);
    int hqSel = 3; bool hqChosen = false; float hqLook = 3;   // (headquarters: the era looked at, picked yet)
private:
    void update(float dt, const Pad& pad);
    void updateRace(float dt, const Pad& p);
    void afterLoad();
    void leaveRace();
    void finishRace();
    void draw();
    void drawRaceView();
    void drawTeam();
    void drawPodium();
    void drawManage(bool wall);
    // managing from the pit wall: a stand on the pit wall at our box, with the timing screens
    Camera pitWallCamera() const;
    void drawPitWall(const Camera& cam);
    void drawMenus(float dt);
    // team headquarters: the era is picked from four model cars on the desk (menu.cpp)
    void drawHQ(float dt);
    // the showroom: the era's cars in the garage, the camera walking round them (menu.cpp)
    int showroomCar = 0; float showYaw = 0.6f, showDist = 5.2f; bool showSpin = true;
    void drawShowroomInfo();
    void drawOptionsFlat();
    void drawFps();
    void screenTitle(Panel& P);
    void screenTrack(Panel& P);
    void screenCar(Panel& P, Panel& info);
    void screenChamp(Panel& P);
    void screenCareer(Panel& P);
    void screenCareerNew(Panel& P);
    void screenLivery(Panel& list, Panel& info);
    void updateCareer(int dx, int dy, bool ok, bool back, bool square);
    void screenLoad(Panel& P);
    void screenHelp(Panel& P);
    void screenTrophies(Panel& P);
    // trophies: what happened in this race (see checkRaceTrophies)
    int gridPos = 0; bool sawNight = false; bool qteWasOn = false;
    void checkRaceTrophies();
    void finishFrame() { trophies.draw(R); gfx::endFrame(); }   // overlays shown on every screen, then present
    void screenOptions(Panel& P);
    void screenControls(Panel& P);
    void updateOptions(const Pad& p, int dx, int dy, bool ok, bool back);
    int updateSetup(int dx, int dy, bool ok, bool back, bool trackGarage);    // 0 stay, 1 leave / drive out, 2 flying lap, 3 skip the session
    bool parcFerme() const;
    Camera inCarCamera(const Car& c, float look) const;
    void drawInCarPanels(const Car& c, const Camera& cam, const M4& carRel, bool trackGarage);
    void screenSetupTablet(Panel& P);
    void screenSetupComputer(Panel& P, bool trackGarage);
    void drawGarageView();
    void nowPlayingBar();
    void drawIntro();

    void drawMenuBack(const char* title);
    void raceAudio();
    void raceEvents();
    void trackSpeakers();
    bool pressed(const Pad& p, bool Pad::*b) const { return p.*b && !(prev.*b); }
};
