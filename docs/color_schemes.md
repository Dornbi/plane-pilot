# Per-Mission Colour Schemes (`color_schemes.md`)

Shipped in **`ppilot.prg` only** behind `-D__ENABLE_COLOR_SCHEMES__` (`make -C c64o COLOR_SCHEMES=` disables it). `fpilot.prg`, demos, and host tests compile all `COLOR_*` macros to the Earth palette constants (`c64o/color.h`, `c64o/world.h`, `c64o/map.cc`).

`kGfxDotGround = 8` (a dedicated sentinel separate from colour indices `0..7`) in both builds so Night's green dots do not collide with ground-coloured dots.

---

## 1. Mission-to-scheme mapping (`c64o/pmission.cc` `kMissionScheme`)

| Scheme | Missions |
| --- | --- |
| **Earth** (0) | 01 Airborne, 02 Getting Down, 03 Solo Flight, 06 Lake Patrol, 09 Crop Duster |
| **Desert** (1) | 04 Find the Runway, 05 Ferry Flight |
| **Dusk** (2) | 07 Airshow, 10 Fuel Challenge |
| **Night** (3) | 08 Aerial Recon |

---

## 2. Viewport palettes (`kColorSchemes[4]` in `pmission.cc`)

In MCCM, colour-RAM slots (`sky8`, `grad1_8`, `dot[]`, `obj[]`) are limited to C64 colours `0..7` (bit 3 = multicolour flag `| 0x08`). Status messages use the inverted font on a black strip, so message text renders in `$D021` (`grad2`).

| Field | Earth (0) | Desert (1) | Dusk (2) | Night (3) |
| --- | --- | --- | --- | --- |
| `sky8` (col RAM) | `6` blue `\| 8` | `6` blue `\| 8` | **`4` purple `\| 8`** | `0` black `\| 8` |
| `grad1_8` (col RAM) | `3` cyan `\| 8` | `7` yellow `\| 8` | `7` yellow `\| 8` | `2` red `\| 8` |
| `grad2` (`$D021`) | `14` lt blue | `15` lt grey | `10` lt red | **`11` dk grey** |
| `ground` (`$D022`) | `5` green | `7` yellow | **`11` dk grey** | **`9` brown** |
| `grndobj` (`$D023`) | `8` orange | `9` brown | `8` orange | `15` lt grey |
| `cloud` (sprite) | `1` white | `1` white | `1` white | `12` med grey |
| `sun` (sprite) | `7` yellow | `7` yellow | `1` white | `1` white |
| `fin` (sprite, offset 7) | `15` lt grey | `15` lt grey | **`12` med grey** | `12` med grey |
| `dot[0..6]` tweaks | default | default | default | cyan→green, yellow→red |
| `obj[0..8]` tweaks | default | default | town/city→yellow | yellow fields→red, town/city→yellow, **runway stays `0` black** |

---

## 3. Map view palettes (`ColorScheme::map[18]`)

The map view ([map.md](map.md)) uses `COLOR_GROUND` for `$D021` (bit pair `00`) and `MAP_TILE_COLORS[tile]` for per-tile pairs `10`/`11`. White (`01`, flight path / navpoint digits / compass) and light green (aircraft marker) stay constant across all schemes.

| Scheme | Map `$D021` (`ground`) | Notable tile-colour overrides |
| --- | --- | --- |
| **Earth** | `5` green | Default `kMapTileColors` |
| **Desert** | `7` yellow | Brown/orange sand & muted grid |
| **Dusk** | **`11` dk grey** | Dark-grey grid (`0xB0`), warm towns/cities |
| **Night** | **`9` brown** | Black runway with light-grey markings (keeps white navpoint digits visible), dimmed fields/water |

---

## 4. Implementation & RAM footprint

- **Struct (`c64o/color.h`):** `ColorScheme` is **42 bytes**: 8 scalar fields (`sky8`, `grad1_8`, `grad2`, `ground`, `grndobj`, `cloud`, `sun`, `fin`) + `dot[7]` + `obj[9]` + `map[18]`. `fin` at byte offset 7 is pinned by `static_assert` in `c64o/sprites_asm.cc`.
- **Storage (`c64o/pmission.cc`):** `kColorSchemes[4]` (168 B data) + active `color_scheme` (42 B BSS) + `kMissionScheme[10]` (10 B byte offsets `0, 42, 84, 126`) = **220 B** data/BSS.
- **Loading (`sim.cc:_start_mission()`):** Copies the mission's 42-byte scheme into `color_scheme`, calls `box_invalidate()`, and resets `render_sky_rows = 0` so sky colour RAM refreshes cleanly.
- **Read sites:** `render.cc` / `render_asm.cc` (`COLOR_SKY8`, hoisted once per `_render_flush_span`), `box.cc` (`COLOR_GRAD1_8`), `mem.cc` (`COLOR_GROUND`, `COLOR_GRNDOBJ`), `gfx.cc` (`COLOR_GRAD2`), `msg.cc`, `clouds.cc` (`COLOR_CLOUD`), `world.cc` (`COLOR_SUN`, `COLOR_DOT`, `COLOR_OBJ`), `sprites.cc` / `sprites_asm.cc` (`COLOR_FIN` at `color_scheme + 7`), and `map.cc` (`COLOR_GROUND`, `MAP_TILE_COLORS`).
- **Previewing:** `tools/shot.cc` accepts `-DSHOT_SCHEME=<0..3>` to render any pose under a specific scheme.
