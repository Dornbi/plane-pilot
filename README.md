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
- [flighta.prg](bin/flighta.prg) — the same game with the angle-of-attack flight model (see the 2026-09-05 note below)

Emulators:

- https://c64online.com/ (online)
- https://retrogamecoders.com/c64-emulator/ (online)
- [VICE](https://vice-emu.sourceforge.io/) (offline)

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

| Keys            | Action                                |
| --------------- | ------------------------------------- |
| `I` `J` `K` `L` | Roll and pitch                        |
| `A` `S`         | Yaw                                   |
| `+` `-`         | Throttle up and down                  |
| `F`             | Toggle flaps                          |
| `G`             | Toggle landing gear                   |
| `B`             | Wheel brakes (on ground)              |
| `1` `2` `3`     | Look left, forward, right             |
| `N`             | Toggle Nav point 1 / 2 (runways)      |
| `M`             | Toggle map view                       |
| `R`             | Reset to starting state               |
| `P`             | Pause / Resume flight                 |
| `V`             | Volume: full / low / off (any screen) |
| `D`             | Toggle debug view                     |
| `H`             | Show the help screen                  |
| `Q`             | Quit to the main menu                 |

The help screen (`H`) is also available from the main menu.

## Development

Much of the code was written with Antigravity and Gemini. Prototyping and
data generation was done in Python, and the C64 code is in C with some assembly.
To compile the code, you need the [oscar64](https://github.com/drmortalwombat/oscar64/blob/main/README.md) cross-compiler.

See [docs/development.md](docs/development.md) for more details,
[docs/project.md](docs/project.md) for the architecture, and
[docs/missions.md](docs/missions.md) for what each mission asks for.

## Updates

### 2026-09-05

The flight model can have an angle of attack, as a build option.

The aircraft now has two directions instead of one: where the nose points, and
where it is actually going. The angle between them drives lift, and everything
else follows from that — the stall is an angle rather than a speed, so the
stall speeds are *derived* (1024 clean, 836 with flaps, against the 1024 and 832
the old model had to be told); turn rate depends on airspeed; induced drag is
one term instead of three stand-ins; and the takeoff needs no rotation fudge,
because rotating makes lift.

What a pilot will notice:

- Level flight needs a little nose-up at every speed, less of it the faster you
  go. Zero pitch is a gentle descent.
- Pulling hard can stall the wing at any speed, not just a slow one.
- A flare with speed in hand lands; holding it off until the wing stops flying
  is a stall onto the runway.
- Inverted flight needs a visibly nose-high attitude and flies nearer the
  stall.
- The glide is longer (7.3 : 1, was 6.1 : 1) and the climb is slower.

It costs +768 bytes, so it ships as **its own binary** rather than replacing
anything: `ppilot.prg` is the arcade model that has always shipped, at 47,607
bytes, and `flighta.prg` is this one at 48,375. `make` builds both from the same
sources. Both are covered by the test suite (`make -C c64o/test test-both`).
Per model step it costs roughly nothing — re-orthonormalizing dominates a step
and is untouched.

`docs/flight.md` specifies both and `docs/flight_aoa.md` is the prototype and
the measurements either side.

### 2026-08-30

Bugfixes:

- Fixed waypoint matching check and made it work for every copy of the world.
- Fixed Crop Duster margins to make it flyable.
- Added altitude limit for the Airshow pass.
- Increased margin for the Fuel Challenge mission.

### 2026-08-29

- Updated counters on the debug screen.
- Optimized the flight model.

### 2026-08-07

- Missions & Objectives: Added 10 playable missions.
- Engine sound.
- Updated flight physics: takeoff roll, touchdown flare, rollout, flaps, gear, etc.
- Map View: Full-screen map view .
- Messages: On-screen messages.
- Added host tests.

### 2026-07-05

- Added support for polygons: runway, lakes
- There is a 32x16 basic map now (map view coming)
- Added support for side views (left and right)
- Many optimizations and bug fixes

### 2026-04-30

- First release.

## Inspirations

- **Stunt Car Racer** is one of the best 3D games for C64, released in 1989 - served
  as a motivation to create a playable, fast 3D game([YouTube](https://www.youtube.com/watch?v=KMgjmIW8fd8)).
- **Spitfire 40** is a flight sim from 1985 with an unplayably low frame rate - served as
  extra motivation to make it fast ([YouTube](https://www.youtube.com/watch?v=cpq0VzBINno)).
- **Chuck Yeager's Air Combat** is a much more advanced flight sim for DOS/PC from 1991,
  served as an inspiration for the horizon rendering and other features ([YouTube](https://www.youtube.com/watch?v=L1x7229289w)).

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
