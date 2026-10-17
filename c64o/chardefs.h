// --------------------------------------------------------------------------
// GENERATED FILE - DO NOT EDIT.
//
// Regenerate with ./generate_all.sh from the repository root.
// Produced by generate_all.py via lib/batch_generator.py.
// --------------------------------------------------------------------------

#ifndef CHARDEFS_H
#define CHARDEFS_H

#include <stdint.h>

// 333 characters in the global set, of which chardefs holds
// 224: the ones the boxes reference, each stored once per vertical
// flip (lib/find_boxes.py build_c_charset()). Fewer than 256, so a byte
// indexes them. They overlap inside one 728 B stream, numbered in
// the order they start in it. kCharDefPage1 is the first one that starts
// 256 B or more in, kCharDefPage2 512 B or more (kCharDefCount if none).
static const uint16_t kCharDefCount = 224;
static const uint16_t kCharDefBytes = 728;
static const uint8_t kCharDefPage1 = 91;
static const uint8_t kCharDefPage2 = 164;

static const uint8_t kCharSolidGround = 128;
static const uint8_t kCharSolidSky = 0;
static const uint8_t kCharSolidGrad1 = 0;
static const uint8_t kCharSolid11 = 0;

extern const uint8_t chardef_bytes[kCharDefBytes];
extern const uint8_t chardef_offset_lo[kCharDefCount];

#pragma compile("chardefs.cc")

#endif
