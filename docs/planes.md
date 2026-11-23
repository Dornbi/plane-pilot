# 3D Opponent Aircraft Renderer (`planes.md`)

Shipped in `fpilot.prg` (`c64o/planedef.h`, `planes.{h,cc}`, `planes_asm.{h,cc}`, `opponent.{h,cc}`, `opponent_asm.{h,cc}`) and standalone `planedemo.prg`. Reference twins live in [lib/planes.py](../lib/planes.py) and [docs/planes-prototype.html](planes-prototype.html) (`make planes` generates `c64o/planedef.h`).

---

## 1. Core idea & constraints

Opponents are drawn as **filled convex 3D silhouettes** directly into dynamic hardware sprite bitmaps, plus **1 static dot sprite** at long range:
- **Flat surfaces:** wing (6-vertex hexagon), tailplane (6-vertex hexagon), and vertical fin (4-vertex quad).
- **Fuselage:** 3-station screen-space tapered tube (6-vertex polygon) plus an end-on octagon when foreshortened.
- **Sprite rules ([sprite_objects.md](sprite_objects.md) §0):** hires only (`$D01C = 0`); Y-expansion (`$D017`) is enabled only together with X-expansion when a near banked silhouette exceeds 21 vertical lines.

---

## 2. Scale & units

Projection uses `px = 256 * S / D` (same focal length as terrain):
- Model geometry (`planedef.h`) is in **1/16 m** (`kPlaneMaxRadius = 68` ≈ 4.25 m half-span, 8.5 m span).
- `fpilot` passes camera-space offsets in **1/8 m** at near range (`<= 2 km`, fighters `kn = 156` ~20 m span / bombers `kn = 204` ~26 m span, +31% larger so bombers read clearly from afar) and **1/2 m** at far range (`2–4 km`, `kn >> 2`):
  - Dot → silhouette (`d >= 3`): **~1.2 km** (back `< 2` at ~1.7 km).
  - Near colour (`d >= 8`, §8): **~620 m** (back `< 7` at ~720 m).
  - 2-sprite cap (`kmax = 88`): **~115 m**.
  - 4-sprite cap (`kmax = 150`): **~66 m**.

---

## 3. Pipeline & model (`planedef.h`)

Generated from `lib/planes.py` (`tools/generate_planes.py`):
- **16 distinct magnitudes** (`kPlaneMagCount = 16`), **15 `(axis, mag)` pairs** (`kPlaneAxisPairs = {1, 12, 14, 16}`), **16 flat vertices** (`kPlaneVertCount = 16`, `kPlanePolyStart = {0, 6, 12, 16}`), **3 fuselage stations** (`kPlaneBodyCount = 3`), **30 vertices max** (`kPlaneVertMax = 30`: 16 flat + 6 tube + 8 end-on octagon).

Runtime pipeline (split across frames in `fpilot`, §13):
1. **Staged cull (`_opponent_cull()`):** row-by-row `vec_dot()` (`front` → `cx > 64`, `up` → vertical frustum, `left` → horizontal frustum).
2. **`planes_centre()` (asm):** cone reject, shares positive `frac_d = c->x` across `cx`, `cy`, `k` via `vec_frac8` (`2 * kn >= c->x` clamps to `view->kmax`), computes apparent radius `d = (R * k) >> 7` via `vec_mul8`.
3. **`planes_begin()`:** dot-tier hysteresis; if dot tier, resets dot layout and returns `false` so `_opponent_axes()` and silhouette work are skipped.
4. **`planes_project_axes()`:** builds screen-space axis components via `_opponent_axes()` (only `(y, z)` rows from level heading + pitch/bank), computes `_planes_mags()`, `_planes_axis_products()`, `_planes_flat()`, `_planes_box()`, `_planes_fuselage()` (3 stations, norm2, 6-vert tube or end-on octagon), `_planes_layout()`, and `_planes_key()` (local byte vertices + cache check).
5. **`planes_place()` / `planes_reslide()` (asm):** places sprites on the viewport stack every frame and slides the bitmap window smoothly across the panel DMA cut (§4).
6. **`planes_fill()` (asm):** clears active sprite blocks and fills the convex polygons if the cache key changed (§6–§7).

---

## 4. Pixel size, layout, DMA slide & cap

- **Ladder (`planedef.h`):** `d = (kPlaneMaxRadius * k) >> 7`, with integer hysteresis (~87% on shrink) to prevent size chatter:
  - Dot tier: `d < kPlaneDDot` (`3`, drops below `2`).
  - 1× hires (`1 × 1` px): `d <= kPlaneD1x` (`21`, drops below `18`).
  - X-expanded (`2 × 1` px): above `21`, up to `kPlaneYLimit` (`41`, holds down to `35`), where X+Y expansion (`2 × 2` px) activates if tall.
  - Multi-sprite grid (`cols ∈ {1, 2}`, `rows ∈ {1, 2}`): holds 2 cols/rows down to `kPlaneColsHold = 20` / `kPlaneRowsHold = 17`; `planes_layout_t` (8 B) centres the silhouette invisibly inside the chosen grid.
- **DMA slide (never reject on cut):** Viewport DMA cutoffs are `cut1 = 89` (unexpanded) and `cut2 = 68` (Y-expanded; [clouds.md](clouds.md) §1.8). When a silhouette nears the bottom cut, `_planes_layout()` / `planes_reslide()` shifts its vertical offset (`oy`) inside the sprite box so the hardware sprite's start raster stays legal without clipping off early.
- **Cap:** Constant `kmax` per view (`kPlaneKMax = 150` for 4-sprite allowance, `kPlaneKMaxTwoSprites = 88` when capped to 2 sprites).

---

## 5. Memory & sprite allocation

### Sprite blocks (17 blocks total)

| Address | Blocks | Purpose & banking |
| --- | ---: | --- |
| `$CFC0–$CFFF` | 1 (ptr 63) | `kOpponentDotBlock` — static 2×2 dot at `(12, 19)`, plain RAM outside I/O (no banking) |
| `$D000–$D3FF` | 16 (ptrs 64–79) | 2 opponents × 2 double-buffered 4-block sets (64–71, 72–79), **under I/O**, time-shared with map screen RAM and menu title sprites |

- Only `planes_fill()` banks RAM under I/O (`mmap_set(MMAP_RAM)` / `MMAP_NO_ROM`); `_gfx_isr` saves and restores `$01`.
- Returning from map or menu calls `opponent_invalidate()`, forcing a clean redraw.

### Stack integration & `fpilot` RAM footprint (pass 152)

- Uses `kSpriteFlagWide` (2-wide horizontal pairs) and `kSpriteFlagExpandY` (42-line Y-expanded near planes) in `sprites_stack_add()`.
- Struct sizes: `planes_view_t` 10 B, `planes_layout_t` 8 B, `planes_frame_t` 19 B, `planes_state_t` **84 B** (pinned by `static_assert` in `opponent_asm.cc`).
- Renderer footprint (`planedef.h` + `planes.cc` + `planes_asm.cc`): **3,544 B** main (3,342 B code + 202 B data incl. 78 B reciprocal table), **33 B** `bss2`, **242 B** `tscratch` (shared screen rows 14–24 at `$EA30–$EBE7`), **34 B** ZP.

---

## 6. Rasteriser

Convex edge-inclusive pixel-centre span fill directly into 1–4 hardware sprite blocks (64 B each, 3 bytes/row × 21 rows):
- **Reciprocal table (`planedef.h`):** `__striped` **39 entries (78 B)** (`kPlaneRecipFirst = 3`, `kPlaneRecipCount = 39`, storing `65536 / dy` for `dy = 3..41`; `dy = 1, 2` use shifts).
- No clipping needed (layout bounds guarantee every vertex lies inside the active sprite grid).
- Wing-hub guard prevents sub-pixel slivers from vanishing; dot bitmap is a fixed 2×2 blob at `(kPlaneDotX = 12, kPlaneDotY = 19)`.

---

## 7. Caching

Two-level cache in `planes_state_t` avoids redundant bitmap clears and polygon fills:
1. **Early check (`_new_axes`):** compares `(k, axes, slide)` using `kAxisScreenByte[12]` before re-projecting.
2. **Vertex key (`_planes_key`):** compares local byte coordinates `key_x[30]` / `key_y[30]` and `key_band` (128-line band); if unchanged, `planes_fill()` is skipped completely and the existing double-buffered sprite blocks are reused.

---

## 8. Colour

`color.h` + `opponent.cc`:
- **Far (`d < 8`, hysteresis `< 7`):** `kColorOpponentFar` (`kColorBlack`).
- **Near (`d >= 8`):** fighters use `kColorOpponentNear` (`kColorDarkGray`), bombers use `kColorBrown`.

---

## 9. Known limitations

- Single aircraft geometry (fighters and bombers share the mesh and differ in colour, speed, and behaviour).
- 1-colour hires silhouette (no canopy/markings).
- Very close passes (`< 66 m` at 4-sprite cap or `< 115 m` at 2-sprite cap) clamp apparent size at `kmax` rather than overflowing 4 sprites.

---

## 10. Reference twins & tests

- **Python / HTML twins:** `lib/planes.py` and `docs/planes-prototype.html` implement the exact same integer pipeline, checked bit-for-bit over **12,304 frames** by `tests/test_planes.py::TestTwin`.
- **Host C++ tests:** `c64o/test/planes_test.cc` (5,848 byte-for-byte frames vs `lib/planes.py`, single-call & split) and `c64o/test/planes_edge_test.cc` (edge/slide/reslide invariants).
- **On-target 6510 tests (`c64o/test/target_test.cc`):** tests 6 & 7 (513 frames), test 16 (`planes_split_reslide_asm`), test 19 (`vec_div_and_planes_centre_asm`).

---

## 11. Implementation & performance

- **6502 `__asm` (`c64o/planes_asm.cc`):** `planes_centre`, `_planes_mags`, `_planes_axis_products` (`_pair_products` inlined quarter-square multiply with zero-component skip), `_planes_flat`, `_planes_box`, `_planes_tube` + `_planes_fuselage`, `_planes_layout`, `planes_place` + `planes_reslide` (sharing `_place`), `_planes_key`, and `planes_fill` (8×-unrolled clear, ZP-backed span fill with `dx == 0` fast-skip and single-load masks). `#ifndef __OSCAR64__` C mirrors remain in `planes.cc` for host tests.
- **`lib/planes.py` cost model:** models projection/fill cycle counts for generator and test regression checks (`tests/test_planes.py`).
- **Measured `fpilot` combat frame times (`tools/bench.sh`, pass 152):** `headon` **93,221 cy** · `chase` **117,568 cy** · `shootdown` **148,865 cy** · `strafe` **150,562 cy** · `scramble` **153,235 cy** (see [framerate.md](framerate.md)).

---

## 12. Standalone demo (`planedemo.prg`)

`c64o/planedemo.cc` runs `planes_render()` on its own in character mode with keyboard flight controls and live cycle readout; `make -C c64o planedemo_prof.prg` times each pipeline stage individually.

---

## 13. Integration in `fpilot` (`opponent.cc`, `opponent_asm.cc`)

`opponent_add_sprites()` drives up to **two simultaneous opponents** (`slot 0` and `slot 1`) through the split renderer:

1. **Half-rate silhouette refill:** Every frame places the currently shown sprite via `planes_place()` (smooth motion at full frame rate). Silhouette projection (`planes_begin` + `planes_project_axes`) runs on one frame and `planes_reslide()` + `planes_fill()` on the next, flipping the opponent's double-buffered 4-block set when ready.
2. **Reslide on DMA cut (`planes_reslide()`):** Re-slides the queued silhouette for the frame it is actually filled in (`kPlaneReslideDeep`, `kPlaneReslideOff`, `kPlaneReslideAlways`), rationing "too high" refills to at most 3 consecutive frames (`_opponent_refills[i] < 3`) while always honouring "cut too deep" so DMA never tears.
3. **Sprite budget cap (`_opponent_cap()`):** `kSpriteTerrainSlots = 7` (slots 1–7) in front/side views and **6** (slots 2–7) behind the 2-sprite tail fin in `VIEW_BACK`. Letting `extra = (_opponent_smoke != 0 || gun_puff_up()) + gun_tracer_up()`:
   - When both opponents are visible, if `extra > (view_state == VIEW_BACK ? 0 : 1)`, both cap to 2 sprites (`kmax = 88`).
   - Otherwise, the nearer opponent gets up to 4 sprites (`kmax = 150`) and the farther gets 2 (nearer switches with 1/8 depth hysteresis; a far-range plane is always farther).
4. **Near / far views:** Within `[-15, 15]` world units (~2 km), `_opponent_world_delta()` passes 1/8 m offsets with `kn = 156`; in `[-31, 31]` units (~4 km), 1/2 m offsets with `kn = 39`.

For opponent AI, waves, missions, guns, damage, and debrief, see [opponent_ai.md](opponent_ai.md) and [projectiles.md](projectiles.md).
