# Opponent behaviour (`opponent_ai.md`)

A plan for fpilot's opponents to react to the player and shoot back. It
covers:
- a small flight model for the opponents;
- four behaviours a mission picks per aircraft;
- two ways of attacking, including strafing a player on the ground;
- collisions, and opponents hitting the ground;
- a damage model for the player.

*Planned and built 2026-11-01; §12 records what was built and where it
differs from the plan - notably the bombers' rear gunner, which fires
straight back rather than aiming, and the hunters' break-off before a
collision, which the plan did not have.* It builds on
`c64o/opponent.cc` as of ff69c2c, on [planes.md](planes.md) §13, and on
[projectiles.md](projectiles.md) §6, whose opponent-gun provision it fills
in. §10 is the step-by-step implementation order. Everything is fpilot only
(`__ENABLE_OPPONENT__`); ppilot does not change.

## 1. What it has to do

**Behaviours,** chosen per aircraft by the mission:

1. **Path.** Flies its waypoints, whatever happens.
2. **Provoked.** Flies its waypoints until the player shoots at it, then
   attacks.
3. **Hunt.** Attacks the player from the start. A new mission, 05 "Head
   on", uses this: the player starts at mid altitude, and two fighters
   come in head-on from a distance, hunting.
4. **Pass.** Flies its waypoints once, then attacks. Mission 04 uses this:
   a straight pass over the airfield, then the aircraft circles back to
   hunt.

**Roles,** chosen per aircraft, each with its own attack:

- **Fighter.** Turns, climbs and descends towards the player at whatever
  angle points its nose at them, and fires its forward guns when the
  player is in front of the nose. If the player is on the ground, it makes
  strafing runs: it dives to a temporary low limit, then pulls back up to a
  slightly higher permanent one.
- **Bomber.** Never leaves its path. A rear gunner fires at a player who
  is behind it, whatever the bomber's behaviour.

No aircraft is unarmed. An aircraft with the Path behaviour never attacks,
but a bomber's gunner does not wait for that: today's recon is a bomber on
a path, brown when near, and shoots at a player on its tail.

**Also:**

- **A simple opponent flight model.** The aircraft climbs or descends at
  one of a few fixed angles, or flies level. In level flight it settles at
  its default speed. It slows while climbing and speeds up while
  descending, within a min/max speed range. At the minimum speed it cannot
  climb any more, and at the maximum it cannot descend any more.
- **An opponent that hits the ground is destroyed.** This should be
  unlikely, because of the height limits.
- **A collision between the player and an opponent is a crash.** The
  opponent is destroyed too.
- **Bombers are dark brown when near** (`kColorBrown`, C64 colour 9). They
  have the same shape as the fighters. Far away they are black, like every
  other opponent.
- **Player damage.** Each hit causes one random new failure, and a
  blinking message says which. The fifth hit shoots the player down.
  Nothing appears on the panel, and nothing repairs a failure.

## 2. What is there today

- **Flight.** `opponent_step()` flies each slot level, at its path's one
  speed, steering bang-bang at one point (the next waypoint) at a fixed
  8/256 rad a step. A comment there already says that reacting to the
  player "would only change the point", which is what this plan does.
- **Heading.** The heading is a level unit vector. The bank is drawn
  (±30°), but it does not affect the flight path.
- **Rounds.** `gun.cc` keeps an owner per round (`_round_owner`), and
  `gun_step()` already tests every non-player round against the eye and
  counts `gun_player_hits`. `_gun_shown()` already draws the youngest enemy
  round as a light-red tracer. Nothing fires enemy rounds yet, and nothing
  reads the hit count.
- **Sprite budget.** `_opponent_cap()` already counts a tracer and the
  smoke.
- **Redraws.** At most one projection or one fill runs a frame, across
  both slots ([planes.md](planes.md) §13). That limit bounds what
  manoeuvring opponents can cost a frame (§9).
- **Player hooks.** `flight.cc` has every hook the damage model needs:
  - the throttle steps up to `kMaxThrottle` (`FLIGHT_INPUT_THROTTLE_UP`);
  - fuel burns `flight_throttle` each whole step;
  - flaps and gear toggle in one `switch`;
  - every airborne rotation goes through one `vec_rotate_step()` call.
- **Messages.** `msg_show()` shows a message for a duration, in black, on
  row 0. It cannot blink yet.

## 3. Data

`opponent_path_t::kind` uses the values 0–3 today. The role and the
behaviour go in its high bits, so the path table does not grow:

```c
// bits 0-1: kOpponentNone / Loop / OneWay / Parked, as now
// bit 2:    role
static const uint8_t kOpponentFighter = 0 << 2;
static const uint8_t kOpponentBomber  = 1 << 2;
// bits 3-4: behaviour
static const uint8_t kOpponentPath     = 0 << 3;
static const uint8_t kOpponentProvoked = 1 << 3;
static const uint8_t kOpponentHunt     = 2 << 3;
static const uint8_t kOpponentPass     = 3 << 3;
```

`speed` becomes the **default** speed, the one held in level flight. A
two-entry table by role holds the rest:

| | Fighter | Bomber |
| :--- | :--- | :--- |
| Speed range | 1,300–2,800 | 1,100–2,000 |
| Turn | ~16/256 rad, about half the radius | 8/256 rad, as now |
| Steepest climb or descent | level 3 | level 1 |
| Bank drawn | 60° | 30° |

Climb levels are defined in §4.

At run time a slot is in one of two **modes**:

| Mode | Fighter | Bomber |
| :--- | :--- | :--- |
| `PATH` | steers for its next waypoint, guns off | steers for its next waypoint, rear gunner on |
| `ATTACK` | steers for the player, forward guns on; strafes if the player is on the ground (§5) | steers for its next waypoint, rear gunner on |

How each behaviour enters `ATTACK`:
- Hunt starts in `ATTACK`.
- Path, Provoked and Pass start in `PATH`.
- A Provoked slot switches to `ATTACK` when it is fired upon (§6).
- A Pass slot switches to `ATTACK` at its last waypoint, where a one-way
  path would otherwise leave the map.

**New state.** All of it is striped, in `bss2` like the rest.

| | Bytes |
| :--- | ---: |
| `_opponent_speed[i]`: current speed | 2 |
| `_opponent_climb[i]`: climb level, −3..+3 | 1 |
| `_opponent_pitch[i]`: the level drawn, held like the bank | 1 |
| `_opponent_gun[i]`: burst count and cooldown | 1 |
| `_opponent_prev[i]`: last step's offset to the eye, for collisions (§7) | 6 |
| ×2 slots | 22 |
| Masks: mode, pulling up (§5); and a 1-byte LFSR | 3 |
| **Total** | **25** |

## 4. The opponent flight model

**Climb levels.** A climb or descent is one of a few fixed slopes,
vertical over horizontal:

| Level | Slope | Angle | Drawn sin, cos (8.8) |
| :--- | :--- | ---: | :--- |
| 0 | level | 0° | 0, 256 |
| ±1 | 1/8 | ~7° | ±32, 254 |
| ±2 | 1/4 | ~14° | ±62, 248 |
| ±3 | 1/2 | ~27° | ±114, 229 |

A slope of 1/2ⁿ is a shift, so the climb rate is `speed2 >> n`, with no
multiply. Bombers and every aircraft in `PATH` use only level ±1. Only
fighters in `ATTACK` use the steeper levels, to point their guns at the
player.

**Each whole model step,** per slot, after the steering (§5) has picked a
level `want`:

```
climb = clamp(want, -role.max_level, role.max_level)
if (speed <= role.min_speed) climb = min(climb, 0)   // too slow to climb
if (speed >= role.max_speed) climb = max(climb, 0)   // too fast to descend
if (z < floor && climb < 0)  climb = 0              // floor: §5

speed -= kClimbDecel << (|climb| - 1)     climbing: more, the steeper
speed += kDiveAccel  << (|climb| - 1)     descending
speed += sign(path.speed - speed) * kLevelAccel     level: back to default

z += step_s(±(speed2 >> (4 - |climb|)))   // 1/8, 1/4, 1/2 of the speed
x += step_s(speed2 * hx); y += step_s(speed2 * hy)   // as now
if (z <= 0) destroy(i)                               // §7
```

- **The speed range does the limiting.** A fighter can zoom from its
  maximum speed down to its minimum. Then it levels off, and has to
  regain speed before it climbs again. A long dive runs up to the maximum
  and then levels off.
- **Starting values:** `kClimbDecel` and `kDiveAccel` about 24 a step at
  level 1, `kLevelAccel` about 16. These are for tuning in play.
- **Parked aircraft** (mission 03) have no airspeed and skip the model,
  as they do now.
- **Whole steps only,** through `_opponent_substep`, as the turn does now,
  so that a faster machine flies the same manoeuvres.
- **No cos(γ) correction** along the ground. At the steepest level it
  would be 11%, and nobody can see that.

## 5. Steering

### One target point

The steering stays as it is and gains a height axis. The target is:

- **in `PATH`, and for a bomber in `ATTACK`:** the next waypoint, at the
  path's height `p->z`;
- **for a fighter in `ATTACK`:** the eye (`flight_eye_*`), except while it
  is pulling up from a strafing run (below).

`_opponent_toward()` already takes a target in 8.8 units, and
`(uint16_t)(flight_eye_x >> 8)` is one. Height is not wrapped, so `dz` is a
plain difference. Then:

- **Horizontal.** Unchanged: the cross and dot products of the heading
  against the direction, cut to 6 bits a component, give a bang-bang turn
  with a 1.8° dead band. The turn rate comes from the role.
- **Vertical.** `dz` is cut by the same shifts as `dx` and `dy`, so the
  ratio holds. With the horizontal distance taken as `h = max + min/2` of
  |dx| and |dy|:
  - `want` is the steepest level whose slope `|dz|` still reaches:
    `|dz| ≥ h >> 1` gives 3, `≥ h >> 2` gives 2, `≥ h >> 3` gives 1;
  - inside `h >> 5`, `want` is 0;
  - in `PATH`, it is only ever ±1, and only outside a ~20 m dead band
    around the path's height.

  These are shifts and compares, with no multiply. The nose ends up
  pointing at the player within a level's step.
- **Pure pursuit, no lead.** A faster fighter overshoots, passes, and
  turns back for another pass. That reads as a dogfight. Two optional
  refinements:
  - match `flight_speed` inside ~150 m behind the player (~30 B);
  - lead the player (~80 B).
- **Range.** `_opponent_toward()` is good to 64 units (8 km) on each axis:
  the whole world on x, but a quarter of it on y. Hunt and Pass missions
  keep their fighters within that, and a `test/mission_test.cc` check
  holds them to it.

### Height limits and strafing

Two floors, for fighters in `ATTACK`:

- **`kFloor`, the permanent minimum, ~60 m.** Below it the fighter does
  not descend, and if it is below it, `want` is at least +1.
- **`kStrafeFloor`, the temporary minimum, ~30 m.** Allowed only on a
  strafing run.

Path heights are the mission's own choice. The floors apply only in
`ATTACK`, so mission 04's pass can stay low.

A strafing run needs to know whether the player is on the ground.
`flight.cc`'s `model_on_ground` is static, so it is exported as
`flight_on_ground()`. The fighter's state is one bit, *pulling up*:

```
run       = player on ground && !pulling_up && player within ~15° of the nose
floor     = run ? kStrafeFloor : kFloor
if (run && (z <= kStrafeFloor + margin || distance < ~150 m || dot < 0))
    pulling_up = true                 // end of the run
if (pulling_up) {
    want = +1 (or more, up to kFloor), and no turning: extend straight
    if (z >= kFloor && distance > ~800 m) pulling_up = false   // turn back for the next run
}
```

- **A run.** The fighter dives at the player at the angle that points its
  nose at them, and fires as the player comes into the cone (§6).
- **The pull-up.** At the temporary floor, or on passing the player, it
  climbs back to the permanent floor. It flies straight until it is
  ~800 m out, then turns back and the next run begins.
- **After take-off.** Once the player is in the air, `run` is false and
  `kFloor` holds. A pull-up in progress still finishes.

## 6. Guns

### Firing a round: `gun_fire_from()`

`gun.cc` gets one entry point:

```c
// A round from opponent slot i at world position p (24.8 half metres),
// along dir (unit, 8.8), with the shooter's own velocity added.
void gun_fire_from(uint8_t i, const int32_t *p, const int16_t *dir,
                   const int16_t *vel);
```

- It brings `p` into the rounds' frame, relative to the eye, with
  `_gun_rel3_to()`.
- It sets the velocity to `3 × dir + vel >> 5`, as `_gun_fire()` does, and
  the owner to `1 + i`.
- **Slot choice:** a free slot, or else the oldest *enemy* round. Enemy
  fire never takes a round the player has in flight. If neither exists,
  the round is not fired. The pool stays at 8.

### Fighters: forward guns

A fighter fires when all of these hold, on the step's steering numbers:

- it is in `ATTACK`;
- the player is within ~7° of the nose:
  - `dot > 0` and `|cross| < dot >> 3`;
  - `|dz − slope(climb) · h| < h >> 3`, where the slope is a shift;
- the player is within ~500 m on every axis, tested on the half metres
  before the 6-bit cut.

A burst is 3 rounds, one every second whole step, followed by a cooldown of
~20 steps. Rounds leave the nose along `(c·hx, c·hy, s)`, with `s` and `c`
from the level's row of the §4 table. Firing costs 2 multiplies.

The guns do not aim. A pilot who keeps off the nose is safe, as
[projectiles.md](projectiles.md) §6 decided. A strafing run puts the nose
on a parked player, so the run is where the danger is.

### Bombers: the rear gunner

The gunner fires when all of these hold:

- the bomber is flying - in any mode, but not parked;
- the player is behind it, within 45° of its tail to either side:
  `back = −dot > 0`, `|cross| < back`;
- the player is from 45° above the plane of its wings to ~10° below it:
  `−back × 3/16 ≤ dz < back`;
- `back` is under ~600 m.

The gunner aims at the player:

- The direction is the line to the eye, each component divided by twice
  `back` (inside the cone that is under 0.71, so the vector is well inside
  what `vec_normalize()` takes), normalised, then scattered by
  `kOpponentScatter` like the fighters' rounds.
- The bomber keeps its heading.
- It fires a burst of three on the fighters' gun cycle.
- The scatter sets the difficulty. Without it, almost every round would
  land in a steady tail chase.

### Fired upon

A Provoked slot switches to `ATTACK` on either of these:

- **A hit:** one line in `opponent_strike()`.
- **A near miss:** a player round passes within ~40 m. `_gun_hits()` takes
  the radius as a parameter, and only slots that are still provokable are
  tested again at the larger radius.

## 7. Collisions and the ground

- **The ground.** After the move, `z <= 0` destroys the slot, the same
  way a third hit does:
  - its down bit is set and its smoke hangs for `kSmokeDown`;
  - it counts towards `WP_TARGETS_DOWN`;
  - the message is "ENEMY CRASHED".

  `opponent_strike()`'s down branch becomes `_opponent_destroy(i)`, which
  both share. Parked aircraft sit at z > 0 and never move, so they never
  get here.
- **The player.** Each step, each live slot's offset to the eye is tested
  against a box of ±12 m, about the drawn span, on all three axes. Parked
  slots are included, so taxiing into one counts.
  - **Why two points.** Closure can reach ~26 m a step (the player at
    15 m and a fighter at its maximum, head-on), which could jump a 24 m
    box between steps. So the test runs at this step's offset and at the
    midpoint with last step's (`_opponent_prev`).
  - **On a hit**, the player crashes with a new last crash status,
    `FLIGHT_CRASH_COLLISION` ("COLLISION" in `kFaultText`), and the slot
    is destroyed.
- Opponents never collide with each other. A formation's spacing (mission
  02: 75 m and 100 m) keeps them apart.

## 8. Player damage, messages and drawing

### The failures

| Failure | Effect | Message |
| :--- | :--- | :--- |
| **Engine** | The maximum throttle is halved, and the throttle is cut to it if it is above. | ENGINE DAMAGED |
| **Fuel leak** | Fuel burns at 2× the throttle. | FUEL LEAK |
| **Oil leak** | After ~30 s (about 200 whole steps) the maximum throttle drops to 0, and the throttle with it. The aircraft glides. | OIL LEAK, then ENGINE SEIZED |
| **Hydraulics** | Flaps and gear stay where they are, and their keys do nothing. | HYDRAULICS HIT |
| **Controls** | Roll, pitch and yaw take only every other input: half the rate. | CONTROLS DAMAGED |

- **Which failure.** The LFSR picks one of the five. If the player already
  has it, the next one in the list that they don't have is taken instead.
  A failure never repeats, never gets worse, and is never repaired.
- **Armour.** *Since 2026-11-05:* the airframe takes the first three hits,
  and a fourth in half the flights (`damage_armour`, picked by
  `damage_reset()`), with nothing broken: the border flashes and the
  message says YOU HAVE BEEN HIT. The failures and the fall below count from the
  hit after those.
- **Shot down.** Hits 1–4 each cause a failure. The fifth ends the flight
  with a new last crash status, `FLIGHT_CRASH_SHOT_DOWN` ("SHOT DOWN"), so
  there is always a new failure to pick.
- **The message blinks.** `msg_show()` gains a `blink` flag. While it is
  set, `msg_render()` writes the text in black and white on alternate
  pairs of frames: one extra byte, and a compare on the duration's bit 1.
  It is for new damage only, for ~3 s. No border flash: the border sits
  under the raster splits.
- **Nothing on the panel.** The messages are the only display.

**Where it goes.** A small `damage.cc`/`damage.h`, built like `gun.h`.
- **State:** the failure bits, the oil countdown and the control toggle,
  3 bytes. The throttle cap and the fuel shift are read from the failure
  bits.
- **Outside fpilot,** inline stubs return the undamaged values, so ppilot
  compiles to what it is today.
- **Hooks in `flight.cc`,** one line each:

```c
// FLIGHT_INPUT_THROTTLE_UP
if (flight_throttle < damage_throttle_max()) ...
// fuel; 2x of kMaxThrottle 0x18 is 0x30, still a byte
uint8_t fuel_consumption = whole_step ? flight_throttle << damage_fuel_shift() : 0;
// FLIGHT_INPUT_TOGGLE_FLAP / _GEAR
if (damage_hydraulics()) return;
// airborne rotation
if (damage_skip_input()) return;   // true every other call once damaged
```

- **Calls from outside.** `gun_step()` calls `damage_hit()` where it now
  does `++gun_player_hits`. `damage_step()`, next to `gun_step()`, runs the
  oil countdown. `damage_reset()` runs with `opponent_reset()`.
- **For free:** the engine sound and the rpm needle follow
  `flight_throttle`, so a damaged or seized engine sounds and reads right
  without extra work.

### Bomber colour and drawn pitch

- **Colour.** `_opponent_draw()` takes the near colour from the role:
  `kColorBrown` for bombers, `kColorOpponentNear` for fighters. Far away,
  both are `kColorOpponentFar`.
- **Pitch.** The level drawn follows `climb` once a level has lasted two
  steps, as the bank does, so one-step corrections don't flick the nose.
  With `s` and `c` from §4's table:

  ```
  front = (c·hx, c·hy, s)
  left  = (−hy, hx, 0)          unchanged
  up    = (−s·hx, −s·hy, c)
  ```

  Each of the camera's two rows reuses `h = row.x·hx + row.y·hy`:
  `front·row = c·h + s·row.z` and `up·row = c·row.z − s·h`. That is 8
  multiplies a projection, skipped at level 0. The bank is applied after
  this, as now.

## 9. Cost

### RAM

fpilot today has **3,357 B free and allocatable, largest run 2,741 B**
(`tools/analyze_ram.py c64o/fpilot.map`, ff69c2c). For scale,
`opponent_step()` is 720 B as built.

| Piece | Code + data, B |
| :--- | ---: |
| Role/behaviour bits, role table, modes, Pass and Hunt | 60–90 |
| Opponent flight model: levels, speed range, z move | 150–200 |
| Target point and height steering | 90–130 |
| Floors and strafing runs, `flight_on_ground()` | 90–130 |
| Ground impact, `_opponent_destroy()` | 30–50 |
| Collision: two-point box test, `FLIGHT_CRASH_COLLISION` and its text | 90–130 |
| Provoking by hit and near miss | 60–80 |
| `gun_fire_from()` and the enemy-slot rule | 130–170 |
| Fighter guns: cone, burst, cooldown, pitched nose | 110–150 |
| Rear gunner: cone, scatter, LFSR, normalise | 120–160 |
| Player damage: random pick, five hooks, oil countdown | 150–200 |
| — its messages, "SHOT DOWN", `FLIGHT_CRASH_SHOT_DOWN` | 120–150 |
| — message blink | 20–30 |
| Drawn pitch in `_opponent_axes()`, its table; bank by role | 80–110 |
| Bomber colour | 10–20 |
| Mission data (02, 04), and mission 05's row in every table | 60–100 |
| State (§3, §8) | 28 |
| **Total** | **~1,400–1,930** |

That leaves about 1.4–2.0 KB.

`-Op` page-aligns every array of 256 B or less, which can hide a saving or
a loss in the gaps. So every step is measured with `analyze_ram.py`
("Free, allocatable" and "largest run"), not with the prg size.

Optional extras, not in the total:
- the tail-sitting speed match, ~30 B;
- lead, ~80 B;
- 12 rounds instead of 8, +56 B of state.

### Cycles

The frame is one model step plus one render: about 120,000–160,000 cycles
at fpilot's 6–8 frames a second. The estimates assume ~150 cycles a
`vec_fastmul8p8` and ~1,500 a `vec_normalize`.

Per model step (`opponent_step()`, `gun_step()`, `damage_step()`):

| Situation | Added cycles a step | Share of a frame |
| :--- | ---: | ---: |
| One slot on its path (height axis, level, speed, z, ground, collision) | ~450 | 0.3% |
| One fighter attacking or strafing, not firing | ~700 | 0.5% |
| — on a firing step | +~1,300 | |
| One bomber, gunner watching | ~600 | 0.4% |
| — on a firing step (normalise and round) | +~2,500 | |
| Each enemy round in flight, tested against the player | ~150–400 | |
| Player damage hooks | < 100 | |
| **Worst step:** two fighters firing, 4 enemy rounds up | **~5,200** | **~3.5%** |
| **Typical:** two path aircraft, nothing firing | **~1,000** | **< 1%** |

Per frame, the render:

- **Pitch in the axes:** ~1,200 cycles, only on a frame that projects a
  slot that is climbing or descending.
- **The worst frame does not change.** It is still one redraw at most,
  across both slots: 29,000 cycles at 75 m, or 46,000 for 2 × 2 at 30 m
  ([planes.md](planes.md) §11).
- **The average close-fight frame does change.**
  - A manoeuvring aircraft changes its axes more often, so frames that
    were cache hits (~3,000) become re-projections (15,000–29,000).
  - This happens only inside ~500 m, and mostly while the player holds
    still.
  - Seven fixed pitch levels, each held for two steps, limit the extra
    changes.
  - Expect perhaps 5–10% fewer frames a second in a close fight. Step 14
    measures it.

## 10. Implementation steps

Every step follows the same routine:

- builds fpilot and ppilot;
- runs the host tests (`c64o/test`);
- records `analyze_ram.py` "free, allocatable" and "largest run" in its
  commit message;
- is committed on its own on main.

The ppilot binary must not change, except in the steps that touch shared
files (`flight.cc`, `msg.cc`), where its size change is recorded.

1. **Baseline.** Record fpilot's RAM (3,357 / 2,741), and a VICE frame
   timing of mission 04's close pass at ~100 m, to compare against in
   step 14.
2. **Data and modes, no behaviour.**
   - Code: the role and behaviour bits in `kind`, the role table, the mode
     mask; every current path marked Path.
   - Tests: `opponent_test` and `mission_test` pass unchanged, and a check
     that every path's role and behaviour decode as intended.
3. **The opponent flight model on today's paths.**
   - Code: `_opponent_speed`, the climb levels, the speed range, the
     z move, and holding the path's height at level ±1.
   - Behaviour: missions play exactly as now, since every path is level.
   - Tests (an `opponent_test` path with a 600 m climb in it):
     - level flight settles at the default speed;
     - a climb stops at the minimum speed and goes on after the speed
       recovers;
     - a long descent levels off at the maximum;
     - the path's height is reached and then held.
4. **Drawn pitch.**
   - Code: the level table, `_opponent_pitch` with its two-step hold, and
     pitch in `_opponent_axes()`.
   - Tests: the axes stay orthonormal to within rounding at every level
     and bank, on the host.
   - VICE: a shot of the step 3 test path, climbing.
5. **Destroyed on the ground.**
   - Code: `_opponent_destroy()` shared with `opponent_strike()`, and the
     `z <= 0` check.
   - Tests: a test path into the ground destroys its slot, shows smoke,
     and counts towards `WP_TARGETS_DOWN`.
6. **Pursuit and the floor.**
   - Code: `ATTACK` steering for fighters (target the eye, steeper levels,
     the role's turn rate), `kFloor`, and the Hunt and Pass behaviours.
   - Tests:
     - a hunting fighter gets within 300 m behind a straight-flying player
       within N s;
     - it climbs and descends to follow the player's height changes;
     - it never goes below `kFloor` while the player is in the air;
     - a Pass slot switches to `ATTACK` at its last waypoint and turns
       back.
   - Mission 05, "Head on": the player starts at 3,000 ft heading north at
     cruise, and two hunting fighters start ~6 km north, at about the same
     height, heading south. They meet head-on, pass, turn and chase.
     `kMissionCount` goes to 5, with a row in every mission table.
     `mission_test`'s plain pilot must see them within 10 s and be within
     1 km inside 90 s.
7. **Strafing.**
   - Code: `flight_on_ground()`, the pulling-up bit, `kStrafeFloor`, and
     the run, pull-up and extend sequence.
   - Tests (player parked on runway 1, against a hunting fighter):
     - runs repeat;
     - every run gets below `kFloor` and never below `kStrafeFloor`;
     - each pull-up climbs back to `kFloor` before the next run;
     - nothing hits the ground.
8. **Mission 04: the pass, then the hunt.**
   - Data: both fighters become Pass, on one-way paths. They make a
     straight low pass along runway 1 as now, then carry on ~2.5 km north
     to their last waypoint, switch to `ATTACK`, and circle back to hunt.
     If the player is still on the ground, they strafe.
   - Tests: `mission_test`'s 04 pilot still reaches and downs them.
   - VICE: play the mission. This is the step at which the behaviour
     first shows in the game, before any guns.
9. **Opponent fire, fighters.**
   - Code: `gun_fire_from()` with the enemy-slot rule, the fighter cone,
     bursts and cooldown, and the pitched nose.
   - Tests:
     - player rounds are never displaced by enemy rounds;
     - a fighter nose-on within 500 m fires a burst that hits a stationary
       eye;
     - a fighter off-nose by 10° doesn't fire;
     - a strafing run on a parked player lands hits.
10. **Opponent fire, bombers.**
    - Code: the rear gunner, the LFSR, and the scatter.
    - Data: mission 02's bombers become Hunt.
    - Tests: in a 60 s tail chase at 300 m, one bomber lands hits at a rate
      inside a set range, which tunes the scatter.
11. **Fired upon.**
    - Code: Provoked by a hit and by a near miss; `_gun_hits()`'s radius
      becomes a parameter.
    - Tests:
      - a Provoked slot holds its path through rounds passing at 60 m;
      - it switches at 30 m, or on a hit.
12. **Player damage.**
    - Code:
      - `damage.cc`/`damage.h` with the random pick;
      - the five hooks in `flight.cc` and the oil countdown;
      - `msg_show()`'s blink;
      - `FLIGHT_CRASH_SHOT_DOWN` and its text.
    - Tests (`flight_test`):
      - each failure's effect;
      - four hits give four different failures, whatever the LFSR's state;
      - the oil leak seizes the engine after its count;
      - the fifth hit ends the flight;
      - ppilot unchanged.
13. **Collision.**
    - Code: the two-point box test, `FLIGHT_CRASH_COLLISION` and its text,
      and the slot destroyed.
    - Tests:
      - a head-on pass at the maximum closure through the box's centre is
        caught;
      - a pass 30 m to one side is not;
      - taxiing into a parked aircraft is caught.
14. **Colour, timing and docs.**
    - Code: brown bombers.
    - Measure: a VICE frame timing of a close fight in mission 04 against
      step 1's baseline. If it is more than ~10% worse, try drawing only
      levels 0 and ±2.
    - Docs: an "As built" section here, and the fpilot line in the
      README.

Steps 2–8 give opponents that fly and hunt without guns, and can be played
from step 8 on. Steps 9–13 are independent of each other once step 9's
`gun_fire_from()` is in.

## 11. Open questions

- **Should provoked fighters return to their path** when the player
  breaks off beyond ~3 km? Not planned: once in `ATTACK`, they stay.
- **Are the numbers right?** The two floors (60 m and 30 m), the speed
  ranges, 5 hits to be shot down, the gunner's scatter and the oil leak's
  30 s are all starting points for play.

## 12. As built

### Step 1: baseline (2026-11-01, at cdecc75)

- **RAM.** fpilot has 3,357 B free and allocatable, with a largest run of
  2,741 B. ppilot has 10,901 B free, with a largest run of 10,333 B.
- **Frame cost** (`tools/bench.sh`, cycles per frame on x64sc):

  | Scene | Cycles |
  | :--- | ---: |
  | start | 119,483 |
  | approach | 143,666 |
  | cruise | 152,265 |
  | town | 144,290 |
  | lake | 134,002 |
  | scramble | 176,257 |
  | shootdown | 172,707 |

  `scramble` is mission 04's close pass, which step 14 compares a close
  fight against.
- **Fix.** `bench.sh` had stopped building at ff69c2c: it did not copy the
  panels' precompressed `.lzo` files into its scratch tree.
  `make_shots.sh` already did.

### Step 2: roles, behaviours and the mode mask

- **Code.** `opponent_path_t::kind` keeps the path kind in bits 0–1. Bit 2
  is the role (`kOpponentFighter`, `kOpponentBomber`) and bits 3–4 the
  behaviour (Path, Provoked, Hunt, Pass). `_opponent_attack` is the mode
  mask; `opponent_reset()` sets a hunting slot's bit.
- **Missions.** Every mission is still Path, with mission 02's two
  aircraft as bombers, so nothing flies differently yet.
- **Not yet.** The role table waits for step 3, the first step that reads
  it.
- **Test.** `opponent_test` checks each mission's roles and behaviours, and
  the mode each slot starts in.
- **RAM.** fpilot 3,357 → 3,323 B free (largest run 2,709). ppilot is
  unchanged at 10,901.

### Step 3: the flight model on today's paths

- **Code.** `_opponent_fly()` does the work: on a whole step it checks the
  speed limits and updates the speed, then on every step it moves the
  height.
- **The speed.** It moves back towards the path's speed by a whole
  `kOpponentLevelAccel` (8) a step, never part of one. Every cost is a
  multiple of 8, so the speed lands on the default exactly, with no clamp.
- **Climbs.** Each climb level has a row in two tables: its slope, used
  through `_opponent_move()`, and its effect on the speed. The first build
  shifted by the level instead. oscar64 compiles a shift by a variable
  through a 56 B `bitshift` table and duplicates the loop, which made
  `_opponent_fly()` 386 B; with the tables it is 244 B.
- **Behaviour.** An aircraft on a path climbs or descends at level 1 to
  hold its path's height, within ±20 m. Every current path is level, so
  the missions fly as before.
- **Tests.**
  - `opponent_test` §6b: level flight holds the path's speed and height
    exactly.
  - A 300 m climb takes 386 steps and a 300 m descent 381, each within one
    climb cost of the default speed, and both end back at the default.
  - At the minimum speed the aircraft does not climb, and at the maximum it
    does not descend, until the speed is back in range.
- **RAM.** fpilot 3,323 → 2,917 B free (largest run 2,306). That is
  406 B against §9's estimate of 150–200. oscar64's code for this kind of
  16-bit logic runs about twice what §9 assumed, so the later estimates
  are likely low too.

### Step 4: the pitch drawn

- **Code.**
  - `_opponent_pitch[i]` follows the climb once it has lasted two whole
    steps. In between it holds the old value rather than levelling.
  - `_opponent_axes()` turns front and up by the pitch, then left and up by
    the bank.
  - Both turns go through one helper, `_opponent_rot()`, which turns a
    static pair `_opponent_ra/rb`. The bank used to be its own copy of the
    same code.
- **What didn't work first.** Locals for the pitch's sine and cosine took
  `_opponent_draw()` onto oscar64's software stack: a 9 B frame and slower
  code, costing 206 B in all. Making them statics didn't help, because the
  optimizer moved them back into temporaries. The shared helper and its
  static pair fixed it.
- **Tests.**
  - The pitch appears on a climb's second whole step, and goes on a level
    flight's second.
  - At every pitch level, bank and 15° of heading, the axes read back
    orthonormal: lengths within 1.4 of 256 and products within 1.8. The
    front's z is the pitch's sine, and the left stays level unless banked.
- **Not done.** No VICE check yet; nothing climbs in the missions until
  step 6.
- **RAM.** fpilot 2,917 → 2,766 B free (largest run 2,158).

### Step 5: down on the ground

- **Code.** `_opponent_destroy(i, text)` downs a slot: its down bit, the
  kill smoke and the message. `opponent_strike()`'s third hit uses it with
  "TARGET DESTROYED". `opponent_step()` uses it with "ENEMY CRASHED" when a
  slot's height reaches 0 after its move.
- **Test.** A slot 8 m below the ground (put there by hand, since a path
  holds its height) goes down:
  - with "ENEMY CRASHED" and `kSmokeDown`;
  - it is no longer a target, and no longer drawn;
  - it counts towards `WP_TARGETS_DOWN`.

  Half a metre up, it stays flying.
- **RAM.** fpilot 2,766 → 2,692 B free (largest run 2,084).

### Step 6: pursuit, the floor, Hunt, Pass and mission 05

- **What a fighter in ATTACK steers for.** It steers for the eye, wrapped
  the same way a waypoint is. It turns at twice a path's rate (`turn` ±2)
  and is drawn banked 60°. Its climb level comes from `_opponent_want()`:
  the slope nearest the player's, worked out in bytes after the turn's
  6-bit cut, and kept above `kOpponentFloor` (60 m).
- **Bombers in ATTACK** keep their path.
- **Pass.** A Pass slot enters ATTACK at its last waypoint.
- **Bank tables.** The bank's cos and sin are now tables by the turn.
- **Mission 05, "Head on".**
  - The player starts at 3,000 ft at cruise, heading north.
  - Two hunting fighters at 2,000 start 4.6 km north, 50 m higher,
    side by side 64 m apart.
  - `kMissionCount` is now 5.
- **Tests.**
  - A hunter catches a straight-flying player 300 m above or below it,
    from 2 km behind, in ~133 s.
  - It follows the player's height up and down 200 m.
  - Over a player 20 m up it never goes below 65 m.
  - A Pass slot flies its lap, attacks at the last waypoint, and closes
    to within 100 m.
  - `mission_test`: 05's hunters are on screen at 4.8 s and within 1 km
    at 33 s.
- **Emulator.** `tools/shot.cc` scenes 11 and 12 and the `headon` and
  `chase` bench scenes fly 05 hands off. The approach shows the pair
  ahead; looking back after the pass shows them turned and following.
  Frame costs: headon 140,539 and chase 154,215 cycles, against
  scramble's 176,257.
- **The size of the first try.** `_opponent_want()` in 16-bit was 266 B.
  In bytes after the cut it is 167.
- **RAM.** fpilot 2,692 → 2,221 B free (largest run 1,613): 313 for the
  code and 158 for mission 05's tables and strings. ppilot is unchanged.
- **For step 13.** Pure pursuit flies a head-on hunter straight at the
  player. Once collisions count, hunters need to break off or offset
  inside ~150 m when closing head-on.

### Step 7: strafing

- **Code.**
  - `flight.cc`'s `model_on_ground` is no longer static, and `flight.h`
    reads it through `flight_on_ground()`. ppilot's free RAM is unchanged.
  - `_opponent_strafe()` gives a hunter its floor and keeps
    `_opponent_pullup`.
- **A run.** It starts while the player is on the ground and within ~14°
  of the nose, ~130 m or more away, and the hunter is above the temporary
  floor plus its margin. The floor is then `kOpponentStrafeFloor` (30 m).
- **Pulling up.**
  - The run ends at that floor, near the player, or once the player is off
    the nose.
  - The hunter then climbs straight ahead at level 1. It stays straight
    until it is back above `kOpponentFloor` and ~1 km out, then turns for
    the next run.
  - In the air, a hunter under `kOpponentFloor` (a Pass slot from a low
    path) pulls up the same way, until it reaches the floor.
- **Distances.** "Near" and "~1 km" come from counting the halvings of the
  6-bit cut, so no extra distance arithmetic is needed.
- **Test.** Against a player parked 16 m up, a hunter from 2 km at 200 m
  makes 10 runs in 10 min. Each goes below 60 m, none below 38 m, and each
  pull-up climbs back above 60 m. Once the player is airborne the hunter
  stays above 60 m.
- **Frame cost** after steps 2–7 (`tools/bench.sh`, against step 1):

  | Scene | Before | After |
  | :--- | ---: | ---: |
  | scramble | 176,257 | 178,281 (+1.1%) |
  | shootdown | 172,707 | 174,188 (+0.9%) |
  | headon | — | 141,620 |
  | chase | — | 155,354 |

  The frozen scenes are within ±0.2%.
- **RAM.** fpilot 2,221 → 1,989 B free (largest run 1,380).

### Where the RAM stands after step 7

Steps 2–7 took 1,368 B: fpilot went from 3,357 to 1,989 B free. §9
estimated about 560–860 B for the same pieces, so the actual cost ran 1.6–2.4×
over. The rest of the plan, steps 8–14, was estimated at about 840–1,170 B.
At the same ratio that would be 1.5–2.5 KB, more than the 1,989 B left.
Before step 9, decide which of these to do:
- free RAM elsewhere;
- cut scope (the rear gunner's normalise, the near-miss test, the blinking
  message);
- write the hottest new logic in assembly, as `planes_asm.cc` does.

Merging argon-loop's RAM passes 51–57 (62aa403) brought fpilot back to 3,052 B
free, largest run 2,409 B. That covers the low end of the projected 1.5–2.5 KB
for steps 8–14 but not the high end, so the decision above still stands; it is
less pressing.

### Step 8: mission 04, the pass and then the hunt

- **Data.** Both fighters are Pass on one-way paths, each with one
  waypoint ~2.5 km north of runway 1. They overtake the parked player as
  before, reach their waypoints at ~84 s, enter ATTACK and are back over
  the runway ~84 s later. The waypoint pool shrinks from 18 to 12.
  `opponent_test`'s straight path, which borrowed 04's waypoints, has its
  own.
- **Tests.**
  - Both stay on their lines until ATTACK, which they enter within a step
    of each other, and both come back within 300 m of a parked player.
  - `mission_test` gains a dogfight pilot. It takes off, climbs to 300 m
    and goes for the nearest fighter. It does not try to out-turn a hunter
    on its tail: it extends at its greater speed to 1.5 km, then turns
    back for a head-on pass, aiming the last few degrees with the rudder.
    It downs both at 4.5 min. Turning with them instead, it never lands a
    hit: the pair sit ~380 m behind in a circle.
- **Emulator.** Bench scene `strafe` (`tools/shot.cc` scene 13) is mission
  04 parked and hands off, timed over frames 900–964 as the pair run back
  in, low and ahead: 165,569 cycles a frame. Getting there takes ~280 M
  cycles, so the bench's per-scene cap is now 400 M.
- **RAM.** fpilot 3,052 → 3,076 B free (largest run 2,433). ppilot is
  unchanged.

### Step 9: the fighters' guns

- **The cone.** `_opponent_strafe()` works it out, as it already holds the
  dot and cross products and the cut: the player within ~7° of the nose
  across, within ~500 m (no more than 4 halvings), and the hunter not
  pulling up. `_opponent_guns()` adds the height: the level wanted is the
  level flown, so the nose points at the player's slope.
- **Bursts.** Three rounds, on whole steps 0, 2 and 4 of a 26-step cycle
  (~0.8 s of fire, ~3.4 s to the next burst). The cycle only runs while
  hunting.
- **Who fires.** `opponent_step()` only sets a bit, `_opponent_firing`.
  `gun_step()`, right after it, asks `opponent_fires()` and fires the round
  itself with `_gun_fire_from()`. Calling into gun.cc from
  `opponent_step()` gave it a 13 B software stack frame and +80 B: the
  deeper call graph pushed its temporaries out of the zero page.
- **The round.** Three times the nose vector, with the climb's sine as its
  z: 48 m a step. The shooter's own speed and the climb's cosine are left
  out; with them it took three multiplies and 66 B more.
- **Slots.** A free slot, or else the oldest enemy round; never one of the
  player's. The player's own fire still takes the oldest of all.
- **Tests.**
  - `gun_test`: a round fired from 400 m straight back hits a stationary
    eye; a pool full of the player's rounds takes no enemy round; with one
    enemy round in it, that one is replaced.
  - `opponent_test`: nose-on at 400 m, rounds at steps 0, 2, 4 and 26;
    5° off fires, 10° off, 700 m away, 37° above, or on a path does not.
  - `mission_test`: parked in 04, the pair's strafing runs land 18 hits in
    5 min, the first at 231 s. The dogfight pilot takes 8 hits on its way
    to downing both, which step 12's five-hit limit will have to allow
    for.
- **Frame cost.**

  | Scene | Step 8 | Step 9 |
  | :--- | ---: | ---: |
  | scramble | 177,884 | 178,308 |
  | shootdown | — | 174,118 |
  | headon | 141,175 | 144,966 (+2.7%) |
  | chase | 154,599 | 164,768 (+6.6%) |
  | strafe | 165,569 | 166,162 |

  The chase pays for the enemy rounds' hit tests and their tracer.
- **RAM.** fpilot 3,076 → 2,603 B free (largest run 1,963): 473 B against
  an estimate of 240–320. `opponent_fires()` alone is 131 B for three
  16-bit sums, `_gun_fire_from()` 123 and the cone 74. ppilot is
  unchanged.

### Step 10: the bombers' rear gunners

- **Not aimed.** §6 planned a gunner that aims at the player within ~45°
  of the tail, the 6-bit direction scattered and normalised. Built, that
  was 550 B (`_opponent_gunner()` alone), so it was cut, as §12 above
  allowed. The gunner fires straight back along the tail, scattered, while
  the player is within ~14° of it across and in height and less than
  ~600 m back. In a tail chase, where it matters, that is where the player
  is.
- **Scatter.** By an 8-bit xorshift (7, 5, 3; 255 values): three bits for
  x and three for y, so up to ~6° across, and two for z, ~2.7°. A Galois
  LFSR's next value is its last shifted a bit, which made a burst's
  rounds walk. Values that straddle the 10 m hit box set the rate: about
  one round in eight lands at 300 m, more closer in.
- **Rate.** The fighters' cycle: a burst of three every 26 whole steps.
- **Where it runs.** The cone is tested in `opponent_fires()`, under
  `gun_step()`. Tested in `opponent_step()`, with the offset to the eye
  shared for the steering, it cost a software stack frame and 57 B more.
- **Mission 02's bombers are Hunt**: their gunners fire from the start, and
  they keep to their path.
- **Tests.**
  - `opponent_test`: 300 m dead astern, a burst of three on the cycle,
    within the scatter; 150 m back and 20 m off it, too. Not at a player
    ahead, 30° off the tail, 100 m above it, or 700 m back.
  - `mission_test`: a minute 300 m behind the first bomber, held there by
    hand: 3 hits.
- **RAM.** fpilot 2,603 → 2,178 B free (largest run 1,539): 425 B against
  an estimate of 120–160. `_opponent_gunner()` is 204 B and the scatter
  adds 100 to `opponent_fires()`. ppilot is unchanged.

### Step 11: fired upon

- **Code.** `_gun_hits()` takes its box's half-side as a parameter and no
  longer frees the round; its callers do, on a hit. For a slot that
  `opponent_calm()` says is still waiting (Provoked, not yet in ATTACK),
  `_gun_hit_opponents()` tests each round that missed it again at 40 m, and
  a near miss calls `opponent_provoke()`. `opponent_strike()` provokes too.
  A Provoked slot then hunts; any other behaviour ignores both.
- **No mission uses Provoked yet.** The code costs nothing a frame until
  one does: the second test runs only for a calm slot.
- **Tests.**
  - `gun_test`: rounds passing a calm slot at 60 m leave it calm, at 30 m
    provoke it once, and neither strikes it; a slot that is not calm is
    never provoked.
  - `opponent_test`: a Provoked fighter holds its path, calm, for 200
    steps; a hit, or a near miss, puts it in ATTACK and it closes to within
    100 m of the player. A Path slot is neither calm nor provoked.
- **RAM.** fpilot 2,178 → 2,066 B free (largest run 1,427): 112 B against
  an estimate of 60–80. ppilot is unchanged.

### Step 12: player damage

- **`damage.h`/`damage.cc`.** The failure bits, the oil countdown and the
  controls' toggle: 3 bytes. Outside fpilot the hooks are the undamaged
  values; ppilot's free RAM is unchanged, once the fuel hook was written as
  a statement of its own (inside the `?:` it cost ppilot's
  `flight_advance()` 36 B).
- **The hooks in `flight.cc`**, one line each: the throttle's top
  (`damage_throttle_max()`), the fuel burnt (`damage_burn()`), flaps and
  gear (`damage_hydraulics()`), and the airborne rotation
  (`damage_skip_input()`, every other input lost).
- **A hit.** `gun_step()` counts it and calls `damage_hit()`. Hits 1–4
  pick a failure from `opponent_random()`'s low four bits, folded to 0–4;
  if the player has it, the next one round they do not. The throttle is
  cut to its new top, and the message blinks for ~3 s. The fifth hit sets
  `FLIGHT_CRASH_SHOT_DOWN`, "CRASHED: SHOT DOWN".
- **The oil leak.** `damage_step()`, on `gun_step()`'s whole steps, seizes
  the engine 200 whole steps (~32 s) after the leak: "ENGINE SEIZED",
  blinking, and the throttle's top 0.
- **The blink.** `msg_blink()`, fpilot only, after `msg_show()`:
  `msg_render()` writes white for black on every other pair of frames until
  the message expires. The next `msg_show()` stops it.
- **A bug from step 9.** `gun_step()` fired the opponents' new rounds before
  testing the rounds against the player, and `_gun_fire_from()` overwrites
  `_gun_t`, which holds the eye for that test. So each round was tested
  against its own muzzle, and one fired slowly across the line of sight -
  head-on - hit as it left. A head-on pass cost the dogfight pilot four hits
  in three seconds. The rounds are now fired after the test, and their
  first step, within 50 m of the shooter, is not tested. The step 9 counts
  above were inflated by it.
- **Scatter for the fighters too.** Pure pursuit keeps a fighter's nose on
  the player, and in a tail chase the player moves along the line of fire,
  so every round landed. The fighters' rounds now get the gunners'
  scatter. Without it the dogfight pilot is shot down; with it, it takes 2
  hits on the way to downing both.
- **Tests** (`fpilot_test`, which links `flight.cc` as fpilot builds it):
  - each failure's message, blinking, and effect: the throttle held at
    half; twice the fuel; the engine seized after exactly 200 whole steps,
    and no throttle after; flaps and gear stuck; ten rolls doing what five
    do;
  - four hits give four different failures for each of the 255 random
    bytes; the fifth gives "CRASHED: SHOT DOWN".
  - `mission_test`: parked in 04, the first hit at 227 s and shot down at
    296 s; the bomber tail chase still takes 3 hits a minute.
- **RAM.** fpilot 2,066 → 1,696 B free (largest run 1,056): 370 B, of
  which ~20 the fighters' scatter, against an estimate of 290–380. ppilot
  is unchanged.

### Step 13: collisions, and breaking off

- **Where the test runs.** In `gun.cc`'s `_gun_hit_opponents()`, which
  already has each live slot's offset from the eye in `_gun_t` every step,
  parked slots included. `_gun_collides()` tests it against a ±12 m box,
  and the midpoint with last step's offset (`_gun_prev`, 12 B), which is
  only used if the slot was in reach on the last step too (`_gun_near`).
  On a hit, `opponent_collide()` sets `FLIGHT_CRASH_COLLISION`,
  "CRASHED: COLLISION", and downs the slot with no message of its own.
- **Breaking off.** Pure pursuit flew a head-on hunter into the player, so
  `_opponent_strafe()` also sets `_opponent_break`: the player in the air
  and ahead, within ~127 m along the ground if it is coming the other way
  (its heading against the hunter's), ~63 m if not. `_opponent_want()` then
  climbs or dives at level 3, away from the player's height, over a player
  level with it or below. Head-on it closes at up to ~20 m a step and needs
  the room; a tail chase closes at 2–5 and, given the same room, climbed
  away from a player it should follow. The head-on test is 69 B.
- **Tests.**
  - `gun_test`: head-on at the greatest closing speed, through the box's
    centre, 11 m off it, and 8 m off on two axes, collides, at each of
    eight phases of the steps; 30 m to one side or above does not; taxiing
    into a parked aircraft does; an offset from a step on which the slot
    was out of reach is not used.
  - `opponent_test`: head-on at the player's height, 8 m above and 8 m
    below, a hunter is never within 29 m on every axis at once. The
    height-following test now uses a hunter slower than the player: a
    faster one overtakes, breaking off over it.
  - `mission_test`: the recon chase without guns flew into the recon, so
    its pilot throttles back inside 150 m. The dogfight pilot's nearest
    pass is now 50 m, and it takes 1 hit.
- **Frame cost.** Scramble 180,797 cycles (+1.4%), shootdown 176,885,
  headon 146,785, chase 167,083, strafe 168,628.
- **RAM.** fpilot 1,696 → 1,364 B free (largest run 737): 332 B against an
  estimate of 90–130 for the collision; the break-off was not in it.
  ppilot is unchanged.

### Step 14: colour, timing and docs

- **Brown bombers.** `opponent_reset()` gives each slot its near colour,
  `_opponent_color[i]`: `kColorBrown` for a bomber, `kColorOpponentNear`
  for a fighter. `_opponent_draw()` reads it: 15 B. `opponent_test`
  checks a bomber 150 m away offered brown, a fighter dark grey, and a
  bomber 1.5 km away black. Not seen on the emulator: in mission 02 a
  parked player never gets within the ~620 m the colour needs, and a scene
  that flies there was not scripted.
- **Timing** (`tools/bench.sh`, against step 1):

  | Scene | Step 1 | Step 14 |
  | :--- | ---: | ---: |
  | start | 119,483 | 119,542 |
  | approach | 143,666 | 144,314 |
  | cruise | 152,265 | 153,559 (+0.8%) |
  | town | 144,290 | 144,642 |
  | lake | 134,002 | 133,895 |
  | scramble | 176,257 | 180,749 (+2.5%) |
  | shootdown | 172,707 | 176,736 (+2.3%) |
  | headon | — | 146,759 |
  | chase | — | 167,039 |
  | strafe | — | 168,548 |

  The close fight in mission 04 is 2.5% dearer, well inside the 10% at
  which §10 would have drawn fewer pitch levels, so all seven stay.
- **README.** fpilot's line describes the five missions.
- **RAM.** fpilot 1,364 → 1,349 B free (largest run 724). ppilot is
  unchanged at 11,345.

### Where the RAM stands after step 14

Steps 8–14 took 1,703 B: fpilot went from 3,052 B free after the argon-loop
merge to 1,349 B (largest run 724). §9 estimated 840–1,170 B for the same
steps, without the break-off; they ran 1.5–2× over, as steps 2–7 did, even
with the aimed gunner cut. The biggest pieces were the guns (473 B), the
rear gunner (425 B), the damage model (370 B) and the collision with the
break-off (332 B). ppilot is unchanged: 11,345 B free.

Merging argon-loop's passes 58–60 afterwards brought fpilot to 1,757 B free
(+408, largest run 1,132 B) and ppilot to 11,446 B (+101). The passes claimed
+432 B for fpilot on their own; the rear gunner's bounds, moved to unsigned
with `_opponent_abs()`, gave back some of it, and `opponent_step()`'s stack
frame grew from 10 to 12 B.

fpilot then got room for ten missions. `kMissionCount` is 10 in both games,
and fmission.cc's 06–10 are placeholders titled RESERVED: each starts parked
on runway 1 (from 04's start row, `kMissionParkedStart`: the six
`kMissionStart*` tables hold only 01–05's since the RAM loop's pass of
2026-11-02), sends nothing (no row in `kOpponentPaths` since the RAM loop's
pass of 2026-11-02: `opponent_reset()` switches both slots off; it was one
empty row each at first, then one shared) and so has its
goals met at once. fpilot's one-page menu is gone, replaced by ppilot's
scrolling one, four missions to a page. Together that cost 495 B: the
scrolling menu 159, the empty opponent rows 160, the other table rows and
the placeholder text the rest. fpilot is at 1,262 B free (largest run 637),
and ppilot is unchanged. A written mission will cost more than its
placeholder: longer text, and waypoints for its opponents.

Then some tweaks after play:

- **05, Head on.** The pair start 2.3 km north of the player instead of 4.6,
  and 128 m apart instead of 64.
- **Rounds into the ground.** A round that goes below the ground leaves a
  puff of smoke there for 10 frames (gun.cc `_gun_puff`), kept relative to
  the eye like the rounds. It takes the one smoke sprite only when no
  aircraft is smoking, so the sprite count in `_opponent_cap()` is
  unchanged: `gun_puff_up()` counts as the smoke.
- **Hits on the player.** The failure's message shows like any other,
  without blinking (`msg_blink()` is gone), and the border flashes red three
  times instead (`damage_frame()`, once a frame from sim.cc). The fifth hit
  flashes too; `damage_reset()` leaves the border black.
- **Tracers.** The opponents' are yellow, like the player's.

That came to 204 B: fpilot is at 1,058 B free (largest run 600), and
ppilot is unchanged. mission_test now flies 03 and 05 to the end as well:
the dogfight pilot strafes 03's parked pair from 60 m, and downs 05's pair
head-on in ~2.3 min unhit. 02's bombers it could not win: on their tail it
was shot down by the rear gunners, standing off it was still hit, and going
round to meet them head-on took it past the 4.4 min they need to reach the
city.

So the rear gunner now fires only at a player above the plane of its wings
(`_opponent_gunner()`, 9 B smaller than the two-sided test), and 02's second
bomber flies at 3,500 ft, 500 ft above the first. The dogfight pilot takes
02 by holding 100 m under each bomber until its pass and holding fire until
it is within 400 m: both down at ~3.8 min, unhit. Firing from out of range
had emptied its 50 rounds before it closed. fpilot is at 1,067 B free.

04's pass was too long: the pair flew ~2.5 km north and turned only after
~90 s, and a player who had taken off and flown north was by then far
ahead of them, so they never came back. Their waypoints are now ~600 m past
the player's start: they overtake, turn to ATTACK at ~16 s, and follow and
shoot from there, at 2,000 instead of 1,500. A parked player is strafed
from ~50 s. mission_test checks a player flying away north is followed, and
its dogfight pilot still takes 04 unhit, turning to fight from 100 m - one
that climbs on to 300 m with them on its tail is shot down. fpilot is at
1,109 B free: the same code, and the data moved by page alignment.

Shot down now means falling, not stopping:

- **Opponents.** The third hit says TARGET DESTROYED and the aircraft falls
  (`_opponent_fall()`): drawn 60 degrees nose down (a new kOpponentDive
  pitch), moving along its heading and down at three quarters of its
  speed, ~37 degrees, trailing its smoke. *Since 2026-11-05:* it was 1.5
  times, ~56 degrees, and fell out of sight too quickly. Half of them, by the random byte, also spin:
  the heading turned a quarter each whole step and the wings drawn at 60
  degrees. It neither flies its path nor fires, and takes no more hits. It
  is down - towards WP_TARGETS_DOWN, with the longer smoke where it fell -
  only when it hits the ground; parked, at once. The falling and spinning
  bits share `_opponent_down`'s byte.
- **The player.** The fifth hit in the air takes the controls away and
  says SHOT DOWN!; flight.cc's `_flight_fall()` pushes the nose down to 60
  degrees whichever way up, and in a spin rolls on as well. The crash is
  the ground, CRASHED: SHOT DOWN. From 3,000 ft that takes ~23 s. On the
  ground the fifth hit is the crash at once, as before.

The cost was ~400 B of code, and it showed what fpilot's "free" figure
hides: analyze_ram counts bss2's ~600 B, which only takes uninitialised
data, so the main region at $0A60-$CFC0 - code, data and the main bss -
had only 491 B. It overflowed. planes.cc's cached products, _planes_px,
_planes_pv and _px_k, moved to bss2 (34 B), and the fall was made cheaper
(the quarter-turn spin, no acceleration, the parked case in the fall): the
main region has 123 B left. Watch that number, not the total.

### Aimed rear gunners (2026-11-04)

The gunner got the cone and the aim §6 first planned, which step 10 had
cut for its 550 B. It fires at a player behind any flying bomber, whatever
its behaviour - so 01's recon, a bomber on a path, shoots back now - within
45 degrees of its tail to either side, from 45 degrees above its wings to
~10 below, and inside ~600 m; never from the ground. Its rounds go along
the line to the player: the line divided by twice the distance behind and
`vec_normalize()`d, whose fast square root is good to ~2% - the round's
speed, not its line. 129 B: fpilot 1,439 -> 1,310 B free, the main region's
largest run 587 B. The cone test loops over the axes, counted down, and
reuses `_opponent_toward()` for the height; written out it was 61 B more.

opponent_test holds each case of the cone to firing or not, every round to
the line within the scatter, the recon to firing, and a parked bomber to
never firing. mission_test's pilots had to learn the cone: chasing the
recon at its height, the pilot was shot down. They now hold 100 m under a
bomber, attack the lowest of a formation first - under the higher one of
02's pair the pilot was level with the lower one, behind it - and do not
aim from inside any gunner's cone with 5 degrees to spare: putting the
nose on a bomber from below climbs into it. Over fifteen random seeds the
recon falls in 2.8-3.0 min unhit every time, and 02's pair in 3.3-3.5 min
with no hit in thirteen and one in two.

### Breaking off turns away (2026-11-04)

With less induced drag (flight.cc `kFlightInducedShift` 6) the player climbs
out of 04's scramble at the fighters' own speed. One of them sat 5-9 m behind
it for 15 s and flew into it: breaking off asks for the steepest level away
from the player's height, which there was a dive, and under its 60 m floor
the floor turned that into a climb - while pure pursuit kept it on the
player's tail. A hunter breaking off now also turns away from the player,
never straight on (`opponent_step()`, 6 B). The break-off cases in
opponent_test still keep it 26 m clear on every axis, and the scramble's
flying-away pilot is followed at ~100 m instead of hit. mission_test's
dogfight pilot, which only turned to fight at its 100 m floor, now does from
30 m up while it climbs on: holding its fire until 100 m it reached the floor
with both fighters on its tail, losing all fifteen random seeds; reacting from
30 m it wins all fifteen in ~2.5 min with under one hit on average.


### Missions 04-10: waves of pairs (2026-11-05)

The five placeholders are written, and two of the old missions got a second
pair. A mission no longer sends one pair: opponent.cc's `kOpponentPaths` is
13 rows of a pair each, and `kOpponentWaves` lists the rows each mission
sends, in order, one byte a pair, with the last marked. `opponent_reset()`
sends the first; `opponent_step()` sends the next once neither slot is
flying, parked or falling and the smoke has cleared, ~5 s after the last
went down. Every pair starts afresh (`_opponent_send()`), and a row can be
sent more than once: 06 and 08 send theirs three times.

- **Goals.** `_opponent_destroy()` counts what goes down. A mission can ask
  for a share of what it sends, in the high bits of its first wave byte:
  then its goals are met once that many are down, even with more to come.
  Otherwise they are met once its last pair is down or gone. After the last
  pair, too few down fails it, CITY BOMBED.
- **Seeing and giving up.** A Provoked aircraft also attacks once the player
  is within 2 km on both axes across the ground (`kOpponentSpot`); before,
  only being fired upon set it off. Bit 5 of the kind, `kOpponentStray`,
  makes one that is attacking give up, gone, once the player is more than
  2.5 km off on either axis (`kOpponentGiveUp`).
- **Landing only.** `flight_init_from_mission()` starts fpilot's missions at
  `kMissionWpBegin`, with that navpoint selected: 07 starts at home's.
- **Unarmed.** `gun_rearm()` gives 09 no rounds, on the ground too.
- **Start rows.** Every mission has its own again (`kMissionStarts` is 10,
  `kMissionParkedStart` is gone).

The missions:

| | Pairs | Goal |
| :--- | :--- | :--- |
| 04 Scramble | the pass, then hunters from the west | all four down |
| 05 Head on | the head-on hunters, then hunters from the south | all four down |
| 06 Big week | bombers over the city, three times, each gone over the south edge after ~80 s | three of six down |
| 07 Way home | two patrols in turn, Provoked and straying | land |
| 08 Turkey shoot | fighters flying runway 2's circuit, three times, never firing | five of six down |
| 09 Unarmed escape | hunters from 450 m behind, straying, no rounds | outrun them, then land |
| 10 Combat patrol | hunters from 1.2 km behind at 6,000 ft | both down |

mission_test's pilots fly all of them. The dogfight pilot waits between
pairs and stops once the goals are met; it downs 04's and 05's four, three
of 06's six from below, unhit, and 10's pair in ~0.6 min. At 08 it gets ~2
of the circuit's fighters per 50 rounds even holding its fire to 400 m, so
the test refills its guns in place of a landing: five down with two
refills. A pilot flying straight home with throttle wide open is seen by
07's first patrol at ~1 min, outruns it at ~3.7 min, and is seen by the
second on the approach; in 09 it is free of the hunters at ~2.2 min, a
minute before home, unhit. opponent_test holds every mission's waves, its
share, the spotting, giving up, and 06's failure.

The cost was ~830 B of the main region: 951 B free there before, 125 B
after (fpilot 829 B allocatable in all, 672 of it bss2). About half is data
- the eight new rows (192 B), the mission text and the start rows - and
`_opponent_send()` is 313 B, most of it the slot set-up `opponent_reset()`
had. ppilot is unchanged.

### Armour before the failures (2026-11-05)

Every hit used to break something, and the fifth shot the player down. Now
the airframe takes the first three hits, or four in half the flights, by the
random byte at `damage_reset()`: the border flashes, YOU HAVE BEEN HIT, and
nothing else. The four failures and the fall follow as before, from the hit
after those, so it takes 8-9 hits to go down. `damage_armour` holds this
flight's count; `kDamageArmour` is the three, and `kDamageHitsToDown` is now
counted past the armour. 42 B: fpilot's main region 125 -> 83 B free.

fpilot_test holds the armour's hits to breaking nothing for both counts, and
the hit after them to one failure. mission_test's parked player in 04 is shot
down at ~6 min instead of ~1.8, and no pilot that won before loses now.
