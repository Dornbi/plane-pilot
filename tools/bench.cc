// Times the flight loop's render work in a frozen scene, for tools/bench.sh.
//
// Like tools/shot.cc, this is never part of a real build. bench.sh copies
// c64o/ to a scratch directory, adds this file and shot.cc beside it, and
// wraps the render work in sim_run() - bm_start() through bm_total(), the
// span docs/framerate.md calls "render work" - in bench_begin() and
// bench_end(). shot.cc's script then drives the menu and freezes the scene.
//
// Frames are counted only once the model is frozen, and the first
// kBenchSkip of those are thrown away: they still carry the mission title
// message and the double buffer settling. The next kBenchFrames are summed
// into g_bench_cycles, and then the program parks in a spin loop for
// tools/vice_dump.sh to break on. VICE is cycle-exact and the scene does not
// move, so the same binary gives the same number on every run; what moves it
// is the code.

#include "bench.h"

#include <stdint.h>

#include "cia.h"
#include "flight.h"

#if __SHOT__ == 9
static const uint8_t kBenchSkip = 1;
#else
static const uint8_t kBenchSkip = 40;
#endif
static const uint8_t kBenchFrames = 16;

// volatile, or oscar64 deletes it: it is only ever written.
volatile uint32_t g_bench_cycles;

static bool _bench_running = false;
static uint8_t _bench_frame = 0;
static uint32_t _bench_t0;

// Timer B counts timer A underflows, so the pair is one 32-bit down-counter.
// Re-read when B moved between the two reads, so an underflow cannot glitch
// the sample by 65,536 cycles - benchmark.cc's readings can, which is fine
// for a debug readout and not for a 2% gate.
static uint32_t _bench_now(void) {
  uint16_t b, a;
  do {
    b = cia2.tb;
    a = cia2.ta;
  } while (b != cia2.tb);
  return ((uint32_t)b << 16) | a;
}

void bench_begin(void) {
  if (!_bench_running) {
    // bm_init() is a no-op outside debug builds, so start the timers here.
    cia2.cra = 0;
    cia2.crb = 0;
    cia2.ta = 0xffff;
    cia2.tb = 0xffff;
    cia2.crb = 0x41;
    cia2.cra = 0x11;
    _bench_running = true;
  }
  _bench_t0 = _bench_now();
}

void bench_end(void) {
  const uint32_t cycles = _bench_t0 - _bench_now();
#if __SHOT__ != 9
  if (!flight_paused) {
    return;
  }
#endif
  ++_bench_frame;
  if (_bench_frame > kBenchSkip) {
    g_bench_cycles += cycles;
  }
  if (_bench_frame == kBenchSkip + kBenchFrames) {
    for (;;) {
    }
  }
}
