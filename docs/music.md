# Music — Implementation Plan

The menu is silent. This document plans the title tune: a 16-bar rock intro in
A minor, three voices, looping, playing on the mission-select screen and under
the help screen it opens.

[`sid-intro-theme.html`](sid-intro-theme.html) is the reference recording — open
it in a browser and press Play. It is not a mock-up: it runs the player this
document specifies on a 50 Hz frame tick, writes a 25-byte shadow of `$D400`
(which it displays live), and the audio engine reacts only to those registers.
Three voices, one oscillator each, drums stealing voice 3, pitches from the
octave-6 table in §5. So it is wrong in the ways a browser is wrong — timer
jitter, an approximated envelope, noise that is not the real LFSR through the
real DAC — and right about everything the plan is actually deciding.

It is a reference, not the source of truth; §5 says what is.

[sound.md](sound.md) plans the *flight* audio and is the document this one sits
next to. It is assumed rather than repeated: §8 there already reserved room for
a menu tune and made two decisions, and this plan starts by reversing one of
them. See [project.md](project.md) for the surrounding architecture.

---

## 1. Scope

| In                                             | Out (for now)                          |
| ---------------------------------------------- | -------------------------------------- |
| A 16-bar loop, three voices, ~25 s             | Music during flight                    |
| Playing under the menu and the help screen     | Gapless music across screen changes    |
| Drums stealing voice 3 from the arpeggio       | Music under the map (§3)               |
| Data generated from `lib/music.py`             | A general tracker / `.sid` player      |
| Respecting `sound_volume`, the existing `V` key | Digis, `$D418` tricks, filter sweeps   |
| Host tests over the player and the data        | Second tune, jingles, stingers         |

---

## 2. What sound.md already settled, and the one thing it got wrong

§8 of sound.md is the prior art and most of it stands.

**The tune is a different owner.** The flight driver's invariant is "the flight
driver owns the SID whenever raster IRQs are running", and the menu is the one
screen that wants sound and has no interrupt to drive it — `menu_run()` reaches
`screen_enter_static_mccm()`, which calls `gfx_stop_raster_irqs()`, which is a
bare `sei`. §8's answer was to drive the tune from `menu_run()`'s own
`gfx_wait_vsync()` loop instead. That is still the answer, and §3 below makes it
an invariant in its own right rather than an arrangement.

**NTSC tempo is accepted as-is.** A tune driven once per frame runs 20% fast on
a 60 Hz machine. Still not worth a fractional-tick counter.

**Zero page is not available.** §8 of sound.md establishes that `$02–$7F` is
oscar64's runtime, not free space, and that `check_zeropage.py` currently passes
with **5 bytes of headroom**. The player takes none. It is main-line code
running on the one screen where nothing else is competing for cycles, so
absolute addressing costs it nothing it will miss.

**The load address decision is moot.** §8 spent a paragraph on placing a tune
immediately below `$D000`, because ripped `.sid` files are position-dependent
and assembled for `$1000`, which is occupied here. That reasoning was correct
for a rip. This tune is compiled in — `musicdata.cc` is ordinary `data` and the
player is ordinary `code`, both placed by the linker like everything else. The
paragraph applies again the day someone wants to play a real `.sid`.

---

## 3. Decisions and rationale

### A second ownership rule, stated the same way as the first

> **The menu tune owns the SID whenever `music_playing` is set.**
> It must be set only by `music_start()`, and cleared — with the chip
> silenced — before control leaves the screen that started it.

The two owners are mutually exclusive by construction, which is the property
worth understanding rather than testing. `sound_blit()` only runs from
`_switch_to_terrain`, so the flight driver can only write `$D400` while raster
interrupts are running; `music_tick()` only runs from a `gfx_wait_vsync()` poll
on a screen that reached it through `gfx_stop_raster_irqs()`, so the tune can
only write `$D400` while they are masked. Neither needs to know about the other
and no arbitration exists to get wrong.

`gfx_stop_raster_irqs()` already calls `sound_silence()` on the way in, so the
tune inherits a chip with every gate clear and the master volume at zero. It
does not need to clear anything before it starts.

### `help_run()` has two callers, and only one of them wants music

This is the trap in the whole design, and it is worth naming before the code
exists. `help_run()` is reached from `menu.cc:144` and from `sim.cc:229` — `H`
works in flight as well as in the menu. Both paths call
`screen_begin_text_page()`, so both mask interrupts and both silence the flight
driver. A `music_tick()` dropped unconditionally into `help_run()`'s loop
therefore starts the title tune when the pilot pauses mid-mission to check the
controls, which is not what anyone asked for.

The guard is that `music_tick()` returns immediately unless `music_playing` is
set, and only `menu_run()` sets it. Help does not start or stop the tune; it
only keeps ticking whatever is already running. One flag, one early return, and
the in-flight help screen stays silent for free.

That resolves §10's open question in sound.md — *"does the menu tune play under
the help screen?"* — as **yes, when help was opened from the menu**, which is the
version of "yes" that is actually wanted.

### Not under the map, and not gapless

sound.md §8 already worked out why, and the conclusion has not changed:
`map_enter()` banks I/O out with `mmap_set(MMAP_RAM)`, so `$D400` is plain RAM
for the duration of the map's multi-frame rebuild. A tune would stutter through
it even with an interrupt-driven player, and the map is reachable only from
flight, where there is no tune anyway.

Gapless music across screen transitions is the one thing a polling player cannot
do, since the loop necessarily stops. The transitions here are menu → flight and
menu → help. The first *should* be a hard cut: the tune ending is how you know
the mission started. The second is covered by the paragraph above. So the
feature a raster-driven player would buy has no customer, and the polling loop
keeps the design at one screen's worth of machinery.

### The frame is the clock, and the tempo has to accept that

`gfx_wait_vsync()` spins on `vic.raster != 255` and then on `== 255`, so it
returns exactly once per video frame — a genuine 50 Hz tick, and a far better
clock than anything the flight loop has. sound.md §2 spends a page on the frame
rate being unusable as a timebase; none of that applies here, because the menu
does almost nothing per frame.

The cost is that tempo is **quantized**. With a pattern row of one sixteenth
note and an integer number of frames per row, only these tempos exist:

| Frames/row | BPM   | Frames/loop | PAL     | NTSC    |
| ---------: | ----: | ----------: | ------: | ------: |
| 3          | 250.0 |         768 | 15.36 s | 12.80 s |
| 4          | 187.5 |        1024 | 20.48 s | 17.07 s |
| **5**      | **150.0** |    **1280** | **25.60 s** | **21.33 s** |
| 6          | 125.0 |        1536 | 30.72 s | 25.60 s |

The browser reference was written at 165 BPM, which is not on the list — it
falls between 4 and 5 frames per row, at 4.545.

**Five frames per row, and the reference moves to 150 BPM to match.** The
alternative that keeps 165 is an alternating 4, 5, 4, 5 row length: an eighth
note is then exactly 9 frames and the tempo lands at 166.7 BPM, within 1% of the
original. It is also a **swing** — consecutive sixteenths at 80 ms and 100 ms is
a 44:56 shuffle — and the sixteenth runs in bars 13 to 16 are the melodic point
of the arrangement and are meant to be even. A shuffle that arrived as a
rounding artefact is the wrong way to acquire one.

187.5 was the other candidate and is genuinely fast rather than driving; at
that tempo the bar-15 run is 21 notes in 3.4 seconds. 150 BPM with eighth-note
bass keeps the drive in the rhythm section, where it belongs, and 25.6 s stays
inside the 30 s the tune was scoped at.

**The reference already plays at 150 BPM on a 50 Hz tick** — a browser version
running at a tempo the C64 cannot reach would be worse than no reference at all.
Once `lib/music.py` generates both outputs (§5) the tempo is one constant in one
file and the two cannot drift apart.

### Three voices: lead, bass, and a shared third

| Voice | Role                        | Waveform   | Retriggers   |
| ----- | --------------------------- | ---------- | ------------ |
| 1     | Lead melody                 | Pulse, PWM | Per note     |
| 2     | Bass                        | Pulse      | Per note     |
| 3     | Arpeggio, and drums stealing it | Saw / noise | Per hit  |

The same three-fixed-roles discipline as sound.md §3, for the same reason: with
no per-voice volume, the mix *is* the waveform and envelope assignment, and a
voice that changes role mid-tune has to be rebalanced against everything else
every time it does.

**The arpeggio advances one chord tone per frame** with the gate held — the
frequency register is rewritten, the envelope is not restarted. At 50 Hz that is
the classic SID chord shimmer, and it is the cheapest thing in the whole player:
two stores and a wrapping index. Note this is *faster* than the browser
reference, which runs three tones per sixteenth; a browser timer cannot be
relied on at 20 ms so the reference approximates. The C64 is the version that
sounds right.

### Drums steal voice 3, and that is a feature

Four parts on three voices is the constraint every SID tune had, and stealing
the arpeggio channel for percussion is how they answered it. The arpeggio is a
texture — losing 40 ms of it is inaudible — while a kick that is not on the beat
is not a kick.

Priority on voice 3 is **drum hit > arpeggio**, with no queuing and no
arbitration state beyond a countdown:

| Hit   | Frames | ms  | Waveform | Shape                          |
| ----- | -----: | --: | -------- | ------------------------------ |
| Kick  |      5 | 100 | Noise + downward sweep | thud, sweeps out of the way |
| Snare |      4 |  80 | Noise    | flat, bright                   |
| Hat   |      2 |  40 | Noise    | shortest thing the ADSR allows |

Measured over the whole loop in the reference: 56 kicks, 36 snares and 40 hats
take **504 of the 1,280 frames**, so the arpeggio holds voice 3 **60%** of the
time — enough to read as a continuous shimmer with holes punched in it, which is
exactly the sound. The `Drum steal` button on the reference page turns the
stealing off, which is the quickest way to hear what it is costing.

When a drum finishes, the arpeggio takes the voice back on the next frame with
a fresh gate. That is a retrigger of the arp envelope, which is audible as a
small swell rather than a click, and is why the arp gets a fast attack and full
sustain (§6) rather than anything with a decay in it. sound.md learned that one
the expensive way in its phases 6a and 6b: **a decaying envelope is just a
quieter sound**.

### Hard restart, but only where a note actually changes

The SID's envelope generator does not reliably retrigger on a gate that goes low
and high within a few cycles, and a new note written over a still-releasing
envelope starts from wherever the old one was. The standard answer is the *hard
restart*: on the frame before a new note, clear the gate and write attack/decay
and sustain/release to `$00`, forcing the envelope counter down so the next
gate edge starts from silence.

Applied to every row that would cost a fifth of every note — at five frames per
row, one restart frame is 20% of a sixteenth. Applied only to rows where the
*next* row actually begins a new note, it costs almost nothing: the lead's
shortest note is two rows, so the restart frame is one in ten. Held notes and
the arpeggio never pay it, and the arpeggio never needs it, because it does not
retrigger at all.

The exception is the bass's fill bars — 4, 8, 12 and 16 — where the pickups are
single rows and a restart frame is 20% of the note after all. Those are the four
places in the tune where a sixteenth-note bass run has to be tight, so the honest
options are to keep the restart and accept a shorter note, or to skip it there
and accept a softer edge. **Keep it.** A note that starts late is still on the
beat; a note whose envelope starts halfway up is not the note that was written.

This is the decision most likely to be quietly dropped during a rewrite and the
one that most affects whether the tune sounds crisp or mushy. It gets its own
test (§7).

### Do not depend on the filter

Unchanged from sound.md §3, and worth restating because a title tune is exactly
where the temptation is strongest. The filter is one global resource, it is the
register block that differs most between a 6581 and an 8580, and it varies
between individual 6581s. A sweep tuned on one chip can be inaudible on another,
and this ships as a `.prg` to unknown emulators and unknown hardware.

What is lost is real — a filter sweep is a signature SID sound and the obvious
way to make an intro build. The substitute is the **pulse width sweep on the
lead**, which is chip-independent, costs two registers, and is what the browser
reference already uses. If the tune sounds flat by ear in phase 8, that sweep's
depth and rate are the knobs, not the filter.

### The tune respects `sound_volume`

`sound_volume` already survives `sound_init()` because it is a setting rather
than flight state, and a player who turned the sound off does not expect the
menu to be the exception. The tune's master volume is `kMasterVolume[]` indexed
the same way, so step 0 silences it with no second code path — the same argument
sound.md §3 makes for folding the volume key into a predicate rather than giving
it a silencing path of its own.

Binding `V` *in the menu* is a separate, smaller question, deferred to phase 6.
The complication is that `sound_cycle_volume()` prints `SOUND OFF` / `LOW` /
`FULL` through `msg.cc`, which writes into the flight viewport and has nowhere
to put a line on a text page.

---

## 4. Budgets

The whole point of this module is that it is the cheap one. It runs on a screen
that renders nothing per frame, with no interrupt in sight, and both of
sound.md's expensive constraints — the frame-free interrupt handler and the
nine-raster-line NTSC margin — are simply absent.

**Cycles.** A PAL frame is 19,656 cycles. The menu loop currently spends a few
hundred on key reads and edge detection and then blocks in `gfx_wait_vsync()`.
The player's worst frame is a row boundary that starts a new note on all three
voices, at roughly 1,500–2,000 cycles — about 10% of a frame. There is no
deadline to miss and nothing downstream waiting on a raster line.

The one real hazard is the opposite of a cycle budget: **frames the menu skips**.
`_render_menu_items()` `memset`s 19 rows and re-renders four mission blurbs when
the cursor scrolls, which can overrun a frame. A skipped tick is a dropped
50th of a second in the tempo, once, on a keypress. Audible only if you are
listening for it, and cheaper to accept than to fix with a raster counter.

**RAM.** Estimated, and deliberately pessimistic — sound.md §4 records its own
code estimate coming in at **twice** what was projected, for a module of
comparable complexity.

| Item                            | Estimate |
| ------------------------------- | -------: |
| Note table (12 × 2)             |       24 |
| Chord table + triads            |       31 |
| Lead events (~110 × 2)          |      220 |
| Bass patterns + order           |       56 |
| Drum patterns + order           |       64 |
| Instrument table (6 × 5)        |       30 |
| **Data subtotal**               |  **425** |
| Player code                     |    ~900 |
| Player state (bss)              |     ~28 |
| **Total**                       | **~1.4 KB** |

Budget **2.5 KB** and expect ~1.8. The heap after phase 6 of sound.md is
`a890 - d000`, **9.9 KB free**, and nothing else allocates from it.

**Zero page: zero bytes.** Stated as a budget line because it is the scarcest
resource in the program (156 bytes, 100% allocated, 5 bytes of headroom before
the compiler's spill area collides with it) and because "just two bytes for the
row counter" is how that headroom disappears.

**Verification.** Unlike sound.md there is no `@stack` gate to check — the
player is main-line and a frame is fine. The gates that do apply after each
build are `check_zeropage.py` still passing on all four programs, and
`ppilot.prg` still fitting under `$D000`.

---

## 5. Where the tune comes from

`lib/music.py` is the source of truth. `tools/generate_music.py` reads it and
emits both consumers:

```
lib/music.py                  the arrangement: chords, melody, patterns, tempo
  │
  ├── tools/generate_music.py ──► c64o/musicdata.cc, c64o/musicdata.h
  └── tools/generate_music.py ──► the data block in sid-intro-theme.html
```

This is the pattern the repository already uses everywhere else —
`lib/chardefs.py` → `c64o/chardefs.cc`, `lib/spritedef.py` → `c64o/spritedef.cc`,
`gfx/ppilot_map_tiles.png` → `c64o/mapdefs.cc` — and it is used here for the
reason it exists: a browser reference and a C64 tune that are edited separately
diverge on the first bugfix, and then the reference is worse than nothing
because it is still convincing.

`make music` regenerates both, and joins `make data`. The exporter refuses to
write if a bar does not sum to 16 rows or a note falls outside the table's
range, so a bad edit fails at generation rather than at `oscar64` or, worse, by
ear.

### The arrangement

Sixteen bars in A minor, 150 BPM, five frames per row, sixteen rows per bar.

```
bar    1    2    3    4    5    6    7    8
      Am   F    G    Am   Am   F    G    E

bar    9   10   11   12   13   14   15   16
      C    G    Am   F    F    G    Am   G
```

Bar 16's descending run lands on the E that opens bar 1, so the loop point is a
turnaround rather than a splice — the seam is the one thing a 16-bar loop cannot
hide, and it is cheaper to compose around than to cross-fade.

Voices 2 and 3 are **generated from the chord table**, not stored: the bass
picks a root, fifth and octave out of it through one of two rhythm patterns, and
the arpeggio walks the triad. That is why the data budget above is mostly the
lead — 16 bytes of chords replace two full channels of pattern data.

### The note table

Twelve 16-bit entries for octave 6, shifted right for every octave below it,
since a SID frequency halves exactly per octave.

| Note | Hz      | `$D400` | Note | Hz      | `$D400` |
| ---- | ------: | ------: | ---- | ------: | ------: |
| C6   | 1046.50 | `$459C` | F#6  | 1479.98 | `$6272` |
| C#6  | 1108.73 | `$49C0` | G6   | 1567.98 | `$684C` |
| D6   | 1174.66 | `$4E23` | G#6  | 1661.22 | `$6E80` |
| D#6  | 1244.51 | `$52C8` | A6   | 1760.00 | `$7512` |
| E6   | 1318.51 | `$57B4` | A#6  | 1864.66 | `$7C08` |
| F6   | 1396.91 | `$5CEB` | B6   | 1975.53 | `$8368` |

PAL values, `Freg = f × 16777216 / 985248`. Octave 6 is chosen because it is the
highest one that fits: a B7 entry would be 67,290 and overflow the register.

The truncation a right shift introduces is bounded and small. Over the range the
tune uses (MIDI 28 to 83, E1 to B5) the **worst error is 0.090%**, at E1, where
five shifts leave 701 against an exact 701.6. A semitone is 5.95% and pitch
error becomes audible somewhere around 0.3%, so a 60-entry table would buy
nothing but 96 bytes of data.

NTSC is 3.7% flat on every note, uniformly — the whole tune is in a slightly
different key, in tune with itself. That is the same trade §2 accepts on tempo
and it needs no second table.

---

## 6. Sound design

**Voice 1, lead.** Pulse, with the width swept independently of the note so a
held note keeps moving. Fast attack, short decay, high sustain: the melody has
sixteenth-note runs in bars 13 to 15 and anything slower than a couple of
milliseconds turns them to mush. Pulse rather than saw because the sweep is the
only timbral movement the design allows itself (§3, on the filter).

The sweep is a triangle over an 0x800 range at **8 units per frame**, so it
cycles every 256 frames and divides the 1,280-frame loop exactly five times. The
step is chosen for that rather than for its rate: a step that did not divide the
loop would leave the pulse width somewhere different on every pass, and the
loop-point identity §7 tests on would fail for a reason that has nothing to do
with the tune. Any of 1, 2, 4, 8, 10, 16 works; 8 is the one that sounds right.

**Voice 2, bass.** Pulse, narrow, with a decay into a moderate sustain so the
eighth notes have a front edge. This is the voice carrying the drive; it plays
on every eighth through the whole tune and drops to sixteenths for the pickups
in bars 4, 12, 8 and 16.

**Voice 3, arpeggio.** Sawtooth, fast attack, full sustain, no decay. The
envelope is deliberately shapeless because the *rhythm* on this voice comes from
the drums interrupting it, and a shape underneath that would fight it. Sawtooth
rather than pulse to keep it distinct from both of the others in a mix that has
no filter to separate them with.

**Voice 3, drums.** Noise. The kick sweeps its frequency down across its five
frames, which is what makes it a drum rather than a burst of static; the snare
and hat are flat. All three run at full sustain for their whole length and end
with a gate-off, per sound.md's 6a/6b finding — the shortest sound in the tune
is 40 ms and there is no room in that for a decay to do anything but make it
quieter.

Balance between voices is set by envelope and waveform alone. `$D418` is master
volume only, so there is nothing else to set it with.

---

## 7. Testing

Two suites, split by what they can actually see.

**`tests/test_music.py`** (pytest, alongside `test_chardefs.py`) tests
`lib/music.py` and the exporter — the data, before it is a program:

- every bar sums to exactly 16 rows, on every channel
- every note lies inside the table's range (MIDI 28–83)
- the chord table has 16 entries and every chord type has a triad
- every order-list index references a pattern that exists
- the emitted `musicdata.cc` parses and its array lengths match its headers
- the browser page's data block and the `.cc` agree, entry for entry

**`c64o/test/music_test.cc`** (g++, alongside `sound_test.cc`) links `music.cc`
and `musicdata.cc` against the host `sid.h` and tests the *player*:

- the note table is monotonic and each entry is within 0.2% of equal temperament
- `music_start()` then 1,280 ticks returns the player to its exact start state —
  the loop-point identity, which catches almost every kind of counter drift
- voice 3 arbitration: a drum hit takes the voice for exactly its frame count,
  the arpeggio does not write the voice while a hit is live, and it resumes with
  a gate edge on the frame after
- the arpeggio advances one tone per frame and **never** clears its gate except
  when stolen — the shimmer collapses into a machine-gun if it retriggers
- hard restart happens on the frame before a new note and **only** there
- `music_stop()` leaves all three gates clear and `$D418` at zero
- `sound_volume == 0` produces a register stream with the master volume zero at
  every one of the 1,280 frames
- `music_tick()` with `music_playing` clear writes nothing at all — the guard
  that keeps the in-flight help screen silent

Mutations that must each fail a test individually: the loop length off by one
row, the arpeggio retriggering per tone, hard restart applied to every row,
hard restart removed, drums not stealing voice 3, a drum never releasing it,
the note table made linear, the octave shift inverted, `music_stop()` leaving
the master volume up, and the `music_playing` guard removed.

Every assertion in the second list has already been run against the reference
implementation in `sid-intro-theme.html`, and two of them found real bugs there
that would have been found again in 6502: a hand-back that re-gated voice 3 on
the same frame as its hard restart, cancelling it; and a free-running pulse-width
sweep whose period did not divide the loop. Both were invisible by ear and
obvious to the loop-identity check. That is the argument for writing
`music_test.cc` early rather than in phase 7 — sound.md's phase 7 note records
the same thing about `sound_test.cc`, which landed in phase 2.

**What neither suite can see** is the thing sound.md's phases 3a, 6a and 6b were
all eventually caught by: whether it sounds right. A player that passes every
assertion above can still be inaudible, deafening, or in the wrong key on a real
chip. That is phase 8 and it is a real phase.

---

## 8. Phases

Each phase leaves the program in a working, committable state.

0. **Reference implementation.** ✅ Done. `sid-intro-theme.html` — the player,
   the register shadow and the arrangement, at 150 BPM on a 50 Hz tick, with the
   §7 checks passing. Everything below is a port of something that exists and
   can be listened to, rather than a design being discovered in 6502.
1. **Data and exporter, no C64 change.** `lib/music.py`, `tools/generate_music.py`,
   `c64o/musicdata.{cc,h}`, `tests/test_music.py`, the `make music` target and
   its entry in `make data`. The reference's data block becomes a generated
   artefact in the same phase, so the two can never disagree afterwards (§5).
2. **Player skeleton and ownership.** `music.{h,cc}` with `music_start()`,
   `music_stop()`, `music_tick()` and the `music_playing` guard; the tick in
   `menu_run()`'s loop at `menu.cc:149` and `help_run()`'s at `help.cc:59`;
   start and stop bracketing `menu_run()`. Voice 1 only. First audible phase,
   and the phase where the help-screen guard gets checked by pressing `H` in
   flight and hearing nothing.
3. **Bass.** Voice 2, the two rhythm patterns, roots from the chord table.
4. **Arpeggio.** Voice 3, one tone per frame, gate held.
5. **Drums.** Voice 3 stealing, the priority countdown, the hand-back.
6. **Hard restart and envelope tuning.** Ordered deliberately after the drums:
   the restart interacts with every voice and is easier to get right against an
   arrangement that already exists than against one arriving underneath it. `V`
   in the menu lands here or is dropped (§3).
7. **Tests.** `music_test.cc` per §7, and the Makefile wiring in
   `c64o/test/Makefile` next to `sound_test`.
8. **Verification.** VICE, PAL and NTSC, 6581 and 8580. Everything in §9 is
   queued behind it, because all of it is a judgement by ear.

The ordering that matters is phase 2 before everything else: the ownership
guard is the only part of this module that can break something outside it, and
it is worth having in place and proven before there is a tune interesting
enough to be distracting.

---

## 9. Open questions

**Does the tune restart or resume across the help screen?** It keeps playing —
help never stops it (§3). But `help_run()` is entered from the menu with a
`screen_begin_text_page()` that `memset`s the whole screen, so the frame it
happens on is long. Whether that is one dropped tick or four is not known until
it runs.

**Is 150 BPM right?** It is the tempo the frame grid allows nearest the 165 the
arrangement was written at, and 187.5 was rejected on the argument in §3 rather
than by ear. If it drags in phase 8, the alternating 4/5 row length is still
available and buys 166.7 BPM at the cost of a shuffle.

**Is the lead loud enough over the bass?** With no per-voice volume this is
decided entirely by envelope and waveform, and it is the first thing that will
be wrong. The lever is the bass's sustain, not the lead's — sound.md §10 makes
the same point about the stall warning, and its conclusion, that pitch placement
works where volume does not, applies here too.

**Should the arpeggio drop out entirely in bars 9 to 12?** Those bars are the
lift in the arrangement and have the longest melody notes. Silence on voice 3
there would let the melody carry them, and would cost nothing but a bit in the
chord table. Composition question, best answered after phase 8.

**Should `V` work in the menu?** §3. The blocker is that
`sound_cycle_volume()`'s confirmation message goes through `msg.cc`, which has
nowhere to write on a text page. Cheapest answer is probably a fixed row on the
menu screen rather than teaching `msg.cc` a second output.

**Does anything want a second tune?** A short jingle on mission completion is
the obvious candidate and would reuse the player unchanged, but it would play
during flight, where the flight driver owns the SID and the ownership rule in
§3 says only one of them can. That is a real design question and not a small
one. Out of scope, noted so the answer is not accidentally foreclosed.
