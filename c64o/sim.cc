#include "sim.h"

#include <string.h>

#include "benchmark.h"
#include "box.h"
#include "chardefs.h"
#include "flight.h"
#include "gfx.h"
#include "help.h"
#include "keys.h"
#include "map.h"
#include "mem.h"
#include "mission.h"
#include "msg.h"
#include "panel.h"
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

  view_state = VIEW_CENTER;
  msg_clear();
  flight_init_from_mission(selected_mission);
  msg_show(kMissionTitles[selected_mission]);

  mem_switch_debug(false);
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
  static const uint8_t kToggleKeyF = 0x08;
  static const uint8_t kToggleKeyG = 0x10;
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
    if (key_pressed(KSCAN_F)) {
      toggles |= kToggleKeyF;
    }
    if (key_pressed(KSCAN_G)) {
      toggles |= kToggleKeyG;
    }
    const uint8_t toggle_edges = keys_edges(toggles, &prev_toggles);

    if (toggle_edges & kToggleKeyD) {
      mem_switch_debug(!mem_debug_enabled);
    }
    if (toggle_edges & kToggleKeyP) {
      flight_paused = !flight_paused;
      if (flight_paused) {
        msg_show("PAUSED", MSG_FOREVER, true);
      } else {
        msg_clear();
      }
    }

    if (key_pressed(KSCAN_Q)) {
      keys_wait_release(KSCAN_Q);
      return;
    }

    // Flight controls and resets are suspended while the map is open, since
    // there's no instrument feedback to show the player what they're doing.
    if (!map_mode) {
      if (key_pressed(KSCAN_R)) {
        msg_clear();
        flight_init_from_mission(selected_mission);
        msg_show(kMissionTitles[selected_mission]);
        view_update_view(VIEW_CENTER);
        mem_switch_debug(false);
      }
      if (key_pressed(KSCAN_J)) {
        flight_input(FLIGHT_INPUT_ROLL_LEFT);
      }
      if (key_pressed(KSCAN_L)) {
        flight_input(FLIGHT_INPUT_ROLL_RIGHT);
      }
      if (key_pressed(KSCAN_I)) {
        flight_input(FLIGHT_INPUT_PITCH_DOWN);
      }
      if (key_pressed(KSCAN_K)) {
        flight_input(FLIGHT_INPUT_PITCH_UP);
      }
      if (key_pressed(KSCAN_A)) {
        flight_input(FLIGHT_INPUT_YAW_LEFT);
      }
      if (key_pressed(KSCAN_S)) {
        flight_input(FLIGHT_INPUT_YAW_RIGHT);
      }
      if (toggle_edges & kToggleKeyF) {
        flight_input(FLIGHT_INPUT_TOGGLE_FLAP);
      }
      if (toggle_edges & kToggleKeyG) {
        flight_input(FLIGHT_INPUT_TOGGLE_GEAR);
      }
      if (key_pressed(KSCAN_Z)) {
        flight_input(FLIGHT_INPUT_MOVE_FORWARD);
      }
      if (key_pressed(KSCAN_X)) {
        flight_input(FLIGHT_INPUT_MOVE_BACKWARD);
      }
      if (key_pressed(KSCAN_PLUS)) {
        flight_input(FLIGHT_INPUT_THROTTLE_UP);
      }
      if (key_pressed(KSCAN_MINUS)) {
        flight_input(FLIGHT_INPUT_THROTTLE_DOWN);
      }
      if (toggle_edges & kToggleKeyN) {
        flight_input(FLIGHT_INPUT_TOGGLE_NAV);
      }
      if (key_pressed(KSCAN_B)) {
        flight_input(FLIGHT_INPUT_BRAKE);
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
      bm_model_start();
      flight_advance();
      if (flight_status) {
        const char *msg = "YOU CRASHED";
        switch (flight_status) {
        case FLIGHT_CRASH_ROLL:
          msg = "CRASH: BANK ANGLE";
          break;
        case FLIGHT_CRASH_INVERTED:
          msg = "CRASH: INVERTED";
          break;
        case FLIGHT_CRASH_PITCH_LOW:
          msg = "CRASH: PITCH TOO LOW";
          break;
        case FLIGHT_CRASH_PITCH_HIGH:
          msg = "CRASH: PITCH TOO HIGH";
          break;
        case FLIGHT_CRASH_VSPEED:
          msg = "CRASH: HARD LANDING";
          break;
        case FLIGHT_CRASH_SPEED:
          msg = "CRASH: TOO FAST";
          break;
        case FLIGHT_CRASH_GEAR:
          msg = "CRASH: GEAR RETRACTED";
          break;
        case FLIGHT_MISSION_COMPLETED:
          msg = "MISSION COMPLETE!";
          break;
        default:
          break;
        }
        msg_show(msg, MSG_FOREVER, true);
      }
      msg_update();
      bm_model_end(630, "MDL:");

      bm_start();
      view_update_cam();
      world_update_roll_state();
      world_update_sun_pos();
      panel_update_instruments();

      panel_maybe_print_debug();

      render_snap_center_chars();
      msg_restore_color();
      render_fill_sky_ground();
      box_prepare();
      box_draw();
      world_render_grid();
      msg_render();
      bm_total(990, "TOT:");
      mem_switch_buffer();
    } else {
      gfx_wait_vsync();
    }
  }
}
