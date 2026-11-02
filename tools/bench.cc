// Times the flight loop's work per frame in a scene, for tools/bench.sh.
//
// Like tools/shot.cc, this is never part of a real build. bench.sh copies
// c64o/ to a scratch directory, adds this file and shot.cc beside it, and
// wraps a frame's work in sim_run() - bm_model_start() through bm_total(),
// the model step and the render, everything but the wait for the flip - in
// bench_begin() and bench_end(). shot.cc's script then drives the menu and
// sets up the scene.
//
// In a frozen scene frames are counted only once the model is frozen; in a
// live one (-D__BENCH_LIVE__) from the first frame of the mission. The first
// __BENCH_SKIP__ of those are thrown away: in a frozen scene they still carry
// the mission title message and the double buffer settling, in a live one
// they are the run-up to what is to be timed. The next __BENCH_FRAMES__ are
// summed into g_bench_cycles, and then the program parks in a spin loop for
// tools/vice_dump.sh to break on. VICE is cycle-exact, and a live scene's
// model steps once a frame whatever a frame costs, so the same binary gives
// the same number on every run; what moves it is the code.

#include "bench.h"

#include <stdint.h>

#include "cia.h"
#include "flight.h"

#ifndef __BENCH_SKIP__
#define __BENCH_SKIP__ 40
#endif
#ifndef __BENCH_FRAMES__
#define __BENCH_FRAMES__ 16
#endif
static const uint16_t kBenchSkip = __BENCH_SKIP__;
static const uint16_t kBenchFrames = __BENCH_FRAMES__;
// bench_end() loads the timers at the end of the last skipped frame.
static_assert(kBenchSkip > 0, "a scene must skip at least one frame");

// volatile, or oscar64 deletes it: it is only ever written.
volatile uint32_t g_bench_cycles;

static uint16_t _bench_frame = 0;

// Timer B counts timer A's underflows, so the pair is one 32-bit down-counter,
// and timer A runs only between bench_begin() and bench_end(): the pair sums
// the frames' work by itself, with no 32-bit arithmetic here. That keeps this
// file small enough to link beside fpilot, which has no room to spare.
void bench_begin(void) {
  cia2.cra = 0x01;
}

void bench_end(void) {
  cia2.cra = 0;
#ifndef __BENCH_LIVE__
  if (!flight_paused) {
    return;
  }
#endif
  ++_bench_frame;
  if (_bench_frame == kBenchSkip) {
    // Both latches to $FFFF, force-loaded, B counting A's underflows; A
    // loaded but left stopped until the next bench_begin().
    cia2.ta = 0xffff;
    cia2.tb = 0xffff;
    cia2.crb = 0x51;
    cia2.cra = 0x10;
  } else if (_bench_frame == kBenchSkip + kBenchFrames) {
    const uint16_t b = cia2.tb;
    const uint16_t a = cia2.ta;
    g_bench_cycles = ~(((uint32_t)b << 16) | a);
    for (;;) {
    }
  }
}
