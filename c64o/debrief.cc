#include "debrief.h"

#include "damage.h"
#include "flight.h"
#include "gfx.h"
#include "gun.h"
#include "keys.h"
#include "mem.h"
#include "mission.h"
#include "print.h"
#include "screen.h"

#include <string.h>

// A screen transition, never per frame: the outliner's trade is free here.
#pragma optimize(push, outline)

static const uint8_t kDebriefCol = 2;
// The answers' right edge, one past their last column: words and numbers
// alike end against it, as the labels start kDebriefCol in from the left.
static const uint8_t kDebriefRight = 40 - kDebriefCol;

static const uint8_t kDebriefRowTitle = 2;
static const uint8_t kDebriefRowGoals = 5;
static const uint8_t kDebriefRowAircraft = kDebriefRowGoals + 1;
static const uint8_t kDebriefRowTallies = kDebriefRowGoals + 3;

// The whole page but the answers, from row 0 at kDebriefCol: a '\t' pads to
// column 11. Keep the rows above in step with its lines.
// clang-format off
static const char kDebriefText[] =
    "\t MISSION DEBRIEF\n"
    "\n"
    "\n"
    "\n"
    "\n"
    "GOALS\n"
    "YOUR AIRCRAFT\n"
    "\n"
    "ROUNDS FIRED\n"
    "ROUNDS ON TARGET\n"
    "ENEMIES SHOT DOWN\n"
    "\n"
    "\n"
    "\n"
    "\tQ TO GO BACK TO MENU";
// clang-format on

// The answers. First by flight_current_wp, how far the goals were got: not
// met, met, and then home as well - the mission completed (flight.cc).
// Then, from kDebriefAircraft, by _debrief_aircraft().
static const uint8_t kDebriefAircraft = 3;
static const char *const kDebriefAnswers[] = {
    "NOT COMPLETED",
    "COMPLETED",
    "COMPLETED, LANDED",
    "UNDAMAGED",     // no hits
    "A FEW HOLES",   // within the armour, nothing broken
    "DAMAGED",       // one or two failures
    "BADLY DAMAGED", // three or four
    "SWISS CHEESE",  // shot down
    "WRECKED",       // crashed any other way
};

// The player's aircraft, as an index into kDebriefAnswers past
// kDebriefAircraft: from the hits it took (damage.h) - the armour's, then a
// failure a hit, then shot down - or a crash.
static uint8_t _debrief_aircraft(void) {
  if (flight_status == FLIGHT_CRASH_SHOT_DOWN || damage_down()) {
    return 4;
  }
  if (flight_crashed()) {
    return 5;
  }
  const uint8_t h = gun_player_hits;
  if (h == 0) {
    return 0;
  }
  if (h <= damage_armour) {
    return 1;
  }
  return (uint8_t)(h - damage_armour) <= 2 ? 2 : 3;
}

// An answer, right-aligned on its row.
static __noinline void _debrief_answer(uint8_t row, const char *text) {
  print_lines(row, (uint8_t)(kDebriefRight - strlen(text)), text);
}

void debrief_run(void) {
  // The aircraft shot down, into the last tally: two digits, which is more
  // than any mission sends.
  uint8_t k = opponent_shot_down;
  uint8_t t = 0;
  while (k >= 10) {
    k -= 10;
    ++t;
  }
  gun_tally[kGunTallyDown + kGunTallyDigits - 2] = t;
  gun_tally[kGunTallyDown + kGunTallyDigits - 1] = k;

  screen_begin_text_page();

  print_lines(0, kDebriefCol, kDebriefText);
  print_lines(kDebriefRowTitle, kDebriefCol,
              kMissionTitles[flight_active_mission_idx]);
  _debrief_answer(kDebriefRowGoals, kDebriefAnswers[flight_current_wp]);
  _debrief_answer(kDebriefRowAircraft,
                  kDebriefAnswers[kDebriefAircraft + _debrief_aircraft()]);

  // The tallies, a row each, right-aligned: leading zeros stay blank, but
  // never the last digit.
  uint8_t i = 0;
  for (uint8_t row = kDebriefRowTallies;
       row < kDebriefRowTallies + kGunTallies; ++row) {
    uint8_t *dst = mem_screen_row_ptrs[row] + (kDebriefRight - kGunTallyDigits);
    bool lead = true;
    for (uint8_t j = 0; j < kGunTallyDigits; ++j) {
      const uint8_t v = gun_tally[i++];
      if (v != 0 || j == kGunTallyDigits - 1) {
        lead = false;
      }
      if (!lead) {
        dst[j] = (uint8_t)('0' + v);
      }
    }
  }

  // Q is what brought the player here, and it is the way on: the press that
  // opened the page must not also close it.
  keys_wait_release(KSCAN_Q);
  do {
    gfx_wait_vsync();
    keys_poll();
  } while (!key_pressed(KSCAN_Q));
}

#pragma optimize(pop)
