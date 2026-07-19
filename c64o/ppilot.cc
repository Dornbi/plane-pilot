#include <string.h>

#include "benchmark.h"
#include "box.h"
#include "chardefs.h"
#include "cia.h"
#include "color.h"
#include "gfx.h"
#include "keys.h"
#include "map.h"
#include "mem.h"
#include "mission.h"
#include "model.h"
#include "render.h"
#include "sprites.h"
#include "vic.h"
#include "view.h"
#include "world.h"

static const uint8_t kMissionRowStart = 4;
static const uint8_t kMissionRowStep = 4;

static void enter_menu() {
  mem_use_main_buffer();
  gfx_stop_raster_irqs();
  vic.spr_enable = 0x00;

  vic.ctrl1 = 0x1b;
  vic.ctrl2 = 0xd8;
  vic.color_border = kColorBlack;
  vic.color_back = kColorWhite;
  // vic.color_back1 = kColorBlue;
  // vic.color_back2 = kColorRed;

  memset(kScreenRamMain, 32, 1000);
  memset(kColorRam, kColorBlack, 1000);

  print_str(0, 14, STRL("PLANE PILOT"));
  print_str(2, 0, STRL("SELECT MISSION:"));

  uint8_t row = kMissionRowStart;
  for (uint8_t i = 0; i < kMissionCount; ++i) {
    print_str(row, 2, kMissionTitles[i], strlen(kMissionTitles[i]));
    print_lines(row + 1, 4, kMissionDesc[i]);
    row += kMissionRowStep;
  }
}

static void draw_mission_cursor(uint8_t selected_mission, bool draw) {
  mem_screen_row_ptrs[kMissionRowStart + selected_mission * kMissionRowStep]
                     [0] = draw ? '>' : ' ';
}

static uint8_t run_menu() {
  enter_menu();

  uint8_t selected_mission = 0;
  draw_mission_cursor(selected_mission, true);

  static const uint8_t kMenuKeyI = 0x01;
  static const uint8_t kMenuKeyK = 0x02;
  static const uint8_t kMenuKeySpace = 0x04;
  static const uint8_t kMenuKeyReturn = 0x08;
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
    const uint8_t menu_edges = menu_toggles & ~prev_menu_toggles;
    prev_menu_toggles = menu_toggles;

    if (menu_edges & kMenuKeyI) {
      if (selected_mission > 0) {
        draw_mission_cursor(selected_mission, false);
        selected_mission--;
        draw_mission_cursor(selected_mission, true);
      }
    }
    if (menu_edges & kMenuKeyK) {
      if (selected_mission < kMissionCount - 1) {
        draw_mission_cursor(selected_mission, false);
        selected_mission++;
        draw_mission_cursor(selected_mission, true);
      }
    }
    if (menu_edges & (kMenuKeySpace | kMenuKeyReturn)) {
      return selected_mission;
    }

    while (vic.raster != 255)
      ;
    while (vic.raster == 255)
      ;
  }
}

static void enter_simulation(uint8_t selected_mission) {
  memset(kScreenRamMain, kCharSolid11, 1000);
  memset(kScreenRamAlt, kCharSolid11, 1000);

  model_init_from_mission(&kMissions[selected_mission]);

  mem_init_mccm();
  view_refresh_panel();
  mem_use_main_buffer();

  sprites_init();
  gfx_init_raster_irqs();
  render_snap_center_chars();
}

static void run_simulation(uint8_t selected_mission) {
  enter_simulation(selected_mission);

  // Keys that toggle state only act on their rising edge, otherwise
  // holding the key would flip the state on every loop iteration.
  static const uint8_t kToggleKeyP = 0x01;
  static const uint8_t kToggleKeyD = 0x02;
  static const uint8_t kToggleKeyN = 0x04;
  uint8_t prev_toggles = 0;

  while (1) {
    keyb_poll();

    // Simulation loop
    uint8_t toggles = 0;
    if (key_pressed(KSCAN_P)) {
      toggles |= kToggleKeyP;
    }
    if (key_pressed(KSCAN_D)) {
      toggles |= kToggleKeyD;
    }
    if (key_pressed(KSCAN_N)) {
      toggles |= kToggleKeyN;
    }
    const uint8_t toggle_edges = toggles & ~prev_toggles;
    prev_toggles = toggles;

    if (toggle_edges & kToggleKeyD) {
      mem_switch_debug(!mem_debug_enabled);
    }
    if (toggle_edges & kToggleKeyP) {
      model_paused = !model_paused;
    }

    if (key_pressed(KSCAN_Q)) {
      while (key_pressed(KSCAN_Q)) {
        keyb_poll();
      }
      return;
    }

    // Flight controls and resets are suspended while the map is open, since
    // there's no instrument feedback to show the player what they're doing.
    if (!map_mode) {
      if (key_pressed(KSCAN_R)) {
        model_init_from_mission(&kMissions[selected_mission]);
      }
      if (key_pressed(KSCAN_T)) {
        model_init_alt();
      }
      if (key_pressed(KSCAN_F)) {
        model_reset_fuel();
      }
      if (key_pressed(KSCAN_J)) {
        model_input(MODEL_INPUT_ROLL_LEFT);
      }
      if (key_pressed(KSCAN_L)) {
        model_input(MODEL_INPUT_ROLL_RIGHT);
      }
      if (key_pressed(KSCAN_I)) {
        model_input(MODEL_INPUT_PITCH_DOWN);
      }
      if (key_pressed(KSCAN_K)) {
        model_input(MODEL_INPUT_PITCH_UP);
      }
      if (key_pressed(KSCAN_A)) {
        model_input(MODEL_INPUT_YAW_LEFT);
      }
      if (key_pressed(KSCAN_S)) {
        model_input(MODEL_INPUT_YAW_RIGHT);
      }
      if (toggle_edges & kToggleKeyN) {
        model_input(MODEL_INPUT_TOGGLE_NAV);
      }
      if (key_pressed(KSCAN_Z)) {
        model_input(MODEL_INPUT_MOVE_FORWARD);
      }
      if (key_pressed(KSCAN_X)) {
        model_input(MODEL_INPUT_MOVE_BACKWARD);
      }
      if (key_pressed(KSCAN_PLUS)) {
        model_input(MODEL_INPUT_THROTTLE_UP);
      }
      if (key_pressed(KSCAN_MINUS)) {
        model_input(MODEL_INPUT_THROTTLE_DOWN);
      }
    }
    if (key_pressed(KSCAN_1)) {
      view_update_view(VIEW_LEFT);
    }
    if (key_pressed(KSCAN_2)) {
      view_update_view(VIEW_CENTER);
    }
    if (key_pressed(KSCAN_3)) {
      view_update_view(VIEW_RIGHT);
    }
    if (key_pressed(KSCAN_M)) {
      if (map_mode) {
        map_exit();
      } else {
        map_enter();
      }
      while (key_pressed(KSCAN_M)) {
        keyb_poll();
      }
    }

    if (!map_mode) {
      model_advance();

      bm_start();
      view_update_cam();
      world_update_roll_state();
      world_update_sun_pos();
      model_update_instruments();

      model_maybe_print_debug();

      render_snap_center_chars();
      render_fill_sky_ground();
      box_prepare();
      box_draw();
      world_render_grid();
      bm_total(990, "TOT:");
      mem_switch_buffer();
    } else {
      while (vic.raster != 255)
        ;
      while (vic.raster == 255)
        ;
    }
  }
}

int main(void) {
  cia_init();
  bm_init();

  mem_init();
  mem_switch_buffer();
  mem_clear_screen();
  mem_switch_buffer();
  mem_clear_screen();

  gfx_init_chars();

  while (1) {
    uint8_t selected_mission = run_menu();
    run_simulation(selected_mission);
  }
}