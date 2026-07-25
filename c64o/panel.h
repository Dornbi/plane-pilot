#ifndef PANEL_H
#define PANEL_H

// Updates instruments state.
void panel_update_instruments();

// Prints debug info.
void panel_maybe_print_debug();


#pragma compile("panel.cc")

#endif