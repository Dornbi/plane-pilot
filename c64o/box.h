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

// Drops the per-slot cache, forcing the next box_prepare() for each screen
// buffer to copy its characters into CHAR_RAM again. Must be called by
// anything that overwrites the gradient character slots ($01..$1D and
// $61..$7D) behind box_prepare's back — otherwise it sees the slot already
// holds this definition and skips the copy into memory that no longer has it.
void box_invalidate(void);

#pragma compile("box.cc")

#endif