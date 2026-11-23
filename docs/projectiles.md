# Projectiles, Ammo, Tracers & Smoke (`projectiles.md`)

Shipped in `fpilot.prg` (`c64o/gun.{h,cc}`, `c64o/gun_asm.{h,cc}`). Handles player and opponent gunfire, hit/near-miss/collision detection, finite ammo and debrief tallies, gun audio, yellow tracer sprites, and black smoke/ground-impact puffs.

---

## 1. Overview

- **Trigger:** `Space` fires alternating converging wing streams (armed only after `Space` is released once on mission start via `_gun_armed`, preventing accidental fire from menu/map exits).
- **Pool:** 8 shared round slots (`kRoundMax = 8`) for both player and opponent rounds.
- **Sprites:** At most **2 viewport stack sprites** (1 tracer + 1 smoke/ground puff) using **zero new sprite blocks** (reusing gauge arms 96–127 and cloud rungs 1–4).

---

## 2. Round state & physics (`gun.cc`, `gun_asm.cc`)

All round positions and velocities are stored **relative to the player's eye** as `int16_t` in **1/16 m** (range ±2,048 m), packed in flat `__striped` arrays (`_round_p[24]`, `_round_v[24]`, 48 B each), plus `gun_z_t _gun_z` (14 B zeroed by one loop in `gun_reset()`), `_round_owner[8]` (8 B), `_gun_eye[3]` (12 B), `_gun_prev[6]` (12 B), `_gun_last` (1 B), and `_gun_puff[3]` (6 B).

- **Eye tracking (`_gun_track_all()`):** Steps `_gun_eye[0..2]` across `flight_eye_*` in exact 32-sub-unit quanta (drift-free) and subtracts eye motion from every active round.
- **Ballistics:**
  - Muzzle speed `kGunMuzzle = 768` (**48 m/step**, chosen to keep rounds visible for several frames at ~8–10 fps).
  - Lifetime `kRoundLife = 12` whole steps (~600 m range).
  - Player firing interval `kGunInterval = 2` whole steps, alternating left/right wings: `kGunAhead = 320` (20 m forward), `kGunWing = 64` (±4 m span), `kGunDrop = 24` (1.5 m below eye), `kGunConverge = 15` (`768 / 50`, harmonised at **200 m**), plus player motion `>> 5`.
- **Enemy slot allocation (`_gun_store_enemy_round()`):** Picks a free slot or evicts the oldest enemy round — never displaces a player round.

---

## 3. Hit, near-miss & collision tests

- **Swept hit & provoke (`_gun_hits(r, box)`):** Scales velocity by `flight_step_shift`, applies a whole-step reach rejection, then tests **4 substeps** against an AABB:
  - Hit box: `kRoundHit = 160` (**10 m** half-side) for both opponents and the player.
  - Near-miss provoke box: `kRoundNear = 640` (**40 m** half-side) wakes calm `Provoked` opponents.
- **Mid-air collision (`_gun_collides()`):** Tests both current opponent offset and midpoint with `_gun_prev` against `kGunCollide = 192` (**±12 m** box) → `opponent_collide(i)`.
- **Exact `gun_step()` ordering** (prevents newly fired enemy rounds from hitting their own shooter or skipping a check):
  1. Raise `FLIGHT_EV_GUN` if trigger held, armed, and ammo > 0.
  2. `_gun_track_all()` (shift all live rounds by eye delta).
  3. Test live enemy rounds against player (`damage_hit(++gun_player_hits)`).
  4. Poll `opponent_fires()` and store new enemy rounds.
  5. `_gun_hit_opponents()` (mid-air collisions + player rounds vs opponents; 5 hits down an opponent into `_opponent_fall()`).
  6. `_gun_step_rounds()` (advance positions; `z < 0` spawns ground puff `kPuffFrames = 10` and retires round; age out expired rounds).
  7. `damage_step()` (oil-leak countdown).
  8. Fire next player round if trigger held, armed, interval elapsed, and ammo > 0.

---

## 4. Tracer rendering (`gun_add_sprites()`)

- **Selection (`_gun_shown()`):** Displays `_gun_last` (the player's newest live round) if active, else the youngest enemy round.
- **Staged projection (`_gun_view_round()` + `_gun_vel_round()`):** Row-by-row camera transform (`front` → `x > 64`, `up` → `|z| < x` + `vec_frac8`, `left` → `|y| < x` + `vec_frac8`), sharing `frac_d = x` and deferring velocity projection until the round passes frustum culling.
- **Orientation (`_gun_dir()`):** Computes the exact screen-space tangent `(y·vx − vy·x, z·vx − vz·x)` and quantises into 16 arm directions using `kTracerTan[4] = {25, 78, 137, 210}`.
- **Size & colour:**
  - `x < kTracerExpand` (`1200`, ~150 m): long arm + `kSpriteFlagExpandX` (`dx >>= 1`).
  - `x < kTracerLong` (`2400`, ~300 m): long arm (1×).
  - Farther: short arm (1×).
  - All tracers (player and opponent) draw in **`kColorYellow`**.

---

## 5. Smoke & ground-impact puff (`_gun_smoke`)

Draws at most **1 dithered sprite** (`kSpriteFlagExpandX | kSpriteFlagAlignDither`) reusing cloud rungs 1–4 via `__striped kSmokeRungX[3] = {2344, 1832, 1496}` (1/8 m: rung 4 `< 186 m` down to rung 1 `> 292 m`):
- Prefers active opponent smoke (`opponent_smoke()`: **14 m** behind heading, 4 m up; `kSmokeHit = 14` frames on hit, `kSmokeDown = 35` frames on kill/fall). Transitions **`kColorYellow` → `kColorOrange` → `kColorBlack`** over the first two frames of a hit or ground impact, leaving the opponent's own sprite colour unchanged.
- Falls back to ground-impact puff `_gun_puff` (`kColorBlack`, `kPuffFrames = 10` frames, 4 m up) when no aircraft is smoking.

---

## 6. Ammo, sound & tallies

- **Ammo (`gun_ammo[3]`):** Three decimal digits starting at **`600`** (60 trigger rounds × 10 displayed on `fpanel` via `gfx_update_ammo()` when `!gun_ammo_shown`; **`000`** in mission 09 `kMissionUnarmed`). Refilled by `gun_rearm()` when halted on a runway.
- **Tallies (`gun_tally[10]`):** Two 5-digit decimal counters (`kGunTallyFired` and `kGunTallyHits`, counted in tens) read by `debrief.cc` ([opponent_ai.md](opponent_ai.md) §8).
- **Sound:** `FLIGHT_EV_GUN` triggers voice-3 decaying noise (`V3_GUN`, [sound.md](sound.md) §6).

---

## 7. Sprite budget

`gun_tracer_up()` and `gun_puff_up()` report active sprites to `_opponent_cap()` ([planes.md](planes.md) §13) so both opponents cap to 2 sprites when tracers/smoke/tail fin would otherwise exceed the 7-slot (front/side) or 6-slot (back) stack.

---

## 8. 6502 assembly, RAM & tests (pass 152)

- **`c64o/gun_asm.cc` (`#ifndef __OSCAR64__` C mirrors in `gun.cc`):** `_gun_track_all`, `_gun_rel_from`, `_gun_hits`, `_gun_collides`, `_gun_store_enemy_round`, `_gun_step_rounds`, `_gun_shown`, `_gun_dir`, `_gun_view_round`, `_gun_vel_round`.
- **Footprint:** **2,673 B** main (2,654 B code + 19 B data), **162 B** `bss2`, **56 B** `tscratch`, **2 B** ZP.
- **Tests:** `c64o/test/gun_test.cc`, `mission_test.cc`, and on-target 6510 `target_test.cc` test 18 (`gun_asm.cc`).
