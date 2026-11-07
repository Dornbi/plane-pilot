#ifndef DEBRIEF_H
#define DEBRIEF_H

#include "opponent.h"

// fpilot's debrief: the text page Q leaves the flight through, on the way
// back to the menu. It shows how far the attempt got with the mission's goals,
// what shape the player's aircraft is in, the rounds fired - reloads included
// - and those on target, and the aircraft shot down; Q goes on to the menu.
// Everything it shows is the attempt's since the mission was started or
// restarted (R): gun.cc and opponent.cc count it, and gun_reset() and
// opponent_reset() zero it.
//
// Like the help screen it is a static page (screen_begin_text_page()), so
// the caller restores its own display afterwards - here the menu does, as
// it always does.
//
// fpilot only, with the opponents and the guns it reports on. Elsewhere it
// compiles to nothing and Q goes straight to the menu as it always has.
#ifdef __ENABLE_OPPONENT__

void debrief_run(void);

#pragma compile("debrief.cc")

#else

inline void debrief_run(void) {}

#endif

#endif
