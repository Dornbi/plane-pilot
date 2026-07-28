#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../msg.h"

// Stubs for C64 hardware dependencies when compiling host test
uint8_t *mem_screen_ram = nullptr;
uint8_t *mem_screen_row_ptrs[25];
uint8_t color_buffer_dummy[560];
extern uint8_t *const mem_color_buffer = color_buffer_dummy;

int main() {
  printf("Running msg_test...\n");

  uint8_t row0[40];
  memset(row0, ' ', sizeof(row0));
  mem_screen_row_ptrs[0] = row0;

  msg_clear();

  // Test 1: Basic temporary message
  msg_show("TEST", 5, false);
  msg_render();
  // Centered length 4 in 40 cols -> offset = (40 - 4)/2 = 18
  assert(memcmp(row0 + 18, "TEST", 4) == 0);
  assert(mem_color_buffer[18] == 0);

  // Update decay
  for (int i = 0; i < 4; ++i) {
    msg_update();
  }
  memset(row0, ' ', sizeof(row0));
  msg_render();
  assert(memcmp(row0 + 18, "TEST", 4) == 0);

  // 5th update expires it
  msg_update();
  memset(row0, ' ', sizeof(row0));
  msg_restore_color();
  msg_render();
  assert(memcmp(row0 + 18, "    ", 4) == 0);
  // The color buffer must be back to a multicolor value (bit 3 set), or the
  // ground chars on row 0 would render as hires.
  for (int i = 18; i < 22; ++i) {
    assert(mem_color_buffer[i] == (6 | 0x08));
  }
  // Restoring twice is a no-op: the second call must not clobber colors
  // written by the sky/box rendering.
  memset(mem_color_buffer + 18, 0xAA, 4);
  msg_restore_color();
  assert(mem_color_buffer[18] == 0xAA);

  // Test 2: Permanent message overrides temporary message
  msg_show("YOU CRASHED", MSG_FOREVER, true);
  memset(row0, ' ', sizeof(row0));
  msg_render();
  // Length 11 -> offset = (40 - 11)/2 = 14
  assert(memcmp(row0 + 14, "YOU CRASHED", 11) == 0);

  // Try overwriting permanent message with a temporary message (should be
  // ignored)
  msg_show("TEMP MSG", 10, false);
  memset(row0, ' ', sizeof(row0));
  msg_render();
  assert(memcmp(row0 + 14, "YOU CRASHED", 11) == 0);

  // Permanent message stays across updates
  for (int i = 0; i < 100; ++i) {
    msg_update();
  }
  memset(row0, ' ', sizeof(row0));
  msg_render();
  assert(memcmp(row0 + 14, "YOU CRASHED", 11) == 0);

  // Clear message
  msg_clear();
  memset(row0, ' ', sizeof(row0));
  msg_render();
  assert(memcmp(row0 + 14, "           ", 11) == 0);

  // Test 3: the message touches only its own span, and reports that span in
  // screen pixels so overlapping sprites can hide themselves. Anything wider
  // than the text would make sprites disappear when they are nowhere near it.
  memset(row0, 0xEE, sizeof(row0));
  memset(color_buffer_dummy, 0xEE, 40);
  msg_show("TEST", 5, false);
  assert(msg_active());
  assert(msg_span_x0 == 18 * 8);
  assert(msg_span_x1 == 22 * 8);
  msg_render();
  for (int i = 0; i < 40; ++i) {
    if (i < 18 || i >= 22) {
      assert(row0[i] == 0xEE);
      assert(mem_color_buffer[i] == 0xEE);
    } else {
      assert(mem_color_buffer[i] == 0);
    }
  }
  msg_restore_color();
  for (int i = 18; i < 22; ++i) {
    assert(mem_color_buffer[i] == (6 | 0x08));
  }

  // The span follows the message width, so a short message frees most of the
  // row and a full width one covers all of it.
  msg_clear();
  msg_show("HI", 5, false);
  assert(msg_span_x0 == 19 * 8 && msg_span_x1 == 21 * 8);
  msg_clear();
  msg_show("0123456789012345678901234567890123456789", 5, false);
  assert(msg_span_x0 == 0 && msg_span_x1 == 40 * 8);
  msg_clear();
  assert(!msg_active());

  printf("msg_test passed successfully!\n");
  return 0;
}
