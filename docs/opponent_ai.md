# Opponent AI, Damage & Debrief (`opponent_ai.md`)

Shipped in `fpilot.prg` (`-D__FPILOT__`, guarded by `__ENABLE_OPPONENT__`). Covers opponent roles, missions/waves, flight model, steering/strafing/break-off, enemy guns, collisions/falling, player damage (`damage.cc`), and the mission debrief screen (`debrief.cc`). See [planes.md](planes.md) for 3D sprite rendering and [projectiles.md](projectiles.md) for the shared projectile/smoke system.

---

## 1. Overview

`fpilot.prg` runs up to **two active opponents** at once across **10 combat missions** (including multi-wave missions of up to 3 pairs = 6 enemies):
- **Roles:** **Fighter** (fast, agile, hunts/strafes, forward nose gun, dark grey near) and **Bomber** (slower, wider turns, aimed rear gunner, brown near).
- **Behaviours:** `Path` (follows waypoints), `Provoked` (flies path until hit, near-missed within 40 m, or player closes within 2 km, then hunts), `Hunt` (pursues player immediately), `Pass` (strafes parked airfield then transitions to hunt), plus `kOpponentStray` flag (gives up pursuit past 2.5 km).

---

## 2. Architecture & files

- `c64o/opponent.{h,cc}` — mission/wave loading, state machine, flight model, steering, gun triggers, falling, smoke state, sprite submission.
- `c64o/opponent_asm.{h,cc}` — 6502 `__asm` leaf/hot helpers (`#ifndef __OSCAR64__` C mirrors in `opponent.cc`):
  - `_opponent_world_delta` (24.8 world delta, x/y wrap, near/far scale directly into ZP `vec_v`)
  - `_opponent_toward` (signed wrapped horizontal delta)
  - `_opponent_steer` (6-bit in-place halving + exact `cross`/`dot` via `vec_mul8` in ZP, dropping `opponent_step@stack` to 0 B)
  - `_opponent_pos`, `_opponent_latch_shown`, `_opponent_copy_cs` (indexed copies)
  - `_opponent_move`, `_opponent_ahead`, `_opponent_axes`, `_opponent_cull_slot` (position integration, camera-space body axes, and staged frustum cull via `vec_asm.cc`'s `vec_mul_ab`)
  - `_opponent_ai_step_asm`, `_opponent_fly_and_move_asm`, `_opponent_fall_asm`, `_opponent_cap_asm` (per-slot steering/strafe/floor/break-off/guns/turn/bank, speed/climb/ground-crash/horizontal advance, shot-down dive/spin, and 2-opponent sprite-budget depth hysteresis/capping)
- `c64o/fmission.cc` — 10 `fpilot` mission briefings, start poses, navpoints, and wave assignments.
- `c64o/damage.{h,cc}` — player armour, 5 progressive system failures, red border hit flash, and shot-down dive trigger.
- `c64o/debrief.{h,cc}` — post-flight debrief screen shown on `Q`.

---

## 3. Data, roles & missions

### `opponent_path_t::kind` bit packing

| Bits | Field | Values |
| --- | --- | --- |
| `0..1` | Path kind | `0` None, `1` Loop, `2` OneWay, `3` Parked |
| `2` | Role | `0` Fighter, `1` Bomber |
| `3..4` | Behaviour | `0` Path, `1` Provoked, `2` Hunt, `3` Pass |
| `5` | `kOpponentStray` | `1` = break off pursuit past `kOpponentGiveUp = 5000` half-metres (2.5 km) |
| `6..7` | Start heading | Quarter turns left of north (`0` N, `1` W, `2` S, `3` E) |

### Tables & wave progression

- `kOpponentRows = 13` path/pair definitions (`kOpponentPaths`, `kOpponentPairs`), sharing `kOpponentWpCount = 20` waypoints.
- `kOpponentWaves[17]` sequences pairs per mission (bit 7 = `kOpponentLastWave`; bits 4–6 encode partial kill quota `_opponent_need`). The next wave spawns once both active slots are downed/gone and smoke has cleared.

### Role parameters

| Parameter | Fighter | Bomber |
| --- | --- | --- |
| Speed range (half-m / 16-step scale) | `1300 .. 2800` | `1100 .. 2000` |
| Turn rate (`kOpponentTurn = 8` ≈ 1.8°/step) | `1×` on path, **`2×`** hunting | `1×` |
| Visual bank angle (after 2 turn steps) | 30° path / **60°** hunter | 30° |
| Near sprite colour (`d >= 8`) | `kColorDarkGray` | `kColorBrown` |
| Armament | Forward nose gun (burst of 3) | Aimed rear gunner (burst of 3) |

### Complete 10-mission table (`fmission.cc` + `opponent.cc`)

| # | Mission | Opponents & Waves | Special rules / Goal |
| ---: | --- | --- | --- |
| 01 | Recon Intercept | 1 Bomber (`OneWay`, 5,000 ft, speed 1,750) | Fails if bomber escapes |
| 02 | Bomber Raid | 2 Bombers (`OneWay`, 3,000/3,500 ft, speed 1,500) | Fails if bombers reach city |
| 03 | Ambush | 2 Parked at Runway 2 (`z = 5 m`) | Destroy parked targets + land |
| 04 | Scramble | Wave 1: 2 `Pass` fighters (2,000 ft); Wave 2: 2 `Hunt` fighters (W) | Starts parked on Runway 1; survive strafe + down waves |
| 05 | Head On | Wave 1: 2 `Hunt` (2.3 km N); Wave 2: 2 `Hunt` (S) | Down both hunter waves + land |
| 06 | Big Week | 3 waves × 2 Bombers over city | Quota: down ≥ 3 of 6 + land |
| 07 | Way Home | 2 `Provoked + Stray` patrols in turn | Starts at wp 1; goal is land at Home Base |
| 08 | Turkey Shoot | 3 waves × 2 `Path` fighters on Runway 2 circuit | Quota: down ≥ 5 of 6 + land |
| 09 | Unarmed Escape | 2 `Hunt + Stray` fighters (2 km ahead, ±1 km flanks) | `kMissionUnarmed` (0 ammo); outrun & land |
| 10 | Combat Patrol | 2 `Hunt` fighters (6,000 ft, speed 2,400, 1.2 km behind) | Down hunters + land |

---

## 4. Opponent flight model

- **Climb levels `−3..+3`:** Vertical slopes `1/8`, `1/4`, `1/2` (~7°, 14°, 27°).
- **Speed dynamics:** `kOpponentClimbCost = 8` (decelerates in climb / accelerates in dive proportional to level) and `kOpponentLevelAccel = 8` toward cruise speed; `kOpponentPathBand = 40` (20 m altitude deadband on path).
- **Attitude smoothing:** Bank (30°/60°) and pitch (`kOpponentPitchSin/Cos`, 8 entries including `kOpponentDive = 4` at ~37° nose down for falling) engage after 2 consecutive whole steps in the same direction, avoiding silhouette twitch.
- **Screen axes (`_opponent_axes()`):** Composes level heading with pitch and bank (`_opponent_rot()`) into screen-space `(y, z)` rows only.

---

## 5. Steering, strafing & break-off

- **Horizontal steering (`_opponent_toward()` + `_opponent_steer()`):** Computes wrapped `(dx, dy, dz)`, halves in-place until `|d| <= 63`, and computes exact `cross` and `dot` against the opponent's heading (`kOpponentReach = 512` = 256 m waypoint capture radius).
- **Vertical steering (`_opponent_want()`):** Picks desired climb level `−3..+3` from vertical slope thresholds `1/16`, `3/16`, `3/8`.
- **Altitude floors:** Normal floor `kOpponentFloor = 120` (60 m, margin `kOpponentFloorMargin = 16` = 8 m); strafing pass drops to `kOpponentStrafeFloor = 60` (30 m). After a strafe pull-up, the fighter extends straight until `z >= 60 m` and horizontal range `cut > 5` (~1 km) or the player takes off.
- **Collision break-off (`_opponent_break`):** When airborne and ahead of the opponent within `cut <= 1` (~63 m), or head-on within `cut == 2` (~127 m), the opponent climbs/dives at level ±3 away from the player's altitude **and** turns away (`cross = -cross; dot = -1`).

---

## 6. Opponent guns & provoking

- **Fire cycle:** `kOpponentGunCycle = 26` whole steps; fires a **3-round burst** on steps 0, 2, 4.
- **Fighter nose cone:** Active when hunting and not pulling up, `cut <= 4` (~500 m), `dot > 0`, `|cross| < dot >> 3` (~±7°), and `want == climb`.
- **Bomber rear gunner (`_opponent_gunner`):** Active on **any flying bomber** (including `Path` bombers) when player is behind: `1 <= back < 1200` (600 m), `|cross| < back` (±45° horizontal), `-back * 3/16 <= dz < back` (−10° to +45° vertical). Aims along `vec_normalize(d / (2 * back))`.
- **Scatter & velocity:** Both add `kOpponentScatter[8]` (`±4..±28` in 8.8 via 8-bit xorshift `7,5,3` `opponent_random()`), scaled to `kGunMuzzle = 768` (48 m/step), with no own-velocity addition. `gun_step()` polls `opponent_fires(i, _gun_v)` and calls `_gun_fire_from(i)`.
- **Provoking:** A `Provoked` opponent switches to `Hunt` on a hit, a 40 m near miss (`kRoundNear`), or player closing inside `kOpponentSpot = 4000` half-metres (2 km).
- **Radio target callouts (`opponent_callout()` in `sim.cc`):** Called once per rendered frame after `msg_update()`, using `_opponent_in_view` (set in `_opponent_draw()` whenever `sprites_stack_add()` accepts a target into the viewport). Immediately warns `"CHECK SIX!"` (45-frame cooldown) when an out-of-view airborne opponent is directly behind (`6 o'clock`, within 2 km); otherwise, whenever no live target has been visible in the viewport for 50 consecutive frames (`kCalloutUnseen = 50`) and no other message is active, calls the nearest live opponent's clock bearing and altitude band (e.g. `"TARGET 4 O'CLOCK HIGH"` / `"LOW"` / level, ±80 m deadband).

---

## 7. Collisions, ground impact & falling

- **Mid-air collision:** Checked in `gun_asm.cc` (`_gun_collides()`) against `kGunCollide = 192` (±12 m box) at both current position and midpoint with previous step. Triggers `opponent_collide(i)` → destroys opponent slot and sets player `FLIGHT_CRASH_COLLISION`.
- **Ground impact (flying opponent):** `z <= 0` destroys the opponent and shows `"ENEMY CRASHED"` (does not increment gun kills).
- **Shot-down fall (`_opponent_fall`):** On the 5th hit (`kOpponentHitsToDown = 5`), increments `opponent_shot_down` and displays `"OPPONENT SHOT DOWN"`. Parked targets go down immediately; airborne targets enter a ~37° dive (`kOpponentFallZ = -192`, 50% chance of spin at quarter-turn/step & 60° bank) trailing black smoke (`kSmokeDown = 35`) until `z <= 0`.

---

## 8. Player damage (`damage.cc`) & debrief (`debrief.cc`)

### Progressive player damage (`c64o/damage.{h,cc}`)

Enemy hits call `damage_hit(++gun_player_hits)`:
1. **Hit feedback:** Starts a 6-frame red border flash (`kDamageFlash = 6` via `damage_frame()`, red on odd counts, black at 0) and raises `FLIGHT_EV_HIT`.
2. **Armour (`damage_armour`):** First **3 or 4 hits** (`kDamageArmour = 3` + `opponent_random() & 1`) are absorbed by armour with no system failure.
3. **System failures (hits 1–4 past armour):** Each hit picks a distinct failure bit in `damage_bits` and posts a HUD warning:

| Bit | Mask | Effect in `flight.cc` |
| --- | --- | --- |
| `kDamageEngine` | `0x01` | Caps max throttle |
| `kDamageFuel` | `0x02` | Doubles fuel burn rate (`damage_burn()`) |
| `kDamageOil` | `0x04` | Counts down `kDamageOilSteps = 200` whole steps (~32 s), then sets `kDamageSeized` (`0x20`, kills engine) |
| `kDamageHydraulics` | `0x08` | Locks flaps and landing gear |
| `kDamageControls` | `0x10` | Skips every other control input (`damage_toggle`) |

4. **Shot down (5th hit past armour, 8–9 hits total):**
   - **Airborne:** Sets `kDamageDown` (`0x40`) + 50% `kDamageSpin` (`0x80`), shows `"SHOT DOWN!"`, locks controls, and `_flight_fall()` dives to `front.z = -222` until ground impact (`FLIGHT_CRASH_SHOT_DOWN`).
   - **On ground:** Crashes immediately (`FLIGHT_CRASH_SHOT_DOWN`) and raises `FLIGHT_EV_CRASH`.

### Mission debrief (`c64o/debrief.{h,cc}`)

Leaving flight with `Q` in `fpilot` opens the debrief screen (`screen_begin_text_page()`, exits on `Q` or Port 2 Fire):
- **Row 2:** Centred mission title (`kMissionTitles`).
- **Row 5 (`GOALS`):** `NOT COMPLETED`, `COMPLETED`, or `COMPLETED, LANDED`.
- **Row 6 (`YOUR AIRCRAFT`):** `UNDAMAGED` (0 hits), `A FEW HOLES` (≤ `damage_armour`), `DAMAGED` (1–2 failures), `BADLY DAMAGED` (3–4 failures), `SWISS CHEESE` (`FLIGHT_CRASH_SHOT_DOWN` or `damage_down()`), or `WRECKED` (other crash).
- **Rows 8–9 (`ROUNDS FIRED` / `ROUNDS ON TARGET`):** 5-digit `gun_tally` (counted in tens).
- **Row 10 (`ENEMIES SHOT DOWN`):** `opponent_shot_down` plane glyphs (`'*'` in custom font; gun kills only — excludes ground crashes & collisions) or `'0'`.
- **Row 11 (`ACCURACY`):** Integer hit rate (`0..100`, or `--` when no rounds fired).
- **Row 13 (`RATING`):** `ACE` (completed + landed, undamaged, ≥25% accuracy or unarmed), `VETERAN` (completed + landed, ≤ armour hits), `SURVIVOR` (completed + landed with failures), `PASS` (goals completed, did not land), `INCOMPLETE` (returned early), or `FAILED` (crashed/shot down).

---

## 9. Memory, timing & tests (pass 152)

| Subsystem | Main (Code + Data) | `bss2` | `tscratch` | ZP |
| --- | ---: | ---: | ---: | ---: |
| `opponent.cc` + `opponent_asm.cc` | **5,953 B** (5,514 + 439) | 260 B | 72 B | 13 B |
| `damage.cc` | **246 B** (232 + 14) | — | — | 1 B |
| `debrief.cc` | **446 B** (312 + 134) | — | — | 0 B |

- **Tests:** `c64o/test/opponent_test.cc`, `mission_test.cc`, `fpilot_test.cc`, `gun_test.cc`, and on-target 6510 `target_test.cc` tests 17, 26, and 31 (`opponent_asm.cc`).
- **Combat frame timings:** see [framerate.md](framerate.md) (`scramble` 153k, `shootdown` 149k, `headon` 93k, `chase` 118k, `strafe` 151k cycles).
