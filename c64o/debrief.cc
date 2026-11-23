#include "debrief.h"

#include "damage.h"
#include "flight.h"
#include "gfx.h"
#include "gun.h"
#include "keys.h"
#include "mem.h"
#include "mission.h"
#include "opponent.h"
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
static const uint8_t kDebriefRowDown = kDebriefRowTallies + kGunTallies;
static const uint8_t kDebriefRowAccuracy = kDebriefRowDown + 1;
static const uint8_t kDebriefRowRating = kDebriefRowAccuracy + 2;

// A plane, one per enemy shot down: '*' in the font (generate_gfx_chars.py),
// which nothing else prints. A mission sends at most three pairs, so six of
// them end well clear of the label.
static const uint8_t kDebriefPlane = '*';

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
    "ACCURACY\n"
    "\n"
    "RATING\n"
    "\n"
    "\n"
    "\tQ / FIRE FOR MENU";
// clang-format on

// The answers. First by flight_current_wp, how far the goals were got: not
// met, met, and then home as well - the mission completed (flight.cc).
// Then from kDebriefAircraft by _debrief_aircraft(), and from kDebriefRating
// by _debrief_rating().
static const uint8_t kDebriefAircraft = 3;
static const uint8_t kDebriefRating = 9;
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
    "ACE",           // completed + landed, undamaged, >=25% or unarmed
    "VETERAN",       // completed + landed, at most armour hits
    "SURVIVOR",      // completed + landed with failures
    "PASS",          // goals completed, did not land
    "INCOMPLETE",    // returned early without goals
    "FAILED",        // crashed or shot down
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

// Prints text either centered on the 40-column screen or right-aligned to
// kDebriefRight on `row`.
static __noinline void _debrief_align(uint8_t row, const char *text,
                                      bool center) {
  const uint8_t len = (uint8_t)strlen(text);
  print_lines(row, center ? (uint8_t)((40 - len) >> 1)
                          : (uint8_t)(kDebriefRight - len),
              text);
}

// Reads the four significant digits of a tally (the fifth is always 0, since
// rounds are counted in tens). Shifts and adds only - no multiply runtime.
static __noinline uint16_t _debrief_tally_val(uint8_t base) {
  uint16_t v = 0;
  for (uint8_t j = 0; j < kGunTallyDigits - 1; ++j) {
    v = (uint16_t)(v + (uint16_t)(v << 2));
    v = (uint16_t)((uint16_t)(v << 1) + gun_tally[base + j]);
  }
  return v;
}

static __noinline void _debrief_wait_release(void) {
  do {
    keys_poll();
  } while (key_pressed(KSCAN_Q) || (joy2_state & kJoyFire));
}

void debrief_run(void) {
  screen_begin_text_page();

  const uint8_t aircraft = _debrief_aircraft();
  print_lines(0, kDebriefCol, kDebriefText);
  _debrief_align(kDebriefRowTitle, kMissionTitles[flight_active_mission_idx],
                 true);
  _debrief_align(kDebriefRowGoals, kDebriefAnswers[flight_current_wp], false);
  _debrief_align(kDebriefRowAircraft,
                 kDebriefAnswers[kDebriefAircraft + aircraft], false);

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

  // The aircraft shot down, as that many planes leftwards from the right
  // edge, or a 0 like the tallies' when there are none.
  uint8_t *down = mem_screen_row_ptrs[kDebriefRowDown];
  uint8_t col = kDebriefRight - 1;
  uint8_t n = opponent_shot_down;
  if (n == 0) {
    down[col] = '0';
  } else {
    do {
      down[col--] = kDebriefPlane;
    } while (--n);
  }

  // Hit rate (0..100, or "--" if no shots fired) without linking multiply or
  // divide; counts directly into decimal tens and ones.
  uint16_t fired = _debrief_tally_val(kGunTallyFired);
  uint16_t hits = _debrief_tally_val(kGunTallyHits);
  uint8_t *acc = mem_screen_row_ptrs[kDebriefRowAccuracy] + (kDebriefRight - 3);
  uint8_t pct = 0;
  if (fired == 0) {
    acc[1] = '-';
    acc[2] = '-';
  } else {
    uint16_t num = 0;
    for (uint8_t k = 100; k != 0; --k) {
      num += hits;
    }
    uint8_t tens = 0, ones = 0;
    while (num >= fired) {
      num -= fired;
      ++pct;
      if (++ones == 10) {
        ones = 0;
        ++tens;
      }
    }
    if (tens >= 10) {
      acc[0] = '1';
      tens = 0;
    }
    if (pct >= 10) {
      acc[1] = (uint8_t)('0' + tens);
    }
    acc[2] = (uint8_t)('0' + ones);
  }

  uint8_t rating;
  if (aircraft >= 4) {
    rating = 5; // FAILED
  } else if (flight_current_wp < 2) {
    rating = (uint8_t)(4 - flight_current_wp); // 0 -> INCOMPLETE, 1 -> PASS
  } else if (aircraft == 0 && (fired == 0 || pct >= 25)) {
    rating = 0; // ACE
  } else {
    rating = aircraft <= 1 ? 1 : 2; // VETERAN / SURVIVOR
  }
  _debrief_align(kDebriefRowRating, kDebriefAnswers[kDebriefRating + rating],
                 false);

  // Q or Port 2 Fire leaves the debrief: wait for release before and after.
  _debrief_wait_release();
  do {
    gfx_wait_vsync();
    keys_poll();
  } while (!key_pressed(KSCAN_Q) && !(joy2_state & kJoyFire));
  _debrief_wait_release();
}

#pragma optimize(pop)
