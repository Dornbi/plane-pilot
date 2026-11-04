#include "keys.h"

#ifdef __OSCAR64__
#include <c64/cia.h>

void keys_poll(void) {
  cia1.ddra = 0xff;
  // All rows deselected, so port B reads only the joystick in port 1.
  cia1.pra = 0xff;
  if (cia1.prb == 0xff) {
    cia1.ddrb = 0x00;
    uint8_t row = 0xfe;
    for (uint8_t i = 0; i < 8; ++i) {
      cia1.pra = row;
      row = (row << 1) | 1;
      keyb_matrix[i] = cia1.prb;
    }
  }
  cia1.pra = ciaa_pra_def;
}

__noinline bool keys_pressed(enum KeyScanCode key) {
  return !(keyb_matrix[key >> 3] & kBit[key & 7]);
}
#endif

__noinline uint8_t keys_scan(const uint8_t *keys, uint8_t count) {
  uint8_t down = 0;
  uint8_t bit = 1;
  for (uint8_t i = 0; i < count; ++i) {
    if (keys_pressed((enum KeyScanCode)keys[i])) {
      down |= bit;
    }
    bit <<= 1;
  }
  return down;
}

inline uint8_t keys_edges(uint8_t current, uint8_t *prev) {
  uint8_t edges = current & ~*prev;
  *prev = current;
  return edges;
}

__noinline void keys_wait_release(enum KeyScanCode key) {
  while (keys_pressed(key)) {
    keys_poll();
  }
}
