#include "menu.h"

#include <string.h>

#include "gfx.h"
#include "help.h"
#include "keys.h"
#include "mem.h"
#include "mission.h"
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

static const uint8_t kMissionRowStart = 6;
static const uint8_t kMissionRowStep = 4;
static const uint8_t kVisibleMissions = 4;

// Feedback after a V press, bottom right, on the same row as the help prompt
// and in the same cell the help screen uses. Transient rather than a permanent
// readout: it is a message, the way the flight loop's is, and a HUD element
// that never changes stops being read after the first look.
//
// Prints nothing at all in a build without sound - sound_volume_label()
// returns null there.
static void _menu_show_volume_notice(void) {
  const char *label = sound_volume_label();
  if (label) {
    screen_notice(label, (uint8_t)strlen(label));
  }
}

static void _menu_render_items(uint8_t scroll_offset) {
  memset(mem_screen_row_ptrs[4], ' ', kScreenWidth * 19);

  uint8_t row = kMissionRowStart;
  uint8_t visible_count = kMissionCount - scroll_offset;
  if (visible_count > kVisibleMissions) {
    visible_count = kVisibleMissions;
  }

  for (uint8_t v = 0; v < visible_count; ++v) {
    uint8_t i = scroll_offset + v;
    print_str(row, 2, kMissionTitles[i], strlen(kMissionTitles[i]));
    print_lines(row + 1, 4, kMissionDesc[i]);
    mem_screen_row_ptrs[row][38] = mission_completed[i] ? '@' : '.';
    row += kMissionRowStep;
  }

  if (scroll_offset > 0) {
    mem_screen_row_ptrs[4][38] = '(';
  }
  if (scroll_offset + kVisibleMissions < kMissionCount) {
    mem_screen_row_ptrs[22][38] = ')';
  }
}

static void _menu_enter(uint8_t scroll_offset) {
  screen_begin_text_page();

  print_str(0, 14, STRL("PLANE PILOT"));
  print_str(3, 12, STRL("SELECT MISSION:"));

  _menu_render_items(scroll_offset);

  print_str(23, 11, STRL("PRESS H FOR HELP"));
  // No volume readout in the initial paint. It is a notice now, not a status
  // line: it appears when V is pressed and clears itself three seconds later.
}

static void _menu_draw_mission_cursor(uint8_t selected_mission,
                                      uint8_t scroll_offset, bool draw) {
  uint8_t visible_slot = selected_mission - scroll_offset;
  mem_screen_row_ptrs[kMissionRowStart + visible_slot * kMissionRowStep][0] =
      draw ? '>' : ' ';
}

uint8_t menu_run() {
  uint8_t selected_mission = 0;
  uint8_t scroll_offset = 0;

  _menu_enter(scroll_offset);
  _menu_draw_mission_cursor(selected_mission, scroll_offset, true);

  // The menu owns the SID from here. _enter_menu() reached
  // gfx_stop_raster_irqs() a moment ago, which silenced the flight driver and
  // masked interrupts, so this is the point where nothing else can be writing
  // $D400. See ../docs/music.md section 3.
  music_start();

  static const uint8_t kMenuKeyI = 0x01;
  static const uint8_t kMenuKeyK = 0x02;
  static const uint8_t kMenuKeySpace = 0x04;
  static const uint8_t kMenuKeyReturn = 0x08;
  static const uint8_t kMenuKeyH = 0x10;
  static const uint8_t kMenuKeyV = 0x20;
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
#ifdef __ENABLE_SOUND__
    if (key_pressed(KSCAN_V)) {
      menu_toggles |= kMenuKeyV;
    }
#endif
    const uint8_t menu_edges = keys_edges(menu_toggles, &prev_menu_toggles);

    if (menu_edges & (kMenuKeyI | kMenuKeyK)) {
      _menu_draw_mission_cursor(selected_mission, scroll_offset, false);
      uint8_t old_scroll = scroll_offset;

      if (menu_edges & kMenuKeyI) {
        if (selected_mission > 0) {
          selected_mission--;
          if (selected_mission < scroll_offset) {
            scroll_offset = selected_mission;
          }
        } else {
          selected_mission = kMissionCount - 1;
          if (kMissionCount > kVisibleMissions) {
            scroll_offset = kMissionCount - kVisibleMissions;
          } else {
            scroll_offset = 0;
          }
        }
      }

      if (menu_edges & kMenuKeyK) {
        if (selected_mission < kMissionCount - 1) {
          selected_mission++;
          if (selected_mission >= scroll_offset + kVisibleMissions) {
            scroll_offset = selected_mission - kVisibleMissions + 1;
          }
        } else {
          selected_mission = 0;
          scroll_offset = 0;
        }
      }

      if (scroll_offset != old_scroll) {
        _menu_render_items(scroll_offset);
      }
      _menu_draw_mission_cursor(selected_mission, scroll_offset, true);
    }

    if (menu_edges & (kMenuKeySpace | kMenuKeyReturn)) {
      // Release the SID before the mission starts. The hard cut is deliberate:
      // the tune ending is how the player knows the menu is over.
      music_stop();
      return selected_mission;
    }
    if (menu_edges & kMenuKeyH) {
      // Not bracketed by stop/start. help_run() has its own vsync loop and
      // ticks whatever is already playing, so the tune carries across.
      help_run();
      _menu_enter(scroll_offset);
      _menu_draw_mission_cursor(selected_mission, scroll_offset, true);
    }
    if (menu_edges & kMenuKeyV) {
      // music_tick() reads sound_volume every frame and composes it with the
      // tune's per-bar ramp, so this is audible on the next tick without any
      // notification path of its own.
      sound_cycle_volume();
      _menu_show_volume_notice();
    }

    music_tick();
    screen_notice_tick();
    gfx_wait_vsync();
  }
}

#pragma optimize(pop)
