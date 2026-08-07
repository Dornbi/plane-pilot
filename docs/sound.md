# Sound — Implementation Plan

Plane Pilot is silent. This document plans flight audio: a propeller engine
that tracks throttle, wind that tracks speed, buffet near the stall, and
one-shot effects for touchdown, gear and flaps. Sound plays only while
actually flying — not in the menu, not on the map, not while paused.

See [project.md](project.md) for the surrounding architecture.

---

## 1. Scope

| In                                              | Out (for now)                              |
| ----------------------------------------------- | ------------------------------------------ |
| Engine tone, tracking throttle                  | Menu / mission music (§8 reserves room)    |
| Wind, tracking airspeed                         | Per-voice mixing beyond three fixed roles  |
| Stall buffet, as a modulation of the wind voice | Stereo / dual-SID                          |
| One-shots: touchdown, gear, flaps               | Digi samples, `$D418` tricks               |
| Explicit silence on every screen transition     | Doppler, ground rumble, crash sound        |
| Host tests over the register mapping            | A user-facing mute key                     |

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

**Frames are slow and jittery.** `mem_switch_buffer()` waits on
`gfx_wait_vsync()`, so a frame is a whole number of 20 ms ticks and the count
varies with roll angle and polygon load. `flight_advance()` therefore runs at a
wobbling ~10 Hz — too slow and too irregular to drive a SID directly.

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

### `_switch_to_terrain`, not the other two handlers

`_switch_to_panel_top` is cycle-counted with 16 hand-placed `nop`s and
early-returns when `mem_debug_enabled` — adding work there breaks the mode
split, and would drop audio in debug view. `_switch_to_panel_bottom` also
early-returns in debug. `_switch_to_terrain` is the shortest handler, has no
debug early-return, and runs after its register writes are done, in the lower
border where there is slack.

### Silence is an invariant, not eight function calls

> **The flight driver owns the SID whenever raster IRQs are running.**
> Anything else that wants the SID must take ownership explicitly, and silence
> it on release.

Because the blit lives in a raster handler, "IRQs masked" and "driver not
running" are the same statement. So a single `sound_silence()` at the top of
`gfx_stop_raster_irqs()` — *before* the `sei`, and before `map_enter()` banks
I/O out — covers the menu, the help screen and the map. No un-mute is needed;
the driver resumes on the next interrupt after `gfx_init_raster_irqs()`.

Everything else is derived state, requiring no call at all, because the
simulation loop keeps running in those cases:

| Condition          | Handled by                                            |
| ------------------ | ----------------------------------------------------- |
| Map open           | `sound_silence()` via `gfx_stop_raster_irqs()`        |
| Help screen        | `sound_silence()` via `gfx_stop_raster_irqs()`        |
| `Q` to main menu   | `sound_silence()` via `gfx_stop_raster_irqs()`        |
| `P` paused         | derived: `flight_paused`                              |
| Crashed            | derived: `flight_crashed()`                           |
| Out of fuel        | derived: `flight_fuel == 0`                           |
| `R` reset          | derived: state is re-read next frame                  |

Eight cases, one call site and one predicate. **The invariant fails if a future
screen stops the driver without masking interrupts** — that is the thing to
watch for when adding screens.

### Three voices, three fixed roles

| Voice | Role                    | Waveform | Continuous? |
| ----- | ----------------------- | -------- | ----------- |
| 1     | Engine                  | Pulse    | yes         |
| 2     | Wind, modulated by buffet | Noise  | yes         |
| 3     | One-shots               | varies   | no          |

Buffet is *continuous* — it lasts as long as the aircraft is near the stall — so
giving it its own voice would leave nothing for one-shots. It is also a
noise-family sound, the same family as wind. Folding it into voice 2 as a
modulation collapses the contention entirely and is closer to what buffet
physically is: the airflow over the wing going rough.

That leaves voice 3 for transients only, which sidesteps a second problem.
Priority between a continuous bed and a 200 ms one-shot is the wrong frame; had
buffet preempted a gear sound, buffet would vanish during gear-down on
approach — exactly backwards from what the pilot needs to hear — and retrigger
with a click when the gear sound ended.

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

**RAM.** Trivial against what is available. `ppilot.map` reports the heap as
`a3c0 - d000` — 11.3 KB contiguous, with nothing allocating from it.

| Item                  | Bytes |
| --------------------- | ----: |
| Shadow register block |    25 |
| Pitch table           |    50 |
| Driver state          |   ~16 |
| Code                  |  ~500 |

**Cycles.** The blit runs at raster 250. On PAL (312 lines) that leaves ~3900
cycles before the frame ends; on NTSC (263 lines) only ~800. A 25-byte unrolled
blit plus the generation check is ~250 cycles, so both targets fit. This is the
reason the blit must stay a flat store sequence and not grow into a driver.

`sound_update()` on the main line is uncounted noise against a ~100 ms frame.

**Verification.** After building, `ppilot.map` must show **no `@stack` entry**
for the blit. That check belongs in the review, not in someone's memory — the
failure mode is silent corruption of an unrelated render frame, appearing once
every few thousand frames.

---

## 5. The interface out of `flight.cc`

Today `model_on_ground` is `static`, there is no exported stall condition, and
the only status signal is the `flight_status` crash latch. Three things are
added.

### `flight_buffet` — a magnitude, not a flag

`flight.cc` already computes a local `stall_speed` (`kStallSpeedWithoutFlaps`
`0x0400`, `kStallSpeedWithFlaps` `0x0340`, plus an altitude penalty). Export the
*deficit*, scaled to a byte:

```c
extern uint8_t flight_buffet;   // 0 = clean, 255 = deep stall
```

A boolean would chatter at the threshold and give a hard on/off. A magnitude
lets buffet build as margin decays, which is both more useful to the pilot and
more realistic. Ground mode already has no stall (`flight.cc:579`), so it is
silent on the runway for free.

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
flight_advance():          clears flight_events at the top of the step,
                           sets bits during it,
                           flips flight_gen LAST.

sound_update():            compares flight_gen against its own copy;
                           acts only on a change.
```

Flipping `flight_gen` after the event bits acts as a release barrier: an
observer that sees the new generation is guaranteed to see the complete event
set. Two bytes of state, no locks, no disabled interrupts.

**Priority within a generation is touchdown > gear > flaps**, and lower-priority
events are **dropped, not queued**. Queuing would surface a flap click 200 ms
after the key press, which reads as a bug rather than as feedback.

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

Frequency comes from the 25-entry table indexed by throttle (§3).

Whether the table is indexed by `flight_throttle` directly or by a lagged value
is **open** — see §10.

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

Option 3 also composes cleanly with buffet: buffet drops the noise frequency and
adds a low-rate wobble, which is what buffet sounds like. The choice is **open**
— see §10.

`flight_speed` runs to `kMaxSpeed` (`0x0F00`); the mapping uses its top bits.

### One-shots — voice 3

| Event     | Character                                          |
| --------- | -------------------------------------------------- |
| Touchdown | Short noise burst, fast attack, medium decay        |
| Gear      | Lower, longer noise burst — mechanical, not impact  |
| Flaps     | Same family as gear, shorter and quieter            |

Gear and flaps sharing a family is deliberate: they are the same class of event
to the pilot, and differentiating them costs bytes for no information gain.

---

## 7. Testing

`c64o/test/` already builds `flight_test`, `map_test` and `msg_test` natively
against the host, driven by `make -C test`. A `sound_test.cc` drops into that
pattern unchanged.

The split in §3 is what makes this possible: `sound_update()` is a pure function
from `(throttle, speed, buffet, events, paused / crashed / fuel)` to 25 bytes.
The test asserts on the bytes.

This matters more here than elsewhere, because **the failure mode of audio code
is silence**, which no amount of playing the game reliably surfaces. Minimum
cases:

- Paused produces gated-off voices.
- Crashed produces gated-off voices.
- Fuel exhausted produces gated-off voices.
- Throttle `0x18` and throttle `0x12` produce *different* voice-1 frequencies —
  the regression test for the pitch-compression bug a linear map would ship.
- Touchdown and flaps in one generation produce the touchdown effect.
- A held event across generations does not retrigger.

---

## 8. Reserving room for a menu tune

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

1. **Plumbing, silent.** `sound.h` / `sound.cc`, the shadow block,
   `sound_silence()` in `gfx_stop_raster_irqs()`, the blit in
   `_switch_to_terrain`. Everything gated off. Confirm `ppilot.map` shows no
   `@stack` entry for the blit, and that frame timing is unchanged.
2. **Engine.** Pitch table, pulse waveform, PWM sweep, the derived silence
   predicates. First audible phase.
3. **Wind.** Voice 2 noise, intensity from `flight_speed` by the §6 mechanism.
4. **Flight model interface.** `flight_buffet`, `flight_events`, `flight_gen`;
   confirm `make -C test flight_test` still builds and passes.
5. **Buffet.** Fold `flight_buffet` into voice 2.
6. **One-shots.** Voice 3, the generation handshake, the priority table.
7. **Tests.** `c64o/test/sound_test.cc` per §7.
8. **Verification.** Both PAL and NTSC in VICE; both 6581 and 8580 as a
   sanity check even though §3 removes the dependency.

Phase 4 is the only one that touches tested code, and it is deliberately placed
after the audio path is proven, so a `flight_test` failure has one obvious
cause.

---

## 10. Open questions

**Engine: instant or lagged?** Currently specified as instant — the pitch table
indexed directly by `flight_throttle`. The alternative is one byte of state and
three instructions per frame:

```c
rpm += (target - rpm) >> 2;
```

That is the entire cost, and it is the largest single difference between "this
is an engine" and "this is a synthesizer following my keypresses." A real engine
spooling up is the sound of mass. Recommend revisiting during phase 2, when it
can be judged by ear rather than argued.

**Wind intensity: filter sweep or noise frequency?** §6 options 1 and 3. Option
1 sounds better; option 3 removes the SID-revision dependency entirely and is
cheaper. Recommend building option 3 in phase 3 and only reaching for option 1
if it disappoints.

**Does the menu tune play under the help screen?** §8. Not answerable until the
tune exists.

**A mute key?** Not in scope, but `C`, `E`, `O`, `T`, `U`, `V`, `W`, `Y` and
`0`, `4`–`9` are unbound if one is wanted. A continuous wind bed over a
3.5-minute flight is the kind of thing players ask to turn off.
