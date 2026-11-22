# Viewport Sprite Objects (`sprite_objects.md`)

Shipped design for all hardware-sprite objects in the 3D viewport: **clouds**, **sun**, **orientation mark**, **tail fin**, and in `fpilot.prg`, **3D opponent aircraft**, **tracers**, and **smoke**. Implementation lives in `c64o/sprites.{h,cc}`, `c64o/sprites_asm.{h,cc}`, `c64o/clouds.{h,cc}`, `c64o/clouds_asm.{h,cc}`, `c64o/opponent.{h,cc}`, `c64o/opponent_asm.{h,cc}`, `c64o/gun.{h,cc}`, and `c64o/gun_asm.{h,cc}`.

---

## 0. Two absolute rules

1. **No multicolour sprites (`$D01C = 0`) in flight.** Every viewport and panel sprite is hires (24 × 21 px, 1 colour per sprite). The only multicolour sprites in the program are the menu's title aeroplane (`title.cc`, never active during flight).
2. **No Y-expansion (`$D017 = 0`) by default.** Exceptions are explicitly budgeted for DMA clearance ([clouds.md](clouds.md) §1.8):
   - Back-view vertical tail fin (slots 0–1, fixed Y = 78 / 120).
   - Near banked 3D opponents (`kSpriteFlagExpandY`, max start Y = 118, see [planes.md](planes.md) §4).

---

## 1. Band split & properties

### 1.1 Three raster bands (`gfx.cc`, `-DNUM_IRQS=3`)

The screen is split into three sprite bands by raster IRQs:

| Band | Starts at raster | Active sprites |
| --- | ---: | --- |
| Viewport (MCCM) | **252** | Slot 0: orient mark (front) or fin top (back); slot 1: fin bottom (back) or stack; slots 1–7 (front/side) or 2–7 (back): depth-sorted stack |
| Panel top (MCBM) | **161** | Sprite 0: vertical-speed needle (slots 1–7 parked) |
| Panel bottom (MCBM) | **187** | Instrument gauge needles (airspeed, horizon/roll, altimeter, fuel, throttle) |

### 1.2 Viewport sprite properties

Every viewport stack candidate passes through `sprites_stack_add()` and obeys the panel-split DMA cutoff (unexpanded start Y ≤ 139, Y-expanded start Y ≤ 118; [clouds.md](clouds.md) §1.8).

---

## 2. Index sharing

All 8 hardware sprite indices are time-shared across the three bands without conflict because each band's raster handler reprograms positions, pointers, colours, `$D010`, `$D01D`, `$D017`, and `$D015` for its own vertical range (`sprites.cc`, `sprites_asm.cc`). See [clouds.md](clouds.md) §1 for double buffering, volatile ordering, and IRQ zero-page constraints.

---

## 3. Sizing and levels of detail

| Mode | Footprint (screen px) | Pixel size | Used by |
| --- | --- | --- | --- |
| 1× hires | 24 × 21 | 1 × 1 | Orient mark, sun, far clouds (rungs 1–3), far/mid opponents, tracers |
| X-expanded (`$D01D`) | 48 × 21 | 2 × 1 | Mid/near clouds (rungs 4–9), smoke puffs, close tracers, mid/near opponents |
| 2-wide or 2-tall | 48 × 42 or 96 × 42 | 1×1 or 2×1 | Stacked clouds (rungs 5–9), near opponents |
| X+Y expanded | 48 × 42 or 96 × 42 | 2 × 2 | Closest banked opponents ([planes.md](planes.md) §4) |

Note on granularity: MCCM ground dots sit on a **2 × 2 screen-pixel** lattice, so X-expanded sprites (2 × 1) and dither-snapped clouds ([clouds.md](clouds.md) §4.5) match the viewport's visual scale closely.

---

## 4. Colour

Hires sprites have one colour register each (`$D027–$D02E`):

| Object | Colour |
| --- | --- |
| Clouds | `COLOR_CLOUD` (`kColorWhite`, or `kColorMedGray` at Night in `ppilot`) — 50% checkerboard dither |
| Sun | `COLOR_SUN` (`kColorYellow`, or `kColorWhite` at Dusk/Night in `ppilot`) |
| Orientation mark | `kColorWhite` |
| Back-view tail fin | `COLOR_FIN` (`kColorLightGray` Earth/Desert, `kColorMedGray` Dusk/Night) |
| Opponents | `kColorOpponentFar` (`kColorBlack`) when far (`d < 8`), `kColorOpponentNear` (`kColorDarkGray` fighters / `kColorBrown` bombers) when near |
| Tracers | `kColorYellow` |
| Smoke / ground puff | `kColorBlack` (checkerboard dither) |

---

## 5. Per-frame cost

Measured against the ~98,500–153,000-cycle frame budget ([framerate.md](framerate.md)):
- Clouds (`CLD`): 11.8k–28.3k cycles ([clouds.md](clouds.md) §5).
- Stack commit (`SPR`): ~250–990 cycles (`sprites_asm.cc`).
- Opponents + guns (`fpilot` live combat): ~35k–50k cycles on close passes ([planes.md](planes.md) §11).

### 5.1 Cheap world-space rejection

Every sprite client rejects out-of-range and behind-camera objects in cheap integer world/camera space before projecting or rasterising (clouds: 5 × 5 cell scan + 4,096 m cull + staged wedge reject; opponents: staged row-by-row `vec_dot` frustum reject; tracers: staged `_gun_view_round`). Never run position projection at half rate (opponents split only *silhouette refill* across frames while placing the sprite every frame so motion stays smooth).

---

## 6. Object classes

### 6.1 Clouds

5 × 5 cell scan (2,048 m cells, 37/128 active), 4,096 m cull, 3-blob groups collapsing to 1 blob past 1,864 m, occupying 14 static sprite blocks at `$D440–$D7BF` (pointers 81–94; block 80 at `$D400` is the orientation mark). Full spec in [clouds.md](clouds.md).

### 6.2 Other aircraft (`fpilot.prg`)

Filled convex 3D polygon silhouettes rasterised dynamically into **16 sprite blocks at `$D000–$D3FF`** (2 opponents × 2 double-buffered 4-block sets, under I/O, time-shared with map screen RAM and title sprites) plus **1 static dot block (`kOpponentDotBlock`) at `$CFC0`**. Up to 4 sprites for the nearer opponent and 2 for the farther (capped to 2+2 when tracers/smoke/fin tighten the 7/6-slot stack). Full spec in [planes.md](planes.md).

### 6.3 Projectiles & smoke (`fpilot.prg`)

Uses at most **2 stack sprites** and **zero new sprite blocks** (`c64o/gun.cc`, [projectiles.md](projectiles.md)):
- **1 tracer:** reuses the 32 instrument arm bitmaps (pointers 96–127) in `kColorYellow`, optional `ExpandX` when close.
- **1 smoke/ground-hit puff:** reuses cloud rungs 1–4 (pointers 81–84) in `kColorBlack` with `ExpandX | AlignDither`.

---

## 7. Message strip

Status notices occupy row 13 of the viewport (`msg.cc`). When `msg_active()` is true, `sprites_stack_add()` rejects candidates overlapping row 13 (`_sprites_hidden_by_msg()`) so sprites do not obscure HUD text.
