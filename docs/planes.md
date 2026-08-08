# Other Aircraft — Traffic Sprites

This document specifies how other aircraft are drawn in the viewport. Scope is
**graphics only**: the rendering pipeline, sprite and RAM allocation, the
rasteriser and the caching scheme. Traffic behaviour (canned kinematic paths),
collision and the map screen are named where they touch the renderer but are
not designed here.

Interactive reference implementation:
[planes-prototype.html](planes-prototype.html). It runs the exact pipeline
below in JavaScript — same projection, same offset scaling, same tier
selection, same rasteriser — so the numbers it prints are the numbers the C64
code should produce.

### Relationship to `sprite_objects.md`

[sprite_objects.md](sprite_objects.md) is the layer underneath this document:
the general scheme for putting *any* world object — clouds, aircraft,
projectiles — into the eight hardware sprites, covering the raster band split,
index sharing with the instrument panel, distance sorting and the candidate
scan. **That document owns the sprite engine; this one owns what goes in an
aircraft's bitmap.** Everything here is built on its §1 and §2.

Three places where this document supersedes or corrects it:

| `sprite_objects.md` | Here |
| :--- | :--- |
| §3 ladder rung 3 is Y-expansion, "free, still one sprite" | Aircraft never use Y-expansion (§4). It stays valid for clouds. |
| §6.2 proposes 6–8 pre-rendered airframe bitmaps | Superseded — aircraft carry no bitmap data at all (§1). |
| §5 measures 8 objects against a 19,705-cycle PAL frame and concludes it does not fit | Wrong denominator; the pipeline runs once per *sim* frame, ~98,500 cycles (§11). |

---

## 1. The core idea

Pre-rendered sprites are out: a plane's appearance depends on two viewing
angles plus size, and the combinations do not fit in RAM. But the sprite does
not need to be looked up, because **the silhouette is two or three straight
lines and the endpoints come straight out of the existing 3D math**.

An aircraft is modelled as four points — nose, tail, left tip, right tip —
plus optionally a fin tip (§6). Project them, draw a line nose→tail and a line
tip→tip into a sprite buffer, point a hardware sprite at it. Perspective, foreshortening, bank and aspect all
fall out of the projection. There is no angle quantisation, no orientation
table, and **no static bitmap data at all**.

The whole feature is therefore code plus scratch RAM, not art.

---

## 2. Scale — how big is a plane, actually

`vec_project()` returns `vec_sx = 256·y/x`, and `gfx.cc` uses it as a pixel
offset directly (`px = 160 − vec_sx`). So the angular scale is **256 pixels per
unit of tangent**, the viewport half-width of 160 px is `tan = 0.625`, and the
horizontal field of view is ~64°.

An object of size `S` at distance `D` therefore spans

```
pixels = 256 · S / D
```

For an 11 m wingspan:

| Span on screen | Distance | Time to close at 50 m/s |
| ---: | ---: | ---: |
| 1 px  | 2816 m | 56 s |
| 4 px  |  704 m | 14 s |
| 24 px |  117 m | 2.3 s |
| 48 px |   59 m | 1.2 s |

**Most of the time a plane is a dot.** It only fills a sprite in the last two
seconds before a merge. This is worth internalising before optimising the close
tiers: the far tier is the common case, the near tiers are the rare one. It is
also an argument for a size exaggeration factor — see §10.

World units: `flight_eye_*` is 24.8 fixed point in metres and `world.cc`
down-shifts by 9, so **render-space units are 2 m**. Since the ratio `S/D` is
dimensionless the pixel formula above is unaffected.

---

## 3. Pipeline

Per plane, per frame:

1. **Relative position.** `P = target_world − flight_eye`, three 32-bit
   subtractions, then `>> 9` to render units (int16).
2. **Cheap world-space reject** before spending a transform, per
   [sprite_objects.md](sprite_objects.md) §5.1: sign of `front · P` plus a
   Manhattan distance bound, roughly three multiplies. With only two planes
   this is nearly free either way, but it is the same code path the cloud
   candidate scan needs.
3. **To camera space.** `vec_transform_inv(&world_cam, &P, &C)` — 9 multiplies,
   already exists.
4. **Cull.** `C.x <= 8` (behind or on top of the camera) → skip. Range cull at
   `C.x > kMaxRange` → skip. Cull if the sprite would land in the message band
   while a message is up ([sprite_objects.md](sprite_objects.md) §7).
5. **Centre.** `vec_project_nocull()` → `cx = 160 − vec_sx`,
   `cy = 56 − vec_sy`.
6. **Perspective scale.** `k = (16 << 8) / C.x` via `vec_div8p8(16, C.x)`.
7. **Body axes in camera space.** Transform only the target's `front` and
   `left` vectors: two `vec_transform_inv` calls, 18 multiplies. The four model
   points are `±front·L` and `±left·H`, so their *directions* are these two
   vectors and no per-point transform is needed.
8. **Screen offsets, in two stages.** First reduce the three model dimensions
   to pixel half-extents — three multiplies, once per plane:

   ```
   pxH  = (vec_fastmul8p8(2·H,  k) + 1) >> 1    // half wingspan
   pxLN = (vec_fastmul8p8(2·LN, k) + 1) >> 1    // nose
   pxLT = (vec_fastmul8p8(2·LT, k) + 1) >> 1    // tail
   ```

   Then each endpoint is one multiply per axis against a unit 8.8 vector:

   ```
   nose = (cx − fmul(fore.y, pxLN), cy − fmul(fore.z, pxLN))
   tipL = (cx − fmul(lat.y,  pxH ), cy − fmul(lat.z,  pxH ))
   ```

   Eleven multiplies instead of sixteen, and the truncation is isolated in
   three places instead of eight. **The doubling is not decoration.**
   `vec_fastmul8p8` truncates toward zero, and on a 12 px half-span that
   systematically loses up to a whole pixel — the prototype measured the
   silhouette rendering ~9% small before this was added. Doubling the input and
   halving the rounded result costs one shift and one add per extent and brings
   the worst-case error over the whole distance range down to 1.7 px of span.

   This is the first-order approximation — it ignores the change in `C.x`
   across the object, which is correct to within a fraction of a pixel for
   anything small enough to fit in a sprite, and it removes the near-plane
   clipping problem entirely.
9. **Tier selection** from the bounding box of the four screen points (§4).
10. **Cache check** on the eight local endpoint bytes (§7). Hit → skip to 12.
11. **Rasterise** into the back buffer: clear, two strokes (§6), flip pointer.
12. **Program the sprite(s)**: position, `$D010` MSB, `$D01D` expansion,
    colour (§8), pointer in both screen RAM copies.

### Why ⅛ m model units

A Cessna's half-span is 5.5 m. In 2 m render units that is 2.75 — rounding it
to 3 is a 9% error in wingspan. Storing model offsets in ⅛ m keeps the half
span at 44 and the half length at 33, both comfortably int8, and the `k`
constant absorbs the unit change: `k = 4096 / C.x` makes
`vec_fastmul8p8(O_eighths, k)` come out in pixels.

Check: `C.x = 50` units (100 m) → `k = 82`; `44 · 82 >> 8 = 14` px half-span,
28 px full span. Direct formula: `256 · 11 / 100 = 28`. ✓

---

## 4. Sprite tiers

The tier is chosen from the **projected bounding box**, not from distance. That
matters: a steeply banked aircraft projects its wingspan vertically, and a
distance-driven ladder would get it wrong.

It is also not a ladder. **The two axes are decided independently** — width
picks X-expansion, height picks the sprite count:

```
xs      = (bbox_w <= 24) ? 1 : 2        // X-expansion
rows    = (bbox_h <= 21) ? 21 : 42      // sprite count = rows / 21
```

plus a dot case when the box is 3 × 3 or smaller.

|  | ≤ 21 px tall | > 21 px tall |
| :--- | :--- | :--- |
| **≤ 24 px wide** | 1 sprite, 1:1 | 2 sprites, 1:1 |
| **> 24 px wide** | 1 sprite, X-expanded | 2 sprites, X-expanded |

A linear ladder gets this wrong in a specific and common way: a steeply banked
aircraft is **tall and narrow**, needs two sprites for its height, and would be
X-expanded along with them for no reason — throwing away horizontal resolution
on the axis that was never the problem. The prototype hits that case at any
bank past ~70°, which is not exotic.

X-expansion only, never Y. Two reasons, and they point the same way:

- **Horizontally, expansion costs nothing against the world.** The terrain is
  drawn in character mode at what
  [sprite_objects.md](sprite_objects.md) §3 calls world-pixel resolution — 160
  across the viewport, i.e. 2 screen pixels each. An X-expanded hires sprite
  pixel is exactly 2 screen pixels. So the expanded cases are not coarser than the world
  they sit in; they match it. An *un*expanded sprite is finer than the terrain
  around it, which is a luxury, not a baseline.
- **Vertically, expansion costs the thing that defines the silhouette.** Both
  strokes are usually near-horizontal, and the readability of a near-horizontal
  line is set by its *vertical* placement precision. Doubling pixel width
  coarsens a run that is already many pixels long; doubling pixel height turns
  the wing into a two-line staircase.

This diverges from [sprite_objects.md](sprite_objects.md) §3, which lists
Y-expansion as the free third rung. For a cloud — a soft blob with no
silhouette to lose — it still is. For an aircraft it is the expensive rung
disguised as a free one.

The two-sprite case is two hardware sprites at the same X, 21 raster lines
apart, sharing expansion and colour. Rasterising is unchanged — the buffer is
simply 42 rows and the second sprite points at the second block.

Past 48 × 42 the model is clamped and the silhouette clips at the buffer edge.
At 48 px span the aircraft is 59 m away and about a second from a collision.

**Hysteresis**, per axis: promote at the limit, demote at 87% of it, so a plane
hovering at a threshold does not flicker between resolutions. Even so, the
1:1 → X-expanded switch doubles the pixel size and will visibly pop; this is
inherent to sprite expansion and is accepted.

---

## 5. Memory and sprite allocation

### Sprite buffers must not live under I/O

The obvious home is the free `$D400–$D7BF` (15 blocks, pointers 80–94). **Do
not use it for dynamic buffers.** It is RAM under the SID, so every write needs
`$01` switched to `MMAP_RAM`, which means interrupts off. Blocking the raster
IRQ for the ~600 cycles of a 63-byte block would delay the panel split by ten
raster lines and glitch the screen edge every frame. That region is fine for
data written once at startup — which is exactly what it is used for today — and
wrong for anything written per frame.

Instead, carve the buffers out of the top of the main region, which is plain
RAM inside VIC bank 3 and needs no banking at all:

```c
#pragma section(sprbuf, 0, , , bss)
#pragma region( sprbuf, 0xCE00, 0xD000, , , {sprbuf} )
#pragma region( main,   0x0860, 0xCE00, , , {code, data, data_compr, bss, heap} )
```

`$CE00–$CFFF` is 8 blocks, sprite pointers **56–63**, and costs 512 B of the
17.9 KB currently free.

### Block assignment

Two planes, up to two sprites each, double buffered:

| Plane | Front/back A | Front/back B |
| :--- | :--- | :--- |
| 0 | 56 / 57 | 58 / 59 |
| 1 | 60 / 61 | 62 / 63 |

Double buffering is not optional. The VIC fetches sprite data on the lines
where the sprite is displayed; rewriting a block in place tears the image for
one frame, and at ~10 fps single tears are clearly visible. Flipping the
pointer costs one byte written to each of the two screen RAM copies.

### Hardware sprite indices

This is just [sprite_objects.md](sprite_objects.md) §2's rule — sort candidates
by distance, nearest gets the lowest index, sun sorts last — instantiated for
two planes:

| Index | Use |
| :---: | :--- |
| 0, 1 | nearer plane — upper block, plus the lower block when 42 rows are needed |
| 2, 3 | farther plane |
| 4 | sun |
| 5–7 | free (clouds) |

Planes therefore always outrank the sun and occlude it correctly, and when two
planes overlap the near one wins, with no per-frame priority logic.

Programmed in the top-of-viewport raster handler alongside the existing
`sprites_show_terrain_sprites()`. Two things the panel handlers must do:

- **`_switch_to_panel_top` must clear `$D01D`** (X-expansion) as well as
  parking sprites at `x = 0`. Parking works because the VIC compares X per
  raster line, but an X-expanded sprite is 48 pixels wide and the left border
  ends at 24 — parked at `x = 0` it would poke 24 pixels into the panel. One
  extra store in a cycle-counted handler.
- **Colour restore moves to `_switch_to_panel_bottom`**, per
  [sprite_objects.md](sprite_objects.md) §2, so the terrain handler is free to
  write all four plane colours without a handshake.

### Budget

| Item | Bytes |
| :--- | ---: |
| Sprite buffers (`$CE00–$CFFF`) | 512 |
| Run-mask tables (`kMaskFrom`, `kMaskTo`, 24 × 3 × 2) | 144 |
| Per-plane state, 2 planes | ~74 |
| Rasteriser code | ~350 |
| Pipeline code | ~450 |
| **Total** | **~1.5 KB** |

---

## 6. The rasteriser

Never plot pixel by pixel. **Iterate over rows** — the sprite is at most 42
rows and a row is three contiguous bytes, which is the natural addressing unit.

```
stroke(x0, y0, x1, y1):
    order the endpoints so y0 <= y1
    dy = y1 - y0
    if dy == 0:
        fill_run(y0, min(x0,x1), max(x0,x1))
        return
    slope = ((x1 - x0) << 8) / dy        # vec_div8p8
    xa = x0 << 8
    for y in y0 .. y1:
        xb = (y == y1) ? (x1 << 8) : xa + slope
        fill_run(y, min(xa,xb) >> 8, max(xa,xb) >> 8)
        xa = xb
```

`fill_run(y, a, b)` ORs `kMaskFrom[a] & kMaskTo[b]` — three bytes — into
`buf + 3·y`. `kMaskFrom[a]` has bits `a..23` set, `kMaskTo[b]` has bits `0..b`
set; 144 bytes of table for both.

Properties that matter:

- **At most 42 iterations, usually far fewer.** Cost is bounded by the buffer
  height, not by line length.
- **Thickness is free on shallow lines** — the per-row run is naturally several
  pixels wide. Steep lines come out 1 px; widen the run by one to keep them
  visible.
- **No special cases** for octant or direction beyond the `dy == 0` guard.

Estimated ~35 cycles per row including the three-byte OR, so ~700 cycles for a
worst-case full-height stroke and typically 200–300.

### A finding from the prototype: two strokes collapse in level flight

Two strokes was the chosen silhouette, and for banked or climbing traffic it
reads well. But when both aircraft are level — the overwhelmingly common case —
the wing and the fuselage project onto the **same screen row**, and the whole
silhouette degenerates to a single horizontal dash:

```
level, head-on, 80 m,          the same, with a fin stroke
2 strokes:                     added from the tail:

...###################..       ............##..........
                               ............##..........
                               ............##..........
                               ............##..........
                               ...###################..
```

The dash is geometrically correct and completely unreadable — it is
indistinguishable from a horizon artefact. A third stroke from the tail along
the aircraft's `up` axis fixes it, costs one more `vec_transform_inv` for the
`up` vector and one more stroke, and the prototype measures the difference at
**175 cycles** — under 0.2% of a sim frame.

The fin also disambiguates upright from inverted, which the two-stroke
silhouette cannot express at all.

The prototype has a **Third stroke: tail fin** checkbox, off by default to
match the decision as made. Worth toggling before committing to two — this is
the one place where the cheap answer looks clearly worse on screen.

---

## 7. Caching

Cache on the **eight local endpoint bytes**, not on an orientation angle.

After step 7 of the pipeline, the four screen points are converted to
buffer-local coordinates. If all eight bytes and the tier match what was
rendered last frame, the bitmap is already correct — skip the clear, skip both
strokes, skip the buffer flip, and only write the sprite position registers.

This is both cheaper and more accurate than tracking orientation, and it hits
often: in level flight the relative *position* changes every frame while the
relative *orientation* barely moves, and the local coordinates depend only on
the latter plus distance. A comparison of eight bytes costs ~40 cycles against
the ~1600 it saves.

---

## 8. Colour

Sprites are hires with an independent colour each, so each plane picks its own
with no negotiation.

The horizon test is exact and needs no projection at all: an aircraft appears
above the horizon **iff its altitude is above the eye's**, because the horizon
is by definition the set of zero-elevation directions from the eye. One 32-bit
comparison:

```c
color = (target_world_z > flight_eye_z) ? kColorBlack : kColorWhite;
```

Black on the blue/cyan sky, white on the green ground. The gradient band around
the horizon is where either choice is mediocre; the switch happens exactly
there, so it is self-correcting.

---

## 9. Known limitations

- **No occlusion against terrain.** Sprite-behind-background would hide the
  plane behind any non-background pixel of the dithered horizon, i.e. almost
  all of it. Planes are always drawn in front. Since the sun stays well above
  the horizon and planes outrank it in priority, nothing else needs handling.
- **The bottom of the viewport clips, and that is fine.** The panel split parks
  sprites at `x = 0` and the VIC compares X per raster line, so a plane low in
  the viewport is cut cleanly at the viewport boundary
  ([sprite_objects.md](sprite_objects.md) §1.1) — no cull needed, provided
  `$D01D` is cleared too (§5). The sun's current whole-sprite cull is stricter
  than the hardware requires and could be relaxed at the same time.
- **X MSB.** The viewport spans the full screen width, so plane sprites cross
  x = 255 constantly; `$D010` handling is required, as it already is on the map
  screen.
- **The message strip.** A plane behind an on-screen message must be culled;
  with two planes the box-overlap test that exists for the sun is affordable,
  and the single-comparison shortcut in
  [sprite_objects.md](sprite_objects.md) §7 is the fallback if it is not.
- **The 1:1 → X-expanded pop.** Inherent to expansion, mitigated only by
  hysteresis.

---

## 10. Open questions

1. **Two strokes or three?** Reopened by §6 — the two-stroke silhouette
   degenerates to a single horizontal dash whenever both aircraft are level,
   which is most of the time, and a tail fin fixes it for 175 cycles. This is
   the highest-value open question in the list.
2. **Size exaggeration.** At true scale a plane is under 4 px beyond 700 m,
   which is most of any encounter. A 1.5–2× model scale would make traffic
   readable at realistic separations at the cost of realism. The prototype has
   a slider for this — worth deciding by eye before writing the C code.
3. **Does the dot tier need its own bitmap** (a static 2×2 blob at pointer 80,
   in the `$D400` region) or is running the normal rasteriser for a 3 px
   silhouette simpler? The static blob saves ~1500 cycles in the common case.
4. **Python reference and tests.** Every other generated asset in this project
   has a Python model in `tools/` with pytest coverage. Nothing is *generated*
   here, but the projection and rasteriser are exactly the kind of code that
   benefits from a host-side reference — the HTML prototype is one, but it is
   not a test. Add `tools/planes.py` + `tests/test_planes.py`, or rely on
   `vectest`-style on-target checks?
5. **Colour pair.** Black/white as specified, or dark grey/light grey for a
   softer look against the gradient band?

Two questions from [sprite_objects.md](sprite_objects.md) §8 are answered here
and need no further debate: aircraft **do** want per-object colour (§8, and it
costs one register write), and the update rate is **every sim frame** — its own
§5 is right that half-rate projection reads as jitter, because camera rotation
moves a stationary object across the screen.

---

## 11. Cycle budget

**The denominator is the sim frame, not the PAL frame.** The viewport is
rebuilt once per `flight_advance` at a wobbling ~10 Hz
([sound.md](sound.md) §), which is five PAL frames, ~98,500 cycles.
[sprite_objects.md](sprite_objects.md) §5 compares its 8,000-cycle estimate
against a single 19,705-cycle frame and concludes eight objects do not fit; on
the correct denominator the same estimate is 8%, and its conclusion should be
revisited rather than treated as a constraint. The mitigations it proposes are
still worth having — they just are not urgent.

| Step | Cycles |
| :--- | ---: |
| Relative position, cull | ~90 |
| `vec_transform_inv` for position | ~450 |
| `vec_project_nocull` + `k` divide | ~400 |
| Two `vec_transform_inv` for body axes | ~900 |
| Extent and offset scaling, tier, cache compare | ~260 |
| — subtotal, cache hit | **~2,100** |
| Clear buffer (unrolled) | ~200 |
| Two strokes, worst case | ~1,500 |
| Sprite registers and pointers | ~60 |
| — subtotal, cache miss | **~3,900** |

Swept over every heading, bank and distance, the prototype's worst single-plane
frame is **3,865 cycles** with two strokes and **4,180** with a fin. Two planes,
both missing: 7,700 / 8,400 cycles — **under 8.5% of the frame** either way. The
rendering is not the expensive part; the per-plane transform is, which is why
§3 spends effort removing multiplies from it and none on the rasteriser.

---

## 12. Phases

| # | Work | Notes |
| --- | --- | --- |
| 1 | `sprbuf` region, block allocation, pointer flipping | Verify VIC reads `$CE00` correctly before anything else |
| 2 | Row-run rasteriser + mask tables, driven by a hardcoded endpoint set | Testable in isolation with a static test pattern |
| 3 | Projection pipeline for one plane at a fixed world position | Fly around a parked aircraft and check the silhouette |
| 4 | Tier selection, X-expansion, two-sprite stacking, per-axis hysteresis | The two axes decide independently (§4) |
| 5 | Endpoint cache, double buffering | Measure the hit rate in level flight |
| 6 | Colour switching, second plane, priority ordering | |
| 7 | Canned kinematic paths | Separate document — behaviour, not graphics |
