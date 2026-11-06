#include "print.h"

#include <stdint.h>
#include <string.h>

#include "bcd.h"

// Declare these as external dependency instead
extern uint8_t *mem_screen_ram;
extern __striped uint8_t *mem_screen_row_ptrs[25];

// Only reached on screen transitions and in the debug view, never from the
// per-frame render path, so the outliner's size-for-a-JSR trade is free here.
#pragma optimize(push, outline)

inline void print_label(uint16_t pos, const char *label) {
  memcpy(mem_screen_ram + pos, label, strlen(label));
}

// __noinline: ten call sites, none of them per frame.
__noinline void print_str(uint8_t row, uint8_t col, const char *label,
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

// print.h's PRINT_* words, in their order, each ended by its 0. A PRINT_* is
// kPrintWord plus one more than its word's offset: w below, which is 0 while
// no word is being printed.
// clang-format off
static const char kPrintWords[] =
    PRINT_WORD_THE "\0"
    PRINT_WORD_AND "\0"
    PRINT_WORD_YOU "\0"
    PRINT_WORD_TOGGLE "\0"
    PRINT_WORD_COMING "\0"
    PRINT_WORD_BOMBERS "\0"
    PRINT_WORD_FIGHTERS "\0"
    PRINT_WORD_SHOOT_DOWN "\0"
    PRINT_WORD_ENEMY "\0"
    PRINT_WORD_BEFORE;
// clang-format on

// A byte at a time: no strchr() and strlen() over each line first. A word is
// printed by the same loop, from kPrintWords, until its 0.
uint8_t print_lines(uint8_t row, uint8_t col, const char *text) {
  uint8_t c = 1, x = col;
  uint8_t *dst = mem_screen_row_ptrs[row];
  uint8_t w = 0;
  for (;;) {
    char ch;
    if (w != 0) {
      ch = kPrintWords[w - 1];
      ++w;
    } else {
      ch = *text++;
      if ((uint8_t)ch >= kPrintWord) {
        w = (uint8_t)ch - kPrintWord;
        continue;
      }
    }
    if (ch == 0) {
      if (w == 0) {
        return c;
      }
      w = 0;
    } else if (ch == '\n') {
      dst = mem_screen_row_ptrs[++row];
      x = col;
      ++c;
    } else if (ch == '\t') {
      while (x < (uint8_t)(col + kPrintTabCol)) {
        dst[x++] = ' ';
      }
    } else {
      dst[x++] = ch;
    }
  }
}

#pragma optimize(pop)
