#include "print.h"

#include <stdint.h>
#include <string.h>

extern uint8_t *mem_screen_ram;

static uint8_t _print_bcd_result[4];

void _print_convert_to_bcd(uint32_t value) {
  __asm {
    sed;

    lda #0; // Initialize BCD results to 0
    sta _print_bcd_result;
    sta _print_bcd_result+1;
    sta _print_bcd_result+2;
    sta _print_bcd_result+3;

    ldx #24; // Loop for 24 bits
  L1:
    asl value; // Shift input value left
    rol value+1;
    rol value+2;

    // Shift the carry into the BCD bytes and add to self (doubling)
    // In decimal mode, ADC performing (N + N + Carry) effectively
    // handles the "If > 4, add 3" logic of Double Dabble automatically.
    lda _print_bcd_result+3;
    adc _print_bcd_result+3;
    sta _print_bcd_result+3;

    lda _print_bcd_result+2;
    adc _print_bcd_result+2;
    sta _print_bcd_result+2;

    lda _print_bcd_result+1;
    adc _print_bcd_result+1;
    sta _print_bcd_result+1;

    lda _print_bcd_result;
    adc _print_bcd_result;
    sta _print_bcd_result;

    dex;
    bne L1;

    cld;
  }
}

inline void print_label(uint16_t pos, const char *label) {
  memcpy(mem_screen_ram + pos, label, strlen(label));
}

void print_bcd(uint16_t pos, uint32_t value, uint8_t num_digits) {
  _print_convert_to_bcd(value);

  uint8_t *dst = mem_screen_ram + pos;
  if (num_digits > 8) {
    num_digits = 8;
  }
  for (uint8_t d = 8 - num_digits; d < sizeof(_print_bcd_result) << 1; ++d) {
    if (d & 0x01) {
      *dst = (_print_bcd_result[d >> 1] & 0x0f) + '0';
    } else {
      *dst = (_print_bcd_result[d >> 1] >> 4) + '0';
    }
    ++dst;
  }
}

void print_signed_bcd(uint16_t pos, int32_t value, uint8_t num_digits) {
  if (value < 0) {
    *(mem_screen_ram + pos) = '-';
    print_bcd(++pos, (uint32_t)-value, num_digits);
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

void print_labeled_signed_bcd(uint16_t pos, const char *label, int32_t value,
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