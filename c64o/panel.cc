#include "panel.h"

#include "benchmark.h"
#include "cpu.h"
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
  gfx_update_stall(flight_stall);
  gfx_update_flap(flight_flap);
  gfx_update_gear(flight_gear);
}

void panel_maybe_print_debug() {
#ifdef __ENABLE_DEBUG__
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

    print_labeled_hex(760, "EX:", flight_eye_x, 8);
    print_labeled_hex(800, "EY:", flight_eye_y, 8);
    print_labeled_hex(840, "EZ:", flight_eye_z, 8);

    print_labeled_bcd(812, "ROL:", roll_angle, 2);
    print_labeled_bcd(852, "HDG:", flight_true_heading, 2);

    // What the boot-time probe made of this machine (cpu.h). us on the left,
    // the step shift it implies on the right.
    print_labeled_bcd(930, "CPU:", cpu_probe_us, 5);
    print_labeled_bcd(970, "CSHIFT: ", cpu_step_shift, 1);

    // A breakdown of two of the stage counters, not three more terms beside
    // them: PLY is the polygon part of GRD and sits directly under it in the
    // right hand column, CLD and SPR are the cloud scan and the sprite stack
    // inside UPD and sit under that in the second column. Neither is added
    // into TOT - see benchmark.h.
    //
    // Five digits each, which is what leaves a space between the columns.
    // PLY is the one with headroom to lose: a runway polygon filling the
    // viewport has been measured at 43,064 cycles (docs/framerate.md), so a
    // frame with two of those would wrap silently. The cloud scan has never
    // been seen above 30,000 and the sprite stack runs in hundreds.
    bm_sub_show(BM_SUB_CLOUDS, 819, "CLD: ", 5);
    bm_sub_show(BM_SUB_SPRITES, 859, "SPR: ", 5);
    bm_sub_show(BM_SUB_POLY, 870, "PLY: ", 5);

    print_labeled_signed_bcd(920, "SPD:", flight_speed, 4);
    print_labeled_signed_bcd(960, "VSP:", flight_vspeed, 4);
  }
#endif
}
