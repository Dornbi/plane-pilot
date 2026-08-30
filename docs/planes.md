# Other Aircraft — Traffic Sprites

**Status: designed, not built.** None of §12's phases has landed; there is no
traffic in `ppilot.prg`. The layer underneath it did ship, though — the sprite
stack of [sprite_objects.md](sprite_objects.md) §2 exists in `c64o/sprites.cc`
and serves the sun and the clouds, so §5's "hardware sprite indices" is a
matter of calling `sprites_stack_add()` rather than of writing an allocator.
Two of §5's numbers have gone stale since; the notes there say which.

This document specifies how other aircraft are drawn in the viewport. Scope is
**graphics only**: the rendering pipeline, sprite and RAM allocation, the
rasteriser and the caching scheme. Traffic behaviour (canned kinematic paths),
collision and the map screen are named where they touch the renderer but are
not designed here.

### Two reference implementations

| | |
| :--- | :--- |
| [planes-prototype.html](planes-prototype.html) | Interactive. Sliders for range, attitude and the model, showing the viewport and the sprite buffer side by side. For deciding how things should look. |
| [`lib/planes.py`](../lib/planes.py) | The same pipeline in Python, covered by [`tests/test_planes.py`](../tests/test_planes.py) (`make test`). For deciding whether they are still right. |

Both run the arithmetic the C64 runs — `fmul` and `fdiv` reproduce
`vec_fastmul8p8` and `vec_div8p8` including their truncation toward zero — so
the bytes they produce are the bytes `ppilot` should produce. They are
cross-checked against each other over 324 cases spanning distance, heading,
bank and pitch, and agree bit for bit — bitmaps, tier, thickness, clamp and
cycle count alike.

That cross-check has already earned its keep. It found 154 mismatched bitmaps
caused by nothing more than Python's `round` being banker's rounding while
JavaScript's `Math.round` is not — a discrepancy that would otherwise have sat
undetected in whichever of the two the C64 was written from.

### Relationship to `sprite_objects.md`

[sprite_objects.md](sprite_objects.md) is the layer underneath this document:
the general scheme for putting *any* world object — clouds, aircraft,
projectiles — into the eight hardware sprites, covering the raster band split,
index sharing with the instrument panel, distance sorting and the candidate
scan. **That document owns the sprite engine; this one owns what goes in an
aircraft's bitmap.** Everything here is built on its §1 and §2.

That engine is now real: `sprites_stack_reset()` / `_add()` / `_commit()` in
`c64o/sprites.cc`, designed in detail in [clouds.md](clouds.md) §1 and covered
by `c64o/test/sprites_test.cc`. An aircraft becomes a third client of it — one
`sprites_stack_add()` per plane, with the depth doing the priority work §5
below spells out by hand. Two consequences for §5: the stack hands out seven
indices rather than eight ([clouds.md](clouds.md) §1.9 keeps index 7 for the
panel band and the orientation mark), and it does not yet wrap a sprite round
the left edge (§1.6 there), so a plane leaving the left of the viewport will
pop the way a cloud does.

Three places where this document supersedes or corrects it:

| `sprite_objects.md` | Here |
| :--- | :--- |
| §3 ladder rung 3 is Y-expansion, "free, still one sprite" | Struck entirely — Y-expansion is not used by any object (§1). |
| §6.2 proposes 6–8 pre-rendered airframe bitmaps | Superseded — aircraft carry one 64-byte block, the far-tier dot (§1). |
| §5 measures 8 objects against a 19,705-cycle PAL frame and concludes it does not fit | Wrong denominator; the pipeline runs once per *sim* frame, ~98,500 cycles (§11). |

---

## 1. The core idea

Pre-rendered sprites are out: a plane's appearance depends on two viewing
angles plus size, and the combinations do not fit in RAM. But the sprite does
not need to be looked up, because **the silhouette is three straight lines and
the endpoints come straight out of the existing 3D math**.

An aircraft is modelled as five points — nose, tail, the two wingtips and the
top of the fin. Project them, then draw three lines into a sprite buffer and
point a hardware sprite at it: nose→tail, tip→tip, and tail→fin. The wing hub
sits **ahead of the centre**, not on it, so the tips carry a forward offset.

Perspective, foreshortening, bank and aspect all fall out of the projection.
There is no angle quantisation and no orientation table. The **only** static
bitmap in the whole feature is a single 64-byte block: the 2 × 2 dot the far
tier uses, where there is no silhouette left to draw (§4).

The feature is therefore code plus scratch RAM, not art.

### Hard constraints

These are settled and apply to every sprite in the viewport, aircraft or not:

- **Hires only. Never multicolour.** One colour per sprite, full horizontal
  resolution.
- **Never expand along Y.** X-expansion is available; `$D017` stays zero.
- **One fixed colour per aircraft**, chosen once — no background-dependent
  switching (§8).

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

**Traffic is drawn 1.5× oversize**, so an 11 m Cessna spans 16.5 m on screen.
At true scale a plane is under 4 px beyond 700 m, which is most of any
encounter — it would be a dot for the whole approach and only become an
aircraft in the last two seconds. The exaggeration buys back a factor of 1.5 in
every distance below, at the cost of realism, and traffic that cannot be
identified is not worth drawing.

| Span on screen | Distance | Time to close at 50 m/s |
| ---: | ---: | ---: |
| 1 px  | 4224 m | past the 4 km cull |
| 4 px  | 1056 m | 21 s |
| 24 px |  176 m | 3.5 s |
| 48 px |   88 m | 1.8 s |

Below about **94 m** the aircraft stops growing, and beyond about a kilometre
it is a fixed dot — both in §4. The band where the silhouette actually changes
shape is therefore roughly 100 m to 1 km, and **the dot is still the common
case**, which is why it gets a static bitmap rather than a rasterised one.

### Traffic does not use the terrain's units

`flight_eye_*` is 24.8 fixed point in metres and `world.cc` down-shifts by 9,
so the terrain grid works in **2 m units**. Traffic must not: the grid needs
kilometres of range and can afford metres of slop, while an aircraft a hundred
metres away needs the opposite.

At 100 m and a few degrees off the nose, a lateral offset is only a handful of
2 m units, so one unit of rounding is a large fraction of it — and the
projected centre is a ratio, so the error lands directly on screen. Measured
over a closing sweep from 400 m to 60 m:

| Relative-position unit | Worst frame-to-frame jump of the projected centre |
| :--- | ---: |
| 2 m, as the terrain grid uses | 7 px |
| **¼ m** | **1 px** |

A quarter metre reaches 4 km inside an int16 (16,000 units), which is beyond
the range cull, so it costs nothing but a different shift: `>> 6` instead of
`>> 9`. Since the pixel formula is a ratio of two lengths it is otherwise
unaffected.

---

## 3. Pipeline

Per plane, per frame:

1. **Relative position.** `P = target_world − flight_eye`, three 32-bit
   subtractions, then `>> 6` to **quarter-metre** units (int16) — not the
   terrain's `>> 9`. See §2.
2. **Cheap world-space reject** before spending a transform, per
   [sprite_objects.md](sprite_objects.md) §5.1: sign of `front · P` plus a
   Manhattan distance bound, roughly three multiplies. With only two planes
   this is nearly free either way, but it is the same code path the cloud
   candidate scan needs.
3. **To camera space.** `vec_transform_inv(&world_cam, &P, &C)` — 9 multiplies,
   already exists.
4. **Cull.** `C.x <= 64` (16 m — behind or on top of the camera) → skip.
   Range cull at `C.x > 16000` (4 km) → skip. Cull if the sprite would land
   in the message band while a message is up
   ([sprite_objects.md](sprite_objects.md) §7).
5. **Centre.** `vec_project_nocull()` → `cx = 160 − vec_sx`,
   `cy = 56 − vec_sy`.
6. **Perspective scale.** `k = 32768 / C.x` via `vec_div8p8(128, C.x)`. Model
   offsets are in eighths of a metre and `C.x` in quarters, so
   `px = 256·(O/8)/(C.x/4) = fmul(O, k)`. Then **clamp**: `k = min(k, kMax)`
   — see §4.
7. **Body axes in camera space.** `vec_transform3_inv(&world_cam, &R)` on the
   target's orientation — 27 multiplies, one existing call — giving `front`,
   `left` and `up` in camera space. Every model point is built from these three
   directions, so no point is transformed individually.
8. **Screen offsets, in two stages.** First reduce the five model dimensions
   to pixel half-extents — five multiplies, once per plane:

   ```
   pxH  = (vec_fastmul8p8(2·H,  k) + 1) >> 1    // half wingspan
   pxLN = (vec_fastmul8p8(2·LN, k) + 1) >> 1    // nose, 0.55·length
   pxLT = (vec_fastmul8p8(2·LT, k) + 1) >> 1    // tail, 0.45·length
   pxWF = (vec_fastmul8p8(2·WF, k) + 1) >> 1    // wing hub ahead of centre
   pxFN = (vec_fastmul8p8(2·FN, k) + 1) >> 1    // fin height
   ```

   Then each endpoint is one multiply per axis against a unit 8.8 vector:

   ```
   nose = (cx − fmul(fore.y, pxLN),  cy − fmul(fore.z, pxLN))
   tail = (cx + fmul(fore.y, pxLT),  cy + fmul(fore.z, pxLT))
   hub  = (cx − fmul(fore.y, pxWF),  cy − fmul(fore.z, pxWF))
   tipL = (hub.x − fmul(lat.y, pxH), hub.y − fmul(lat.z, pxH))
   tipR = (hub.x + fmul(lat.y, pxH), hub.y + fmul(lat.z, pxH))
   fin  = (tail.x − fmul(up.y, pxFN), tail.y − fmul(up.z, pxFN))
   ```

   The wing hub is one extra evaluation of the same expression as the nose,
   and both tips are measured from it rather than from the centre.
   The truncation is isolated in the five extents instead of in every point.

   **The doubling is not decoration.** `vec_fastmul8p8` truncates toward zero,
   and on a 12 px half-span that systematically loses up to a whole pixel — the prototype measured the
   silhouette rendering ~9% small before this was added. Doubling the input and
   halving the rounded result costs one shift and one add per extent and brings
   the worst-case error over the whole distance range down to 1.7 px of span.

   This is the first-order approximation — it ignores the change in `C.x`
   across the object, which is correct to within a fraction of a pixel for
   anything small enough to fit in a sprite, and it removes the near-plane
   clipping problem entirely.
9. **Tier selection** from the bounding box of the five screen points (§4).
10. **Cache check** on the ten local endpoint bytes (§7). Hit → skip to 12.
11. **Rasterise** into the back buffer: clear, three strokes (§6), flip pointer.
12. **Program the sprite(s)**: position, `$D010` MSB, `$D01D` expansion,
    colour (§8), pointer in both screen RAM copies.

### The model

| Dimension | Value | In ⅛ m, for a Cessna 172 |
| :--- | :--- | ---: |
| Half wingspan `H` | span / 2 | 44 |
| Nose `LN` | 0.55 · length | 37 |
| Tail `LT` | 0.45 · length | 30 |
| **Wing hub ahead of centre `WF`** | **0.20 · length** | **13** |
| Fin height `FN` | 0.13 · span | 11 |

**The wing sits 20% of the length ahead of the centre.** A wing centred on the
midpoint of the fuselage reads as a plus sign rather than an aeroplane; moving
it forward is what puts a tail on the shape. The offset is only visible from
three-quarter angles — head-on the fuselage is foreshortened to nothing and
side-on the wing is, so both hide it — but those angles are most of an
encounter. `WF` costs one extra evaluation of the same expression as the nose
(§3 step 8), and the tips are measured from the hub rather than from the
centre.

### Why ⅛ m model units

A Cessna's half-span is 5.5 m. In 2 m render units that is 2.75 — rounding it
to 3 is a 9% error in wingspan. Storing model offsets in ⅛ m keeps the half
span at 44 and the half length at 33, both comfortably int8, and the `k`
constant absorbs the unit change: `k = 4096 / C.x` makes
`vec_fastmul8p8(O_eighths, k)` come out in pixels.

Check: `C.x = 400` quarter-metres (100 m) → `k = 32768/400 = 81`;
`44 · 81 >> 8 = 13` px half-span, 27 px full span, and the rounding in step 8
recovers the last one. Direct formula: `256 · 11 / 100 = 28`. ✓

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

### The far tier is a static bitmap

Beyond about a kilometre an aircraft is under 4 px and there is no silhouette
left to draw, so it gets a fixed 2 × 2 blob: no buffer clear, no strokes, no
pointer flip, and no dynamic block. On the C64 it is one block in `$D400`,
written once at startup alongside the instrument needles, and the sprite
pointer is simply aimed at it.

This is the common case by a wide margin, and it costs **2,160 cycles against
2,655** for a rasterised frame — so the tier that skips almost everything this
document describes is the one that runs most of the time.

|  | ≤ 21 px tall | > 21 px tall |
| :--- | :--- | :--- |
| **≤ 24 px wide** | 1 sprite, 1:1 | 2 sprites, 1:1 |
| **> 24 px wide** | 1 sprite, X-expanded | 2 sprites, X-expanded |

A linear ladder gets this wrong in a specific and common way: a steeply banked
aircraft is **tall and narrow**, needs two sprites for its height, and would be
X-expanded along with them for no reason — throwing away horizontal resolution
on the axis that was never the problem. The prototype hits that case at any
bank past ~70°, which is not exotic.

**X-expansion only. Y-expansion is never used, by any object in the viewport
(§1).** Two reasons, and they point the same way:

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

[sprite_objects.md](sprite_objects.md) §3 originally listed Y-expansion as a
free third rung on the grounds that it costs nothing from the budget of eight
sprites. It costs nothing in *sprites* and a great deal in *shape*, and it has
since been struck from that document too — for clouds as well as aircraft.

The two-sprite case is two hardware sprites at the same X, 21 raster lines
apart, sharing expansion and colour. Rasterising is unchanged — the buffer is
simply 42 rows and the second sprite points at the second block.

### Below ~94 m the aircraft stops growing

There is no tier past 48 × 42, and cropping an aircraft that outgrows it shows
the *middle* of an aeroplane rather than an aeroplane. So instead of cropping,
**hold the apparent size**: cap `k`, which is exactly pretending the target
stopped approaching. Everything downstream — extents, thickness, tier — follows
`k`, so the whole silhouette freezes together with no special cases.

The cap is a **constant of the model, not a function of the current bounding
box**:

```
R    = max(halfSpan, nose − wingFwd, hypot(tail + wingFwd, fin))   // eighths
kMax = 46 · 128 / R                       // 46 is the buffer WIDTH
```

`R` is the furthest any model point can be from the wing hub, so every
projected point lies within `R · k / 128` pixels of it whatever the attitude.
For a Cessna at 1.5× (§2), `R` = 67 eighths and `kMax` = 87: the silhouette
freezes at about 45 px across, from roughly 94 m inward.

**Constant, not bounding-box-derived.** Scaling to the box fills the buffer
better — median 98% against 87% — but a bbox factor changes with attitude, so
the aircraft **changes size as it rotates**, which reads as breathing rather
than as a size limit. Measured across 11,664 attitudes in the clamped band, the
constant cap gives the projected scale exactly **one** value at every range.

**Width, not height.** `R` bounds both axes, so capping on the height (41)
would guarantee a fit at every attitude — but it would cost 15% of silhouette
in *all* of them to protect a handful of extreme ones. Capping on the width
(46) keeps that 15%; the price is that the height is no longer guaranteed. Past
about **73° of bank** the wingspan projects vertically into 41 rows of buffer
when the cap allows 45, and the tips clip by one or two pixels:

| Bank | Bounding box | |
| ---: | :--- | :--- |
| ≤ 70° | 14 × 40 | fits |
| 74° | 12 × 42 | tips clip 1 px |
| 89° | 6 × 44 | tips clip 2 px |

Swept over 113,400 attitudes at up to 60° of bank — well past anything canned
traffic will fly — **nothing clips at all**. Only the nose, tail and fin are
guaranteed in frame at every attitude; the wingtips are the deliberate
exception, and they clip symmetrically because the buffer is anchored on the
wing hub.

If the size ever needs to grow, more sprites in a *fixed* shape is not the way
— see the option below. Note also the interaction with the exaggeration: scaling the model up moves the
freeze point out by the same factor, from ~63 m at true scale to ~94 m at 1.5×.
If the freeze feels too early, the lever is the exaggeration, not the cap.

**Two off-by-ones live here**, both invisible until the clamp promised that
nothing clips:

- A bounding box of *extent* 21 spans 22 rows, so the usable extents are one
  less than the buffer, and one less again on the width. Hence
  `TIER_W, TIER_H = 23, 20` and `MAX_BBOX = 46 × 41`.
- Mapping a screen pixel to an X-expanded column must **floor**, not round: a
  column covers screen pixels `2c` and `2c+1`. Rounding biased both ends of a
  silhouette outward, which pushed a wingtip off the buffer whenever one
  reached the edge.

**Hysteresis**, per axis: promote at the limit, demote at 87% of it, so a plane
hovering at a threshold does not flicker between resolutions. Even so, the
1:1 → X-expanded switch doubles the pixel size and will visibly pop; this is
inherent to sprite expansion and is accepted.

### Option: switching layout by attitude

**Not implemented. Costed and recorded here so the obvious version does not get
built by mistake.**

The first instinct when the silhouette wants to be bigger is more sprites in a
bigger fixed rectangle. That mostly does not work, because a cap that holds at
every attitude is bounded by the **shorter** buffer dimension — the wingspan
can point either way — and a sprite is 21 px tall against 48 wide expanded. So
height is what binds, and buying width buys nothing:

| Fixed layout | Sprites | Buffer | Max silhouette |
| :--- | :---: | :--- | ---: |
| 1 × 2 (today) | 2 | 48 × 42 | 40 px |
| **2 × 2** | 4 | 96 × 42 | **40 px — four sprites, no gain** |
| 1 × 3 | 3 | 48 × 63 | 46 px |
| 1 × 4 | 4 | 48 × 84 | 46 px — no gain over 1 × 3 |

The lever that does work is **choosing the layout per attitude**. A level
aircraft is wide and flat and wants a row of sprites; a knife-edge one is tall
and narrow and wants a column. The layout is invisible — only the silhouette is
drawn — so the apparent size stays constant and nothing pops. Measured on the
five-point model over 4,914 attitudes:

| Budget | Max silhouette | Clamp range | vs today |
| :--- | ---: | ---: | ---: |
| today, 2 sprites, clips past 73° | 45 px | 94 m | — |
| switching, 3 sprites, **never clips** | 46 px | 90 m | +2% |
| switching, 4 sprites, **never clips** | 60 px | 69 m | +33% |
| switching, 6 sprites | 62 px | 67 m | +38% |

Three sprites buys away the knife-edge clipping at the same size; four buys a
third more aircraft. Six is not worth it. With four, the layouts fall out like
this:

| Attitude | Bounding box | Layout | Sprites |
| :--- | :--- | :--- | :---: |
| level, head-on | 60 × 8 | 2 × 1 X-expanded | 2 |
| level, oblique | 42 × 8 | 1 × 1 X-expanded | 1 |
| banked 30° | 52 × 30 | 2 × 2 X-expanded | 4 |
| banked 60° | 30 × 52 | 1 × 3 X-expanded | 3 |
| knife-edge | 8 × 60 | 1 × 3 X-expanded | 3 |

**What it costs.**

- **The rasteriser stops being three bytes wide.** A 2-wide layout is 48
  columns spanning *two* sprite blocks, and the two interleave by row — row `y`
  is bytes `3y..3y+2` of block A and of block B. `fill_run` has to split a run
  across both. The mask tables grow from 144 B to ~576 B, or stay at 24 columns
  with a second pass for the right-hand block.
- **Tier selection becomes a search**, not two independent axis decisions
  (§4). Roughly eight candidate layouts, picked smallest-first, with hysteresis
  per layout rather than per axis.
- **Scratch RAM roughly doubles**: 4 sprites double-buffered is 8 blocks for
  the near aircraft plus 4 for the far one, so the region grows from 512 B to
  1 KB — `$CC00–$CFFF` instead of `$CE00–$CFFF`.
- **Cycles roughly double** in the worst case, from clearing and stroking a
  96 × 42 buffer instead of 48 × 42. One close aircraft would be ~13,000
  cycles, 13% of a sim frame.
- **The sprite budget stops being static.** Four for the near aircraft, two for
  the far one and one for the sun is 7 of 8 — but only if the *second*
  aircraft is capped at two. Two simultaneous close aircraft would want nine.
  The allocation rule becomes "nearest gets the full budget, everyone else gets
  two", which the current fixed assignment (§5) does not need.

None of that is hard; it is a day's work rather than an afternoon, and it is
only worth spending when 45 px turns out to be too small on a real screen.

---

## 5. Memory and sprite allocation

### Sprite buffers must not live under I/O

The obvious home was the then-free `$D400–$D7BF` (15 blocks, pointers 80–94).
**Do not use it for dynamic buffers**, and it is no longer free in any case:
the cloud art took 81–94 and the orientation mark took 80, so all 48 blocks of
`$D400–$DFFF` are allocated.

The reason not to, which still stands whatever is in it: It is RAM under the SID, so every write needs
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

`$CE00–$CFFF` is 8 blocks, sprite pointers **56–63**, and cost 512 B of the
17.9 KB free when this was written.

> **Stale, twice over.** The title screen's aeroplane now lives at
> `$CF00–$CFFF` (`mem.h` `kTitleSpriteData`, pointers 60–63), so only
> `$CE00–$CEFF` — 4 blocks, pointers 56–59 — is available: enough for one plane
> double buffered, not two. And free allocatable RAM is 3,308 B in a largest run
> of 2,976 B ([memory_map.md](memory_map.md)), not 17.9 KB, so §5's ~1.5 KB
> budget is now half the headroom rather than a tenth of it. Either the title
> aeroplane moves or the block assignment below shrinks to one plane.

The **dot bitmap is the exception** and belongs under I/O with the other static
art: it is written once at startup, so the banking restriction costs nothing,
and traffic in the dot tier needs no dynamic block at all. Pointer 80, which
this section named for it, went to the orientation mark; the block would have to
come from somewhere in `$D400–$DFFF` or from the four in `$CE00`. Same division of labour as the
cloud bitmaps in [sprite_objects.md](sprite_objects.md) §6.1 — static art under
I/O, dynamic buffers in plain RAM.

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

**As built, none of this is a table.** Indices are not assigned by role at all:
each plane is one `sprites_stack_add()` call carrying its camera-space depth,
and `sprites_stack_commit()` sorts and hands out 0 upward. The sun passes
`INT16_MAX` and lands last by construction, and a plane nearer than a cloud
takes the lower index without anyone deciding it should. The one fixed index is
7, which the stack never hands out. The two panel-handler requirements below
are also done, in `sprites_show_panel_top_sprites()` and
`sprites_show_panel_bottom_sprites()`:

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
stroke(x0, y0, x1, y1, tv, th):
    clip the SEGMENT to the buffer rectangle      # never clamp - see below
    order the endpoints so y0 <= y1
    dy = y1 - y0
    if dy == 0:
        fill_run(y0, min(x0,x1), max(x0,x1))
        return
    steep = |dx| < dy
    slope = ((x1 - x0) << 8) / dy        # vec_div8p8
    xa = x0 << 8
    for y in y0 .. y1:
        xb = (y == y1) ? (x1 << 8) : xa + slope
        a, b = min(xa,xb) >> 8, max(xa,xb) >> 8
        if steep:  fill_run(y, a - th/2, a - th/2 + th - 1)
        else:      for r in 0 .. tv-1: fill_run(y - tv/2 + r, a, b)
        xa = xb
```

`fill_run(y, a, b)` ORs `kMaskFrom[a] & kMaskTo[b]` — three bytes — into
`buf + 3·y`. `kMaskFrom[a]` has bits `a..23` set, `kMaskTo[b]` has bits `0..b`
set; 144 bytes of table for both.

Properties that matter:

- **At most 42 iterations, usually far fewer.** Cost is bounded by the buffer
  height, not by line length.
- **Thickness is applied perpendicular to the stroke.** A shallow line repeats
  its run on `tv` rows; a steep line widens its run to `th` columns. Applying
  both to both would just lengthen the line rather than thicken it.
- **No special cases** for octant or direction beyond the `dy == 0` guard.

Estimated ~35 cycles for a row whose mask has to be built and ~15 for a
thickness repeat, which reuses the mask already in hand.

### Thickness

**Thickness is specified in screen pixels and the ladder is 1, 2, 4 — never
3.** The three strokes do not share an input, because they are not the same
kind of object.

#### The fuselage is a body of revolution; the wing is a plate

A fuselage looks the same width from every angle. A wing does not: face-on you
see its chord, edge-on you see almost nothing. So:

- **Fuselage — distance only.** Driven by the **unforeshortened wingspan in
  screen pixels**, `2 · pxH` = `256 · span / D`, which is a function of
  distance and the model and does not move when the aircraft rotates. Swept
  over every heading, bank and pitch at a fixed range, the reference reports
  exactly one body thickness.
- **Wing and fin — the projected chord.** Each is a flat plate whose apparent
  width is its chord projected perpendicular to its own screen direction.

The obvious input for all three would be the projected bounding box, since the
tier already needs it. It is the wrong one for anything: the box swings with
aspect, so a bbox-driven ladder changes stroke weight **while the aircraft
rotates**, which reads as a glitch rather than as level of detail.

#### Projecting the chord

With `F` the fuselage screen vector (nose − tail) and `S` the surface's screen
direction (tip → tip for the wing, tail → fin for the fin):

```
chord_px = |F.x·S.y − F.y·S.x| / |S| · (chord / length)
```

a 2D cross product over a length. `|S|` uses the octagonal approximation
(`max + min/2`), which is within ~4% and needs no square root. Face-on this
returns the full chord; edge-on it returns zero.

It inherits the parallel-projection approximation of §3 step 8, so it responds
to the relative orientation of aircraft and camera but not to where the target
sits in the field of view. That is consistent with the rest of the renderer.

#### The ladders

| Fuselage: wingspan on screen | | | Wing and fin: projected chord | |
| :--- | :---: | --- | :--- | :---: |
| < 12 px (D > ~235 m) | 1 | | < 2 px | 1 |
| < 48 px (D > ~59 m) | 2 | | < 4 px | 2 |
| otherwise | 4 | | otherwise | 4 |

`tv` is the row count and `th` the column count: `th = tv` at 1:1 and `tv / 2`
when X-expanded, since an expanded column is two screen pixels.

Two independent continuity constraints force the missing 3, and both were found
by sweeping the prototype rather than by reasoning:

1. **The steep/shallow test must be made in screen space, not buffer space.**
   In an X-expanded sprite one buffer column is two screen pixels, so a stroke
   that measures 45° in the buffer is really 26° on screen and wants row
   thickening, not column thickening. The test is `|dx| · xs < dy`. Getting
   this wrong produces a visible weight jump as an aircraft rotates through the
   crossover — the wing flips thickening mode at a heading where nothing else
   about it changes.
2. **At the crossover the two modes must weigh the same.** `th` columns weigh
   `th · xs` screen pixels against `tv` rows weighing `tv`, so `th · xs = tv`
   exactly. And across an X-expansion tier change the screen weight must not
   move, so `tv` cannot depend on `xs`. Both hold only if `tv` is divisible by
   every `xs` in use — that is, even.

**Hysteresis on every threshold, same 87% rule as the tier**, with a separate
latch per stroke. Without it an aircraft holding station at a boundary
alternates weight every frame, which is far worse than the one-time step it
protects against. The prototype flickered between 2 and 4 across three
consecutive samples before this was added.

The residual cost of the even-only ladder is that the 2 → 4 step doubles the
stroke weight in one go. It lands at 48 px of wingspan, where the
aircraft is already large enough that four pixels is 8% of the silhouette, so it reads as
an LOD change rather than a glitch. Accepting one clean doubling is the price
of removing the rotation artefact, which was the more objectionable of the two.

### Clip the segment; never clamp the endpoints

This is the one place where the obvious shortcut is actively wrong. The size
clamp (§4) means endpoints no longer land outside the buffer in normal
operation, but the rasteriser must not depend on that — and the reasoning is
worth keeping, because the failure it produces is so much worse than a crop.
When an endpoint does project outside the buffer: Clamping each endpoint into range pins all of them to
corners, and the result is not a cropped aircraft but **the two diagonals of
the buffer — a bare X, with the fin swallowed into the tail.** The shape stops
depending on orientation at exactly the moment the aircraft is most visible.

Clipping each segment against the buffer rectangle instead preserves the true
slope and gives an honest crop: the wing runs off both edges, the fuselage
still crosses at its real angle, the fin still points where it should. Cost is
a Liang–Barsky clip per stroke — four comparisons and at most two divides, and
only when an endpoint is actually outside.

### Centre on the wing when the silhouette overruns

**Superseded in normal operation by the size clamp (§4), and kept as a guard.**
With the clamp in place nothing overruns, so this path should never be taken;
it costs one comparison and it is the difference between a wrong picture and a
crash if a model is ever changed without re-deriving `kMax`.

The reasoning, for when it does run: once the silhouette is larger than the
buffer, centring the *box* means the fuselage — usually the longest thing on
screen — pushes the frame around and takes a wingtip out with it. The wing is
what makes the shape readable, so losing a tip to keep both ends of the
fuselage is the wrong trade.

```
fits   = bbox_w <= 24·xs  and  bbox_h <= rows
anchor = fits ? centre of the bounding box : the wing hub
```

The hub is the wingtips' midpoint, so anchoring there keeps the whole wing in
frame whenever the projected span fits, and lets the fuselage run off both
ends.

### Why there is a third stroke

Two strokes was the original choice, and for banked or climbing traffic it
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
silhouette cannot express at all. It is part of the spec; the prototype's
checkbox is left in only so the difference can be seen.

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

**One fixed colour, always.** No background test, no switching, no multicolour.

```c
vic.spr_color[idx] = kColorTraffic;      // kColorLightGray to start with
```

Light grey is the opening choice, medium grey the fallback if it proves too
close to the sky gradient. Both read against blue sky, the cyan/light-blue
gradient band and green ground without being as loud as white, which is the
instrument colour and wants to stay unambiguous.

An earlier draft switched colour on whether the aircraft was above or below the
eye's altitude — a genuinely cheap test, one 32-bit comparison, no projection.
It was dropped anyway: a target crossing the horizon would change colour
mid-manoeuvre, which draws the eye to the wrong thing, and the constant colour
is one less piece of state to keep consistent between the terrain and panel
raster handlers.

Colour is per sprite, so the two sprites of a stacked pair must both be set.

---

## 9. Known limitations

- **No occlusion against terrain.** Sprite-behind-background would hide the
  plane behind any non-background pixel of the dithered horizon, i.e. almost
  all of it. Planes are always drawn in front. Since the sun stays well above
  the horizon and planes outrank it in priority, nothing else needs handling.
- **The wingtips clip past ~73° of bank.** Deliberate, bounded at two pixels,
  and confined to attitudes canned traffic will not fly — see §4.
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

1. **Light grey or medium grey?** §8 starts at light grey. This is a look at a
   screenshot, not an argument.

That is the whole list.

Settled, and recorded here so they are not reopened: traffic is drawn **1.5×
oversize** (§2), the far tier uses a **static bitmap** (§4), the wing hub sits
**20% of the aircraft length ahead of the centre** (§3), the silhouette is **three
strokes** (§6), the colour is **one fixed value with no background test** (§8),
sprites are **hires and never Y-expanded** (§1), and the update rate is **every
sim frame** — [sprite_objects.md](sprite_objects.md) §5 is right that half-rate
projection reads as jitter, because camera rotation moves a stationary object
across the screen even when it is not moving.

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
| Three strokes with thickness, worst case | ~4,500 |
| Sprite registers and pointers | ~60 |
| — subtotal, cache miss | **~6,900** |

Swept over every heading, bank and distance in the prototype, the worst
single-plane frame is **6,900 cycles** — three strokes at thickness 4, 18 m
away, silhouette overrunning the buffer on every axis, 56 mask-built rows and
172 thickness repeats.

| Situation | Cycles | Share of a sim frame |
| :--- | ---: | ---: |
| Both in the dot tier | 4,320 | 4.4% |
| Both cached | 4,200 | 4.3% |
| One close and redrawing, one far and cached | 9,000 | 9.1% |
| Both close, both redrawing | 13,800 | 14.0% |

The last row is a near-collision with two aircraft simultaneously — momentary,
survivable, and not worth designing around. The middle row is the case to hold
in mind.

Thickness is what moved these numbers: it roughly tripled the rasteriser's
share. That is also why the repeated rows must be written as mask reuse rather
than as `tv` independent `fill_run` calls — at thickness 4 the difference is
~3,400 cycles, which is the margin between the table above and one that does
not fit.

Even so, the per-plane transform is still half the cost of a cached frame,
which is why §3 spends effort removing multiplies from it.

---

## 12. Phases

| # | Work | Notes |
| --- | --- | --- |
| 1 | `sprbuf` region, block allocation, pointer flipping | Verify VIC reads `$CE00` correctly before anything else |
| 2 | Row-run rasteriser + mask tables + segment clipping, driven by a hardcoded endpoint set | Check against `lib/planes.py`'s golden silhouettes. Clipping is not optional — see §6 |
| 3 | Projection pipeline for one plane at a fixed world position | Fly around a parked aircraft and check the silhouette |
| 4 | Tier selection, X-expansion, two-sprite stacking, per-axis hysteresis, size clamp | The two axes decide independently, and `kMax` is a constant (§4) |
| 5 | Endpoint cache, double buffering | Measure the hit rate in level flight |
| 6 | Second plane, priority ordering, both thickness ladders | Colour is a constant, so there is nothing to switch |
| 6b | Static dot block in `$D400`, far-tier short circuit | Skips the rasteriser in the most common case; worth doing early if frames are tight |
| 7 | Canned kinematic paths | Separate document — behaviour, not graphics |
| — | *Optional:* layout switching by attitude | §4. Only if 45 px proves too small: +33% silhouette for a day's work and a 48-column rasteriser |

Throughout: `make test` runs `tests/test_planes.py`, which pins the invariants
this document argues for — angle-invariant body thickness, the even ladder,
clip-not-clamp, the wing staying framed, and the cycle budget.
