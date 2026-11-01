# Opponent behaviour (`opponent_ai.md`)

A plan for fpilot's opponents to react to the player and shoot back. It
covers four parts: a small flight model for the opponents, three
behaviours a mission picks per aircraft, two ways of attacking, and a
damage model for the player.

*Plan only, 2026-11-01; nothing here is built.* It builds on
`c64o/opponent.cc` as of ff69c2c, on [planes.md](planes.md) §13, and on
[projectiles.md](projectiles.md) §6, whose opponent-gun provision it fills
in. Everything is fpilot only (`__ENABLE_OPPONENT__`); ppilot does not
change.

## 1. What it has to do

**Behaviours,** chosen per aircraft by the mission:

1. **Path.** The aircraft flies its waypoints whatever happens.
2. **Provoked.** It flies its waypoints until the player shoots at it, then
   attacks.
3. **Hunt.** It attacks the player from the start.

**Roles,** chosen per aircraft. Each role has its own attack:

- **Fighter.** It turns, climbs and descends towards the player, and fires
  its forward guns when the player is in front of its nose.
- **Bomber.** It never leaves its path. A rear gunner fires at a player who
  is behind it.

No aircraft is unarmed. An aircraft with the Path behaviour never attacks,
so today's recon is just a fighter or a bomber on a path.

**Also:**

- **A simple opponent flight model.**
  - The aircraft is level, climbing or descending, and climbs or descends
    at one fixed rate each.
  - In level flight it settles at its default speed. It slows while
    climbing and speeds up while descending, within a min/max speed range.
  - At the minimum speed it cannot climb any further. At the maximum it
    cannot descend any further.
- **Bombers are dark brown when near** (`kColorBrown`, C64 colour 9). They
  have the same shape as the fighters. Far away they are black, like every
  other opponent.
- **A damage model for the player.** Hits cause failures: lost engine
  power, a fuel leak, an oil leak, jammed flaps and gear, or damaged
  controls. Enough hits shoot the player down.

## 2. What is there today

- **Movement.** `opponent_step()` flies each slot level, at its path's one
  speed. It steers bang-bang at one point, the next waypoint, turning a
  fixed 8/256 rad a step. A comment there already says that reacting to the
  player "would only change the point", and that is the plan here.
- **Drawing.** The heading is a level unit vector. The bank is drawn (±30°),
  but nothing in the movement uses it.
- **Rounds.** `gun.cc` already supports opponent-owned rounds:
  - every round has an owner (`_round_owner`);
  - `gun_step()` tests every non-player round against the eye and counts
    `gun_player_hits`;
  - `_gun_shown()` draws the youngest enemy round as a light-red tracer
    when the player is not firing.

  Nothing fires enemy rounds yet, and nothing reads the hits.
- **Sprites.** `_opponent_cap()` already counts a tracer and the smoke in
  the sprite budget.
- **Redraw cap.** At most one projection or one fill happens a frame,
  across both slots ([planes.md](planes.md) §13). That cap bounds the cycle
  cost of manoeuvring opponents (§9).
- **Player model.** `flight.cc` has every hook the damage model needs:
  - the throttle steps up to `kMaxThrottle` (`FLIGHT_INPUT_THROTTLE_UP`);
  - fuel burns `flight_throttle` a whole step;
  - flaps and gear toggle in one `switch`;
  - every airborne rotation goes through one `vec_rotate_step()` call.

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
```

- `speed` becomes the **default** speed, the one held in level flight.
- Everything else that differs by role comes from a two-entry table: the
  min/max speed, the turn rate and the bank drawn. Bombers keep today's
  8/256 turn and 30° bank. Fighters get about 16/256, roughly half the
  turn radius, and a 60° bank.

At run time a slot is in one of two **modes**:

| Mode | Fighter | Bomber |
| :--- | :--- | :--- |
| `PATH` | steers for its next waypoint; guns off | steers for its next waypoint; gunner off |
| `ATTACK` | steers for the player; forward guns | steers for its next waypoint; rear gunner |

The mission's behaviour sets the mode:

- **Hunt** starts in `ATTACK`.
- **Path** and **Provoked** start in `PATH`.
- Being **fired upon** (§6) switches a Provoked slot to `ATTACK`.

The mode is one bit per slot in a mask byte, like `_opponent_down`. A
bomber in `ATTACK` keeps its path, so the attack only wakes its gunner.

New state per slot:

| | Bytes |
| :--- | ---: |
| `_opponent_speed[i]`: the current speed | 2 |
| `_opponent_climb[i]`: −1, 0 or +1 | 1 |
| `_opponent_pitch[i]`: the pitch drawn, held like the bank | 1 |
| `_opponent_gun[i]`: the burst count and cooldown | 1 |
| ×2 slots, plus the mode mask and a 1-byte LFSR | **12** |

All of it is striped, in `bss2` like the rest.

## 4. The opponent flight model

There are no multiplies and no angles: only adds and compares. This runs
per slot on each whole model step, after the steering (§5) has asked for
a climb, a descent or level flight (`want`, −1/0/+1):

```
climb = want
if (speed <= role.min_speed) climb = min(climb, 0)   // too slow to climb
if (speed >= role.max_speed) climb = max(climb, 0)   // too fast to descend further
if (z <= kOpponentFloor)     climb = max(climb, 0)   // never into the ground

if      (climb > 0) speed -= kClimbDecel
else if (climb < 0) speed += kDiveAccel
else                speed += sign(path.speed - speed) * kLevelAccel   // back to default

z += step_s(climb > 0 ? kClimbRate : climb < 0 ? -kDescendRate : 0)
x += step_s(speed2 * hx); y += step_s(speed2 * hy)   // as now
```

- **One climb rate and one descend rate** (`kClimbRate`, `kDescendRate`),
  both vertical speeds in the position's units a step, shared by every
  aircraft. A climb gains height at the same rate whatever the speed;
  only how long it can last depends on the speed.
- **The speed range does the limiting.**
  - A fighter can zoom from its maximum speed down to its minimum, then it
    levels and has to recover speed before it can climb again.
  - A long descent runs up to the maximum, then it levels. In level flight
    it slows back towards the default and can descend again.
- **Example numbers:**
  - bomber: default 1,500, range 1,100–2,000;
  - fighter: default 2,000, range 1,300–2,800.

  The minimum is above the player's 1,024 stall. These are starting points
  for play.
- **`kOpponentFloor`, ~150 m.** Without it, a hunting fighter chasing a
  player on the deck would follow them into the ground.
- **Parked aircraft** (mission 03) have no airspeed and skip the model, as
  they do now.
- **Whole steps only,** through `_opponent_substep`, as the turn does now.
  Under a larger `flight_step_shift`, a faster machine flies the same
  manoeuvre, not a slower one.
- **No cos(γ) correction on the horizontal.** The climb is not an angle
  here, and the error is a few per cent at most.

## 5. Steering: one target point

Today's steering stays and gains a height axis. The target point is:

- **in `PATH`, and for a bomber in `ATTACK`:** the next waypoint, at the
  path's height `p->z`;
- **for a fighter in `ATTACK`:** the eye (`flight_eye_*`).

`_opponent_toward()` already takes a target in 8.8 units, and
`(uint16_t)(flight_eye_x >> 8)` is one. Height is not wrapped, so `dz` is a
plain difference.

- **Horizontal:** unchanged. The cross and dot products of the heading
  against the direction, cut to 6 bits a component, give a bang-bang turn
  with a 1.8° dead band. Only the turn rate now comes from the role.
- **Vertical:** `want` is +1 if `dz` is above a dead band of ~20 m, −1
  below it, and 0 inside it. This needs no multiply. An aircraft on its
  path holds its height this way, and gets it back after a speed or floor
  limit has pushed it off.
- **Pure pursuit, no lead.** A faster fighter overshoots, passes the
  player, and turns back for another pass. That reads as a dogfight and
  costs nothing. Two refinements, if play wants them:
  - inside ~150 m behind the player, aim the speed at `flight_speed`
    instead of the default, so the fighter sits on the tail (~30 B);
  - lead the player by a few steps of its velocity (~80 B).
- **Range limit.** `_opponent_toward()` is good to 64 units, 8 km, on each
  axis. That covers the whole world on x but only a quarter of it on y. A
  hunt mission starts its fighters well inside that, and a
  `test/mission_test.cc` check keeps them there.

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
  fire never takes a round the player has in flight. With neither free,
  the round is not fired. The pool stays at 8.

### Fighters: forward guns

A fighter fires when all of these hold on the step's steering numbers:

- it is in `ATTACK`;
- the player is within ~7° of its nose: `dot > 0`, `|cross| < dot >> 3`,
  and the height error is under `dot >> 3`;
- the player is inside ~500 m on every axis, tested on the half metres
  before they are cut to 6 bits.

A burst is 3 rounds, one every second whole step, followed by a cooldown of
~20 steps. The rounds leave from the nose along the heading, tilted up or
down by the drawn pitch. They are not aimed: a pilot who keeps off the nose
is safe, as [projectiles.md](projectiles.md) §6 decided.

### Bombers: the rear gunner

A bomber fires when all of these hold:

- it is in `ATTACK`;
- the player is within ~45° of its tail: `dot < 0` and `|cross| < -dot`,
  and the height error is no bigger than `-dot`;
- the player is inside ~600 m.

The gunner aims at the player, and the heading does not change:

- **Aim.** The direction is the 6-bit vector to the eye, scattered by
  ±1–2 units of the LFSR on each axis, then normalised with
  `vec_normalize()`.
- **Rate.** One round every ~4 whole steps, with no bursts.
- **Scatter.** This is the difficulty knob. Without it nearly every round
  would land in a steady tail chase, because the player's ±10 m hit box
  barely moves relative to the bomber.
- **Formations.** Two bombers, as in mission 02, double the fire. That is
  the point of a formation.

### Fired upon

A Provoked slot switches to `ATTACK` on either of these:

- **A hit.** One line in `opponent_strike()`.
- **A near miss.** A player round passes within ~40 m. `_gun_hits()` takes
  the radius as a parameter. Slots still in `PATH` are tested again at the
  larger radius, and only while they are still provokable. ~60 B.

Both are recommended. With hits alone, a pilot who keeps missing never sees
a reaction.

## 7. Player damage

### The failures

Each hit causes one failure, chosen at random.

| Failure | Effect | Message |
| :--- | :--- | :--- |
| **Engine** | The maximum throttle is halved, and the throttle is cut to it if it is above. | ENGINE DAMAGED |
| **Fuel leak** | Fuel burns at 2× the throttle. | FUEL LEAK |
| **Oil leak** | After ~30 s, about 200 whole steps, the maximum throttle drops to 0 and the throttle with it. The aircraft glides. | OIL LEAK, then ENGINE SEIZED |
| **Hydraulics** | Flaps and gear are stuck where they are, and their keys do nothing. | HYDRAULICS HIT |
| **Controls** | Roll, pitch and yaw take only every other input, so they turn at half the rate. | CONTROLS DAMAGED |

**Shot down.** At `kPlayerHitsToDown`, starting at 5, the flight ends:
- `flight_status` becomes a new last crash status,
  `FLIGHT_CRASH_SHOT_DOWN` ("SHOT DOWN" in `kFaultText`);
- the flight ends as any crash does, so R and Q need nothing new.

**Choosing a failure.** The LFSR picks one of the five. If the player
already has that failure, the next one in the list that they don't have is
taken instead. A failure never repeats and never gets worse. Hits 1–4 cause
failures and the fifth shoots the player down, so there is always a new
failure left to pick. Nothing repairs a failure: it lasts until the mission
ends.

Every hit also shows its message and flashes the border for a frame. The
gun sound ([projectiles.md](projectiles.md) phase 4) adds the noise once it
exists.

### Where it goes

A small `damage.cc`/`damage.h`, built the same way as `gun.h`:

- In fpilot it holds the state:

  | | Bytes |
  | :--- | ---: |
  | failure bits | 1 |
  | oil countdown | 1 |
  | control toggle | 1 |
  | **total** | **3** |

  The throttle cap and the fuel shift are read from the failure bits, so
  they need no state of their own.

- Everywhere else it is inline stubs that return the undamaged values, so
  ppilot compiles to what it is today.

The hooks in `flight.cc` are one line each:

```c
// FLIGHT_INPUT_THROTTLE_UP
if (flight_throttle < damage_throttle_max()) ...
// fuel
uint8_t fuel_consumption = whole_step ? flight_throttle << damage_fuel_shift() : 0;
// FLIGHT_INPUT_TOGGLE_FLAP / _GEAR
if (damage_hydraulics()) return;
// airborne rotation
if (damage_skip_input()) return;   // toggles; true every other call when damaged
```

- **The oil countdown** runs in `damage_step()`, called next to
  `gun_step()`.
- **`gun_step()`** calls `damage_hit()` where it now does
  `++gun_player_hits`.
- **Throttle range.** `kMaxThrottle` is `0x18`, and at 2× the burn is
  `0x30`, which still fits the byte `fuel_consumption`.
- **Side effects.** The engine sound and the rpm needle follow
  `flight_throttle`, so a damaged or seized engine shows and sounds right
  for nothing.
- **No repair.** `damage_reset()` runs only with `opponent_reset()`, at the
  start of a mission. Halting on a runway refuels and rearms, but does not
  repair.

## 8. Drawing: colour and pitch

- **Bomber colour.** In `_opponent_draw()` the near colour comes from the
  role: `kColorBrown` for bombers, `kColorOpponentNear` for fighters. Far
  away both are `kColorOpponentFar`, so the dot looks the same.
- **Pitch.** The drawn pitch is one of three fixed attitudes: level, or
  about ±10° for climbing and descending. It follows `climb` once a state
  has lasted two steps, as the bank does, so a one-step correction does not
  flick the nose. From the constants `s` and `c`:

  ```
  front = (c·hx, c·hy, s)
  left  = (−hy, hx, 0)          unchanged
  up    = (−s·hx, −s·hy, c)
  ```

  For each of the camera's two rows, the existing `h = row.x·hx + row.y·hy`
  is reused: `front·row = c·h + s·row.z` and `up·row = c·row.z − s·h`.
  That is 4 multiplies a row and 8 a projection. Level flight skips them,
  as a level bank skips its own. The bank is applied after this, as now.

## 9. Cost

### RAM

fpilot today has **3,357 B free and allocatable; the largest run is
2,741 B** (`tools/analyze_ram.py c64o/fpilot.map`, ff69c2c). For scale,
`opponent_step()` is 720 B as built.

| Piece | Code + data, B |
| :--- | ---: |
| Opponent flight model: climb state, speed range, z move, floor | 120–160 |
| Target point (waypoint or player), height steering | 80–120 |
| Modes and provoking (hit and near miss) | 70–100 |
| Fighter guns: cone, burst, cooldown | 100–130 |
| Rear gunner: cone, scatter, LFSR, normalise | 120–160 |
| `gun_fire_from()` and the enemy-slot rule | 130–170 |
| Player damage: random pick, five hooks, oil countdown | 150–200 |
| — its six messages and "SHOT DOWN" | 100–120 |
| — `FLIGHT_CRASH_SHOT_DOWN`, border flash | 30–50 |
| Pitch in `_opponent_axes()`; bank and turn by role | 70–100 |
| Bomber colour | 10–20 |
| Role table, mission data changes | 20–40 |
| State (§3, §7) | 15 |
| **Total** | **~1,020–1,390** |

That leaves about 2.0–2.3 KB free.

`-Op` page-aligns every array of 256 B or less, which can hide a saving or
a loss in the gaps it leaves. So each phase is measured with
`analyze_ram.py` ("Free, allocatable" and "largest run"), not with the prg
size.

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
| One slot on its path (height axis, climb state, speed, z move) | ~250 | 0.2% |
| One fighter attacking, not firing | ~500 | 0.4% |
| — on a firing step | +~1,200 | |
| One bomber, gunner watching | ~400 | 0.3% |
| — on a firing step (normalise + round) | +~2,500 | |
| Each enemy round in flight, tested against the player | ~150–400 | |
| Player damage hooks | < 100 | |
| **Worst step:** two fighters both firing, 4 enemy rounds up | **~4,600** | **~3%** |
| **Typical:** two path aircraft, nothing firing | **~600** | **< 0.5%** |

Per frame, the render:

- **Pitch in the axes.** 8 multiplies, ~1,200 cycles, but only on a frame
  that projects a climbing or descending slot.
- **The worst frame does not change.** It is still at most one redraw a
  frame across both slots: today's 29,000 cycles at 75 m, or 46,000 for a
  2 × 2 at 30 m ([planes.md](planes.md) §11).
- **The average close-fight frame does change.**
  - A manoeuvring aircraft changes its axes more often, so frames that a
    path follower spent on a cache hit (~3,000) become re-projections
    (15,000–29,000).
  - This shows only inside ~500 m, and only while the player holds still.
    When the player manoeuvres, the camera changes the axes anyway.
  - The pitch's three fixed attitudes, held for two steps, keep the extra
    changes few.
  - Expect perhaps 5–10% fewer frames a second in a close fight than in a
    close pass today. Phase 3 measures it on VICE.

## 10. Order of work

Each phase is measured with `analyze_ram.py` and tested on the host before
the next one starts.

1. **Opponent flight model, on today's paths.** Build the climb state, the
   speed range, the z move, the floor, height steering to the path, and the
   drawn pitch. Missions play as now. `test/opponent_test.cc` checks that:
   - level flight settles at the default speed;
   - a climb stops at the minimum speed, and a descent levels off at the
     maximum;
   - the height never goes under the floor.
2. **Modes and pursuit.** Build the role and behaviour bits, `ATTACK`
   steering for fighters, and provoking by a hit or a near miss. Tests:
   - a hunting fighter gets within 300 m behind a straight-flying player
     inside N s;
   - a Provoked slot stays on its path until a round passes within 40 m,
     then attacks.
3. **Enemy fire.** Build `gun_fire_from()`, fighter bursts and the rear
   gunner, with hits on the player counted. Tests:
   - player rounds are never taken for enemy fire;
   - one bomber hits a tail-chasing player at a sane rate (a range, to tune
     the scatter).

   Then a VICE timing run of a close fight, against today's close pass.
4. **Player damage.** Build `damage.cc` and its five hooks, the random
   pick, and `FLIGHT_CRASH_SHOT_DOWN`. `test/flight_test.cc` checks:
   - each failure's effect;
   - the oil countdown;
   - four hits give four different failures;
   - the fifth hit ends the flight;
   - ppilot builds byte-identical.
5. **Colour and missions.** Make the bombers brown, then set the missions'
   roles:
   - 01 recon: fighter, Path;
   - 02 bombers: bombers, Hunt, so their gunners are live from the start;
   - 04 scramble: two fighters, Provoked;
   - a hunt mission, which needs the decision in §11.

   `test/mission_test.cc`'s pilots must still complete every mission.

## 11. Open questions

- **Where does the hunt mission go?** Either turn 03 (Ambush, two parked
  aircraft) into an interception, or add a fifth mission. A fifth costs
  ~60 B of mission tables and a `kMissionCount` change in the menu.
- **Do provoked fighters go back to their path** when the player breaks off
  (beyond ~3 km)? Not planned: once provoked, they keep hunting.
- **Collisions** between the player and an opponent are not modelled. Pure
  pursuit can fly a fighter through the player on a head-on pass. One 10 m
  box test a step would make that a crash, for ~40 B.
- **Do the numbers play well?** The 5 hits to shoot the player down, the
  gunner's scatter, the oil leak's 30 s, and both speed ranges are all
  starting points for play.
- **Should damage show on the panel?** The messages are the only display
  for now. A lamp would need a glyph, and the panel's hidden glyph space is
  full.
