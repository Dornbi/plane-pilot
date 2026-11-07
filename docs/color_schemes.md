# Viewport colour schemes (`color_schemes.md`)

A plan for ppilot missions to choose a colour scheme for everything in the
viewport: the sky and its gradient, the ground, the dots and objects on the
map, the clouds, the sun and the tail fin, and the map view (§8). The instrument
panel does not change.

*Planned and built 2026-11-07; §7 records what was built and where it
differs from the plan.* The colours come from the scheme sheet
"C64 / Plane Pilot" (Earth, Bright, Desert, Mountain, Dusk, Night); four of
them are used here. §6 is the step-by-step implementation order.

**fpilot does not change.** ppilot builds with `-D__ENABLE_COLOR_SCHEMES__`
(c64o/Makefile `COLOR_SCHEMES`); without it every scheme read is a macro for
today's constant, so fpilot, the demos and the host tests keep their code.

## 1. The four schemes and their missions

| Scheme | Missions |
|---|---|
| Earth (today's colours) | 01 Airborne, 02 Getting Down, 03 Solo Flight, 06 Lake Patrol, 09 Crop Duster |
| Desert | 04 Find the Runway, 05 Ferry Flight |
| Dusk | 07 Airshow, 10 Fuel Challenge |
| Night | 08 Aerial Recon |

Players fly the first three missions on the familiar look, and a new one comes
at 04, 07 and 08. 06 and 09 stay on Earth because lakes and fields are what
those missions are about, and they read best on green. 08 is the reason Night
exists: its targets are the cities, which Night lights up.

## 2. What a scheme holds

| Field | Where it goes | Earth | Desert | Dusk | Night |
|---|---|---|---|---|---|
| `sky8` | colour RAM, bit pair 11 (`| 0x08` stored) | 6 blue | 6 | 2 red | 0 black |
| `grad2` | `$D021`, upper gradient band | 14 lt blue | 14 | 10 lt red | 9 brown |
| `grad1_8` | colour RAM, horizon band (`| 0x08` stored) | 3 cyan | 3 | 7 yellow | 2 red |
| `ground` | `$D022` | 5 green | 8 orange | 12 grey | 11 dk grey |
| `grndobj` | `$D023`, the fields | 8 orange | 9 brown | 8 | 15 lt grey |
| `cloud` | cloud sprites | 1 white | 1 | 1 | 12 grey |
| `sun` | sun sprite (the moon at night) | 7 yellow | 7 | 1 white | 1 white |
| `fin` | tail fin sprites | 12 grey | 12 | **11 dk grey** | 12 grey |
| `dot[7]` | `KWorldDotColors` | today's | today's | today's | black, cyan→green, blue, yellow→red |
| `obj[9]` | `kWorldObjColors` | today's | today's | town and city yellow | town and city yellow, yellow fields red, **runway white** |

That is 8 bytes of colours and 16 of tables, 24 bytes a scheme.

The values in bold are fixes the scheme sheet does not cover; the first was
planned and then dropped:

1. **Message text, Night.** Planned as a `text` field, white at night, since
   black text looked invisible on the black sky. It was not (§7); the field
   is gone and the message line keeps its black.
2. **Runway, Night.** A black runway on dark grey ground is hard to find, and
   08 ends with a landing on runway 2. Night makes it white, a lit runway.
3. **Tail fin, Dusk.** The fin is medium grey (12). Dusk's ground is 12, so
   the fin disappears over it; dark grey (11) instead. Night's fin stays
   medium grey, the user's choice, although its clouds are the same grey
   (the plan had light grey, 15).

### The colour RAM limit

In multicolour character mode colour RAM holds only colours 0 to 7 (bit 3 is
the multicolour flag). Every field that lands in colour RAM - `sky8`,
`grad1_8`, `dot` and `obj` - must be 0 to 7. `grad2`, `ground`,
`grndobj` and the sprite colours can be any of the 16. All four schemes above
keep to this; a `static_assert` over the table (§6 step 5) keeps it that way.

## 3. RAM

ppilot only, roughly:

| Item | Bytes |
|---|---|
| The current scheme, `color_scheme` in bss | 25 |
| Four schemes in data, 4 × 25 | 100 |
| `kMissionScheme[10]` in pmission.cc | 10 |
| Code: about 15 sites from `#imm` to absolute, the copy at mission start, `box_invalidate()` | ~35 |
| **Total** | **~170** |

ppilot had 12,885 B free (`make pram`), so this is not a constraint. fpilot:
0 B.

Earth's two tables are exactly `KWorldDotColors` and `kWorldObjColors` as
they are today; the tables can move into the scheme data instead of being
kept beside it, which saves their 16 B.

## 4. Where the colours are read today

| Constant | Sites |
|---|---|
| `kColorSky` | [render.cc](../c64o/render.cc) `_fill_full_row` and both loops in the span fill; [msg.cc](../c64o/msg.cc) `msg_restore_color`; [box.cc](../c64o/box.cc) `box_prepare` (×2); [mem.cc](../c64o/mem.cc) colour buffer fill at boot |
| `kColorGrad1` | [box.cc](../c64o/box.cc) `box_prepare` (×3) |
| `kColorGrad2` | [mem.cc](../c64o/mem.cc) `mem_init_mccm`; [gfx.cc](../c64o/gfx.cc) `_gfx_switch_to_terrain` (every frame, in the raster split) |
| `kColorGround`, `kColorGrndObj` | [mem.cc](../c64o/mem.cc) `mem_init_mccm` |
| `kColorCloud` | [clouds.cc](../c64o/clouds.cc) `sprites_stack_add` call |
| `kColorSun` | [world.cc](../c64o/world.cc) sun `sprites_stack_add` call |
| `kColorAircraft` | [sprites.cc](../c64o/sprites.cc) tail fin |
| `kColorBlack` (message) | [msg.cc](../c64o/msg.cc) `msg_render` |
| `KWorldDotColors`, `kWorldObjColors` | [world.cc](../c64o/world.cc) dot and object draws |

`gfx_project_and_draw`'s `color == kColorGround` test stays as it is. That
value only marks a dot as a ground dot, which is drawn with the ground
characters and so gets its colour from `$D022`; it never reaches colour RAM.
Earth's `dot[MAP_DOT_GROUND]` must keep `kColorGround`, and so must every
other scheme's.

## 5. Design

**color.h** gets the scheme type and one macro per field:

```c
#ifdef __FPILOT__
#define COLOR_SKY8 (kColorSky | 0x08)
// ... one per field, each today's constant
#else
struct ColorScheme { uint8_t sky8, grad2, grad1_8, ground, grndobj,
                     cloud, sun, fin, dot[7], obj[9]; };
extern ColorScheme color_scheme;
#define COLOR_SKY8 (color_scheme.sky8)
// ...
#endif
```

The tables follow the same pattern: `WORLD_DOT_COLORS` is `KWorldDotColors`
in fpilot and `color_scheme.dot` in ppilot.

**pmission.cc** gets `kMissionScheme[kMissionCount]` and the four schemes,
`kColorSchemes[4]`. fmission.cc gets neither.

**Loading.** `_start_mission()` in [sim.cc](../c64o/sim.cc) copies
`kColorSchemes[kMissionScheme[m]]` into `color_scheme` and calls
`box_invalidate()`, so the horizon boxes' colour caches are rebuilt with the
new colours. It already runs before `mem_init_mccm()` in
`_enter_simulation()`, which sets `$D022` and `$D023`; returning from the map
calls `mem_init_mccm()` again ([screen.cc](../c64o/screen.cc)), so both stay
right. R restarts the same mission, so its scheme does not change.

**The colour buffer fill at boot** ([mem.cc](../c64o/mem.cc)) runs before any
mission. Check whether every frame rewrites the whole buffer; if it does, the
fill can stay on the constant, and if not, `_start_mission()` refills it.

**Speed.** The span fill in [render.cc](../c64o/render.cc) stores the sky
colour once per cell. With the colour in memory that is a cycle a cell more
if oscar64 reloads it inside the loop; check `ppilot.asm` and hoist it into a
local if it does.

## 6. Implementation order

Each step builds both games and runs the existing tests.

1. **The scheme, Earth only.** Add `ColorScheme`, `color_scheme`,
   `kColorSchemes` with Earth alone, `kMissionScheme` all 0, and the copy and
   `box_invalidate()` in `_start_mission()`. Nothing reads the scheme yet.
2. **Read the colours from the scheme.** Replace every site in §4 with its
   macro. ppilot must look exactly as before on every mission, and
   `fpilot.prg` must be byte-identical to the build before this step (keep a
   copy and `cmp` it).
3. **The tables.** Move `KWorldDotColors` and `kWorldObjColors` behind their
   macros, into the scheme for ppilot. ppilot still looks as before.
4. **Text and fin.** Read the message colour and the tail fin colour from the
   scheme. Still Earth, still unchanged.
5. **The other three schemes.** Add Desert, Dusk and Night with the values in
   §2, a `static_assert` that every colour RAM field is below 8, and the
   mission assignment in §1.
6. **Look at it.** Fly each scheme in VICE and take screenshots: the gradient
   bands, the fin over ground and over sky, clouds, the sun, the message line,
   and the runway at night. Check Desert's fin (12 on orange) in
   particular. Measure the frame time against
   [framerate.md](framerate.md) on a busy view.
7. **Docs.** Record the schemes in [missions.md](missions.md) and the RAM in
   [memory_map.md](memory_map.md), and add a section here on what was built.

## 7. What was built

All of §6 in one change. Where it differs from the plan:

- **A build flag, not `__FPILOT__`.** `__ENABLE_COLOR_SCHEMES__` is set for
  ppilot.prg only. The demos and the host tests build without it, so they
  need no `color_scheme` of their own and still test the constant colours.
- **The ground-dot marker moved to 8.** `gfx_project_and_draw()` recognised a
  ground dot by `kColorGround`, green - and Night's cyan dots are green, so
  they would have been drawn as ground. The marker is now `kGfxDotGround`,
  8, which is no colour RAM colour (gfx.h), in both games. That is the whole
  of fpilot's change: the same size, two bytes different, `cmp #5` and the
  table's 5 now 8.
- **Earth's tables replaced the old ones.** ppilot leaves `KWorldDotColors`
  and `kWorldObjColors` out (world_map.cc) and draws from the scheme through
  `WORLD_DOT_COLORS` / `WORLD_OBJ_COLORS` (world.h).
- **No static_assert on the colour RAM fields.** The table is plain const data
  oscar64 cannot assert over; pmission.cc says the rule next to it.
- **The boot fill stays on the constant.** Every frame rewrites the colour of
  every sky and box cell, and ground cells never read theirs, so the fill
  before the first mission does not matter.
- **The message line.** The font is inverted: the letters are pixel pair 00,
  `$D021` (`grad2`), and the scheme's `text` colour fills the box around them.
  So black text was never invisible at night - it is brown letters on the
  black sky, a box that vanishes. White, as planned, is brown letters in a
  white box. Both read; the user chose black, which every scheme then had,
  so the `text` field was dropped and msg.cc writes `kColorBlack` again.

**Desert's ground, after:** orange rather than the sheet's light red, at
the user's choice. Its fields, `grndobj`, are brown with it, as in the
sheet's Mountain: orange like Earth's, they would be the ground's own colour.

**RAM:** ppilot 52,288 → 52,445 B used, +157 B (`make pram`); fpilot
unchanged.

**Speed:** the span fill in render.cc now loads the sky colour from memory,
`LDA abs` for `LDA #`, two cycles a cell. Only the partly-sky rows of a banked
view pay it, at most 14 × 40 cells, so ≤ 1,120 cycles a frame and usually
under half of that. The full rows go through `_fill_line()`, which loads the
value once. Not measured on target: tools/bench.sh is fpilot's.

**Checked:** Earth's three posed scenes (final, bank, lake) are
pixel-identical to the build before the change. All four schemes were
captured in those scenes and in the back view with a message up
(`SHOT_CFLAGS="-DSHOT_SCHEME=n -DSHOT_BACK -DSHOT_MSG"
tools/make_shots.sh`, tools/shot.cc). The posed scenes pin Earth by default,
so screens/ keeps showing it although they start mission 04, now Desert.

## 8. The map view

Added after §7. The map (map.cc, [map.md](map.md)) is a multicolour bitmap
of its own, so it took none of the view's colours: green ground in `$D021`
and two colours per tile, `kMapTileColors` (mapdefs.cc, generated from
gfx/ppilot_map_tiles.png). With schemes, `$D021` is the scheme's `ground`
and the tiles' colours are `ColorScheme::map[18]`, Earth's being
`kMapTileColors` exactly. fpilot reads `kMapTileColors` and green as before
(map.cc `MAP_TILE_COLORS`), byte-identical. In a bitmap colour RAM has all
16 colours, so the map has no 0-to-7 limit.

Each scheme's tiles follow its view, and a tile that would vanish into the
ground is moved off it:

| Tile | Earth | Desert | Dusk | Night |
|---|---|---|---|---|
| ground (`$D021`) | green | orange | grey | dark grey |
| grid | grey | grey | dark grey | grey |
| white dots | light grey | light grey | white | light grey |
| cyan dots | cyan | cyan | cyan | green |
| yellow dots | light green, yellow | same | same | green, red |
| runway | black, grey | same | black, light grey | black, light grey |
| fields | orange, grey | brown, grey | orange, light grey | light grey, grey |
| yellow fields | yellow, light grey | same | same | red, grey |
| towns, cities | orange, black | brown, black | orange, yellow | light grey, yellow |

Night's runway is black on the map although it is white in the view: the
navpoint digit on it is white (bit pair 01), and on a white runway it was
gone. The white path, the digits, the black surround with its compass
letters and the light green aircraft marker are the same in every scheme.

**RAM:** ppilot +73 B (52,459 → 52,532 used, against HEAD built the same
way). **Checked:** the map scene in all four schemes
(`SHOT_CFLAGS=-DSHOT_SCHEME=n tools/make_shots.sh map`; the scene now
applies `SHOT_SCHEME`, Earth by default, before pressing M).

