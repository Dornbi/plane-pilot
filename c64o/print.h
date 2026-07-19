#ifndef PRINT_H
#define PRINT_H

#include <stdint.h>

// clang-format off
#ifdef __OSCAR64__
#define SCREEN_STR(str) s ##str
#else
#define SCREEN_STR(str) str
#endif

#define STRL(str) str, sizeof(str)-1
// clang-format on

void print_label(uint16_t pos, const char *label);
void print_str(uint8_t row, uint8_t col, const char *str, uint8_t len);

void print_bcd(uint16_t pos, uint32_t value, uint8_t num_digits = 5);
void print_signed_bcd(uint16_t pos, int16_t value, uint8_t num_digits = 5);
void print_hex(uint16_t pos, uint32_t value, uint8_t num_digits = 4);

void print_labeled_bcd(uint16_t pos, const char *label, uint32_t value,
                       uint8_t num_digits = 5);
void print_labeled_signed_bcd(uint16_t pos, const char *label, int16_t value,
                              uint8_t num_digits = 5);
void print_labeled_hex(uint16_t pos, const char *label, uint32_t value,
                       uint8_t num_digits = 4);

// Returns the number of rows.
uint8_t print_lines(uint8_t row, uint8_t col, const char *text);

#pragma compile("print.cc")

#endif