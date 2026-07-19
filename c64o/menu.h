#ifndef MENU_H
#define MENU_H

#include <stdint.h>

// Runs the main menu screen (MCCM, no sprites, no raster IRQ, single
// buffer) until the player picks a mission with I/K to move the cursor and
// SPACE/RETURN to confirm. H opens the help screen without leaving the
// menu.
// @result the selected mission index
uint8_t menu_run(void);

#pragma compile("menu.cc")

#endif
