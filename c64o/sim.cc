#include "sim.h"

#include <string.h>

#include "benchmark.h"
#include "box.h"
#include "chardefs.h"
#include "gfx.h"
#include "help.h"
#include "keys.h"
#include "map.h"
#include "mem.h"
#include "mission.h"
#include "model.h"
#include "render.h"
#include "screen.h"
#include "sprites.h"
#include "view.h"
#include "world.h"

// Only reached on screen transitions and in the debug view, never from the
// per-frame render path, so the outliner's size-for-a-JSR trade is free here.
#pragma optimize(push, outline)

static void _enter_simulation(uint8_t selected_mission) {
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

#pragma optimize(pop)

void sim_run(uint8_t selected_mission) {
  _enter_simulation(selected_mission);

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
    const uint8_t toggle_edges = keys_edges(toggles, &prev_toggles);

    if (toggle_edges & kToggleKeyD) {
      mem_switch_debug(!mem_debug_enabled);
    }
    if (toggle_edges & kToggleKeyP) {
      model_paused = !model_paused;
    }

    if (key_pressed(KSCAN_Q)) {
      keys_wait_release(KSCAN_Q);
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
      keys_wait_release(KSCAN_M);
    }
    if (!map_mode && key_pressed(KSCAN_H)) {
      help_run();
      screen_restore_simulation();
      keys_wait_release(KSCAN_H);
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
      gfx_wait_vsync();
    }
  }
}
