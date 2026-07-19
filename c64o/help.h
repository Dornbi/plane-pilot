#ifndef HELP_H
#define HELP_H

// Displays a static, full-screen list of keyboard controls and waits for
// the player to press RETURN or SPACE before returning. Uses the same
// static-screen mode as the main menu (MCCM, no sprites, no raster IRQ,
// single buffer). Callers that were mid-simulation (the sim screen) must
// restore their own display mode afterwards, e.g. via
// screen_restore_simulation().
void help_run(void);

#pragma compile("help.cc")

#endif
