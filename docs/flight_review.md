# Review: `docs/flight.md` vs `c64o/flight.cc` vs `c64o/test/flight_test.cc`

Findings are ordered by severity. Numeric claims were verified by driving the real
`flight.cc` through a throwaway host harness (built with the same flags as the test
suite, then discarded). Throttle percentages below are relative to
`kMaxThrottle = 0x18 = 24`.

**Every finding in sections A–E is now resolved** — see the Status table at the end for
what each resolution was. The sections are kept because they record the measurements the
spec's numbers now rest on, and why several of them replaced plausible-looking figures
that the model never produced. Section F holds the findings from the most recent pass.

---

## A. Structural mismatch: the model has no angle of attack — **FIXED**

> Resolved in the spec, not the code: the AoA model was the thing that did not exist, and
> the arcade model is the one that ships. `flight.md` §2.1 is rewritten to state plainly
> that lift is a function of $V^2$ and bank only, that `front.z` never enters it, and that
> pitch trades airspeed for climb rate rather than making lift. The one-sided deficit and
> what "trim" therefore means are in §2.4. Original report below.


**`flight.md` §2.1** — "Lift is proportional to Angle of Attack ($\alpha$) and airspeed
squared ($V^2$)."

**`flight.cc`** — lift is
`raw_lift = fastmul8p8(speed_sqr >> 2, up.z)`. It depends on **speed and bank only**.
`front.z` (pitch) never enters the lift equation. There is no AoA term anywhere.

This is the root cause of most of the divergences in §B. In the code, pitch does exactly
two things: it adds a gravity drag term (`speed -= front.z >> 3`) and it sets the flight
path (`vspeed = front.z * speed / 256 - sink_penalty`). Pitching up does not generate lift,
it converts speed into climb rate. That is a legitimate arcade design, but `flight.md`
is written as if a lift-curve/trim model exists.

A related asymmetry: when lift exceeds weight (`deficit <= 0`) nothing happens — there is
no upward force. So "trim" is one-sided: below ~0x0800 airspeed you sink, above it you are
exactly level at `front.z = 0` and there is no pitch that holds level flight faster.

---

## B. Numbers in `flight.md` that the code does not produce

### B1. "25% throttle is the minimum for sustained level flight" (§2.1, §2.3, matrix row *Slow Level Flight*) — **FIXED**

> `flight.md` §2.1 now carries the measured throttle/pitch/airspeed table, with the floor
> at throttle 11 (46%) and the note that from throttle 16 (67%) up the level trim is
> `front.z = 0` exactly. §2.3's "zero excess thrust" is moved from 25% to 46% and the
> matrix rows are rebuilt around the measured values. Re-measured against the current
> `flight.cc` (the flap lift bonus from B3 has landed since the original sweep, so the
> clean-configuration numbers were re-taken rather than assumed to still hold):
>
> | Throttle | Level flight? | Lowest level pitch | Settled speed |
> | --- | --- | --- | --- |
> | 10 (41%) | no — best vspeed −2 | — | — |
> | 11 (46%) | yes | 20 | 1535 |
> | 12 (50%) | yes | 8 | 1846 |
> | 16 (67%) | yes | 0 | 2111 |
> | 24 (100%) | yes | 0 | 2509 |
>
> With flaps the floor drops to throttle 9 (37%). Original report below.


Measured, upright, clean, sea level, best pitch found by sweep, run to steady state:

| Throttle | Best achievable vspeed | at pitch |
| --- | --- | --- |
| 6 (25%) | no level trim exists — stalls, descends, crashes | — |
| 8 (33%) | no level trim exists — crashes | — |
| 10 (41%) | −2 (marginal) | 40 |
| 11 (45%) | +33 | 48 |
| 16 (66%) | +57 | 8 |

The real floor for sustained level flight is **~41–45% throttle**, not 25%. At 25% the
aircraft cannot hold altitude at any pitch angle; it stalls and hits the ground.

Consequently §2.3 "**25% Throttle: Zero excess thrust**" is also wrong — 25% has
*negative* excess thrust; it cannot even sustain level flight, let alone a climb.

### B2. "Sustained inverted level flight requires 50%–60% throttle" (§3.2, matrix row *Inverted Level Flight*) — **FIXED**

> `flight.md` §3.2 now states 71% (throttle 17) with the measured table, and records that
> the trim sits a few units above stall speed so inverted level flight is flown on the
> stall boundary. Re-measured against the current `flight.cc`:
>
> | Throttle | Level flight? | Lowest level pitch | Settled speed |
> | --- | --- | --- | --- |
> | 16 (67%) | no — best vspeed −9 | — | — |
> | 17 (71%) | yes | 82 (~19°) | 1049 |
> | 18 (75%) | yes | 78 (~18°) | 1144 |
> | 24 (100%) | yes | 68 (~15°) | 1698 |
>
> The original sweep put the trim at throttle 18 / pitch 102 / speed 1023. The finer sweep
> finds it one notch lower and slightly flatter, and the settled speed lands just *above*
> the 1024 stall speed rather than one unit under it — so the "permanently stalled" reading
> in the original report was an artefact of the coarser sweep. The margin is still only a
> couple of dozen units. Original report below.


Measured with `up.z = -256`:

| Throttle | Best vspeed | at pitch |
| --- | --- | --- |
| 12 (50%) | no level trim — crashes | — |
| 14 (58%) | −70 | 66 |
| 16 (66%) | −9 | 82 |
| 18 (75%) | +60 | 102 |

Inverted level flight needs **~70–75% throttle** and ~+102 nose-up (≈23°). Also worth
recording in the spec: at that trim the aircraft sits pinned at speed 1023, i.e. exactly
one unit under the 0x0400 stall speed, permanently on the stall boundary.

### B3. "Flaps increase $C_L$ by ~30%" and "requires less nose-up pitch" (§4.2) — **FIXED**

> Resolved by implementing the lift bonus in `flight.cc` (`lift += lift >> 1`, applied to
> the signed lift so it deepens the downforce when inverted) and correcting `flight.md`
> §4.2 to +50%. ×1.5 is what the pre-existing stall constants already implied:
> $\text{0x0400}/\sqrt{1.5} = \text{0x0343} \approx \text{0x0340}$, so the multiplier and
> the constant now describe the same wing. Re-measured after the fix:
>
> | | clean | flaps |
> | --- | --- | --- |
> | throttle 12, pitch 0 | speed 1846, **vspeed −48** | speed 1698, **vspeed 0** |
> | min throttle for a level trim | 11 (45%) | 9 (37%) |
> | nose-up pitch to hold level @ thr 12 | 8 | 0 |
> | nose-up pitch to hold level @ thr 14 | 4 | 0 |
>
> Flaps now buy lift at the price of speed, and §4.2's "less nose-up pitch at slow speeds"
> holds. Tests: `test_flap_drag_lift_and_stall_reduction`. Original report below.


`flight.cc` changes only two things for `flight_flap`: it adds `speed_sqr >> 12` drag and
it swaps the stall speed constant. **Lift is untouched.** Measured at 50% throttle,
`front.z = 0`, steady state:

- clean: speed 1846, vspeed −48
- flaps: speed 1619, vspeed −96

Flaps make level flight *harder*, the opposite of §4.2's "Equilibrium Impact" bullet.

Separately, the "~30%" is internally inconsistent with the stall speeds the same section
quotes: stall speed scales as $1/\sqrt{C_L}$, and `0x0340 / 0x0400 = 0.8125` implies
**+51%** $C_L$, not +30%. A +30% $C_L$ would give a stall speed of ~`0x0384`.

### B4. "Yaw rate is proportional to left.z · V" (§3.1) — **wrong**

`flight.cc` uses `int8_t rot = flight_cam.left.z >> 5`, applied as a fixed yaw rotation.
Turn rate is a function of bank angle **only**; airspeed does not affect it. So a slow
banked turn and a fast banked turn have the same turn rate, and the horizontal turn force
$F_{turn} = L \cdot \text{left.z}$ from §3.1 does not exist in the code at all — the turn
is implemented purely as a heading rotation with no lateral force.

### B5. "Cruising Level Flight — 75% — Low Nose Up" (matrix) — **FIXED**

> The matrix row now reads "75% (18) — **Zero** pitch", with the explanation that above
> ~67% throttle the level trim is `front.z = 0` exactly. A *Slow Level Flight* row at 50%
> and a *Minimum Level Flight* row at 46% carry the low-nose-up cases the original row was
> reaching for. Confirmed against the current code. Original report below.


At 75% throttle the level trim (`vspeed = 0`) is at `front.z = 0` exactly. Any positive
pitch is a climb (+74 vspeed at pitch 10). Same at 100%. "Low nose up" is only correct
for the 45–58% band.

---

## C. `flight.md` claims the code contradicts on edge cases

### C1. Stall pitch-down is *not* always toward the ground (§2.2)

§2.2 states: "*Pitch down always happens towards the ground, not relative to the
aircraft's canopy/belly.*"

`flight.cc` has two paths, and the spec documents only one:

- `front.z <= kMaxStallPitchZ (224)`: direct `front.z -= s` — world-space, correct.
- `front.z > 224`: `vec_transform3(&kVecPitchDown, &flight_cam)` — this is a **body-axis**
  rotation. Inverted (`up.z < 0`) with the nose above ~61°, it moves the nose *away* from
  the ground, i.e. exactly the case §2.2 says cannot happen.

The `kMaxStallPitchZ` dead-spot workaround is well commented in the code but is absent
from `flight.md` entirely. Either document it (with its inverted caveat) or make the
high-nose path attitude-aware.

### C2. Touchdown does not zero vertical speed — the aircraft balloons off the runway (§5.3) — **FIXED**

> Resolved in `flight.cc`: vertical speed is zeroed on touchdown (after the envelope
> check, which still sees the arrival sink rate) and held at 0 for as long as
> `model_on_ground` is set. Regression test: `test_rollout_stays_on_ground`.
> Original report below.


§5.3: "*If all safety thresholds are satisfied: transition to `model_on_ground = true`,
zero out vertical speed, level wings.*"

The code sets `model_on_ground = true` and (next frame) levels the wings, but **never
zeroes `flight_vspeed` and never resets `front.z`**. Ground mode clamps `front.z >= 0` but
leaves a positive flare pitch intact, and `vspeed = front.z * speed / 256` stays positive,
while `flight_move_forward` only clamps altitude from *below*. Measured, after the
"successful landing" case from `test_touchdown_flare_and_crash_envelope` (front.z = 45):

```
frame 0: z=8192  front.z=45 vspeed= 66
frame 1: z=8412  front.z=43 vspeed=220
frame 4: z=9000  front.z=39 vspeed=184
frame 7: z=9524  front.z=35 vspeed=170
```

The aircraft flies back up off the runway while still in ground mode. This is a genuine
bug, not just a doc gap. The existing test never sees it because it advances exactly one
frame.

### C3. The crash envelope is checked continuously on the ground, not just at touchdown — **FIXED**

> The continuous check is intended and is now stated in `flight.md` §5.3. The margin problem
> is fixed by splitting the speed limit: `kMaxGroundSpeed = 0x0D00` applies once already
> rolling, `kMaxLandingSpeed = 0x0A00` on the touchdown frame. Full-throttle ground
> equilibrium is 2290 against a 3328 limit — ~45% headroom instead of 51 units.
> `test_takeoff_roll_speed_margin`. The runway check needed the same treatment for the same
> reason; see F2. Original report below.


§5.3 frames the checks as a *touchdown* event. In the code `if (flight_eye_z <= kMinEyeZ)`
runs every frame, so the gear and overspeed checks also apply during taxi and takeoff roll.
Two consequences the spec should state:

- Taxiing with gear retracted crashes immediately (probably intended, but undocumented).
- Ground roll is capped by `kMaxLandingSpeed = 0x0A00 = 2560`. Full-throttle ground
  equilibrium is 2509 — only **51 units of margin** before a takeoff roll self-destructs.
  A mission starting on the ground with a high `start_speed`, or any future drag tweak,
  will trip this.

### C5. The sink-rate limit is unreachable upright, and the bank check has an inverted blind spot — **FIXED (inverted trigger added)**

> The blind spot is closed: `up.z < kMinLandingUpZ` is a crash trigger in its own right,
> documented in `flight.md` §5.3 and covered by `test_landing_envelope_inverted`. The
> threshold is 0 rather than a tight $\cos(\text{roll})$ bound because `up.z` also falls
> with nose-up pitch — at the maximum legal flare it is already down to 250 — and a legal
> flare must not trip it.
>
> The other half is resolved too, in two steps. `kMaxLandingVSpeed` was tightened
> `-0x0180` → `-0x00E0` (see *Tightening the sink rate limit* below), and `kMinLandingPitch`
> was later widened `-16` → `-32`, which raised the worst reachable sink from −251 to −315.
> The sink check therefore fires on its own now rather than being shadowed by the pitch
> check, and §5.3 tabulates the airspeed a nose-down arrival needs at each pitch.
> `test_landing_envelope_sink_rate` prints the worst reachable sink on every run, so a
> model change that made it dead again would be visible immediately.
>
> **Note on numbering**: this review calls the triggers by the numbers `flight.md` used at
> the time. §5.3 has since been renumbered to follow the order `_landing_fault()` actually
> evaluates them in, because that order decides which fault is reported. The old "trigger 2"
> (sink rate) is now trigger 4; the old "trigger 3" (bank) is now 5.
>
> Original report below.


Found while writing the E3 tests. Sweeping every orthonormal attitude that passes the
*other* four checks (pitch ∈ [−16, 64], |left.z| ≤ 32, gear down, speed ≤ 0x0A00) across
the full speed range:

| | worst reachable vspeed | limit |
| --- | --- | --- |
| upright (`up.z > 0`) | **−301** | −384 |
| inverted (`up.z < 0`) | −803 | −384 |

Vertical speed is $\text{front.z} \cdot V / 256 - \text{sink}$, and both terms are bounded
for an upright arrival: pitch cannot go below `kMinLandingPitch` without trigger 4 firing
first, and the sink penalty is bounded by the stall speed floor. So **crash trigger 2 can
never fire on an upright landing** — every arrival that would trip it has already tripped
the pitch check. As a "hard landing" rule it is dead.

The flip side is that it *is* reachable inverted, and the reason is trigger 3's blind spot:
the bank check tests `|left.z| > 32`, and after a full 180° roll `left.z` is back to ~0.
**A wings-level inverted arrival passes the bank check.** It gets caught, but by the sink
rate rather than by the attitude, and only because the inverted lift deficit happens to be
large.

Both behaviours are now pinned by `test_landing_envelope_sink_rate`, which asserts the
upright arrival stays inside the limit and that the inverted one trips trigger 2 with
triggers 3, 4 and 5 all explicitly clear. Worth deciding whether the intended rule is
"|left.z| > 32" or "up.z < 246", i.e. whether landing inverted should be its own crash
trigger.

### C4. Ground steering keys (§5.1)

§5.1 says roll inputs are "(J/K)". In `sim.cc` roll is **J / L**; K is pitch up. Minor,
but the doc names specific keys.

---

## D. `flight.md` gaps (behaviour in the code with no requirement)

1. **`kMaxSpeed = 0x0F00` hard clamp** is never mentioned. §6.1 talks about a
   $V_{max\_terminal}$ where drag balances gravity — that balance is real (≈0x0EF7 at full
   throttle in a vertical dive), but the separate hard clamp is undocumented and the test
   asserts against it.
2. ~~**Lift-deficit sink (`sink_penalty = deficit >> 4`)** is the single most important term
   in the model and appears nowhere in `flight.md`.~~ — **FIXED**: now specified in
   `flight.md` §2.4, together with `kTrimLift`, the deficit-induced drag term, the 0x0800
   trim speed and the one-sided nature of the deficit (excess lift produces no climb).
   Tests: `test_trim_speed_boundary`.
3. **The `kMaxStallPitchZ` branch** (see C1).
4. ~~**Trim lift constant `kTrimLift = 0x1000`** — the implied trim speed of 0x0800 (below
   which you sink, above which you cannot) is the model's central number and is
   unspecified.~~ — **FIXED**: specified in `flight.md` §2.4 alongside the deficit and the
   sink penalty, and pinned by `test_trim_speed_boundary`. Resolved together with D2.
5. **§6.2 "optimal glide ~ −10°"** is asserted with no derivation and nothing in the code
   targets it. Unverified.
6. **§1 "`flight_advance()` must execute within < 1000 CPU cycles"** — nothing measures
   this. `flight_advance` does ~6 `fastmul8p8`, a `fastsqr8p8`, and often a full
   `vec_orthonormalize` (3 normalizes + 2 cross products). 1000 cycles looks optimistic by
   a large factor. Either instrument it (`__DEBUG_CYCLES__` exists for `vectest`) or
   restate the budget.
7. **"Bank angle 40% / 80%"** notation is never defined. It appears to mean percent of 90°
   (40% → 36° → `left.z = 150`, 80% → 72° → `left.z = 243`), which is self-consistent with
   §3.1's "~36°" and "L_Z ≈ 30%", but a reader will assume `left.z/256`.

---

## E. Test suite issues

### E1. Vacuous / tautological tests — **FIXED**

> All three rewritten. A shared `_settle()` helper holds an attitude at a fixed throttle
> until the scalar state stops changing, so the equilibrium tests assert the trim the model
> converges to rather than a single frame.
>
> - `test_level_cruise_equilibrium` now asserts `vspeed == 0`, that speed and altitude are
>   unchanged over a further 50 frames, and that the settled speed is above `kTrimSpeed`.
>   Split out `test_trim_speed_boundary`, which pins the one-sided deficit: above trim,
>   zero pitch is genuinely level; below it, zero pitch sinks.
> - `test_inverted_flight_drag_and_pitch` now compares against an upright baseline at the
>   same throttle (2290/vspeed 0 vs 1915/vspeed −479) and searches for the shallowest pitch
>   that stops the descent, asserting it is positive — i.e. nose up relative to the horizon,
>   which is what §3.2 claims.
> - `test_flap_drag_and_stall_reduction` → `test_flap_drag_lift_and_stall_reduction`, which
>   asserts all three flap effects separately: drag (2290 → 2111), lift (sink at 0x0600
>   goes −112 → −40), and stall speed (a speed between the two constants stalls clean, does
>   not stall with flaps).
>
> Original report below.


- **`test_flap_drag_and_stall_reduction`** asserts only `flight_flap == 1` — it sets the
  flag and checks the flag. It tests nothing. Neither the drag increase nor the stall
  speed reduction it is named for is covered.
- **`test_level_cruise_equilibrium`** asserts `flight_speed > 0x0500` after 200 frames at
  throttle 0x14, where equilibrium is 0x08F2. It never checks `flight_vspeed`, i.e. the
  one thing "level cruise equilibrium" means.
- **`test_inverted_flight_drag_and_pitch`** asserts `flight_speed < 0x0800`. Starting speed
  is 0x0860 and everything decelerates; this passes without demonstrating the lift-deficit
  drag mechanism. Despite its name it asserts nothing about pitch.

### E2. `test_takeoff_stall_speed_gate` — first half tests the wrong branch

`flight_init()` sets `model_on_ground = false`. The test then sets `flight_eye_z = 0x2000`
but the static flag is still false, so the first `flight_input(FLIGHT_INPUT_PITCH_UP)`
takes the **airborne** path and rotates unconditionally — the ground stall gate it means
to test is never reached. The subsequent `assert(flight_eye_z == 0x2000)` passes because
`flight_move_forward` clamps to `kMinEyeZ`, not because the gate held. Only the second
half (after `model_on_ground` has been set true by the ground-contact check) exercises the
real gate.

### E3. Crash envelope coverage is incomplete — **FIXED**

> Three tests added — `test_landing_envelope_sink_rate`, `..._bank_angle`,
> `..._touchdown_speed` — each with a pass/crash pair, driven by an `_arm_touchdown()`
> helper that flies one frame at altitude to learn the vertical speed the state actually
> produces, then re-arms exactly that far above the ground. That makes them real descents
> through the ground plane rather than the below-ground setup criticised in E4.
>
> Two things surfaced while writing them, both now pinned by assertions:
> bank has to be seeded through `up` rather than `left` (`vec_orthonormalize` derives left
> from up × front, silently discarding anything written into `left.z`), and the sink-rate
> trigger is unreachable for upright arrivals — see C5.
>
> Original report below.


Of the five crash triggers in §5.3, tests cover only #1 (gear up) and #4 (pitch). There is
**no test** for:

- #2 excess sink rate (`vspeed < -0x0180`)
- #3 excess bank (`|left.z| > 32`)
- #5 excess touchdown speed (`speed > 0x0A00`)

Note also that in every landing test the computed `flight_vspeed` is *positive* (nose-up
flare), so the sink-rate criterion is not even incidentally exercised.

### E4. Landing tests set up an already-below-ground state

`flight_eye_z = 0x2000 - vs - 1` uses `vs = front.z * speed / 256`, ignoring `sink_penalty`,
so the assumed vspeed is wrong (predicted 225, actual 69 in the flare case). It happens to
still land because the altitude is already *below* `kMinEyeZ` at the start of the frame and
gets clamped. These tests are validating the clamp, not a descent through the ground plane.
They are brittle: any change to `sink_penalty` silently changes what they exercise.

### E5. Requirements with no test at all

§2.1 trim-vs-throttle ordering · §3.1 turn rate / heading actually changing · §3.1 altitude
loss in a banked turn (test 5 checks speed only) · §3.2 inverted level flight throttle
requirement · §4.2 flap stall-speed reduction and lift · §5.1 ground steering (roll → yaw)
· §5.3 crash triggers 2/3/5 · §6.2 optimal glide angle · §1 cycle budget.

### E6. Cosmetic

Header prints "25 DYNAMIC TESTS"; one of the 25 (`test_host_multiply_matches_c64`) is a
static contract check, not a dynamic test.

---

## F. Second pass: drift since the first review

Sections A–E were written against an older `flight.cc`. The code has moved on — the landing
envelope in particular — and `flight.md` did not always move with it. Everything in this
section was found by re-reading the current `flight.cc` against the spec.

### F1. The landing pitch limit in `flight.md` is not the one in the code — **FIXED**

§5.3 trigger 4 gave the nose-down limit as `front.z < -16` and the safe range as
$-16 \le \text{front.z} \le 64$. `flight.cc` has `kMinLandingPitch = -32`. The spec was
half as permissive as the model, so a pilot flying to the spec would have believed a legal
arrival was a crash. §5.3 now states $-32 \le \text{front.z} \le 64$.

### F2. The runway requirement and the approach warnings are absent from `flight.md` — **FIXED**

Two features exist in `flight.cc` with no requirement behind them:

- **`FLIGHT_CRASH_NOT_ON_RUNWAY`.** `_on_runway()` tests the map tile under the aircraft,
  and a touchdown anywhere but a `MAP_OBJ_RUNWAY` tile is a crash however good the attitude
  is. It is applied on the touchdown frame only — otherwise rolling off the far end of the
  runway would crash an aircraft that had already landed cleanly.
- **Approach warnings.** The same envelope runs non-destructively whenever the aircraft is
  airborne, descending and below $Z = \text{0x4000}$, and reports the first violation as
  `WARNING: <fault>`. This is why the trigger *order* matters: it decides which single
  fault the pilot is shown. The order was never specified.

Both are now in §5.3, including the touchdown-only/continuous split for the runway check
alongside the existing split for the speed limit. Tests: `test_landing_off_runway_crash`,
`test_runway_1_bounds_alignment`, `test_low_altitude_approach_warnings`.

### F3. The sink-rate reachable range is stale in the spec **and** in the code comment

The `-32` pitch limit of F1 moves the worst reachable sink. `flight.md` §5.3 said the range
above stall was "roughly −251 to 0", and the comment on `kMaxLandingVSpeed` in `flight.cc`
says the same. Measured on the current model — and printed by
`test_landing_envelope_sink_rate` on every run — it is **−315, at pitch −31, speed 1030**.

The −251 in both texts is not wrong so much as **stale by exactly one constant**: −251 is
the worst sink at pitch −16, which *was* the pitch limit when the sentence was written.
Widening the limit to −32 opened up steeper arrivals and with them deeper sinks.

This does not change the choice of `-0x00E0`: the limit still sits inside the reachable
range and still fires, and the worst a level-or-nose-up flare produces is unchanged at
−194. What it changes is the shape of the rule at the steep end, now tabulated in §5.3:

| Arrival pitch | Worst sink | Minimum airspeed to survive |
| --- | --- | --- |
| −32 | −315 | unsurvivable at any speed |
| −24 | −283 | ~1746 |
| −16 | −251 | ~1331 |
| −8 | −219 | any |
| ≥ 0 | −194 | any |

Triggers 4 and 6 therefore meet with no gap between them, which is a stronger property than
the spec previously claimed and worth keeping if the constants are ever retuned.

**`flight.md` §5.3 is corrected. The comment on `kMaxLandingVSpeed` in `flight.cc` is
not — it still says −251, and should be updated to −315 when that file is next touched.**

### F4. Terminal velocity in §6.1 was measured with the wrong attitude — **FIXED**

§6.1 gave full-throttle vertical-dive terminal velocity as ~`0x0EF7` (3831). Measured with
the nose genuinely straight down it is **3693 (`0x0E6D`)**. The old figure is what you get
if `up.z` is left at 256 while `front.z` is forced to −256 — an attitude that is not
orthonormal and cannot occur. Nose truly down means $\text{up.z} = 0$, so lift is zero, the
full `kTrimLift` deficit is charged as induced drag, and the balance lands lower. §6.1 now
carries clean / gear-down / idle figures, all of them under the `0x0F00` clamp.

### F5. Ground friction and the wheel brake are unquantified — **FIXED**

§5.1 said only that "throttle at 0% applies a constant wheel friction drag". The rate is 2
units per frame, applied only at closed throttle. `FLIGHT_INPUT_BRAKE` — 32 units per
input, ground-only — was not mentioned at all, despite having a test
(`test_ground_braking`). Both are now specified.

### F6. Cosmetic: two stale comments in `flight_test.cc`

- `test_optimal_glide_angle` says "the documented optimum is `front.z = -49`". `flight.md`
  §6.2 says −50, and the test's own output prints −50. Off by one, in a comment.
- `test_landing_envelope_sink_rate` and its neighbours refer to the crash triggers by the
  numbers `flight.md` §5.3 used *before* F2 renumbered them to follow evaluation order
  ("Trigger 2 armed", "6 clear", "// 27. ... (crash trigger 3)"). The assertions are right;
  only the numbers in the comments have moved. Worth a pass when that file is next edited.

Neither affects behaviour and neither is worth a commit on its own.

---

## Status

**Done**

| Item | Resolution |
| --- | --- |
| C2 | Touchdown balloon fixed. `test_rollout_stays_on_ground` |
| B3 | Flap $C_L$ bonus implemented (`lift += lift >> 1`), §4.2 corrected to +50%. `test_flap_drag_lift_and_stall_reduction` |
| D2 | Lift-deficit sink specified in `flight.md` §2.4. `test_trim_speed_boundary` |
| C5 | New crash trigger 6: touchdown with `up.z < 0`. `test_landing_envelope_inverted` |
| C1 / D3 | Stall break now picks its body rotation by the sign of `up.z`, so the nose always falls toward the ground. Dead spot documented in §2.2. `test_inverted_high_nose_stall_breaks_downward` |
| B4 | `· V` dropped from §3.1 — yaw rate is bank-only, as the code has it |
| C4 | §5.1 corrected to J/L |
| D1 | `kMaxSpeed` clamp documented in §6.1, distinct from the drag/gravity terminal velocity |
| D5 | Measured: optimum is `front.z = -50` (~ -11°) at ~4.96 : 1, so §6.2's "~ -10°" was close. Numbers tightened. `test_optimal_glide_angle` |
| D6 | §1 rewritten: PAL frame context, split scalar (~1,000) vs re-orthonormalizing (~5,500) budgets, flagged as estimates pending `__DEBUG_CYCLES__` measurement |
| D7 | Bank-percentage convention (percent of 90°) defined in §3.1 |
| E1 | Three vacuous tests replaced |
| E2 | `test_takeoff_stall_speed_gate` rewritten around a `_put_on_ground()` helper, so it exercises the ground branch; also covers the flap-lowered gate |
| E3 | Sink rate, bank angle, touchdown speed covered |
| E6 | "DYNAMIC" mislabel gone with the header rewrite |
| Sink limit | `kMaxLandingVSpeed` tightened `-0x0180` → `-0x00E0`, making the sink-rate trigger live. See below |
| C3 | Ground-roll speed limit split out as `kMaxGroundSpeed`. `test_takeoff_roll_speed_margin` |
| E4 | Landing tests converted to genuine descents where physically possible |
| E5 | Turn rate, banked altitude loss and ground steering now covered |
| G1 | Ground heading no longer decays back to the nearest axis. See below |
| G2 | Ground steering requires `flight_speed > 0` |
| A | §2.1 rewritten with no AoA term: lift is $f(V^2, \text{up.z})$, pitch trades airspeed for climb rate |
| B1 | §2.1 carries the measured throttle table; floor is throttle 11 (46%), not 25%. §2.3 "zero excess thrust" moved to 46% |
| B2 | §3.2 corrected to 71% (throttle 17), ~19° nose up, settled speed ~1049 |
| B5 | Matrix *Cruising Level Flight* is zero pitch; new *Slow* (50%) and *Minimum* (46%) rows carry the nose-up cases |
| D4 | `kTrimLift` / trim speed specified in §2.4 with D2 |
| F1 | §5.3 landing pitch range corrected `-16` → `-32` to match `kMinLandingPitch` |
| F2 | Runway requirement and approach warnings specified in §5.3, with the trigger order they depend on |
| F4 | §6.1 terminal velocity re-measured at a real vertical attitude: `0x0E6D`, not `0x0EF7` |
| F5 | §5.1 quantifies wheel friction (2/frame) and documents `FLIGHT_INPUT_BRAKE` (32/input) |

**Open** — both are code comments, not behaviour, and neither affects the model:

| Item | What is left |
| --- | --- |
| F3 | The comment on `kMaxLandingVSpeed` in `flight.cc` still says the worst reachable sink above stall is −251. It is −315. `flight.md` §5.3 is already correct |
| F6 | `flight_test.cc` comments: `-49` for the glide optimum (should be −50), and crash-trigger numbers from before the §5.3 renumbering |

### G1: heading decay on the ground

Reported symptom: steer off the centreline and the aircraft slowly turns back by itself.

The cause is a rounding ratchet, not anything in the flight model. `vec_normalize` truncates
when it scales a vector back to length 256, so the larger component gains a unit before the
smaller one does, and repeated application walks a vector toward its dominant axis. Feeding
it `(223, 123)` five times gives `(226, 121)`:

```
(223,123) -> (224,123) -> (225,123) -> (223,121) -> (226,122) -> (225,121)
```

Ground mode ran that round trip *every frame* — it rebuilt `left` from `front`, recomputed
`up` via an 8.8 cross product that loses a little length, and unconditionally set
`model_need_normalize`. Measured decay at any speed: 28.88° → 0.00° in ~300 frames.

The fix is to rebuild only when the wings are actually off level (`left.z != 0`), which is
the condition the block exists to enforce. Heading now holds exactly. It also removes a full
`vec_orthonormalize` (~4,500 cycles by the §1 estimate) from every frame of taxi and takeoff
roll.

**Side effect, resolved.** The old every-frame rebuild was also what made a landed aircraft's
nose settle from its flare pitch down to 0 — accidentally, via the same rounding drift.
Gating the rebuild left `front.z` wherever the flare put it. `front.z` is now snapped to 0
explicitly on the touchdown transition (`!was_on_ground && !flight_crashed`), which costs one
`vec_orthonormalize` and no ongoing drift. Easing it in over the rollout was rejected: that
means touching the attitude every frame, which reintroduces the ratchet, since the bias lives
in `vec_normalize`. `test_rollout_stays_on_ground` asserts `front.z == 0` for all 600 frames
of rollout and that the heading moves less than 0.5° across the touchdown normalize.

### Tightening the sink rate limit

The first cut at this was wrong, and the reason is worth recording. My earlier suggestion of
`-0x0100` came from a sweep that measured vertical speed *before* the landing frame ran, and
found arrivals sinking at -301. But those all sit below stall speed, where the stall break
(§2.2) has already driven the nose past `kMinLandingPitch` — so trigger **4** owns them, not
trigger 2. Measuring only arrivals above stall speed, where the sink limit can actually be
the deciding check, the reachable range is much narrower:

| Limit | Legal descending arrivals that crash (of 4888) | ...with a level-or-nose-up flare |
| --- | --- | --- |
| `-0x0100` (-256) | 0 | 0 |
| **`-0x00E0` (-224)** | **99** | **0** |
| `-0x00C0` (-192) | 396 | 2 |

`-0x0100` would have been another no-op. `-0x00E0` was chosen because it is the tightest
value that never punishes a correctly flown landing: the worst sink a level-or-nose-up flare
can produce is -194, comfortably inside it. The rule the pilot learns is "flare and you are
safe; arrive nose-down and you need airspeed" — at `front.z = -16` the aircraft has to be
above ~1350 to survive. `-0x00C0` would start failing flares that arrive near stall speed.

Three existing tests had been leaning on the slack in the old limit and were fixed:

- `_put_on_ground()` and the two ground-roll tests were placing the aircraft at ground level
  while the model still thought it was airborne, so their first frame was a touchdown with a
  -240 sink. They now make contact at `kTrimSpeed`, where the lift deficit is zero and the
  arrival is genuinely level.
- `test_touchdown_exact_boundary_limits` was flying its nose-down pair at 0x0500, which now
  sinks at -234 — the pair would have been testing trigger 2 rather than the pitch boundary
  it is named for. Moved to `kTrimSpeed`, and it now asserts the sink limit is *not* the
  binding check, so the isolation is explicit.

`test_landing_envelope_sink_rate` was inverted to match: instead of asserting the limit is
unreachable, it now asserts a slow nose-down arrival trips it with all five other triggers
verified clear, and sweeps above-stall arrivals to assert both that the limit is reachable at
all and that no flared arrival ever reaches it.

Suite is at **37 tests, all passing**. Also fixed on the way through: the host test build was
broken at HEAD — the "Populate navpoints from the mission" commit added `kMissionWaypoints`
/ `kWaypointDefault` references to `flight.cc` without adding `mission.cc` to
`FLIGHT_TEST_SRC`. Added.

The C64 build is still **not** verified here — `oscar64` is a macOS arm64 binary and this
host is Linux, so only `make test` was run. The `flight.cc` changes are a shift-add, a
ternary over two existing rotation matrices and one extra comparison in the envelope check,
but run `make` before trusting them on target.

**Open**

1. **Decide the direction for §A** — either add an AoA term to lift, or rewrite §2.1/§2.3
   and the summary matrix to describe the speed-and-bank lift model that actually exists.
   §2.4 carries a "not yet reconciled" note pointing at this.

   **Partly papered over, not resolved.** The missing pitch term showed at the takeoff end
   as a liftoff speed of 1608 against a 1024 stall speed — the aircraft was legal to rotate
   long before it could fly, and skipped off the runway once per frame in between.
   `kFlightRotatePitchZ` (flight.md §5.2) drives the rotation to ~10.6° instead of 3.6°,
   which brings liftoff to 1047 and costs nothing else: no trim table, envelope limit or
   equilibrium test moves, because it changes an attitude and not the lift equation. It is a
   local fudge and is commented as one. The same hole is still open at the landing end — see
   E4 below, where a nose-up attitude only descends when the deficit outweighs the pitch
   term — and that one has no stopgap. If AoA lands, `kFlightRotatePitchZ` gets deleted.
2. **Re-measure and rewrite the throttle numbers** in §2.1, §2.3, §3.2 and the §7 matrix
   (B1, B2, B5). Blocked on 1, since the numbers move if lift changes.
3. ~~**C3**~~ — **FIXED**. Correction to the original report: the 51-unit figure was wrong,
   because it ignored gear drag, and the gear is necessarily down on the ground (trigger 1).
   Measured full-throttle ground top speed is **2290**, so the real margin was 270, not 51.
   Still too thin for an impact limit. The speed check is now split — `kMaxLandingSpeed`
   (`0x0A00`) on the touchdown frame, `kMaxGroundSpeed` (`0x0D00`) once already rolling —
   which leaves the takeoff roll ~45% headroom without loosening the landing envelope at
   all. Every-frame checking is retained for the other five triggers and is now documented
   in §5.3 as intended behaviour. `test_takeoff_roll_speed_margin`.
4. ~~Trigger 2 is now fully dormant~~ — **FIXED**: `kMaxLandingVSpeed` tightened from
   `-0x0180` to `-0x00E0`. See below.
5. **D6 measurement** — wrap `flight_advance()` in `benchmark.h` and replace the estimated
   budget in §1 with real numbers. The only remaining item that cannot be settled from the
   host build.
6. ~~**E5**~~ — **FIXED**. `test_turn_rate_depends_on_bank_not_speed` (level = no turn,
   steeper bank = faster turn, and bit-identical heading change at 1200 / 1800 / 2400,
   which pins B4), `test_banked_turn_loses_altitude` (0 / -13724 / -48532 at level / 45° /
   70° bank, full throttle, no pitch compensation), `test_ground_steering` (roll steers,
   `left.z` stays 0, left and right are symmetric, and roll input matches the dedicated yaw
   input exactly).
7. ~~**E4**~~ — **FIXED**. Both flare cases in `test_touchdown_flare_and_crash_envelope` are
   now genuine descents through the ground plane. This turned up a real constraint: a
   nose-up attitude only descends when the lift deficit outweighs the pitch term, so just
   above stall the steepest descending flare is `front.z = 51`. The excessive-flare crash is
   therefore only reachable below stall speed — the classic "hold it off too long and stall
   onto the runway" — so that case is flown at 700 and asserts on the pitch the envelope
   actually saw (67), since the stall break trims the nose on the way in.
   `test_touchdown_exact_boundary_limits` keeps its parked-below-ground setup, because the
   exact 64/65 boundary cannot be isolated in a descent for the same reason; that is now
   stated in the test rather than left as an accident.
