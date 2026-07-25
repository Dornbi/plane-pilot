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
  msg_render();
  assert(memcmp(row0 + 18, "    ", 4) == 0);

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

  printf("msg_test passed successfully!\n");
  return 0;
}
