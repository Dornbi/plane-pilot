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
  - The frame step computation (`flight_advance()`) must execute within `< 1000` CPU cycles.

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
  - Pitch down always happens towards the ground, not relative to the aircraft's canopy/belly.
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
- **Service Ceiling**:
  - Air density reduction at high altitude mask-shifts effective thrust and lift, reducing ROC to 0 at service ceiling.

---

## 3. Maneuvering Flight & Banked Turns

### 3.1. Banked Turn Dynamics (Bank Angles: 40%, 80%)

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
  - Yaw rate is proportional to $\text{left.z} \cdot V$.
  - **Induced Drag Penalty**: Banked turns generate extra drag proportional to bank angle ($C_{D,\text{turn}} \propto \text{left.z}^2$). Airspeed bleeds off as turn rate increases.

### 3.2. Inverted Flight Dynamics (Flying Upside Down)

- **Inverted Lift Mechanics**:
  - Inverted state occurs when $\text{flight\_cam.up.z} < 0$.
  - Normal airfoils produce negative lift when upside down.
  - To maintain level flight inverted, the nose must be pitched **UP relative to the horizon** (pushing stick forward / negative AoA relative to canopy).
- **Inverted Throttle & Drag Penalty**:
  - Flying upside down incurs a 40% drag penalty due to inefficient inverted airfoil shape.
  - Sustained inverted level flight requires **at least 50% - 60% throttle**. Lower throttle settings cause altitude loss or stall.

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
  - **Lift Increase**: Increases $C_L$ by ~30%, permitting level flight at lower airspeeds.
  - **Stall Speed Reduction**: Lowers stall speed from `0x0400` to `0x0340`.
- **Equilibrium Impact**:
  - Maintaining equal airspeed requires **more throttle** due to flap drag.
  - Maintaining level flight at slow speeds requires **less nose-up pitch** compared to clean configuration.

---

## 5. Ground Interaction & Transition Physics

### 5.1. On-Ground State Dynamics

- **Attitude & Alignment Locks**:
  - Altitude is locked to ground plane ($Z = Z_{\text{min}}$).
  - Pitch attitude is clamped ($\text{front.z} \ge 0$). Negative pitch is prohibited.
  - Wings are locked level ($\text{left.z} = 0$, $\text{up.z} = 256$).
- **Ground Steering**:
  - Roll inputs (J/K) are mapped to nose-wheel steering (yaw left/right).
- **Ground Friction & Braking**:
  - Throttle at 0% applies a constant wheel friction drag, decelerating the aircraft to a full stop.

### 5.2. Takeoff Requirements

- **Conditions to Become Airborne**:
  1. Airspeed must strictly exceed stall speed ($V > V_{\text{stall}}$).
  2. Pilot must initiate Pitch UP input ($\Delta \text{pitch} > 0$).
- **Below Stall Speed Behaviour**:
  - Pitch UP below $V_{\text{stall}}$ keeps aircraft on ground (or causes tail-drag penalty without leaving ground).

### 5.3. Touchdown & Crash Envelope Checks

Touchdown check triggers when altitude $Z \le Z_{\text{min}}$:

- **Crash Triggers**:
  1. **Gear Retracted**: `flight_gear == 0`.
  2. **Excess Vertical Speed**: Sink rate exceeds limit ($V_{\text{vspeed}} < - \text{0x0180}$).
  3. **Excess Bank Angle**: Roll/bank exceeds threshold ($|\text{left.z}| > 32$, approx > 7°).
  4. **Invalid Touchdown Pitch**: Touchdown with negative pitch ($\text{front.z} < 0$, nose pointed down into runway) or excessive pitch flare ($\text{front.z} > 64$, > 15° pitch up). Safe landing flare range is $0 \le \text{front.z} \le 64$.
  5. **Excess Airspeed**: Touchdown speed exceeds gear threshold ($V > \text{0x0A00}$).
- **Successful Landing**:
  - If all safety thresholds are satisfied: transition to `model_on_ground = true`, zero out vertical speed, level wings.

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
  - Gravity accelerates the aircraft toward Terminal Velocity ($V_{\text{max\_terminal}}$) where drag balances gravity.

### 6.2. Engine Failure & Gliding Physics

- **Zero Fuel State**:
  - When `flight_fuel == 0`, throttle drops to 0%.
  - Aircraft becomes an unpowered glider.
  - **Optimal Glide Speed**: Best distance-over-ground ratio achieved at moderate pitch angle (~ -10°). Steeper pitch bleeds altitude fast; flatter pitch stalls.

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
