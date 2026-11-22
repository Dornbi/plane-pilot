# Missions (`missions.md`)

Plane Pilot (`c64o/pmission.cc`) and Fighter Pilot (`c64o/fmission.cc`) each ship **10 missions** over the same 32 × 16-cell repeating world (`c64o/world_map.cc`). Shared declarations live in `c64o/mission.h` and checks in `_flight_check_mission_waypoints()` (`c64o/flight.cc`). See [project.md](project.md) and [flight.md](flight.md).

---

## 1. Units & world wrap

| Quantity | Stored as | One unit is |
| --- | --- | --- |
| Position `x`, `y` | `flight_eye_* >> 16` | 128 m (1 **world unit**); a map cell is 8 × 8 units (1,024 m) |
| Altitude `z` | `flight_eye_z` | `0x020000` = 1,000 ft (256 m); ground floor is `0x2000` |
| Airspeed | `flight_speed` | `0x0800` trim, `0x0400` clean stall; 256 = 1 m/step |
| Fuel | `flight_fuel` | burns `flight_throttle` per whole step; full tank `0x21FFF` (`start_fuel = 0x22`) |

- **World wrap:** `kWorldMap` is 16 rows × 32 columns of 8 world units, so **x repeats every 128 units** (16.4 km) and **y repeats every 256 units** (32.8 km). Waypoint distances take the short way around each period.
- **Wind:** `kMissionWindX` / `kMissionWindY` exist in `mission.h` as all-zero placeholders unused by `flight.cc`.

---

## 2. Waypoint constraints & HUD messages

`MissionWaypointConstraint` in `c64o/mission.h`. Positional waypoints match within ±16 world units in x and y (±4 for `WP_LANDED`, a runway). `(0, 0)` means "anywhere".

| Constraint | Satisfied when | HUD nag (`_flight_join` appends `" FOR MISSION"`) |
| --- | --- | --- |
| `WP_NOTHING` | in the box | — |
| `WP_LANDED` | on ground, `flight_speed <= 0x0010` | `fpilot`: `LAND ON RUNWAY FOR MISSION` |
| `WP_MIN_1000FT` | `z >= 0x020000` | `CLIMB ABOVE 1000FT FOR MISSION` |
| `WP_MIN_3000FT` | `z >= 0x060000` | `CLIMB ABOVE 3000FT FOR MISSION` |
| `WP_MAX_250FT` | `z <= 0x008000` | `GO BELOW 250FT FOR MISSION` |
| `WP_UPSIDE_DOWN` | `up.z < -128` (>120° roll) **and** `z <= 0x008000` | `FLY LOW INVERTED FOR MISSION` |
| `WP_TARGETS_DOWN` | `opponent_targets_down()` (`fpilot` only) | — |

- **Completion messages:** `WAYPOINT <n> REACHED` (`ppilot`), `NEXT GOAL COMPLETED` / `GOALS COMPLETED!` (`fpilot`), and `MISSION COMPLETE!` (`FLIGHT_MISSION_COMPLETED`). Flight continues after completion.
- **Refuel & rearm:** Coming to a full stop on a runway (`model_on_ground && flight_speed == 0`) refills `flight_fuel = kFlightFullFuel` (`0x21FFF`) and in `fpilot` calls `gun_rearm()` (0 rounds in unarmed mission 09).

---

## 3. Plane Pilot missions (`c64o/pmission.cc`)

`ppilot.prg` pairs each mission with a viewport/map **colour scheme** ([color_schemes.md](color_schemes.md)) and up to 4 navpoints (`N`). "Ideal fuel" is straight-line distance at cheapest cruise (thr 16); "Flown" and "Time" (~6.25 steps/s) are upper bounds from an autopilot restricted to real key presses.

| # | Title | Scheme | Objective | Start | Dist | Ideal | Flown | Time |
| ---: | --- | --- | --- | --- | ---: | ---: | ---: | ---: |
| 01 | Airborne | Earth | Climb to 1,000 ft | Runway 1, parked | — | — | 6% | 1 min |
| 02 | Getting Down | Earth | Land on Runway 1 | Final, 1,000 ft, 2 km out | 16 u | 3% | 2% | 1 min |
| 03 | Solo Flight | Earth | Climb to 1,000 ft, then land | Runway 1, parked | 4 u | 1% | 65% | 9–15 min |
| 04 | Find the Runway | Desert | Find Runway 1 and land | 2,000 ft, 4.5 km away | 36 u | 7% | 9% | 3 min |
| 05 | Ferry Flight | Desert | Runway 1 to Runway 2 | Runway 1, parked | 145 u | 27% | 35% | 8 min |
| 06 | Lake Patrol | Earth | 3 lakes, then land on Runway 2 | 2,000 ft, airborne | 211 u | 39% | 40% | 9 min |
| 07 | Airshow | Dusk | Low inverted pass over Runway 2, then land | 2,000 ft, 4 km out | 32 u | 6% | 40% | 11 min |
| 08 | Aerial Recon | Night | 3 cities above 3,000 ft, then land | 2,000 ft, airborne | 249 u | 46% | 50% | 10 min |
| 09 | Crop Duster | Earth | 3 fields below 250 ft, then land | 500 ft, airborne | 172 u | 32% | 27% | 6 min |
| 10 | Fuel Challenge | Dusk | Reach Runway 2 and land | 2,000 ft, tank `0x05` | 50 u | **62%** | 77% | 4 min |

Notes:
- **01 / 03:** First waypoint is `(0, 0)` (anywhere), so the climb counts anywhere.
- **02:** Starts 16 u out at 1,000 ft (8.5:1 approach vs 6.13:1 sea-level / 4.982:1 high-alt best power-off glide at `front.z = -50`), so power is needed all the way down.
- **07:** Inverted lift is negative, leaving ~8 s between the 250 ft ceiling and ground; roll in above the runway and roll out after the pass.
- **09:** Uses 3 of the 8 field cells (numbered on navpoints); flying in the upper half of `0x2000..0x8000` avoids `< 0x4000` descent warnings.
- **10:** Direct route uses 62% of the `0x05` tank; tight circuit lands with ~23% spare, wide pattern arrives dead-stick.

---

## 4. Fighter Pilot missions (`c64o/fmission.cc`)

`fpilot.prg` compiles without `__ENABLE_COLOR_SCHEMES__` (Earth palette only). Airborne starts launch at cruise (`0x88 << 4 = 2176`, throttle `18`, facing `kMissionStartTurns` quarter turns left of north); parked starts (`start_z == 0`) start idle on Runway 1. All start with full fuel (`0x22`).

- **Navpoints:** Two fixed slots — `1: TARGET` (tracks active opponent) and `2: HOME BASE` (Runway 1). `N` locks to `HOME BASE` once `opponent_none_left()`.
- **Two-tier completion:** `kMissionGoals = 1` (`GOALS COMPLETED!`, 1 menu checkmark) when combat/escape goals pass; `kMissionCompleted = 2` (`MISSION COMPLETE!`, 2 checkmarks) after also landing and halting on Runway 1.
- **Debrief (`c64o/debrief.cc`):** Pressing `Q` opens the debrief screen (mission title, `GOALS`, 6-tier `YOUR AIRCRAFT` damage status, 5-digit `ROUNDS FIRED` / `ROUNDS ON TARGET`, and `ENEMIES SHOT DOWN` plane glyphs) before returning to the menu.

Full wave tables, AI behaviours, and damage mechanics live in [opponent_ai.md](opponent_ai.md).

| # | Title | Start | Opponents / Waves | Goal |
| ---: | --- | --- | --- | --- |
| 01 | Recon Intercept | Airborne | 1 bomber (5,000 ft, speed 1750) | Down before escape + land |
| 02 | Bomber Raid | Airborne | 2 bombers (3,000/3,500 ft, speed 1500) | Down before city + land |
| 03 | Ambush | Airborne | 2 parked at Runway 2 (`z = 5 m`) | Destroy parked targets + land |
| 04 | Scramble | Runway 1, parked | 2 Pass fighters + 2 Hunt fighters | Survive strafe, down waves + land |
| 05 | Head On | Airborne | 2 Hunt (N) + 2 Hunt (S) | Down both hunter waves + land |
| 06 | Big Week | Airborne | 3 waves × 2 bombers over city | Down ≥ 3 of 6 + land |
| 07 | Way Home | Airborne (wp 1) | 2 Provoked+Stray patrols in turn | Return and land at Home Base |
| 08 | Turkey Shoot | Airborne | 3 waves × 2 Path fighters at Runway 2 | Down ≥ 5 of 6 + land |
| 09 | Unarmed Escape | Airborne (`kMissionUnarmed`, 0 ammo) | 2 Hunt+Stray fighters ahead/flanking | Outrun hunters and land |
| 10 | Combat Patrol | Airborne | 2 Hunt fighters (6,000 ft, speed 2400, behind) | Down hunters + land |

---

## 5. Map feature coordinates (`world_map.cc`)

Every waypoint sits on the terrain feature named in its briefing: 2 runways, 3 lakes, 1 city, 2 towns, and 8 field cells.

| Feature | World units `(x, y)` |
| --- | --- |
| Runway | `(32, 64)` · `(96, 192)` |
| Lake | `(16, 208)` · `(64, 224)` · `(88, 16)` |
| City | `(16, 104)` |
| Town | `(104, 152)` · `(112, 232)` |
| Fields | `(56, 176)` · `(104, 112)` · `(48, 136)` · `(64, 96)` · `(72, 56)` · `(8, 160)` · `(48, 24)` · `(120, 72)` |
