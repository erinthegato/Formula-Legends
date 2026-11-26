# Formula Legends (PS Vita)

A Formula 1 inspired racer for the PS Vita by CODE 4 Studios, built on the Endurance Motor Racing 3D
engine (physics, circuits, trackside scenery, pit crews and their animations). Not for distribution.

## What's in it
* **Four eras**, ten fictional teams each, two cars per team:
  * **1980 wing cars**: venturi sidepods with sliding skirts, small winglets, exposed V8s, big rear
    tyres, a round three-spoke wheel and analogue dials, five-speed gearboxes, no refuelling, no safety
    car (double yellows only), slow tyre stops, 9-6-4-3-2-1 points; the car is built in code
    (`tools/gen_car_1980.py`, its parts in `tools/gen_parts_1980.py`)
  * **Grid girls** on the 1980 and 2010 grids: one in front of each car holding its number board,
    walking off just before the lights (those starts wait 4 s longer)
  * **2010 V8**: long high noses, shark fins, no refuelling
  * **2016 hybrid**: low "finger" noses, ERS boost, DRS
  * **2025 ground effect**: the halo, wide low noses, 18-inch wheels with covers, ERS, DRS (three zones)
* **Cars built from code** (`tools/gen_f1.py`): every team's car, its livery, the era's road wheels,
  steering wheel and DRS flap. Open cockpits: the cockpit camera sees the nose, the front tyres and
  (2025) the halo.
* **29 real circuits** (`tools/build_tracks.py`), the real layouts scaled to the official lap length
  and the real heights: Interlagos, Imola, Monaco, Barcelona, Montreal, Magny-Cours, Hungaroring, Spa,
  Monza, Suzuka, Bahrain, Melbourne, Sepang, Shanghai, Istanbul, Singapore, Yas Marina, Sochi, Baku,
  Red Bull Ring, Silverstone, Hockenheim, COTA, Mexico City, Jeddah, Miami, Zandvoort, Las Vegas and
  Lusail. Each era has a 10-round calendar of circuits it really raced at. Street circuits get walls,
  debris fences and city blocks; night races are under floodlights. The layouts are today's versions.
* **F1 rules**: standing starts with the five red lights, DRS (from lap 3, within a second of the car
  ahead at the detection point, dry, no safety car), no refuelling, a virtual safety
  car from 2016 (the safety car in 2010, none in 1980), tyres and fuel sized to the race distance, the
  1980 9-6-4-3-2-1 points and the 25-18-15-... points after.
* **Physics per era** (each era's own pecking order: 2010 quickest, then 2016 and 2025, the 1980 wing
  cars slowest). AI on Hard, as first tuned to real pole times; the 1980 row is the AI's own times:

  | AI on Hard (real pole) | Monza | Spa | Monaco | Suzuka |
  |---|---|---|---|---|
  | 1980 | 1:32.3 | 1:58.4 | 1:19.1 | 1:43.3 |
  | 2010 | 1:22.7 (1:21.9) | 1:45.4 (1:45.8) | 1:12.8 (1:13.8) | 1:31.4 (1:30.8) |
  | 2016 | 1:21.3 (1:21.1) | 1:44.5 (1:46.7) | 1:12.0 (1:13.6) | 1:31.2 (1:30.6) |
  | 2025 | 1:19.4 (1:18.8) | 1:40.3 (1:40.5) | 1:09.5 (1:10.0) | 1:27.2 (1:27.0) |

  The cars are not glued to the road (`Race::bodyUpdate`, `CarBody`): the body rides on ~3 Hz
  springs, keeps its upward speed over crests and kerbs and only comes down under its weight and
  downforce, holds its angle in the air, and the tyres' grip follows the load they carry (light
  over a crest, none in the air, a touch more in a dip). Braking dips the nose, corners roll the
  body, the tyres stay on the road. Barrier hits keep the speed along the wall and bounce off,
  instead of turning the car parallel. `./fl_test physlap <track> <era> <laps>` times the AI with no
  drawing and reports how long the car was light or in the air (these changes cost it 0.1-1.2 %).

* **Modes**: quick race, time trial (ghost), championship (an era's 10-round season), career (your own
  two-car team in an era: sponsors, a team-mate and a reserve, upgrades, livery), race weekends
  (practice, qualifying, race), replays and photo mode, trophies.
* **The intro**, cut to *Take On The World* (All Good Folks, Uppbeat; `DATA/music/intro.emu`, the
  first 70 s, 124.4 BPM): "CODE 4 STUDIOS" strobing on the opening beats, then from the first big hit a
  highlight reel of the four eras in the game itself (2025 at Suzuka, 1980 at Monaco, 2010 in a storm at
  Spa, 2016 at the Red Bull Ring): empty-track crane shots, DRS trains, overtakes, pit stops, crashes, a
  grid start, era title cards (the next circuit loads behind them) and FORMULA LEGENDS at the end. Any
  button skips it. The script is `src/intro.cpp` (shots in bars of four beats).
* **The attract movie**: leave headquarters or the main menu alone (Options: "Attract screen after",
  10 s by default) and a second movie plays a whole song from `DATA/music/attract.emu`, cut on the bar
  lines with one era per section, looping until a button is pressed (script: `ATTRACT_SHOTS` in
  `src/intro.cpp`, beat grid in `attract.txt`). The song is "Nocturn" (Tonebox), an original made for
  the game with ElevenLabs: 100 BPM, bar 0 at 0.292 s.
* PS Vita: `cmake -S vita -B build-vita && cmake --build build-vita` with VitaSDK + vitaGL
  (GitHub Actions builds the VPK on every push). It makes two files:
  * `FormulaLegends.vpk` (~9 MB): the game. Install it with VitaShell; updates install in seconds.
  * `FormulaLegends-music.zip` (~78 MB): the race, attract and podium music. Install it **once**:
    copy the zip to the Vita, open it in VitaShell, select all, copy, and paste at the root of `ux0:`
    (it unpacks to `ux0:data/FormulaLegends/DATA/music/`). Only needed again when the music changes.
    Without it the game runs with the intro music only, and the main menu says so.
  Data the app doesn't carry is read from `ux0:data/FormulaLegends/DATA/`; already-compressed data goes
  into the VPK unzipped (`tools/make_vpk.py`), so the Vita doesn't spend the install unzipping it.
* PC test build: `make -f Makefile.pc`, then from this folder e.g.
  `./fl_test race monza 0 3 9 shots 20 60` (car 0 = the 1980 Marchetti; AI drives your car,
  screenshots at 20 s and 60 s), `./fl_test intro shots` (frames of the intro), `./fl_test menus shots`.
  The PC build reads `HD/` (the Vita's models) unless `FL_LOWRES` is set.

## Rebuilding the data
* Cars: `python3 tools/gen_f1.py [preview dir]` -> `HD/models/car_*.emm`, `wheel_*`, `steering_*`,
  `flap_*`, `livery_career_*.lvy`. Team list and colours are read from `src/data.cpp`.
* The 2010 car is made in Blender (the first-generation low-poly car: no driver, a hollow cockpit
  for the steering wheel and its display; sized to the 2010 technical regulations):
  `tools/blender/f1_v8_car.py` builds it (three LODs, 942 / 670 / 416 triangles) and exports
  `tools/blender/f1_v8_car.json`, which `gen_f1.py` paints every 2010 team's livery onto:
  `blender -b -P tools/blender/f1_v8_car.py -- --export tools/blender/f1_v8_car.json`. After editing the
  model by hand, re-export with `--export-only`. Its axles, wheel track and rear wing / DRS flap are
  shared with `gen_f1.py` (`E['v8']`) and `src/render.cpp` (`COCKPIT[1]`, `FLAP_PIVOT[1]`).
* `tools/blender/f1_eras.py` builds the 1998, 2016 and 2025 cars in the same style, sized to each
  season's technical regulations; `export_json(year, path)` exports one to `tools/blender/f1_<year>_car.json`,
  which `gen_f1.py` paints every team's livery onto (all four eras are Blender cars now).
* `tools/blender/trackside.py` builds the trackside objects (start gantry, corner bleachers, tents,
  marshal light panels, recovery cranes, TecPro barriers) and exports them to `tools/blender/props/`;
  `python3 tools/gen_trackside.py` turns them into `HD/models/*.emm` with one 64 px colour texture each
  (about 15-30 KB a model). `Track::addTrackside()` places them on every circuit.
* The circuit buildings (grandstand, billboard, advertising bridge, race control, hospitality) are in
  `tools/blender/trackside.py` too. They share one 512 px atlas of sponsor boards, the timing screen, the
  crowd and glass, painted by `tools/gen_trackside.py` and stored once, in `grandstand.emm`.
* The era safety cars: `build_safety_cars()` + `export_json('SC<year>', ...)` in `tools/blender/f1_eras.py`,
  then `python3 tools/gen_safety_cars.py` -> `HD/models/safety_car_<era>.emm` (tyres on the model).
* F1 rear light: a lens on each car's crash structure, lit by `Renderer::rearLightMode` (rain; energy
  harvesting and the pit lane limiter on the hybrid cars).
* Track life: the marshal light panels flash yellow at incidents, under the VSC and the safety car, and
  blue for blue flags; fans wave flags in the teams' colours; a crowd loop plays near the stands
  (`DATA/sfx/crowd_stands.raw`); pit crews wait in the box, carry the wheels, hold the jacks, and
  release the car with a lollipop (1980) or the light gantry (2010 on), with a shout (`pit_crew_go.raw`).
  Test modes: `fl_test screel <dir> <era>`, `fl_test pitreel <dir> <era>`.
* `tools/blender/pit_crew.py` builds the rigged pit crew member (full-face helmet, wheel gun) and his
  four motions, keyed on the timeline (idle 1-10, wheel gun 11-20, push 21-30, celebrate 31-40);
  `export_json()` -> `tools/blender/pit_crew.json`, then `python3 tools/gen_crew.py` writes the 16 poses
  (`crew_<motion>_<pose>.emm`) and `src/team_colours.h`. The crew and the podium drivers wear their team's
  colours: palette entries 1-4 of the shared 64 px texture are swapped per team (`Renderer::drawPerson`).
* Memory: the F1 cars' smaller levels of detail share level 0's texture (`Model::sharedTex`), the career
  livery keeps only the chosen style in memory, and each era's steering wheel and DRS flap load with
  the first car of that era. `FL_MEM=1` prints the PC test's heap peak, `FL_FILES=1` every file it reads.
  Previews: `fl_test crewreel <dir>` (the four motions) and `fl_test pitreel <dir>` (a real stop).
* The F1 engine loops (`DATA/audio/f1_*.raw`) were generated with ElevenLabs sound effects;
  `tools/audio/make_engine_loops.py <folder of takes>` picks the steadiest take per era and cuts the loop.
* Circuits: `python3 tools/build_tracks.py` -> `DATA/tracks/*.emt`. Outlines in
  `tools/tracks/circuits/` (bacinger/f1-circuits, MIT). Heights: `tools/tracks/emr/*.emt` (EMR's
  EU-DEM / SRTM profiles for Imola, Spa, Interlagos, COTA, Barcelona, Monza, Lusail, Bahrain) and
  `tools/tracks/elev_opentopodata.json` (40 samples a lap from OpenTopoData for the others; Mexico City
  flat at 2,232 m). Surface models see buildings in city centres, so street circuits keep the lower
  envelope of the samples.
* Intro music: `ffmpeg -i song.mp3 -t 64.5 -af afade=t=out:st=61.8:d=2.6 -ac 2 -ar 22050 -f s16le - |
  tools/music/adpcm_enc DATA/music/intro.emu 2`; `DATA/music/intro.txt` holds the first beat and the
  beat length. Attract music: the same without `-t`/fade into `DATA/music/attract.emu`, and
  `attract.txt` with its first downbeat, beat length and length. `./fl_test attract shots` checks it
  starts after 10 s idle, loops, and a button stops it.

Data credits: bacinger/f1-circuits (MIT), EU-DEM (Copernicus), SRTM (NASA) via OpenTopoData,
OpenStreetMap contributors (ODbL, EMR's circuits). Engine, crew, props and engine sounds from
Endurance Motor Racing / EMR 3D. Font: DejaVu Sans. Team names are fictional.
Music: 'Take On The World' by All Good Folks on #Uppbeat https://uppbeat.io/c/all-good-folks  License code: TOLCYIUTLLD5VYGM
Music: 'Salsa' by Hey Pluto! on #Uppbeat https://uppbeat.io/c/hey-pluto  License code: CCZCUH212QC10SVI (2025 era menu music)
2016 era menu music: six songs from Uppbeat (uppbeat.io) and one from Epidemic Sound (epidemicsound.com)
2025 era menu music: Salsa and twelve more songs from Uppbeat (uppbeat.io)
1980 era menu music: 'Home in A Minor', 'Midnight Walker', 'Midnight Resolve' and 'Fast Flying Lap', made for Formula Legends; three songs by Ruzer from Epidemic Sound (epidemicsound.com) and one from Uppbeat (uppbeat.io)
