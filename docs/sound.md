# Flight Sound Effects (`sound.md`)

Shipped in both **`ppilot.prg`** and **`fpilot.prg`** behind `-D__ENABLE_SOUND__` (`make -C c64o SOUND=` omits both sound effects and title music). Implementation lives in `c64o/sound.{h,cc}` and `c64o/sid.h`. See [music.md](music.md) for the menu title tune and [project.md](project.md) for overall architecture.

---

## 1. Scope

| Feature | Build | SID Voice | Trigger |
| --- | --- | --- | --- |
| Engine hum & PWM roughness | both | Voice 1 (Pulse) | Continuous while flying (`_sound_flying`) |
| Airspeed wind rush | both | Voice 2 (Noise) | Continuous while flying & `flight_speed >= 0x0080` |
| Crash roar | both | Voice 3 (Noise) | `FLIGHT_EV_CRASH` (`0x08`) |
| Touchdown tyre squeal | both | Voice 3 (Noise) | `FLIGHT_EV_TOUCHDOWN` (`0x01`) |
| Player aircraft hit crack | `fpilot` | Voice 3 (Noise) | `FLIGHT_EV_HIT` (`0x20`) |
| Stall warning warble | both | Voice 3 (Pulse) | Level: `flight_stall_warning()` |
| Player gun fire | `fpilot` | Voice 3 (Noise) | `FLIGHT_EV_GUN` (`0x10`) |
| Landing gear rumble | both | Voice 3 (Noise) | `FLIGHT_EV_GEAR` (`0x02`) |
| Flaps click/rumble | both | Voice 3 (Noise) | `FLIGHT_EV_FLAPS` (`0x04`) |
| Master volume (`V` key) | both | `$D418` | Cycles downward `2 → 1 → 0 → 2` (full / low / off) on every screen |

*(Enemy gun fire and enemy aircraft explosions do not have separate SFX.)*

---

## 2. Hardware & engine constraints

1. **Zero-page & stack-free IRQ blit:** `sound_blit()` runs inside `_gfx_switch_to_terrain` at raster 252, so it must allocate **0 B** of `@stack` and touch no oscar64 runtime zero page (`< $60`, enforced by `tools/check_irq_zp.py`).
2. **Main-loop rate vs IRQ rate:** `sound_update()` runs once per rendered frame (~6–10 Hz on stock C64, 50 Hz on SuperCPU), while `sound_blit()` fires every PAL/NTSC frame (50/60 Hz).
3. **Mutually exclusive SID ownership:** Non-flight screens (menu, help, map) stop raster IRQs via `gfx_stop_raster_irqs()` (which calls `sound_silence()` before `sei`). Title music owns the SID whenever `music_playing` is true ([music.md](music.md) §3).
4. **No per-voice volume & no SID filter:** The SID has only one global 4-bit volume (`$D418` low nibble); voice balance comes from ADSR sustain levels. The analog filter (`$D415–$D417`) is kept at 0 so 6581 and 8580 chips sound consistent.

---

## 3. Architecture & invariants

### Main-line compute → shadow → IRQ blit

- **`sound_update()`** (`sim.cc`; 6502 assembly in `sound_asm.cc` on C64, matching pure C counterpart in `sound.cc` on host) computes all 3 voices, writes `sound_shadow[25]` (on host through volatile `_sound_poke()` to feed the test interleaving observer), and calls `sound_mark_dirty()`.
- **Torn-read safety:** `sound_update()` never runs a blanking `memset` over `sound_shadow[]` and always writes each voice's **`Ctrl` register last** — an IRQ landing mid-update never latches a stale sustain or gate-off glitch.
- **`sound_blit()`** (inlined in `gfx.cc:_gfx_switch_to_terrain` at raster 252):
  - Clean tick (`!sound_dirty`): 1 load + branch (**6 cy** `ppilot`, **7 cy** `fpilot`).
  - Dirty tick (once per rendered frame): clears `sound_dirty`; if `sound_gen != sound_gen_seen`, updates `sound_gen_seen` and forces V3 gate low (`sid.voices[2].ctrl = sound_shadow[18] & ~SID_CTRL_GATE`) to guarantee a fresh attack; then loops `i = 6..0` copying V1 (`$D400+i`), V2 (`$D407+i`), V3 (`$D40E+i`), and `$D418` (**22 registers** total, skipping unused filter regs `$D415–$D417`). Costs **~257–269 cycles** once per frame.

### Silence predicates & `V` key

- `sound_silence()` always zeroes `sound_shadow[25]`; if `music_playing` is true it returns immediately (preserving menu↔help music continuity), otherwise it writes 0 to all 25 SID registers via `_sound_write_through()`.
- Derived predicates inside `sound_update()`:
  - `_sound_driver_live()`: `sound_volume != 0 && !flight_paused` (gates everything, including crash).
  - `_sound_flying(live)`: `live && !flight_crashed() && flight_fuel != 0` (gates V1 engine & V2 wind).
  - `$D418` master volume: `(flying || effect != V3_NONE) ? kMasterVolume[sound_volume] : 0`, where `kMasterVolume[3] = {0, 7, 15}`.
- `sound_volume` defaults to `2` (full), survives `sound_init()`, and `sound_cycle_volume()` cycles **downward** `2 → 1 → 0 → 2` (`"SOUND FULL"`, `"SOUND LOW"`, `"SOUND OFF"`).

---

## 4. Budgets (current `ppilot.map` / `fpilot.map`, pass 161)

| Segment | `ppilot.prg` | `fpilot.prg` | Contents |
| --- | ---: | ---: | --- |
| Code (`sound.cc`, `sound_asm.cc`) | 571 B | 585 B | `sound_update` (532/546), `sound_silence` (24), `sound_cycle_volume` (15) |
| Inlined `sound_blit()` | 51 B | 56 B | Inside `_gfx_switch_to_terrain` |
| Data + strings | 170 B | 170 B | `kSoundEngineFreqLo/Hi` (50), `kSoundWindFreqLo/Hi` (32), `kSoundV3*` tables (47), labels/strings (37), `kMasterVolume` (3), `sound_volume` (1) |
| BSS | 29 B | 35 B | `sound_shadow[25]` in `bss2` (+ state bytes in main BSS) |
| ZP | 8 B | 2 B | `_sound_v3_effect`, `_sound_v3_frames` (+ remaining state bytes in `ppilot`) |
| `@stack` | **0 B** | **0 B** | Zero software stack frames (`sound_blit` has none) |
| **Total** | **829 B** | **848 B** | ZP headroom: **11 B** (`ppilot`, `$54`), **10 B** (`fpilot`, `$55`) |

---

## 5. Flight model interface

`sound_update()` reads flight state without locks or races:
- Level inputs: `flight_throttle`, `flight_speed`, `flight_fuel`, `flight_paused`, `flight_crashed()`, and `flight_stall_warning()` (returns `flight_stall` in arcade, `flight_stall_warn` under `__FLIGHT_AOA__`).
- One-shot event handoff (`flight.h`, `sim.cc`):
  - Six event bits: `FLIGHT_EV_TOUCHDOWN` (`0x01`), `FLIGHT_EV_GEAR` (`0x02`), `FLIGHT_EV_FLAPS` (`0x04`), `FLIGHT_EV_CRASH` (`0x08`), `FLIGHT_EV_GUN` (`0x10`), `FLIGHT_EV_HIT` (`0x20`).
  - `flight_advance()` publishes `model_pending_events` into `flight_events` and increments `flight_gen`; under `T` time compression, `sim_run()` ORs events across all substeps into `flight_events`.
  - `sound_update()` compares `flight_gen` against `_sound_flight_gen_seen` so each batch of events is consumed once; starting a new V3 effect increments `sound_gen` so `sound_blit()` pulses V3 gate low before writing the new ADSR/frequency.

---

## 6. Sound design

### Voice 1 — Engine (Pulse, while `_sound_flying`)

- **Pitch:** 25-entry geometric PAL table `kSoundEngineFreqLo/Hi[25]` (`$0353`–`$06FC`, 50–105 Hz, 0.54 semitones/throttle step).
- **Roughness:** Proportional pitch jitter via `vec_mul8` on C64 (`vec_fastmul8p8` on host) with `amp = base >> 5` (`kSoundEngineJitterShift = 5`, ±3.1% peak), driven by an 8-bit Galois LFSR (`taps 0xB8`, `seed 0xA5`, 2 draws/frame).
- **PWM sweep:** Triangle wave over `$0400..$0BF0` (`_sound_pwm_phase` stepped by `6 + (r_pw & 3)`, plus `r_pw & 0x7F` jitter).
- **ADSR:** `AttDec = 0x10`, `SusRel = 0xF0` (fast attack, full sustain).

### Voice 2 — Wind (Noise, while `_sound_flying && flight_speed >= 0x0080`)

- **Brightness:** 16-entry geometric table `kSoundWindFreqLo/Hi[16]` indexed by `flight_speed >> 8` (`$0600`–`$1800`).
- **ADSR:** `AttDec = 0x60` (A=6 ~68 ms), `SusRel = 0xA6` (S=10, R=6 ~204 ms) — soft onset and tail below the engine.

### Voice 3 — Transients & alarms (table-driven via `kSoundV3*` + `_sound_pick_v3`)

**Priority order:** `crash > touchdown > hit > stall > gun > gear > flap`.

- **Preemption rules:**
  - `V3_CRASH` preempts everything and is the only sound that plays while `!flying` (dropped on `!driver_live`, expiry, or `!flight_crashed()` on `R` reset).
  - Both **`V3_TOUCHDOWN`** and **`V3_HIT`** preempt an active one-shot or stall (`_sound_pick_v3(events, 0, 2)`).
  - Other active one-shots run to completion against `stall`/`gun`/`gear`/`flap`; losing events are dropped, never queued.
  - Most V3 effects hold sustain (`S=15`) and release when their frame count expires; **`V3_GUN`** (`S=0`) and **`V3_HIT`** (`S=1`) are the two decaying combat exceptions.

| Effect | Bit | Wave | Freq (`$D40E–$D40F`) | PW | AttDec | SusRel | Frames | Notes |
| --- | --- | --- | --- | --- | --- | --- | ---: | --- |
| `V3_CRASH` | `0x08` | Noise | `$1000 → $0200` | `0` | `0x00` | `0xF9` | 16 | Linear sweep `-224/fr`; plays while `!flying` |
| `V3_TOUCHDOWN` | `0x01` | Noise | `$1800` | `0` | `0x00` | `0xF5` | 3 | Preempts stall & one-shots |
| `V3_HIT` | `0x20` | Noise | `$2400` | `0` | `0x26` | `0x10` | 2 | Decaying crack + low sustain tail; preempts stall & gun |
| `V3_STALL` | level | Pulse | `$3800` (~840 Hz) | `$0800` | `0x00` | `0xF3` | 2 on / 4 | Warbles via gate off on frames 2–3 |
| `V3_GUN` | `0x10` | Noise | `$0800` | `0` | `0x05` | `0x00` | 1 | Decays to 0 inside 1 frame; raised each firing frame |
| `V3_GEAR` | `0x02` | Noise | `$0900` | `0` | `0x00` | `0xF6` | 5 | Low mechanical rumble |
| `V3_FLAP` | `0x04` | Noise | `$0C00` | `0` | `0x00` | `0xF4` | 4 | Shorter, higher click/rumble |

---

## 7. Testing

`c64o/test/sound_test.cc` compiles `sound.cc` and `sound_asm.cc` natively with `g++` (stubbing the 9 flight/music globals and `sid.h`):
- Verifies silence predicates, `V`-key downward cycle (`2 → 1 → 0 → 2`), `music_playing` guard in `sound_silence()`, engine pitch/PWM/LFSR bounds, wind threshold/table, V3 priority and preemption (incl. gun, hit, and crash), and full `sound_shadow_observer` interleaving sweeps confirming Ctrl-last torn-read safety.
- Emulated 6510 equivalence (`c64o/test/target_test.cc` Test 30, in `target_planes_test.prg`): tests `sound_asm.cc`'s `sound_update()` against a pure-C reference across throttle/speed clamps, PWM triangle wrap, pause/volume/fuel silence, Voice 3 priority/preemption, stall warble, and crash sweep/abort.
- Target-side build verification (`c64o/Makefile`): `check_irq_zp.py` verifies zero runtime ZP in `_gfx_switch_to_terrain` / `sound_blit()`, and `check_mul_div.py` ensures `sound.cc`/`sound_asm.cc` link no 16-bit multiply/divide runtime.
