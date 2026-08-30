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

### 2026-08-30

- **Waypoints Match on Every Copy of the World**: The map is 16 rows of 8 world units, so the terrain repeats every 128 units in x - and the map view draws it that way. The waypoint test did not: it compared positions modulo 256, so past the seam you could park on runway 2, at the pixel the map draws the runway at, and be told you were 128 units short. Waypoint distances are now measured the short way round each axis' own period, x every 128 and y every 256.
- **No More Phantom Waypoints Half a Map Away**: The same test took its absolute value by negating an `int8_t`, and -128 negates to itself - so a waypoint exactly 128 units off in y passed a ±16 check. Mission 06's first lake could be claimed from open ground 32 km away. The arithmetic is unsigned now.
- **The Crop Duster Has Room to Fly**: `WP_MAX_125FT` becomes `WP_MAX_250FT`. The old ceiling left 62 ft between the limit and the ground - two ticks of the altimeter's fine needle, which moves one per 31 ft - with an instant crash at the bottom.
- **`NOT ON RUNWAY` Is a Crash, Not a Warning**: The approach advisory fires on any descent below 125 ft anywhere in the world, so it warned about the runway on every low pass, including the ones the missions ask for - mission 09 spent its whole run being told off for the altitude the briefing demanded. Being first in the fault order it also hid the warnings worth having: a gear-up approach away from the field reported the runway, not the gear. Landing off a runway is still a crash and still says so.
- **The Airshow Pass Has to Be Low**: `WP_UPSIDE_DOWN` only checked `up.z < 0`, so the "low pass upside down" could be flown as a lazy roll at 3,400 ft. It now wants 250 ft or below, and `up.z < -128` - 120 degrees of roll - so a steep bank no longer reads as inverted.
- **Mission 10 Can Afford an Approach**: The 49.6 world units to runway 2 cost about 78% of the old tank at the cheapest cruise the model has, which left nothing for the circuit. Flown by an autopilot, a tight pattern landed with 3.7% left, a wider one arrived dead-stick, and a wider one still came down short. The tank goes from 0x04 to 0x05: still a fuel challenge, now with room for one normal approach.

### 2026-08-29

- **One Binary Again**: `ppilot.prg` is the only release again. Sound effects and the debug view (`D`, `Z`, `X`) ship together, so there is no longer a build you have to swap to in order to read the cycle counters, and none that trades the counters for sound. `ppilotd.prg` is gone.
- **Per-Stage Cycle Counters for Polygons, Clouds and Sprites**: The debug view (`D`) now reports `PLY`, `CLD` and `SPR` — the cost of polygon drawing, the cloud scan and the sprite stack — as a breakdown of the existing `GRD` and `UPD` stages. Parked on the runway, the single runway polygon is 43,064 cycles: 53% of the grid walk and a third of the whole measured frame.
- **Faster Flight Model**: The roll-induced turn was a general 3×3 matrix multiply against a matrix that is the identity plus two terms — 27 multiplies where 6 do the work. Written out as `vec_turn3_xy()`, it is bit-for-bit identical and the model step drops from 20,922 cycles to 16,769 while rolling.
- **Steadier Frame Times**: The flight model is back to one step per rendered frame. The raster-timebase catch-up loop it replaces held airspeed constant across scenes, but it owed the model a second step on exactly the frames that had already overrun their budget — so the worst frame did double the model work. Holding roll, the worst frame drops from 44,678 cycles to 20,922. The trade is that airspeed through the world now varies with the scene by up to a third. It also removes a bug where the map view and the help screen banked up simulation time and applied it in one frame on exit, enough to come back from the map already crashed.

### 2026-08-09

- **Dual Binary Build**: Split the application into two dedicated binaries: `ppilot.prg` (sound effects enabled, debug keys `D`/`Z`/`X` disabled) and `ppilotd.prg` (debug view enabled, volume key `V` disabled). Updated in-game help screens accordingly.

### 2026-08-07

- **Missions & Objectives System**: Added 10 playable missions (Airborne, Getting Down, Solo Flight, Find the Runway, Ferry Flight, Lake Patrol, Airshow, Aerial Recon, Crop Duster, Fuel Challenge) with waypoint navigation and altitude/attitude constraint checking.
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
- **HUD & Warning Messages**: On-screen messages for approach warnings (`GEAR RETRACTED`, `SINK RATE`, `BANK ANGLE`), mission status/waypoint alerts, fuel depletion notices, crash reasons, and pause indicator (`PAUSED`).
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
| `H`             | Show the help screen                    |
| `Q`             | Quit to the main menu                   |

The help screen (`H`) is also available from the main menu.

## Development

Much of the code was written with Antigravity and Gemini. Prototyping and
data generation was done in Python, and the C64 code is in C with some assembly.
To compile the code, you need the [oscar64](https://github.com/drmortalwombat/oscar64/blob/main/README.md) cross-compiler.

See [docs/development.md](docs/development.md) for more details,
[docs/project.md](docs/project.md) for the architecture, and
[docs/missions.md](docs/missions.md) for what each mission asks for.

## Inspirations

- **Stunt Car Racer** is one of the best 3D games for C64, released in 1989 ([YouTube](https://www.youtube.com/watch?v=KMgjmIW8fd8))
- **Spitfire 40** is an earlier flight sim from 1985 ([YouTube](https://www.youtube.com/watch?v=cpq0VzBINno))
- **Chuck Yeager's Air Combat** is a much more advanced flight sim for DOS/PC from 1991, served as an inspiration for the horizon rendering ([YouTube](https://www.youtube.com/watch?v=L1x7229289w))

## License

Plane Pilot is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.

Copyright (C) 2026 Peter Dornbach. The full text is in [LICENSE](LICENSE).
