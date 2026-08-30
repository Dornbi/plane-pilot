# Missions (`missions.md`)

What each of the ten missions asks for, and what it costs to fly. The data
itself is in [`mission.cc`](../c64o/mission.cc); the checks that read it are
`_flight_check_mission_waypoints()` in [`flight.cc`](../c64o/flight.cc).

See [project.md](project.md) for the surrounding architecture and
[flight.md](flight.md) for the model these numbers come out of.

---

## 1. Units

| Quantity | Stored as | One unit is |
| --- | --- | --- |
| Position `x`, `y` | `flight_eye_* >> 16` | 256 m — one **world unit**; a map cell is 8 of them |
| Altitude `z` | `flight_eye_z` | `0x020000` = 1000 ft, ground is `0x2000` |
| Airspeed | `flight_speed` | `0x0800` is trim speed, `0x0400` the clean stall |
| Fuel | `flight_fuel` | burns `flight_throttle` per model step |

The world wraps, and not with the same period on both axes: `kWorldMap` is 16
rows of 8 world units, so **x repeats every 128 units**, while its 32 columns
make **y repeat every 256**. Waypoint distance is measured the short way round
each axis' own period, which is what lets a waypoint match on whichever copy
of the world the aircraft is over.

## 2. Waypoint constraints

`MissionWaypointConstraint` in [`mission.h`](../c64o/mission.h). Every
positional waypoint also wants the aircraft inside ±16 world units in x, and
±16 in y — ±4 for `WP_LANDED`, which is a runway rather than a region.

| Constraint | Satisfied when | HUD nag while unmet |
| --- | --- | --- |
| `WP_NOTHING` | in the box | — |
| `WP_LANDED` | on the ground, `flight_speed <= 0x0010` | — |
| `WP_MIN_1000FT` | `z >= 0x020000` | `CLIMB ABOVE 1000FT` |
| `WP_MIN_3000FT` | `z >= 0x060000` | `CLIMB ABOVE 3000FT` |
| `WP_MAX_250FT` | `z <= 0x008000` | `GO BELOW 250FT` |
| `WP_UPSIDE_DOWN` | `up.z < -128` **and** `z <= 0x008000` | `FLY LOW INVERTED` |

`up.z` is `256 * cos(roll)`, so `-128` is 120 degrees — far enough over that a
steep bank cannot be mistaken for a pass on the aircraft's back.

## 3. The missions

Distance is the sum of the straight legs from the start through every
waypoint. "Ideal fuel" is that distance flown at the cheapest cruise the model
has — throttle 16 holding trim speed, which covers 0.0039 world units per unit
of fuel — and so is a floor, not an estimate: it buys no climb, no circuit and
no approach. "Flown" is what an autopilot restricted to real key presses
actually used, which is the more honest number and still not an optimal pilot.

| # | Title | Objective | Start | Distance | Ideal fuel | Flown | Time |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 01 | Airborne | Climb to 1000 ft | Runway 1, parked | — | — | 6% | 1 min |
| 02 | Getting Down | Land on runway 1 | On final, 1000 ft, 4 km out | 16 u | 3% | 2% | 1 min |
| 03 | Solo Flight | Climb to 1000 ft, then land | Runway 1, parked | 4 u | 1% | 65% | 9–15 min |
| 04 | Find the Runway | Find runway 1 and land | 2000 ft, 9 km away | 36 u | 7% | 9% | 3 min |
| 05 | Ferry Flight | Runway 1 to runway 2 | Runway 1, parked | 145 u | 27% | 35% | 8 min |
| 06 | Lake Patrol | Three lakes, then land on runway 2 | 2000 ft, airborne | 211 u | 39% | 40% | 9 min |
| 07 | Airshow | Low inverted pass over runway 2, then land | 2000 ft, 8 km out | 32 u | 6% | 40% | 11 min |
| 08 | Aerial Recon | Three cities above 3000 ft, then land | 2000 ft, airborne | 249 u | 46% | 50% | 10 min |
| 09 | Crop Duster | Three fields below 250 ft, then land | 500 ft, airborne | 172 u | 32% | 27% | 6 min |
| 10 | Fuel Challenge | Reach runway 2 and land | 2000 ft, tank at `0x05` | 50 u | **62%** | 77% | 4 min |

Times are at the ~6.25 model steps per second a stock C64 renders
([framerate.md](framerate.md)) and are upper bounds — the autopilot flies a
wider circuit than a pilot who knows where the runway is. Mission 03's spread
is the cost of finding the field again: the same flight is 9 minutes when the
turn onto final comes out right the first time.

### Notes on individual missions

**01 / 03** start parked with the throttle closed, and their first waypoint
carries no position — `kMissionWpX/Y` of `(0, 0)` means "anywhere". So the
climb counts wherever it happens, and mission 03 only has to find the runway
again afterwards.

**02** starts 16 units out at 1000 ft. That is 8.5:1 of horizontal to vertical
in model units, against the 4.955:1 the aircraft manages power-off at its best
glide (`test_optimal_glide_angle` measures it, at `front.z = -50`). The
approach is shallower than the aeroplane glides, so it needs power the whole
way down — a normal final, not a dive.

**07** is the only mission that asks for something the flight model actively
resists. Inverted, lift is negative and deepens the faster you go, so the
aircraft comes down hard; the 250 ft ceiling and the `0x2000` ground leave
about 8 seconds of band to cross at the sink an uncorrected inverted pass
builds. Rolling in above the runway, taking the pass on the way down and
rolling out is the manoeuvre.

**09** wants three of the map's eight field cells — the ones the navpoints
number. The band is `0x2000` of ground to `0x008000` of ceiling, and the
touchdown advisories only wake below `0x4000` (125 ft) on a descent, so flying
the upper half of the band keeps the HUD quiet.

**10** is the only mission where the tank is the constraint. The direct route
alone is 62% of it, so the margin pays for the circuit and little else: flown
on a tight pattern it lands with 23% in hand, on a wide one it arrives
dead-stick but still arrives. On the old `0x04` tank the wide pattern came
down short of the field.

## 4. The map

Every waypoint sits on the terrain feature its briefing names. The map holds
one `CITY` and two `TOWN`s (mission 08 visits all three), three `LAKE`s
(mission 06 visits all three), two runways, and eight field cells of four
kinds, of which mission 09 uses three.

| Feature | Cells (world units) |
| --- | --- |
| Runway | (32, 64) · (96, 192) |
| Lake | (16, 208) · (64, 224) · (88, 16) |
| City | (16, 104) |
| Town | (104, 152) · (112, 232) |
| Fields | (56, 176) · (104, 112) · (48, 136) · (64, 96) · (72, 56) · (8, 160) · (48, 24) · (120, 72) |
