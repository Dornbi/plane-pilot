#ifndef KEYS_H
#define KEYS_H

#include "bool.h"

#ifdef __OSCAR64__
#include <c64/keyboard.h>
#else
enum KeyScanCode {
  KSCAN_SPACE,
  KSCAN_A,
  KSCAN_D,
  KSCAN_F,
  KSCAN_I,
  KSCAN_J,
  KSCAN_K,
  KSCAN_L,
  KSCAN_P,
  KSCAN_R,
  KSCAN_S,
  KSCAN_T,
  KSCAN_X,
  KSCAN_Z,
  KSCAN_PLUS,
  KSCAN_MINUS,
};

inline void keyb_poll() {}
inline bool key_pressed(enum KeyScanCode key) { return false; }
#endif

#endif