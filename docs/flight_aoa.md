# Prototype: a flight model with an angle of attack (`flight_aoa.md`)

> **This landed, as its own binary.** The model described here is in
> `c64o/flight.cc` behind `__FLIGHT_AOA__`, and `docs/flight.md` §1 has the
> selection and the trade. `make` builds both: `ppilot.prg` with the arcade
> model at 47,607 bytes, `flighta.prg` with this one at 48,375. Both are covered
> by the host suite, which carries two sets of expectations because for several
> cases they are opposites. This
> document is kept for what it is - the prototype, the sweeps that justified
> the change, and the record of two numbers in it that turned out to be
> measuring the harness rather than the model. §4 and §5 below carry the
> corrections; where this document and `flight.md` disagree, `flight.md` is
> right.
>
> What shipped differs from the prototype's defaults in three constants, all
> for reasons the prototype itself surfaced: `kFlightInducedShift` is 5 rather
> than 4, the stall angle is 56 rather than 64, and the camber is 128 rather
> than 0. See "What actually shipped" at the end.

`flight_review.md` closed with an open item, and it had been open the longest of
any of them:

> **Decide the direction for §A** — either add an AoA term to lift, or rewrite
> §2.1/§2.3 and the summary matrix to describe the speed-and-bank lift model
> that actually exists.

The spec half was done: `flight.md` §2.1 said plainly that there was no angle of
attack, that lift was $f(V^2, \text{bank})$, and that pitch traded airspeed for
climb rate. What was never done is the other half — building the AoA model far
enough to see what it would actually fly like. This document is that, and the
code is `c64o/proto/`.

**The prototype is still not in the game build.** `c64o/Makefile` does not list
it, and it links the shipping `flight.cc` rather than modifying it — which now
means both columns of every table below are the *same* model, since the change
landed. That is still useful: set the prototype's tunables to what `flight.cc`
ships (`kAoaAlphaStall = 56`, `kAoaCamberCl = 128`) and any difference between
the columns is a porting bug.

```bash
make -C c64o/proto run
make -C c64o/proto run S=level     # one section
make -C c64o/proto size            # the same model built for the 6510
```

Sections: `wing level turns stall takeoff glide dive induced`.

---

## 1. The one change, and everything that falls out of it

The shipping model has an attitude and no flight path. Airspeed points along the
nose by definition, so `vspeed = front.z * speed`, and lift is a function of
speed and bank with `front.z` nowhere in it.

The prototype gives the flight path its own state:

| | `flight.cc` | prototype |
| :--- | :--- | :--- |
| where the aircraft points | `front` | `front` |
| where the aircraft *goes* | `front` | `aoa_gamma` |
| angle of attack | — | `alpha16 = (front.z << 4) - aoa_gamma` |
| lift | $f(V^2, \text{up.z})$ | $f(C_L(\alpha), V^2, \text{up.z})$ |
| what pitch does | sets the flight path | sets the wing's angle |

That is the whole change. Everything below is a consequence of it, not a
separate feature:

- **Lift is two-sided.** `flight.cc` applies a *deficit* only when positive —
  lift above weight produces no upward force at all (`flight.md` §2.4). Here the
  net vertical force integrates into the flight path in both directions, so the
  aeroplane can be pushed up as well as allowed to fall.
- **The stall is an angle.** Not a speed. The speeds fall out of it, and so do
  the two stalls a speed gate cannot see: the accelerated stall and the inverted
  stall.
- **Induced drag is one term.** $C_L^2 V^2$ replaces three separate stand-ins in
  `flight.cc` — the lift deficit's `deficit >> 10`, the bank term
  `left.z^2 >> 5`, and the implicit inverted penalty. All three were the same
  thing: the wing being asked for lift it has to work for.
- **The turn is the horizontal half of lift.** Same equation, same $1/V$, other
  component. `flight.cc` turns at `left.z >> 5` whatever the wing is doing,
  which is why its turn rate does not depend on airspeed (`flight_review.md`
  §B4).
- **`kFlightRotatePitchZ` is deleted, not retuned.** The comment on it in
  `flight.cc` asks for exactly this: *"If lift ever grows a pitch term this
  constant should be deleted rather than retuned."*

### The arithmetic is the same arithmetic

int16 8.8 fixed point, `vec_fastmul8p8`, shifts, and one 16-entry table. No
floating point and no runtime division — `vec_div8p8` is never called. Written
against `flight_step_shift == 0`; the substep scaling of `flight.md` §8
multiplies through unchanged and is left out as noise.

Same airspeed scale and clamp, same weight (`kAoaTrimLift = 0x1000`), same
parasite/gear/flap drag coefficients, same thrust per throttle unit, same
density decay, same ground plane. That is what makes every number below
readable against `flight.md`'s.

---

## 2. There is no lift curve table

The lift slope was chosen so that $C_L$ in 8.8 and `alpha16` are the same
number:

$$C_L = \text{alpha16} = (\text{front.z} \ll 4) - \text{aoa\_gamma} \qquad (|\alpha| \le \alpha_{\text{stall}})$$

Below the stall the "curve" is the identity — no table, no index arithmetic, no
interpolation. Past the peak the wing droops at five eighths, which is what
makes a stall a stall rather than a ceiling: pull harder there and you get
*less* lift, so the flight path falls away faster than the nose does and
$\alpha$ runs away until the pitching break ends it.

`kAoaClPeak` is therefore not a free parameter — it is `kAoaAlphaStall << 4`.
Raise the stall angle and the peak $C_L$ rises with it and the stall speed
falls, which is the relationship a real wing has and the one two independent
constants could not hold.

**The stall speeds are never written down anywhere in the model.** They are
consequences, and they land on `flight.cc`'s hand-written constants:

| | prototype, derived | `flight.cc`, told |
| :--- | ---: | ---: |
| clean | **1024** | `0x0400` = 1024 |
| flaps down | **836** | `0x0340` = 832 |
| inverted | **1024** | `0x0400` = 1024 |
| inverted + flaps | 1448 | `0x0480` = 1152 |

Flaps are a *camber shift* — they add to $C_L$ rather than scaling it. That one
choice is the whole of `flight.md` §4.2's inverted flap penalty, with no
`up.z < 0` case anywhere: upright the offset adds to a positive $C_L$ and the
stall speed drops; inverted the attitude needs a negative $C_L$ and the same
offset fights it, so the stall speed rises. The prototype's inverted-flap
penalty is harsher than the shipping constant, which is a tuning question, not
a structural one.

---

## 3. What it flies like

All figures measured by `aoa_proto.cc`, run to a steady state, at
`kAoaInducedShift = 4` and `kAoaForceShift = 0`.

### 3.1 Level flight

| Throttle | `flight.cc` pitch / speed | AoA pitch / alpha / speed |
| :--- | :--- | :--- |
| 11 (45%) | 20 / 1535 | — cannot hold level |
| 12 (50%) | 8 / 1846 | — cannot hold level |
| 14 (58%) | 4 / 1982 | 40 / 47 / 1181 |
| 16 (66%) | −1 / 2111 | 28 / 27 / 1578 |
| 18 (75%) | −1 / 2173 | 20 / 19 / 1881 |
| 24 (100%) | −1 / 2509 | 14 / 12 / 2347 |

(The sweep reports −1 rather than `flight.md` §2.1's 0 for `flight.cc`'s level
attitude; it searches upward from −64 and −1 is the first attitude whose mean
vertical speed is not negative. They are the same trim.)

The shape of the AoA column is the point. `flight.cc` has no pitch term in lift,
so above its trim speed the level attitude is `front.z = 0` **exactly** and
there is no faster level trim — the attitude indicator says the same thing at
every cruise speed. Here level flight always needs a positive $\alpha$, and the
faster you go the less of it, which is the trim behaviour every real aeroplane
has and the one the instrument panel has been implying all along.

The cost is the level-flight floor, which moves from 45% to 58%. See §5.

### 3.2 Inverted flight

Inverted comes out as the upright table **mirrored**: same attitude, same speed,
$\alpha$ negated, floor at 58% rather than `flight.cc`'s 70%.

That is exactly right for the wing as tuned and exactly wrong for the aeroplane
`flight.md` §3.2 describes, where sustained inverted flight is meant to sit on
the edge of the stall. The reason is that `kAoaCamberCl` is 0, and a symmetric
section does not care which way up it is. Camber is the knob — the flap
mechanism with the flaps welded down — and at 96 it takes the full-throttle
inverted attitude from 14 to 20 and $\alpha$ from −13 to −19. Getting
`flight.md`'s "inverted flight is expensive" back is a matter of choosing that
number, and it costs one addition.

Note what the sign flip in `aoa_advance()` is doing here, because it is the one
line that is easy to get wrong: angle of attack is a *body* angle — which side
of the wing the air arrives from — and rolling inverted swaps those sides. Upside
down, a nose held above the flight path in world terms is air arriving on the
canopy side, which is negative $\alpha$ and negative lift. That single
`up.z >= 0 ? d : -d` is everything `flight.md` §3.2 says about inverted flight.

### 3.3 Banked turns

Holding the level trim attitude and rolling in, full throttle, 300 steps:

| Bank | `flight.cc` dAlt / rate | AoA dAlt / rate |
| :--- | :--- | :--- |
| 0° | 0 / 0.0°/s | −6552 / 0.0°/s |
| 35° | −863 / 0.0°/s | −15189 / 0.0°/s |
| 71° | −50890 / 8.1°/s | −253952 / 5.6°/s |

The pitch that holds the turn level, full throttle:

| Bank | `flight.cc` | AoA |
| :--- | :--- | :--- |
| 0° | pitch −3, speed 2509 | pitch 18, alpha 12, speed 2326 |
| 35° | pitch 4, speed 2402 | pitch 22, alpha 16, speed 2210 |
| 71° | pitch 28, speed 1847 | **cannot hold it level** |

The last row is a feature. A level 71° turn asks for 3.07 times the weight in
lift; the wing runs out of $\alpha$ and the induced drag runs out of throttle
before it gets there. `flight.md` §3.1 already claims steep banks are severe —
this is the model actually charging for them.

Turn rate against airspeed, 71° bank, level, speed pinned:

| Speed | `flight.cc` | AoA |
| ---: | ---: | ---: |
| 1280 | 7.8°/s | 12.1°/s (at $C_{L\max}$) |
| 2048 | 7.8°/s | 13.8°/s |
| 2816 | 7.8°/s | 8.0°/s |

`flight.cc` is flat by construction. The AoA column is $g \tan\phi / V$ with a
corner in it: somewhere between 1280 and 2048 the wing stops being able to make
the 3.07 g the bank asks for, and below that the turn is wing-limited rather
than bank-limited. That corner is a real thing
an aeroplane has and a fixed `left.z >> 5` cannot express.

### 3.4 The stall

**Power-off, from level flight.** `flight.cc` breaks at step 98 and speed 1023 —
one unit under the stall speed it was told. The prototype breaks at step 82 and
speed 939, because $\alpha$ reached 64 first: the aeroplane was decelerating and
holding attitude, so it ran out of angle slightly before it ran out of speed.

**Accelerated stall.** Level at 2816, then the stick all the way back:

- `flight.cc`: **never stalls.** Forty steps of full back stick leave it at
  speed 1894 with the nose 44° up and the stall lamp dark, because 1894 is above
  1024 and that is the entire test.
- prototype: stalls at step 8, at speed **2290** — 2.2× the 1 g stall speed,
  which is what a 29° pull is worth.

This is the case that cannot be papered over with a constant, and it is the
strongest single argument for the change. Every "pull up harder to make the
turn" and every yanked recovery in the game is currently unpoliced.

**Recovery** is hands-off and takes 102 steps (16 s) from a power-off stall,
ending in a 72° dive at speed 1189. The break drives the nose back toward the
flight path, which needs no "toward the ground" special case: at low speed the
flight path is already steeply down, so chasing it *is* the nose drop, at any
attitude and either way up.

### 3.5 Takeoff

`flight.cc` gets airborne at speed 1048 — but only because `kFlightRotatePitchZ`
drives the nose to a fixed 10.6° the moment the aircraft passes the stall speed,
a constant its own comment calls a fudge.

The prototype has no rotation constant. The pilot holds an attitude and the
aeroplane leaves the ground when the wing carries it:

| Held attitude | Clean | Flaps |
| :--- | ---: | ---: |
| 16 (3.6°) | 2116 | 1210 |
| 31 (7.0°) | 1481 | 1038 |
| 47 (10.6°) | 1217 | 948 |
| 64 (14.5°) | 1051 | 872 |

Rotate fully and you unstick at the stall speed; rotate less and you unstick
later, exactly as much later as the lift equation says. `flight.md` §5.2's table
of 1608-at-16 and 1047-at-47 becomes a property rather than a tuning exercise.

This also removes the pathology §5.2 documents at length. `flight.cc` had to
gate the rotation because otherwise the wheels lifted and came straight back
down once per frame, rattling the touchdown sound. In the prototype the step
that lifts off is also the step that integrates a positive net force into a
climb — the liftoff test sits *ahead* of the flight-path integration for exactly
this reason — and ground contact tests for a descent rather than a position.
Both are one-line consequences of having a flight path at all.

### 3.6 Glide and dive

Best glide: **6.94 : 1** at 40 units nose down, against `flight.cc`'s 6.13 : 1 at
50. Comparable, and slightly better.

> **This number was wrong, and finding out why changed the tuning.** The
> harness read the glide off a run that had never settled - at a fixed
> attitude below the minimum-drag speed the model is speed-divergent, so there
> is no steady state to read - and the shipping column was measured from
> `0x0400000`, far above the density knee, in half density air. Measured
> properly, as distance over altitude lost at a pinned altitude, the
> prototype's tuning glides **3.8 : 1**, not 7.7 : 1. See §5. The two shallowest attitudes cannot hold a
glide and stall, which is what `flight.md` §6.2 says of the shipping model too.

Terminal velocity, nose truly straight down:

| | `flight.cc` | AoA |
| :--- | ---: | ---: |
| full throttle, clean | 3693 | 3832 |
| full throttle, gear | 3319 | 3430 |
| idle, clean | 2710 | 2897 |

The `flight.cc` column reproduces `flight.md` §6.1 exactly, which is the
harness checking itself. Both stay under the `0x0F00` clamp, though the AoA
model's clean dive is now close enough to it that the clamp is worth watching.

---

## 4. Three things the harness taught, worth keeping

**A settle that starts far from trim measures the harness, not the model.** The
model is stable while the wing is below its stall angle. A bench run that starts
with the flight path flat and the nose 25° down starts with a large *negative*
$\alpha$; the path swings down past the trim angle and out the far side into a
stall, where the droop makes it run away. In flight the pitching break ends
that — but a bench run that pins the attitude has taken the break away, and it
settles into a mush that is an artefact. The harness now starts every run at the
flight path a trimmed aeroplane would already be on, and refines it with a
second pass seeded from the first pass's airspeed. That turned a glide table
where 64 units nose-down mushed while 50 and 80 either side of it flew into a
monotonic one.

**A glide read off an unsettled run is a number about the harness.** Below the
minimum-drag speed the model is on the back side of the power curve: as speed
falls the angle of attack rises, induced drag rises as its square, and the
aircraft slows further. There is no steady state at a fixed attitude there, so
"settle for N steps and read the vertical speed" returns whatever point of the
divergence the run stopped at, and it moves with N. Worse, the glide sweep was
flown from `0x0400000`, which is far above the `0x080000` density knee, so it
measured the glide in half-density air - which barely troubles a model whose
only drag is parasite and wrecks one with an induced term, because thin air
needs more $C_L$ for the same weight and induced drag goes as its square.

Both together inflated the prototype's glide by about a factor of two, and the
inflated figure is what the first tuning was chosen on. Measuring distance over
altitude lost, at a pinned altitude, reversed the choice - see §5.

**`flight_test`'s own `_settle` cannot do a vertical dive.** Writing
`front = (256, 0, -256)` and `up = (0, 0, 0)` and calling `vec_orthonormalize`
leaves a **45°** dive, because normalizing `(256, 0, -256)` is `(181, 0, -181)`.
`flight.md` §6.1's terminal velocities are quoted for a nose "truly straight
down"; with the frame built properly the shipping model produces 3693 / 3319 /
2710, which is what §6.1 says — so the published numbers are right and it is the
obvious way of reproducing them that is wrong. Worth fixing in `flight_test.cc`
if any test there ever needs a true vertical.

---

## 5. What it would cost

### Resolution, and the oscillation that is not there

The flight path is carried at 4096 = 1.0 rather than the direction cosines'
256, and $\alpha$ at sixteen units to one of `front.z`. Neither is decoration.

At one unit of $\alpha$ per unit of `front.z`, one step of $\alpha$ is 16 units
of $C_L$, which at cruise is **6% of the aircraft's weight**. A flight path
integrated from a force that quantized could never settle — it could only hunt
across a lift error a sixteenth of a g wide, and it did: the first version of
this model showed a limit cycle in every steady state, a few units of flight
path wide, visible as vertical speed that would not sit still.

At sixteen units to one the smallest lift step is under 0.4% of weight, inside
the integrator's dead band, and every steady state in this document is
bit-stable — the state variables stop moving entirely by about step 400 and do
not move again. It costs nothing, because both operands are already carried at
that scale or finer: `alpha16` is one shift and one subtraction.

The same problem in the longitudinal axis is solved the same way. Forces are
summed at eight times `flight_speed`'s resolution and divided once, with the
remainder carried, so a drag worth two thirds of a unit a step costs two units
every three steps instead of being truncated away. `flight.cc` truncates each of
its five terms separately and keeps no remainder, which is affordable there
because none of its terms is ever small. The turn needs the same treatment: a
15° bank at cruise works out at under one unit of rotation a step, and one unit
is the smallest turn `vec_turn3_xy` can be asked for — without the remainder, a
gentle bank does not turn at all.

### Bytes

Built for the 6510 with the game's own flags (`make -C c64o/proto size`):

| | bytes |
| :--- | ---: |
| `aoa_advance` | 1626 |
| `aoa_input` | 178 |
| `aoa_cl16` | 137 |
| **prototype total** | **1941** |
| `flight_advance` (shipping) | 1213 |
| `flight_input` (shipping) | 388 |
| **shipping total** | **1601** |

The 340-byte gap understates it: the shipping figure also carries the landing
envelope, the fuel burn, the event publishing and the nav sampling, none of
which the prototype has. The aerodynamics alone is roughly 600–800 bytes more
than what it would replace. Against `docs/codesize.md`'s budget that is real
money.

### Cycles

**Measured, in the end.** `c64o/proto/cycles_probe.cc` runs on an emulated 6510
and times single calls with CIA2 timer A, reporting the cheapest of sixteen:

| | cycles |
| :--- | ---: |
| a step that does not re-orthonormalize | **5,184** |
| a step that does | **12,186** |
| `vec_orthonormalize()` alone | 5,754 wings level, ~9,600 banked |

The estimate this replaced was "about +1,000 cycles a step, +20% on the cheap
case". The cheap case measured 5,184 against the ~5,000 `flight.md` recorded for
the old model - the same to within the precision that figure was quoted at,
because the aerodynamics gained several multiplies and lost the lift-deficit
chain, the bank drag term and its `vec_fastsqr8p8`, and the two roughly cancel.
Re-orthonormalization dominates an expensive step and is untouched.

Three things the probe got wrong first, all of which produced numbers that
looked plausible:

- **The 32-bit CIA pair of `docs/emulator.md` cannot time a single call.**
  Reading `ta` then `tb` is two instructions and `ta` can underflow between
  them, which moves the combined value by 65,536. That showed up as readings of
  `0x40001621` - the right answer in the low word and nonsense above it. One
  16-bit timer is enough for a call this short, and one read is atomic.
- **The scenario has to be pinned, or it is not the scenario.** Timing "steps
  in a banked turn" gave 6,654 one day and 19,470 another, because the bank
  evolves during the timed window and the turn rate rounds to zero at some of
  it. Forcing the expensive path - one roll input before each step, outside the
  timed window, purely to set `model_need_normalize` - made it repeatable.
- **Both scenarios flew into the ground during the settle** and were timing a
  ground roll. The witnesses (`g_level_stall`, `g_turn_leftz_end`) exist because
  that is invisible in a cycle count.

A like-for-like re-measure of the *old* build with the same probe was attempted
and abandoned: oscar64 aborts on a copy of the tree, and the only alternative
was overwriting the working one.

### The two scale knobs

`kAoaInducedShift` sets the shape of the drag curve — where it bottoms out, and
therefore the throttle needed to hold level flight. `kAoaForceShift` divides
thrust and drag together and leaves gravity alone, so it is the aeroplane's
weight measured against its engine and its airframe.

The prototype's sweep of them is below, and **the glide column of it is wrong**
for the reason §4 gives — it is read off unsettled runs in thin air. It is kept
as the record of what the tuning decision was originally made on:

| induced | force | level floor | cruise | best glide (**inflated**) | best climb |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 3 | 0 | 23 (95%) | 2063 | 4.25 : 1 | +207 |
| 4 | 0 | 14 (58%) | 2347 | 7.72 : 1 | +610 |
| 5 | 0 | 10 (41%) | 2429 | 13.14 : 1 | +857 |
| 4 | 1 | 9 (37%) | 2347 | 12.80 : 1 | +717 |
| 5 | 1 | 7 (29%) | 2422 | 11.41 : 1 | +951 |
| 4 | 2 | 4 (16%) | 2347 | 15.54 : 1 | +992 |
| — | `flight.cc` (old) | 11 (45%) | 2509 | 6.29 : 1 | +663 |

On those numbers induced 4 looked like the closest match to the aeroplane the
game flew, and it was chosen. Re-measured with the glide taken as distance over
altitude lost at a pinned altitude, the same two candidates read:

| induced shift | level floor | cruise | best glide | best climb |
| ---: | ---: | ---: | ---: | ---: |
| 4 | 14 (58%) | 2340 | 3.84 : 1 | +354 |
| **5** | **8 (33%)** | **2429** | **7.33 : 1** | **+526** |
| old model | 11 (45%) | 2509 | 6.13 : 1 | +663 |

which reverses the choice completely: 5 is closer on every column, and better
than the old model on the two that were open — the glide is longer and the
climb is slower, which is what `TODO.md` asks for. **Shift 5 is what ships.**

**A measurement that contradicted its own prediction, kept because it did.** The
force shift was added on the theory that halving thrust and drag would halve the
climb rate and fix `TODO.md`'s "climb rate is too fast". It does halve the climb
*angle* — and raises the climb *rate*, +717 against +610, because the aeroplane
also settles faster at any given angle and the rate is the product of the two.
Climb rate belongs to the gravity term, which that knob deliberately does not
touch. It is not used: `flight.cc` keeps the old model's force scale, and the
climb came down anyway because induced drag at climbing angles of attack is
real now.

### Camber, and the one that had to be paired with it

The prototype shipped with `kAoaCamberCl = 0`, and the measured consequence was
that inverted level flight is *exactly* as cheap as upright — same attitude
mirrored, same speed, same throttle floor. Correct for a symmetric wing and
wrong for the aeroplane `flight.md` §3.2 describes.

Camber on its own moves the upright stall speed too, and `0x0400` is a number
the airspeed dial's green arc and the whole of `flight.md` §5.3 are built on.
Lowering the stall *angle* by the same amount puts it back, because upright
$C_{L\max}$ is peak + camber and holding that sum at 1024 holds the clean stall
speed at 1024 exactly. The camber then comes entirely out of the inverted side:

| stall angle / camber | clean | flaps | inverted | inverted + flaps |
| :--- | ---: | ---: | ---: | ---: |
| 64 / 0 | 1024 | 836 | 1024 | 1448 |
| 60 / 64 | 1024 | 836 | 1094 | 1672 |
| **56 / 128** | **1024** | **836** | **1182** | **2048** |
| 52 / 192 | 1024 | 836 | 1295 | 2896 |
| 48 / 256 | 1024 | 836 | 1448 | — (impossible) |

56 / 128 is what ships. It reproduces both of the old model's stall constants
exactly, makes inverted flight harder than it was, and leaves inverted flight
with flaps possible but close to unflyable — which is the shape `flight.md`
§4.2 always described.

## 6. What is not in the prototype

Left out deliberately, as not being about the aerodynamics: fuel burn, the
landing envelope and its crash triggers, approach warnings, navigation and
waypoints, the map trail, sound events, and the `flight_step_shift` substep
scaling. All of them sit either side of the physics and none of them would
change shape.

Three real limitations, which would need answering before this could land:

1. **$\alpha$ in a steep bank is understated.** It is measured in the world
   vertical plane, and the pull in a banked turn is in the body plane. The exact
   correction is a division by `up.z`, which blows up at knife edge. The model
   therefore under-reports how close a steep turn is to the stall — it errs on
   the forgiving side, but §3.3's "cannot hold a level 71° turn" would come
   *sooner*, not later, with the correction in.
2. **Horizontal travel still uses the full airspeed**, not $\cos\gamma$ of it,
   the same as `flight.cc`. That is a change to the world rather than the
   aerodynamics, and it was left alone so ground speeds stay comparable — but
   with a real flight path there is now a right answer to use.
3. **The landing envelope has not been re-derived.** Every sink-rate and pitch
   limit in `flight.md` §5.3 was measured against the shipping model, and the
   arrival attitudes here are different — a flare now has a real $\alpha$ and a
   real flight path, and `kFlightMaxLandingVSpeed`'s "worst reachable sink above
   stall is −315" would have to be re-measured before any of those numbers could
   be trusted.

## 7. What actually shipped

It landed as a second binary. `c64o/flight.cc` carries both models behind
`__FLIGHT_AOA__`, `make` builds `ppilot.prg` (arcade) and `flighta.prg` (this
one) from the same sources, and both pass the host suite
(`make -C c64o/test test-both`) - 63 tests for the arcade model, 64 for this one
- alongside the 3,080 on-target cases.

**What it cost**: **+768 bytes**, 47,607 against 48,375, which is why it is a
separate binary rather than a replacement. Where the bytes go, from the two maps:

| | bytes |
| :--- | ---: |
| `flight_advance` | +496 |
| `_flight_cl16` | +142 |
| `kFlightRecipV` and `_flight_recip_v` | +58 |
| everything else (outlining, `flight_init_from_mission`, ...) | +76 |

There is no large single item to remove. The lift curve function is the only
one worth a second look, and flattening the post-stall droop to save ~60 of its
142 bytes would take the runaway out of the stall - which is the thing that
makes it a stall rather than a ceiling. Cycles are in §5 and are close to a
wash on the cheap step.

**What it fixed**, all of it from `flight_review.md`'s open list:

- §A, the missing angle of attack — the reason this document exists.
- §B4, the turn rate that did not depend on airspeed.
- `kFlightRotatePitchZ`, deleted rather than retuned, exactly as its own comment
  asked.
- The one-sided lift deficit, so lift above the weight now pushes the flight
  path up.
- The stall constants `0x0400`, `0x0340` and `0x0480`, which are consequences
  now rather than assertions.
- The once-a-frame touchdown cycle on the takeoff roll, at the root.

**Three bugs the port surfaced that were not in the prototype**, each found by a
test rather than by reading:

- **The liftoff bounced at `flight_step_shift` 2.** Exempting only the step the
  wheels left the ground on is enough at the stock rate and not at a quarter of
  it, where the flight path takes four steps to build: fourteen touchdowns in
  forty frames. The contact test asks whether the aircraft is descending, plus
  a `hit_floor` flag from `_flight_move_forward` for the case the clamp hides.
- **The rotation limit collided with the landing envelope.** Set equal to
  `kMaxLandingPitch`, the takeoff roll crashed on trigger 7 — the envelope runs
  every frame at ground level, and `vec_orthonormalize` puts a unit back so 65
  > 64. `kFlightMaxGroundPitch` is 48, wedged under that limit and under the
  stall angle.
- **Holding the stick back through a rotation stalled the aeroplane at nought
  feet.** The break pushes back with a quarter of what one keypress adds, so a
  held key wins. The fix is the one control law in the model: the elevator will
  not drive the wing *deeper* into a stall. It leaves the accelerated stall
  reachable, and leaves the aeroplane free to stall itself.

**What changed for the pilot**, and these are behaviour changes rather than
bugs:

- Level flight needs a positive attitude at every speed; `front.z = 0` is a
  descent.
- A flare with speed in hand is safe; holding it off until the wing stops
  flying is now a stall onto the runway and a crash on sink rate.
- Trigger 7 (pitch too high) can no longer be reached from the air at all, and
  polices over-rotation on the ground roll instead.
- Inverted flight costs attitude and stall margin rather than throttle.
- Holding pitch-down flies an outside loop rather than a dive.

**Still open**, and unchanged from §6: the angle of attack is measured in the
world vertical plane rather than the body plane, so it is understated in a steep
bank — the model is forgiving there, and a level 71° turn would become
*harder*, not easier, with the correction in. Horizontal travel still uses the
full airspeed rather than $\cos\gamma$ of it.
