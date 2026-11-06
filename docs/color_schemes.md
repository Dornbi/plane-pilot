# Viewport colour schemes (`color_schemes.md`)

A plan for ppilot missions to choose a colour scheme for everything in the
viewport: the sky and its gradient, the ground, the dots and objects on the
map, the clouds, the sun, the tail fin and the message line. The instrument
panel does not change.

*Planned 2026-11-07, not built yet.* The colours come from the scheme sheet
"C64 / Plane Pilot" (Earth, Bright, Desert, Mountain, Dusk, Night); four of
them are used here. §6 is the step-by-step implementation order.

**fpilot does not change.** Every scheme read goes through a macro that, with
`-D__FPILOT__`, is today's constant, so fpilot's code stays byte for byte what
it is now. §6 step 2 checks this.

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
| `ground` | `$D022` | 5 green | 10 lt red | 12 grey | 11 dk grey |
| `grndobj` | `$D023` | 8 orange | 8 | 8 | 15 lt grey |
| `cloud` | cloud sprites | 1 white | 1 | 1 | 12 grey |
| `sun` | sun sprite (the moon at night) | 7 yellow | 7 | 1 white | 1 white |
| `fin` | tail fin sprites | 12 grey | 12 | **11 dk grey** | **15 lt grey** |
| `text` | message line, colour RAM | 0 black | 0 | 0 | **1 white** |
| `dot[7]` | `KWorldDotColors` | today's | today's | today's | black, cyan→green, blue, yellow→red |
| `obj[9]` | `kWorldObjColors` | today's | today's | town and city yellow | town and city yellow, yellow fields red, **runway white** |

That is 9 bytes of colours and 16 of tables, 25 bytes a scheme.

The values in bold are the three fixes the scheme sheet does not cover:

1. **Message text, Night.** [msg.cc](../c64o/msg.cc) writes the message in
   black, which is invisible on Night's black sky. Night uses white.
2. **Runway, Night.** A black runway on dark grey ground is hard to find, and
   08 ends with a landing on runway 2. Night makes it white, a lit runway.
3. **Tail fin, Dusk and Night.** The fin is medium grey (12). Dusk's ground is
   12, so the fin disappears over it; dark grey (11) instead. Night's clouds
   are 12, so the fin looks like a cloud; light grey (15) instead.

### The colour RAM limit

In multicolour character mode colour RAM holds only colours 0 to 7 (bit 3 is
the multicolour flag). Every field that lands in colour RAM - `sky8`,
`grad1_8`, `text`, `dot` and `obj` - must be 0 to 7. `grad2`, `ground`,
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
                     cloud, sun, fin, text, dot[7], obj[9]; };
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
   and the runway at night. Check Desert's fin (12 on light red) in
   particular. Measure the frame time against
   [framerate.md](framerate.md) on a busy view.
7. **Docs.** Record the schemes in [missions.md](missions.md) and the RAM in
   [memory_map.md](memory_map.md), and add a section here on what was built.
