# Endurance Motor Racing 3D (PSP)

A 3D endurance racer for the PSP / PPSSPP, made from the Unity game *Endurance Motor Racing*.

* **Cars**: 17 cars built procedurally by `tools/gen_cars.py` (8 Hypercars, 9 LMGT3s), 3 levels of
  detail each. Hypercars are sleek, enclosed single-seaters with a bubble canopy, tall wheel arches, an
  engine-cover fin and a full-width wing; LMGT3s are blockier two-seat coupes with a splitter, mirrors and
  a swan-neck wing. Liveries, class number panels (red / orange) and wheels are generated too. Teams,
  numbers, engines and Balance of Performance are the Unity game's.
* **Engine sound**: the Unity project's 10 engine recordings, pitched with the revs (your car + the 3
  nearest rivals).
* **Circuits**: 10 fictional circuits. Each starts from real map data (OpenStreetMap / bacinger
  f1-circuits) and real terrain (EU-DEM 25 m in Europe, SRTM 30 m elsewhere), then
  `tools/build_tracks.py` reshapes it: the road wanders up to ~13 m sideways in smooth waves (corners
  get a little tighter or wider, straights bend a touch; the pit straight stays put) and every lap is
  stretched 2-4 % longer than the real one. Circuit des Sablons is 13.98 km.
  Map data (c) OpenStreetMap contributors (ODbL).
* **Cockpit**: generated with the car -- roll cage, dash, steering wheel. Hypercar: one central seat.
  LMGT3: right-hand drive, the passenger side stripped to the cage and an instrument pod (speedometer,
  position, timer). The cockpit camera has no HUD except the damage diagram and the pit-in indicator:
  everything else is on the car's own displays, and the start lights are on a gantry over the track.
* **Menus**: a 3D pit garage; every menu is a screen or board standing in it.
* **Settings**: sound levels, performance (optimisation preset, vsync, 60 / 30 fps, draw distance,
  scenery, effects) and button rebinding, saved to `settings.cfg`.

## Install
Copy the `EnduranceMR3D` folder from the build artifact (it holds `EBOOT.PBP` and `DATA/`) to
`PSP/GAME/` on the memory stick (or the PPSSPP / RG56 Pro `PSP/GAME` folder).

## How to play
### Controls
| Button | Action |
|---|---|
| X or R / stick up | accelerate |
| Square or L / stick down | brake |
| Analogue stick / D-pad left-right | steer |
| Triangle | camera: chase / far chase / cockpit |
| Circle (hold) | look behind |
| Select | pit request (race: box this lap; practice / qualifying: back to the garage) |
| D-pad up | engine map: Safety car / Normal / Attack |
| Start | pause |
| L / R in the menus | skip music tracks |

Every action except steering, Start and L / R can be moved to another button or set to no button in
Settings > Controls (Square clears a binding). Gear up / down have no button by default (automatic gears).

### Race weekend
* Practice and qualifying start in the garage, sitting in the car at the diagnostics computer: change the
  setup, then **Drive out** (off the jacks, engine on, the crew pushes the car to the box and the pit-lane
  driver takes it out), go straight onto a **Flying lap**, speed time up (sessions over 5 minutes) or
  **Skip** to the next session. Pit request on track takes you down the pit lane and back into the garage.
* **Parc ferme** from qualifying on: rear wing, springs and gearing are sealed; brakes, engine map, tyres
  and fuel can still change.
* Car setup: rear wing 1-5, springs soft / medium / stiff, gearing short / medium / long, brake pressure
  front / rear (default 75 % / 40 %), engine map, starting tyre and fuel.
* Race settings are on a tablet in the car (main menu > Quick race > car > settings); the diagnostics
  computer stands to the right of the car.

### Race
* Pit stops: Select to box. The team screen sets tyres, fuel, repairs, driver swap, who drives (you or the
  team, with up to 16x time) and saves the race. Manual pit stops: press the shown button once while the
  marker is in the green. Pit stops use a cinematic camera (Settings > Pit stop camera).
* The cars slide: too much steering at speed and the car drifts wide or steps out and has to be caught;
  more rear brake makes it rotate under braking.

## Building
* PSP: `make` with the [pspdev](https://github.com/pspdev/pspdev) toolchain (GitHub Actions does this
  on every push and uploads the `EnduranceMR3D-PSP` artifact).
* PC test build: `make -f Makefile.pc`, then run it from this folder:
  `./emr3d_test race ardennes 1 2 11 shots 10 60` (the AI drives your car, screenshots go to `shots/`).

## Rebuilding the data (`DATA/`)
1. In Unity, copy `tools/unity/*.cs` to `Assets/PSP3DExport/Editor/`. `PspModelExport.ExportAll()`
   writes every model to `PSP3DExport_Data/models`; `PspFetchQueue` downloads the map / elevation data.
2. `tools/build_models.py`, `tools/build_tracks.py` (+ `tools/tracks/lemans.py` for Le Mans),
   `tools/build_assets.py` build the PSP files. `tools/meshtool/bake.cpp` is the simplifier + texture baker.

Data credits: OpenStreetMap contributors (ODbL), bacinger/f1-circuits (MIT), EU-DEM (Copernicus),
SRTM (NASA), via OpenTopoData. Font: DejaVu Sans.
