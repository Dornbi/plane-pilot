#ifndef KEYS_H
#define KEYS_H

#include "bool.h"

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

#endif