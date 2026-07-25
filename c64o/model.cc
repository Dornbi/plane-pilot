#include "model.h"

#include <stdint.h>
#include <stdlib.h>

#include "benchmark.h"
#include "flight.h"
#include "fmath.h"
#include "gfx.h"
#include "roll.h"
#include "sprites.h"
#include "vec.h"
#include "view.h"
#include "world.h"

#ifdef __OSCAR64__
#pragma bss(bss2)
#endif

static uint8_t _model_nav;
static int16_t _model_nav_x;
static int16_t _model_nav_y;
static uint8_t _model_true_heading;
static uint8_t _model_nav_heading;

// Location of navigation waypoints.
// They match the eye_x and eye_y coordinates >> 8
// Corresponding the runways in world_map.cc
static const uint16_t kNavPointX[kGfxNumNavpoints] = {
    0x6000,
    0x2000,
};
static const uint16_t kNavPointY[kGfxNumNavpoints] = {
    0xBF80,
    0x3F80,
};

void model_init() {
  flight_init();
  _model_nav = 0;
}

void model_init_alt() {
  flight_init_alt();
  _model_nav = 1;
}

void model_init_from_mission(const mission_t *mission) {
  flight_init_from_mission(mission);
  _model_nav = (mission->start_y >= 0x80) ? 1 : 0;
}

void model_advance() {
  bm_model_start();
  if (!model_crashed) {
    flight_advance();
  }
  bm_model_end(630, "MDL:");
}

void model_update_instruments() {
  sprites_set_speed(model_speed >> 6);
  sprites_set_alt(model_eye_z >> 8);
  sprites_set_vspeed(model_vspeed);
  if (view_state == VIEW_CENTER) {
    // With centered view, we can reuse the roll angle from the view.
    sprites_set_roll(roll_angle);
  } else {
    // Otherwise compute it from model_cam.
    sprites_set_roll(_get_roll_angle(model_cam.up.z, model_cam.left.z));
  }
  sprites_set_pitch(model_cam.front.z >> 2);
  sprites_set_throttle(model_throttle);
  sprites_set_fuel(model_fuel);
  _model_true_heading = _get_heading(model_cam.front.x, model_cam.front.y);
  gfx_update_heading_bitmap(_model_true_heading);
  _model_nav_x = kNavPointX[_model_nav] - (model_eye_x >> 8);
  _model_nav_y = kNavPointY[_model_nav] - (model_eye_y >> 8);
  _model_nav_heading =
      _get_heading(_model_nav_x, _model_nav_y) - _model_true_heading;
  if (_model_nav_heading > kHeadingMax) {
    // If this triggers, then we are in underflow -> go back.
    _model_nav_heading += kHeadingMax;
  }
  gfx_update_nav_heading(_model_nav_heading);
  gfx_update_flap(model_flap);
  gfx_update_gear(model_gear);
}

void model_maybe_print_debug() {
#ifdef __DEBUG_MODEL__
  if (mem_debug_enabled) {
    print_labeled_signed_bcd(600, "FX: ", model_cam.front.x, 4);
    print_labeled_signed_bcd(610, "FY: ", model_cam.front.y, 4);
    print_labeled_signed_bcd(620, "FZ: ", model_cam.front.z, 4);
    print_labeled_signed_bcd(640, "LX: ", model_cam.left.x, 4);
    print_labeled_signed_bcd(650, "LY: ", model_cam.left.y, 4);
    print_labeled_signed_bcd(660, "LZ: ", model_cam.left.z, 4);
    print_labeled_signed_bcd(680, "UX: ", model_cam.up.x, 4);
    print_labeled_signed_bcd(690, "UY: ", model_cam.up.y, 4);
    print_labeled_signed_bcd(700, "UZ: ", model_cam.up.z, 4);

    print_labeled_hex(778, "EX:", model_eye_x, 8);
    print_labeled_hex(818, "EY:", model_eye_y, 8);
    print_labeled_hex(858, "EZ:", model_eye_z, 8);

    print_labeled_signed_bcd(760, "NX:", _model_nav_x);
    print_labeled_signed_bcd(800, "NY:", _model_nav_y);
    print_labeled_bcd(840, "NAV:", _model_nav_heading);

    print_labeled_bcd(850, "HDG:", _model_true_heading, 3);
    print_labeled_signed_bcd(920, "SPD:", model_speed, 4);
    print_labeled_signed_bcd(960, "VSP:", model_vspeed, 4);
  }
#endif
}

void model_input(enum model_input_t input) {
  if (input == MODEL_INPUT_TOGGLE_NAV) {
    _model_nav = 1 - _model_nav;
    return;
  }
  flight_input((enum flight_input_t)input);
}
