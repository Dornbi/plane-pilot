#ifndef KEYS_H
#define KEYS_H

#include "bool.h"
#include <stdint.h>

#ifdef __OSCAR64__
#include <c64/keyboard.h>
#else
enum KeyScanCode {
  KSCAN_SPACE,
  KSCAN_1,
  KSCAN_2,
  KSCAN_3,
  KSCAN_A,
  KSCAN_D,
  KSCAN_F,
  KSCAN_G,
  KSCAN_H,
  KSCAN_I,
  KSCAN_J,
  KSCAN_K,
  KSCAN_L,
  KSCAN_M,
  KSCAN_N,
  KSCAN_P,
  KSCAN_Q,
  KSCAN_R,
  KSCAN_S,
  KSCAN_T,
  KSCAN_X,
  KSCAN_Z,
  KSCAN_PLUS,
  KSCAN_MINUS,
  KSCAN_RETURN,
};

inline void keyb_poll() {}
inline bool key_pressed(enum KeyScanCode key) { return false; }
#endif

// Returns which bits of `current` were not set in `*prev` (i.e. the keys
// that transitioned from released to pressed since the last call), and
// updates `*prev` to `current`. Shared by every screen that has toggle keys
// (menu selection, pause, debug view, map, help, ...) so they act once per
// key press instead of once per polled frame while the key is held.
uint8_t keys_edges(uint8_t current, uint8_t *prev);

// Polls the keyboard until `key` is no longer pressed. Used after handling
// a momentary key (map, help, quit) so the same press isn't seen again on
// the next loop iteration.
void keys_wait_release(enum KeyScanCode key);

#pragma compile("keys.cc")

#endif