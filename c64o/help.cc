#include "help.h"

#include <string.h>

#include "color.h"
#include "gfx.h"
#include "keys.h"
#include "mem.h"
#include "print.h"
#include "screen.h"
#include "vic.h"

// Keep in sync with the key handling in menu.cc and sim.cc, and
// with the "Controls" table in README.md.
static const char *const kHelpKeys[] = {
    "I J K L", "A S", "+ -", "Z X", "1 2 3", "N", "D",
    "M",       "R",   "T",   "F",   "P",     "Q", "H",
};
static const char *const kHelpDesc[] = {
    "ROLL AND PITCH",         "YAW",
    "THROTTLE UP/DOWN",       "MOVE FWD/BACK (WHEN PAUSED)",
    "LOOK LEFT/CENTER/RIGHT", "TOGGLE NAV POINT",
    "TOGGLE DEBUG VIEW",      "TOGGLE MAP VIEW",
    "RESTART MISSION",        "RESET TO ALT. START",
    "RESET TO MAX FUEL",      "PAUSE / RESUME",
    "QUIT TO MENU",           "SHOW THIS HELP SCREEN",
};
static const uint8_t kHelpCount = sizeof(kHelpKeys) / sizeof(kHelpKeys[0]);

static const uint8_t kHelpRowStart = 2;
static const uint8_t kHelpKeyCol = 2;
static const uint8_t kHelpDescCol = 12;

void help_run(void) {
  screen_enter_static_mccm();

  vic.color_border = kColorBlack;
  vic.color_back = kColorWhite;

  memset(kScreenRamMain, 32, 1000);
  memset(kColorRam, kColorBlack, 1000);

  print_str(0, 11, STRL("KEYBOARD CONTROLS"));
  for (uint8_t i = 0; i < kHelpCount; ++i) {
    print_str(kHelpRowStart + i, kHelpKeyCol, kHelpKeys[i],
              strlen(kHelpKeys[i]));
    print_str(kHelpRowStart + i, kHelpDescCol, kHelpDesc[i],
              strlen(kHelpDesc[i]));
  }
  print_str(kHelpRowStart + kHelpCount + 2, 2,
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
