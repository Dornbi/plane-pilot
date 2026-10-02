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
// indexes them.
static const uint16_t kCharDefCount = 224;

static const uint8_t kCharSolidGround = 128;
static const uint8_t kCharSolidSky = 0;
static const uint8_t kCharSolidGrad1 = 0;
static const uint8_t kCharSolid11 = 0;

extern const uint8_t chardefs[kCharDefCount][8];

#pragma compile("chardefs.cc")

#endif
