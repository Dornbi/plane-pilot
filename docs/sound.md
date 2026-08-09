# Sound — Implementation Plan

Plane Pilot is silent. This document plans flight audio: a propeller engine
that tracks throttle, wind that tracks speed, a stall warning, and one-shot
effects for touchdown, gear and flaps. Sound plays only while actually flying
— not in the menu, not on the map, not while paused.

See [project.md](project.md) for the surrounding architecture.

---

## 1. Scope

| In                                            | Out (for now)                             |
| --------------------------------------------- | ----------------------------------------- |
| Engine tone, tracking throttle                | Menu / mission music (§8 reserves room)   |
| Wind, tracking airspeed                       | Per-voice mixing beyond three fixed roles |
| Stall warning, a repeating warble on voice 3  | Stereo / dual-SID                         |
| One-shots: touchdown, gear, flaps, crash      | Digi samples, `$D418` tricks              |
| Explicit silence on every screen transition   | Doppler, ground rumble                    |
| Host tests over the register mapping          | Per-effect volume or a mix menu           |
| A volume key (`V`): off / low / full          |                                           |

---

## 2. The constraints that shaped this

Four facts about the existing program drive nearly every decision below.

**An interrupt handler must not allocate a stack frame.** This is a correctness
constraint, not a space one.

oscar64 allocates `@stack` frames **statically, by call graph**, overlaying
frames it believes cannot be live at the same time. The raster handlers are
installed as function pointers through `rirq_call()`, so they are invisible to
that analysis. Any interrupt-side function needing a frame may be overlaid onto
`world_render_grid@stack` or `vec_transform3_inv@stack` and corrupt them when
the interrupt fires mid-render — a fault that appears once every few thousand
frames, in a module unrelated to sound. None of the three existing handlers has
an `@stack` entry; that is what makes them safe, and it is a constraint the
sound code inherits.

Space is a secondary concern, and it is adjustable. `mem.h:19` declares the
stack region as `$0200–$0280`; oscar64 carves the static frames out of the top
of it and lets the dynamic stack grow down from there. Today:

```
0200 - 025e : STACK,  stack      (dynamic stack, 94 bytes)
025e - 0280 : SSTACK             (static frames, 34 bytes, fully allocated)
0280 - 0800 : bss2               (full to the byte)
```

A new frame pushes `StackEnd` down and shrinks the dynamic stack. If that ever
gets tight, raising the stack region's top in `mem.h:19` and bss2's start in
`mem.h:22` by the same amount converts bss2 space into frame space — at the cost
of relocating that much data from bss2 into `main`. Two numbers in one file.

**Frames are slow and jittery.** `flight_advance()` runs at a wobbling ~10 Hz,
varying with roll angle and polygon load — too slow and too irregular to drive
a SID directly.

Note that a frame is *not* a whole number of 20 ms ticks. It was when
`mem_switch_buffer()` waited on `gfx_wait_vsync()`, but it now waits on
`gfx_wait_flip_window()`, an 81-line raster range that most frames are already
inside. Frame length is free-running, so nothing downstream may assume frames
quantize to the video clock or count them as a 50 Hz timebase. This does not
affect the design below, which deliberately drives the SID from the raster
interrupt rather than from the frame — if anything it strengthens the argument
in §3, since the frame is now an even worse clock than it looked.

**Every non-flight screen masks interrupts** — by choice, and reversibly; see
§8 for what it would take to change. oscar64's `rirq_stop()` is a bare
`sei`. It is reached from exactly two places: `screen_enter_static_mccm()`
(`screen.cc:23`, which serves both the menu and the help screen) and
`map_enter()` (`map.cc:303`). Map mode additionally banks I/O out with
`mmap_set(MMAP_RAM)`, so `$D400` is plain RAM for the duration. A SID left
gated during any of these plays one frozen note forever.

**The SID has no per-voice volume.** `$D418` is master only. The obvious
substitute — modulating a voice's sustain level — is asymmetric: lowering
sustain during the sustain phase drops the level, but *raising* it does nothing
until the voice is retriggered. Sound that must get louder over time cannot be
done that way. §6 deals with the consequences.

---

## 3. Decisions and rationale

### Compute on the main line, blit from the interrupt

The driver splits by rate:

- **`sound_update()`** runs once per frame (~10 Hz) in `sim_run()`, next to
  `panel_update_instruments()`. It is ordinary C — locals, calls, sstack frames
  all fine, because it sits in the main call graph where oscar64's overlay
  analysis is correct. Its only output is a 25-byte shadow register block.
- **`sound_blit()`** is inlined into `_switch_to_terrain` (raster 250) as a
  flat unrolled `lda shadow+n` / `sta $d400+n` sequence. No locals, no calls,
  no `@stack` entry, ~200 cycles.

This resolves four problems at once: the interrupt allocates no stack frame; the
mapping becomes a pure function that host tests can assert on; the cycle cost
fits NTSC's tighter budget (§4); and the audio keeps updating at a steady 50/60
Hz even though the game frame rate wobbles.

The price is up to 100 ms of latency on an effect's onset. That is one video
frame at the current frame rate, so it is not worth engineering away.

Re-blitting every register every tick is safe. Rewriting frequency, pulse
width, ADSR and a *held* gate bit changes nothing; only a 0→1 gate transition
or the TEST bit would retrigger, and neither happens by accident.

### Torn reads are safe only by construction

The interrupt can land between any two of the stores `sound_update()` makes, so
it can blit a half-written register set. An earlier version of this document
called that harmless, reasoning that "the two halves are both valid register
sets and the next tick corrects it 20 ms later".

**The second clause is false for sustain.** §2 already establishes that sustain
is asymmetric — lowering it drops the level, raising it does nothing until the
voice is retriggered — which makes it *latching*. A voice gated on at an
instant when its sustain register reads 0 lands on silence, and every later
frame rewrites the correct sustain to no effect, because nothing produces the
gate edge the chip needs to act on it. The voice stays dead until something
unrelated cycles its gate.

That shipped, and it is what caused one or both continuous voices to drop out
during flight in VICE and return seconds later — the return being a second torn
read that happened to catch a wider window and clear the gate. Two properties
now prevent it, and both are easy to undo by accident:

- **No blanking pass.** Every register is written every frame with its final
  value, silent or not; silence is gates clear and master volume zero, not an
  absence of writes. A torn read then mixes two valid register sets.
- **The control register is written last** within a voice, after the envelope
  it will latch. A torn read then sees either the old gate with the new
  envelope or the new gate with the new envelope — never a gate raised ahead of
  its sustain.

Either one alone fixes the steady state, and it is worth knowing why both are
kept. `sound_silence()` *does* blank the shadow — it has to, since it runs on
the way to an `sei` with the chip about to be banked out — so on the first
frame after it, sustain genuinely reads zero. That frame is every return from
the map, the help screen and the main menu, with the raster interrupt already
running again. Without the ordering, a voice stranded there would be silent for
the whole next flight.

The stores go through a volatile pointer so an optimiser cannot reorder them.
Nothing else in the program can observe the difference, so without that the
ordering is not something the compiler is obliged to preserve. This is the one
part of the fix a host test cannot check — see §7.

### `_switch_to_terrain`, not the other two handlers

`_switch_to_panel_top` is cycle-counted with 16 hand-placed `nop`s and
early-returns when `mem_debug_enabled` — adding work there breaks the mode
split, and would drop audio in debug view. `_switch_to_panel_bottom` also
early-returns in debug. `_switch_to_terrain` is the shortest handler, has no
debug early-return, and runs after its register writes are done, in the lower
border where there is slack.

How much slack is worth being explicit about, because it used to be five lines
and is not any more. `_switch_to_terrain` fires at raster 250 and
`mem_switch_buffer()` used to wait for the line-255 edge; a handler that ran
past 255 would make that wait miss the line and stall a whole frame, which
~200 cycles of `sound_blit` came uncomfortably close to doing. That wait is now
`gfx_wait_flip_window()`, a range that closes at 242, so no amount of work at
250 can be stepped over.

What the handler must still do promptly is its *first* job — latching
`mem_using_alt_buffer` into `$d018`. Keep the blit after the mode-switch writes,
as §3 already specifies, and a late blit only delays itself, in the border,
where nothing is watching.

### Silence is an invariant, not eight function calls

> **The flight driver owns the SID whenever raster IRQs are running.**
> Anything else that wants the SID must take ownership explicitly, and silence
> it on release.

Because the blit lives in a raster handler, "IRQs masked" and "driver not
running" are the same statement. So a single `sound_silence()` at the top of
`gfx_stop_raster_irqs()` — *before* the `sei`, and before `map_enter()` banks
I/O out — covers the menu, the help screen and the map. No un-silencing call is
needed;
the driver resumes on the next interrupt after `gfx_init_raster_irqs()`.

Everything else is derived state, requiring no call at all, because the
simulation loop keeps running in those cases:

| Condition          | Handled by                                            |
| ------------------ | ----------------------------------------------------- |
| Map open           | `sound_silence()` via `gfx_stop_raster_irqs()`        |
| Help screen        | `sound_silence()` via `gfx_stop_raster_irqs()`        |
| `Q` to main menu   | `sound_silence()` via `gfx_stop_raster_irqs()`        |
| `P` paused         | derived: `flight_paused`                              |
| `V` at step 0      | derived: `sound_volume == 0`                          |
| Crashed            | derived: `flight_crashed()` — **except the crash**    |
| Out of fuel        | derived: `flight_fuel == 0`                           |
| `R` reset          | derived: state is re-read next frame                  |

Nine cases, one call site and **two** predicates. **The invariant fails if a
future screen stops the driver without masking interrupts** — that is the thing
to watch for when adding screens.

### Two predicates, because the crash has to be heard

The derived rules were one predicate until the crash sound arrived, and the
split is worth stating plainly because collapsing it back is an easy tidy-up
that would silence the crash entirely:

- **`_driver_live()`** — `sound_volume != 0 && !flight_paused`. Both terms are
  the player's own doing, which is what makes this the level at which "no
  sound" means no sound. Nothing survives it.
- **`_flying()`** — additionally `!flight_crashed() && flight_fuel > 0`. The
  aircraft is in a state that makes flying noises. Voices 1 and 2 key off this.

Voice 3 keys off `_driver_live()`, so the crash burst plays with the engine and
the wind already gated off. Pausing still silences it: the crash outlives the
*aircraft*, not the player's controls.

Master volume follows `_driver_live() && (flying || something on voice 3)`. The
second term is what keeps silence expressible as a single property of the
register set — otherwise a wreck whose burst had finished would sit at gates
clear with the volume still up, and "is this silent" would become "gates clear,
and also check whether anything is mid-release".

The volume key's *off* step is in the derived column deliberately. Giving it
its own silencing path would mean every voice added in phases 3 to 6 has to
remember to consult it; folding it into the predicate means they cannot
forget. It also needs no write-through of its own — `sound_update()` rewrites
the shadow on the next frame and the blit pushes it 20 ms later, which at a
~10 Hz simulation rate is as immediate as any other control.

The other two steps are not a silencing question at all: they differ only in
what reaches `$D418`, from a three-entry table `{0, 7, 15}`. 7 rather than 8
for the middle step because it is half amplitude, about -6 dB, and stays clear
of the bottom of the range where the 6581 in particular gets noisy. `$D418` is
global, so this scales every voice together — the *mix between* voices is
still set entirely by waveform and envelope, and §2's point about there being
no per-voice volume is unaffected.

`sound_volume` is the one piece of driver state that survives `sound_init()`.
It is a setting rather than flight state: a player who turned the sound down
does not want it back up every time they restart a mission.

### Three voices, three fixed roles

| Voice | Role                          | Waveform | Continuous? |
| ----- | ----------------------------- | -------- | ----------- |
| 1     | Engine                        | Pulse    | yes         |
| 2     | Wind                          | Noise    | yes         |
| 3     | Stall warning and one-shots   | varies   | no          |

Only voices 1 and 2 hold a note indefinitely. Voice 3 is the transient voice:
every effect on it is a discrete burst with a beginning and an end, which is
what lets one voice carry four unrelated sounds.

That is the reason the stall lives on voice 3 rather than being folded into
the wind. **The stall is a warning, and a warning is a repeating alarm, not a
texture.** Continuous modulation of the wind bed says "the air is rough";
a warble that keeps re-announcing itself says "you are about to fall out of
the sky", which is the message. Real light aircraft agree — a stall warner is
a horn, not a rumble.

The contention this creates is real but resolves cleanly, because the stall
and the one-shots are not equals. §6 gives voice 3 a priority order with the
stall above gear and flaps: a gear or flap click is confirmation of something
the pilot just did and is safely dropped, while a stall warning is news. The
one case that would have been backwards under the old design — buffet
vanishing during a gear-down on approach — is exactly the case this ordering
gets right.

Touchdown outranks the stall. It is a single unmissable event, it is over in
200 ms, and by definition the aircraft is on the ground when it fires, where
`flight_stall` is already false on the very next step.

### Do not depend on the filter

The SID's filter is a single global resource, and it is the register block that
differs most between chip revisions:

- **6581** (breadbin): narrow cutoff range, sitting low, and varying noticeably
  *between individual chips*.
- **8580** (C64C): wider, more linear, predictable.

A filter setting tuned on one can be nearly inaudible on the other. Since the
project ships as a `.prg` to unknown emulators and unknown hardware, the design
avoids depending on the filter for anything essential. The chip question then
does not need answering and neither revision needs testing. This is what makes
§6's choice of wind-intensity mechanism a real decision rather than an obvious
one.

### A pitch table, not a multiply

`kMaxThrottle` is `0x18` — 25 discrete throttle steps. A real propeller's
idle-to-full span is roughly a 2:1 frequency ratio, and pitch perception is
logarithmic, so a linear map across that range compresses the top: the last
five or six throttle steps would sound nearly identical.

A 25-entry lookup table of 16-bit SID frequencies costs 50 bytes, is exact, is
chip-independent, and takes *fewer cycles* than the multiply a linear map needs.
With 11 KB free (§8) this is not a trade.

---

## 4. Budgets

**RAM.** Trivial against what is available. Before phase 1 the heap was
`a360 - d000` — 11.0 KB contiguous, with nothing allocating from it.

| Item                   | Estimated | Measured (all phases) |
| ---------------------- | --------: | --------------------: |
| Shadow register block  |        25 |                    25 |
| Pitch table            |        50 |                    50 |
| Wind table             |         — |                    32 |
| Other tables / strings |         — |                    41 |
| Driver state           |       ~16 |         11 (zeropage) |
| Code                   |      ~500 |                 1,018 |
| **Total**              |  **~590** |            **1,177** |

Measured from `ppilot.map`, so these are what actually shipped rather than a
projection. The code line breaks down as:

| | Bytes |
| ---------------------------------- | ----: |
| `sound_update` | 733 |
| `sound_blit`, inlined into `_switch_to_terrain` | 166 |
| `_set_voice` | 40 |
| `sound_wind_freq` | 33 |
| `sound_silence` (with `_write_through` inlined) | 29 |
| `_next_rand` | 17 |

`sound_init`, `sound_cycle_volume`, `sound_engine_base_freq`,
`sound_wind_audible`, `_poke`, `_driver_live` and `_flying` do not appear at
all — oscar64 inlined every one of them into its callers.

**Code came in at twice the estimate**, and `sound_update` is nearly three
quarters of it. §10 carries what could be done about that. The estimate was not
unreasonable for what §3 described; the growth is the volume key, the crash,
and the arbitration between five effects on one voice, none of which existed
when the number was written.

**Zero page is the expensive line, not the code.** Eleven bytes, in a region
(`$60–$FC`, 156 bytes) that the map shows is **100% allocated**. Nine of those
eleven are touched only by `sound_update` on the main line and have no reason
to be there; only `sound_gen` and `sound_gen_seen` are read by the interrupt,
where zero page actually buys something.

**Cycles.** The blit runs at raster 250. On PAL (312 lines) that leaves ~3900
cycles before the frame ends; on NTSC (263 lines) only ~800. This is the reason
the blit must stay a flat store sequence and not grow into a driver.

Measured by counting straight-line cost through `_switch_to_terrain`:

| Build              | Handler | Blit | Ends at | PAL left | NTSC left |
| ------------------ | ------: | ---: | ------: | -------: | --------: |
| Before sound       |     147 |    — |       — |        — |         — |
| Phase 1            |     331 |  184 |    ~255 |       57 |         8 |
| Phases 1–6         |     311 |  164 |    ~254 |       58 |         9 |

The blit got *cheaper* across five phases of feature work, which is worth
knowing was not luck: narrowing the retrigger pass from all three voices to
voice 3 alone (phase 5, §6) removed two `lda`/`and`/`sta` triples from the
mismatch path and more than paid for everything since. The 25 register stores
are a fixed 200 cycles and cannot change; everything else here is the
generation check and the gate-off pass.

**The NTSC margin is nine lines**, and it is the tightest constraint in the
design. Nothing waits on a raster line past 242, so overrunning only delays the
blit itself — but nine lines is roughly 580 cycles, which is about three more
registers' worth of gate handling. Anything added to this handler should be
measured, not estimated.

`sound_update()` on the main line is uncounted noise against a ~100 ms frame.

**Verification.** After building, `ppilot.map` must show **no `@stack` entry**
for the blit, and `STACK` must still read `0200 - 025e`.

> **The literal `0200 - 025e` is stale**, and following it will raise a false
> alarm. It was measured before `ppilot` and `ppilotd` became separate
> binaries: `ppilot` now reads `0200 - 025c` and `ppilotd` `0200 - 025e`, with
> no sound or music change between them. The check is still right — compare
> against a build of the *same* configuration without the change under test,
> rather than against the number. See [music.md](music.md) §4. That check belongs in
the review, not in someone's memory — the failure mode is silent corruption of
an unrelated render frame, appearing once every few thousand frames, in a
module with nothing to do with sound.

Both hold as of phases 1–6: `sound_blit` is inlined with no frame, and the
dynamic stack is untouched at 94 bytes. The `@stack` entries that do appear —
`sound_update@stack`, `sound_silence@stack` — are zero-size and main-line, which
is the harmless case.

---

## 5. The interface out of `flight.cc`

Today `model_on_ground` is `static` and the only status signal is the
`flight_status` crash latch. Two things are added; the first already exists.

### `flight_stall` — the flag the physics already computes

```c
extern uint8_t flight_stall;    // 0 = flying, 1 = below stall speed
```

**Already implemented**, because the panel needs it too: the STALL lamp at row
15 column 13 is driven by `gfx_update_stall(flight_stall)` from
`panel_update_instruments()`. The sound driver reads the same byte, which is
the point — a lamp and a warning horn that disagree about whether the aircraft
is stalled would be worse than either alone.

It is not a new computation. `flight_advance()` already derives a local
`stall_speed` (`kStallSpeedWithoutFlaps` `0x0400`, `kStallSpeedWithFlaps`
`0x0340`, an inverted-with-flaps case at `0x0480`, plus an altitude penalty)
and tests `flight_speed` against it to decide whether to break the nose down.
`flight_stall` is that same test, assigned to a global instead of living in an
`if`. Ground mode sets it false explicitly, so the runway is silent and the
lamp is dark for free.

**Why a flag and not a magnitude.** An earlier draft exported the *deficit*
below stall speed as a 0–255 byte, so buffet could build smoothly as margin
decayed. That is the right interface for a texture and the wrong one for a
warning. A warning has to be unambiguous: it is sounding or it is not, and the
pilot should never have to judge how loudly. The magnitude also bought a
resolution the delivery could not use — at ~10 Hz through a 20 ms blit, the
audible difference between deficit 40 and deficit 60 is nil.

The one real objection to a boolean is **chatter at the threshold**, and it is
worth being precise about why it does not bite here. `flight_speed` crossing
`stall_speed` back and forth on successive steps would strobe the lamp and
machine-gun the horn. But the stall break is not a passive threshold: below it
the model pitches the nose down every step, which trades altitude for speed
and pushes `flight_speed` *up*, away from the boundary. The condition is
self-clearing rather than marginally stable, so a recovery crosses once and
leaves. If a flight profile is ever found that does sit on the line, the fix
is hysteresis in `flight.cc` — set at `stall_speed`, clear at `stall_speed +
delta` — which keeps the lamp and the horn consistent because they read the
same byte. It is deliberately not solved in the sound driver.

### `flight_events` — one-shots

```c
#define FLIGHT_EV_TOUCHDOWN  0x01
#define FLIGHT_EV_GEAR       0x02
#define FLIGHT_EV_FLAP       0x04

extern uint8_t flight_events;
extern uint8_t flight_gen;
```

Touchdown is set on the `!was_on_ground` transition already detected around
`flight.cc:642`. Gear and flaps are set where `FLIGHT_INPUT_TOGGLE_GEAR` and
`FLIGHT_INPUT_TOGGLE_FLAP` are handled.

### Handoff without a race

Shared read-modify-write between the main line and an interrupt is a lost-update
bug waiting for the worst possible moment. It is avoided by giving every byte
exactly one writer.

```
flight_input():            records gear and flap into a private pending byte.

flight_advance():          sets touchdown during the step,
                           publishes flight_events from the pending byte,
                           flips flight_gen LAST.

sound_update():            compares flight_gen against its own copy;
                           acts only on a change.
```

**The pending byte is not in the original sketch and is unavoidable.** That
sketch had `flight_advance()` clear `flight_events` at the top of the step,
which reads well until you look at the order `sim_run()` actually calls things:
the keys are polled and `flight_input()` runs *first*, `flight_advance()`
second. Clearing at the top of the step would throw away every gear and flap
event before the step that publishes them had begun. `model_pending_events` is
private to `flight.cc` and both its writers are on the main line in a fixed
order within the frame, so it needs no synchronisation of its own.

`flight_gen` is deliberately **not** reset by `flight_init_from_mission()`. It
is a free-running counter whose only job is to differ from the last value a
consumer saw; restarting it at zero could land on exactly the value already
recorded and drop one event set on the first frame of a mission. For the same
reason `sound_init()` seeds its copy from `flight_gen` rather than from zero.

Flipping `flight_gen` after the event bits acts as a release barrier: an
observer that sees the new generation is guaranteed to see the complete event
set. Two bytes of state, no locks, no disabled interrupts.

**Priority within a generation is touchdown > gear > flaps**, and lower-priority
events are **dropped, not queued**. Queuing would surface a flap click 200 ms
after the key press, which reads as a bug rather than as feedback.

`flight_stall` sits in this priority order too, between touchdown and gear —
see §6. It is not a member of `flight_events`, though, and deliberately so: it
is a *level*, not an edge. The event bytes exist to carry things that happen
once and would otherwise be missed between two frames; a level can simply be
read every tick, and giving it an event bit would mean tracking its falling
edge as well.

### Retriggering across the shadow block

A held gate blits harmlessly, but a *new* effect needs a 1→0→1 transition, and
routing that through the shadow would cost a frame of silence. Instead:

```
sound_update():  bumps sound_gen when it starts an effect
sound_blit():    if (sound_gen != sound_gen_seen) {
                   sound_gen_seen = sound_gen;
                   gate off, then gate on, inline
                 }
```

Only the main line writes `sound_gen`; only the interrupt writes
`sound_gen_seen`. Same one-writer-per-byte discipline as above.

---

## 6. Sound design

### Engine — voice 1, pulse

A propeller's fundamental is blade-passing, roughly 50–120 Hz, and what makes it
*sound* like a propeller is the harmonic stack above it, not the fundamental
itself. A pulse wave supplies that stack; triangle and sawtooth have the wrong
character. Ring modulation is the tempting third option but needs a second
voice, which is not available.

Pulse width is swept slowly and *independently of RPM*. This is what stops a
constant-throttle cruise — the majority of any flight — from degenerating into a
dead drone.

As built: a triangle over `$0400`–`$0BF0`, driven by an 8-bit phase advanced 6
per frame, so a full cycle is about 4 seconds. Both ends of the 12-bit range
are avoided deliberately — at 0 and `$FFF` a pulse wave is DC and the voice
goes silent, so a sweep that touched them would drop the engine out once a
cycle. The phase advances even while the driver is silent, so unpausing does
not restart the timbre from the same point every time.

### Roughness

The first audible build of the above was correct and sounded like a
synthesizer holding a note. A clean pitch table plus a clean triangle is too
steady to be a machine with moving parts, so both are perturbed every frame
from a pseudo-random source.

**What this can be is set by where it runs.** `sound_update()` is on the main
line at ~10 Hz, so this is a *flutter* — an engine running rough — and not a
texture. Per-cycle grit would have to happen at audio rate, which means in the
blit, and §3 requires the blit to stay a flat store sequence with no state.
Two alternatives that would have given genuine noise were both rejected:

- **`RECT|NOISE` as a combined waveform.** On the 6581 this zeroes the noise
  LFSR and silences the voice until the TEST bit is toggled to reseed it —
  precisely the chip-revision dependency §3 spent a page avoiding.
- **Putting the engine on a noise waveform outright.** Prop-like on its own,
  but voice 2 is noise already (wind, phase 3) and the two would mush into a
  single texture.

**The source** is an 8-bit Galois LFSR, taps x⁸ + x⁶ + x⁵ + x⁴ + 1. Maximal
length: 255 non-zero states before repeating, about 25 seconds at frame rate,
long enough that the engine never audibly loops. Zero is the one state it can
neither enter nor leave, which is why `sound_init()` seeds it non-zero.

Two draws per frame, not one. A single byte does not have enough independent
bits for both jobs, and sharing them would tie the timbre to the pitch — the
two moving together every frame is a pattern the ear picks out as machinery of
the wrong kind. The LFSR advances whether or not anything is audible, so a
pause does not freeze the engine into the same few states each flight.

**Pitch** is perturbed by up to `base >> kEngineJitterShift`, so the deviation
is proportional and idle is as unsteady as full power. At the shipped value of
5 that measures ±3.1% peak and 1.5% mean, which is a quarter-tone of wobble.
`kEngineJitterShift` is the knob: 6 halves it, 4 doubles it into a struggling
engine.

One consequence worth knowing before retuning it. A throttle step is 0.54
semitones and the peak wobble is about 0.5, so at full deflection the jitter
very nearly spans one step. That is fine for an engine — the ear tracks the
centre, not the excursion — but it does slightly blur how distinctly a single
throttle keypress reads. If throttle changes become hard to hear, this is the
first thing to turn down.

**Pulse width** is roughened twice over: the phase step varies between 6 and 9
per frame, and a `kPwmJitterMask` offset knocks the result off the triangle.
The varying step is what makes the sweep aperiodic, and that matters more than
it looks — with a fixed step of 6 the 8-bit phase returns to exactly where it
was every 128 frames, and four seconds is slow enough that the ear hears a
period that exact as a repeating figure rather than as drift.

Frequency comes from the 25-entry table indexed by throttle (§3), spanning
50 Hz at idle to 105 Hz at full power — the roughly 2:1 ratio §3 argued for.
The steps are geometric, 0.54 semitones apart, so the throttle sounds equally
responsive across its whole range.

The table is indexed by `flight_throttle` **directly**, with no spool-up lag.
See §10 for what was decided and what the alternative costs.

### Wind — voice 2, noise

Wind intensity must rise with airspeed, and §2 established that the natural
mechanism (sustain-level modulation) only works *downward*. Three options:

1. **Filter cutoff sweep.** Sounds best, and is the standard approach.
   Chip-dependent, which §3 argues against.
2. **Sustain modulation with periodic retriggering.** Chip-independent, but
   retriggering noise clicks unless the attack is slow.
3. **Noise frequency as brightness.** Higher noise frequency reads as brighter
   and more intense. Not literally louder, but for "wind rises with speed" it
   sells. One register, no retrigger, chip-independent.

**Option 3 is what was built.** Option 1 sounds better and option 3 does not
depend on the chip revision, and that decided it — see §3 on why the filter is
not something this design leans on.

`flight_speed` runs to `kMaxSpeed` (`0x0F00`), and the mapping uses its top
bits: `speed >> 8` indexes a 16-entry table spanning `$0600` to `$1800`, or
1443 to 5773 LFSR shifts per second. Geometric between the endpoints, for the
same reason the engine table is — a linear ramp would spend most of its range
in the bottom of the speed envelope, where an aircraft rarely is.

**The register values overlap the engine table, and that means nothing.** The
same number is a pitch on a pulse voice and an LFSR clock on a noise voice, and
the noise rate is 16× the frequency the pulse voice would produce. Comparing
the two tables entry for entry is a category error. The property that does
matter is the ratio of the sounds: the slowest wind runs at over ten times the
fastest engine fundamental, so wind can never be mistaken for a chug. §7 asserts
that, converted to rates, rather than the register comparison it looks like.

**Wind gates off below `kWindMinSpeed`.** No airspeed, no wind — and brightness
alone cannot express that, because option 3 changes the wind's colour and never
its level, so a stationary aircraft would hiss exactly as loudly as one at
cruise. The threshold is about 3% of the speed envelope, below any speed the
aircraft can sustain in the air and below a brisk taxi. Attack 6 (~68 ms) and
release 6 (~200 ms) make the crossing a swell rather than a click, in both
directions.

Below the threshold the voice is still *written* — waveform and envelope
intact, gate clear — rather than left to the `memset` at the top of
`sound_update()`. The memset would zero the release nibble along with
everything else, and a bed that stops dead on touchdown is audibly a bug.

**Balance is a static sustain difference.** Wind sits at sustain 10 against the
engine's 15. `$D418` is global (§2), so this is the only mix control the chip
offers, and noise reads louder than a pulse tone at equal envelope level —
equal sustains bury the engine. Keeping it *static* is what makes it safe:
sustain can be lowered at will but only rises on a retrigger, so a value that
never changes never needs one. That is the trap option 2 above walks into.

### Voice 3 — stall warning and one-shots

Voice 3 runs a small state machine, driven once per `sound_update()`. Its
inputs are `flight_stall` and, via the generation handshake in §5, at most one
event per frame. Priority when more than one wants the voice:

```
crash  >  touchdown  >  stall  >  gear  >  flaps
```

A one-shot that loses is dropped, not queued (§5). A stall that loses is not
dropped in any meaningful sense — it is a level, so it simply gets the voice
back on the next tick that nothing outranks it, which for touchdown is
immediately and for gear or flaps is 200 ms later.

**Nothing on voice 3 decays.** Every effect is attack 0, decay 0, sustain 15 —
straight to full level and held there — and ends by being gated off, with the
release nibble giving it a tail. Length is always a frame count.

Both earlier attempts got this wrong in the same direction. The first gave
every effect a decay-to-zero envelope, which is the obvious reading of "short
noise burst, fast attack, medium decay" below, and gear and flap came out
almost inaudible. The second fixed the one-shots but left the stall decaying,
on the theory that a repeating alarm wants to end itself.

**A decay is simply a quieter sound.** The level is falling for most of the
time the effect is audible, and it is competing with an engine held at sustain
15 and a wind bed at 10, neither of which ever decays. That reasoning does not
stop at the one-shots — a warning horn has more reason to carry, not less.

For the stall the consequence is that its gap is now expressed by the gate:
the warble gates **on** for `kStallOnFrames` and **off** for the rest of
`kStallPeriodFrames`, about 200 ms of tone and 200 ms of silence. That is
better than the decay version even setting loudness aside — the silence between
beeps is the information, so it should be something the code states rather than
a side effect of an envelope running out. It is also directly testable: §7 can
now assert on the gate bit, where before it had to count retriggers.

The release has to fit inside the gap or the beeps run together. 72 ms of tail
into 200 ms of silence leaves room; shortening the gap is the thing to watch
when retuning.

`sound_gen` is still bumped at the start of every burst. Gating off between
beeps produces an edge on its own, so for the warble the bump is belt and
braces — it earns its keep for one-shots, where two events in consecutive
frames would otherwise leave the gate set throughout and the second sound would
never restart its envelope.

One further consequence: no effect on voice 3 now relies on sustain 0 under a
set gate, so §7's interleaving invariant covers all three voices again instead
of excusing this one. Anything added here that *does* decay to nothing would
have to re-open that exemption.

**The retrigger pass in `sound_blit()` touches voice 3 only.** It cycled all
three in phase 1, which was harmless while `sound_gen` never moved. Now that
the stall warning bumps it several times a second, widening it back would
retrigger the engine and the wind on every burst — a stutter on voice 1, and on
voice 2 a pulsing wash, since its attack is 68 ms. Voice 3 is the only
transient voice, which is precisely why the handshake belongs to it alone.

**The stall warble.** While `flight_stall` is set, voice 3 gates on for
`kStallOnFrames` and off for the rest of `kStallPeriodFrames` — 2 on, 4 total,
so about 200 ms of tone and 200 ms of silence at the wobbling ~10 Hz frame
rate. That is 2.5 Hz, cockpit-warner territory. As emitted:

```
##..##..##..##..      # tone   . silence
```

The gap between bursts is the whole point and is worth defending. A tone held
continuously stops being information after about two seconds: the ear adapts,
and the pilot is left with a drone under the engine that no longer means
anything. Re-onset does not adapt. It is also the only way the warning can
share a voice with the one-shots at all — a held gate would have to be torn
down and rebuilt around every gear click, which is both more code and audibly
worse than a gap that was going to be there anyway.

A pulse or triangle tone, not noise: it has to be distinguishable from the
wind bed on voice 2, and the wind is the loudest thing near the stall the
warning is competing with. Rate is around 3–4 Hz, the same territory as a real
cockpit warner.

Because it is a *level*, no falling-edge handling is needed. `sound_update()`
sees `flight_stall == 0` on some tick, stops scheduling retriggers, and the
last burst decays on its own envelope. Pause, crash and fuel exhaustion are
covered by the same derived-silence predicates as everything else (§3), so a
crash while stalled does not leave the horn sounding even though
`flight_stall` itself holds its last value.

**One-shots.**

| Event     | Freq            | LFSR rate       | Length           | Character                |
| --------- | --------------- | --------------- | ---------------- | ------------------------ |
| Crash     | `$1000`→`$0200` | 3850→481 / sec  | 16 fr (~1.6 s)   | a collapsing rumble      |
| Touchdown | `$1800`         | 5773 / sec      | 3 fr (~300 ms)   | bright, an impact        |
| Gear      | `$0900`         | 2165 / sec      | 5 fr (~500 ms)   | low, mechanical, longest |
| Flaps     | `$0C00`         | 2886 / sec      | 4 fr (~400 ms)   | between the two, shorter |

**The crash is the only one that sweeps**, and the only one with the chip to
itself. The engine and the wind are gated off the moment `flight_crashed()`
goes true, so none of the "stay out of the engine's fundamental" reasoning that
pushed gear up to `$0900` applies — it can go as low as it likes. Its noise
clock falls across the burst, so the rumble collapses rather than sitting on
one note, which is the difference between an impact and a long hiss. The
amplitude does not fall: this is a pitch sweep under a held sustain, not a
decay.

It needs no flag in `flight.cc` to fire exactly once. `flight_advance()`
returns early on every frame after the crash, so simply reaching the end of a
step while `flight_crashed()` is what identifies the step that did it.

Nothing preempts it once started, and nothing needs to — the model stops
publishing events the moment the aircraft is wrecked. It does drop itself if
`flight_crashed()` goes false, which is what `R` does: a restart clears the
status without going near the sound driver, and a wreck still rumbling over the
first two seconds of the next attempt would be a strange thing to hear.

Gear and flaps sharing a family is deliberate: they are the same class of event
to the pilot, and differentiating them costs bytes for no information gain. All
three are noise-family, which separates them from the stall tone by waveform as
well as by rhythm.

**"Quieter" for flaps was not achievable.** There is no per-voice volume (§2)
and all three now sit at full sustain because that is what made them audible at
all, so the only levers left are pitch and length. Flap is shorter and higher
than gear; a shorter, higher sound reads as smaller, which is the nearest this
chip gets to the intent.

**Gear moved up from `$0300`.** That put its energy under about 360 Hz —
directly on the engine's fundamental and first harmonics, which is the worst
place to put a sound whose whole job is to be noticed over the engine. It is
still the lowest of the three, as §6 asks, just no longer inside the drone.

A one-shot owns the voice for its own frame count and is then released, with
the release nibble giving it a tail rather than chopping it. Only touchdown
preempts something already running; a stall beginning mid-burst waits for it.
Going silent — paused, crashed, out of fuel, `V` at step 0 — drops the effect
outright rather than letting it run down, so unpausing does not resume a
half-finished gear click.

---

## 7. Testing

`c64o/test/` already builds `flight_test`, `map_test` and `msg_test` natively
against the host, driven by `make -C test`. `sound_test.cc` drops into that
pattern unchanged and landed in phase 2.

It links `sound.cc` **and nothing else from the simulation**. The four flight
globals `sound.cc` reads are defined in the test file instead of coming from
`flight.cc`. That keeps it a unit test of the register mapping rather than a
second copy of `flight_test`, and it lets the test set input combinations
directly — full throttle while crashed, fuel of exactly 1 — that the flight
model would need a contrived trajectory to produce.

The split in §3 is what makes this possible: `sound_update()` is a pure function
from `(throttle, speed, stall, events, paused / crashed / fuel)` to 25 bytes.
The test asserts on the bytes.

This matters more here than elsewhere, because **the failure mode of audio code
is silence**, which no amount of playing the game reliably surfaces. Minimum
cases:

- Volume step 0 produces gated-off voices; steps 1 and 2 do not, and step 2 is
  strictly louder than step 1. A middle step equal to full is a control that
  does nothing on two of its three settings, and a middle step of 0 is a
  second, silent "off" — both are the kind of thing that survives a listening
  test.
- `V` cycles 2 → 0 → 1 → 2 and wraps, rather than sticking at either end.
- The volume survives `sound_init()`, which is what stops the setting from
  being quietly undone on entry to every flight.
- An out-of-range step does not index past the volume table. The symptom if it
  did would be a stray high nibble in `$D418` switching a filter on, which
  reads as a bug in a completely different module.
- Paused produces gated-off voices.
- Crashed produces gated-off voices — but `FLIGHT_MISSION_COMPLETED` does
  *not*, since it is a record of an achievement and not a stop state. That is
  the case a plain truth test on `flight_status` gets wrong.
- Fuel exhausted produces gated-off voices; fuel of exactly 1 does not.
- Throttle `0x18` and throttle `0x12` produce *different* voice-1 frequencies —
  the regression test for the pitch-compression bug a linear map would ship.
  The stronger form of the same check, which is the one that actually pins the
  table's shape down, is that *every* adjacent pair is separated by about the
  same ratio. A linear table passes the first check and fails this one.
- Master volume is non-zero. A perfectly correct voice behind a zeroed `$D418`
  is the single most likely way for this module to ship silent.
- The pulse width never reaches either end of the 12-bit range, where a pulse
  wave is DC and the voice drops out once per sweep.
- Voice 1's frequency does not move while the pulse width sweeps — the §6
  independence claim, and the thing that keeps a cruise from droning.
- Throttle above `kMaxThrottle` clamps rather than reading past the table. The
  flight model cannot produce this, which is exactly why nothing else would
  catch the table and `kMaxThrottle` drifting apart.
- Over a full LFSR period the engine frequency takes many distinct values, on
  both sides of the table entry, and never leaves the bound the tuning
  constant sets. One value means a stuck generator — the failure mode of
  seeding it zero — and an unbounded excursion would eventually read as a
  different throttle setting.
- The pulse width lands off the triangle's multiple-of-8 grid, so the jitter
  is not being swallowed by the sweep.
- The pulse width at frame *i* and frame *i + 128* are frequently further
  apart than the jitter alone could account for. That lag is where a fixed
  phase step would make the sweep exactly periodic, so this is the test for
  the aperiodicity §6 asks for.

- Wind brightness rises monotonically with airspeed across the whole envelope,
  and travels far enough to be heard as a change at all. A table that went
  backwards anywhere would make the aircraft sound like it was slowing down
  while accelerating.
- Wind gates off at zero airspeed and on at cruise, with **exactly one**
  crossing in between. Two thresholds disagreeing by one unit would leave a
  band where the wind is neither on nor off, heard as a flutter while
  accelerating through it.
- Below the threshold the wind voice keeps its waveform and a non-zero release
  nibble, so it fades rather than being cut dead.
- Wind sustain is non-zero and strictly below the engine's — it is a bed, and
  a static sustain difference is the only mix control the SID offers.
- The slowest wind runs at more than ten times the fastest engine fundamental,
  compared as *rates* and not as register values. Wind must never read as a
  chug.
- Out-of-range speeds clamp at both ends, negative included. `flight_speed` is
  signed and a negative right-shift stays negative, so this one is a real
  crash rather than a wrong note.
- `flight_stall` set produces a warble with **both halves present**: over 40
  frames the gate must be seen set at least 8 times and clear at least 8 times,
  and the burst count must land in warner territory rather than one per frame.
  A held tone fails the first check outright. This became assertable on the
  gate only once the stall stopped decaying — while its gap came from an
  envelope running out, the gate stayed set throughout and the test had to
  count `sound_gen` instead.
- The stall's envelope is full sustain, decay 0 and a non-zero release, like
  every other effect on the voice. A warning horn that fades while it sounds is
  the same mistake that made gear and flap inaudible.
- `flight_stall` clearing stops the retriggers, nothing re-arms them, and the
  voice ends up released.
- Crashed while `flight_stall` is still set produces silence. The flag holds
  its last value after a crash (`flight_advance()` returns early), so this only
  works if the derived-silence predicates are checked before the stall logic,
  and that ordering is what is being tested.
- The crash burst plays **while the engine and wind are gated off**, with the
  master volume non-zero throughout. This is the property that fails the moment
  someone collapses `_driver_live()` and `_flying()` back into one predicate,
  and the symptom would be no crash sound at all.
- It runs at least 11 frames, so over a second, and its frequency at the end is
  lower than at the start.
- Crashed with **no** crash event published is still silent — the case a test
  that only ever published the event would miss.
- The crash outranks a stall in progress; hitting the ground while the warning
  sounds is the common case, not a corner one.
- Pausing during the burst silences it, and `flight_status` returning to
  `FLIGHT_ONGOING` drops it, so an `R` restart does not carry a rumble into the
  next attempt.
- Once the burst ends the shadow is silent by the strict test — gates clear
  *and* master volume zero, not merely inaudible.
- In `flight_test`: the crash event fires on exactly the step that wrecks the
  aircraft, and no later frame publishes anything or bumps the generation
  again. That is what stops the crash sound retriggering on every frame for the
  rest of the flight, and it rests entirely on the early return staying at the
  top of `flight_advance()`.
- Each event produces a burst on the noise waveform.
- Every one-shot sits at **at least the engine's sustain** and has a non-zero
  release. This is the regression test for the first version, where they used
  the stall's decay-to-zero envelope and were almost inaudible under the
  continuous voices.
- Every one-shot holds the voice for at least two frames, so roughly 200 ms.
  The length is *measured* — counting frames until the voice releases — rather
  than read off a register, because with a plateau envelope no single register
  encodes it.
- Gear is longer than flap and lower in pitch, which is the whole of what
  distinguishes them once "quieter" is off the table.
- Touchdown preempts a stall in progress. Gear and flap do not, and are
  **dropped** rather than queued — checked by running many frames afterwards
  and confirming the burst never turns up late. This is the case the abandoned
  voice-2 buffet design got backwards: buffet would have vanished during
  gear-down on approach, exactly when it is most wanted.
- A stall beginning mid-burst waits for the one-shot rather than cutting it
  off, and does get the voice back shortly after.
- An event set is consumed exactly once. Calling `sound_update()` repeatedly
  without a new generation must produce no further retriggers — without which
  the last event of a flight would repeat forever, since `flight_advance()`
  stops bumping the generation once the aircraft is wrecked.
- Going silent drops voice 3 outright rather than letting it run down, so
  unpausing does not resume a half-finished gear click.

Bounds in these come from the tuning constants — `kEngineJitterShift`,
`kPwmJitterMask` — rather than from repeated literals, which is why those live
in `sound.h`. Retuning the roughness moves the test with it instead of
falsifying it.

### Interleaving with the interrupt

The cases above all look at the shadow before and after an update, and a whole
class of bug is invisible from there: the raster interrupt can land *between*
two of the stores that build it. That is what caused the dropout described in
§3, and no amount of before-and-after checking would have found it.

`sound.h` therefore declares a `sound_shadow_observer` hook, compiled out of
the C64 build entirely, which `sound.cc` calls after every individual byte it
writes into the shadow. The test uses it two ways:

- **A model of the SID envelope**, deep enough to represent the one rule that
  matters — a gate edge latches the level from whatever sustain reads at that
  instant, and sustain never raises the level afterwards. Levels only, no
  attack or decay rates: the question is never how fast a voice reaches its
  level, only whether it ever leaves zero again.
- **An exhaustive sweep.** For every instant a blit could occur during
  `sound_update()`, fire one there and then run thirty more clean frames.
  Both continuous voices must be audible at the end. A dropout that recovers
  on the next frame is a click; one that is still silent thirty frames later
  is the bug.

  The sweep runs from **two** starting points, and the second is the one that
  earns its keep. From steady-state flight, every register already holds a sane
  value and only the no-blanking-pass property is under test. From the frame
  immediately after `sound_silence()`, sustain genuinely reads zero going in,
  and only the write-ordering saves it. Testing just the first would have
  passed a build that goes silent every time you come back from the map.

Two invariants are also asserted directly, so a failure names the cause rather
than only reporting that something went quiet: no instant may show a voice's
gate set over a zeroed sustain, and no instant during an audible update may
show master volume at zero.

**Two things this cannot check**, both of which have to be confirmed by
reading the build or by ear:

- **The volatile qualifier on the stores.** Host g++ does not reorder them
  either way, so the test passes with or without it. It is protection against
  the *target* compiler, and the only confirmation is to read `ppilot.asm` and
  check the stores come out in source order, with the control register last for
  each voice.
- **That `sound_blit()`'s retrigger pass touches voice 3 alone.** The pass
  clears a gate and the full copy restores it a few stores later, so nothing
  observable survives the call for a test to assert on. Widening it back to all
  three voices — which is what it did in phase 1, harmlessly, because
  `sound_gen` never moved then — would now retrigger the engine and the wind
  several times a second while the stall warning sounds. That is a stutter on
  voice 1 and a pulsing wash on voice 2's 68 ms attack. It is very audible, and
  it is the one regression in this module that only a listen will catch.

---

## 8. Reserving room for a menu tune

> **Superseded in part by [music.md](music.md)**, which plans the tune itself.
> The ownership argument below is what that plan builds on and is unchanged;
> the load-address paragraph no longer applies, because the tune is compiled in
> rather than ripped. Read this section for the reasoning and music.md for what
> is actually being built.

A SID tune for the menu is possible later. It is not in scope, but two decisions
are cheaper to make now than to retrofit.

**It inverts the silence invariant.** The menu reaches
`screen_enter_static_mccm()` and therefore runs with interrupts masked — it is
simultaneously the one screen that wants sound and the one screen with no
interrupt to drive it. The answer is that the tune is a *different owner* under
§3's ownership rule, driven from `menu_run()`'s own `gfx_wait_vsync()` loop
(`menu.cc:149`) at a stable 50 Hz with no interrupts involved. The flight
driver's invariant is unaffected.

**The `sei` is a choice, not a law — but polling is still the better one here.**
`rirq_stop()` masks *all* interrupts only because the raster split must not run;
`map.cc:295` spells out why (a stray `cli` restarts the split and redraws the
map in three bands). Nothing stops a non-flight screen from stopping the split
and installing a plain single raster IRQ that drives only the player. For the
menu that is easy — `screen_begin_text_page()` does nothing but `memset`s — it
is simply more machinery than a `gfx_wait_vsync()` poll for the same 50 Hz tick.

It would earn its keep in one case: keeping music **gapless across screen
transitions**, where a polling loop necessarily stops. If that is ever wanted,
the hazard to plan around is map mode. `map_enter()` banks I/O out
(`mmap_set(MMAP_RAM)`) for pass B, and an interrupt firing during that window
can reach neither the SID nor `$D019` — it cannot even acknowledge itself.
Interrupts would have to be masked explicitly around the banked-out passes, and
the tune would stutter through the map's multi-frame rebuild regardless.

The tune has no equivalent chokepoint, though: `help_run()` has its own
`gfx_wait_vsync()` loop (`help.cc:59`) and help is reachable from the menu.
Whether the tune continues under help — one extra call site — or stops and
restarts around it is **open**.

**Load address: immediately below `$D000`.** Ripped `.sid` files are
position-dependent and overwhelmingly assembled for `$1000`, which is occupied
here (code runs `$0860–$66DC`, data to `$9675`). Placing the tune at the *top*
of the heap keeps the free region below it contiguous and maximally useful for
everything else. A ripped tune therefore needs relocating (`sidreloc`) or the
tune needs composing to that address.

**Zero page is *not* available for a player.** An earlier version of this
section claimed 126 free bytes at `$02–$7F`, reasoning that the convention
against touching them comes from BASIC and KERNAL working storage, that neither
ROM is banked in here (`MMAP_NO_ROM`), and that no KERNAL interrupt runs
(`rirq_init(false)`). The first two premises hold. The conclusion does not: the
space is not free, it is oscar64's.

The empirical half of that claim — grepping `ppilot.asm` for zero-page stores
below `$80` and finding none — was a bad grep. Matching `$xx` as text is mostly
matching `#$xx` immediates; decoding the opcode byte instead shows the
compiler's runtime spread across `$02–$5A`. With `-xz` (extended zero page,
which the Makefile passes) the layout is:

| Range | Contents |
| ----------- | -------------------------------------------------- |
| `$00–$01` | 6510 I/O port |
| `$02–$06` | `WORK` |
| `$0D–$24` | `FPARAMS`, the call parameter area |
| `$25`,`$27` | `IP`, `ACCU` |
| `$2B`,`$2F` | `ADDR`, `sp` |
| `$31` | `LOCALS` |
| `$33–$52` | `TMP`, the caller-saved temporaries |
| `$53–` | spilled temporaries, growing upward |

The last row has no fixed top: it grows with the call graph, and oscar64 does
not bound it. `$53` is a hard floor in the other direction — `BC_REG_TMP_SAVED`
is compiled into oscar64 and no pragma moves it.

What was actually free has since been taken: `mem.h` now runs the zeropage
region from `$60` (the measured spill high water mark is `$5A`, so this is the
low end plus a little margin) to `$100` rather than `$FF`, since the region end
is exclusive and the old spelling never allocated `$FF`. That is 32 bytes wider
than the oscar64 default, all of it spent on `__zeropage` globals, and
`tools/check_zeropage.py` fails the build if the spill area ever climbs into it.

So a player needing zero page has to take it from the region in `mem.h`, at the
cost of globals currently living there — not from a hole at `$02–$7F` that does
not exist.

**NTSC tempo is accepted as-is.** A PAL-composed tune driven once per frame
plays ~20% fast on NTSC. Not worth a fractional-tick counter.

---

## 9. Phases

Each phase leaves the program in a working, committable state.

1. **Plumbing, silent.** ✅ Done. `sid.h`, `sound.h` / `sound.cc`, the shadow
   block, `sound_silence()` in `gfx_stop_raster_irqs()`, the blit in
   `_switch_to_terrain`, `sound_update()` in the frame loop next to
   `panel_update_instruments()`. Everything gated off.

   Verified: `ppilot.map` has no `sound_blit@stack` entry (the blit inlines
   fully — 25 `lda`/`sta` pairs, no loop, no call); `STACK` is unchanged at
   `0200 - 025e`, so no new static frame was allocated; the generated code
   writes `$D400`–`$D418` in ascending order with the three gate-off writes
   ahead of them; `sound_silence()` has exactly three call sites —
   `_enter_simulation`, `screen_begin_text_page` and `map_enter` — which is the
   invariant holding, since `screen_enter_static_mccm()` has no other caller;
   `check_zeropage.py` still passes with 5 bytes of headroom; and all four
   programs plus the three host tests build and pass.
2. **Engine.** ✅ Done. The 25-entry pitch table, pulse waveform on voice 1,
   the independent PWM sweep, and the three derived silence predicates
   (`flight_paused`, `flight_crashed()`, `flight_fuel == 0`). Master volume on.
   First audible phase.

   `kMaxThrottle` moved from `flight.cc` to `flight.h` so the table is sized by
   the throttle range rather than by a second copy of `0x18`; a table one entry
   short would read past its end at full power, and nothing in the game would
   have shown it.

   `sid.h` gained `SID_REGS`, the register block as flat bytes, replacing the
   two hard-coded `0xD400` casts. On the host build it points at the test's
   buffer, which is what lets `sound_test.cc` call `sound_silence()` — the
   write-through would otherwise be a store to address `0xD400` on a PC.

   **2a — roughness.** The first build was correct and sounded like a
   synthesizer. Pitch and pulse width are now both perturbed every frame from
   an 8-bit LFSR; see the roughness part of §6 for what was chosen and what
   was rejected. `sound_engine_base_freq()` was exported at the same time,
   because once the emitted frequency is deliberately never the table value, a
   test that can only see the output cannot tell a correct table from a broken
   one.

   Verified: host tests pass, including ten deliberate mutations of `sound.cc`
   that `sound_test.cc` catches individually — volume never written, linear
   pitch table, PWM derived from throttle, gate never set, fuel predicate
   dropped, LFSR seeded zero, pitch jitter disabled, jitter bound widened,
   PWM jitter removed, PWM phase step fixed. **Not** caught by any test:
   feeding both jitter terms from the same LFSR draw, which correlates timbre
   with pitch. It is audible rather than incorrect, and testing for it means
   reconstructing the draw from the output, which fits the test to one
   mutation rather than to the property.

   **Not** verified at the time: the oscar64 build, `ppilot.map`, and how any
   of it sounds. The first two have since been checked — see the build note at
   the end of this section.
3. **Wind.** ✅ Done. Voice 2 noise, brightness from `flight_speed` through a
   16-entry geometric table (§6 option 3), gated off below `kWindMinSpeed`, and
   balanced under the engine by a static sustain difference.

   `kMaxSpeed` moved from `flight.cc` to `flight.h` for the same reason
   `kMaxThrottle` did in phase 2 — the table is sized by the speed envelope
   rather than by a second copy of `0x0F00`.

   `sound_wind_freq()` and `sound_wind_audible()` are exported alongside
   `sound_engine_base_freq()`. The gate threshold is a decision rather than an
   implementation detail, and the table's shape is the thing worth asserting
   on.

   Verified: the table monotonic and geometric across the whole envelope;
   clamping at both ends including negative speeds, which matters because
   `flight_speed` is signed and a negative right-shift stays negative; exactly
   one gate crossing, so there is no band where the wind flutters on and off;
   wind sustain strictly below the engine's; the silence predicates zeroing
   voice 2 along with everything else. **Not** verified at the time: the
   oscar64 build, since checked, and how it sounds against the engine, which is
   still phase 8.

   **3a — the dropout.** Testing in VICE turned up one or both continuous
   voices going silent during flight and returning seconds later. Cause: a torn
   read gating a voice on over a zeroed sustain, which the chip latches at zero
   and never lifts. See §3 for the mechanism and §7 for how it is now tested;
   the fix is that the shadow has no blanking pass and the control register is
   written last within a voice.

   Worth recording that this was a *design* error rather than a slip. `sound.h`
   asserted torn reads were harmless and the code was written to that
   assumption; §2 already contained the fact that falsified it. The exhaustive
   sweep found 4 of 40 possible interrupt instants stranded a voice — stores 29
   and 30 for the engine, 36 and 37 for the wind, which is exactly the two-store
   window between a voice's control register and its sustain.
4. **Flight model interface.** ✅ Done. `flight_events` and `flight_gen` in
   `flight.cc`, plus the private `model_pending_events` the frame ordering
   forced (§5). Touchdown is set on the `!was_on_ground` transition; gear and
   flap in both branches of `flight_input()`, since the ground and airborne
   switches are separate and each has its own cases.

   `flight_stall` was already done — it landed with the STALL panel lamp,
   ahead of the audio work, because the panel needed the same byte.

   `flight_test` still passes unchanged, which was the point of putting this
   phase after the audio path was proven.
5. **Stall warning.** ✅ Done. A ~840 Hz pulse tone on voice 3, retriggered
   every 3 frames while `flight_stall` is set. Pulse rather than noise so it
   is distinct from the wind bed; up where the engine's harmonic stack is thin,
   which §10 gives as the lever to reach for instead of ducking the engine.

   `sound_blit()`'s retrigger pass was narrowed to voice 3 as part of this —
   see §6. That is the change most likely to be undone by accident and the one
   the tests cannot catch.
6. **One-shots.** ✅ Done. Touchdown, gear and flap on voice 3, the generation
   handshake, and the priority order `touchdown > stall > gear > flap`. Losing
   one-shots are dropped rather than queued; a losing stall simply takes the
   voice back on the first frame nothing outranks it.

   **6a — audibility.** First listen: gear and flap were barely audible. Both
   causes were in the envelope rather than the mix — they used a decay-to-zero
   shape, so the sound was already fading 2 ms in, and at 72 ms flap was over
   before it registered. One-shots now hold a plateau at full sustain and are
   gated off after a per-effect frame count of 300–500 ms. `kGearFreq` also
   moved up from `$0300`, which had put it underneath the engine's fundamental.

   **6b — the stall too.** The same fix, applied to the one effect 6a left
   behind: the warning kept its decaying envelope on the theory that a
   repeating alarm should end itself. A decay is just a quieter sound, and a
   warning horn has more reason to carry than a gear click, not less. The
   warble's gap is now a gate-off rather than an envelope running out — which
   is also what let §7 assert on the gate bit instead of counting retriggers,
   and what let the interleaving invariant widen back to all three voices.

   **6c — the crash.** A ~1.6 s collapsing rumble on voice 3, swept from
   `$1000` down to `$0200`. `FLIGHT_EV_CRASH` needs no transition flag: the
   early return at the top of `flight_advance()` means reaching the end of a
   step while wrecked identifies the step that did it.

   This is the change that split the derived-silence rule into
   `_driver_live()` and `_flying()` (§3), because `flight_crashed()` was
   already a silence condition and the crash sound has to survive it. Nine
   further mutations fail the tests, including the crash event never published,
   the crash gated behind `_flying()`, the sweep flattened, the burst
   shortened, the two predicates collapsed back into one, and the crash left
   running through an `R` restart.

   Verified across phases 4–6: nine mutations of the arbitration each fail the
   test individually — stall held rather than retriggered, gate never set,
   stall on the wrong waveform, events consumed without the generation check,
   gear promoted above stall, touchdown demoted below it, the generation check
   removed, silence letting voice 3 run on, and flap made identical to gear.
   **Not** verified at the time: the oscar64 build, since checked, and how any
   of it sounds, which is still phase 8.
7. **Tests.** ✅ Done. `c64o/test/sound_test.cc` per §7. It landed in phase 2
   rather than waiting, covering the register block layout, the silence
   predicates and the engine voice; each later phase extended it rather than
   creating it. It now covers §7's whole minimum list plus the exhaustive
   interleaving sweep that came out of 3a, and it runs under `make test`
   alongside `flight_test`, `msg_test` and `map_test`.
8. **Verification.** ⬜ Not started, and now the only phase that is. Both PAL
   and NTSC in VICE; both 6581 and 8580 as a sanity check even though §3
   removes the dependency. Everything still open in §10 is queued behind this,
   because all of it is a judgement by ear.

### The oscar64 build

Phases 2, 3 and 6 each recorded "**not** verified: the oscar64 build" — the
work was done against the host tests, which compile `sound.cc` with `g++` and
never touch the cross-compiler. That gap is now closed, at the state after
phase 6:

- All four programs build: `ppilot`, `polydemo`, `vecdemo`, `vectest`.
- `check_zeropage.py` passes on all four; `ppilot` still has 5 bytes of
  headroom between the runtime's high water mark at `$5A` and the region start
  at `$60`.
- All four host suites pass, `sound_test` included.
- The §4 stack and cycle gates hold — see there for the numbers.
- `ppilot.prg` is 40,859 bytes, `$0801–$A799`, leaving the heap at
  `a890 - d000`: **9.9 KB free**.

What this does *not* cover is anything about how it sounds, which is phase 8
and cannot be automated. A clean build and a passing test suite are consistent
with a driver that is inaudible, deafening, or playing the wrong thing.

Two orderings in that list were deliberate and both paid off. Phase 4 was the
only one touching tested code and was placed after the audio path was proven,
so a `flight_test` failure would have had one obvious cause; it passed
unchanged. And phase 5 came before phase 6 because the stall is the harder
consumer of voice 3 — it is the one with a rhythm — so the priority logic in
phase 6 was written against a case that already existed rather than an imagined
one.

What the ordering did *not* protect against is the two defects that actually
turned up, 3a and 6a/6b. Both were found by listening, neither was reachable
from the host tests as written, and both were in the layer the plan spent least
time on: 3a in the shadow's write ordering, 6a/6b in envelope shapes. That is
the argument for phase 8 being a real phase rather than a formality.

---

## 10. Open questions

**~~Engine: instant or lagged?~~ Resolved in phase 2: instant.** The pitch
table is indexed directly by `flight_throttle`. The alternative was one byte of
state and three instructions per frame:

```c
rpm += (target - rpm) >> 2;
```

The argument for it was that a real engine spooling up is the sound of mass,
and that this is the largest single difference between "an engine" and "a
synthesizer following my keypresses." That argument still stands and this is
worth revisiting in phase 8, by ear. It was not taken now for two reasons.
Throttle is a keypress-at-a-time control, so a step is one table entry — 0.54
semitones, small enough that instant does not read as a jump. And a lag term
is state, which means it needs a defined value after `R`, after a crash, and
across the menu; adding it while the driver is one voice old buys a
correctness question in exchange for a nuance nobody has heard yet. It is a
three-line change to add later, and `sound_test.cc` already pins the mapping
it would have to preserve.

**~~Wind intensity: filter sweep or noise frequency?~~ Resolved in phase 3:
noise frequency,** §6 option 3. Option 1 — a filter cutoff sweep — sounds
better and is the standard approach, but it is chip-dependent in exactly the
way §3 spent a page arguing against, and this is a `.prg` shipping to unknown
emulators and unknown hardware. Option 3 costs one register, needs no
retrigger, and behaves identically on a 6581 and an 8580.

It remains the thing to reach for if the wind disappoints by ear, and the cost
of switching is contained: `sound_wind_freq()` is the only place that decides
what a speed sounds like. Worth noting that option 1 would apply the filter to
voice 2 alone, so the chip dependency would be confined to one sound rather
than reintroduced across the design.

**~~Stall warble rate and burst length?~~ Built, not yet judged.** Two frames on
out of four, so roughly 200 ms of tone and 200 ms of silence at 2.5 Hz. Both are
frame counts now — `kStallOnFrames` and `kStallPeriodFrames`, next to each other
in `sound.cc`. The constraint to respect when retuning is that the release
(`kStallSusRel`, 72 ms) has to fit inside the gap, or consecutive beeps run
together and the rhythm that carries the warning is lost.

**Are the one-shot pitches right?** `kTouchdownFreq`, `kGearFreq` and
`kFlapFreq` were picked to be distinct and out of the engine's way, not by
listening. Gear is the lowest and longest because §6 asks for "mechanical, not
impact"; touchdown is the brightest.

The first set was judged by ear and failed — gear and flap were barely
audible — but the fix was the envelope rather than the pitches (§6), so the
pitches themselves are still only half-tested. Gear and flap now sit inside the
wind's noise band (1443–5773 shifts/sec); they are transients at full sustain
against a bed at 10, which should be enough, but if they sound muddy on
approach rather than quiet, that is the thing to change and moving them *above*
the wind is the direction.

**Should the stall warning duck the engine?** Not currently specified, and
probably not worth it — `$D418` is master volume only (§2), so ducking one
voice is not available, and dropping the master would duck the warning along
with everything else. Noted because it is the obvious thing to reach for when
the warble turns out to be hard to hear over the engine, and it is a dead end.
The lever that does work is voice 3's pitch: put it where the engine's
harmonic stack is thin.

**What is worth optimising?** §4 has the measurements. In descending order of
value, and none of it urgent — the heap still has ~9.7 KB free:

1. **Move nine bytes out of zero page.** `_pwm_phase`, `_rng`, `_v3_effect`,
   `_v3_frames`, `_stall_phase`, `_flight_gen_seen`, `flight_events`,
   `flight_gen` and `flight_stall` are main-line only, and the zero page region
   is 100% allocated. Only `sound_gen` and `sound_gen_seen` are read from the
   interrupt and earn their place. This costs a few bytes of code (absolute
   rather than zero-page addressing) to buy back the scarcest RAM in the
   program.
2. **Turn the voice-3 parameter `switch` into a table.** Five effects × five
   registers is 101 bytes of comparison chains and immediate loads between the
   first and last `_set_voice` call. A `const` table indexed by the effect enum
   would be about 30 bytes of data and a handful of code, with the crash's
   swept frequency staying the one computed exception. This document asserted
   the opposite when the switch was written — "cheaper as a `switch` than as a
   lookup" — on no evidence.
3. **`sound_wind_freq` is exported only so the test can assert on it**, which
   costs it 33 bytes as a real function plus a call. Making it `static` under
   `__OSCAR64__` would let oscar64 inline it as it did with every other
   accessor.

Already done: the crash sweep dragged in `mul32by8`, a 54-byte runtime routine
nothing else in the program used, because it was written with a 32-bit cast in
a comment block explaining that the arithmetic fits in 16 bits. It is now two
shifts and a subtract, producing byte-identical output.

Also done, and the reason the jitter multiply is no longer on the list above:
`amp * n` now goes through `vec_fastmul8p8` rather than `mul16`. The shift
rewrite this section originally proposed was the wrong fix — `n` is a random
draw, not a constant, so no shift chain computes the product without changing
the jitter's distribution. Routing it through the quarter-square routine keeps
the arithmetic exactly as it was. `vec_fastmul8p8` returns `trunc(a * b / 256)`,
so shifting `n` up by 8 recovers the exact product and the `>> 4` still happens
in C; every `amp`/`n` pair produces the same register value as before.

The win was not the call setup. `render_fill_sky_ground`, `_pull_to_center` and
`_draw_one_box` were converted the same way in the same pass, which was the
point: `mul16` (66 bytes), `mul16by8` (56) and `mul16@proxy` (8) are all
unreferenced now and the linker drops them. Together with the smaller call
setup at each site that is 137 bytes off `ppilot.prg`.

Two things make the conversion non-obvious, and the first one is not the one
that bit. `vec_fastmul8p8` builds the product from the magnitudes and applies
the sign last, so it wraps sign-magnitude where `mul16` wrapped two's
complement — the documented hazard, and in the end not a problem at any of the
four sites. What actually shipped broken was the operand shift: only an
`int8_t` survives `<< 8` intact, and the horizon term negated its operand
*before* passing it, so `render_cy_chars == -128` became 128, wrapped back to
-128, and flipped the sign of the product. Diagonal bands through the sky fill.
Negating the result instead fixes it, and the reason it got through was a check
that assumed `render_cy_chars` could not reach -128 and skipped the case —
`render_cy_chars` is a truncating `int8_t` cast of a horizon that can sit far
off screen, so it reaches every value in the range.

`test/mul_test.cc` now replays all four conversions against the products they
replaced over each operand's full range, asserting nothing about what the call
sites are believed to stay inside. That is the test that would have caught it.

**~~Does the menu tune play under the help screen?~~ Answered in
[music.md](music.md) §3: yes, when help was opened from the menu.** The catch
that makes it a real question rather than a call site is that `help_run()` has
*two* callers — `menu.cc:144` and `sim.cc:229` — and the in-flight one must stay
silent. A `music_playing` flag set only by `menu_run()` covers both, since help
then ticks whatever is already running rather than starting anything.

**~~A mute key?~~ Done: `V`, and it is a volume rather than a mute.** Three
steps — off, low, full — because "too loud" and "off" are different complaints
and a plain mute only answers one of them. Step 0 is one more term in the
`_audible()` predicate (§3); steps 1 and 2 differ only in `$D418`.

**Superseded: it now cycles *downward* — full, low, off — and is bound on the
menu and help screens as well as in flight.** See [music.md](music.md) §3. The
original reasoning, kept because it is the argument that was wrong: it cycled
upward from a default of full so that the first press silenced the game, which
is what a player reaching for an unfamiliar key most likely wants. That holds
for a mute key. With three steps the second press has to mean something too,
and loud → off → quiet → loud gives one key two different meanings. Each press shows `SOUND OFF` / `SOUND LOW` /
`SOUND FULL` — with three steps the key alone no longer says where you ended
up, and without any message at all an accidental press is indistinguishable
from the sound having broken, which is the failure mode this whole module has
to design around.

`C`, `E`, `O`, `T`, `U`, `W`, `Y` and `0`, `4`–`9` remain unbound.
