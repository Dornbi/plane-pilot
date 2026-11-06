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

// Prints `text` from (row, col), a row per '\n'; a '\t' pads with spaces to
// kPrintTabCol columns from col, the help screen's description column; and a
// byte from kPrintWord up prints one of the words the help and mission texts
// repeat: the PRINT_* below, each kPrintWord plus one more than its word's
// offset in print.cc's kPrintWords (static_asserted below), and each word,
// spaces included, the PRINT_WORD_* beside it. Returns the number of rows.
static const uint8_t kPrintTabCol = 9;
static const uint8_t kPrintWord = 0x80;
// clang-format off
#define PRINT_THE        "\x81"
#define PRINT_AND        "\x86"
#define PRINT_YOU        "\x8c"
#define PRINT_TOGGLE     "\x91"
#define PRINT_COMING     "\x99"
#define PRINT_BOMBERS    "\xa1"
#define PRINT_FIGHTERS   "\xaa"
#define PRINT_SHOOT_DOWN "\xb8"
#define PRINT_ENEMY      "\xcd"
#define PRINT_BEFORE     "\xd5"
#define PRINT_WORD_THE        " THE"
#define PRINT_WORD_AND        " AND "
#define PRINT_WORD_YOU        " YOU"
#define PRINT_WORD_TOGGLE     "TOGGLE "
#define PRINT_WORD_COMING     "COMING "
#define PRINT_WORD_BOMBERS    "BOMBERS "
#define PRINT_WORD_FIGHTERS   "FIGHTERS ARE "
#define PRINT_WORD_SHOOT_DOWN "SHOOT DOWN AT LEAST "
#define PRINT_WORD_ENEMY      " ENEMY "
#define PRINT_WORD_BEFORE     "BEFORE"
// clang-format on

// The offsets, checked in the host build (fmission.cc and the tests include
// this): oscar64 does not index a string literal in a static_assert.
#ifndef __OSCAR64__
#define PRINT_NEXT(a, b)                                                       \
  static_assert((uint8_t)PRINT_##b[0] ==                                       \
                    (uint8_t)PRINT_##a[0] + sizeof(PRINT_WORD_##a),            \
                "PRINT_" #b " follows PRINT_WORD_" #a)
static_assert((uint8_t)PRINT_THE[0] == kPrintWord + 1, "the first word");
PRINT_NEXT(THE, AND);
PRINT_NEXT(AND, YOU);
PRINT_NEXT(YOU, TOGGLE);
PRINT_NEXT(TOGGLE, COMING);
PRINT_NEXT(COMING, BOMBERS);
PRINT_NEXT(BOMBERS, FIGHTERS);
PRINT_NEXT(FIGHTERS, SHOOT_DOWN);
PRINT_NEXT(SHOOT_DOWN, ENEMY);
PRINT_NEXT(ENEMY, BEFORE);
#undef PRINT_NEXT
#endif
uint8_t print_lines(uint8_t row, uint8_t col, const char *text);

#pragma compile("print.cc")

#endif