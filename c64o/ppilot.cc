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

  while (1) {
    keyb_poll();
#ifdef __DEBUG_CYCLES__
    bm_start();
#endif
    if (key_pressed(KSCAN_R)) {
      model_init();
    }
    if (key_pressed(KSCAN_T)) {
      model_init_alt();
    }
    if (key_pressed(KSCAN_F)) {
      model_reset_fuel();
    }
    if (key_pressed(KSCAN_D)) {
      mem_switch_debug(!mem_debug_enabled);
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
    if (key_pressed(KSCAN_PLUS)) {
      model_input(MODEL_INPUT_THROTTLE_UP);
    }
    if (key_pressed(KSCAN_MINUS)) {
      model_input(MODEL_INPUT_THROTTLE_DOWN);
    }

    model_update();
    render_snap_center_chars();
    render_fill_sky_ground();
    box_prepare();
    box_draw();
    model_render_grid();
#ifdef __DEBUG_CYCLES__
    bm_end(990, SCREEN_STR("TOT:"));
#else
    bm_total(990, SCREEN_STR("TOT:"));
#endif
    mem_switch_buffer();
  }
}