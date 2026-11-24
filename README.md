# Formula Legends (PS Vita)

A Formula 1 inspired racer for the PS Vita by CODE 4 Studios, built on the Endurance Motor Racing 3D
engine (physics, circuits, trackside scenery, pit crews and their animations). Not for distribution.

## What's in it
* **Four eras**, ten fictional teams each, two cars per team:
  * **1998 V10**: narrow cars, raised noses, tall airboxes, grooved tyres, refuelling at the stops
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
  ahead at the detection point, dry, no safety car), refuelling only in the 1990s, a virtual safety
  car from 2016 (the safety car before), tyres and fuel sized to the race distance, the 1990s
  10-6-4-3-2-1 points and the 25-18-15-... points after.
* **Physics per era**, tuned so the AI on Hard laps close to each era's real pole times:

  | AI on Hard (real pole) | Monza | Spa | Monaco | Suzuka |
  |---|---|---|---|---|
  | 1998 | 1:23.9 (1:25.3) | 1:49.5 (1:48.7) | 1:15.7 (1:19.8) | 1:36.4 (1:36.3) |
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
* **The intro**, cut to *Kickstart My Heart* (the copy in `DATA/music/intro.emu`, the first 64 s):
  "CODE 4 STUDIOS" flashing on the motorcycle revs, then from the band's first beat a highlight reel
  of the four eras in the game itself (2025 at Suzuka, 1998 at Monaco, 2010 in a storm at Spa, 2016
  at the Red Bull Ring): empty-track crane shots, DRS trains, overtakes, pit stops, crashes, a grid
  start, era title cards (the next circuit loads behind them) and FORMULA LEGENDS at the end. Any
  button skips it. The script is `src/intro.cpp` (shots in bars of four beats, 178 BPM).
* **The attract movie**: leave the title screen alone (Options: "Attract screen after", 10 s by
  default) and a second movie plays to the whole of *Lose My Mind* (Don Toliver feat. Doja Cat,
  `DATA/music/attract.emu`, 110 BPM), cut on the bar lines and one era per section of the song:
  1998 at Imola from the drop, 2010 at night in Singapore for the chorus, 2016 at Silverstone (DRS)
  and in the rain at Interlagos, 2025 on the Las Vegas Strip for the Doja Cat verse and Zandvoort,
  then the title over the outro. It loops with the song until a button is pressed. Same script
  file, `ATTRACT_SHOTS`.

## Install (PS Vita)
Get `FormulaLegends.vpk` from the GitHub Actions run (artifact `FormulaLegends-Vita`), copy it to
`ux0:data` and install it with VitaShell (title ID `FLEG00001`). Needs `libshacccg.suprx`, like EMR 3D.
Saves go to `ux0:data/FormulaLegends/`.

## Controls
| Button | Action |
|---|---|
| X or R / right stick up | accelerate |
| Square or L / right stick down | brake |
| Left stick / D-pad | steer (tilt steering in Settings) |
| Triangle | camera: chase / far chase / cockpit |
| Circle (hold) | look behind |
| Select | box this lap |
| D-pad up | engine map |
| Start | pause |
| L / R on the circuit and team screens | change era |

DRS opens by itself when you have it; the DRS box by the speedometer shows it (outlined: you have it for the next zone, green: open).

## Building
* PS Vita: `cmake -S vita -B build-vita && cmake --build build-vita` with VitaSDK + vitaGL
  (GitHub Actions builds the VPK on every push).
* PC test build: `make -f Makefile.pc`, then from this folder e.g.
  `./fl_test race monza 0 3 9 shots 20 60` (car 0 = the 1998 Marchetti; AI drives your car,
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
* `tools/blender/f1_eras.py` builds 1998, 2016 and 2025 cars in the same style (Blender only, not in
  the game yet), sized to each season's technical regulations; `export_json(year, path)` exports one.
* Circuits: `python3 tools/build_tracks.py` -> `DATA/tracks/*.emt`. Outlines in
  `tools/tracks/circuits/` (bacinger/f1-circuits, MIT). Heights: `tools/tracks/emr/*.emt` (EMR's
  EU-DEM / SRTM profiles for Imola, Spa, Interlagos, COTA, Barcelona, Monza, Lusail, Bahrain) and
  `tools/tracks/elev_opentopodata.json` (40 samples a lap from OpenTopoData for the others; Mexico City
  flat at 2,232 m). Surface models see buildings in city centres, so street circuits keep the lower
  envelope of the samples.
* Intro music: `ffmpeg -i song.mp3 -t 64.5 -af afade=t=out:st=61.8:d=2.6 -ac 2 -ar 22050 -f s16le - |
  tools/music/adpcm_enc DATA/music/intro.emu 2`; `DATA/music/intro.txt` holds the first beat and the
  beat length. Attract music: the same without `-t`/fade into `DATA/music/attract.emu`;
  `attract.txt`: first downbeat 0.340 s, beat 0.545454 s (fitted to the onsets, steady through the
  song). `./fl_test attract shots` checks it starts after 10 s idle, loops, and a button stops it.

Data credits: bacinger/f1-circuits (MIT), EU-DEM (Copernicus), SRTM (NASA) via OpenTopoData,
OpenStreetMap contributors (ODbL, EMR's circuits). Engine, crew, props and engine sounds from
Endurance Motor Racing / EMR 3D. Font: DejaVu Sans. Team names are fictional.
