#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

#include "sid.h"

// Flight audio. See ../docs/sound.md for the design; the short version is
// that the driver is split by rate.
//
//   sound_update()  runs on the main line, once per frame (~10 Hz), and its
//                   only output is sound_shadow[].
//   sound_blit()    runs from _switch_to_terrain at raster 250 and copies
//                   sound_shadow[] to $D400.
//
// The split exists for three reasons. oscar64 allocates @stack frames
// statically by call graph, and the raster handlers are installed as function
// pointers through rirq_call(), so they are invisible to that analysis - an
// interrupt-side function needing a frame may be overlaid onto a live one and
// corrupt it. Keeping all the work on the main line means only sound_blit()
// has to be frame-free, and it is (no locals, no calls). Second, the frame
// rate is a bad clock: it wobbles with roll angle and polygon load, and since
// gfx_wait_flip_window() replaced the vsync edge it does not even quantize to
// the video clock. Third, sound_update() is then a pure function of flight
// state, which is what makes it host-testable.
//
// The cost is up to 100 ms of latency on an effect's onset. That is one video
// frame at the current frame rate.

// The writable SID registers, $D400..$D418. $D419..$D41C are read-only
// (paddles, noise, envelope 3) and are not part of the block.
static const uint8_t kSoundRegCount = 25;

// Indices into sound_shadow of the three voice control registers, which is
// where the gate bit lives. Voices are 7 registers apart.
static const uint8_t kSoundRegV1Ctrl = 4;
static const uint8_t kSoundRegV2Ctrl = 11;
static const uint8_t kSoundRegV3Ctrl = 18;

// What the SID should hold. Written by sound_update() and sound_silence() on
// the main line, read by sound_blit() from the interrupt. A torn read is
// harmless: the two halves are both valid register sets and the next tick
// corrects it 20 ms later.
extern uint8_t sound_shadow[kSoundRegCount];

// The retrigger handshake. A held gate blits harmlessly every tick, but a new
// effect needs a 1 -> 0 -> 1 transition, and routing that through the shadow
// alone would cost a whole frame of silence. So sound_update() bumps
// sound_gen when it starts an effect and sound_blit() does the gate-off pass
// inline when it notices.
//
// Only the main line writes sound_gen; only sound_blit() writes
// sound_gen_seen. One writer per byte, so no interrupt masking is needed on
// either side.
extern uint8_t sound_gen;
extern uint8_t sound_gen_seen;

// Zeroes the shadow and the chip. Call once before the raster interrupts
// start.
void sound_init(void);

// Recomputes sound_shadow from the current flight state. Main line only,
// once per frame.
void sound_update(void);

// Releases the SID: silences the chip and zeroes the shadow, so that a blit
// after the interrupts come back does not restore whatever was playing.
//
// Called from gfx_stop_raster_irqs(), which is the one operation that means
// "the driver has stopped running" - it is reached only from
// screen_enter_static_mccm() (menu, help) and map_enter(). That gives the
// invariant the whole design leans on:
//
//   The flight driver owns the SID whenever raster IRQs are running.
//   Anything else that wants the SID must take ownership explicitly, and
//   silence it on release.
//
// Everything else - paused, crashed, out of fuel, reset - needs no call at
// all, because the simulation loop keeps running and sound_update() derives
// those from state.
void sound_silence(void);

// Copies the shadow to $D400. Interrupt side, inlined into
// _switch_to_terrain, and deliberately defined here rather than in sound.cc
// so it lands inside gfx.cc's #pragma optimize(noasm, nooutline) region.
//
// Must stay flat: no locals the compiler could spill into an @stack frame, no
// calls. Verify after building that ppilot.map has no sound_blit@stack entry.
//
// Keep it after the mode-switch writes in the handler. The blit is ~200
// cycles and the handler's first job - latching mem_using_alt_buffer into
// $d018 - is the only part with a deadline; a late blit only delays itself,
// in the lower border, where nothing is watching.
inline void sound_blit(void) {
  if (sound_gen != sound_gen_seen) {
    sound_gen_seen = sound_gen;
    // Gate off. The full copy below immediately re-writes these three with
    // the gate bit as the shadow has it, so a voice whose gate is set sees a
    // 1 -> 0 -> 1 edge and retriggers its envelope. Several register writes
    // separate the two, which is far more than the one cycle the SID needs.
    sid.voices[0].ctrl = sound_shadow[kSoundRegV1Ctrl] & ~SID_CTRL_GATE;
    sid.voices[1].ctrl = sound_shadow[kSoundRegV2Ctrl] & ~SID_CTRL_GATE;
    sid.voices[2].ctrl = sound_shadow[kSoundRegV3Ctrl] & ~SID_CTRL_GATE;
  }

#pragma unroll(full)
  for (uint8_t i = 0; i < kSoundRegCount; ++i) {
    ((volatile uint8_t *)0xD400)[i] = sound_shadow[i];
  }
}

#pragma compile("sound.cc")

#endif // SOUND_H
