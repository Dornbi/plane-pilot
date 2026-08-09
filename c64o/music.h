#ifndef MUSIC_H
#define MUSIC_H

#include <stdint.h>

#include "bool.h"
#include "musicdef.h"
#include "sid.h"

// The title tune. See ../docs/music.md for the design; the short version is
// that this is a second, independent owner of the SID.
//
//   The flight driver (sound.cc) owns $D400 whenever raster IRQs are running,
//   because its blit lives in _switch_to_terrain.
//   The menu tune owns $D400 whenever music_playing is set, and it only runs
//   from gfx_wait_vsync() polling loops on screens that got there through
//   gfx_stop_raster_irqs() - which is a bare sei.
//
// The two are therefore mutually exclusive by construction rather than by
// arbitration, and neither needs to know the other exists. There is no shadow
// block and no torn-read problem here: music_tick() writes the chip directly,
// on the main line, with interrupts masked.
//
// Unlike sound.cc this file may have stack frames, locals and calls. It is not
// interrupt-side code.

#ifdef __ENABLE_SOUND__

// Set by music_start(), cleared by music_stop(). Read by music_tick(), which
// returns immediately when it is clear.
//
// This flag is the whole reason help_run() can tick the player unconditionally.
// help_run() has two callers - menu.cc when H is pressed in the menu, and
// sim.cc when H is pressed in flight - and only the first wants music. Both
// mask interrupts and both silence the flight driver, so without the flag,
// checking the controls mid-mission would start the title tune.
//
// Only menu_run() sets it. Help never starts or stops the tune; it only keeps
// ticking whatever is already running.
extern bool music_playing;

// Take the SID and start at bar 1. Call with raster IRQs already stopped -
// gfx_stop_raster_irqs() has by then called sound_silence(), so the chip
// arrives with every gate clear and the master volume at zero.
void music_start(void);

// Release the SID: gates clear, master volume zero, music_playing false.
// Must be called before control leaves the screen that started the tune,
// which in practice means before menu_run() returns.
void music_stop(void);

// One 50 Hz frame. Call once per gfx_wait_vsync() in a polling loop.
// No-op unless music_playing.
void music_tick(void);

// --- Exported for c64o/test/music_test.cc ---------------------------------
//
// The test cannot see anything except $D400, so the two functions whose
// mapping it has to assert on are exported rather than static. Both are pure.

// A MIDI note to its PAL $D400 frequency, via the octave-6 table shifted down.
// Notes below the table's range clamp rather than shifting to zero.
uint16_t music_note_freq(uint8_t midi);

// The tune's per-bar volume composed with the player's sound_volume setting.
// Never a multiply: a 3 x 16 table, where row 0 is all zeros so "sound off"
// is silent by construction, row 2 is the identity, and row 1 scales the ramp
// to a ceiling of 7 rather than clipping it flat. See ../docs/music.md
// section 3.
uint8_t music_master_volume(uint8_t bar_volume, uint8_t setting);

#else

// Compiled out entirely in ppilotd. The call sites stay unconditional and the
// linker sees nothing - including the 1.1 KB in musicdef.cc, whose body is
// guarded by the same flag.
inline void music_start(void) {}
inline void music_stop(void) {}
inline void music_tick(void) {}

#endif // __ENABLE_SOUND__

#pragma compile("music.cc")

#endif // MUSIC_H
