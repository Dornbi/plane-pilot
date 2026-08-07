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
#include "sound.h"
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
  // Before the interrupts start, so the first sound_blit() has a defined
  // shadow to push rather than whatever the chip was left holding.
  sound_init();
  gfx_init_raster_irqs();
  render_snap_center_chars();
}

// The keys that close the map: M because it is the toggle that opened it,
// and RETURN, SPACE or Q to match the help screen's way out. Nothing else on
// the map does anything at all.
//
// Keep in sync with the "Controls" table in help.cc and README.md.
static const enum KeyScanCode kMapExitKeys[] = {KSCAN_M, KSCAN_SPACE,
                                                KSCAN_RETURN, KSCAN_Q};
static const uint8_t kMapExitKeyCount =
    sizeof(kMapExitKeys) / sizeof(kMapExitKeys[0]);

// Closes the map if one of those keys is down, and reports whether it did.
static bool _map_poll_exit(void) {
  for (uint8_t i = 0; i < kMapExitKeyCount; ++i) {
    if (key_pressed(kMapExitKeys[i])) {
      map_exit();
      // Every one of these means something else in the simulation loop --
      // M would reopen the map on the very next iteration and Q would quit
      // to the menu -- so the press has to be spent here. Closing the map
      // is all Q does; quitting takes a second, deliberate press.
      keys_wait_release(kMapExitKeys[i]);
      return true;
    }
  }
  return false;
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

    // The map is a modal page, like the help screen: the simulation is
    // frozen and nothing but the exit keys responds. That is partly because
    // there is no instrument feedback to fly on, and partly because the map
    // owns the character set, both screen buffers and the panel bitmap that
    // most of the other keys write to. The edges above are still collected
    // first, so a toggle pressed over the map is consumed rather than saved
    // up to fire on the way out.
    if (map_mode) {
      if (!_map_poll_exit()) {
        gfx_wait_vsync();
      }
      continue;
    }

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
    if (key_pressed(KSCAN_1)) {
      view_update_view(VIEW_LEFT);
    }
    if (key_pressed(KSCAN_2)) {
      view_update_view(VIEW_CENTER);
    }
    if (key_pressed(KSCAN_3)) {
      view_update_view(VIEW_RIGHT);
    }
    // Only the opening half of the toggle lives here; the map's own key
    // handling above closes it, M included.
    if (key_pressed(KSCAN_M)) {
      map_enter();
      keys_wait_release(KSCAN_M);
      continue;
    }
    if (key_pressed(KSCAN_H)) {
      help_run();
      screen_restore_simulation();
      keys_wait_release(KSCAN_H);
    }

    bm_model_start();
    flight_advance();
    if (flight_crashed()) {
      // A crash is the end of the flight, so its message stays up until R or
      // Q. Completing a mission is not: flight.cc announces it for a few
      // seconds and the simulation carries on, which is why only crashes are
      // handled here.
      const char *msg = flight_status_text(flight_status, true);
      msg_show(msg, MSG_FOREVER, true);
    }
    msg_update();
    bm_model_end(630, "MDL:");

    bm_start();
    view_update_cam();
    world_update_roll_state();
    world_update_sun_pos();

    render_snap_center_chars();
    msg_restore_color();
    render_fill_sky_ground();
    box_prepare();
    box_draw();
    world_render_grid();
    msg_render();

    // Everything above draws the viewport, so it has to finish before
    // mem_switch_buffer() puts that buffer on screen. The panel does not: its
    // bitmap at $F000 and the sprite registers are single-buffered and belong
    // to the raster handlers, not to either screen buffer. Running it here
    // rather than before the render spends it in the gap between the last
    // drawing call and the raster reaching the flip window, which is time
    // mem_switch_buffer() would otherwise spin away.
    //
    // Note it still reads this iteration's flight state - world_update_roll_state()
    // above produced roll_angle - so nothing is pipelined and no input gets a
    // frame staler. Moving flight_advance() or view_update_cam() down here
    // would fill the gap more completely, but only by rendering the previous
    // iteration's state, and at a ~10 Hz frame rate that added latency costs
    // more at the controls than the cycles are worth.
    panel_update_instruments();
    panel_maybe_print_debug();

    // Same gap, and for the same reason: sound_update() writes only
    // sound_shadow, which belongs to the raster handler rather than to either
    // screen buffer, so it has no business holding up the flip.
    sound_update();

    bm_total(990, "TOT:");
    mem_switch_buffer();
  }
}
