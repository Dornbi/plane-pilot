# Flight Model (`c64o/flight.cc`)

## 1. Overview & architecture constraints

Both `ppilot.prg` and `fpilot.prg` ship with the **arcade flight model** (`FLIGHT_AOA ?= 0` in [c64o/Makefile](../c64o/Makefile)). An optional **angle-of-attack (AoA) model** (`FLIGHT_AOA=1`, `-D__FLIGHT_AOA__`, tracked by `c64o/.flight_model`) remains in `flight.cc` (covered by `flight_test` — 63 arcade / 64 AoA tests — and `fpilot_test`), but was reverted because it cannot fly a loop (§9).

### Fixed-point scales & conventions

| Quantity | Type / Format | Range & Physical Meaning |
| --- | --- | --- |
| Position `flight_eye_{x,y,z}` | `int32_t` (24.8) | Half-metres (`>> 16` = 128 m world unit, `>> 9` = 1 m; `Z = 0x2000` = ground floor `kFlightMinEyeZ`) |
| Airspeed `flight_speed` | `int16_t` (8.8) | `0 .. kMaxSpeed` (`0x0F00` = 3840); step displacement is `flight_speed << 1` (256 = 1 m/step) |
| Vertical speed `flight_vspeed` | `int16_t` (8.8) | Signed vertical displacement per whole step |
| Throttle `flight_throttle` | `uint8_t` | `0 .. kMaxThrottle` (`0x18` = 24) |
| Orientation `flight_cam` | `mat3_t` (`front`, `left`, `up`) | Orthonormal 8.8 direction cosines (`256 = 1.0`), maintained by `vec_orthonormalize()` |
| Fuel `flight_fuel` | `int32_t` | Full tank `kFlightFullFuel = 0x21FFF` |

### Hardware constraints

- **No floating point and no runtime 16-bit division** (enforced by `check_mul_div.py`). All products use quarter-square `vec_fastmul8p8` / `vec_fastsqr8p8`, incremental control rotations use `vec_rotate_step()` (6 multiplies, 6-byte table), and horizontal turns use `vec_turn3_xy()` (6 multiplies).
- **Cycle budget:** One PAL frame is 19,705 cycles. `flight_advance()` runs ~5,000 cycles straight and level and ~12,000–16,800 cycles when re-orthonormalising (`vec_orthonormalize()` is ~5,750–9,700 cycles).

---

## 2. Core aerodynamics (shipping arcade model)

### 2.1 Level flight & lift

Lift depends on airspeed squared, upright component `up.z`, altitude air density, and flaps — there is **no angle-of-attack term** (`front.z` does not enter lift):

```text
raw_lift = vec_fastmul8p8(speed_sqr >> 2, up.z)
lift     = vec_fastmul8p8(raw_lift, density)
if (flight_flaps) lift += lift >> 1
```

Trim lift is `kFlightTrimLift = 0x1000` (4096), corresponding to level upright clean trim speed `0x0800` (2048). The lift deficit is **one-sided**:

```text
deficit = kFlightTrimLift - lift
if (deficit > 0):
    sink_penalty = deficit >> 4
    induced_drag = deficit >> 10
else:
    sink_penalty = 0
    induced_drag = 0
```

Above `0x0800`, holding `front.z = 0` flies level (`vspeed = 0`) regardless of throttle. Below `0x0800`, positive pitch is required to cancel `sink_penalty`.

Measured clean sea-level trim (arcade):

| Throttle | % | Pitch (`front.z`) | Speed |
| ---: | ---: | ---: | ---: |
| 11 | 46% | 20 (~4.5°) | 1535 |
| 12 | 50% | 8 (~1.8°) | 1846 |
| 16 | 67% | 0 (0°) | 2111 |
| 20 | 83% | 0 (0°) | 2290 |
| 24 | 100% | 0 (0°) | 2509 |

Minimum level-flight throttle: **46%** (thr 11) clean, **37%** (thr 9) with flaps.

### 2.2 Stall & recovery

Airborne stall speed rises with altitude penalty:

```text
stall_speed = base_stall_speed + (alt_penalty << 1)
```

| Configuration | `base_stall_speed` |
| --- | ---: |
| Clean | `0x0400` (1024) |
| Flaps, upright (`up.z >= 0`) | `0x0340` (832) |
| Flaps, inverted (`up.z < 0`) | `0x0480` (1152) |

- `flight_stall = (!model_on_ground && flight_speed < stall_speed)`; `flight_stall_warning()` returns `flight_stall` (arcade).
- **Stall break:** Drops the nose toward the ground:
  - If `front.z > kFlightMaxStallPitchZ` (224, ~61°), pitches through `vec_rotate_step(up.z < 0 ? PITCH_UP : PITCH_DOWN)` to avoid degenerating `front`.
  - Otherwise subtracts `max(whole_step ? 1 : 0, flight_step_s(stall_speed - flight_speed) >> 5)` from `front.z` and re-orthonormalises.

### 2.3 Climb & altitude density

- Vertical speed: `flight_vspeed = vec_fastmul8p8(front.z, flight_speed) - sink_penalty`.
- Gravity acceleration along nose: `flight_speed -= flight_step_s(front.z) >> 3`.
- **Altitude density:** Flat (`density = 256`) up to `Z = 0x080000` (4,000 ft); above that:
  - `alt_penalty = min(128, (Z - 0x080000) >> 12)`
  - `density = 256 - alt_penalty` (halved at service ceiling)
  - Scales both thrust (`vec_fastmul8p8(throttle, density)`) and lift, and adds `alt_penalty << 1` to `stall_speed`.

### 2.4 Drag

Total drag subtracted per step (`speed_sqr = vec_fastsqr8p8(flight_speed)`):

- **Parasite drag:** `speed_sqr >> 10`
- **Landing gear:** `+ (speed_sqr >> 12)` (+25% parasite)
- **Flaps:** `+ (speed_sqr >> 12)` (+25% parasite)
- **Bank drag (airborne):** `vec_fastsqr8p8(left.z) >> 5`
- **Lift-deficit drag:** `deficit >> 10` when `deficit > 0`

---

## 3. Maneuvering flight

### 3.1 Banked turns & loops

- Horizontal turn rate is purely bank-dependent (speed-independent): `rot = flight_step_s(left.z) >> 5` (`|rot| <= 8`), applied via `vec_turn3_xy(rot, &flight_cam)`.
- Banking reduces `up.z`, lowering lift, creating sink penalty, deficit drag, and bank drag (`left.z^2 >> 5`).
- A sustained pull from cruise cleanly completes a full 360° vertical loop (~108 whole steps).

### 3.2 Inverted flight (`up.z < 0`)

Inverted, `lift` is negative, so `deficit = kFlightTrimLift - lift` is roughly doubled. Sustaining level inverted flight requires nose-up pitch relative to the horizon and high throttle:

- Level inverted floor: **71%** (thr 17, `front.z = 82`, speed 1049).
- Thr 18: `front.z = 78`, speed 1144. Thr 24 (100%): `front.z = 68`, speed 1698.

---

## 4. Configuration & drag devices

### 4.1 Landing gear (`G`)

- Adds `+25%` parasite drag (`speed_sqr >> 12`).
- **Ground lockout:** `FLIGHT_INPUT_TOGGLE_GEAR` is ignored while `model_on_ground == true` (cannot retract gear on the runway).

### 4.2 Flaps (`F`)

- Adds `+25%` parasite drag (`speed_sqr >> 12`).
- Applies signed `lift += lift >> 1` (+50% lift upright, deepens negative lift when inverted).
- Changes `base_stall_speed` to `0x0340` (832) upright and `0x0480` (1152) inverted.

---

## 5. Ground interaction & touchdown envelope

### 5.1 On-ground physics

- Clamped to `Z = kFlightMinEyeZ` (`0x2000`) with `front.z >= 0`.
- Wings are levelled only when `left.z != 0` (avoiding a `vec_normalize` heading drift ratchet when already level).
- `J`/`L` and `A`/`S` steer yaw while `flight_speed > 0`.
- Rolling friction at zero throttle subtracts `2` per `whole_step`; wheel brakes (`B`, ground-only) subtract `32` per input.
- **Refuel & rearm on halt:** Transitioning from moving to stopped (`model_on_ground && flight_speed == 0`) refills `flight_fuel = kFlightFullFuel` (`0x21FFF`) and in `fpilot` calls `gun_rearm()` (0 rounds in unarmed mission 09).

### 5.2 Takeoff

On the ground, `PITCH_UP` is gated by `flight_speed > stall_speed` (`0x0400` clean / `0x0340` flaps). Once above stall speed, pulling up rotates in a loop (up to `kFlightMaxRotateSteps = 16` iterations) until `front.z >= kFlightRotatePitchZ` (`47`, ~10.6°), clears `model_on_ground`, and lifts off.

### 5.3 Touchdown, crash envelope & approach advisories

Evaluated whenever `Z <= kFlightMinEyeZ`. Speed limit is `kFlightMaxLandingSpeed = 0x0A00` (2560) on touchdown and `kFlightMaxGroundSpeed = 0x0D00` (3328) while rolling (full-throttle ground top speed is 2290).

Faults are checked in strict priority order (`_flight_landing_fault()`):

| Priority | Fault condition | `kFlightFaultText` | Checked on |
| ---: | --- | --- | --- |
| 1 | Not over a runway (`!_flight_on_runway()`) | `NOT ON RUNWAY` | Touchdown only |
| 2 | `up.z < 0` | `INVERTED` | Touchdown & roll & advisory |
| 3 | `!flight_gear` | `GEAR RETRACTED` | Touchdown & roll & advisory |
| 4 | `flight_vspeed < kFlightMaxLandingVSpeed` (`-0x0120` = -288) | `SINK RATE` | Touchdown & advisory |
| 5 | `|left.z| > 32` (~7.2°) | `BANK ANGLE` | Touchdown & roll & advisory |
| 6 | `front.z < -32` (~-7.2°) | `PITCH TOO LOW` | Touchdown & roll & advisory |
| 7 | `front.z > 64` (~14.5°) | `PITCH TOO HIGH` | Touchdown & roll & advisory |
| 8 | `flight_speed > speed_limit` | `TOO FAST` | Touchdown & roll & advisory |

- On fault at `Z <= 0x2000`, status becomes `FLIGHT_CRASH_*`, HUD shows `CRASHED: <fault>`, and `FLIGHT_EV_CRASH` fires.
- **`fpilot` combat crashes:** `FLIGHT_CRASH_SHOT_DOWN` (`SHOT DOWN`) and `FLIGHT_CRASH_COLLISION` (`COLLISION`).
- **Clean touchdown:** Sets `model_on_ground = true`, rates touchdown softness from sink rate (`SMOOTH LANDING` `>= -96`, `FIRM LANDING` `>= -192`, `HARD LANDING` `>= -288`), zeroes `flight_vspeed`, snaps `front.z = 0`, and emits `FLIGHT_EV_TOUCHDOWN`.
- **Approach warnings:** While airborne with `flight_vspeed < 0` and `Z <= 0x4000`, `_flight_landing_fault(kFlightMaxLandingSpeed, false)` runs with **`check_runway = false`** (so low passes off-runway do not nag) and displays `WARNING: <fault>`.

---

## 6. Edge cases, fuel, pause & combat hooks

### 6.1 Vertical flight & speed clamp

`flight_speed` clamps to `[0, kMaxSpeed (0x0F00 = 3840)]`. Vertical dive terminal speeds: **3693** clean full throttle, **3319** gear down, **2710** idle.

### 6.2 Fuel & gliding

- Burns `flight_throttle` per `whole_step` (doubled when `kDamageFuel` is active in `fpilot`).
- Reaching `0` displays `OUT OF FUEL` once and forces `flight_throttle = 0`.
- Optimal power-off glide (`front.z = -50`, ~-11°): **6.13:1** at sea level (**4.982:1** at high altitude `Z = 0x0400000` in `flight_test`).

### 6.3 Pause, completion, events & `fpilot` damage hooks

- **Control lockout:** While paused, crashed (`flight_status >= FLIGHT_CRASH_ROLL`), or falling (`damage_down()`), all inputs except `FLIGHT_INPUT_TOGGLE_NAV` are ignored.
- **Mission completion (`FLIGHT_MISSION_COMPLETED`):** Does **not** stop physics; the player can keep flying and land.
- **Events & trail:** One-shot events (`FLIGHT_EV_TOUCHDOWN`, `GEAR`, `FLAPS`, `CRASH`, `GUN`, `HIT`) publish into `flight_events` alongside incrementing `flight_gen` for `sound.cc` ([sound.md](sound.md) §5). The map flight trail records 128 positions (`kFlightPathLen`) on `whole_step` into sprite scratch RAM.
- **Damage hooks (`fpilot`):** `damage.h` gates throttle (`kDamageEngine`), doubles fuel burn (`kDamageFuel`), cuts engine on oil seizure (`kDamageSeized`), locks flaps/gear (`kDamageHydraulics`), skips every other control step (`kDamageControls`), and hands shot-down airborne aircraft to `_flight_fall()` (dives to `front.z = -222` with optional spin until `FLIGHT_CRASH_SHOT_DOWN` on ground impact; see [opponent_ai.md](opponent_ai.md) §8).

---

## 7. Summary matrix (shipping arcade model)

| Metric | Clean | Flaps / Gear |
| --- | --- | --- |
| Stall speed (sea level, upright) | `0x0400` (1024) | `0x0340` (832) flaps / `0x0480` (1152) flaps inverted |
| Trim speed (`front.z = 0`) | `0x0800` (2048) | — |
| Top level speed (sea level, thr 24) | 2509 | — |
| Min level-flight throttle | 46% (thr 11) | 37% (thr 9, flaps) |
| Min inverted level throttle | 71% (thr 17) | — |
| Ground full-throttle top speed | 2290 | — |
| Touchdown speed / sink limits | `0x0A00` (2560) / `-0x0120` (-288) | — |
| Optimal glide ratio (`front.z = -50`) | 6.13:1 (sea level) / 4.982:1 (high alt) | — |
| Terminal vertical dive speed | 3693 (thr 24) / 2710 (idle) | 3319 (gear down, thr 24) |
| Vertical loop from cruise | ~108 whole steps | — |

---

## 8. Timebase, substepping & time compression

### One step per rendered frame

`sim_run()` runs **one flight step per rendered frame** (or a fixed 2 or 4 under time compression). An earlier raster-clock catch-up loop (`kFlightFramesPerStep`) was reverted because a slow frame provoked two consecutive model steps, creating a spiral of uneven frame pacing; `kFlightFramesPerStep` remains declared in `flight.h` but is unused.

### Boot-time SuperCPU substepping (`cpu_step_shift`)

`ppilot.cc` calls `cpu_probe()` before `mem_init()` and `flight_set_step_shift(cpu_step_shift)` afterwards (`shift = 0` on a stock 1 MHz C64, `shift = 2` on a 20 MHz SuperCPU; see [supercpu.md](supercpu.md)):

- Sets `flight_step_shift` (`0..2`), `kFlightSubstepMask` (`(1 << shift) - 1`), and calls `vec_set_rotation_shift(shift)` (rescaling the 6-byte `vec_rotate_step()` angle table).
- Continuous per-step accelerations and displacements scale via `__noinline flight_step_s(v)` (`v >> shift`).
- **Two-stage fine control (`flight_fine_input`):** On a stock C64 (`flight_step_shift == 0`), `sim.cc` sets `flight_fine_input = true` on the first step a roll/pitch/yaw key or Port 2 joystick direction is held, halving that step's rotation angle for fine aiming and runway alignment; holding continues at full rate.
- Discrete/non-divisible terms (ground rolling friction `-2`, stall break floor `1`, fuel burn, map trail sampling) run only when `whole_step = (model_substep == 0)`.

### Time compression (`T`)

Pressing `T` in flight cycles `sim_time_steps` through `1 → 2 → 4 → 1` (`TIME COMPRESSION 2X`, `4X`, `OFF`). Each rendered frame executes `sim_time_steps` full model steps (`flight_advance()`, plus `opponent_step()` and `gun_step(trigger)` in `fpilot`), re-polling held keys before each extra step and ORing `flight_events` across the steps. Resets to `1` on mission start, `R`, and pause/resume (`P`).

---

## 9. Optional angle-of-attack model (`FLIGHT_AOA=1`)

Compiled into `flight.cc` under `-D__FLIGHT_AOA__` (`make prg FLIGHT_AOA=1`, tested via `make -C c64o/test test-both`).

### Key differences from arcade

- Tracks flight-path sine `flight_gamma` and angle of attack `alpha = front.z - flight_gamma` (world-plane approximation).
- Lift coefficient piece-wise linear with camber and flaps up to `kFlightAlphaStall = 56`, peaking at `kFlightClPeak = 896`, with `kFlightCamberCl = 128`, `kFlightFlapDeltaCl = 512`, `kFlightLiftSat = 4096`, and induced drag scaled by `kFlightInducedShift = 6` (`~9:1` glide).
- Uses a 16-entry `kFlightRecipV` table for `1/V` in path-angle updates and bank-turn scaling.
- Ground pitch capped at `kFlightMaxGroundPitch = 48`; adds a pre-stall slow-speed warning (`kFlightStallWarnSpeed = 1200`, `kFlightStallWarnSteps = 4`) returned by `flight_stall_warning()`, and an elevator stall-deepening lock.
- Cycle probe: ~5,184 cy straight / ~12,186 cy re-orthonormalising.

### Why it was reverted (cannot loop)

Three small-angle assumptions prevent the AoA model from flying over the top of a vertical loop:
1. Vertical acceleration computes `lift * up.z - W` instead of `L - W cos(gamma)`, reversing sign inconsistently past the vertical.
2. `flight_gamma` stores `sin(gamma)` clamped to `±4096` with no past-vertical branch (`cos(gamma) < 0` is unrepresentable).
3. The stall-deepening elevator lock holds the nose ~15° above the stalled path, preventing pitch-over.

Additional minor limits: world-plane `alpha` understates steep-bank AoA, and horizontal speed uses full `V` rather than `V cos(gamma)`.

### Prototype harness (`c64o/proto/`)

`c64o/proto/` (`aoa.h`, `aoa.cc`, `aoa_proto.cc`, `cycles_probe.cc`, `size_probe.cc`) is a standalone development harness used while exploring the AoA model. It is not part of `make prg`, and its defaults (`kAoaAlphaStall = 64`, `kAoaCamberCl = 0`, `kAoaInducedShift = 4`, `kAoaMaxGroundPitch = 64`) predate `flight.cc`'s tuned `__FLIGHT_AOA__` constants.
