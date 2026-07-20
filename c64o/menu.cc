#include "menu.h"

#include <string.h>

#include "color.h"
#include "gfx.h"
#include "help.h"
#include "keys.h"
#include "mem.h"
#include "mission.h"
#include "print.h"
#include "screen.h"
#include "vic.h"

// The routines below only run on screen transitions (menu, help, map) or at
// startup, never inside the per-frame simulation loop, so the outliner's
// size-for-a-JSR trade costs nothing that matters here. It stays off
// globally so the renderer and the raster IRQ handlers keep their
// straight-line code.
#pragma optimize(push, outline)

static const uint8_t kMissionRowStart = 4;
static const uint8_t kMissionRowStep = 4;

static void _enter_menu() {
  screen_begin_text_page();

  print_str(0, 14, STRL("PLANE PILOT"));
  print_str(2, 12, STRL("SELECT MISSION:"));

  uint8_t row = kMissionRowStart;
  for (uint8_t i = 0; i < kMissionCount; ++i) {
    print_str(row, 2, kMissionTitles[i], strlen(kMissionTitles[i]));
    print_lines(row + 1, 4, kMissionDesc[i]);
    row += kMissionRowStep;
  }
  print_str(row + 2, 11, STRL("PRESS H FOR HELP"));
}

static void _draw_mission_cursor(uint8_t selected_mission, bool draw) {
  mem_screen_row_ptrs[kMissionRowStart + selected_mission * kMissionRowStep]
                     [0] = draw ? '>' : ' ';
}

uint8_t menu_run() {
  _enter_menu();

  uint8_t selected_mission = 0;
  _draw_mission_cursor(selected_mission, true);

  static const uint8_t kMenuKeyI = 0x01;
  static const uint8_t kMenuKeyK = 0x02;
  static const uint8_t kMenuKeySpace = 0x04;
  static const uint8_t kMenuKeyReturn = 0x08;
  static const uint8_t kMenuKeyH = 0x10;
  uint8_t prev_menu_toggles = 0;

  while (1) {
    keyb_poll();

    uint8_t menu_toggles = 0;
    if (key_pressed(KSCAN_I)) {
      menu_toggles |= kMenuKeyI;
    }
    if (key_pressed(KSCAN_K)) {
      menu_toggles |= kMenuKeyK;
    }
    if (key_pressed(KSCAN_SPACE)) {
      menu_toggles |= kMenuKeySpace;
    }
    if (key_pressed(KSCAN_RETURN)) {
      menu_toggles |= kMenuKeyReturn;
    }
    if (key_pressed(KSCAN_H)) {
      menu_toggles |= kMenuKeyH;
    }
    const uint8_t menu_edges = keys_edges(menu_toggles, &prev_menu_toggles);

    _draw_mission_cursor(selected_mission, false);
    if (menu_edges & kMenuKeyI) {
      if (selected_mission > 0) {
        selected_mission--;
      } else {
        selected_mission = kMissionCount - 1;
      }
    }
    if (menu_edges & kMenuKeyK) {
      if (selected_mission < kMissionCount - 1) {
        selected_mission++;
      } else {
        selected_mission = 0;
      }
    }
    _draw_mission_cursor(selected_mission, true);
    if (menu_edges & (kMenuKeySpace | kMenuKeyReturn)) {
      return selected_mission;
    }
    if (menu_edges & kMenuKeyH) {
      help_run();
      _enter_menu();
      _draw_mission_cursor(selected_mission, true);
    }

    gfx_wait_vsync();
  }
}

#pragma optimize(pop)
