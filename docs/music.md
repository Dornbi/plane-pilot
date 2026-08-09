# Music — Implementation Plan

The menu is silent. This document plans the title tune: a 16-bar atmospheric
theme in D minor, three voices, looping every 30.7 seconds, playing on the
mission-select screen and under the help screen it opens. It ships in
`ppilot.prg` only — `ppilotd.prg`, the debug build, stays silent (§4).

[`sid-intro-theme.html`](sid-intro-theme.html) is the reference recording — open
it in a browser and press Play. It is not a mock-up: it runs the player this
document specifies on a 50 Hz frame tick, writes a 25-byte shadow of `$D400`
(which it displays live), and the audio engine reacts only to those registers.
Three voices, one oscillator each, drums stealing voice 3, pitches from the
octave-6 table in §5. So it is wrong in the ways a browser is wrong — timer
jitter, an approximated envelope, noise that is not the real LFSR through the
real DAC — and right about everything the plan is actually deciding.

It carries **two** arrangements behind a switcher: the atmospheric theme planned
here, and the earlier rock intro at 150 BPM. The second one is kept
deliberately. It is the only way to A/B a mood decision by ear, and it costs
nothing but a table entry — see §9, where the choice between them is still open.
Both are 16 bars now, so the comparison is between arrangements; the tempi
differ because tempo is part of what each arrangement *is*, not because the
test is uncontrolled. Making both 125 BPM is one byte in `lib/music.py` if the
stricter comparison is ever wanted, but a rock intro at 125 is a different piece
than the one being judged.

It is a reference, not the source of truth; §5 says what is.

[sound.md](sound.md) plans the *flight* audio and is the document this one sits
next to. It is assumed rather than repeated: §8 there already reserved room for
a menu tune and made two decisions, and this plan starts by reversing one of
them. See [project.md](project.md) for the surrounding architecture.

---

## 1. Scope

| In                                              | Out (for now)                             |
| ----------------------------------------------- | ----------------------------------------- |
| A 16-bar loop, three voices, 30.7 s             | Music during flight, or help from flight  |
| `ppilot.prg` only, behind `__ENABLE_SOUND__`    | Anything in `ppilotd.prg`, the debug build |
| Playing under the menu and help opened from it  | Gapless music across screen changes       |
| Drums stealing voice 3 from the arpeggio        | Music under the map (§3)                  |
| A per-bar fade-in on `$D418` (§3)               | Per-voice volume — the chip has none      |
| Data generated from `lib/music.py`              | A general tracker / `.sid` player         |
| Respecting `sound_volume`, the existing `V` key | Digis, `$D418` sample tricks, the filter  |
| Host tests over the player and the data         | Jingles, stingers, mission-complete music |

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
a 60 Hz machine: 30.72 s becomes 25.60 s, and 125 BPM becomes 150. Still not
worth a fractional-tick counter.

**Zero page is tight, but it is a price rather than a wall.** §8 of sound.md
establishes that `$02–$7F` is oscar64's runtime rather than free space, and
`check_zeropage.py` currently passes with **5 bytes of headroom**. Room can be
made — the region in `mem.h` can be widened downward, or globals that do not
earn their place can be evicted; sound.md §10 lists nine of its own bytes that
are main-line only and have no reason to be there.

The player still asks for none, and the reason is about the player rather than
about scarcity: it runs main-line on a screen where nothing else is competing
for cycles, so absolute addressing costs it nothing it will miss. If phase 8
turns up a real reason, the two bytes worth having are the row and frame
counters in the tick's inner path — and at that point the question is what they
displace, not whether the space exists.

**The load address decision is moot.** §8 spent a paragraph on placing a tune
immediately below `$D000`, because ripped `.sid` files are position-dependent
and assembled for `$1000`, which is occupied here. That reasoning was correct
for a rip. This tune is compiled in — `musicdef.cc` is ordinary `data` and the
player will be ordinary `code`, both placed by the linker like everything else.
The paragraph applies again the day someone wants to play a real `.sid`.

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

The cost is that tempo is **quantized**. A beat is four rows of sixteenths, so
`BPM = 60 × 50 / (4 × speed) = 750 / speed`, and only integer frame counts
exist. Over 256 rows:

| Frames/row | BPM   | Frames/loop | PAL     | NTSC    |
| ---------: | ----: | ----------: | ------: | ------: |
| 3          | 250.0 |         768 | 15.36 s | 12.80 s |
| 4          | 187.5 |       1,024 | 20.48 s | 17.07 s |
| 5          | 150.0 |       1,280 | 25.60 s | 21.33 s |
| **6**      | **125.0** |   **1,536** | **30.72 s** | **25.60 s** |

**Six frames per row, 125 BPM.** The arrangement is atmospheric rather than
driving — a four-bar pedal-bass opening, a motif that enters at bar 5, a
turnaround that is also the melodic peak — and every one of those depends on
having room to breathe. 125 BPM is the slowest tempo on the grid, which is the
correct end of it for this piece; the 150 BPM entry is where the rock
arrangement sits and the reference still has it for comparison.

### The bar count is not free either

A bar is `16 × speed` frames — 96 at speed 6 — and §6's pulse-width sweep cycles
every 256 frames. For the loop to be clean, `bars × 96` has to be a multiple of
256, which means **the bar count must be a multiple of 8**:

| Bars | Frames | PAL     | Loop-clean? |
| ---: | -----: | ------: | ----------- |
| 8    |    768 | 15.36 s | yes         |
| **16** | **1,536** | **30.72 s** | **yes** |
| 24   |  2,304 | 46.08 s | yes         |
| 28   |  2,688 | 53.76 s | no          |
| 32   |  3,072 | 61.44 s | yes         |

This arrangement started at 32 bars and came down twice — first to 24 when the
outro was cut (§5), then to 16 for RAM (§4). Both stops were on the list above,
which was luck rather than judgement the first time and is worth writing down
before someone tries 20 or 28. It is not an absolute constraint: 28 bars would
work if the PWM step moved from 8 to 16, since 2,688 / 128 = 21. But that
couples two unrelated numbers, and the reason to know it is so that a future
odd bar count fails loudly in the test rather than quietly in the pulse width.

**30.72 seconds is also the right answer to a problem the 32-bar version had.**
A player picking a mission is on this screen for perhaps ten to thirty seconds.
At 61 seconds most would never hear the section the arrangement built toward,
and every one of them heard the quietest part first. At 16 bars the whole tune
fits inside a plausible visit.

The reference page displays the tempo from this formula. It previously used
`(50/speed)*30`, which is `1500/speed` — exactly double — and reported 250 BPM
for a tune running at 125. Fixed, and noted here because a reference tool that
lies about the number the plan is deciding on is worse than one that shows
nothing.

### Three voices: lead, bass, and a shared third

| Voice | Role                            | Waveform    | Retriggers |
| ----- | ------------------------------- | ----------- | ---------- |
| 1     | Lead melody                     | Pulse, PWM  | Per note   |
| 2     | Bass                            | Pulse       | Per note   |
| 3     | Arpeggio, and drums stealing it | Saw / noise | Per hit    |

The same three-fixed-roles discipline as sound.md §3, for the same reason: with
no per-voice volume, the mix *is* the waveform and envelope assignment, and a
voice that changes role mid-tune has to be rebalanced against everything else
every time it does.

**The arpeggio advances one chord tone per frame** with the gate held — the
frequency register is rewritten, the envelope is not restarted. At 50 Hz that is
the classic SID chord shimmer, and it is the cheapest thing in the whole player:
two stores and a wrapping index.

The atmospheric arrangement leans on this far harder than the rock one. Voice 1 is
silent for the first four bars and voice 3 carries the opening alone, which is
also why the arpeggio's envelope has to be the shapeless one described in §6 —
for fifteen seconds it is not a texture under something, it *is* the tune.

### Drums steal voice 3, and that is a feature

Four parts on three voices is the constraint every SID tune had, and stealing
the arpeggio channel for percussion is how they answered it. The arpeggio is a
texture — losing 40 ms of it is inaudible — while a kick that is not on the beat
is not a kick.

Priority on voice 3 is **drum hit > arpeggio**, with no queuing and no
arbitration state beyond a countdown:

| Hit   | Frames | ms  | Waveform               | Shape                       |
| ----- | -----: | --: | ---------------------- | --------------------------- |
| Kick  |      5 | 100 | Noise + downward sweep | thud, sweeps out of the way |
| Snare |      4 |  80 | Noise                  | flat, bright                |
| Hat   |      2 |  40 | Noise                  | shortest the ADSR allows    |

Measured over the whole loop in the reference: 28 kicks, 18 snares and 52 hats
take **316 of the 1,536 frames**, so the arpeggio holds voice 3 **79%** of the
time. That is far more than the rock arrangement's 60%, and the difference is
the whole first half of the tune: bars 1 to 4 have no drums at all and bars 5 to
8 have only hats, so the shimmer is uninterrupted for exactly as long as it is
the only thing playing. The `Drum steal` button on the reference page turns the
stealing off, which is the quickest way to hear what it costs once the kit does
come in.

When a drum finishes, the arpeggio takes the voice back on the next frame with
a fresh gate. That is a retrigger of the arp envelope, which is audible as a
small swell rather than a click, and is why the arp gets a fast attack and full
sustain (§6) rather than anything with a decay in it. sound.md learned that one
the expensive way in its phases 6a and 6b: **a decaying envelope is just a
quieter sound**.

### The fade-in is a per-bar `$D418` ramp, and it has to compose with `V`

The arrangement opens with a volume ramp — `8, 9, 10, 11, 12, 13, 14, 15` across
bars 1 to 8, flat at 15 through the theme, then `12, 10` across the turnaround.
It is the one mechanism in the tune that reaches past note data into a global
register.

It works because `$D418`'s low nibble is the only volume control the chip has,
and here that limitation is an advantage rather than a problem: a fade that
applies to all three voices at once is exactly what a fade should be. §2 of
sound.md spends a page on why per-voice volume is unavailable and why modulating
sustain is not a substitute. None of that applies to a global fade.

**The problem is that `sound_volume` also lands in that nibble.** sound.md
already puts `{0, 7, 15}` there for the `V` key's off / low / full steps, and
the tune has to respect that setting — a player who turned the sound off does
not expect the menu to be the exception. Two things must not happen: the fade
must not override a player who asked for quiet, and step 0 must remain
*exactly* silent so that "is this silent" stays a single property of the
register set.

The obvious `min(ramp, kMasterVolume[sound_volume])` is wrong. At the low step
it gives `4, 6, 7, 7, 7, 7, 7, 7` — the fade flattens after two bars and the
opening loses the shape the arrangement was written around.

**Use a 3 × 16 lookup table**, indexed by `sound_volume` and then by the ramp
value, 48 bytes of `const`:

- row 0 is sixteen zeros, so the off step is silent by construction and needs
  no predicate of its own;
- row 2 is the identity, so at full volume the ramp is exactly as composed;
- row 1 is the ramp scaled to a 7 ceiling, so the *shape* survives the low step
  rather than being clipped by it.

No multiply, no division, one indexed load per bar. This is the same argument
sound.md §3 makes for folding the volume key into a predicate rather than giving
it a silencing path of its own: a mechanism every future voice would have to
remember to consult is a mechanism that will eventually be forgotten.

**The ramp floors at 8, not 0, and the tail matters more than the head.** A
fade from silence is the obvious opening and it is wrong here, because the
opening is heard once and the *loop point* is heard forever. Bar 16 ends the
turnaround at full volume and bar 1 restarts the build; without a glide that is
a 12-step drop every 30 seconds, which reads as the tune breaking rather than
turning over. So the ramp descends `12, 10` across bars 15 to 16 and picks up at
8 in bar 1 — a step of 2 at the seam. The cost is a shallower fade on first
play, and it is the right trade: one listener hears the opening once, and the
same listener hears the seam repeatedly. Restoring a deeper first-play fade is
one entry in `VOL_MAP`.

**The ramp is not currently exported.** `lib/music.py` has `VOL_MAP` and the
reference page has `volMap`, but `musicdef.h` declares no `kMusicVolMap` and
`musicdef.cc` emits none. Phase 1 shipped without it. Anything built against the
generated data today would play at full volume throughout — no opening fade, and
a hard edge at the loop point — see §8.

### Hard restart, but only where a note actually changes

The SID's envelope generator does not reliably retrigger on a gate that goes low
and high within a few cycles, and a new note written over a still-releasing
envelope starts from wherever the old one was. The standard answer is the *hard
restart*: on the frame before a new note, clear the gate and write attack/decay
and sustain/release to `$00`, forcing the envelope counter down so the next
gate edge starts from silence.

Applied to every row that would cost a sixth of every note — at six frames per
row, one restart frame is 17% of a sixteenth. Applied only to rows where the
*next* row actually begins a new note, it costs almost nothing: this arrangement
moves in quarters, so the lead's restart frame is one in twenty-four. The bass
pays more, since its rhythm patterns drop to single rows in the push bars, and
that is where it matters most — a bass pickup whose envelope starts halfway up
is a note with no front edge, on the one voice whose front edge is the rhythm.
**Keep it there too.** A note that starts one frame late is still on the beat.

Held notes and the arpeggio never pay it, and the arpeggio never needs it,
because it does not retrigger at all.

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
way to make an atmospheric intro build. Two substitutes carry that weight
instead, and both are chip-independent: the **pulse width sweep on the lead**
(§6), and the **volume ramp** above, which is doing exactly the job a filter
opening up would have done and is the reason the arrangement can afford not to
have one.

### The loop must be identical in what you can *hear*, not in all 25 registers

The earlier version of this plan asserted that frames *N* and *N + total* must
be byte-identical across all 25 registers, and used it as the main structural
test. On the rock arrangement that holds. **On the atmospheric one it does not,
and the tune is still correct.**

Measured: 1,918 differing bytes between the first and second pass, every one of
them on voice 1, and **zero** of them on a voice whose gate was set. The cause
is the arrangement, not the player — the lead is silent for bars 1 to 4, so
voice 1's frequency and envelope registers still hold the last note of bar 16
while its gate is clear. Inaudible, and unavoidable without writing registers
that have no reason to be written.

So the invariant is restated:

> At the loop point, every register of every **gated** voice matches, and so do
> the four global registers.

That is the property that means the loop is seamless, and it is strictly the one
worth testing. The alternative — having the player zero a silent voice's
frequency so the strict version holds — buys a tidier test by adding stores that
exist only for the test's benefit, which is the wrong trade.

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
50th of a second in the tempo, once, on a keypress.

**RAM.** Measured from the `musicdef.cc` the generator actually emits, not
estimated:

| Item                                  |    Bytes |
| ------------------------------------- | -------: |
| `kMusicLeadStart[256]`                |      256 |
| `kMusicLeadOn[256]`                   |      256 |
| `kMusicBassStart[256]`                |      256 |
| `kMusicBassOn[256]`                   |      256 |
| `kMusicDrumAt[256]`                   |      256 |
| `kMusicChords[16]` (root + triad)     |       64 |
| Six instruments × 8                   |       48 |
| `kMusicNoteTable[12]`                 |       24 |
| **Data, as generated**                | **1,416** |
| Volume ramp + composition table (§3)  |    16+48 |
| Player code                           |     ~900 |
| Player state (bss)                    |      ~28 |
| **Total**                             | **~2.4 KB** |

Against `a178 - d000`, **11.6 KB free** in `ppilot.prg`. The tune is about a
fifth of it.

**This is the number that set the bar count.** At 32 bars the same table was
2,760 bytes of data and ~3.8 KB all in — a third of the heap for the menu
screen's background music, which is the wrong share for a feature nothing else
depends on. The arrangement lost its climax section to get here (§5) and that is
a real loss, honestly recorded; what it bought is a tune that costs about as
much as the instrument panel's Koala image.

**The flat tables are 1,280 of those 1,416 bytes, and half of that is air.**
Phase 1 chose per-row arrays over the pattern-plus-order encoding the original
plan specified. That buys a player where every channel is one indexed load per
row and no pattern pointer exists to get wrong, which is a real simplification.
What it costs, measured over the 16 bars:

| Table                  | Unique bars |  Flat | As patterns + order |
| ---------------------- | ----------: | ----: | ------------------: |
| `kMusicLeadStart`      |       12/16 |   256 |                 208 |
| `kMusicLeadOn`         |        3/16 |   256 |                  64 |
| `kMusicBassStart`      |       11/16 |   256 |                 192 |
| `kMusicBassOn`         |        1/16 |   256 |                  32 |
| `kMusicDrumAt`         |        5/16 |   256 |                  96 |
| **Total**              |             | **1,280** |           **592** |

**54% of that data is redundant**, and two entries are worse than redundant.
`kMusicBassOn` is 256 bytes every one of which is `1` — it encodes a constant.
`kMusicLeadOn` holds only 0 and 1, so a bitfield would be 32 bytes rather than
256. Dropping the first and packing the second is 480 bytes for almost no work
and no change to the player's shape — **and it is worth doing before trading
away any more of the arrangement.** 480 bytes is most of a bar-count step.

**`ppilot.prg` only.** The build already splits on `__ENABLE_SOUND__` for
`ppilot` and `__ENABLE_DEBUG__` for `ppilotd`, and `PPILOTD_SRC` is literally
`$(PPILOT_SRC)` — the two binaries differ by a define, not a file list. So the
music guard is the flag that already exists: `music.cc` and the body of
`musicdef.cc` sit inside `#ifdef __ENABLE_SOUND__`, and `menu_run()`'s calls
compile to nothing in the debug build.

Wrapping `musicdef.cc` as well as the player is belt-and-braces rather than
strictly needed — oscar64's linker drops unreferenced symbols, as sound.md §10
records it doing for `mul16` — but 1.4 KB is a lot to leave resting on that, and
the debug build is the one with less headroom (`a360 - d000`, 11.2 KB).

**Verification.** Unlike sound.md there is no `@stack` gate to check — the
player is main-line and a frame is fine. What must be checked after each build
is `check_zeropage.py` passing on all five programs, both `.prg` files fitting
under `$D000`, and **`ppilotd.map` containing no `kMusic*` symbol at all** —
that last one is the guard against the `#ifdef` quietly not covering the data.

**Zero page: zero bytes requested.** Stated as a budget line because it is the
scarcest resource in the program — 156 bytes, 100% allocated, 5 bytes of
headroom before the compiler's spill area collides with it. Space can be freed
if the player ever earns it (§2); the line is here so that spending it is a
decision someone makes rather than one that happens.

---

## 5. Where the tune comes from

`lib/music.py` is the source of truth. `tools/generate_music.py` reads it and
emits both consumers:

```
lib/music.py                  the arrangement: chords, melody, patterns, tempo
  │
  ├── tools/generate_music.py ──► c64o/musicdef.cc, c64o/musicdef.h
  └── tools/generate_music.py ──► the TUNES block in sid-intro-theme.html
```

This is the pattern the repository already uses everywhere else —
`lib/chardefs.py` → `c64o/chardefs.cc`, `lib/spritedef.py` → `c64o/spritedef.cc`
— and it is used here for the reason it exists: a browser reference and a C64
tune that are edited separately diverge on the first bugfix, and then the
reference is worse than nothing because it is still convincing.

`make music` regenerates both and joins `make data`. The exporter validates
before it writes: bar row sums, note range, chord table length, order indices.

**The known gaps between the three copies**, all of which belong to phase 1 and
none of which the current `tests/test_music.py` catches:

- `VOL_MAP` reaches the reference page and does not reach `musicdef.h`, so the
  fade in §3 exists in two of the three places it has to (§8).
- `lib/music.py` holds both arrangements and only `TUNES[0]` is emitted to C.
  That is correct — the C64 ships one tune — but it means the exporter has a
  silent selection step, and nothing asserts which tune it selected.
- Whether a tune has the four-bar pedal opening is now an explicit `soft_intro`
  flag on the tune dict. It used to be inferred from `bars == 32`, which broke
  the moment the atmospheric arrangement became 16 bars and collided with the
  rock one. Inferring arrangement structure from a length is the class of bug
  worth naming: it works until two things are the same size.

A fourth gap was in the reference page rather than the data. The tune selector's
`<option>` labels were written out in the markup and had drifted — the labels
were in the opposite order to `TUNES`, so choosing the rock intro played the
atmospheric theme. They are now built from `TUNES` at load, which is the same
argument as the rest of this section: two hand-maintained copies of one fact
diverge, and the convincing one is the one that lies.

### The 16-bar arrangement

Sixteen bars in D minor, 125 BPM, six frames per row, sixteen rows per bar.
MIDI 29 to 82 (F1 to A♯5), inside the note table's 28–83.

- **Bars 1–4, soft intro build.** `Dm / A♯ / C / Am`, one pedal bass note per
  bar under the arpeggio. Lead and drums silent; the volume ramp starts at 8.
- **Bars 5–8, motif entry.** The low lead motif arrives (`D4`, `F4`, `A4`) and
  hi-hats enter. The ramp reaches full at bar 8, so the fade completing and the
  kit arriving land together.
- **Bars 9–14, main theme.** `Dm / F / C / Gm / Dm / A♯`, full rhythm section,
  the melody climbing across the six bars so that what follows is a peak rather
  than an afterthought.
- **Bars 15–16, lift turnaround.** `Gm → A7`, and the highest notes in the tune.

### Why the outro was deleted rather than shortened

The 32-bar version ended with eight bars of outro: four of descending quarter
notes through `Gm → C7 → F → A♯`, then a rising scalar line into a
sixteenth-note cadential figure over `A7` — `E5 F5 G5 F5 E5 C♯5` — resolving
`C♯ → D`. Four things were wrong with it and only one was length.

- All four descent bars had identical rhythm and identical shape, and the theme
  before them was also in quarters, so there was nothing to contrast against.
- `Gm → C7 → F` walks out to the relative major and then has to climb back,
  which is a lot of ground for a menu tune to cover.
- The cadential figure is a Baroque ornament. It is a different genre from
  everything before it.
- `C♯ → D` is a V7–i with the leading tone in the melody: the most *finished*
  gesture in tonal music, at the one point in the piece where nothing should
  sound finished.

The fix needed no new harmony, because the turnaround was already there. Bars 23
and 24 of the old arrangement were `Gm` and `A7` — iv and V7 in D minor,
resolving onto bar 1's `Dm`. Cutting after them left a working loop point with
zero recomposition; **only the melody over those two bars is new**:

```
Bar 15 (Gm):  D5   F5   G5   A#5      opens upward
Bar 16 (A7):  A5   G5   F5   E5       peak, then down onto the 5th
```

It climbs where the old one fell, and it **never plays C♯**. With the leading
tone withheld the `A7` stops announcing itself as a dominant, so the return to
`Dm` reads as the phrase carrying on rather than starting again. `F5` over `A7`
is the flat 13th, which is also the third of D minor — it keeps the home key in
earshot right through the turn. And because these are the two highest bars in
the tune, the loop point is now the moment the melody is most worth hearing.

The volume glide in §3 lands under exactly these two bars, so the tune reaches
its melodic peak while getting quieter — which is what makes it turn over
instead of stop.

### The note table

Twelve 16-bit entries for octave 6, shifted right for every octave below it,
since a SID frequency halves exactly per octave. Unchanged from the 16-bar
version; the tune changed, the chip did not.

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

The truncation a right shift introduces is bounded and small. Over MIDI 28 to 83
the **worst error is 0.090%**, at E1, where five shifts leave 701 against an
exact 701.6. A semitone is 5.95% and pitch error becomes audible somewhere
around 0.3%, so a 60-entry table would buy nothing but 96 bytes of data.

NTSC is 3.7% flat on every note, uniformly — the whole tune is in a slightly
different key, in tune with itself. That is the same trade §2 accepts on tempo
and it needs no second table.

---

## 6. Sound design

**Voice 1, lead.** Pulse, with the width swept independently of the note so a
held note keeps moving. Fast attack, short decay, high sustain — the melody is
in quarters but the arrangement is sparse enough that a slow attack would be
audible as sloppiness rather than warmth. Pulse rather than saw because the
sweep is one of only two pieces of timbral movement the design allows itself
(§3, on the filter).

The sweep is a triangle over an 0x800 range at **8 units per frame**, so it
cycles every 256 frames and divides the 1,536-frame loop exactly six times. The
step is chosen for that rather than for its rate: a step that did not divide the
loop would leave the pulse width somewhere different on every pass. It divides
the rock arrangement's 1,280 frames as well, which is why one constant serves
both — and it is the reason §3's bar count has to be a multiple of 8.

**Voice 2, bass.** Pulse, narrow, with a decay into a moderate sustain so notes
have a front edge. For the first four bars it is a single pedal note per bar and
that front edge is the only rhythmic information in the piece; from bar 5 it
moves to eighths. This is the voice the hard restart in §3 was kept for.

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
volume only, and here it is spoken for by the fade in §3.

---

## 7. Testing

Two suites, split by what they can actually see.

**`tests/test_music.py`** (pytest, alongside `test_chardefs.py`) tests
`lib/music.py` and the exporter — the data, before it is a program. Present
today: bar row sums, MIDI bounds, chord table layout, flattened table lengths,
code generation, and entry-for-entry agreement between the HTML and the C
output. To add:

- `VOL_MAP` has one entry per bar, every entry in 0–15, and it **round-trips to
  `musicdef.cc`** — the check that would have caught §3's missing export
- the exporter emits the tune it was asked for, asserted by name rather than by
  the shape of what came out
- every bar of the chord table has a triad, and every lead and bass note lies
  within the triad's key
- **`bars × 16 × speed` is a multiple of 256** — the PWM divisor constraint from
  §3, which is cheap to assert on the data and expensive to discover by ear
- the last bar's `VOL_MAP` entry is within a few steps of the first, so the loop
  seam cannot silently reopen
- `soft_intro` tunes have a silent lead and no drums for `SOFT_INTRO_BARS`, and
  hats only up to `SOFT_HAT_BARS` — the structure that used to be inferred from
  the bar count and is now a flag

**`c64o/test/music_test.cc`** (g++, alongside `sound_test.cc`) links `music.cc`
and `musicdef.cc` against the host `sid.h` and tests the *player*:

- the note table is monotonic and each entry within 0.2% of equal temperament
- **loop identity over 1,536 frames, on gated voices only** (§3) — the strict
  all-25-registers form is wrong for this arrangement and would fail on a
  correct player
- voice 3 arbitration: a drum hit takes the voice for exactly its frame count,
  the arpeggio does not write the voice while a hit is live, and it resumes with
  a gate edge on the frame after
- the arpeggio advances one tone per frame and **never** clears its gate except
  when stolen or hard-restarted — the shimmer collapses into a machine gun if it
  retriggers
- hard restart happens on the frame before a new note and **only** there
- the volume ramp reaches `$D418` bar by bar, and composing it with
  `sound_volume == 1` preserves the ramp's shape rather than clipping it flat
- `sound_volume == 0` produces master volume zero at every one of the 1,536
  frames, ramp or no ramp
- `music_stop()` leaves all three gates clear and `$D418` at zero
- `music_tick()` with `music_playing` clear writes nothing at all — the guard
  that keeps the in-flight help screen silent

Mutations that must each fail a test individually: the loop length off by one
row, the arpeggio retriggering per tone, hard restart applied to every row, hard
restart removed, drums not stealing voice 3, a drum never releasing it, the note
table made linear, the octave shift inverted, the volume ramp ignored, the ramp
applied *instead of* `sound_volume` rather than through it, `music_stop()`
leaving the master volume up, and the `music_playing` guard removed.

Every assertion in the second list has been run against the reference
implementation, and it has now found four things: the loop-identity invariant
was wrong as written (§3); the reference's BPM readout was double the real
tempo; the volume ramp never reaches C; and the tune selector's labels were in
the opposite order to the tunes they selected. Not one was audible as a wrong
note. That is the argument for writing `music_test.cc` early rather than in
phase 7 — sound.md's phase 7 note records the same thing about `sound_test.cc`,
which landed in phase 2.

**What neither suite can see** is the thing sound.md's phases 3a, 6a and 6b were
all eventually caught by: whether it sounds right. A player that passes every
assertion above can still be inaudible, deafening, or in the wrong key on a real
chip. That is phase 8 and it is a real phase.

---

## 8. Phases

Each phase leaves the program in a working, committable state.

0. **Reference implementation.** ✅ Done. `sid-intro-theme.html` — the player,
   the register shadow and both arrangements, on a 50 Hz tick, with §7's checks
   passing. Everything below is a port of something that can be listened to
   rather than a design being discovered in 6502.
1. **Data and exporter.** ✅ Done, with gaps. `lib/music.py`,
   `tools/generate_music.py`, `c64o/musicdef.{cc,h}`, `tests/test_music.py`, the
   `make music` target and its entry in `make data`.

   **1a — the arrangement was cut to 16 bars.** ✅ Done. The eight-bar cadential
   outro went first, for the musical reasons in §5; the climax section went with
   the second cut, for the RAM reasons in §4. Bars 15–16 carry a new two-bar
   lift turnaround, and `VOL_MAP` gained the glide that lands under it. Data is
   1,416 bytes, down from 2,760. Verified: loop clean on gated voices, hard
   restart on every new note and only there, drums stealing voice 3 for exactly
   their frame counts, MIDI 29–82, and `C♯` absent from the turnaround.

   **1b — the volume ramp.** ⬜ `VOL_MAP` is in the Python source and the
   reference page but not in `musicdef.h`. Until it is exported the C64 build
   has neither the opening fade nor the glide into the loop. Add
   `kMusicVolMap[16]` plus the 3 × 16 composition table from §3, and the
   round-trip test from §7 that stops it happening again.

   **1c — the data is 54% redundant.** ⬜ Measured in §4. `kMusicBassOn` encodes
   a constant in 256 bytes; `kMusicLeadOn` is a bitfield written as bytes.
   Together 480 bytes, which is most of a bar-count step — worth taking before
   the arrangement is asked to give up anything else.
2. **Player skeleton and ownership.** ⬜ `music.{h,cc}` with `music_start()`,
   `music_stop()`, `music_tick()` and the `music_playing` guard; the tick in
   `menu_run()`'s loop at `menu.cc:149` and `help_run()`'s at `help.cc:59`;
   start and stop bracketing `menu_run()`. All of it inside
   `#ifdef __ENABLE_SOUND__`, and `musicdef.cc`'s body too (§4). Voice 1 only.

   First audible phase, and the phase with two things to check that are not
   about music: pressing `H` in flight must stay silent, and `ppilotd.map` must
   contain no `kMusic*` symbol.
3. **Bass.** Voice 2. The pedal-bass opening is the easiest thing in the
   arrangement to get audibly wrong, because for four bars it is one note.
4. **Arpeggio.** Voice 3, one tone per frame, gate held. It carries bars 1–4
   alone, so this is the phase where the opening either works or does not.
5. **Drums.** Voice 3 stealing, the priority countdown, the hand-back.
6. **Hard restart, the volume ramp, and envelope tuning.** Ordered deliberately
   after the drums: both interact with every voice and are easier to get right
   against an arrangement that already exists. `V` in the menu lands here or is
   dropped (§9).
7. **Tests.** `music_test.cc` per §7, and the wiring in `c64o/test/Makefile`
   next to `sound_test`.
8. **Verification.** VICE, PAL and NTSC, 6581 and 8580. Everything in §9 is
   queued behind it, because all of it is a judgement by ear.

The ordering that matters is phase 2 before everything else: the ownership guard
is the only part of this module that can break something outside it, and it is
worth having in place and proven before there is a tune interesting enough to be
distracting.

---

## 9. Open questions

**Which tune ships?** The reference carries both, now at the same length. The
atmospheric theme is the default and this plan is written for it, but that is a
judgement made on paper. The rock intro is more immediate; the atmospheric one
has an opening that earns its 30 seconds. Decide by ear in phase 8, with the
switcher, and note that the decision costs one constant in `lib/music.py`.

**~~Is 61 seconds too long for a menu?~~ Resolved by cutting to 16 bars**, for
RAM rather than for pacing — but the pacing argument was real and the cut
answers it too. At 30.7 seconds the whole tune fits inside a plausible visit to
the mission list, the fade completes at about 12 s rather than 15, and the
melodic peak now arrives at 28 s instead of 40. The thing that was actually
given up is the climax section, and whether the tune misses it is the first
question for phase 8.

**Does the tune restart or resume across the help screen?** It keeps playing —
help never stops it (§3). But `help_run()` is entered with a
`screen_begin_text_page()` that `memset`s the whole screen, so the frame it
happens on is long. Whether that is one dropped tick or four is not known until
it runs.

**Is the lead loud enough over the arpeggio?** With no per-voice volume this is
decided entirely by envelope and waveform, and it is the first thing that will
be wrong. The lever is the arpeggio's sustain, not the lead's — sound.md §10
makes the same point about the stall warning, and its conclusion, that pitch
placement works where volume does not, applies here too.

**Does the tune miss its climax?** The 32-bar version had eight bars of
sixteenth-note runs and high octaves between the theme and the turnaround, and
they are gone. The two-bar lift is now doing that job in a quarter of the space.
If it sounds like the tune arrives at its peak without having climbed to it, the
cheap answer is not more bars — it is making bars 13–14 push harder, which costs
nothing. The expensive answer is 24 bars, which §3's table says is legal and §4
says costs another ~680 bytes.

**Is the opening too long now?** Eight of sixteen bars are build: four with no
lead or drums at all, four with hats only. That was a reasonable proportion of
32 bars and it is half the tune at 16. It may be exactly what gives the piece
its character, or it may mean a player hears a minute of the menu and remembers
only pedal tones. Shortening the pedal section to two bars is one edit to
`SOFT_INTRO_BARS`. Ear decision.

**Should `V` work in the menu?** §3 makes the tune respect `sound_volume`;
letting the player *change* it there is separate. The blocker is that
`sound_cycle_volume()`'s confirmation message goes through `msg.cc`, which
writes into the flight viewport and has nowhere to put a line on a text page.
Cheapest answer is probably a fixed row on the menu screen rather than teaching
`msg.cc` a second output.

**Does anything want a second tune?** A short jingle on mission completion is
the obvious candidate and would reuse the player unchanged, but it would play
during flight, where the flight driver owns the SID and §3's ownership rule says
only one of them can. That is a real design question and not a small one. Out of
scope, noted so the answer is not accidentally foreclosed.
