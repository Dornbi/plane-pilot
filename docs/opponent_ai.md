# Opponent behaviour (`opponent_ai.md`)

A plan for fpilot's opponents to react to the player and shoot back. It
covers:
- a small flight model for the opponents;
- four behaviours a mission picks per aircraft;
- two ways of attacking, including strafing a player on the ground;
- collisions, and opponents hitting the ground;
- a damage model for the player.

*Plan only, 2026-11-01; nothing here is built.* It builds on
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
  is behind it.

No aircraft is unarmed. An aircraft with the Path behaviour never attacks,
so today's recon is a fighter or a bomber on a path.

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
| `PATH` | steers for its next waypoint, guns off | steers for its next waypoint, gunner off |
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

- the bomber is in `ATTACK`;
- the player is within ~45° of its tail: `dot < 0`, `|cross| < −dot`, and
  `|dz|` is no more than `−dot`;
- the player is within ~600 m.

The gunner aims at the player:

- The direction is the 6-bit vector to the eye, scattered by ±1–2 units
  from the LFSR on each axis, then normalised with `vec_normalize()`.
- The bomber keeps its heading.
- It fires one round every ~4 whole steps, with no bursts.
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
