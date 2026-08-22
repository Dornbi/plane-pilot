# Flight Dynamics Model Requirements Specification (`flight.md`)

## 1. Overview & 8-Bit Architecture Constraints

This document specifies the flight dynamics model requirements for the C64 flight simulator project. The flight model is engineered specifically for performance on 8-bit architecture (MOS 6502 @ 1 MHz).

### 8-Bit Implementation Rules

- **Fixed-Point Precision**:
  - Aircraft position ($X, Y, Z$): 24.8 fixed-point (`int32_t`).
  - Velocities ($V_{\text{speed}}, V_{\text{vspeed}}$), angles, and scalars: 8.8 fixed-point (`int16_t` / `uint16_t`).
  - Orientation matrix: 3x3 orthonormal direction cosine matrix (`mat3_t`), with periodic re-orthonormalization (`vec_orthonormalize`).
- **No Floating Point / No Runtime Division**:
  - All aerodynamic equations must use integer shifts (`>>`), 8.8 fast multiplications (`vec_fastmul8p8`), and precomputed lookup tables (LUTs).
- **Execution Budget**:
  - A PAL C64 frame at 50 Hz is **19,705 cycles**. `flight_advance()` is one item in a frame that also has to render the 3D view, so its share has to stay small.
  - The cost splits in two. The scalar path — drag, thrust, lift deficit, the envelope checks — is on the order of **~1,000 cycles**. Re-orthonormalization (`vec_orthonormalize`: 3 × `vec_normalize` + 2 × `vec_cross`) is the dominant term at roughly **~4,500 cycles**, and it runs on any frame where `model_need_normalize` was set — which is most airborne frames, since any control input, any bank, and every ground frame set it.
  - Working budget: **< 6,000 cycles** for a frame that re-orthonormalizes, **< 1,500** for one that does not. That is ~30% of the frame in the common case, so `model_need_normalize` is worth being stingy with.
  - **These figures are estimates from operation counts, not measurements.** `benchmark.h` already provides the harness (`__DEBUG_CYCLES__`), and `flight_advance()` should be wrapped in it and measured on target before the budget above is treated as authoritative.

---

## 2. Core Aerodynamic & Equilibrium Flight Regimes

### 2.1. Straight & Level Flight Dynamics

> **Throttle convention**: throttle is an integer $0 \dots \text{kMaxThrottle} = \text{0x18} = 24$. Percentages below are of that range, so "50%" means throttle 12.

- **Lift Balance**: $L = W$. **There is no angle of attack in this model.** Lift is a function of airspeed squared and bank only — $\text{lift} = f(V^2, \text{up.z})$ — and `front.z` never enters it (§2.4). Pitch does exactly two things: it adds a gravity term to the speed equation ($V \mathrel{-}= \text{front.z} \gg 3$) and it sets the flight path ($V_{\text{vspeed}} = \text{front.z} \cdot V / 256 - \text{sink}$). Pitching up does not make lift, it trades airspeed for climb rate.
- **What "trim" means here**: because the lift deficit is one-sided (§2.4), holding level flight is a matter of pitching up just enough to cancel the sink penalty. Once airspeed reaches $V_{\text{trim}}$ the sink is zero and level flight is $\text{front.z} = 0$ exactly.
- **Trim vs. Airspeed & Throttle** (measured, upright, clean, sea level, run to steady state):

| Throttle | Level flight? | Lowest pitch holding level | Settled airspeed |
| :--- | :--- | :--- | :--- |
| $\le 10$ ($\le 41\%$) | **No** — sinks at every pitch, bleeds to stall | — | — |
| 11 (46%) | Marginal | $\text{front.z} = 20$ (~4.5° nose up) | 1535 |
| 12 (50%) | Yes | $\text{front.z} = 8$ (~1.8°) | 1846 |
| 14 (58%) | Yes | $\text{front.z} = 4$ (~0.9°) | 1982 |
| 16 (67%) | Yes | $\text{front.z} = 0$ | 2111 |
| 18 (75%) | Yes | $\text{front.z} = 0$ | 2173 |
| 24 (100%) | Yes | $\text{front.z} = 0$ | 2509 |

- **Minimum throttle for sustained level flight is ~46% (throttle 11)**, not 25%. Below that the aircraft cannot hold altitude at *any* pitch angle: nose-up bleeds speed faster than the reduced sink saves altitude, and it descends into the ground.
- **From ~67% throttle up, the level trim is $\text{front.z} = 0$ exactly** — airspeed is at or above $V_{\text{trim}}$, the deficit is zero, and any positive pitch is a climb. "Low nose up" is only the correct level attitude in the ~46–58% band.
- With flaps down the floor drops to **throttle 9 (~37%)**, because the flap lift bonus (§4.2) shrinks the deficit.

### 2.2. Stall Dynamics & Automatic Recovery

- **Stall Speeds**:
  - Clean configuration: $V_{\text{stall, clean}} = \text{0x0400}$ (8.8 fixed-point).
  - Flaps down configuration: $V_{\text{stall, flap}} = \text{0x0340}$ (reduced stall speed).
- **Stall Onset**:
  - Triggers when airspeed $V < V_{\text{stall}}$ while airborne.
  - Lift generation drops dramatically.
  - An automatic pitch-down moment is applied by directly decreasing `front.z` in world space ($\Delta \text{front.z} \propto (V_{\text{stall}} - V)$), tilting the nose toward the ground regardless of bank or inverted attitude.
  - **Pitch down always happens towards the ground**, not relative to the aircraft's canopy/belly. This holds at every attitude, including inverted.
- **Near-Vertical Dead Spot** ($\text{front.z} > \text{kMaxStallPitchZ} = 224$, i.e. nose above ~61°):
  - `front` is a unit vector, so with the nose this high its horizontal component is tiny and the end-of-frame `vec_orthonormalize` scales the vector back to length 256, restoring nearly all of a direct change to `front.z`. Pointing straight up it restores all of it and the nose never drops.
  - Above the threshold the break is therefore applied as a **body-axis rotation**, which is well defined at any attitude. One step tips the nose off the vertical; from there the direct path works again.
  - **Direction selection**: a body pitch step moves `front` by $\mp\,\text{up}/16$, so `front.z` changes by $\mp\,\text{up.z}/16$. A "pitch down" rotation therefore *raises* the nose whenever $\text{up.z} < 0$. The rotation is selected by the sign of `up.z` — pitch-up when inverted, pitch-down when upright — so the break still points at the ground.
  - At $\text{up.z} = 0$ (knife-edge with the nose near vertical) neither rotation changes `front.z` to first order; the rotation still changes `front.x`/`front.y`, and subsequent frames recover.
- **Stall Recovery**:
  - Pitching downward causes gravity to accelerate the aircraft ($V_{\text{vspeed}} < 0$).
  - When airspeed accelerates back above $V_{\text{stall}}$, lift is restored.

### 2.3. Climbing & Altitude Mechanics

- **Thrust, Drag & Gravity Vector**:
  - Forward acceleration equation: $\Delta V = \text{Thrust} - \text{Drag} - g \cdot \sin(\theta)$, where $\sin(\theta) = \text{flight\_cam.front.z}$.
- **Climb Performance by Throttle Level**:
  - **100% Throttle**: Maximum rate of climb (ROC). Sustained airspeed during climb is slower than level cruise at 100% throttle due to gravity penalty.
  - **75% Throttle**: Moderate rate of climb at lower airspeed.
  - **~46% Throttle (11)**: **Zero excess thrust** — the floor from §2.1. This is the throttle at which the best achievable vertical speed is barely positive; there is nothing left over for a climb.
  - **Below ~46% Throttle**: *negative* excess thrust. Not merely "cannot climb" — cannot hold altitude either. At 25% (throttle 6) the best any pitch achieves is $V_{\text{vspeed}} = -145$, and pitching up to fight it bleeds airspeed into a stall.
- **Service Ceiling & Altitude Density Decay**:
  - Continuous air density decay applies above ceiling threshold altitude ($Z > \text{0x080000}$):
    $\text{alt\_penalty} = (Z - \text{0x080000}) \gg 12$ (clamped to max 128).
    $\text{density} = 256 - \text{alt\_penalty}$.
  - **Thrust Decay**: Effective thrust scales down: $\text{Thrust}_{\text{eff}} = (\text{Thrust} \cdot \text{density}) \gg 8$.
  - **Lift Decay**: Effective lift scales down: $\text{Lift}_{\text{eff}} = (\text{Lift} \cdot \text{density}) \gg 8$.
  - **Higher Stall Speed at Altitude**: Due to reduced dynamic pressure at high altitude, base stall speed increases proportionally: $V_{\text{stall}}(Z) = V_{\text{stall, base}} + (\text{alt\_penalty} \ll 1)$.

### 2.4. Lift Deficit & Sink Penalty

This is the central mechanism of the model: it is what makes banked turns descend (§3.1), what makes inverted flight expensive (§3.2), and what sets the minimum throttle for level flight.

- **Lift**: $\text{lift} = ((V^2 \gg 2) \cdot \text{up.z} \cdot \text{density}) \gg 16$, with the flap bonus of §4.2 applied afterwards. Note that lift is a function of **airspeed and bank only** — pitch attitude does not enter it.
- **Trim Lift (weight)**: $\text{kTrimLift} = \text{0x1000}$. This is the lift needed to hold altitude.
- **Deficit**: $\text{deficit} = \text{kTrimLift} - \text{lift}$, applied **only when positive**.
- **Sink Penalty**: $\text{sink} = \text{deficit} \gg 4$, subtracted directly from vertical speed:
  $V_{\text{vspeed}} = (\text{front.z} \cdot V) \gg 8 - \text{sink}$.
- **Induced Drag from Deficit**: $\Delta V = -(\text{deficit} \gg 10)$. A wing working below its trim point costs airspeed. This single term stands in for induced drag in inverted flight, in banked flight and at low speed, without any regime-specific conditionals.

**Trim Speed.** The deficit reaches zero when $\text{lift} = \text{0x1000}$. Upright, clean, at sea level this is $V_{\text{trim}} = \text{0x0800}$.

**The deficit is one-sided.** Lift in excess of `kTrimLift` produces no upward force — there is no negative sink. The consequences are worth stating explicitly, because they define what "level flight" means in this model:

- Below $V_{\text{trim}}$: the aircraft sinks, and nose-up pitch is what offsets the sink. Lower airspeed needs more nose-up pitch.
- At or above $V_{\text{trim}}$: sink is zero, and level flight means $\text{front.z} = 0$ **exactly**. Any positive pitch is a climb; there is no faster level trim.

> **Reconciled** with the code as of the current `flight.cc`. §2.1 no longer claims an angle-of-attack term (there is none), and the throttle figures in §2.1, §2.3, §3.2 and the §7 matrix are measured rather than assumed. See `flight_review.md` §A, §B1, §B2 and §B5 for the measurements and the numbers they replaced.

---

## 3. Maneuvering Flight & Banked Turns

### 3.1. Banked Turn Dynamics (Bank Angles: 40%, 80%)

> **Bank percentage convention**: bank is quoted as a percentage of 90°, not as a fraction of `left.z`'s 256 unit range. So 40% bank means 36°, i.e. $\text{left.z} = 256 \sin 36° = 150$; 80% bank means 72°, i.e. $\text{left.z} = 243$.

- **Lift Vector Tilting**:
  - Total Lift vector $L$ aligns with `flight_cam.up`.
  - Vertical lift component: $L_Z = L \cdot \cos(\phi) = L \cdot \text{up.z}$.
  - Horizontal turn force: $F_{\text{turn}} = L \cdot \sin(\phi) = L \cdot \text{left.z}$.
- **Altitude Loss in Banked Flight**:
  - At bank angles of 40% (~36°) and 80% (~72°), $L_Z < W$.
  - Without increasing pitch or throttle, the plane **turns and loses altitude** ($V_{\text{vspeed}} < 0$).
  - **Steep Bank (80%)**: Severe vertical lift loss ($L_Z \approx 30\%$ of total lift). Requires full throttle (100%) and pull-up pitch to maintain level altitude.
- **Turn Rate & Induced Turn Drag**:
  - **Body-Axis Pitching**: Pitching UP rotates around the aircraft's body pitch axis (`left` vector). In steep banked turns (e.g. 80%), pulling back on the stick tightens the horizontal turn radius; the pilot must reduce bank angle toward level flight to raise the nose relative to the horizon.
  - Yaw rate is proportional to bank angle alone: $\text{rot} = \text{left.z} \gg 5$. It does **not** scale with airspeed — a slow banked turn and a fast one turn at the same rate.
  - **Induced Drag Penalty**: Banked turns generate extra drag proportional to bank angle ($C_{D,\text{turn}} \propto \text{left.z}^2$). Airspeed bleeds off as turn rate increases.

### 3.2. Inverted Flight Dynamics (Flying Upside Down)

- **Inverted Lift Mechanics**:
  - Inverted state occurs when $\text{flight\_cam.up.z} < 0$.
  - Normal airfoils produce negative lift when upside down.
  - To maintain level flight inverted, the nose must be pitched **UP relative to the horizon** (pushing stick forward / negative AoA relative to canopy).
- **Implicit Inverted Drag & Lift Deficit**:
  - Inverted flight ($\text{up.z} < 0$) causes wing lift to act downward ($\text{lift} < 0$).
  - The high lift deficit ($\text{deficit} = \text{kTrimLift} - \text{lift}$) implicitly produces high induced drag ($\Delta \text{speed} \propto \text{deficit} \gg 10$), naturally bleeding airspeed without needing ad-hoc conditional logic.
  - **Sustained inverted level flight requires ~71% throttle (17), not 50–60%.** See the table below.

Measured at $\text{up.z} = -256$, clean, sea level, run to steady state:

| Throttle | Level flight? | Lowest pitch holding level | Settled airspeed |
| :--- | :--- | :--- | :--- |
| 12 (50%) | **No** — best vspeed is $-127$ | — | — |
| 16 (67%) | **No** — best vspeed is $-9$ | — | — |
| 17 (71%) | Yes | $\text{front.z} = 82$ (~19° nose up) | 1049 |
| 18 (75%) | Yes | $\text{front.z} = 78$ (~18°) | 1144 |
| 24 (100%) | Yes | $\text{front.z} = 68$ (~15°) | 1698 |

At the 71% trim the aircraft sits at ~1049, a few units above the `0x0400` (1024) stall speed — inverted level flight is flown on the edge of the stall, and any further speed loss breaks it. Note that the required pitch *decreases* with throttle: more speed means more (negative) lift, a smaller deficit, and less sink to cancel.

---

## 4. Flight Configuration & Drag Devices

### 4.1. Landing Gear Dynamics

- **Parasite Drag Increase**:
  - Gear down (`flight_gear == 1`) increases total parasite drag by **25%** ($\Delta \text{Drag}_{\text{gear}} = \text{speed}^2 \gg 12$).
- **Performance Impact**:
  - Reduces top level speed.
  - Increases glide descent angle at idle throttle.
  - No impact on stall speed or wing lift curve.

### 4.2. Flap Dynamics

- **Lift & Drag Modification**:
  - Flaps down (`flight_flap == 1`) increases both Lift Coefficient ($C_L$) and Drag Coefficient ($C_D$).
  - **Drag Increase**: Parasite drag increases by 25% ($\Delta \text{Drag}_{\text{flap}} = \text{speed}^2 \gg 12$).
  - **Lift Increase**: Flaps raise the magnitude of the lift coefficient by **50%**: $\text{lift} \mathrel{+}= \text{lift} \gg 1$. The multiplier applies to the signed lift, so it acts on both upright and inverted flight.
  - **Upright Stall Reduction**: In upright flight ($\text{up.z} \ge 0$) the extra lift lowers the stall speed from `0x0400` to `0x0340`. The two numbers describe the same wing: stall speed scales as $1/\sqrt{C_L}$, and $\text{0x0400} / \sqrt{1.5} = \text{0x0343}$.
  - **Inverted Flap Adverse Camber**: In inverted flight ($\text{up.z} < 0$) lift is already negative, so the same 50% raises the *downward* lift. This is the adverse camber penalty, and it is why the inverted stall speed goes **up**, from `0x0400` to `0x0480`, rather than down.
- **Equilibrium Impact**:
  - Maintaining equal airspeed requires **more throttle** due to flap drag.
  - Upright, at speeds below the trim speed (§2.4), flaps reduce the lift deficit and therefore the sink penalty, so **less nose-up pitch** is needed to hold level flight than in clean configuration. Above the trim speed the deficit is already zero and flaps change nothing but drag.
  - Inverted, flaps make things strictly worse: more drag *and* a larger lift deficit.

---

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

- **Conditions to Become Airborne**:
  1. Airspeed must strictly exceed stall speed ($V > V_{\text{stall}}$).
  2. Pilot must initiate Pitch UP input ($\Delta \text{pitch} > 0$).
- **Below Stall Speed Behaviour**:
  - Pitch UP below $V_{\text{stall}}$ keeps aircraft on ground (or causes tail-drag penalty without leaving ground).

**Rotation is one action, not a pitch step.** Above the gate, the Pitch UP input drives the nose straight to $\text{kRotatePitchZ} = 47$ (~10.6° nose up) and hands the aircraft to the airborne branch, which owns the pitch from there. It is written as a target attitude reached by a bounded loop of `kVecPitchUp` rather than as a fixed number of steps, because the step size is scaled by the host's speed (§8) — a step count would rotate a stock C64 and a SuperCPU to different attitudes and give them different liftoff speeds.

**Why the rotation exists at all.** The gate says when the pilot may rotate; it does not say when the aircraft flies. Because lift has no pitch term (§2.1), rotating cannot make the force that lifts the aircraft off — all pitch can do is aim the flight path up and out-climb the sink penalty of §2.4. With one 3.6° pitch step that crossover sat at **1608**, and the arc on the airspeed dial starts at $V_{\text{stall}}$, so the aircraft read as ready to fly some 600 units before it was. Worse, every frame in between was a full airborne→ground cycle: the wheels lifted, `vspeed` came out non-positive, the ground contact check put them back down and zeroed `front.z`, so the pitch could never accumulate and the pilot could not hold it off into the air. Each of those cycles is a touchdown as far as §5.3 and the sound driver are concerned, so it also rattled the touchdown effect once per frame.

Measured liftoff airspeed, pinned speed with the stick back:

| Rotation attitude | Clean | Flaps down |
| :--- | ---: | ---: |
| $\text{front.z} = 16$ (~3.6°, one step) | 1608 | 1372 |
| $\text{front.z} = 47$ (~10.6°, current) | **1047** | **958** |

Against $V_{\text{stall, clean}} = 1024$ that is 23 units of margin instead of 584, which is what the dial has been promising all along. `kRotatePitchZ` is a fudge that stands in for the missing pitch term in lift — if lift ever grows one, it should be deleted rather than retuned. See `flight_review.md` §A.

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

Vertical speed at touchdown is $\text{front.z} \cdot V / 256 - \text{sink}$, so sink rate is driven by nose-down pitch *and* by the lift deficit, which grows as speed falls.

The limit has to sit inside the range reachable **above stall speed**. A below-stall arrival has already had its nose driven past $\text{kMinLandingPitch}$ by the stall break (§2.2), so trigger 6 owns it, and any sink limit that only bites there is redundant. Above stall the worst reachable sink is $-315$, so the limit of $-224$ sits inside the reachable range and genuinely fires.

The resulting rule the pilot learns is **"flare and you are safe; arrive nose-down and you need airspeed"**. Measured over every legal arrival above stall speed:

| Arrival pitch | Worst sink at that pitch | Minimum airspeed to survive |
| :--- | :--- | :--- |
| $-32$ (the pitch limit itself) | $-315$ | unsurvivable at any speed |
| $-24$ | $-283$ | ~1746 |
| $-16$ | $-251$ | ~1331 |
| $-8$ | $-219$ | any (already inside the limit) |
| $\ge 0$ (any flare) | $-194$ | any |

So triggers 4 and 6 meet with no gap: pitch alone becomes illegal at $-32$, and just above that the sink rate makes the arrival unsurvivable whatever the airspeed. `test_landing_envelope_sink_rate` re-derives the $-315$ and $-194$ figures on every run and asserts both properties — that the limit is reachable at all, and that a flare never trips it.

---

## 6. Creative Additions & Edge Case Regimes

### 6.1. Vertical Flight & Low-Speed Pitch Recovery

- **No Backward Flight Rule**:
  - The plane **never flies backwards**. Airspeed is strictly bounded $V \ge 0$.
  - When airspeed drops close to zero (e.g., during a steep climb or power-off stall), an automatic pitch-down force drives the nose downward toward the earth until forward airspeed is restored above $V_{\text{stall}}$.
- **Straight Up (+90° Pitch / Vertical Climb)**:
  - Thrust acts directly against gravity; airspeed rapidly bleeds toward zero.
  - Before speed reaches zero, the nose pitches down into a dive to regain forward airspeed.
- **Straight Down (-90° Pitch / Vertical Dive)**:
  - Gravity accelerates the aircraft toward Terminal Velocity ($V_{\text{max\_terminal}}$) where drag balances gravity. Measured with the nose truly straight down (so $\text{up.z} = 0$, no lift, and the full lift deficit is charged as induced drag):

| Configuration | Terminal velocity |
| :--- | :--- |
| Full throttle, clean | **3693** (`0x0E6D`) |
| Full throttle, gear down | 3319 (`0x0CF7`) |
| Idle throttle, clean | 2710 (`0x0A96`) |

- **Absolute Speed Clamp**: independently of the drag balance, airspeed is hard-clamped to $\text{kMaxSpeed} = \text{0x0F00} = 3840$. Terminal velocity sits under the clamp in every configuration, so in normal flight the clamp is not what limits the aircraft — but it bounds `flight_speed` for every downstream calculation regardless of attitude or altitude.

### 6.2. Engine Failure & Gliding Physics

- **Zero Fuel State**:
  - When `flight_fuel == 0`, throttle drops to 0%.
  - `OUT OF FUEL` is shown on the frame the tank runs dry, once. Later frames find it already empty and say nothing.
  - Aircraft becomes an unpowered glider.
  - **Optimal Glide Speed**: Best distance-over-ground ratio is achieved at $\text{front.z} = -50$ (**~ -11°**), giving a glide ratio of **~4.96 : 1**. Steeper pitch bleeds altitude fast (~4.0 : 1 at -100, ~2.9 : 1 at -25); flatter than about -8° the aircraft cannot hold glide speed and stalls. The peak sits on a shelf, since settled glide speed moves in steps.

---

## 7. Summary Matrix of Flight Test Cases

Throttle is quoted as a percentage of $\text{kMaxThrottle} = 24$, with the raw value in brackets. Figures are measured, not nominal.

| Scenario / Test Case         |  Throttle   |      Pitch Angle       | Bank Angle |    Gear / Flap    | Expected Aircraft Behavior                                                                           |
| :--------------------------- | :---------: | :--------------------: | :--------: | :---------------: | :--------------------------------------------------------------------------------------------------- |
| **Cruising Level Flight**    |  75% (18)   |   **Zero** pitch       |     0°     |       Clean       | Stable level flight ($V_{\text{vspeed}} = 0$) at speed 2173. Above ~67% throttle the level trim is $\text{front.z} = 0$ exactly; any nose-up is a climb |
| **Slow Level Flight**        |  50% (12)   |  Low Nose Up (~1.8°)   |     0°     |       Clean       | Stable level flight at speed 1846                                                                    |
| **Minimum Level Flight**     |  46% (11)   |  Nose Up (~4.5°)       |     0°     |       Clean       | The floor: level flight at speed 1535. One notch of throttle lower and no pitch holds altitude       |
| **Below Minimum Power**      | $\le$ 41% (10) |  any                |     0°     |       Clean       | Cannot hold altitude at any pitch; sinks, bleeds to stall, descends into the ground                  |
| **Power-Off Stall**          |     0%      |    Moderate Nose Up    |     0°     |       Clean       | Airspeed drops $< V_{\text{stall}}$, nose auto-drops, loses altitude to regain speed                 |
| **Max Climb**                |  100% (24)  |      Positive Up       |     0°     |       Clean       | Sustained climb, airspeed lower than 100% level cruise                                               |
| **Low-Power Climb Attempt**  |  46% (11)   |      Positive Up       |     0°     |       Clean       | Zero excess thrust: airspeed bleeds $\rightarrow$ stall onset. Below 46% this also happens at level pitch |
| **Moderate Bank Turn**       |  75% (18)   |      Level Pitch       |  40% Bank  |       Clean       | Aircraft turns smoothly, slight altitude drop if pitch not added                                     |
| **Steep Bank Turn**          |  100% (24)  |      High Nose Up      |  80% Bank  |       Clean       | High turn rate, heavy drag penalty, severe altitude drop without high throttle & pitch               |
| **Inverted Level Flight**    |  71% (17)   | Nose Up ~19° vs horizon |   180°    |       Clean       | Level inverted flight at speed ~1049 — a few units above stall, so permanently on the stall boundary. Below 71% no pitch holds it |
| **Dirty Configuration**      |  50% (12)   |      Level Pitch       |     0°     | Gear & Flaps Down | Lower stall speed, higher drag, lower level speed for given throttle. Flaps drop the level-flight floor to 37% (9) |
| **Clean Touchdown**          |  Idle (0%)  |  $-32 \le$ pitch $\le 64$ |  0°     |     Gear Down     | Smooth ground transition, no crash — provided the aircraft is over a runway tile                     |
| **Off-Runway Touchdown**     |  Idle (0%)  |    Touchdown Pitch     |     0°     |     Gear Down     | **CRASH** (`NOT ON RUNWAY`) even with a perfect attitude                                             |
| **Gear-Up Belly Landing**    |  Idle (0%)  |    Touchdown Pitch     |     0°     |      Gear Up      | **CRASH** upon ground contact                                                                        |
| **Inverted Touchdown**       |  Idle (0%)  |    Touchdown Pitch     |    180°    |     Gear Down     | **CRASH** (`INVERTED`) — wings-level inverted passes the bank check, so this is its own trigger      |
| **Vertical Pitch Up (+90°)** |  100% (24)  |      Vertical Up       |     0°     |       Clean       | Speed bleeds toward 0 $\rightarrow$ auto pitch-down to dive and regain airspeed (no backward flight) |
| **Vertical Dive (-90°)**     |  100% (24)  |     Vertical Down      |     0°     |       Clean       | Settles at terminal velocity 3693 (`0x0E6D`), under the `0x0F00` clamp                               |

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

**The timebase is the raster, not the render.** `gfx_frame_count` is bumped by
the handler at raster 250 — the one thing in the program that runs exactly once
per frame — and `sim.cc` takes a step per `kFlightFramesPerStep` of them, in a
loop rather than an `if` so that a heavy scene pays its step late instead of
skipping it.

That fixed a bug that predates any of this. The model used to advance once per
*render*, so the aircraft covered the same ground per frame however long the
frame took, and airspeed through the world moved with what was on screen: 13%
between the runway and cruise on a stock C64 (docs/framerate.md), and **4.67x**
on a SuperCPU, where the render is twenty times faster.

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

