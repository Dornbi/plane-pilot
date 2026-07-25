#include "print.h"

#include <stdint.h>
#include <string.h>

#include "bcd.h"

// Declare these as external dependency instead
extern uint8_t *mem_screen_ram;
extern uint8_t *mem_screen_row_ptrs[25];

// Only reached on screen transitions and in the debug view, never from the
// per-frame render path, so the outliner's size-for-a-JSR trade is free here.
#pragma optimize(push, outline)

inline void print_label(uint16_t pos, const char *label) {
  memcpy(mem_screen_ram + pos, label, strlen(label));
}

inline void print_str(uint8_t row, uint8_t col, const char *label,
                      uint8_t num_chars) {
  memcpy(mem_screen_row_ptrs[row] + col, label, num_chars);
}

void print_bcd(uint16_t pos, uint32_t value, uint8_t num_digits) {
  bcd_convert32(value);

  uint8_t *dst = mem_screen_ram + pos;
  if (num_digits > 8) {
    num_digits = 8;
  }
  for (uint8_t d = 8 - num_digits; d < sizeof(bcd_result) << 1; ++d) {
    if (d & 0x01) {
      *dst = (bcd_result[d >> 1] & 0x0f) + '0';
    } else {
      *dst = (bcd_result[d >> 1] >> 4) + '0';
    }
    ++dst;
  }
}

void print_signed_bcd(uint16_t pos, int16_t value, uint8_t num_digits) {
  if (value < 0) {
    *(mem_screen_ram + pos) = '-';
    print_bcd(++pos, (uint32_t)(uint16_t)-value, num_digits);
  } else {
    *(mem_screen_ram + pos) = ' ';
    print_bcd(++pos, value, num_digits);
  }
}

void print_labeled_bcd(uint16_t pos, const char *label, uint32_t value,
                       uint8_t num_digits) {
  const uint8_t len = strlen(label);
  memcpy(mem_screen_ram + pos, label, len);
  print_bcd(pos + len, value, num_digits);
}

void print_labeled_signed_bcd(uint16_t pos, const char *label, int16_t value,
                              uint8_t num_digits) {
  const uint8_t len = strlen(label);
  memcpy(mem_screen_ram + pos, label, len);
  print_signed_bcd(pos + len, value, num_digits);
}

void print_hex(uint16_t pos, uint32_t value, uint8_t num_digits) {
  if (num_digits > 8) {
    num_digits = 8;
  }
  uint8_t *dst = mem_screen_ram + pos + num_digits;
  for (uint8_t i = 0; i < num_digits; ++i) {
    uint8_t digit = value & 0x0f;
    --dst;
    if (digit < 10) {
      *dst = digit + '0';
    } else {
      *dst = digit - 10 + 'A';
    }
    value >>= 4;
  }
}

void print_labeled_hex(uint16_t pos, const char *label, uint32_t value,
                       uint8_t num_digits) {
  const uint8_t len = strlen(label);
  memcpy(mem_screen_ram + pos, label, len);
  print_hex(pos + len, value, num_digits);
}

uint8_t print_lines(uint8_t row, uint8_t col, const char *text) {
  uint8_t c = 0;
  for (;;) {
    const char *p = strchr(text, '\n');
    if (p == nullptr) {
      break;
    }
    print_str(row + c, col, text, p - text);
    ++c;
    text = p + 1;
  }
  print_str(row + c, col, text, strlen(text));
  return c + 1;
}

#pragma optimize(pop)
