# What is worth trimming (`codesize.md`)

A measured survey of `c64o/ppilot.map` and `c64o/ppilot.asm`, taken on
**27 August 2026** against the source at that date, built with oscar64
`v1.32.272-117-ga7305f9`. The brief was narrow: find code size to give back
*without* paying for it in cycles, and look hardest at what the last two months
of commits added — clouds, the sprite stack, the map view, missions, messages,
the SID music and sound drivers, and the landing envelope.

Every number below was produced by building the variant and diffing the map.
Nothing here is an estimate. That matters, because the first pass at this
document *was* estimates, and when they were built three of the four largest
ones turned out to cost bytes rather than save them - §4 keeps them, written up
as dead ends, so nobody spends the afternoon again.

---

## 0. The metric is free RAM, not `.prg` size

`ppilot.prg`'s length is pinned by the top of the data segments, not by the
code. Shrink a function and the linker slides the data down; the file gets
shorter, but that is a side effect and it moves in steps rather than smoothly.
The number that actually matters is what `tools/analyze_ram.py` calls
**free, allocatable** - the run below `$D000` that anything new would have to
come out of.

```
make -C c64o && python3 tools/analyze_ram.py c64o/ppilot.map
```

Baseline, at `8676c11`:

| | |
| --- | ---: |
| code segment | 26,711 B |
| free, allocatable | 6,418 B |
| largest free run | 6,048 B at `$B860` |
| `ppilot.prg` | 44,702 B |

> **2026-08-29.** That baseline predates the merge of `ppilotd.prg` back into
> `ppilot.prg`, which put the debug view and the benchmark counters into the
> shipping binary for 1,792 bytes. Free, allocatable is **3,546 B** now, in a
> largest run of 3,208 B at `$C278` - so the headroom this survey was written
> against is roughly half of what it says. The findings below are unaffected;
> the urgency of acting on them is not.

---

## 1. Where the code is

Code bytes per source file, from the map's `objects by size` section, attributed
by definition site. The top of the list is also, almost exactly, the list of
things the last two months touched.

| file | code | largest symbols |
| --- | ---: | --- |
| `world.cc` | 2,964 | `world_render_grid` 958 · `_world_init_start_dx_dy` 653 · `world_update_roll_state` 476 |
| `poly.cc` | 2,942 | `_project_vertices` 1,341 · `_poly_scan_lines2` 526 · `_clip_2d` 519 |
| `flight.cc` | 2,772 | `flight_advance` 1,232 · `flight_input` 465 · `_flight_check_mission_waypoints` 362 |
| `vec.cc` | 1,856 | `vec_transform_inv` 351 · `vec_normalize` 309 · `vec_div8p8` 293 |
| `render.cc` | 1,817 | `render_fill_sky_ground` 998 · `render_snap_center_chars` 365 · `_pull_to_center` 307 |
| `clouds.cc` | 1,437 | `clouds_add_candidates` 1,079 · `_clouds_build_basis` 208 |
| `map.cc` | 1,293 | `flight_init_from_mission` 336 · `map_enter` 291 · `_map_draw_object_layer` 171 |
| `gfx.cc` | 1,124 | `_gfx_switch_to_terrain` 296 · `_switch_to_panel_bottom` 229 · `gfx_init_raster_irqs` 170 |
| `box.cc` | 1,056 | `box_prepare` 438 · `box_draw` 314 · `_draw_one_box` 304 |
| `music.cc` | 978 | `music_tick` 723 |
| `sprites.cc` | 921 | `sprites_stack_add` 460 · `sprites_stack_commit` 285 |
| `sound.cc` | 805 | `sound_update` 700 |
| `view.cc` | 788 | `_view_copy_and_fill` 357 · `view_update_cam` 269 |
| `sim.cc` | 694 | `sim_run` 526 |
| `menu.cc` | 677 | `menu_run` 311 · `_menu_render_items` 215 |
| `panel.cc` | 567 | `panel_update_instruments` 567 |

Attribution is by definition site, so a `static` whose name also appears at the
head of a comment elsewhere can land in the wrong column; `sim_run` and
`_gfx_switch_to_terrain` were corrected by hand. Treat the totals as within a
few hundred bytes, and the per-symbol figures as exact.

Compiler-generated overhead is smaller than it looks: **38 `@proxy`
trampolines totalling 387 B** and **31 `$outline` routines totalling 467 B**.
See §4.2 before treating either as recoverable.

---

## 2. Landed

Both in `8676c11`. Together: **code 26,869 → 26,711 B (−158), free RAM 6,300 →
6,418 B (+118)**, with 35 B of that spent back on the new tables.

### 2.1. Key dispatch tables in `sim.cc` (−72 B in `sim_run`)

`sim_run()` carried twelve `if (key_pressed(K)) flight_input(I);` pairs and six
more `if (key_pressed(K)) toggles |= mask;`. Each pair is a constant load, a
`JSR`, a branch and a second constant load. They became two tables - `kHeldKeys`
and `kToggleKeys` - and two loops.

The `kToggleKey*` bit masks moved out of the function body to sit beside the
table they now index into, with the `__ENABLE_DEBUG__` and `__ENABLE_SOUND__`
arms written out per build rather than derived. That is deliberate: the masks
have to follow the table's order, and a key compiled out of the table shifts
every mask above it. Writing both variants out is what makes that impossible to
get subtly wrong, and a `static_assert` holds the mask to one byte.

Cost: about a dozen table loads per frame at ~10 Hz, against a frame that runs
100 ms. Not free, not measurable.

### 2.2. `flight_input()` deduplication (−70 B, 535 → 465 B)

Two things, both in [flight.cc](../c64o/flight.cc):

- Throttle up/down, the flap toggle and move forward/backward were a copy each
  in the ground switch and the airborne switch. They are one switch above the
  `model_on_ground` split now.
- The six airborne rotation arms were the same three lines with a different
  matrix, so they collapse to
  `vec_transform3(kFlightRotations[input - FLIGHT_INPUT_ROLL_LEFT], ...)`.
  A `static_assert` ties the table's length to the contiguous run of rotation
  values in `enum flight_input_t`, which is the thing that would break silently
  if a new input were inserted in the middle.

Ground behaviour is untouched - the nose-wheel speed gate, the rotate-to-
attitude pitch step of `33364c0`, the gear lockout and the brake all still live
in the ground switch, and the airborne gear toggle stays distinct from the
ground one because they genuinely differ. Speed: one extra range compare on a
path that runs on a keypress.

---

## 3. Open — measured, not landed

Four more, measured against the `8676c11` baseline. **All four together: code
−80 B, free RAM +128 B** (the total beats the sum of the code deltas because
one of them trades code for data).

| # | change | code | data | free RAM | speed |
| --- | --- | ---: | ---: | ---: | --- |
| 3.1 | outliner on `fmath.cc` | −42 | — | +42 | ≤3 calls/frame |
| 3.2 | outliner on the cold half of `vec.cc` | −33 | — | +33 | 6.25 calls/s |
| 3.3 | merge `_flight_step_u` into `_flight_step_s` | −25 | — | +25 | identical |
| 3.4 | `kMusicVolumeMix` → arithmetic | +14 | −48 | +34 | 2 compares vs 1 load |

### 3.1. The `nooutline` pragma on `fmath.cc` is too broad

`fmath.cc` opts out of the outliner because the file-level pragma is the only
granularity the Makefile comment thinks in. But nothing in it is per-vertex:
`_get_ratio()` (266 B) is reached from `_get_heading()` twice per model step and
from `_get_roll_angle()` once per frame, so at most three calls a frame. Letting
the outliner have the file gives **−42 B** for a handful of `JSR`/`RTS` pairs at
10 Hz.

### 3.2. Same argument, half of `vec.cc`

`vec.cc`'s `nooutline` is right for `vec_transform_inv()` and `vec_div8p8()`,
which run per cloud candidate and per projected vertex. It is not right for
`vec_normalize()`, `vec_cross()` and `vec_orthonormalize()`: `flight_advance()`
calls `vec_orthonormalize()` once per model step and nothing else in the program
reaches any of the three. `#pragma optimize(push, outline)` around just those
three gives **−33 B**.

`vec_cross()` alone loses 62 B of that, but the net is 33: the outliner shares
extracted sequences program-wide, so re-cutting them grows some of the existing
`$outline` routines. That is why the file-level sweep in §4.4 does not decompose
into its symbols either.

### 3.3. One shift helper instead of two

[flight.cc](../c64o/flight.cc) has `_flight_step_u(uint16_t)` and
`_flight_step_s(int16_t)`, identical but for the shift's signedness. For
non-negative inputs they are bit-identical, so the signed one can serve both:
**−25 B**, and `make test` passes.

**The caveat is the reason this has not landed.** It is only correct while every
`_flight_step_u()` argument stays under `0x8000` - `vec_fastsqr8p8(flight_speed)`,
`vec_fastsqr8p8(flight_cam.left.z)` and `stall_speed - flight_speed`. They are,
comfortably, at the speeds the envelope allows today. But that is a property of
the flight model rather than of the types, nothing checks it, and the failure
mode is a sign flip in drag at high speed. Twenty-five bytes for an unguarded
invariant is a poor trade unless a `__assume()` or an assert comes with it.

### 3.4. `kMusicVolumeMix` is three closed forms

The 48-byte `kMusicVolumeMix[3][16]` in [music.cc](../c64o/music.cc) has rows
that are all-zero, `v >> 1` and `v` - the "low" row's stated
`round(v * 7 / 15)` agrees with `v >> 1` on all sixteen inputs, checked. As
arithmetic it costs **14 B of code to save 48 B of data**, net +34 B of RAM.

This is the one item in §3 that is not speed-neutral: it swaps an indexed load
for up to two compares, in a function `music_tick()` calls a few times per row.
Inaudible, but it is a trade rather than a free lunch, and it is listed
separately for that reason.

---

## 4. Does not work

### 4.1. Packing `sprite_cand_t` from 9 to 8 bytes — **costs 29 B**

`sizeof(sprite_cand_t) == 9`, so `_sprites_cand[i]` indexes through
`__multab9L`. Folding `color` (4 bits) and `flags` (2 bits) into one byte makes
it 8 and should turn the multiply into three shifts.

Built, it **grows the code by 29 B**: `sprites_stack_add()` +25,
`sprites_stack_commit()` +4, against `__multab9L` −5 and `_sprites_cand` −8 of
BSS. The masking at the two read sites costs more than the table lookup saved,
and the insertion sort's struct copy does not get cheaper at 8 bytes. (Measured
at `d1570fc`; at the current baseline the variant does not link at all, for the
reason in §5.)

`__multab9L` does not go away either: `kCloudGroupOffset` in
[clouds.cc](../c64o/clouds.cc) is `[4][3][3]`, so its rows are 9 bytes too and
`clouds_add_candidates()` indexes them the same way.

### 4.2. Eliminating `@proxy` trampolines — **0 B**

oscar64 emits a `@proxy` to stage constant arguments, and the obvious reading of
the map - three `msg_show@proxy` copies, 38 B - is that the default arguments in
`msg.h` are the cause. Splitting `msg_show()` into a one-argument function and a
three-argument `msg_show_for()` produces **byte-identical proxies under the new
name**: −38 B of `msg_show@proxy`, +38 B of `msg_show_for@proxy`, net zero.

The compiler already emits one proxy per distinct constant set and shares it
across call sites. Inlining the staging at each site would be strictly larger.
Treat the 387 B of proxies as the cost of constant parameters (`-Op`), not as
slack.

### 4.3. `#pragma optimize(size)` — **neutral at best, and it breaks two builds**

The pragma exists and is used nowhere in the project. On the cold UI files:

| file | code |
| --- | ---: |
| `help.cc` | **+22** |
| `print.cc` | 0 |
| `screen.cc` | −5 |
| `menu.cc` | build fails |
| `map.cc` | build fails |

`-O2 -Op -Oa -Oi -Oz -Oo` already lands close to size-optimal on cold code, so
there is nothing left for `size` to find. Worse, on `map.cc` it raises oscar64's
own runtime zero-page usage into `$60-$63` and `check_zeropage.py` fails the
link; on `menu.cc` it shifts the layout into the trap in §5.

### 4.4. Dropping `nooutline` on the per-frame files — **462 B, all of it in cycles**

For completeness, since the map makes this look like the biggest number on the
table. Flipping each file's pragma to `outline`:

| file | code | | file | code |
| --- | ---: | --- | --- | ---: |
| `vec.cc` | −192 | | `world.cc` | −34 |
| `render.cc` | −115 | | `poly.cc` | −22 |
| `clouds.cc` | −43 | | `box.cc` | −14 |
| `fmath.cc` | −42 | | `vec_asm.cc` | 0 |

Every byte of it is a `JSR`/`RTS` pair added to code that runs per vertex, per
cell or per candidate. This is exactly the trade the `OPTFLAGS` comment in
[c64o/Makefile](../c64o/Makefile) describes making deliberately, and it should
stay made. §3.1 and §3.2 are the two files where the *file* is the wrong unit,
not the two files where the trade was wrong.

### 4.5. A pivot table for the instrument sprites — **blocked by §5**

`panel_update_instruments()` is 567 B, almost all of it the eight inline
`sprites_set_*()` wrappers staging arguments for
`_sprites_set_instrument_sprite()`. Two of those five arguments, `pivot_x` and
`pivot_y`, are a property of the gauge index, so a 16-byte table indexed by
`idx` should drop them from six call sites.

Untested, because the build never got that far: the 16 bytes of new data
relocated the BSS block into the BASIC ROM window and `check_rom_window.py`
failed the link. See below.

---

## 5. The layout trap any of this can spring

`cpu_probe_us` currently sits at `$85FE`, nowhere near a ROM window. Three of
the variants tested above - §4.1, §4.3's `menu.cc`, §4.5 - relocated the BSS
block wholesale and landed it at `$ACxx`, inside the BASIC ROM at `$A000-$BFFF`,
where `cpu_probe()`'s write goes to RAM and its read comes back from ROM. That
is the frozen simulation `6543dcb` fixed.

This is not a margin that shrinks as data grows. The linker fits BSS wherever it
fits, and a change of a few bytes can move it by kilobytes in either direction.
`check_rom_window.py` catches it every time, which is the only reason it is a
nuisance rather than a bug - but it means **any** change to data size can fail
the build for a reason that has nothing to do with the change, and the fix is to
move the access after `mem_init()` rather than to make the change smaller.

---

## 6. Not pursued

Named so the next pass can skip or resume them, not because they are dead:

- **`flight_advance()` (1,232 B)** and **`clouds_add_candidates()` (1,079 B)**
  are the two largest functions in the program and both are hot. `clouds.cc`
  additionally spends bytes on purpose: the three unconditional
  `_clouds_add_step()` calls carry a measurement in their own comment - the
  branch that would have avoided them cost 137 B and bought nothing - and the
  flat rung table is there because the obvious form miscompiles
  ([clouds.md](clouds.md) §3.4).
- **`map.cc` (1,293 B)** is entirely cold and entirely new, which makes it the
  best remaining ratio of size to risk. Nothing here looked at it closely.
- **`_project_vertices()` (1,341 B)** repeats the same
  `if (n < 3) return 0; src = dst; dst = ...` ping-pong for each of the four
  clip edges. A four-element descriptor loop is the obvious shape, but
  `57ac560` reworked this code for precision recently and the clipper is where
  visual artifacts come from, so it wants its own session and its own
  before/after frames rather than a byte count.
