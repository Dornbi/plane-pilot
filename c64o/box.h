#ifndef BOX_H
#define BOX_H

#include <stdint.h>

// Prepares a boxdef for drawing:
// - Copies unique characters to CHAR_RAM
// - Populates box_chars mapping
// @param: boxdef
// @param: mem_box_char_start
// @result: box_chars
void box_prepare(void);

// Draws the box to the screen.
// @param: boxdef
void box_draw(void);

#pragma compile("box.cc")

#endif