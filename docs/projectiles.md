# Projectiles (`projectiles.md`)

Guns for fpilot, the player's first and the opponents' provided for.
*Since 2026-10-24* §9's phases 1 and 2 are built (`c64o/gun.cc`): see §11.
The rest of this is the plan they follow. Everything here is fpilot only (`__FPILOT__`);
ppilot does not change. Lengths are on the current scale, 128 m to the
world unit ([map.md](map.md) §4, "The scale").

## 1. What it has to do

- The player fires with **Space** (free in flight; only the map uses it, to
  exit). Rounds leave the wings alternately and converge ahead.
- A round that passes close enough to an opponent hits it. Enough hits set
  its `_opponent_down` bit, which already completes `WP_TARGETS_DOWN`.
- **One hardware sprite** for all of it, whoever fired.
- Opponents can fire through the same code later: a round has an owner, and
  a round from an opponent can hit the player.

## 2. Rounds are state, the sprite is a view of one

The rounds and the sprite are separate. Rounds are a small pool flown in
world space every model step, hit-tested whether or not they are drawn.
Each frame at most one of them is offered to the sprite stack. That is how
one sprite carries a burst: the hits are the rounds', the picture is a
tracer.

```c
struct round_t {
  int32_t x, y, z;     // flight_eye_*'s 24.8 half metres
  int16_t vx, vy, vz;  // per model step, the same 1/256ths
  uint8_t age;         // steps flown; 0 = free
  uint8_t owner;       // kRoundPlayer, or 1 + opponent slot
};
static round_t _rounds[kRoundMax];  // 8 x 20 bytes
```

- **Velocity fits int16.** A round at 48 m a step plus the player's 15 at
  `kMaxSpeed` is 63 m, 32,256 of the 1/256ths. So muzzle speed is capped
  near 48 m a step, about 340 m/s at 7 steps a second. That is slower than a
  real gun, which is what lets a tracer be seen for more than two frames.
- **Life:** `kRoundSteps` ≈ 12 at 48 m a step, so about 600 m of range,
  then the slot is freed.
- **Pool:** player fire every second step while Space is held, alternating
  wings, needs 6 slots for 12 steps of life. 8 slots, shared, with the
  oldest replaced when full, leave room for an opponent's burst.
- **Steps:** advance in `opponent_step()`'s place in the loop, after
  `flight_advance()`, under `flight_step_shift` like everything else that
  moves (`_flight_step_s()`).

### Firing from the wings

At fire time, in the player's camera frame (`flight_cam`):

```
start = eye + front * 20 m + left * (±4 m) - up * 1.5 m
v     = player velocity + front * kMuzzle - left * (±4 m / kConverge) * kMuzzle
```

- The ± alternates per round: the wings in turn.
- `kConverge` ≈ 200 m is the harmonisation range, where both streams cross
  the boresight.
- 20 m ahead clears the renderer's near cull (`C.x <= 64` eighth metres is
  8 m). At that range 4 m to the side and 1.5 m down projects about 51 px
  out and 19 px below the centre, so the round appears from the lower left
  or right of the viewport and runs in towards the mark.
- **Fallback, if the wings look wrong:** set the side offset to 0 and every
  round leaves from the middle, under the mark. Nothing else changes. Wings
  are the choice (§10).

## 3. Hitting

Per step, for every live round and every opponent in the near range:

- `rel = opponent - round`, in half metres, int16. Rounds never get past
  ~600 m, so both are near.
- `u` is the round's unit direction, kept at fire time in 8.8.
- `t = clamp(rel · u, 0, step length)`.
- Hit if `|rel|² - t² < r²`.

This is a segment test, not a point test, because a round moves 48 m a step
and would jump straight over a 20 m aircraft.

- **Hit radius `r`:** about 10 m against the ~20 m span (`kOpponentKn`
  156), generous for 6–8 fps.
- **Cost:** about 8 rounds × 2 opponents, each a dot product and two
  squares in 32-bit, so 5,000–8,000 cycles a step.
- **On a hit:** free the round, count the hit (`_opponent_hits[i]`), and
  past `kHitsToDown` (3) set the down bit.
- **Down for now means gone:** the cull skips it. Smoke and a fall are the
  "smoke and damage" item that follows.
- **Rounds against the player** (owner ≠ player): the same test against the
  eye, radius ~6 m, counting `flight_hits`. What a hit does to the player
  is left to that later item; the counter is the provision.

## 4. The tracer: the instrument arms, reused

The needle bitmaps are already lines at 16 angles over half a turn
(`kSpriteDefLongArm`, 14 px, blocks 96–111; `kSpriteDefShortArm`, 10 px,
112–127; [spritedef.h](../c64o/spritedef.h)). A tracer is a short line along
its own motion on screen, so it needs only a choice among those blocks,
never new art. The blocks are in the bank the viewport band points at, since
the panel band shows them from the same video matrix.

Per frame, for the round being drawn:

1. **Position:** project it like an opponent: world relative in eighth
   metres, `vec_transform_inv(&world_cam, ...)`, centre by `vec_fracn()`.
   Factor the opponent's near-range half of `_opponent_camera()` into a
   helper both can call.
2. **Direction:** the line from the round's screen point towards the
   vanishing point of its velocity, which is `v` projected as a direction
   `(cx0 - 256·vy/vx, cy0 - 256·vz/vx)` in camera space. A line is
   symmetric, so half a turn is enough. Quantise to 16 steps of 11.25° with
   an octant fold and four tangent thresholds (×256: 25, 77, 137, 210). That
   costs compares and one multiply, no atan table.
3. **Stretch, near:** inside ~150 m, set `kSpriteFlagExpandX` with the long
   arm, so a just-fired round is a 28 px streak. Expanding doubles x, so
   pick the bitmap for slope `2·dy/dx` (halve dx before quantising), and
   the stretched line comes out on the true slope. Plain long arm out to
   ~300 m, then the short arm.
4. **Offer it:** `sprites_stack_add(depth, x, y, 12, 10, block, NB, colour,
   flags)`. The pivot is the arm's centre, `kSpriteDefArmCenterX/Y`, not its
   hub: the line is drawn centred on the round. Depth in the clouds' 1 m
   units, so it sorts with everything else and, being near, takes a low
   index and draws in front. The message strip and the culls come with
   `sprites_stack_add()`.

**Which round gets the sprite:** the player's youngest live round while
firing, then the nearest opponent round in view. Holding Space and firing
every other step, the youngest alternates wings, so the picture is streaks
from left and right in turn. That is the one-sprite alternation asked for,
and it needs no rule of its own.

Colour: yellow for the player's (`kColorSun`'s), light red for an
opponent's, so incoming fire reads as incoming.

## 5. The sprite budget

The stack has 7 slots in front (1–7) and 6 behind the fin (2–7).

| | Front | Back |
| :--- | ---: | ---: |
| Two aircraft, 4 + 2 | 6 | 6 |
| + tracer | 7, fits | 7, one too many |

The stack drops the farthest entry. Behind, an incoming round sits between
the player and the near aircraft, so it would cost the far aircraft whole.

**Provision:** `_opponent_cap()` learns whether a tracer is up this frame.
In the back view with a tracer, the nearer aircraft is held to two as well
(2 + 2 + 1 = 5). That is the rule 5e2f33e removed, back only while it is
needed. In front nothing changes: the sun and clouds go first, as now.

## 6. Opponents firing (provided, not switched on)

- **Owner:** in `round_t` from the start, and the hit test already looks at
  both directions (§3).
- **Gun state:** `_opponent_gun[i]`, a cooldown, and a per-path flag in
  `opponent_path_t` (`guns`), 0 for every current mission.
- **Fire rule,** for when it is switched on: the player within ~6° of the
  opponent's nose (`head · dir ≥ cos`) and within ~500 m. Bursts of 3
  rounds a second step apart, then a cooldown of ~20 steps. Rounds start
  from the opponent's position plus its own front, with its velocity
  added. Parked aircraft never fire.
- **Aiming:** none at first, only straight ahead, so a pilot who stays off
  its nose is safe. Lead is a later refinement.
- **Back view:** incoming tracers are what makes it worth having. §5 keeps
  them from costing the far aircraft its sprites.

## 7. Sound

A gun one-shot on voice 3, the transient voice ([sound.md](sound.md) §6): a
short noise burst per round, through `flight_events` like the other
one-shots. In voice 3's priority order it goes below the stall warning and
the crash, so a stall still sounds while firing.

## 8. Cost

- **RAM:** the pool is 160 bytes, plus ~10 for hits and gun state. Code is
  probably 1.0–1.5 KB: rounds, hit test, tracer, cap change, sound.
- **Budget:** fpilot has 4,610 bytes free, enough, but this is the biggest
  feature since the opponents, so measure after each phase.
- **Per step:** 5,000–8,000 cycles for the hit tests while rounds are live,
  nothing when not.
- **Per frame:** one projection and one stack entry, ~2,000 cycles.

## 9. Order of work

1. **Rounds and hits, no picture.** Pool, Space to fire, wing alternation,
   convergence, segment test, hit counts, down bits. A host
   `test/projectile_test.cc` covers:
   - streams converge at `kConverge`;
   - a round aimed at a crossing target hits, and a round off by more than
     `r` misses;
   - no round jumps an aircraft at full closing speed;
   - three hits set the bit, and `mission_test` can complete 04 by shooting.
2. **The tracer.** Projection helper shared with `opponent.cc`, direction
   quantiser, arm choice and stretch, stack entry. Host test the quantiser
   at all 16 directions and the expanded slopes. VICE shot of a burst.
3. **Budget.** The tracer flag into `_opponent_cap()`, and the
   `opponent_test` budget check with a tracer in the back view.
4. **Sound.** The voice 3 one-shot.
5. **Opponent guns.** Fire rule, cooldowns, hits on the player, behind a
   path flag that no mission sets yet.

## 10. Decided

- **Muzzle speed favours visibility.** About 48 m a step, slower than a real
  gun, so a tracer is seen for ~12 frames rather than two or three.
- **3 hits to down.** The 10 m hit radius is still a starting point for
  play.
- **Unlimited ammunition** for now, so nothing on the panel.
- **Wings, not middle.** The difference is only at fire time:
  - Wings add a side offset to the start and an inward term to the
    velocity. That is six multiplies, ~1,000 cycles per round, so ~500 a
    step while firing.
  - It costs ~60–80 bytes of code and one byte for the alternation.
  - Moving the rounds, the hit test and the tracer cost the same either way.
    Opponent rounds need the general direction choice anyway, so the middle
    would save none of it.
  - From the middle, a round starts 19 px under the mark and shrinks into
    it, so a tracer barely travels on screen. From a wing it crosses ~50 px
    inwards, which is what reads as gunfire.

## 11. As built (phases 1 and 2)

- **`gun.cc`, `gun.h`.** Space fires; `gun_step()` runs after
  `opponent_step()`, and `gun_add_sprite()` after `opponent_add_sprites()`.
  `opponent.cc` gives it `opponent_target()` and `opponent_strike()`.
  `kOpponentHitsToDown` is 3, and a downed aircraft is no longer drawn
  ("TARGET DOWN").
- **Rounds relative to the eye, not in world space.** A round stays within a
  kilometre or so of the player, so its position is kept relative to the eye
  in sixteenths of a metre, int16, good to 2 km. Each step the eye's own
  movement is taken off every round, and what the shift drops is kept, so
  nothing drifts. Velocities are in the same units: 48 m a step is 768.
  - World positions as planned in §2 cost twice the code: 32-bit arithmetic
    per axis.
  - The arrays are flat and `__striped`, round r's axis c at `[c * 8 + r]`,
    which avoids the multiply a `[3][8]` table costs.
- **Hit test as planned (§3)**, with the box tested at four points per step.
  Targets are taken the short way round the world, so a round still hits
  across the x seam.
- **The tracer as planned (§4).** The direction is the line from the round
  to where it will be a step on. A round coming at the eye uses where it was
  a step ago instead; a line has no direction, so either serves. In VICE a
  burst shows as yellow streaks from the lower left and right in turn,
  running in towards the mark.
- **Cost.** fpilot went from 4,601 to 2,152 bytes free, 2,449 in all, the
  help line included; ppilot is unchanged. That is about 2.2 KB of code,
  more than §8's estimate:
  - most of it is `gun_step()`, `_gun_camera()`, `_gun_fire()` and
    `gun_add_sprite()`, at ~260–350 bytes each;
  - the rounds take 96 bytes, plus ~40 of state.
- **Tests.**
  - `test/gun_test.cc` checks:
    - the wings in turn;
    - the streams crossing 190–250 m ahead;
    - the life;
    - the world course held while the eye moves;
    - hits on the boresight, and a miss 30 m off;
    - no round jumping a target head-on at full closing speed;
    - the x seam;
    - an incoming round hitting the player;
    - all 16 needle angles;
    - the tracer offered.
  - `test/opponent_test.cc` checks the strikes.
- **Still to do:**
  - phase 3, the back-view cap with a tracer up, which matters only once
    opponents fire;
  - phase 4, the gun's sound;
  - phase 5, opponent guns.
