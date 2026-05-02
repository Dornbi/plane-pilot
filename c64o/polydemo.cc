#include "poly.h"

#include <string.h>

#include "benchmark.h"
#include "cia.h"
#include "mem.h"

// --- Example Usage ---
int main() {
  cia_init();
  bm_init();
  mem_screen_ram = (uint8_t *)0x0400;
  memset(mem_screen_ram, ' ', 1000);
  mem_debug_enabled = true;

  // Clear the screen (basic spaces)
  for (int i = 0; i < 1000; i++) {
    mem_screen_ram[i] = 32;
  }

  // Define a convex diamond/kite shape
  vertex_t poly[] = {
      {20, 2},  // Top
      {35, 10}, // Right
      {20, 22}, // Bottom
      {5, 10}   // Left
  };

  // Infinite loop to keep the screen visible
  while (1) {
    // Fill with character '8' (PETSCII solid circle/checkerboard depending on
    // charset)
    fill_poly(poly, 4, 81);
  }

  return 0;
}