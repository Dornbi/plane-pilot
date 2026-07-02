#include "benchmark.h"
#include "box.h"
#include "cia.h"
#include "gfx.h"
#include "keys.h"
#include "mem.h"
#include "model.h"
#include "print.h"
#include "render.h"
#include "sprites.h"
#include "view.h"
#include "world.h"

int main(void) {
  cia_init();
  bm_init();

  mem_init();
  mem_switch_buffer();
  mem_clear_screen();
  mem_switch_buffer();
  mem_clear_screen();

  mem_init_mccm();
  model_init();
  sprites_init();
  gfx_init_chars();
  gfx_init_raster_irqs();
  render_snap_center_chars();

  // Keys that toggle state only act on their rising edge, otherwise
  // holding the key would flip the state on every loop iteration.
  static const uint8_t kToggleKeyP = 0x01;
  static const uint8_t kToggleKeyD = 0x02;
  static const uint8_t kToggleKeyN = 0x04;

  uint8_t prev_toggles = 0;
  while (1) {
    keyb_poll();

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

    if (key_pressed(KSCAN_R)) {
      model_init();
    }
    if (key_pressed(KSCAN_T)) {
      model_init_alt();
    }
    if (key_pressed(KSCAN_F)) {
      model_reset_fuel();
    }
    if (toggle_edges & kToggleKeyD) {
      mem_switch_debug(!mem_debug_enabled);
    }
    if (toggle_edges & kToggleKeyP) {
      model_paused = !model_paused;
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
    if (key_pressed(KSCAN_1)) {
      view_update_view(VIEW_LEFT);
    }
    if (key_pressed(KSCAN_2)) {
      view_update_view(VIEW_CENTER);
    }
    if (key_pressed(KSCAN_3)) {
      view_update_view(VIEW_RIGHT);
    }

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
    bm_total(990, SCREEN_STR("TOT:"));
    mem_switch_buffer();
  }
}