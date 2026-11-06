#include "help.h"

#include "gfx.h"
#include "keys.h"
#include "music.h"
#include "opponent.h"
#include "print.h"
#include "screen.h"
#include "sound.h"

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
// it directly, so there is no pointer table and no per-row loop below. A
// '\t' after each key pads it to print_lines()' kPrintTabCol, so the
// descriptions line up in one column without the padding stored, and the
// words print.h has, its PRINT_*, are a byte each.
// clang-format off
static const char kHelpText[] =
    "I J K L\tROLL" PRINT_AND "PITCH\n"
    "A S\tYAW\n"
    "+ -\tTHROTTLE UP/DOWN\n"
    "F\t" PRINT_TOGGLE "FLAPS\n"
    "G\t" PRINT_TOGGLE "GEAR\n"
    "B\tBRAKE ON GROUND\n"
#ifdef __ENABLE_OPPONENT__
    "SPACE\tFIRE\n"
#endif
    "\n"
    "1 2 3 4\tLOOK LEFT/FWD/RIGHT/BACK\n"
    "N\t" PRINT_TOGGLE "NAV POINT\n"
    "M\t" PRINT_TOGGLE "MAP VIEW\n"
    "P\tPAUSE / RESUME\n"
#ifdef __ENABLE_SOUND__
    "V\tSOUND FULL / LOW / OFF\n"
#endif
    "R\tRESTART MISSION\n"
    "Q\tQUIT TO MENU\n"
#ifdef __ENABLE_DEBUG__
    "\n"
    "D\t" PRINT_TOGGLE "DEBUG VIEW\n"
#endif
    "\n"
    "H\tSHOW THIS HELP SCREEN";
// clang-format on

// The keys that leave, then V: entry i is bit i of keys_scan()'s answer.
static const uint8_t kHelpKeys[] = {
    KSCAN_RETURN, KSCAN_SPACE, KSCAN_Q, KSCAN_H,
#ifdef __ENABLE_SOUND__
    KSCAN_V,
#endif
};
static const uint8_t kHelpKeyCount = sizeof(kHelpKeys);
static const uint8_t kHelpKeysExitCount = 4;
static const uint8_t kHelpKeysExit = (1 << kHelpKeysExitCount) - 1;
#ifdef __ENABLE_SOUND__
static const uint8_t kHelpKeyV = 1 << kHelpKeysExitCount;
static_assert(kHelpKeyCount == kHelpKeysExitCount + 1, "V follows the exits");
#endif

static const uint8_t kHelpRowStart = 2;
static const uint8_t kHelpKeyCol = 2;

void help_run(void) {
  screen_begin_text_page();

  print_str(0, 11, STRL("KEYBOARD CONTROLS"));
  const uint8_t rows = print_lines(kHelpRowStart, kHelpKeyCol, kHelpText);
  print_str(kHelpRowStart + rows + 2, 7, STRL("RETURN OR SPACE TO GO BACK"));

  keys_wait_release(KSCAN_H);

  // V is a toggle here as it is everywhere else, so it needs edge detection -
  // the other four keys in this loop are momentary and do not.
  uint8_t prev_help_toggles = 0;

  while (1) {
    keys_poll();
    const uint8_t down = keys_scan(kHelpKeys, kHelpKeyCount);
    if (down & kHelpKeysExit) {
      break;
    }
#ifdef __ENABLE_SOUND__
    if (keys_edges(down, &prev_help_toggles) & kHelpKeyV) {
      // Reached from the menu, where the tune is playing, and from flight,
      // where it is not and the flight driver is silenced. Both want the key
      // to work: the setting is what survives, and the next screen with sound
      // picks it up.
      sound_cycle_volume();
      screen_notice(sound_volume_label());
    }
#else
    (void)prev_help_toggles;
#endif
    // Unconditional, and safe because music_tick() returns immediately unless
    // music_playing is set. This function has two callers - menu.cc when H is
    // pressed in the menu, and sim.cc when H is pressed in flight - and only
    // the first one has a tune running. Without that guard, checking the
    // controls mid-mission would start the title music.
    // See ../docs/music.md section 3.
    music_tick();
    screen_notice_tick();
    gfx_wait_vsync();
  }
  while (keys_scan(kHelpKeys, kHelpKeysExitCount)) {
    keys_poll();
  }
}

#pragma optimize(pop)
