#include "help.h"

#include "gfx.h"
#include "keys.h"
#include "mem.h"
#include "print.h"
#include "screen.h"

// The routines below only run on screen transitions (menu, help, map) or at
// startup, never inside the per-frame simulation loop, so the outliner's
// size-for-a-JSR trade costs nothing that matters here. It stays off
// globally so the renderer and the raster IRQ handlers keep their
// straight-line code.
#pragma optimize(push, outline)


// Keep in sync with the key handling in menu.cc and sim.cc, and
// with the "Controls" table in README.md.
//
// One blob rather than parallel key/description arrays: print_lines() walks
// it directly, so there is no pointer table and no per-row loop below. The
// key column is space-padded so the descriptions line up in one column.
// clang-format off
static const char kHelpText[] =
    "I J K L   ROLL AND PITCH\n"
    "A S       YAW\n"
    "+ -       THROTTLE UP/DOWN\n"
    "Z X       MOVE FWD/BACK (WHEN PAUSED)\n"
    "1 2 3     LOOK LEFT/CENTER/RIGHT\n"
    "N         TOGGLE NAV POINT\n"
    "D         TOGGLE DEBUG VIEW\n"
    "M         TOGGLE MAP VIEW\n"
    "R         RESTART MISSION\n"
    "T         RESET TO ALT. START\n"
    "F         RESET TO MAX FUEL\n"
    "P         PAUSE / RESUME\n"
    "Q         QUIT TO MENU\n"
    "H         SHOW THIS HELP SCREEN";
// clang-format on

static const uint8_t kHelpRowStart = 2;
static const uint8_t kHelpKeyCol = 2;

void help_run(void) {
  screen_begin_text_page();

  print_str(0, 11, STRL("KEYBOARD CONTROLS"));
  const uint8_t rows = print_lines(kHelpRowStart, kHelpKeyCol, kHelpText);
  print_str(kHelpRowStart + rows + 2, kHelpKeyCol,
            STRL("RETURN OR SPACE TO GO BACK"));

  while (1) {
    keyb_poll();
    if (key_pressed(KSCAN_RETURN) || key_pressed(KSCAN_SPACE) ||
        key_pressed(KSCAN_Q)) {
      break;
    }
    gfx_wait_vsync();
  }
  while (key_pressed(KSCAN_RETURN) || key_pressed(KSCAN_SPACE) ||
         key_pressed(KSCAN_Q)) {
    keyb_poll();
  }
}

#pragma optimize(pop)
