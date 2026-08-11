#include "help.h"

#include "gfx.h"
#include "keys.h"
#include "music.h"
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
// it directly, so there is no pointer table and no per-row loop below. The
// key column is space-padded so the descriptions line up in one column.
// clang-format off
static const char kHelpText[] =
    "I J K L  ROLL AND PITCH\n"
    "A S      YAW\n"
    "+ -      THROTTLE UP/DOWN\n"
    "F        TOGGLE FLAPS\n"
    "G        TOGGLE GEAR\n"
    "B        BRAKE ON GROUND\n"
    "\n"
    "1 2 3    LOOK LEFT/CENTER/RIGHT\n"
    "N        TOGGLE NAV POINT\n"
    "M        TOGGLE MAP VIEW\n"
    "P        PAUSE / RESUME\n"
#ifdef __ENABLE_SOUND__
    "V        SOUND FULL / LOW / OFF\n"
#endif
    "R        RESTART MISSION\n"
    "Q        QUIT TO MENU\n"
#ifdef __ENABLE_DEBUG__
    "\n"
    "D        TOGGLE DEBUG VIEW\n"
    "Z X      MOVE FWD/BACK WHEN PAUSED\n"
#endif
    "\n"
    "H        SHOW THIS HELP SCREEN";
// clang-format on

static const uint8_t kHelpRowStart = 2;
static const uint8_t kHelpKeyCol = 2;

// Same cell as the menu's, so the notice does not appear to move when the
// player crosses between the two screens.
static void _help_show_volume_notice(void) {
  const char *label = sound_volume_label();
  if (label) {
    screen_notice(label, kSoundVolumeLabelLen);
  }
}

void help_run(void) {
  screen_begin_text_page();

  print_str(0, 11, STRL("KEYBOARD CONTROLS"));
  const uint8_t rows = print_lines(kHelpRowStart, kHelpKeyCol, kHelpText);
  print_str(kHelpRowStart + rows + 2, 7, STRL("RETURN OR SPACE TO GO BACK"));

  // V is a toggle here as it is everywhere else, so it needs edge detection -
  // the other three keys in this loop are momentary and do not.
  static const uint8_t kHelpKeyV = 0x01;
  uint8_t prev_help_toggles = 0;

  while (1) {
    keyb_poll();
    if (key_pressed(KSCAN_RETURN) || key_pressed(KSCAN_SPACE) ||
        key_pressed(KSCAN_Q)) {
      break;
    }
#ifdef __ENABLE_SOUND__
    uint8_t help_toggles = 0;
    if (key_pressed(KSCAN_V)) {
      help_toggles |= kHelpKeyV;
    }
    if (keys_edges(help_toggles, &prev_help_toggles) & kHelpKeyV) {
      // Reached from the menu, where the tune is playing, and from flight,
      // where it is not and the flight driver is silenced. Both want the key
      // to work: the setting is what survives, and the next screen with sound
      // picks it up.
      sound_cycle_volume();
      _help_show_volume_notice();
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
  while (key_pressed(KSCAN_RETURN) || key_pressed(KSCAN_SPACE) ||
         key_pressed(KSCAN_Q)) {
    keyb_poll();
  }
}

#pragma optimize(pop)
