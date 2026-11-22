# Title Screen Music (`music.md`)

Shipped in both **`ppilot.prg`** and **`fpilot.prg`** behind `-D__ENABLE_SOUND__` (`make -C c64o SOUND=` disables it). Plays a 24-bar D-minor chiptune (`tune2` from [lib/music.py](../lib/music.py): 384 rows, speed 6 = 125 BPM PAL, 2,304 frames = 46.08 s PAL / 38.40 s NTSC) on the main menu and menu-opened help screen.

`make music` runs `tools/generate_music.py`, generating `c64o/musicdef.{h,cc}` and the standalone WebAudio preview [docs/sid-intro-theme.html](sid-intro-theme.html).

---

## 1. Scope

| Property | Value |
| --- | --- |
| Target binaries | `ppilot.prg` and `fpilot.prg` (`#ifdef __ENABLE_SOUND__`); omitted from demos (`polydemo`, `vecdemo`, `vectest`, `planedemo`) |
| Active screens | Main menu (`menu.cc`) and help opened from menu (`help.cc`); silent during flight, map, debrief, and in-flight help |
| Volume control | Shares `sound_volume` (`V` key cycles `2 → 1 → 0 → 2`), composed with the per-bar volume ramp |
| Source of truth | `lib/music.py` (`TUNES[0]` ships; 4 tunes in catalogue: `tune2`, `rock`, `wide`, `tune1`) |

---

## 2. Relationship to `sound.md`

- **Mutually exclusive SID owner:** Flight raster IRQs are off during menu and help, so `music_tick()` polls vsync (`gfx_wait_vsync()`) at 50 Hz PAL (60 Hz NTSC, ~20% faster, accepted) with no interrupt handler.
- **Compiled-in tables:** Tune data is packed into `musicdef.cc` rather than loaded as a relocated `.sid` binary.
- **Zero page:** `music.cc` uses no `#pragma` ZP reservations of its own; oscar64 allocates **10 B ZP** in `ppilot.map` (5 B ZP + 5 B BSS in `fpilot.map`).

---

## 3. Architecture & invariants

### Ownership & screen handoff

- `music_playing` is owned exclusively by `menu_run()`: `music_start()` runs after `_menu_enter()` (`gfx_stop_raster_irqs()`), and `music_stop()` runs before entering flight.
- Both `menu_run()` and `help_run()` tick music via `screen_notice_frame()` (`screen.cc`), which calls `music_tick()` then `gfx_wait_vsync()`. When help is opened from flight, `music_playing` is `false` so `music_tick()` returns immediately.
- `sound_silence()` returns before writing SID registers while `music_playing` is true, so entering/leaving help from the menu never silences the tune.

### Tempo, bar count & voice-3 stealing

- `BPM = 750 / speed` at 50 Hz PAL (`speed = 6` → 125 BPM, 96 frames/bar, 2,304 frames/loop).
- **Bar count invariant:** Must be a multiple of 8 so `bars * 16 * speed` divides evenly by the 256-frame lead PWM cycle (`2304 / 256 = 9`).
- **Three fixed voices:**
  - **Voice 1:** Lead pulse (PWM sweep).
  - **Voice 2:** Bass pulse (continuous, never rests: `MUSIC_BASS_ON(row) == 1`).
  - **Voice 3:** Sawtooth chord arpeggio (1 triad tone/frame, gate held) interrupted by noise drums (`kMusicDrumIns[3]`; 568/2,304 frames raw, 1,814/2,304 gated frames with restart windows).

### Volume composition (`music_master_volume`)

Per-bar volume ramp `kMusicVolMap[24]` (`11, 12, 13, 14`, 18×`15`, `13, 11`) composes with `sound_volume` (`0/1/2`) via branch arithmetic in `music_master_volume(bar_vol, setting)`: `0` when off, `bar_vol >> 1` when low (ceiling 7), `bar_vol` when full (no 48 B lookup table).

### Hardware invariants

1. **Write-only SID rule:** `music.cc` never reads `SID_REGS`; it maintains a 3-byte `_ctrl[3]` shadow (enforced by `tests/test_music.py::test_player_never_reads_a_sid_register`).
2. **Filter neutralisation:** `music_start()` explicitly zeroes `$D415–$D417` (filter unused for 6581/8580 consistency).
3. **Hard restart & loop identity:**
   - Lead and bass drop gate + ADSR on the last frame of any row preceding a new note.
   - Voice 3 inserts `kMusicV3RestartFrames = 1` frame of gate-low + zeroed ADSR before any drum hit and after any hit ends.
   - `kV3LoopRestartFrames = 4` frames run at both `music_start()` and the bar-24 → bar-1 wrap, making pass 1 and every subsequent loop register-identical on gated voices and player globals.

---

## 4. Budgets & packing format (current `ppilot.map` / `fpilot.map`, pass 152)

### Packed `musicdef.cc` tables (380 B total data)

`tools/generate_music.py` packs all 24 bars (384 rows) using deduplicated bar pools and a single shared lane-index table:

| Symbol | Bytes | Encoding |
| --- | ---: | --- |
| `kMusicNoteTable[12]` | 24 B | `__striped` octave-6 PAL frequencies (`$459C–$8368`), right-shifted by octave |
| `kMusicLeadStartPool[133]` + `kMusicLeadStartBar[24]` | 157 B | Even rows only (odd rows = 0, asserted); 20 unique 8-byte bar patterns greedily overlapped |
| `kMusicBassStepPool[48]` | 48 B | Interval above chord root + 1 (`0` = no new note); 3 unique 16-byte rhythm shapes (`MUSIC_BASS_STEP_IN` / `MUSIC_BASS_NOTE`) |
| `kMusicChordPat[7]` + `kMusicChordBar[24]` | 52 B | 7 distinct `{root, triad[3]}` patterns + 24 bar indices (`MUSIC_CHORD(bar)`) |
| `kMusicDrumBitsPool[20]` | 20 B | 2 bits/row (`0=none, 1=kick, 2=snare, 3=hat`), 5 unique 4-byte bars |
| `kMusicLeadOnBitsPool[6]` | 6 B | 1 bit/row LSB-first, 3 unique 2-byte bars |
| `kMusicBarLanes[24]` | 24 B | Packed offsets per bar: `bits 0–1` lead gate (`<< 1`), `bits 2–4` drum (`& 0x1C`), `bits 5–6` bass (`>> 1`) |
| `kMusicVolMap[24]` | 24 B | Per-bar master volume (`11..15..11`) |
| `kMusicDrumIns[3]` | 24 B | Kick, snare, hat descriptors (`kMusicInsLead/Bass/Arp` constant-folded into code by oscar64) |
| `music_playing` | 1 B | Ownership flag |
| `kMusicBassOn` | **0 B** | Deleted (`MUSIC_BASS_ON(row)` is constant `1`, asserted by generator & tests) |

### Full RAM & CPU footprint

| Segment | `ppilot.prg` | `fpilot.prg` | Contents |
| --- | ---: | ---: | --- |
| Code (`music.cc`) | 873 B | 898 B | `music_tick` (591/611), `music_note_freq` (55), `music_start` (54/59), `_drum_at` (38), helpers & `@proxy` (`music_stop` inlined) |
| Data (`musicdef.cc`) | 380 B | 380 B | Packed tables above |
| BSS | 4 B | 9 B | `_music_pwm_phase`, `_ctrl[3]` (+ 5 bytes overflowed from ZP in `fpilot`) |
| ZP | 10 B | 5 B | Row/bar/frame counters, arp index, V3 owner/timer/freq/step |
| **Total** | **1,267 B** | **1,292 B** | Worst-case frame: ~1,500–2,000 cycles (~8–10% of a PAL frame) |

---

## 5. Source of truth & arrangements

- **Pipeline:** Edit `lib/music.py` → run `make music` → regenerates `c64o/musicdef.{h,cc}` and `docs/sid-intro-theme.html`. `TUNES[0]` is compiled into the C64 binaries; all 4 tunes (`tune2`, `rock`, `wide`, `tune1`) are playable in the HTML preview. `musicdef.h` deliberately avoids `<stdbool.h>` for host C++/oscar64 compatibility.
- **Shipping arrangement (`tune2`, 24 bars, D minor):**
  - Bars 1–4: soft intro (`VOL_MAP` `11, 12, 13, 14`) establishing bass, arpeggio, and motif.
  - Bars 5–22: full volume (`15`) through A, B, lift, and climax sections.
  - Bars 23–24: turnaround fade (`13, 11`) looping seamlessly back to bar 1.
- **Octave-6 note table:** `kMusicNoteTable[12]` covers C6–B6 (`$459C–$8368`), right-shifted by `(6 - octave)` for MIDI 28–83 (worst frequency error **0.090%**).

---

## 6. Sound design

| Voice | Waveform | PW / Sweep | ADSR (`ad` / `sr`) | Details |
| --- | --- | --- | --- | --- |
| V1 Lead | Pulse | Triangle `$0300–$0500` (step 4/fr, 256-fr cycle) | `0x05` / `0xC5` | Hard-restarted on last frame before new note |
| V2 Bass | Pulse | Fixed `kMusicBassPw = 0x0500` | `0x06` / `0x84` | Continuous (`MUSIC_BASS_ON == 1`), hard-restarted on new note |
| V3 Arp | Sawtooth | — | `0x00` / `0xF3` | Chord triads shifted `+12` (`ARP_OCTAVE_SHIFT`), 1 tone/frame |
| V3 Kick | Noise | — | `0x00` / `0xF0` | 5 fr, start `$1000`, step `-665/fr` |
| V3 Snare | Noise | — | `0x00` / `0xF0` | 4 fr, flat `$3800` |
| V3 Hat | Noise | — | `0x00` / `0xF0` | 2 fr, flat `$5000` |

---

## 7. Testing

- **Python (`tests/test_music.py`, via `make test`):** Validates tune data, octave-6 pitch accuracy, packed table round-trip vs unpacked rows, bar-dedup savings, no `kMusicBassOn`, no `<stdbool.h>` in `musicdef.h`, and source scan confirming `music.cc` never reads `SID_REGS`.
- **Host C++ (`c64o/test/music_test.cc`, 19 tests via `make -C c64o test`):** Verifies `music_playing` guard, `music_start`/`music_stop`, `$D415–$D417` filter zeroing, note-frequency conversion, volume composition & ramp, hard-restart frames, arp/drum V3 stealing across all 1,814 gated reference frames, and exact loop identity across bar-24 → bar-1 wrap.
