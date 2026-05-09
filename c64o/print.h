#ifndef PRINT_H
#define PRINT_H

#include <stdint.h>

// clang-format off
#ifdef __OSCAR64__
#define SCREEN_STR(str) s ##str
#else
#define SCREEN_STR(str) str
#endif
// clang-format on

inline void print_label(uint16_t pos, const char *label);
void print_bcd(uint16_t pos, uint32_t value, uint8_t num_digits = 5);
void print_signed_bcd(uint16_t pos, int32_t value, uint8_t num_digits = 5);
void print_labeled_bcd(uint16_t pos, const char *label, uint32_t value,
                       uint8_t num_digits = 5);
void print_labeled_signed_bcd(uint16_t pos, const char *label, int32_t value,
                              uint8_t num_digits = 5);

#pragma compile("print.cc")

#endif