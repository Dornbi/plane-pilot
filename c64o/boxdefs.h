#ifndef BOXDEFS_H
#define BOXDEFS_H

#include <stdint.h>

static const uint8_t kMaxBoxTotalSize = 96;
static const uint8_t kMaxBoxCharCount = 29;

struct boxdef_t {
  // Width of the box in chars.
  uint8_t w;
  // Height of the box in chars.
  uint8_t h;
  // Total number of chars in the box = w * h.
  uint8_t total_size;
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
  // Address of the character data.
  const uint8_t **char_addr;
  // Index of each character in the local char_idx array.
  const uint8_t *box_chars;
};

extern boxdef_t boxdef;

// Updates boxdef based on roll_angle.
// Returns the definition that was copied into boxdef, which box_prepare
// uses as a cache identity. Returns NULL if roll_angle is out of range
// (boxdef is left unchanged).
// @param roll_angle
// @result boxdef
const boxdef_t *boxdef_set_main();

// Same as boxdef_set_main but prefers the alternative (shifted) box.
// @param roll_angle
// @result boxdef
const boxdef_t *boxdef_set_alt();

#pragma compile("boxdefs.cc")

#endif
