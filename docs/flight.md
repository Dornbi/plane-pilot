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

- **Lift Balance**: $L = W$ (Lift equals Weight). Lift is proportional to Angle of Attack ($\alpha$) and airspeed squared ($V^2$).
- **Trim vs. Airspeed & Throttle**:
  - **High Throttle (75% – 100%)**: Higher equilibrium airspeed $\implies$ lower nose pitch angle required for level flight ($V_{\text{vspeed}} = 0$).
  - **Cruise / Medium Throttle (50%)**: Moderate airspeed $\implies$ moderate nose pitch.
  - **Low Level Throttle (25%)**: Minimum throttle setting capable of sustained level flight. Requires high nose-up pitch angle.
  - **Below 25% Throttle**: Airspeed bleeds below the level flight threshold, resulting in insufficient lift and triggering a stall.

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
  - **25% Throttle**: **Zero excess thrust**. Attempting to climb (positive pitch) at 25% throttle bleeds speed rapidly, leading to a stall.
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

> **Not yet reconciled**: §2.1 describes lift as proportional to angle of attack. The model has no AoA term — see `flight_review.md` §A and §B for the measured consequences (the throttle figures in §2.1, §2.3 and §3.2 and in the §7 matrix are still the un-reviewed originals).

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
  - Sustained inverted level flight requires higher throttle (50%–60%) to overcome this implicit lift-deficit drag.

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
  - Throttle at 0% applies a constant wheel friction drag, decelerating the aircraft to a full stop.

### 5.2. Takeoff Requirements

- **Conditions to Become Airborne**:
  1. Airspeed must strictly exceed stall speed ($V > V_{\text{stall}}$).
  2. Pilot must initiate Pitch UP input ($\Delta \text{pitch} > 0$).
- **Below Stall Speed Behaviour**:
  - Pitch UP below $V_{\text{stall}}$ keeps aircraft on ground (or causes tail-drag penalty without leaving ground).

### 5.3. Touchdown & Crash Envelope Checks

The check triggers whenever altitude $Z \le Z_{\text{min}}$ — **every frame the aircraft is at ground level, not only on the touchdown frame**. It therefore also polices taxi and takeoff roll: rolling with the gear retracted, or with the wings or nose out of limits, fails immediately. This is intended.

The one trigger that must not be applied unchanged during a ground roll is the speed limit, which is an *impact* limit. Full throttle with the gear down settles at 2290, only 270 under $\text{kMaxLandingSpeed}$, so a normal takeoff run would sit uncomfortably close to crashing. The limit is therefore split:

| State | Speed limit |
| :--- | :--- |
| Touchdown (was airborne last frame) | $\text{kMaxLandingSpeed} = \text{0x0A00}$ |
| Already rolling | $\text{kMaxGroundSpeed} = \text{0x0D00}$ |

The looser ground limit still rejects nonsense start states from mission data while leaving the takeoff roll ~45% headroom.

- **Crash Triggers**:
  1. **Gear Retracted**: `flight_gear == 0`.
  2. **Excess Vertical Speed**: Sink rate exceeds limit ($V_{\text{vspeed}} < - \text{0x00E0}$).
  3. **Excess Bank Angle**: Roll/bank exceeds threshold ($|\text{left.z}| > 32$, approx > 7°).
  4. **Invalid Touchdown Pitch**: Touchdown with steep nose-down pitch ($\text{front.z} < -16$, > -3.5° nose down) or excessive pitch flare ($\text{front.z} > 64$, > 15° pitch up). Safe landing pitch range is $-16 \le \text{front.z} \le 64$.
  5. **Excess Airspeed**: Touchdown speed exceeds gear threshold ($V > \text{0x0A00}$).
  6. **Belly-Up Arrival**: Touchdown while inverted ($\text{up.z} < 0$). Trigger 3 does not cover this — `left.z` returns to ~0 after a full 180° roll, so a wings-level inverted arrival passes the bank check. The threshold is 0 rather than a tight $\cos(\text{roll})$ bound because `up.z` also falls with nose-up pitch, and a legal flare must not trip it.

- **Note on trigger 2 — where the limit comes from**: vertical speed at touchdown is $\text{front.z} \cdot V / 256 - \text{sink}$, so sink rate is driven by nose-down pitch *and* by the lift deficit, which grows as speed falls.
  - The limit has to sit inside the range reachable **above stall speed**. A below-stall arrival has already had its nose driven past $\text{kMinLandingPitch}$ by the stall break (§2.2), so trigger 4 owns it and any sink limit that only bites there is redundant.
  - Above stall the reachable range is roughly $-251$ (at $\text{front.z} = -16$, just above stall) to $0$. The limit of $-\text{0x00E0} = -224$ sits inside it.
  - **Resulting rule**: a level-or-nose-up flare ($\text{front.z} \ge 0$) is always survivable — the worst sink it can produce is $-194$. A nose-down arrival needs airspeed: at $\text{front.z} = -16$ the aircraft must be above ~1350 to survive touchdown.
- **Successful Landing**:
  - If all safety thresholds are satisfied: transition to `model_on_ground = true`, zero out vertical speed, level wings, and drop the nose ($\text{front.z} = 0$).
  - The nose drop happens **once, on the touchdown transition**, not eased in over the rollout. Easing it would mean adjusting the attitude on every frame, and `vec_normalize` truncates when it rescales, so a per-frame nudge would ratchet the heading toward the nearest axis — see §5.1.

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
  - Gravity accelerates the aircraft toward Terminal Velocity ($V_{\text{max\_terminal}}$) where drag balances gravity. At full throttle in a vertical dive this balance lands at ~`0x0EF7`.
- **Absolute Speed Clamp**: independently of the drag balance, airspeed is hard-clamped to $\text{kMaxSpeed} = \text{0x0F00}$. Terminal velocity sits just under the clamp, so in normal flight the clamp is not what limits the aircraft — but it bounds `flight_speed` for every downstream calculation regardless of attitude or altitude.

### 6.2. Engine Failure & Gliding Physics

- **Zero Fuel State**:
  - When `flight_fuel == 0`, throttle drops to 0%.
  - Aircraft becomes an unpowered glider.
  - **Optimal Glide Speed**: Best distance-over-ground ratio is achieved at $\text{front.z} = -50$ (**~ -11°**), giving a glide ratio of **~4.96 : 1**. Steeper pitch bleeds altitude fast (~4.0 : 1 at -100, ~2.9 : 1 at -25); flatter than about -8° the aircraft cannot hold glide speed and stalls. The peak sits on a shelf, since settled glide speed moves in steps.

---

## 7. Summary Matrix of Flight Test Cases

| Scenario / Test Case         | Throttle  |      Pitch Angle       | Bank Angle |    Gear / Flap    | Expected Aircraft Behavior                                                                           |
| :--------------------------- | :-------: | :--------------------: | :--------: | :---------------: | :--------------------------------------------------------------------------------------------------- |
| **Cruising Level Flight**    |    75%    |      Low Nose Up       |     0°     |       Clean       | Stable level flight ($V_{\text{vspeed}} = 0$), high speed                                            |
| **Slow Level Flight**        |    25%    |      High Nose Up      |     0°     |       Clean       | Stable level flight at minimum level speed                                                           |
| **Power-Off Stall**          |    0%     |    Moderate Nose Up    |     0°     |       Clean       | Airspeed drops $< V_{\text{stall}}$, nose auto-drops, loses altitude to regain speed                 |
| **Max Climb**                |   100%    |      Positive Up       |     0°     |       Clean       | Sustained climb, airspeed lower than 100% level cruise                                               |
| **Low-Power Climb Attempt**  |    25%    |      Positive Up       |     0°     |       Clean       | Airspeed bleeds rapidly $\rightarrow$ stall onset                                                    |
| **Moderate Bank Turn**       |    75%    |      Level Pitch       |  40% Bank  |       Clean       | Aircraft turns smoothly, slight altitude drop if pitch not added                                     |
| **Steep Bank Turn**          |   100%    |      High Nose Up      |  80% Bank  |       Clean       | High turn rate, heavy drag penalty, severe altitude drop without high throttle & pitch               |
| **Inverted Level Flight**    |    60%    | Pitch Down (vs Canopy) |    180°    |       Clean       | Maintains level inverted flight; high drag penalty                                                   |
| **Dirty Configuration**      |    50%    |      Low Nose Up       |     0°     | Gear & Flaps Down | Lower stall speed, higher drag, lower level speed for given throttle                                 |
| **Clean Touchdown**          | Idle (0%) |    Touchdown Pitch     |     0°     |     Gear Down     | Smooth ground transition, no crash                                                                   |
| **Gear-Up Belly Landing**    | Idle (0%) |    Touchdown Pitch     |     0°     |      Gear Up      | **CRASH** upon ground contact                                                                        |
| **Vertical Pitch Up (+90°)** |   100%    |      Vertical Up       |     0°     |       Clean       | Speed bleeds toward 0 $\rightarrow$ auto pitch-down to dive and regain airspeed (no backward flight) |
