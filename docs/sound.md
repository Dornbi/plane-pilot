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
| One-shots: touchdown, gear, flaps             | Digi samples, `$D418` tricks              |
| Explicit silence on every screen transition   | Doppler, ground rumble, crash sound       |
| Host tests over the register mapping          | A user-facing mute key                    |

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

| Item                  | Bytes | Phase 1 measured | Phase 2 |
| --------------------- | ----: | ---------------: | ------: |
| Shadow register block |    25 |               25 |      25 |
| Pitch table           |    50 |                — |      50 |
| Driver state          |   ~16 |    2 (zero page) |       3 |
| Code                  |  ~500 |              229 |     TBM |

Phase 1 cost 256 bytes all in, leaving the heap at `a460 - d000`, 10.7 KB.

Phase 2 added the pitch table at its predicted 50 bytes exactly (25 entries,
16-bit) and one byte of driver state for the PWM sweep phase. Code growth is
**not yet measured** — the phase 2 work was done without an oscar64 build to
hand, so `ppilot.map` has not been re-read. Do that before treating this phase
as closed; the number to check is whether `sound_update()` acquired an
`@stack` frame large enough to matter, and it should not have, since it has
one small helper and no recursion.

**Cycles.** The blit runs at raster 250. On PAL (312 lines) that leaves ~3900
cycles before the frame ends; on NTSC (263 lines) only ~800. This is the reason
the blit must stay a flat store sequence and not grow into a driver.

Measured on the phase 1 build, counting straight-line cost through
`_switch_to_terrain`: 147 cycles before, 331 after, so the blit is **184
cycles** with the generation check and about 214 on a tick that retriggers.
The handler therefore ends around raster 255 — 57 lines of border left on PAL,
8 on NTSC. Comfortable, but the NTSC margin is the number to watch if anything
is ever added here.

`sound_update()` on the main line is uncounted noise against a ~100 ms frame.

**Verification.** After building, `ppilot.map` must show **no `@stack` entry**
for the blit. That check belongs in the review, not in someone's memory — the
failure mode is silent corruption of an unrelated render frame, appearing once
every few thousand frames.

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
not restart the timbre from the same point every time. Frame jitter (§2) makes
the sweep slightly uneven, which is closer to a real engine than a metronomic
one and is left alone.

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

The choice is **open** — see §10.

`flight_speed` runs to `kMaxSpeed` (`0x0F00`); the mapping uses its top bits.

Note that voice 2 now carries wind and nothing else. Wind no longer has to
reserve headroom in whatever parameter it modulates for a buffet term on top,
so the speed mapping can use the full range of the mechanism it picks.

### Voice 3 — stall warning and one-shots

Voice 3 runs a small state machine, driven once per `sound_update()`. Its
inputs are `flight_stall` and, via the generation handshake in §5, at most one
event per frame. Priority when more than one wants the voice:

```
touchdown  >  stall  >  gear  >  flaps
```

A one-shot that loses is dropped, not queued (§5). A stall that loses is not
dropped in any meaningful sense — it is a level, so it simply gets the voice
back on the next tick that nothing outranks it, which for touchdown is
immediately and for gear or flaps is 200 ms later.

**The stall warble.** While `flight_stall` is set, voice 3 retriggers a short
tone every N ticks — bumping `sound_gen` so §5's blit-side gate cycle fires —
and stops when the flag clears. Two constants, a period and a duration, both
tuned by ear in phase 5.

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

| Event     | Character                                          |
| --------- | -------------------------------------------------- |
| Touchdown | Short noise burst, fast attack, medium decay        |
| Gear      | Lower, longer noise burst — mechanical, not impact  |
| Flaps     | Same family as gear, shorter and quieter            |

Gear and flaps sharing a family is deliberate: they are the same class of event
to the pilot, and differentiating them costs bytes for no information gain. All
three are noise-family, which separates them from the stall tone by waveform as
well as by rhythm.

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
- Touchdown and flaps in one generation produce the touchdown effect.
- A held event across generations does not retrigger.
- `flight_stall` set produces a voice-3 gate that goes off and on again over a
  run of ticks — the regression test for a stall warning that silently becomes
  a held tone.
- `flight_stall` clearing stops the retriggers, and nothing re-arms them.
- A gear event during a stall yields the gear effect on that tick and the
  warble again afterwards — the §6 priority order, and the case the old
  voice-2 buffet design got backwards.
- Crashed while `flight_stall` is still set produces gated-off voices. The
  flag holds its last value after a crash (`flight_advance()` returns early),
  so this only works if the derived-silence predicates are checked before the
  stall logic, and that ordering is the thing being tested.

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

   Verified: host tests pass, including five deliberate mutations of
   `sound.cc` (volume never written, linear pitch table, PWM derived from
   throttle, gate never set, fuel predicate dropped) which `sound_test.cc`
   catches individually. **Not** verified: the oscar64 build, `ppilot.map`, and
   how any of it sounds.
3. **Wind.** Voice 2 noise, intensity from `flight_speed` by the §6 mechanism.
4. **Flight model interface.** `flight_events` and `flight_gen` in `flight.cc`;
   confirm `make -C test flight_test` still builds and passes.

   `flight_stall` is **already done** — it landed with the STALL panel lamp,
   ahead of the audio work, because the panel needed the same byte. What is
   left for this phase is the event bitfield and the generation counter.
5. **Stall warning.** The voice-3 warble from `flight_stall`: period, burst
   length, waveform and pitch, all tuned by ear.
6. **One-shots.** The rest of voice 3 — the generation handshake and the
   priority table from §6, with the stall already in place to arbitrate
   against.
7. **Tests.** `c64o/test/sound_test.cc` per §7. Partly done: the file landed
   in phase 2 rather than waiting, covering the register block layout, the
   silence predicates and the whole engine voice. Each later phase extends it
   rather than creating it.
8. **Verification.** Both PAL and NTSC in VICE; both 6581 and 8580 as a
   sanity check even though §3 removes the dependency.

Phase 4 is the only remaining one that touches tested code, and it is
deliberately placed after the audio path is proven, so a `flight_test` failure
has one obvious cause. Phase 5 before phase 6 is also deliberate: the stall is
the harder consumer of voice 3 — it is the one with a rhythm — so building it
first means the priority logic in phase 6 is written against a case that
already exists rather than an imagined one.

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

**Wind intensity: filter sweep or noise frequency?** §6 options 1 and 3. Option
1 sounds better; option 3 removes the SID-revision dependency entirely and is
cheaper. Recommend building option 3 in phase 3 and only reaching for option 1
if it disappoints.

**Stall warble rate and burst length?** §6 suggests 3–4 Hz. At a 20 ms blit
tick that is a period of 12–16 ticks, but the burst length interacts with the
envelope: too long and the gap closes up into a held tone, too short and it
clicks. Judge by ear in phase 5.

**Should the stall warning duck the engine?** Not currently specified, and
probably not worth it — `$D418` is master volume only (§2), so ducking one
voice is not available, and dropping the master would duck the warning along
with everything else. Noted because it is the obvious thing to reach for when
the warble turns out to be hard to hear over the engine, and it is a dead end.
The lever that does work is voice 3's pitch: put it where the engine's
harmonic stack is thin.

**Does the menu tune play under the help screen?** §8. Not answerable until the
tune exists.

**A mute key?** Not in scope, but `C`, `E`, `O`, `T`, `U`, `V`, `W`, `Y` and
`0`, `4`–`9` are unbound if one is wanted. A continuous wind bed over a
3.5-minute flight is the kind of thing players ask to turn off.
