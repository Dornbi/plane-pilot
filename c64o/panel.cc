#include "panel.h"

#include "flight.h"
#include "gfx.h"
#include "roll.h"
#include "sprites.h"
#include "view.h"

void panel_update_instruments() {
  sprites_set_speed(flight_speed >> 6);
  sprites_set_alt(flight_eye_z >> 8);
  sprites_set_vspeed(flight_vspeed);
  if (view_state == VIEW_CENTER) {
    // With centered view, we can reuse the roll angle from the view.
    sprites_set_roll(roll_angle);
  } else {
    // Otherwise compute it from flight_cam.
    sprites_set_roll(_get_roll_angle(flight_cam.up.z, flight_cam.left.z));
  }
  sprites_set_pitch(flight_cam.front.z >> 2);
  sprites_set_throttle(flight_throttle);
  sprites_set_fuel(flight_fuel);
  gfx_update_heading_bitmap(flight_true_heading);
  gfx_update_nav_heading(flight_nav_heading);
  gfx_update_flap(flight_flap);
  gfx_update_gear(flight_gear);
}

void panel_maybe_print_debug() {
#ifdef __DEBUG_MODEL__
  if (mem_debug_enabled) {
    print_labeled_signed_bcd(600, "FX: ", flight_cam.front.x, 4);
    print_labeled_signed_bcd(610, "FY: ", flight_cam.front.y, 4);
    print_labeled_signed_bcd(620, "FZ: ", flight_cam.front.z, 4);
    print_labeled_signed_bcd(640, "LX: ", flight_cam.left.x, 4);
    print_labeled_signed_bcd(650, "LY: ", flight_cam.left.y, 4);
    print_labeled_signed_bcd(660, "LZ: ", flight_cam.left.z, 4);
    print_labeled_signed_bcd(680, "UX: ", flight_cam.up.x, 4);
    print_labeled_signed_bcd(690, "UY: ", flight_cam.up.y, 4);
    print_labeled_signed_bcd(700, "UZ: ", flight_cam.up.z, 4);

    print_labeled_hex(778, "EX:", flight_eye_x, 8);
    print_labeled_hex(818, "EY:", flight_eye_y, 8);
    print_labeled_hex(858, "EZ:", flight_eye_z, 8);

    print_labeled_signed_bcd(920, "SPD:", flight_speed, 4);
    print_labeled_signed_bcd(960, "VSP:", flight_vspeed, 4);

    print_labeled_bcd(930, "HDG:", flight_true_heading, 3);
    print_labeled_bcd(970, "NAV:", flight_nav_heading);
  }
#endif
}
