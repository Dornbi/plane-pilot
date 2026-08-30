// Scripted keyboard input, for capturing the screenshots in screens/.
//
// This file is never part of a real build. tools/make_shots.sh copies c64o/
// to a scratch directory, drops this in beside it, and redirects the keyboard
// poll of the three screen loops (menu, help, flight) at shot_poll() below.
//
// Why it has to exist: the game scans the key matrix directly, so VICE's
// -keybuf - which fills the BASIC keyboard buffer - cannot drive it, and
// nothing else reaches the matrix headlessly. shot_poll() polls the real
// matrix and then clears the bits of whatever key the script says is held, so
// every key_pressed() call site sees a press without knowing anything about
// this file. See docs/emulator.md.
//
// The script is measured in polls of whichever loop is running, not in cycles:
// the menu and the help screen poll at 50 Hz, the flight loop at whatever it
// renders at (~7 Hz). Two consequences worth knowing before editing a script:
//
//   - keys_wait_release() deliberately keeps calling the plain keyb_poll(), so
//     a one-poll scripted press releases itself on the next iteration. That is
//     what lets {1, KSCAN_M} open the map instead of opening it and having the
//     still-held key close it again on the next pass.
//   - A key only fires once per press where the game edge-detects it, so
//     stepping the menu cursor down three rows is three separate one-poll
//     presses with a gap between them, not one press held for three polls.
//
// Scene numbers are chosen with -D__SHOT__=n and pair with the table in
// tools/make_shots.sh, which owns the capture point and the output name.

#include "shot.h"

#include "flight.h"

// Script entries: hold `key` for `polls` polls of whichever loop is running.
// `key` is a KeyScanCode, or one of the pseudo-keys below.
#define SHOT_IDLE 0xff
// Freezes the flight model directly rather than through the P key, so the
// frame holds still for the capture with no PAUSED banner over the viewport.
#define SHOT_FREEZE 0xfe

struct shot_step_t {
  uint16_t polls;
  uint8_t key;
};

// Stepping the menu cursor down one row: the press, then a gap long enough for
// the loop to see the release before the next one.
#define SHOT_MENU_DOWN {1, KSCAN_K}, {6, SHOT_IDLE}
// How long the menu is left alone before the script touches it. At 50 Hz this
// is two seconds, which is enough for the title flyby to be somewhere
// photogenic and for a human watching the capture to see what happened.
#define SHOT_MENU_SETTLE {100, SHOT_IDLE}

#if __SHOT__ == 1
// The menu, with the title flyby running. Nothing to press.
static const shot_step_t kShotScript[] = {
    {1, SHOT_IDLE},
};
#elif __SHOT__ == 2
// The help screen, opened from the menu with H and then left up.
static const shot_step_t kShotScript[] = {
    SHOT_MENU_SETTLE,
    {1, KSCAN_H},
};
#elif __SHOT__ == 3
// Mission 02, which starts on final approach: the runway is dead ahead and the
// panel is showing a real approach. Frozen a couple of seconds in.
static const shot_step_t kShotScript[] = {
    SHOT_MENU_SETTLE, SHOT_MENU_DOWN,   // 01 -> 02
    {1, KSCAN_RETURN},
    {25, SHOT_IDLE},
    {1, SHOT_FREEZE},
};
#elif __SHOT__ == 4
// Mission 04, which starts airborne at cruise, rolled hard right with L and
// frozen at about 75 degrees of bank: the horizon goes near vertical and the
// artificial horizon on the panel goes with it. Seventeen polls of L is the
// most bank that still reads as flying - a few more and the aeroplane is
// inverted, which the model treats as a crash rather than as a photograph.
static const shot_step_t kShotScript[] = {
    SHOT_MENU_SETTLE, SHOT_MENU_DOWN, SHOT_MENU_DOWN, SHOT_MENU_DOWN,
    {1, KSCAN_RETURN},
    {10, SHOT_IDLE},
    {17, KSCAN_L},
    {1, SHOT_FREEZE},
};
#elif __SHOT__ == 5
// Mission 04 again, flown for a minute and a half to lay down a breadcrumb
// trail, with one brief roll partway so the trail has a bend in it and reads
// as a flight rather than a ruler. Then M for the map, which is modal and
// freezes the simulation under it, so there is nothing to hold still after.
//
// Nothing counters the roll, and nothing needs to: four polls of L is a few
// degrees of bank, the model washes it out, and the aeroplane goes on flying
// unattended for the rest of the script.
static const shot_step_t kShotScript[] = {
    SHOT_MENU_SETTLE, SHOT_MENU_DOWN, SHOT_MENU_DOWN, SHOT_MENU_DOWN,
    {1, KSCAN_RETURN},
    {200, SHOT_IDLE},
    {4, KSCAN_L},
    {300, SHOT_IDLE},
    {1, KSCAN_M},
};
#elif __SHOT__ == 6
// Mission 04 with the debug view switched on. Deliberately not frozen: the
// counters are the subject, and a frozen model reports a model cost of nothing.
static const shot_step_t kShotScript[] = {
    SHOT_MENU_SETTLE, SHOT_MENU_DOWN, SHOT_MENU_DOWN, SHOT_MENU_DOWN,
    {1, KSCAN_RETURN},
    {8, SHOT_IDLE},
    {1, KSCAN_D},
};
#elif __SHOT__ == 7
// Mission 07, looking left with the 1 key. The forward view at cruise is
// mostly empty sky and empty ground - the world's features are small at that
// altitude and the ones near a start position are behind or beside it - and
// this is where they all land in one frame: the sun, a cloud, the lake south
// of runway 2 and the scattered ground objects around it. The side view gives
// up two thirds of the picture to the wing to do it, which is the trade, and
// it is also the only screenshot of the 1 / 2 / 3 views working.
static const shot_step_t kShotScript[] = {
    SHOT_MENU_SETTLE, SHOT_MENU_DOWN, SHOT_MENU_DOWN, SHOT_MENU_DOWN,
    SHOT_MENU_DOWN,   SHOT_MENU_DOWN, SHOT_MENU_DOWN,
    {1, KSCAN_RETURN},
    {26, SHOT_IDLE},
    {2, KSCAN_1},
    {2, SHOT_IDLE},
    {1, SHOT_FREEZE},
};
#else
#error "build with -D__SHOT__=<scene number>"
#endif

static const uint8_t kShotSteps = sizeof(kShotScript) / sizeof(kShotScript[0]);

static uint8_t shot_idx = 0;
static uint16_t shot_left = 0;

void shot_poll(void) {
  keyb_poll();

  if (shot_idx >= kShotSteps) {
    return;
  }
  if (shot_left == 0) {
    shot_left = kShotScript[shot_idx].polls;
  }

  const uint8_t key = kShotScript[shot_idx].key;
  if (key < 64) {
    // The matrix is active low: a cleared bit is a key that is down.
    keyb_matrix[key >> 3] &= ~(1 << (key & 7));
  } else if (key == SHOT_FREEZE) {
    flight_paused = true;
  }

  if (--shot_left == 0) {
    ++shot_idx;
  }
}
