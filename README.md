# Plane Pilot

Plane Pilot is a 3D flight simulator demo for the C64.

![Plane Pilot Screenshot](screens/screen01_crt.png)

## History and motivation

Plane Pilot is an attempt to show how modern compilers and AI tools can
be used in to create something retro in 2026. It is also an attempt
to push the boundaries of C64 in a somewhat non-typical genre -
3D simulations - something the C64 was absolutely not designed to do.

## How to play

There are many online and offline C64 emulators.
One of the easiest: https://ty64.krissz.hu/

Click the web icon and paste the URL:

```
https://github.com/Dornbi/plane-pilot/raw/refs/heads/main/bin/ppilot.prg
```

Alternatively, download the binary and upload it to any of the online or offline emulators:

- [ppilot.prg](bin/ppilot.prg) — the whole game: sound effects, music, and the debug view behind `D`

Emulators:
- https://c64online.com/ (online)
- https://retrogamecoders.com/c64-emulator/ (online)
- [VICE](https://vice-emu.sourceforge.io/) (offline)

## Updates

### 2026-08-29

- **One Binary Again**: `ppilot.prg` is the only release again. Sound effects and the debug view (`D`, `Z`, `X`) ship together, so there is no longer a build you have to swap to in order to read the cycle counters, and none that trades the counters for sound. `ppilotd.prg` is gone.
- **Faster Flight Model**: The roll-induced turn was a general 3×3 matrix multiply against a matrix that is the identity plus two terms — 27 multiplies where 6 do the work. Written out as `vec_turn3_xy()`, it is bit-for-bit identical and the model step drops from 20,922 cycles to 16,769 while rolling.
- **Steadier Frame Times**: The flight model is back to one step per rendered frame. The raster-timebase catch-up loop it replaces held airspeed constant across scenes, but it owed the model a second step on exactly the frames that had already overrun their budget — so the worst frame did double the model work. Holding roll, the worst frame drops from 44,678 cycles to 20,922. The trade is that airspeed through the world now varies with the scene by up to a third. It also removes a bug where the map view and the help screen banked up simulation time and applied it in one frame on exit, enough to come back from the map already crashed.

### 2026-08-09

- **Dual Binary Build**: Split the application into two dedicated binaries: `ppilot.prg` (sound effects enabled, debug keys `D`/`Z`/`X` disabled) and `ppilotd.prg` (debug view enabled, volume key `V` disabled). Updated in-game help screens accordingly.

### 2026-08-07

- **Missions & Objectives System**: Added 10 playable missions (Takeoff, Landing, Solo Flight, Find Runway, Ferry Flight, Area Patrol, Airshow, Aerial Recon, Crop Duster, Fuel Challenge) with waypoint navigation and altitude/attitude constraint checking.
- **Main Menu**: Interactive mission selection menu with scrolling support and in-game help screen (`H`).
- **Sound Engine**: Integrated SID sound effects including throttle-based engine sound, stall warning alarm, touchdown squeal, mechanical flap/gear clicks, crash sounds, and volume control (`V`).
- **Complete Flight & Ground Physics**:
  - Full takeoff roll, touchdown flare, and rollout physics.
  - Realistic landing envelope checks (sink rate, bank angle, touchdown speed, runway alignment).
  - Flap lift & drag mechanics (+50% lift, stall speed reduction).
  - Gear drag penalty and ground wheel braking (`B`).
  - Nose-wheel steering remapped on ground.
  - Safety lockout preventing landing gear retraction while on the ground.
- **Map View & Flight Path Tracking**: Full-screen map view (`M`) rendering terrain features, mission waypoints, current aircraft position marker, and flight path breadcrumb trail.
- **HUD & Warning Messages**: On-screen messages for approach warnings (`GEAR RETRACTED`, `NOT ON RUNWAY`), mission status/waypoint alerts, fuel depletion notices, crash reasons, and pause indicator (`PAUSED`).
- **Testing**: Added a comprehensive 56-test host verification suite.

### 2026-07-05

- Added support for polygons: runway, lakes
- There is a 32x16 basic map now (map view coming)
- Added support for side views (left and right)
- Many optimizations and bug fixes

### 2026-04-30

- First release.

## Features

What you can do in Plane Pilot:

- Select and play from 10 distinct flight missions with waypoint objectives
- Interactive main menu and mission selection
- Take off, navigate, flare, land, and taxi on runways
- Dynamic flight model: speed, altitude, pitch/roll/yaw, stall recovery, flap lift/drag, gear drag, and fuel consumption
- SID sound effects: engine roar, stall alarm, touchdown squeal, flap/gear clicks, crash sound effects
- Full-screen 128x128 map view (`M`) displaying world terrain, numbered mission waypoints, aircraft location marker, and real-time flight path tracking trail
- Dashboard instrument panel with working indicator lamps (flaps, gear, stall warning, nav points)
- Look forward, left, right, and toggle the HUD debug view with per-stage cycle counters (`D`)
- On-screen HUD notifications, approach warnings, and crash diagnostics
- Wheel braking (`B`) and ground nose-wheel steering
- Maintain ~10 frames per second on standard C64 hardware

What you cannot do:

- No joystick support (keyboard controls only)
- No combat / dogfighting opponents

Instrument panel:

![Instrument panel](screens/panel.png)

## Controls

To fly the plane you can use the following keys:

| Keys            | Action                                  |
| --------------- | --------------------------------------- |
| `I` `J` `K` `L` | Roll and pitch                          |
| `A` `S`         | Yaw                                     |
| `+` `-`         | Throttle up and down                    |
| `F`             | Toggle flaps                            |
| `G`             | Toggle landing gear                     |
| `B`             | Wheel brakes (on ground)                |
| `1` `2` `3`     | Look left, forward, right               |
| `N`             | Toggle Nav point 1 / 2 (runways)        |
| `M`             | Toggle map view                         |
| `R`             | Reset to starting state                 |
| `P`             | Pause / Resume flight                   |
| `V`             | Volume: full / low / off (any screen)   |
| `D`             | Toggle debug view                       |
| `X` `Z`         | Move forward and backward (when paused) |
| `H`             | Show the help screen                    |
| `Q`             | Quit to the main menu                   |

The help screen (`H`) is also available from the main menu.

## Development

Much of the code was written with Antigravity and Gemini. Prototyping and
data generation was done in Python, and the C64 code is in C with some assembly.
To compile the code, you need the [oscar64](https://github.com/drmortalwombat/oscar64/blob/main/README.md) cross-compiler.

See [docs/development.md](docs/development.md) for more details, and
[docs/project.md](docs/project.md) for the architecture.

## Inspirations

- **Stunt Car Racer** is one of the best 3D games for C64, released in 1989 ([YouTube](https://www.youtube.com/watch?v=KMgjmIW8fd8))
- **Spitfire 40** is an earlier flight sim from 1985 ([YouTube](https://www.youtube.com/watch?v=cpq0VzBINno))
- **Chuck Yeager's Air Combat** is a much more advanced flight sim for DOS/PC from 1991, served as an inspiration for the horizon rendering ([YouTube](https://www.youtube.com/watch?v=L1x7229289w))
