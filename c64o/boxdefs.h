// --------------------------------------------------------------------------
// GENERATED FILE - DO NOT EDIT.
//
// Regenerate with ./generate_all.sh from the repository root.
// Produced by generate_all.py via lib/find_boxes.py.
// --------------------------------------------------------------------------

#ifndef BOXDEFS_H
#define BOXDEFS_H

#include <stdint.h>

static const uint8_t kMaxBoxTotalSize = 96;
static const uint8_t kMaxBoxCharCount = 32;

struct boxdef_t {
  // Width of the box in chars.
  uint8_t w;
  // Height of the box in chars.
  uint8_t h;
  // Step in x direction to draw the next adjacent box.
  int8_t step_x;
  // Step in y direction to draw the next adjacent box.
  int8_t step_y;
  // X position of the starting box, relative to the center.
  int8_t rel_x;
  // Y position of the starting box, relative to the center.
  int8_t rel_y;
  // Local character index where Grad1 color usage starts.
  uint8_t grad1_color_start;
  // Number of unique characters used by this box (excluding solid 0,1)
  uint8_t char_count;
  // Which local characters are their chardefs entry upside down, for
  // box_prepare to copy backwards. The generator orders each box so
  // they are one run that touches grad1_color_start: the low nibble
  // is how many of them lie below it, the high nibble how many from it
  // up, so the run is grad1_color_start - low .. grad1_color_start +
  // high - 1, and zero means none.
  uint8_t flip_around;
  // Directly after it in boxdefs.cc, in the same object: the chardefs
  // index of each character, char_count of them, followed by the box's
  // w * h cells, each cell's index into the local characters (0..2 the
  // solid ones, 3.. the indices' [0..]). No pointer to them: 2 B a box.
};

extern boxdef_t boxdef;
// Where boxdef's indices and cells are: just past its definition.
extern const uint8_t *boxdef_char_idx;

// Updates boxdef and boxdef_char_idx based on roll_angle.
// Returns the definition that was copied into boxdef, which box_prepare
// uses as a cache identity. Returns NULL if roll_angle is out of range
// (both are left unchanged).
// @param roll_angle
// @result boxdef
const boxdef_t *boxdef_set_main();

// Same as boxdef_set_main but prefers the alternative (shifted) box.
// @param roll_angle
// @result boxdef
const boxdef_t *boxdef_set_alt();

#pragma compile("boxdefs.cc")

#endif
