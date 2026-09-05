# Flight Dynamics Model Requirements Specification (`flight.md`)

## 1. Overview & 8-Bit Architecture Constraints

This document specifies the flight dynamics model requirements for the C64 flight simulator project. The flight model is engineered specifically for performance on 8-bit architecture (MOS 6502 @ 1 MHz).

### Two models, one build flag

There are two flight models in `flight.cc`, selected by `__FLIGHT_AOA__`, and
`make` builds **both binaries** from the same sources:

| | `ppilot.prg` | `flighta.prg` |
| :--- | :--- | :--- |
| Model | **arcade** - what has always shipped | angle of attack |
| Lift | $f(V^2, \text{bank})$; `front.z` never enters it | $f(C_L(\alpha), V^2, \text{up.z})$ |
| Airspeed points | along the nose, by definition | along its own flight path |
| Stall | a speed the code is told | an angle; the speeds are derived |
| Size | 47,607 bytes | 48,375 bytes (**+768**) |

The angle-of-attack model is the better aeroplane and the arcade one is the
cheaper, which is the whole of the trade and why both exist. Both are covered by
the host suite - `make -C c64o/test test-both` - which carries two sets of
expectations because for several cases they are opposites, and by a narrowing
baseline each.

`flighta.prg` is built from `ppilot.cc` like the game is, with one extra define,
so it stays in step by construction rather than by anyone remembering to keep it
there.

**`ppilot.prg` can be switched too**, with `make prg FLIGHT_AOA=1`, which is
what the test suite's flag does for the host build. That path keeps a
`.flight_model` stamp the binary depends on, because make otherwise sees the
same sources under the same name and hands back the binary it already had - a
silent wrong answer, and the worst kind for a flag whose purpose is comparing
two things. `flighta.prg` needs no stamp: its model is fixed by its name.

**Everything from §2 on describes the angle-of-attack model** unless it says
otherwise, because that is the model these sections were written against and
measured on. Where the arcade model differs, the difference is called out - and
it usually is the difference, since almost every number here moved.

### The shape of the model (`FLIGHT_AOA=1`)

The aircraft has two directions, not one. `flight_cam.front` is where the nose
points; `flight_gamma` is where the aircraft is actually going. The angle
between them is the angle of attack, and it is the wing's input:

$$\alpha_{16} = (\text{front.z} \ll 4) - \text{flight\_gamma}, \qquad C_L = \alpha_{16} + \text{camber}$$

Lift is $C_L V^2$, drag is parasite plus $C_L^2 V^2$, and the net vertical
force integrates into the flight path. The pilot's stick sets the nose; the
wing sets the path. **Everything else in this document is a consequence of
that**, including the stall speeds, which the model is never told.

It was not always the only model, and it still is not the default one. The
arcade model - lift $f(V^2, \text{bank})$ with `front.z` nowhere in it, airspeed
along the nose by definition, and the stall a speed the code carries as a
constant - is what `make prg` builds. `docs/flight_aoa.md` is the prototype that
produced the alternative and the measurements either side.

### 8-Bit Implementation Rules

- **Fixed-Point Precision**:
  - Aircraft position ($X, Y, Z$): 24.8 fixed-point (`int32_t`).
  - Velocities ($V_{\text{speed}}, V_{\text{vspeed}}$), angles, and scalars: 8.8 fixed-point (`int16_t` / `uint16_t`).
  - Orientation matrix: 3x3 orthonormal direction cosine matrix (`mat3_t`), with periodic re-orthonormalization (`vec_orthonormalize`).
  - **Flight path and angle of attack: 4096 = 1.0**, sixteen times finer than a
    direction cosine. This is not spare precision, it is the minimum the model
    works at. The flight path is integrated from a force, so a lift imbalance
    too small to move one unit of it is an imbalance the aircraft never feels;
    at the 256 scale that dead band is 6% of the weight, and the flight path
    could not settle, only hunt across it. At 4096 the smallest step is under
    0.4% and every steady state in this document is bit-stable.
- **No Floating Point / No Runtime Division**:
  - All aerodynamic equations must use integer shifts (`>>`), 8.8 fast multiplications (`vec_fastmul8p8`), and precomputed lookup tables (LUTs).
  - There is exactly one table, `kFlightRecipV`, sixteen entries of $65536/V$.
    The lift curve needs none: the lift slope is chosen so that $C_L$ and
    $\alpha_{16}$ are the same number below the stall.
  - Longitudinal forces are summed at eight times `flight_speed`'s resolution
    and divided once, with the remainder carried in `model_dv_rem`. The turn
    carries its own remainder for the same reason - a 15 degree bank at cruise
    turns at under one unit a step, and one unit is the smallest rotation
    `vec_turn3_xy` can be asked for.
- **Execution Budget**:
  - A PAL C64 frame at 50 Hz is **19,705 cycles**. `flight_advance()` is one
    item in a frame that also has to render the 3D view, so its share has to
    stay small.
  - The cost splits in two, and the split is most of the story. Measured on an
    emulated 6510 by `c64o/proto/cycles_probe.cc` (`make -C c64o/proto
    cycles`), which times single calls with CIA2 timer A and reports the
    cheapest of sixteen:

| | cycles |
| --- | ---: |
| a step that does not re-orthonormalize (trimmed, wings level) | **5,184** |
| a step that does (any control input, any bank, every ground frame) | **12,186** |
| `vec_orthonormalize()` alone | 5,754 wings level, ~9,600 banked |

  - So re-orthonormalization is more than half of an expensive step, and it is
    untouched by the angle-of-attack work. The figure this document carried for
    the old model's cheap step was ~5,000, which is 5,184 to within the
    precision it was quoted at - the aerodynamics grew several multiplies and
    lost the lift-deficit chain, the bank drag term and its `vec_fastsqr8p8`,
    and the two roughly cancel. `model_need_normalize` is still the thing worth
    being stingy with.
  - A like-for-like re-measure of the old build with the same probe was
    attempted and abandoned: oscar64 aborts on a copy of the tree, and the
    alternative was overwriting the working one. The old numbers below are the
    in-game `MDL` readings this document has always carried.

---

## 2. Core Aerodynamic & Equilibrium Flight Regimes

### 2.1. Straight & Level Flight Dynamics

> **Throttle convention**: throttle is an integer $0 \dots \text{kMaxThrottle} = \text{0x18} = 24$. Percentages below are of that range, so "50%" means throttle 12.

- **Lift Balance**: $L = W$, and the wing gets there by angle. $\text{lift} =
  C_L(\alpha) \cdot V^2 \cdot \text{up.z}$, so holding altitude at any airspeed
  is a question of how much angle of attack the pilot is holding.
- **What "trim" means here**: the attitude that puts the wing at the angle the
  weight needs. In level flight the flight path is zero, so the level attitude
  *is* the angle of attack, and the faster the aircraft goes the less of it it
  needs.
- **There is no speed at which level flight is free.** $\text{front.z} = 0$ is a
  descent at every throttle setting, because a symmetric-ish wing at zero angle
  makes only its camber's worth of lift. This is the most visible change from
  the model that came before, where above a trim speed the level attitude was
  $\text{front.z} = 0$ exactly and no faster level trim existed.
- **Trim vs. Airspeed & Throttle** (measured, upright, clean, sea level, run to
  a bit-stable steady state):

| Throttle | Level pitch | Angle of attack | Settled airspeed |
| :--- | ---: | ---: | ---: |
| 12 (50%) | 24 (~5.4°) | 22 | 1476 |
| 14 (58%) | 16 (~3.6°) | 14 | 1719 |
| 16 (67%) | 12 (~2.7°) | 10 | 1906 |
| 18 (75%) | 10 (~2.2°) | 8 | 2054 |
| 20 (83%) | 8 (~1.8°) | 6 | 2188 |
| 24 (100%) | 6 (~1.3°) | 3 | 2429 |

- **Below about 50% throttle the only level trims are on the back side of the
  power curve**, at angles within a few units of the break and airspeeds around
  1000-1100. They exist - the sweep finds one at 33% - but they are not trims a
  pilot can hold: a unit of angle either way either stalls the wing or starts a
  descent that needs more power than there is to arrest. Treat ~50% as the
  practical floor and the 33% row as a curiosity of the drag curve.
- With flaps down the picture shifts down and forward: 41% holds level at
  airspeed 981, and at 67% and above the level attitude is *below* the horizon
  (-17 at 67%, -27 at full power), because the flap camber makes the weight's
  worth of lift at a negative angle.

### 2.2. Stall Dynamics & Automatic Recovery

- **The stall is an angle, not a speed.** It triggers when
  $|\alpha| > \text{kFlightAlphaStall} = 56$ (about 12.6°), airborne, and
  nothing else. The speeds below are consequences of it and appear nowhere in
  the code:

| Configuration | $C_{L\max}$ | Stall speed |
| :--- | ---: | ---: |
| Clean, upright | 1024 | **1024** |
| Flaps down | 1536 | **836** |
| Inverted | 768 | **1182** |
| Inverted with flaps | 256 | 2048 |

  The clean figure is exactly the `0x0400` the old model had to be handed, and
  the flapped one is 836 against its `0x0340` = 832. That is the constants
  falling out of the wing rather than being asserted about it, and it is the
  result the change exists for.
- **Past the peak the wing droops** at five eighths, so pulling harder there
  gets *less* lift: the flight path falls away faster than the nose does and
  the angle runs away. That is what makes a stall a stall rather than a
  ceiling.
- **The break** drives the nose back toward the flight path at
  $(|\alpha_{16}| - C_{L\text{peak}}) \gg 5$ per step. It needs no "toward the
  ground" special case at any attitude or either way up: at low speed the
  flight path is already steeply down, so chasing it *is* the nose drop.
- **Near-Vertical Dead Spot** ($|\text{front.z}| > \text{kFlightMaxStallPitchZ}
  = 224$): `front` is a unit vector, so with the nose this high the end-of-frame
  `vec_orthonormalize` restores nearly all of a direct change to `front.z`.
  Above the threshold the break is applied as a **body-axis rotation** instead,
  which is well defined at any attitude. Which rotation is the sign of $\alpha$
  and not of the nose-down direction - a pitch-down step moves `front.z` by
  $-\text{up.z}/16$, so the `up.z` in it cancels the one in the angle.
- **Two stalls the old model could not see.** The accelerated stall: level at
  airspeed 2816 with the stick full back, the wing breaks at **2290**, 2.2x the
  1 g stall speed. And the inverted stall, which the camber makes reachable
  well above the upright stall speed. A speed gate can express neither.
- **The elevator will not drive the wing deeper into a stall.** A pitch-up
  input is refused while stalled with a positive angle, and pitch-down while
  stalled with a negative one. This is a control law, not aerodynamics, and it
  is there because the pilot has no stick force to feel: one keypress is 3.6
  degrees of pitch and the break pushes back with a quarter of that, so a held
  key wins and keeps winning. Holding the stick back through a rotation - the
  most ordinary thing a player does - stalled the aeroplane at nought feet.
  Note what it does *not* do: the input that carries the wing from just under
  the break to just past it is allowed, so the stall is still reachable and the
  accelerated stall still fires; and nothing stops the flight path from
  stalling the wing on its own, which is how a stall arrives in a banked turn
  or a pull-up. The pilot cannot bury it; the aeroplane can.
- **Recovery** is hands-off and takes about 100 steps (16 s) from a power-off
  stall, ending in a steep dive with the airspeed back.

### 2.3. Climbing & Altitude Mechanics

- **Forward acceleration**: $\Delta V = \text{Thrust} - \text{Drag}_{\text{parasite}} - \text{Drag}_{\text{induced}} - g \sin\gamma$,
  where $\gamma$ is the **flight path**, not the nose. The difference is
  exactly the aeroplane that is pointing up while sinking, which is the
  attitude every approach and every stall entry is flown at.
- **Best rate of climb** at full throttle is **+526**, at a held pitch of 118
  (~27°). The old model's was +663 at a shallower attitude, so the aircraft now
  climbs more slowly - which is the direction `TODO.md` asks for.
- **Service Ceiling & Altitude Density Decay**: unchanged in mechanism -
  $\text{alt\_penalty} = (Z - \text{0x080000}) \gg 12$ clamped to 128, and
  $\text{density} = 256 - \text{alt\_penalty}$ scales thrust and lift. What is
  gone is the hand-written rule that raised the stall *speed* with altitude:
  thinner air makes less lift at the same angle and speed, so the wing needs
  more angle and reaches the break sooner, and the effect now falls out of the
  same density term as everything else.

### 2.4. Drag

- **Parasite** as before: $\text{speed}^2 \gg 10$, with a further $\gg 12$ each
  for the gear and the flaps.
- **Induced**: $(C_L^2 V^2) \gg \text{kFlightInducedShift}$, and this one term
  replaces three separate stand-ins the old model needed - the lift deficit's
  $\text{deficit} \gg 10$, the bank term $\text{left.z}^2 \gg 5$, and the
  implicit inverted penalty. All three were the same thing: the wing being
  asked for lift it has to work for.
- The shift sets where the drag curve bottoms out and therefore the throttle
  needed for level flight, the glide ratio and the climb rate together. 5 is
  what ships; `flight.cc` carries the measurements either side of it.
- **Lift is two-sided.** The old model applied a deficit only when positive, so
  lift in excess of the weight produced no upward force at all. The net
  vertical force integrates into the flight path in both directions now, which
  is why flaps make the aircraft balloon and why a pull-up is a pull-up.

---

## 3. Maneuvering Flight & Banked Turns

### 3.1. Banked Turn Dynamics

> **Bank percentage convention**: bank is quoted as a percentage of 90°, not as a fraction of `left.z`'s 256 unit range. So 40% bank means 36°, i.e. $\text{left.z} = 256 \sin 36° = 150$; 80% bank means 72°, i.e. $\text{left.z} = 243$.

- **The turn is the horizontal half of lift.** Total lift acts along
  `flight_cam.up`; its vertical component $L \cdot \text{up.z}$ fights the
  weight and its horizontal component $L \cdot \text{left.z}$ turns the
  aircraft. Both go through the same $1/V$:

$$\text{rot} = \frac{2 L \sin\phi}{V}, \qquad \dot\gamma = \frac{L\cos\phi - W}{W}\cdot\frac{g}{V}$$

- **Turn rate depends on airspeed**, which is new and is
  `flight_review.md` §B4 closed: a level turn's rate is $g\tan\phi/V$, so the
  slower turn is the tighter one. The old model turned at
  $\text{left.z} \gg 5$ whatever the wing was doing and at every speed alike.
  The scale is not free - one unit of `rot` is 1/256 of a radian a step, the
  weight is `kFlightTrimLift` and gravity is 32 speed units a step per radian,
  which fixes the shift at seven.
- **There is a corner speed.** A level 71° turn asks for 3.07 times the weight
  in lift; below about airspeed 2000 the wing runs out of angle before it can
  make it, and the turn becomes wing-limited rather than bank-limited. A fixed
  $\text{left.z} \gg 5$ cannot express that.
- **Altitude loss in banked flight**: at 36° and 72° of bank with no added
  pitch, $L_Z < W$ and the aircraft turns and descends. Holding the turn level
  needs angle of attack, the angle needs $C_L$, and $C_L$ costs induced drag -
  so a level turn settles slower than level flight at the same throttle, and a
  steep enough one cannot be held level at all. The old model charged a flat
  $\text{left.z}^2 \gg 5$ for the bank and had no wing in the loop; that term
  is gone and induced drag covers it.
- **Pulling in a turn is how the accelerated stall arrives**, since the extra
  angle the bank needs is added to whatever the pilot is already holding.

### 3.2. Inverted Flight Dynamics (Flying Upside Down)

- **Angle of attack is a body angle**, so rolling inverted swaps which side of
  the wing the air arrives from. Upside down, a nose held above the flight path
  in world terms is air arriving on the canopy side, which is negative $\alpha$
  and negative lift - and negative lift times a negative `up.z` carries the
  aeroplane. The whole of that is one line:
  `alpha16 = up.z >= 0 ? d : -d`.
- **Inverted flight is expensive because the wing is cambered.**
  `kFlightCamberCl` is $C_L$ at zero angle; upright it adds to the angle's
  worth of lift and inverted it fights it. That single constant is the entire
  inverted penalty, with no `up.z < 0` case anywhere - and it is paired with
  the stall angle so that upright $C_{L\max}$ stays at 1024 and the clean stall
  speed stays at exactly `0x0400`. The cost comes out of the inverted side:
  $C_{L\max}$ there is 768 and the inverted stall speed is 1182.
- **The nose must be pitched UP relative to the horizon**, and further up than
  the upright trim at the same throttle. Measured, run to a steady state:

| Throttle | Inverted level pitch | Angle of attack | Settled airspeed | (upright pitch) |
| :--- | ---: | ---: | ---: | ---: |
| 12 (50%) | 42 (~9.4°) | −41 | 1457 | 24 |
| 16 (67%) | 28 (~6.3°) | −27 | 1906 | 12 |
| 18 (75%) | 26 (~5.8°) | −25 | 2054 | 10 |
| 24 (100%) | 22 (~4.9°) | −20 | 2429 | 6 |

- The settled airspeeds match the upright ones, because at trim the total $C_L$
  is fixed by the weight and so is the induced drag it costs. What inverted
  flight costs is *attitude* and *stall margin*, not throttle: the aircraft is
  flying nearer its break, and with flaps down the inverted $C_{L\max}$ falls to
  256 and the stall speed goes to 2048, which is most of the envelope.

## 4. Flight Configuration & Drag Devices

### 4.1. Landing Gear Dynamics

- **Parasite Drag Increase**:
  - Gear down (`flight_gear == 1`) increases total parasite drag by **25%** ($\Delta \text{Drag}_{\text{gear}} = \text{speed}^2 \gg 12$).
- **Performance Impact**:
  - Reduces top level speed.
  - Increases glide descent angle at idle throttle.
  - No impact on stall speed or wing lift curve.

### 4.2. Flap Dynamics

- **Flaps are a camber shift, not a multiplier**: $C_L \mathrel{+}=
  \text{kFlightFlapDeltaCl} = 512$, added to the coefficient rather than
  scaling it. That one choice is the whole of the flap model, and the sign does
  the rest:
  - **Upright** it adds to the lift the angle is making, so $C_{L\max}$ goes to
    1536 and the stall speed falls to **836** - against the `0x0340` = 832 the
    old model was told, which is the same wing described two ways.
  - **Inverted** the attitude needs a negative $C_L$ and the offset fights it,
    so $C_{L\max}$ falls to 256 and the stall speed rises to 2048. This is the
    adverse camber penalty of the old §4.2, and it now needs **no `up.z < 0`
    case at all** - the old model had to carry a separate `0x0480` constant for
    it.
- **Drag**: parasite rises by 25% ($\text{speed}^2 \gg 12$) as before, and the
  induced term follows the total $C_L$, so flaps at high speed are expensive
  twice over.
- **Equilibrium impact**: the level attitude drops. At 67% throttle and above
  the flapped aircraft trims level with the nose *below* the horizon (−17 at
  67%, −27 at full power), because the camber makes the weight's worth of lift
  at a negative angle. At low speed flaps buy a genuinely lower floor: 41%
  throttle holds level at airspeed 981, where the clean wing cannot.
- **Ballooning is real now.** Lift above the weight pushes the flight path up,
  so selecting flaps at speed pitches the aircraft into a climb - which the
  one-sided deficit of the old model discarded.

## 5. Ground Interaction & Transition Physics

### 5.1. On-Ground State Dynamics

- **Attitude & Alignment Locks**:
  - Altitude is locked to ground plane ($Z = Z_{\text{min}}$).
  - Pitch attitude is clamped ($\text{front.z} \ge 0$). Negative pitch is prohibited.
  - Wings are locked level ($\text{left.z} = 0$, $\text{up.z} = 256$).
- **Ground Steering**:
  - Roll inputs (J/L) are mapped to nose-wheel steering (yaw left/right), alongside the dedicated yaw inputs (A/S). All four behave identically on the ground.
  - **Steering requires the wheels to be turning**: all four steering inputs are ignored when $V = 0$. A parked aircraft cannot pivot on the spot.
  - **Heading is held, not restored.** The wing-levelling rebuild (`left` from `front`, then `up = front \times left`) runs *only when the wings are actually off level*. Running it every frame slowly turned the aircraft back toward whichever axis it was nearest: the 8.8 cross product loses a little length, `vec_orthonormalize` scales `front` back up, and that scaling truncates — so the dominant component gains a unit before the smaller one does. On the runway that walked a 29° heading back to 0 in about 300 frames. Skipping the no-op case also saves a full re-orthonormalization on every frame of taxi and takeoff roll (§1).
- **Ground Friction & Braking**:
  - Throttle at 0% applies a constant wheel friction drag of **2 units per frame**, on top of the usual airframe and gear drag, decelerating the aircraft to a full stop. It is not applied while the throttle is open.
  - **Wheel brake** (`FLIGHT_INPUT_BRAKE`): each input removes **32** from airspeed, flooring at 0. It is a ground-only control — airborne, the input is ignored.

### 5.2. Takeoff Requirements

**The wing decides.** There is no speed gate and no rotation constant. Pitch-up
on the runway rotates the nose one step at a time up to
`kFlightMaxGroundPitch = 48` (~10.8°), the rotation makes lift like any other
angle of attack, and `flight_advance()` unsticks the aircraft on the step where
$L \cdot \text{up.z}$ exceeds the weight. The liftoff speed is therefore a
consequence of the attitude the pilot chose:

| Held attitude | Clean | Flaps down |
| :--- | ---: | ---: |
| 16 (~3.6°) | 1782 | 1156 |
| 31 (~7.0°) | 1380 | 1019 |
| 47 (~10.6°) | 1166 | 947 |
| 48 (the limit) | **1165** | **943** |

Against a derived stall speed of 1024 clean and 836 flapped, a full rotation
unsticks with about 14% in hand, which is the margin a real rotation has.

**`kFlightRotatePitchZ` is deleted, not retuned**, exactly as its own comment
asked: *"If lift ever grows a pitch term this constant should be deleted rather
than retuned."* It existed because lift had no pitch term, so rotating could
not make the force that lifts the aircraft off - all pitch could do was aim the
flight path up and out-climb a sink penalty, which put the crossover at
airspeed 1608 with a stall speed of 1024. Worse, every frame in between was a
full airborne→ground cycle: the wheels lifted, the vertical speed came out
non-positive, the contact check put them back down, and the sound driver heard
a touchdown once a frame. Both problems are gone at the root. The step that
lifts off is also the step that integrates a positive net force into a climb -
which is why the liftoff test sits *ahead* of the flight-path integration - and
the contact check asks whether the aircraft is descending rather than where it
is.

**Two bounds on the rotation limit**, and it is wedged between them:

- Above, by `kMaxLandingPitch` = 64. The landing envelope runs on **every**
  frame at ground level (§5.3) and trigger 7 does not care that the aircraft is
  taking off rather than arriving. Set equal to it, the takeoff roll crashed:
  `vec_orthonormalize` puts a unit back and 65 > 64.
- Below, by `kFlightAlphaStall` = 56. A full rotation has to leave the wing
  short of the break, or the aeroplane stalls the moment it unsticks.

**Machine independence** is unchanged in kind but now free: every rate scales
with `flight_step_shift`, so a stock C64 and a SuperCPU take a different number
of inputs to the same attitude and unstick at the same airspeed. The old model
had to express the rotation as a target attitude rather than a step count for
exactly this reason; here nothing special is needed.

### 5.3. Touchdown & Crash Envelope Checks

The check triggers whenever altitude $Z \le Z_{\text{min}}$ — **every frame the aircraft is at ground level, not only on the touchdown frame**. It therefore also polices taxi and takeoff roll: rolling with the gear retracted, or with the wings or nose out of limits, fails immediately. This is intended.

Two triggers must not be applied unchanged during a ground roll:

- **The speed limit**, which is an *impact* limit. Full throttle with the gear down settles at 2290, only 270 under $\text{kMaxLandingSpeed}$, so a normal takeoff run would sit uncomfortably close to crashing.
- **The runway check**, which is an *arrival* condition. Applied continuously it would crash an aircraft that simply rolled past the end of the runway.

Both are therefore keyed on whether this frame is a touchdown (the aircraft was airborne last frame) or another frame of an existing ground roll:

| State | Speed limit | Runway check |
| :--- | :--- | :--- |
| Touchdown (was airborne last frame) | $\text{kMaxLandingSpeed} = \text{0x0A00}$ | applied |
| Already rolling | $\text{kMaxGroundSpeed} = \text{0x0D00}$ | skipped |

The looser ground limit still rejects nonsense start states from mission data while leaving the takeoff roll ~45% headroom.

- **Crash Triggers**, in the order the code evaluates them. **The order is part of the specification**: the first violation found is the one reported, so it decides which fault the pilot is told about when an arrival breaks several rules at once. It runs from what has to be settled early on the approach to what is trimmed on short final.

  1. **Not On A Runway**: the ground tile under the aircraft is not `MAP_OBJ_RUNWAY`. Checked **only on the touchdown frame**, not during an existing ground roll — once down, the aircraft may roll off the end without a second crash verdict.
  2. **Belly-Up Arrival**: touchdown while inverted ($\text{up.z} < \text{kMinLandingUpZ} = 0$). Trigger 5 does not cover this — `left.z` returns to ~0 after a full 180° roll, so a wings-level inverted arrival passes the bank check. The threshold is 0 rather than a tight $\cos(\text{roll})$ bound because `up.z` also falls with nose-up pitch, and a legal flare must not trip it.
  3. **Gear Retracted**: `flight_gear == 0`.
  4. **Excess Vertical Speed**: sink rate exceeds limit ($V_{\text{vspeed}} < \text{kMaxLandingVSpeed} = -\text{0x00E0} = -224$).
  5. **Excess Bank Angle**: roll/bank exceeds threshold ($|\text{left.z}| > \text{kMaxLandingRoll} = 32$, approx > 7°).
  6. **Pitch Too Low**: $\text{front.z} < \text{kMinLandingPitch} = -32$ (steeper than ~-7° nose down).
  7. **Pitch Too High**: $\text{front.z} > \text{kMaxLandingPitch} = 64$ (more than ~15° of flare).
  8. **Excess Airspeed**: $V >$ the applicable speed limit from the table above.

  Safe landing pitch range is therefore $-32 \le \text{front.z} \le 64$.

- **Approach warnings**: the *same* envelope test runs non-destructively while the aircraft is airborne, descending, and below $Z = \text{0x4000}$. The first violation is shown as `WARNING: <fault>` instead of crashing, so the pilot is told about a retracted gear or a wrong approach path before the wheels arrive. The warning pass always applies the runway check and the touchdown speed limit, since it is by definition not a ground roll.

- **Successful Landing**:
  - If all safety thresholds are satisfied: transition to `model_on_ground = true`, zero out vertical speed, level wings, and drop the nose ($\text{front.z} = 0$).
  - The nose drop happens **once, on the touchdown transition**, not eased in over the rollout. Easing it would mean adjusting the attitude on every frame, and `vec_normalize` truncates when it rescales, so a per-frame nudge would ratchet the heading toward the nearest axis — see §5.1.

#### Where the sink limit comes from (trigger 4)

Vertical speed is the **flight path** times airspeed now, not the nose attitude
times airspeed, and that changes which arrivals are dangerous.

The rule the pilot used to learn was *"flare and you are safe; arrive nose-down
and you need airspeed"*, and it was true because nose-up simply could not
descend fast when the nose *was* the flight path. It is not true any more, and
losing it is the point of the model rather than a regression: an aeroplane held
nose-high near the stall is descending steeply whatever the nose is doing. The
worst arrival in the whole legal envelope is now a **slow** one, not a fast
one - a flare at 1030, sinking at −903 against a limit of −288.

What survives is the same rule with the speed named:

| Arrival | Worst sink | Verdict |
| :--- | ---: | :--- |
| Nose down at airspeed 2048, pitch −24 | −245 | inside the limit |
| Nose down at airspeed 2048, pitch −32 | −309 | trips trigger 4 |
| Any flare at or above 1.2 × the stall speed | −176 | always survivable |
| Flare at 1030 (just above the stall) | −903 | trips trigger 4 |

So: **a flare with speed in hand is safe, and holding it off until the wing
stops flying is a stall onto the runway** - which is what the classic accident
is, and what the old model could not represent.

#### What that does to trigger 7 (pitch too high)

It can no longer be reached from the air at all, and this is worth stating
because it changes what the trigger is for rather than making it dead code.

A *descending* aircraft with the nose above 64 has its flight path below the
horizon and its nose well above it, so its angle of attack is past the 56 unit
break by construction - the stall break has already fired, and at that excess
it trims the nose down by more in one step than the envelope limit is wide. One
that is *not* stalled at that attitude is climbing, and never arrives. Every
nose-high arrival is therefore caught by trigger 4 instead, and reported as the
sink it is.

On the ground neither applies: the runway is the flight path, there is no
stall, and the nose stays where it is put. **Trigger 7 polices over-rotation on
the roll now**, which is why `kFlightMaxGroundPitch` has to sit below
`kMaxLandingPitch` (§5.2), and `test_touchdown_exact_boundary_limits` tests it
from there.

## 6. Creative Additions & Edge Case Regimes

### 6.1. Vertical Flight & Low-Speed Pitch Recovery

- **No Backward Flight Rule**: airspeed is strictly bounded $V \ge 0$.
- **Straight Up**: thrust acts against gravity, airspeed bleeds away, the angle
  of attack runs up as the flight path falls behind the nose, and the break
  drops the nose into a dive. No special case: the break chases the flight
  path, and at the top of a zoom the flight path is already going down.
- **Holding an input is not holding an attitude.** Pitch-down held forever
  flies an outside loop - the nose keeps rotating, the flight path follows it
  round, and the aircraft comes over the top and climbs away. In the old model
  the same input drove it straight into the ground, because its flight path was
  its attitude. Anything that wants a dive has to hold the *attitude*.
- **Straight Down**: gravity accelerates the aircraft to terminal velocity,
  measured with the nose truly straight down. Note that building that frame by
  writing $\text{front} = (256, 0, -256)$ and normalizing gives a **45°** dive,
  not a vertical one, which is the obvious way to do it and is wrong:

| Configuration | Terminal velocity |
| :--- | ---: |
| Full throttle, clean | **3776** (`0x0EC0`) |
| Full throttle, gear down | 3392 |
| Idle throttle, clean | 2857 |

- **Absolute Speed Clamp**: `kMaxSpeed` = `0x0F00` = 3840. Terminal velocity
  sits under it in every configuration, but the clean dive is now close enough
  to it to be worth watching.

### 6.2. Engine Failure & Gliding Physics

- **Zero Fuel State**: unchanged - throttle drops to 0%, `OUT OF FUEL` is shown
  once, and the aircraft becomes a glider.
- **Optimal Glide**: **7.33 : 1** at $\text{front.z} = -24$ (~ −5.4°), against
  the old model's 6.13 : 1 at −50. Measured as distance over altitude lost at a
  pinned altitude, which matters: measured the obvious way, from a great height,
  the aircraft glides in half-density air above the `0x080000` ceiling knee and
  reads 3.8 : 1 - thin air needs more $C_L$ for the same weight, and induced
  drag goes as its square.

| Held pitch | Glide ratio |
| :--- | ---: |
| −8 | 4.20 : 1 |
| −25 | **7.33 : 1** |
| −50 | 5.54 : 1 |
| −80 | 4.90 : 1 |
| −100 | 4.33 : 1 |

- **A glide at a fixed attitude is not always a steady state.** Below the
  minimum-drag speed the model is on the back side of the power curve: as speed
  falls the angle of attack rises, induced drag rises as its square, and the
  aircraft slows further. That is the real shape of a drag polar and it is why
  the glide table is measured over a run rather than read off a settled
  airspeed.

## 7. Summary Matrix of Flight Test Cases

Throttle is quoted as a percentage of $\text{kMaxThrottle} = 24$, with the raw
value in brackets. Figures are measured, not nominal.

| Scenario / Test Case | Throttle | Pitch Angle | Bank | Gear / Flap | Expected Aircraft Behavior |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **Cruising Level Flight** | 75% (18) | 10 (~2.2°) | 0° | Clean | Level at airspeed 2054, angle of attack 8. Level flight always needs a positive angle; zero pitch is a descent at every throttle |
| **Slow Level Flight** | 50% (12) | 24 (~5.4°) | 0° | Clean | Level at airspeed 1476, angle 22 |
| **Minimum Level Flight** | ~50% (12) | 24 | 0° | Clean | The practical floor. Below it the only level trims are on the back side of the power curve, within a few units of the break |
| **Power-Off Stall** | 0% | Moderate Nose Up | 0° | Clean | Angle of attack reaches 56, $C_L$ droops, nose drops toward the flight path, ~100 steps to recover in a dive |
| **Accelerated Stall** | 100% (24) | Stick full back | 0° | Clean | Breaks at airspeed **2290**, 2.2x the 1 g stall speed. The old model could not stall above 1024 at all |
| **Max Climb** | 100% (24) | 118 (~27°) | 0° | Clean | Best rate of climb +526, slower than the old model's +663 |
| **Moderate Bank Turn** | 75% (18) | Level trim | 36° | Clean | Turns and descends unless the pilot adds angle; holding it level costs airspeed to induced drag |
| **Steep Bank Turn** | 100% (24) | High Nose Up | 72° | Clean | Asks 3.07 g of the wing. Below about airspeed 2000 it runs out of angle first and the turn is wing-limited |
| **Inverted Level Flight** | 75% (18) | 26 (~5.8°) nose up vs the horizon | 180° | Clean | Level at airspeed 2054 - the same speed as upright, because trim $C_L$ is set by the weight. What inverted costs is attitude and stall margin: $C_{L\max}$ is 768 and the stall speed 1182 |
| **Dirty Configuration** | 67% (16) | −17 (nose **below** the horizon) | 0° | Gear & Flaps | Flap camber makes the weight's worth of lift at a negative angle. Floor drops to 41% |
| **Clean Touchdown** | Idle (0%) | $-32 \le$ pitch $\le 64$, **with speed in hand** | 0° | Gear Down | Smooth ground transition over a runway tile. A flare at or above 1.2x the stall speed always survives |
| **Slow Flare** | Idle (0%) | Nose high, near the stall | 0° | Gear Down | **CRASH** (`VERTICAL SPEED`) - the stall onto the runway. New: the old model could not descend fast nose-high |
| **Off-Runway Touchdown** | Idle (0%) | Touchdown Pitch | 0° | Gear Down | **CRASH** (`NOT ON RUNWAY`) even with a perfect attitude |
| **Gear-Up Belly Landing** | Idle (0%) | Touchdown Pitch | 0° | Gear Up | **CRASH** upon ground contact |
| **Inverted Touchdown** | Idle (0%) | Touchdown Pitch | 180° | Gear Down | **CRASH** (`INVERTED`) |
| **Over-Rotation on the Roll** | any | front.z > 64 | 0° | Gear Down | **CRASH** (`PITCH TOO HIGH`). This is what trigger 7 polices now; it cannot be reached from the air |
| **Takeoff** | 100% (24) | Rotate to 48 | 0° | Clean | Unsticks at airspeed 1165 against a derived stall speed of 1024. No rotation constant and no speed gate |
| **Vertical Pitch Up (+90°)** | 100% (24) | Vertical Up | 0° | Clean | Speed bleeds, the flight path falls behind the nose, the break drops the nose into a dive |
| **Vertical Dive (−90°)** | 100% (24) | Vertical Down | 0° | Clean | Settles at terminal velocity 3776, under the `0x0F00` clamp |
| **Best Glide** | 0% | −24 (~ −5.4°) | 0° | Clean | 7.33 : 1 |

## 8. The step, and the rate it is taken at

The model has no elapsed time in it. It advances in fixed steps, so what the
aircraft actually does is **(step size) x (steps per second)** and nothing else.
Two constants set those, and they are deliberately the same knob:

| | where | effect |
| --- | --- | --- |
| `flight_step_shift` | `flight.h` | divides the step |
| `kFlightFramesPerStep` | `flight.h`, `8 >> flight_step_shift` | multiplies the rate |

Their product is invariant, so raising the shift buys smoothness and never
speed. 0 is the C64: one step every 8 raster frames, 6.25 Hz. 2 is a quarter of
the step four times as often, 25 Hz, which is what a 20 MHz machine can hold.

**It is measured at boot, not chosen at build time.** `main()` runs `cpu_probe()`
(`cpu.h`, docs/supercpu.md) and hands the answer to `flight_set_step_shift()`,
which also rescales the six control rotations — they are ordinary RAM read
through a pointer, so they can be rebuilt at init. One binary therefore flies
identically on a stock C64 and on a 20 MHz accelerator, and would do the right
thing on a 4 MHz Turbo Master nobody has tested it on.

There is one binary and no build-time alternative to it. There briefly was: a
`-D__FLIGHT_STEP_SHIFT__` that pinned the shift and folded every scale back to a
constant shift chain. It existed to measure the run-time path against, that
measurement is below, and it was removed once taken — a second code path that
nothing builds and no test covers is a liability, and git has it if the number
ever needs re-deriving.

**What the run-time path costs**, measured on a stock C64 against that pinned
build: `flight_advance()` goes from 6,559 to 7,117 cycles. That is 558 cycles a
step, and at 6.25 steps a second, 0.35% of the machine — plus 295 bytes. The
helper is `__noinline` because of a second measurement: inlining its ten call
sites saved 206 cycles a step and cost 217 bytes, and bytes are scarcer here.

**It is a shift because every per-step quantity is a power of two.** The rate
terms are written as shifts already (`speed_sqr >> 10`, `>> 12`, `>> 5`), and
the six control rotations are matrices whose off-diagonal over 256 is the angle:
32 is 7.2 degrees of roll, 16 is 3.6 of pitch, 8 is 1.8 of yaw. Halving all of
those is exact. Dividing them by seven is not, which is why the model rate is a
power of two times the raster rather than anything matched to a frame rate.

Three terms cannot be divided at all: the flat `-= 2` of ground drag, the floor
of 1 under the stall break, and the fuel burn. Those are gated on `model_substep
== 0` instead — applied once per old step and skipped on the substeps — so their
total over a whole old step is what it always was. At shift 0 the mask is zero,
every one of them fires on every step, and the arithmetic is bit for bit the
model this document describes.

**One model step per rendered frame.** `sim.cc` calls `flight_advance()` exactly
once per iteration of the simulation loop. The model carries no elapsed time, so
the aircraft covers the same ground per frame however long the frame took, and
airspeed through the world moves with what is on screen.

That is a deliberate choice and it was not always this one. Between
`bfc81c2` (18 August 2026) and `9b31591` the timebase was the raster:
`gfx_frame_count`, bumped by the handler at line 250, with a step taken per
`kFlightFramesPerStep` of them in a catch-up loop. That version held airspeed
constant across scenes and machines, and the measurements it was built on are
kept below because they are the honest statement of what the render timebase
costs. It was reverted because the cost landed in the wrong place:

| | per model step, holding roll | worst frame |
| --- | ---: | ---: |
| raster timebase | 22,339 | **44,678** (two steps owed) |
| one step per render | 20,922 | 20,922 |
| and with `vec_turn3_xy()` (below) | **16,769** | **16,769** |

Those three are in-game `MDL` readings taken against the model that came before
the angle of attack, on mission 02 on final with the roll held. They are kept
because the comparison between them is the point of this section and is
unaffected by the change. For what a step costs now, see the bench figures in
§1: they were taken in isolation and are not comparable to the numbers in this
table.

Mission 02 on final, holding roll left, stock C64 in `x64sc`, read off `MDL:`
in the debug view. A frame that overran its budget was also the frame that owed
the model a second step, so the catch-up loop made the worst frame worse - which
is the opposite of what a frame-rate budget wants. One step per render bounds
the model's cost by construction, and the debt that modal screens used to
accumulate cannot exist at all.

**What it costs.** Airspeed through the world now varies with the scene: about
13% between the runway and cruise on a stock C64, and across the three poses in
[framerate.md](framerate.md) whose frame rates run 6.27 to 8.35 fps, up to a
third. The aircraft flies slowest where the view is busiest, which is on final
with the runway filling the viewport. On an accelerated machine the step *size*
still scales with `cpu_step_shift` but the step *rate* no longer does, so the
two halves of that knob no longer cancel: at the 50.1 fps `xscpu64` reaches in
[supercpu.md](supercpu.md) against a stock 7.0, a quarter-size step at shift 2
leaves the aircraft roughly 1.8x fast. That is arithmetic from those two frame
rates, not a measurement - it is the open item on this section. Keeping the
shift is still much better than dropping it, which is the 4.67x the same
document records.

**Where the step actually goes.** Measured by ablation, same pose and harness,
cycles per `flight_advance()`:

| | per step |
| --- | ---: |
| model arithmetic, attitude not changing | 4,934 |
| + the roll-induced turn, as a general 3x3 | +7,089 |
| + `vec_orthonormalize()` | +9,705 |
| **banked and turning** | **22,339** |

Straight and level with no control input the same step is 5,031. That is the
whole spread: `MDL` is bimodal on *whether the attitude is changing*, not on
anything about the timebase.

**The turn is now `vec_turn3_xy()`** and the step is 16,769. The matrix it
replaced was the identity carrying `front.y = rot` and `left.x = -rot`, and the
general routine paid a `vec_fastmul8p8` for all nine entries plus an 18-byte
transposed copy - 27 multiplies where 6 do the work. The replacement is bit for
bit identical, not merely close, and `test/host_vec.cc` sweeps 16 rotations
against 2,197 attitudes to keep it that way; vec.h records the three
`vec_fastmul8p8` identities that make the exactness hold rather than
approximately hold.

**What is left.** `vec_orthonormalize()` is now 58% of the step, and about
4,000 of its cycles are six `vec_div8p8` calls at 554-729 each (`vectest`).
Those three divisions inside each `vec_normalize()` share a divisor, so the
divisor-side work could be hoisted and stay bit-exact, but it is worth only a
few hundred cycles; anything bigger means a reciprocal-multiply, which changes
the rounding - and the truncation behaviour of this routine is load bearing,
since it is what the wing-levelling and nose-wheel comments above are guarding
against. Building without the orthonormalize at all crashes the aircraft within
seconds, so the numerical margin here is not large.

The same "identity plus one antisymmetric pair" shape describes all six
`kVec*` control matrices, so `vec_transform3()` on the input path has the
identical 27-for-6 win available. Measured at **6,191 cycles a frame** while a
control key is held, against a frame of about 146,000. Not done.

**How the scaled model is checked.** Not by `flight_test`: that suite counts
steps — "roll for six frames, then assert the bank" — so a quarter-size step
does not reach the same attitude in the same number of them, and it refuses to
build scaled. It is the reference for the model at shift 0 and it passes there
unchanged. The shift is checked the only way that means anything, by flying the
same four seconds of wall clock from mission 02's start and comparing where the
aeroplane ended up. Distance covered, in world units:

| build | C64 | SuperCPU | ratio |
| --- | ---: | ---: | ---: |
| once per render (before) | 96,122 | 448,602 | **4.67** |
| shift 0, raster timebase | 69,480 | 72,200 | 1.04 |
| shift 2, 25 Hz | 71,838 | 73,229 | 1.02 |
| **one binary, shift measured** | **69,480** | **73,229** | **1.05** |

The last row is the same binary run twice. Its two numbers are not merely close
to the pinned builds above, they are identical to them — the run-time path picks
the same shift each machine would have been compiled with, and then does the
same arithmetic.

The residual few percent is truncation: a quarter-size step loses a little on
every shift, which shows up as slightly less drag and so slightly more speed.

**The C64 got slower, on purpose.** Pinning to 8 raster frames costs about a
quarter of the distance flown at poses where the render was finishing in 6 —
that is exactly the inconsistency being paid off. `kFlightFramesPerStep` is the
one constant to move if it feels sluggish, but only multiples of 4 keep the
halving exact.

