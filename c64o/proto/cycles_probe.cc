// Cycle cost of one flight_advance() on a 6510, measured rather than estimated.
//
//   make -C c64o/proto cycles
//
// The method is docs/emulator.md's: run the CIA2 timers as a 32-bit counter,
// read them either side of the call, park on a spin loop and let
// tools/vice_dump.sh read the globals back out. The results land in
// docs/flight.md 1.
//
// Two scenarios, because the cost splits in two and the split is the whole
// story: a step that does not re-orthonormalize, and one that does.
// vec_orthonormalize() was 58% of the old model's turning step and is
// unchanged by the angle-of-attack work, so what matters is what moved
// *beside* it.
// mem.h first, and built with -D__MAX_RAM__ like the game is: flight.cc puts
// its map trail in bss2, and that section only exists under that define. Same
// memory map as ppilot.prg, so the code being timed is laid out the way the
// game lays it out.
#include "../mem.h"

#include <c64/cia.h>

#include "../flight.h"
#include "../vec.h"

volatile uint16_t g_level_cycles;
volatile uint16_t g_turn_cycles;
// Witnesses, so a reading cannot quietly be of the wrong thing: the level run
// must not be stalling (the break sets model_need_normalize and turns the
// cheap step into the expensive one) and the turning run must be turning.
volatile uint16_t g_level_stall;
volatile uint16_t g_level_alpha;
// vec_orthonormalize() on its own, because the step's cost splits in two and
// the split is most of the story. flight.md 1 has carried an estimate of
// ~4,500 for it; this is what it actually costs.
volatile uint16_t g_ortho_cycles;
volatile uint16_t g_turn_stall;
// The bank *after* the timed steps, so it describes the steps that were timed
// rather than the state they started from.
volatile uint16_t g_turn_leftz_end;

// Timer A alone, sixteen bits, counting phi2 down from 0xffff.
//
// The 32-bit pair of docs/emulator.md is for timing a whole frame. It cannot
// time one call reliably, because reading ta and tb is two instructions and ta
// can underflow between them - tb has decremented and ta has reloaded, and the
// combined value jumps by 65536. That showed up here as readings of
// 0x40001621: the right answer in the low word and nonsense above it.
//
// One flight_advance() is well under 65536 cycles, so one timer is enough and
// one read is atomic.
static void _timers(void) {
  cia2.cra = 0;
  cia2.ta = 0xffff;
  cia2.cra = 0x11; // Start, continuous
}

// The cheapest of `n` timed calls. Cheapest rather than mean because the only
// thing that can make a call look dearer than it is - an interrupt landing
// inside the window - cannot make one look cheaper.
#define kRuns 16

static uint16_t _time_advance(uint8_t n) {
  uint16_t best = 0xffff;
  while (n--) {
    const uint16_t t0 = cia2.ta;
    flight_advance();
    const uint16_t t1 = cia2.ta;
    const uint16_t d = t0 - t1;
    if (d < best) {
      best = d;
    }
  }
  return best;
}

int main(void) {
  flight_set_step_shift(0);
  _timers();

  // --- Straight and level, no control input ---------------------------------
  //
  // Trimmed, not merely untouched. Level flight needs a positive angle of
  // attack, so front.z = 0 at cruise throttle is a descent that ends in a
  // stall - and a stalling step re-orthonormalizes, which is the expensive
  // case and not the one this is meant to measure. 10 is the measured level
  // trim at throttle 0x14.
  //
  // The altitude is held through the settle for the same reason: left to fall,
  // the aeroplane reaches the ground inside the run and the timing is of a
  // ground roll.
  flight_init();
  flight_throttle = 0x14;
  flight_fuel = 0x0FFFFFFF;
  flight_cam.front.z = 10;
  vec_orthonormalize(&flight_cam);
  for (uint8_t i = 0; i < 120; ++i) {
    flight_cam.front.z = 10;
    flight_eye_z = 0x040000;
    flight_advance();
    flight_throttle = 0x14;
  }
  flight_eye_z = 0x040000;
  g_level_stall = flight_stall ? 1 : 0;
  g_level_alpha = (uint16_t)flight_alpha();
  g_level_cycles = _time_advance(kRuns);

  // --- A step that re-orthonormalizes ---------------------------------------
  //
  // Guaranteed to be the expensive path rather than merely likely to be. An
  // earlier version of this rolled the aeroplane into a turn and timed the
  // steps that followed, which is what the game does but is not something a
  // bench can hold still: the bank evolves during the timed window, the turn
  // rate rounds to zero at some of it, and the reading moved between 6,654 and
  // 19,470 depending on where in the turn the sixteen steps happened to fall.
  //
  // One roll input before each step sets model_need_normalize, so every timed
  // step takes the branch. The input is outside the timed window.
  flight_init();
  flight_throttle = 0x18;
  flight_fuel = 0x0FFFFFFF;
  flight_cam.front.z = 10;
  vec_orthonormalize(&flight_cam);
  for (uint8_t i = 0; i < 60; ++i) {
    flight_cam.front.z = 10;
    flight_eye_z = 0x040000;
    flight_advance();
    flight_throttle = 0x18;
  }
  {
    uint16_t best = 0xffff;
    for (uint8_t i = 0; i < kRuns; ++i) {
      flight_eye_z = 0x040000;
      flight_input(FLIGHT_INPUT_ROLL_RIGHT);
      flight_input(FLIGHT_INPUT_ROLL_LEFT); // Back again: the attitude does not
                                            // wander, only the flag is set
      const uint16_t t0 = cia2.ta;
      flight_advance();
      const uint16_t t1 = cia2.ta;
      const uint16_t d = t0 - t1;
      if (d < best) {
        best = d;
      }
      flight_throttle = 0x18;
    }
    g_turn_cycles = best;
    g_turn_stall = flight_stall ? 1 : 0;
    g_turn_leftz_end = (uint16_t)flight_cam.left.z;
  }

  // --- vec_orthonormalize on its own ----------------------------------------
  {
    const mat3_t m = flight_cam;
    uint16_t best = 0xffff;
    for (uint8_t i = 0; i < kRuns; ++i) {
      flight_cam = m;
      const uint16_t t0 = cia2.ta;
      vec_orthonormalize(&flight_cam);
      const uint16_t t1 = cia2.ta;
      const uint16_t d = t0 - t1;
      if (d < best) {
        best = d;
      }
    }
    g_ortho_cycles = best;
  }

  for (;;) { // Something for @spin to break on
  }
}
