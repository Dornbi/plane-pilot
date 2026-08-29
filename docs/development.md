# Plane Pilot

## Background

Plane Pilot came to be from an idea to vibe-code a game demo on C64 using an agent.
At the end it is probably 50-60% was vibe coded using [Antigravity](https://antigravity.google/) and Gemini.

About the “game”: the goal was to create something a different than most C64 games.
There are many great platformers, scrolling, and shooting games on the platform.
Plane Pilot is something the C64 is not great for: a “3D” flight sim, with reasonable frame rate.

## Compiling the C64 code

The C64 code is mostly written in C. The files have a .cc extenstion because they
use some C++ features. oscar64 can optimize code pretty well, so only the most critical parts
are written in assembly.

Compiling the C64 code needs the install the [oscar64](https://github.com/drmortalwombat/oscar64/blob/main/README.md) cross-compiler and `make`. You may need to adjust `OSCAR64_INCLUDE` in the [c64o/Makefile](../c64o/Makefile).

```bash
make prg
```

If everything goes well it builds these executables into `c64o/`:

- `ppilot.prg`: The game. Sound, music and the debug view behind `D`.
- `polydemo.prg`: Polygon rendering prototype.
- `vecdemo.prg`: Simple character mode prototype of the dots on the ground.
- `vectest.prg`: Correctness test and cycle count for 3D vector operations.

`ppilot.prg` is one binary: sound effects, the title tune and the `D` debug
view with its per-stage cycle counters all ship in it. Three make variables
each take a feature back out, which is how what a feature costs gets measured
rather than guessed:

```bash
make -C c64o SOUND=      # no sound effects, no music
make -C c64o DEBUG=      # no debug view, no benchmark counters
make -C c64o CLOUDS=     # no clouds
```

## Running a build in an emulator

`x64sc` runs the real `.prg` headlessly, which is how the cycle counts and the
pixel-identical comparisons in [clouds.md](clouds.md) were arrived at rather
than argued about. `tools/vice_shot.sh` takes a screenshot, `tools/vice_dump.sh`
reads a number out of memory. See [emulator.md](emulator.md) for the
method and [framerate.md](framerate.md) for what the frame is currently
spent on.

## Releasing a build

`c64o/*.prg` is build output and gitignored. The copies in [bin/](../bin) are the
checked-in ones that README links to for the online emulators, so a build only
reaches anyone after it is published there:

```bash
make release
```

This builds and then copies the four `.prg` files into `bin/`, reporting which
ones actually changed. Check the result with `git status bin/` and commit it
along with the source change that produced it — otherwise the downloadable
binary drifts behind the code.

## Debug info

With the `D` key the instrument panel shows debug info instead of the instrument panel.

The left hand side show the state of the plane:

| Label          | Value        |
| -------------- | ------------ |
| `FX` `FY` `FZ` | Front vector |
| `LX` `LY` `LZ` | Left vector  |
| `UX` `UY` `UZ` | Up vector    |
| `EX` `EY` `EZ` | Eye position |

The cycle counters run down the right hand side, in the order the frame
executes them, with `UPD` and its two sub-counters in a second column:

| Label | Value                                        |
| ----- | -------------------------------------------- |
| `MDL` | Model the plane state (motion etc.)          |
| `SNP` | "Snap" the view vector to screen coordinates |
| `BGR` | Draw the background without the sky gradient |
| `CHR` | Copy the relevant characters to char RAM     |
| `DRW` | Draw the tiles                               |
| `GRD` | Draw the grid dots on the ground             |
| `COL` | Copy to the color RAM                        |
| `UPD` | Camera, roll state, cloud scan, sprite stack |
| `TOT` | The sum of the eight stages above            |

`TOT` is the sum of those eight and nothing else. The key scan, the panel
instruments, `sound_update()` and the wait for the flip window are outside
every counter, so `TOT` is less than the frame period rather than equal to it.

One wrinkle when checking that sum by hand: `bm_total()` prints and clears the
accumulator *before* `mem_switch_buffer()` runs, so the `COL` inside `TOT` is
the previous frame's, not the one displayed beside it. They are usually the
same value and the column adds up exactly; when `COL` moves between frames the
sum reads a few dozen cycles out, and that is why.

Three more counters are a **breakdown** of two of the stages above rather than
extra terms beside them, so they are never added into `TOT`. Each sits directly
under its parent: `PLY` below `GRD` in the right hand column, `CLD` and `SPR`
below `UPD` in the second one.

| Label | Value                                                 |
| ----- | ----------------------------------------------------- |
| `PLY` | `poly_draw_3d()` over the whole frame - part of `GRD` |
| `CLD` | `clouds_add_candidates()` - part of `UPD`             |
| `SPR` | `sprites_stack_commit()` - part of `UPD`              |

`PLY` accumulates, since the grid walk calls it once per object on screen. The
other two are single calls. All three print five digits, so a stage that ever
passed 99,999 cycles in one frame would wrap silently - `PLY` is the only one
near that, and the largest yet seen is 43,064.

Two readings to calibrate against, both in `x64sc` with the aircraft frozen and
the debug view up:

| pose | `MDL` | `SNP` | `BGR` | `CHR` | `DRW` | `GRD` | `PLY` | `COL` | `UPD` | `CLD` | `SPR` | `TOT` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| mission 01, on the runway | 2,721 | 1,067 | 6,755 | 405 | 13,826 | 81,236 | **43,064** | 7,899 | 17,653 | 11,761 | 725 | 131,562 |
| mission 02, on final | 3,841 | 924 | 5,304 | 475 | 12,922 | 52,522 | 15,597 | 7,511 | 19,559 | 13,430 | 989 | 103,095 |

The runway row reconciles to the cycle; the other is 37 out, which is the `COL`
lag above. `PLY` is inside `GRD` and `CLD` + `SPR` inside `UPD` in both. On the
runway the single runway polygon is 53% of the grid walk and a third of
everything measured, which is the finding [framerate.md](framerate.md) arrived
at by hand and this now reports live.

`CHR` is small in both because a frozen pose keeps the same box definition
every frame and `box_prepare()` takes its early return - 405 cycles of
`boxdef_set_main()` copying a `boxdef_t`, and nothing more. Rolling, where the
definition changes every frame, it runs 10,000 to 15,000.

![Debug info](../screens/debug_crt.png)

## Python prototype and scripts

The Python library is in [lib/](../lib), and the command line tools that drive it
are in [tools/](../tools). The most important ones:

- [generate_frame.py](../tools/generate_frame.py): Generates a single reference frame as PNG.

- [generate_all.py](../tools/generate_all.py): Generates reference frames that match the C64 graphics
  capabilities at all roll angles as PNG, and turns them into:
  - [chardefs.py](../lib/chardefs.py): The character set used to render the sky gradient.
  - [boxdefs.py](../lib/boxdefs.py): The tiles used to render the sky gradient.

- [render_frame.py](../tools/render_frame.py): Renders a single frame using the generated chardefs and boxdefs.

- [render_all.py](../tools/render_all.py): Renders frame using the generated chardefs and boxdefs.

- [flight_demo.py](../tools/flight_demo.py): A more interactive demo to test roll and pitch usiing
  the chardefs and boxdefs.

- [generate_sprites.py](../tools/generate_sprites.py): Generates sprite data for the C64 code.

- [generate_map_tiles.py](../tools/generate_map_tiles.py): Turns the map view's
  tile sheet, [gfx/ppilot_map_tiles.png](../gfx/ppilot_map_tiles.png), into
  `c64o/mapdefs.{cc,h}`. The PNG is the source of truth — edit it in GIMP and
  re-run `make map-tiles`. See [map.md](map.md) for the format and the color
  budget the generator enforces.

- [make_map_tiles_draft.py](../tools/make_map_tiles_draft.py): Lays down the
  first version of that tile sheet from ASCII art. It refuses to overwrite an
  existing sheet without `--force`, since the PNG outranks it once drawn.

- [render_map_preview.py](../tools/render_map_preview.py): Composites the
  generated tiles over `kWorldMap` to `out/map_preview.png`, so the tile sheet
  can be judged as a whole map. A verification tool, not part of the build.

The tools take their canonical flags from the [Makefile](../Makefile) in the repo
root, so prefer the make targets over calling the scripts by hand:

```bash
make data        # regenerate chardefs, boxdefs, gfx chars, sprites, map tiles
make map-tiles   # just the map tiles, after editing the tile sheet
make map-preview # render out/map_preview.png from the current tiles
make render      # render all roll angles to out/
make prg         # build the C64 binaries via c64o/Makefile
make help        # list everything
```

### Python dependencies

The scripts target a plain Python 3 install plus:

| Package  | Needed by                                              |
| -------- | ------------------------------------------------------ |
| `pillow` | everything that reads or writes a PNG — most of `lib/` and `tools/` |
| `pytest` | `make test`                                            |
| `pygame` | `make demo` only                                       |

```bash
pip install pillow pytest pygame
```

Generated images and frames are written to `out/`, which is gitignored. The
scripts anchor their own paths to the repo root, so they work from any directory.

There are some unit tests in the [tests](../tests) directory. You can run them with:

```bash
make test
```

This is an example reference frame from [generate_frame.py](../tools/generate_frame.py) for roll `r16u1`:

![Reference frame](../screens/flight_frame_c160_96_01_r16u1.png)

## Design conisderations

Rendering the horizon gradient at a reasonable frame rate has been an important consideration
from the start. To speed things up, it uses multicolor charater mode (MCCM), with pre-rendered
characters and tiles for the 60 fixed roll angles. Each tile is somewhere between 4x5 and 6x16
characters, and each tile uses up to 32 unique characters. All in all:

- There are 60 roll angles.
- There are 68 `boxdef`s: 60 for one of each roll angle,
  and 8 alternate ones with some of the angles. For example for the horizontal horizon,
  the main `boxdef` aligns with the character boundary, while the alternate goes through
  the middle of the character.
- The sum of tile size for the 68 `boxdef`s 3268 characters.
- They use 333 unique characters across all `boxdef`s. Since this is more than 256,
  the characters for the current `boxdef` are constantly being copied into the character RAM
  (up to 32 chars at a time).

Another consideration was the colors to use. In MCCM, 3 colors are fixed for the whole screen,
and the fourth color can be customized using the color RAM. To achieve a reasonable gradient,
4 colors are used: green, cyan, light blue and blue. Sticking with these 4 colors in the entire
viewport would make things easier, but also would make the entire color
space a bit flat. Instead, it uses a fifth color (orange) for the dots
on the ground. Since the color ram is a fixed address ad $D800, this
means it has to be copied to the VIC II chip every frame. With the current
scheme other objects using the first 8 colors of the paletter would
be relatively easy to add, for example black or white.

Double buffering is achieved by switching the screen RAM, and copying
the color RAM at every frame. In addition, alternate frames use a different
region of the character RAM, with 32 characters each.

The instrument panel is rendered in multicolor bitmap model (MCBM).
The switch between the two modes is done at a raster interrupt.

The executable is 27 kilobytes. At runtime there is about 22 kilobyte ram still free,
so the only bottleneck to turn this into a real game is the token limit at human time.
