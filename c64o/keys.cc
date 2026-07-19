#include "keys.h"

inline uint8_t keys_edges(uint8_t current, uint8_t *prev) {
  uint8_t edges = current & ~*prev;
  *prev = current;
  return edges;
}

inline void keys_wait_release(enum KeyScanCode key) {
  while (key_pressed(key)) {
    keyb_poll();
  }
}
