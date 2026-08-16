// --------------------------------------------------------------------------
// GENERATED FILE - DO NOT EDIT.
//
// Regenerate with ./generate_all.sh from the repository root.
// Produced by generate_all.py via lib/find_boxes.py.
// --------------------------------------------------------------------------

#include "boxdefs.h"

#include <stddef.h>
#include <string.h>

#include "roll.h"

#pragma data(data_box)

static const uint8_t box_d8_idx[] = { 44, 0, 43, 30 };
static const uint8_t box_d8_chars[] = { 2, 5, 6, 3, 4, 2, 5, 6, 3, 4, 2, 5, 6, 3, 4, 2, 5, 6, 3, 4 };
static const boxdef_t box_d8_def = {
    5, // w
    4, // h
    20, // total_size
    0, // step_x
    4, // step_y
    0, // rel_x
    0, // rel_y
    2, // grad1_color_start
    4, // char_count
    214, // char_offset
    box_d8_idx, // char_idx
    box_d8_chars // box_chars
};

static const uint8_t box_d8_alt_idx[] = { 31, 0, 39, 27, 8 };
static const uint8_t box_d8_alt_chars[] = { 5, 2, 6, 7, 3, 4, 5, 2, 6, 7, 3, 4, 5, 2, 6, 7, 3, 4, 5, 2, 6, 7, 3, 4 };
static const boxdef_t box_d8_alt_def = {
    6, // w
    4, // h
    24, // total_size
    0, // step_x
    4, // step_y
    0, // rel_x
    0, // rel_y
    2, // grad1_color_start
    5, // char_count
    224, // char_offset
    box_d8_alt_idx, // char_idx
    box_d8_alt_chars // box_chars
};

static const uint8_t box_l10d16_idx[] = { 59, 56, 66, 75, 70, 58, 55, 78, 60, 85, 29, 86, 87, 88, 0, 30, 74, 80, 69, 84, 89, 79, 39, 81, 82, 73, 83 };
static const uint8_t box_l10d16_chars[] = { 0, 0, 0, 0, 12, 2, 13, 14, 3, 4, 5, 0, 0, 0, 15, 16, 17, 18, 19, 6, 7, 1, 0, 0, 0, 20, 2, 21, 22, 8, 4, 5, 1, 0, 0, 23, 2, 13, 14, 3, 9, 5, 1, 1, 0, 0, 24, 2, 18, 25, 6, 4, 1, 1, 1, 0, 20, 2, 21, 14, 10, 4, 5, 1, 1, 1, 26, 27, 28, 18, 3, 11, 7, 1, 1, 1, 1, 29, 2, 21, 22, 8, 4, 5, 1, 1, 1, 1 };
static const boxdef_t box_l10d16_def = {
    11, // w
    8, // h
    88, // total_size
    -5, // step_x
    8, // step_y
    -5, // rel_x
    0, // rel_y
    9, // grad1_color_start
    27, // char_count
    158, // char_offset
    box_l10d16_idx, // char_idx
    box_l10d16_chars // box_chars
};

static const uint8_t box_l10u16_idx[] = { 50, 46, 47, 36, 48, 65, 0, 63, 59, 32, 40, 39, 55, 64, 60, 54, 61, 35, 41, 66, 47, 58, 1, 62, 49, 51, 57, 56 };
static const uint8_t box_l10u16_chars[] = { 3, 4, 5, 13, 14, 15, 16, 0, 0, 0, 0, 1, 6, 7, 17, 18, 14, 19, 0, 0, 0, 0, 1, 8, 4, 9, 20, 21, 2, 22, 0, 0, 0, 1, 1, 3, 10, 23, 18, 14, 24, 0, 0, 0, 1, 1, 8, 11, 7, 17, 25, 14, 19, 0, 0, 1, 1, 1, 12, 4, 5, 13, 21, 26, 27, 0, 1, 1, 1, 1, 6, 10, 28, 18, 14, 29, 0, 1, 1, 1, 1, 8, 11, 7, 20, 21, 2, 30 };
static const boxdef_t box_l10u16_def = {
    11, // w
    8, // h
    88, // total_size
    -5, // step_x
    -8, // step_y
    -10, // rel_x
    -7, // rel_y
    10, // grad1_color_start
    28, // char_count
    63, // char_offset
    box_l10u16_idx, // char_idx
    box_l10u16_chars // box_chars
};

static const uint8_t box_l16d1_idx[] = { 38, 46, 56, 37, 36, 30, 40, 5, 58, 57, 63, 62, 61, 60, 59, 35, 34, 64, 31, 45, 65, 0, 47, 32, 39 };
static const uint8_t box_l16d1_chars[] = { 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 2, 2, 2, 2, 2, 2, 2, 2, 2, 18, 18, 18, 18, 18, 19, 19, 19, 20, 20, 20, 20, 20, 21, 22, 22, 23, 23, 23, 23, 23, 24, 25, 25, 25, 25, 25, 26, 26, 26, 26, 27, 27, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 9, 9, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 1, 1, 1, 1, 1, 1 };
static const boxdef_t box_l16d1_def = {
    16, // w
    6, // h
    96, // total_size
    -16, // step_x
    1, // step_y
    -15, // rel_x
    0, // rel_y
    8, // grad1_color_start
    25, // char_count
    123, // char_offset
    box_l16d1_idx, // char_idx
    box_l16d1_chars // box_chars
};

static const uint8_t box_l16u1_idx[] = { 46, 38, 30, 36, 37, 56, 3, 5, 40, 59, 60, 61, 62, 63, 57, 58, 34, 35, 0, 33, 45, 31, 39, 32, 47 };
static const uint8_t box_l16u1_chars[] = { 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 0, 0, 19, 19, 19, 20, 20, 20, 20, 2, 2, 2, 2, 2, 2, 2, 2, 2, 21, 21, 21, 22, 22, 22, 23, 23, 23, 24, 24, 24, 24, 24, 19, 19, 3, 3, 3, 4, 4, 4, 25, 25, 25, 26, 26, 26, 26, 27, 27, 27, 5, 5, 6, 6, 6, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 1, 1, 1, 1, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11 };
static const boxdef_t box_l16u1_def = {
    16, // w
    6, // h
    96, // total_size
    -16, // step_x
    -1, // step_y
    -15, // rel_x
    -1, // rel_y
    9, // grad1_color_start
    25, // char_count
    123, // char_offset
    box_l16u1_idx, // char_idx
    box_l16u1_chars // box_chars
};

static const uint8_t box_l2d16_idx[] = { 41, 48, 11, 16, 7, 38, 0, 19, 40, 27, 49, 50, 36, 15, 46, 34, 47, 14 };
static const uint8_t box_l2d16_chars[] = { 0, 2, 11, 12, 3, 4, 13, 2, 11, 14, 3, 5, 13, 2, 15, 16, 6, 7, 17, 2, 18, 16, 8, 7, 17, 2, 18, 16, 8, 7, 19, 20, 12, 9, 4, 7, 19, 20, 12, 10, 4, 7, 2, 11, 12, 3, 4, 1 };
static const boxdef_t box_l2d16_def = {
    6, // w
    8, // h
    48, // total_size
    -1, // step_x
    8, // step_y
    -1, // rel_x
    0, // rel_y
    8, // grad1_color_start
    18, // char_count
    217, // char_offset
    box_l2d16_idx, // char_idx
    box_l2d16_chars // box_chars
};

static const uint8_t box_l2d8_idx[] = { 44, 0, 19, 10, 41, 22, 45, 43, 30, 46, 39, 18, 47, 37, 48, 17 };
static const uint8_t box_l2d8_chars[] = { 9, 2, 10, 11, 3, 4, 12, 2, 13, 14, 5, 6, 15, 2, 16, 14, 7, 6, 17, 18, 11, 8, 4, 6 };
static const boxdef_t box_l2d8_def = {
    6, // w
    4, // h
    24, // total_size
    -1, // step_x
    4, // step_y
    -1, // rel_x
    0, // rel_y
    6, // grad1_color_start
    16, // char_count
    214, // char_offset
    box_l2d8_idx, // char_idx
    box_l2d8_chars // box_chars
};

static const uint8_t box_l2u16_idx[] = { 38, 0, 49, 22, 55, 17, 53, 51, 18, 50, 40, 56, 48, 34, 57, 54, 52, 58 };
static const uint8_t box_l2u16_chars[] = { 3, 4, 10, 11, 2, 12, 5, 4, 13, 11, 14, 12, 5, 4, 6, 11, 14, 15, 1, 7, 6, 16, 14, 15, 1, 7, 8, 17, 14, 18, 1, 9, 8, 19, 14, 18, 1, 3, 8, 19, 20, 2, 1, 3, 4, 19, 11, 2 };
static const boxdef_t box_l2u16_def = {
    6, // w
    8, // h
    48, // total_size
    -1, // step_x
    -8, // step_y
    -6, // rel_x
    -7, // rel_y
    7, // grad1_color_start
    18, // char_count
    46, // char_offset
    box_l2u16_idx, // char_idx
    box_l2u16_chars // box_chars
};

static const uint8_t box_l2u8_idx[] = { 49, 0, 55, 22, 53, 17, 38, 40, 18, 56, 60, 57, 61, 52, 58, 62, 59 };
static const uint8_t box_l2u8_chars[] = { 3, 4, 10, 11, 12, 13, 0, 1, 5, 6, 14, 12, 15, 0, 1, 7, 8, 16, 17, 18, 0, 1, 9, 4, 16, 11, 2, 19 };
static const boxdef_t box_l2u8_def = {
    7, // w
    4, // h
    28, // total_size
    -1, // step_x
    -4, // step_y
    -6, // rel_x
    -3, // rel_y
    7, // grad1_color_start
    17, // char_count
    46, // char_offset
    box_l2u8_idx, // char_idx
    box_l2u8_chars // box_chars
};

static const uint8_t box_l4d8_idx[] = { 49, 27, 37, 31, 41, 61, 0, 57, 62, 1, 45 };
static const uint8_t box_l4d8_chars[] = { 0, 8, 2, 9, 10, 3, 4, 5, 0, 11, 2, 12, 13, 6, 7, 1, 8, 2, 9, 10, 3, 4, 5, 1, 11, 2, 12, 13, 6, 7, 1, 1 };
static const boxdef_t box_l4d8_def = {
    8, // w
    4, // h
    32, // total_size
    -2, // step_x
    4, // step_y
    -2, // rel_x
    0, // rel_y
    5, // grad1_color_start
    11, // char_count
    187, // char_offset
    box_l4d8_idx, // char_idx
    box_l4d8_chars // box_chars
};

static const uint8_t box_l4u8_idx[] = { 31, 45, 46, 35, 47, 53, 38, 56, 34, 0, 55 };
static const uint8_t box_l4u8_chars[] = { 3, 4, 5, 8, 9, 10, 0, 0, 1, 6, 7, 11, 12, 2, 13, 0, 1, 3, 4, 5, 8, 9, 10, 0, 1, 1, 6, 7, 11, 12, 2, 13 };
static const boxdef_t box_l4u8_def = {
    8, // w
    4, // h
    32, // total_size
    -2, // step_x
    -4, // step_y
    -7, // rel_x
    -3, // rel_y
    5, // grad1_color_start
    11, // char_count
    64, // char_offset
    box_l4u8_idx, // char_idx
    box_l4u8_chars // box_chars
};

static const uint8_t box_l6d16_idx[] = { 49, 27, 37, 46, 41, 30, 71, 31, 68, 69, 70, 57, 63, 64, 45, 59, 44, 65, 55, 52, 0, 66, 67 };
static const uint8_t box_l6d16_chars[] = { 0, 0, 12, 2, 13, 14, 3, 4, 5, 0, 0, 15, 2, 16, 17, 6, 7, 1, 0, 0, 18, 19, 14, 8, 4, 5, 1, 0, 20, 2, 13, 21, 9, 4, 1, 1, 0, 15, 2, 16, 17, 10, 5, 1, 1, 22, 2, 23, 14, 3, 4, 5, 1, 1, 20, 2, 24, 21, 6, 7, 1, 1, 1, 25, 19, 16, 8, 11, 5, 1, 1, 1 };
static const boxdef_t box_l6d16_def = {
    9, // w
    8, // h
    72, // total_size
    -3, // step_x
    8, // step_y
    -3, // rel_x
    0, // rel_y
    9, // grad1_color_start
    23, // char_count
    187, // char_offset
    box_l6d16_idx, // char_idx
    box_l6d16_chars // box_chars
};

static const uint8_t box_l6d8_idx[] = { 47, 48, 58, 63, 44, 46, 54, 66, 59, 17, 32, 60, 42, 61, 18, 62, 33, 64, 45, 0, 65, 57 };
static const uint8_t box_l6d8_chars[] = { 0, 0, 11, 2, 12, 13, 3, 4, 5, 0, 14, 15, 16, 17, 18, 6, 7, 1, 19, 20, 2, 21, 22, 8, 7, 9, 1, 23, 2, 24, 13, 10, 7, 9, 1, 1 };
static const boxdef_t box_l6d8_def = {
    9, // w
    4, // h
    36, // total_size
    -3, // step_x
    4, // step_y
    -3, // rel_x
    0, // rel_y
    8, // grad1_color_start
    22, // char_count
    170, // char_offset
    box_l6d8_idx, // char_idx
    box_l6d8_chars // box_chars
};

static const uint8_t box_l6u16_idx[] = { 32, 46, 47, 38, 0, 21, 36, 48, 50, 54, 39, 44, 40, 41, 55, 51, 1, 43, 35, 49, 52, 53 };
static const uint8_t box_l6u16_chars[] = { 3, 4, 5, 12, 13, 14, 0, 0, 0, 1, 6, 7, 15, 16, 17, 0, 0, 0, 1, 8, 4, 18, 19, 13, 20, 0, 0, 1, 3, 4, 5, 15, 13, 14, 0, 0, 1, 1, 9, 10, 21, 16, 2, 22, 0, 1, 1, 11, 4, 18, 19, 13, 20, 0, 1, 1, 1, 4, 7, 15, 13, 23, 0, 1, 1, 1, 9, 10, 21, 19, 2, 24 };
static const boxdef_t box_l6u16_def = {
    9, // w
    8, // h
    72, // total_size
    -3, // step_x
    -8, // step_y
    -8, // rel_x
    -7, // rel_y
    9, // grad1_color_start
    22, // char_count
    63, // char_offset
    box_l6u16_idx, // char_idx
    box_l6u16_chars // box_chars
};

static const uint8_t box_l6u8_idx[] = { 50, 63, 36, 65, 59, 48, 32, 46, 0, 47, 68, 41, 69, 70, 51, 54, 39, 71, 72, 60, 1, 73, 35, 67 };
static const uint8_t box_l6u8_chars[] = { 3, 4, 12, 13, 14, 15, 16, 0, 0, 1, 5, 4, 17, 18, 19, 20, 21, 0, 1, 6, 7, 8, 22, 23, 19, 24, 0, 1, 1, 9, 10, 11, 25, 14, 2, 26 };
static const boxdef_t box_l6u8_def = {
    9, // w
    4, // h
    36, // total_size
    -3, // step_x
    -4, // step_y
    -8, // rel_x
    -3, // rel_y
    9, // grad1_color_start
    24, // char_count
    63, // char_offset
    box_l6u8_idx, // char_idx
    box_l6u8_chars // box_chars
};

static const uint8_t box_l8_idx[] = { 25, 9, 0, 16 };
static const uint8_t box_l8_chars[] = { 2, 2, 2, 2, 5, 5, 5, 5, 6, 6, 6, 6, 3, 3, 3, 3, 4, 4, 4, 4 };
static const boxdef_t box_l8_def = {
    4, // w
    5, // h
    20, // total_size
    -4, // step_x
    0, // step_y
    -3, // rel_x
    0, // rel_y
    2, // grad1_color_start
    4, // char_count
    154, // char_offset
    box_l8_idx, // char_idx
    box_l8_chars // box_chars
};

static const uint8_t box_l8d1_idx[] = { 33, 41, 51, 32, 31, 25, 35, 0, 61, 62, 63, 64, 65, 66, 67, 68, 30, 29, 59, 26, 40, 60, 42, 69, 34 };
static const uint8_t box_l8d1_chars[] = { 0, 0, 0, 0, 0, 0, 0, 11, 12, 13, 14, 15, 16, 17, 18, 2, 2, 2, 2, 19, 19, 20, 20, 21, 21, 22, 23, 24, 24, 24, 25, 25, 25, 26, 27, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 9, 9, 10, 10, 10, 1, 1, 1, 1 };
static const boxdef_t box_l8d1_def = {
    8, // w
    7, // h
    56, // total_size
    -8, // step_x
    1, // step_y
    -7, // rel_x
    -1, // rel_y
    8, // grad1_color_start
    25, // char_count
    128, // char_offset
    box_l8d1_idx, // char_idx
    box_l8d1_chars // box_chars
};

static const uint8_t box_l8d2_idx[] = { 33, 51, 31, 35, 0, 70, 71, 72, 73, 30, 59, 40, 60, 74, 42, 69 };
static const uint8_t box_l8d2_chars[] = { 0, 0, 0, 8, 9, 10, 11, 2, 2, 12, 12, 13, 14, 15, 16, 17, 18, 3, 3, 4, 4, 5, 5, 6, 6, 7, 1, 1 };
static const boxdef_t box_l8d2_def = {
    4, // w
    7, // h
    28, // total_size
    -4, // step_x
    1, // step_y
    -3, // rel_x
    -1, // rel_y
    5, // grad1_color_start
    16, // char_count
    128, // char_offset
    box_l8d2_idx, // char_idx
    box_l8d2_chars // box_chars
};

static const uint8_t box_l8d3_idx[] = { 33, 41, 51, 85, 86, 35, 0, 77, 80, 81, 82, 75, 76, 83, 84, 30, 59, 78, 79, 40, 60, 42, 29, 74, 34, 69 };
static const uint8_t box_l8d3_chars[] = { 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0, 11, 12, 13, 2, 0, 14, 15, 16, 17, 2, 18, 19, 20, 21, 2, 18, 19, 22, 23, 24, 18, 25, 19, 23, 26, 24, 3, 4, 23, 26, 24, 27, 3, 5, 6, 7, 28, 3, 5, 6, 6, 8, 9, 1, 5, 6, 8, 9, 1, 1, 1, 1, 8, 1, 1, 1, 1, 1, 1, 1 };
static const boxdef_t box_l8d3_def = {
    8, // w
    9, // h
    72, // total_size
    -8, // step_x
    3, // step_y
    -7, // rel_x
    -1, // rel_y
    7, // grad1_color_start
    26, // char_count
    128, // char_offset
    box_l8d3_idx, // char_idx
    box_l8d3_chars // box_chars
};

static const uint8_t box_l8d4_idx[] = { 11, 2, 55, 5, 46, 52, 0, 29, 30, 44, 39 };
static const uint8_t box_l8d4_chars[] = { 0, 0, 0, 7, 0, 7, 8, 2, 8, 2, 9, 10, 9, 10, 11, 12, 11, 12, 13, 3, 13, 3, 4, 5, 4, 5, 6, 1, 6, 1, 1, 1 };
static const boxdef_t box_l8d4_def = {
    4, // w
    8, // h
    32, // total_size
    -4, // step_x
    2, // step_y
    -3, // rel_x
    -1, // rel_y
    4, // grad1_color_start
    11, // char_count
    158, // char_offset
    box_l8d4_idx, // char_idx
    box_l8d4_chars // box_chars
};

static const uint8_t box_l8d5_idx[] = { 89, 51, 85, 33, 90, 86, 0, 35, 88, 76, 80, 83, 81, 84, 30, 59, 77, 82, 87, 74, 75, 78, 60, 42, 79, 69 };
static const uint8_t box_l8d5_chars[] = { 0, 0, 0, 0, 0, 0, 0, 12, 0, 0, 0, 0, 0, 13, 14, 2, 0, 0, 0, 0, 15, 16, 17, 18, 0, 0, 19, 20, 2, 17, 21, 22, 23, 24, 2, 17, 18, 25, 26, 3, 27, 2, 18, 25, 22, 3, 4, 5, 17, 18, 22, 28, 6, 7, 8, 9, 25, 26, 3, 4, 5, 10, 1, 1, 28, 11, 5, 8, 1, 1, 1, 1, 4, 8, 9, 1, 1, 1, 1, 1, 10, 1, 1, 1, 1, 1, 1, 1 };
static const boxdef_t box_l8d5_def = {
    8, // w
    11, // h
    88, // total_size
    -8, // step_x
    5, // step_y
    -7, // rel_x
    -1, // rel_y
    9, // grad1_color_start
    26, // char_count
    128, // char_offset
    box_l8d5_idx, // char_idx
    box_l8d5_chars // box_chars
};

static const uint8_t box_l8d6_idx[] = { 89, 90, 88, 85, 35, 51, 86, 0, 94, 75, 91, 92, 93, 59, 84, 30, 74, 87, 42, 60, 69 };
static const uint8_t box_l8d6_chars[] = { 0, 0, 0, 11, 0, 12, 13, 2, 14, 15, 2, 16, 17, 18, 16, 19, 18, 20, 21, 3, 22, 21, 3, 4, 23, 5, 6, 7, 8, 9, 10, 1, 9, 1, 1, 1 };
static const boxdef_t box_l8d6_def = {
    4, // w
    9, // h
    36, // total_size
    -4, // step_x
    3, // step_y
    -3, // rel_x
    -1, // rel_y
    8, // grad1_color_start
    21, // char_count
    128, // char_offset
    box_l8d6_idx, // char_idx
    box_l8d6_chars // box_chars
};

static const uint8_t box_l8d8_idx[] = { 29, 27, 37, 36, 0, 1, 10 };
static const uint8_t box_l8d8_chars[] = { 0, 0, 0, 6, 0, 0, 6, 2, 0, 6, 2, 7, 6, 2, 7, 8, 2, 7, 8, 9, 7, 8, 9, 3, 8, 9, 3, 4, 9, 3, 4, 5, 3, 4, 5, 1, 4, 5, 1, 1, 5, 1, 1, 1 };
static const boxdef_t box_l8d8_def = {
    4, // w
    11, // h
    44, // total_size
    -4, // step_x
    4, // step_y
    -3, // rel_x
    -1, // rel_y
    3, // grad1_color_start
    7, // char_count
    187, // char_offset
    box_l8d8_idx, // char_idx
    box_l8d8_chars // box_chars
};

static const uint8_t box_l8d8_alt_idx[] = { 59, 60, 70, 67, 68, 0, 69, 44 };
static const uint8_t box_l8d8_alt_chars[] = { 0, 0, 0, 6, 0, 0, 6, 7, 0, 6, 7, 8, 6, 7, 8, 9, 7, 8, 9, 10, 8, 9, 10, 3, 9, 10, 3, 4, 10, 3, 4, 5, 3, 4, 5, 1, 4, 5, 1, 1, 5, 1, 1, 1 };
static const boxdef_t box_l8d8_alt_def = {
    4, // w
    11, // h
    44, // total_size
    -4, // step_x
    4, // step_y
    -3, // rel_x
    -1, // rel_y
    3, // grad1_color_start
    8, // char_count
    158, // char_offset
    box_l8d8_alt_idx, // char_idx
    box_l8d8_alt_chars // box_chars
};

static const uint8_t box_l8u1_idx[] = { 46, 38, 30, 36, 37, 56, 3, 5, 40, 53, 54, 55, 48, 49, 50, 51, 34, 35, 52, 0, 33, 45, 31, 39, 32, 47 };
static const uint8_t box_l8u1_chars[] = { 12, 13, 14, 15, 16, 17, 18, 0, 19, 20, 20, 2, 2, 2, 2, 21, 22, 23, 24, 24, 24, 25, 25, 19, 3, 4, 4, 26, 26, 27, 28, 22, 5, 6, 7, 7, 7, 8, 9, 9, 1, 1, 10, 10, 10, 11, 11, 11 };
static const boxdef_t box_l8u1_def = {
    8, // w
    6, // h
    48, // total_size
    -8, // step_x
    -1, // step_y
    -7, // rel_x
    -1, // rel_y
    9, // grad1_color_start
    26, // char_count
    123, // char_offset
    box_l8u1_idx, // char_idx
    box_l8u1_chars // box_chars
};

static const uint8_t box_l8u2_idx[] = { 39, 36, 37, 46, 5, 40, 30, 42, 43, 44, 35, 41, 45, 34, 39, 47, 0 };
static const uint8_t box_l8u2_chars[] = { 10, 11, 12, 0, 13, 2, 2, 14, 15, 15, 16, 16, 3, 17, 18, 19, 4, 5, 6, 6, 7, 7, 8, 9 };
static const boxdef_t box_l8u2_def = {
    4, // w
    6, // h
    24, // total_size
    -4, // step_x
    -1, // step_y
    -3, // rel_x
    -1, // rel_y
    7, // grad1_color_start
    17, // char_count
    123, // char_offset
    box_l8u2_idx, // char_idx
    box_l8u2_chars // box_chars
};

static const uint8_t box_l8u3_idx[] = { 15, 0, 52, 42, 49, 17, 2, 48, 50, 39, 38, 21, 37, 36, 24, 43, 46, 47, 35, 34, 41, 44, 12, 6, 40, 51, 45 };
static const uint8_t box_l8u3_chars[] = { 12, 13, 0, 0, 0, 0, 0, 0, 2, 14, 15, 16, 17, 0, 0, 0, 18, 19, 20, 2, 21, 22, 23, 0, 24, 25, 26, 18, 19, 2, 2, 27, 3, 4, 28, 24, 25, 26, 19, 20, 5, 6, 7, 3, 4, 28, 25, 29, 1, 1, 8, 9, 10, 7, 3, 11, 1, 1, 1, 1, 8, 8, 9, 7, 1, 1, 1, 1, 1, 1, 1, 8 };
static const boxdef_t box_l8u3_def = {
    8, // w
    9, // h
    72, // total_size
    -8, // step_x
    -3, // step_y
    -7, // rel_x
    -3, // rel_y
    9, // grad1_color_start
    27, // char_count
    111, // char_offset
    box_l8u3_idx, // char_idx
    box_l8u3_chars // box_chars
};

static const uint8_t box_l8u4_idx[] = { 9, 51, 7, 26, 45, 49, 2, 0, 21, 15, 8 };
static const uint8_t box_l8u4_chars[] = { 7, 0, 0, 0, 2, 8, 7, 0, 9, 10, 2, 8, 11, 12, 9, 10, 3, 13, 11, 12, 4, 5, 3, 13, 6, 6, 4, 5, 1, 1, 6, 6 };
static const boxdef_t box_l8u4_def = {
    4, // w
    8, // h
    32, // total_size
    -4, // step_x
    -2, // step_y
    -3, // rel_x
    -2, // rel_y
    4, // grad1_color_start
    11, // char_count
    102, // char_offset
    box_l8u4_idx, // char_idx
    box_l8u4_chars // box_chars
};

static const uint8_t box_l8u5_idx[] = { 7, 9, 26, 20, 24, 42, 11, 47, 30, 48, 2, 49, 50, 29, 0, 43, 33, 8, 21, 15, 44, 45, 46, 12 };
static const uint8_t box_l8u5_chars[] = { 10, 0, 0, 0, 0, 0, 0, 0, 11, 12, 0, 0, 0, 0, 0, 0, 13, 2, 14, 15, 0, 0, 0, 0, 16, 13, 17, 2, 18, 19, 0, 0, 20, 21, 22, 13, 2, 23, 24, 0, 3, 4, 20, 21, 22, 17, 2, 25, 5, 6, 7, 8, 21, 16, 13, 17, 1, 5, 9, 3, 4, 20, 21, 22, 1, 1, 1, 5, 9, 3, 4, 26, 1, 1, 1, 1, 1, 9, 6, 7, 1, 1, 1, 1, 1, 1, 5, 9 };
static const boxdef_t box_l8u5_def = {
    8, // w
    11, // h
    88, // total_size
    -8, // step_x
    -5, // step_y
    -7, // rel_x
    -5, // rel_y
    7, // grad1_color_start
    24, // char_count
    102, // char_offset
    box_l8u5_idx, // char_idx
    box_l8u5_chars // box_chars
};

static const uint8_t box_l8u6_idx[] = { 24, 11, 7, 42, 9, 26, 20, 33, 39, 40, 0, 30, 41, 15, 2, 38, 12, 29, 8, 21 };
static const uint8_t box_l8u6_chars[] = { 10, 0, 0, 0, 11, 12, 0, 0, 13, 14, 15, 0, 16, 17, 2, 18, 19, 20, 17, 13, 3, 21, 22, 16, 4, 5, 6, 22, 1, 4, 5, 7, 1, 1, 8, 9, 1, 1, 1, 8 };
static const boxdef_t box_l8u6_def = {
    4, // w
    10, // h
    40, // total_size
    -4, // step_x
    -3, // step_y
    -3, // rel_x
    -3, // rel_y
    7, // grad1_color_start
    20, // char_count
    102, // char_offset
    box_l8u6_idx, // char_idx
    box_l8u6_chars // box_chars
};

static const uint8_t box_l8u8_idx[] = { 22, 18, 24, 33, 0, 27, 6 };
static const uint8_t box_l8u8_chars[] = { 6, 0, 0, 0, 2, 6, 0, 0, 7, 2, 6, 0, 8, 7, 2, 6, 9, 8, 7, 2, 3, 9, 8, 7, 4, 3, 9, 8, 5, 4, 3, 9, 1, 5, 4, 3, 1, 1, 5, 4, 1, 1, 1, 5 };
static const boxdef_t box_l8u8_def = {
    4, // w
    11, // h
    44, // total_size
    -4, // step_x
    -4, // step_y
    -3, // rel_x
    -3, // rel_y
    3, // grad1_color_start
    7, // char_count
    104, // char_offset
    box_l8u8_idx, // char_idx
    box_l8u8_chars // box_chars
};

static const uint8_t box_l8u8_alt_idx[] = { 9, 7, 11, 37, 36, 0, 15, 21 };
static const uint8_t box_l8u8_alt_chars[] = { 6, 0, 0, 0, 7, 6, 0, 0, 8, 7, 6, 0, 9, 8, 7, 6, 10, 9, 8, 7, 3, 10, 9, 8, 4, 3, 10, 9, 5, 4, 3, 10, 1, 5, 4, 3, 1, 1, 5, 4, 1, 1, 1, 5 };
static const boxdef_t box_l8u8_alt_def = {
    4, // w
    11, // h
    44, // total_size
    -4, // step_x
    -4, // step_y
    -3, // rel_x
    -3, // rel_y
    3, // grad1_color_start
    8, // char_count
    102, // char_offset
    box_l8u8_alt_idx, // char_idx
    box_l8u8_alt_chars // box_chars
};

static const uint8_t box_l8_alt_idx[] = { 33, 31, 0, 57, 30, 28 };
static const uint8_t box_l8_alt_chars[] = { 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5 };
static const boxdef_t box_l8_alt_def = {
    4, // w
    6, // h
    24, // total_size
    -4, // step_x
    0, // step_y
    -3, // rel_x
    0, // rel_y
    3, // grad1_color_start
    6, // char_count
    128, // char_offset
    box_l8_alt_idx, // char_idx
    box_l8_alt_chars // box_chars
};

static const uint8_t box_r10d16_idx[] = { 58, 47, 56, 37, 39, 33, 111, 45, 115, 40, 52, 36, 35, 51, 60, 38, 12, 41, 57, 61, 62, 115, 54, 55, 59, 48, 19, 0, 53 };
static const uint8_t box_r10d16_chars[] = { 13, 2, 14, 15, 3, 4, 1, 1, 1, 1, 1, 16, 17, 18, 19, 20, 5, 6, 1, 1, 1, 1, 0, 21, 2, 22, 19, 7, 8, 9, 1, 1, 1, 0, 0, 23, 2, 14, 24, 10, 4, 1, 1, 1, 0, 0, 25, 2, 26, 19, 11, 5, 9, 1, 1, 0, 0, 0, 21, 2, 14, 19, 3, 8, 1, 1, 0, 0, 0, 27, 28, 18, 29, 30, 10, 4, 1, 0, 0, 0, 0, 31, 2, 26, 19, 12, 5, 9 };
static const boxdef_t box_r10d16_def = {
    11, // w
    8, // h
    88, // total_size
    5, // step_x
    8, // step_y
    0, // rel_x
    0, // rel_y
    10, // grad1_color_start
    29, // char_count
    232, // char_offset
    box_r10d16_idx, // char_idx
    box_r10d16_chars // box_chars
};

static const uint8_t box_r10u16_idx[] = { 45, 40, 62, 52, 57, 54, 41, 1, 0, 42, 63, 47, 72, 73, 66, 58, 67, 43, 74, 75, 44, 65, 2, 68, 69, 46, 70, 71 };
static const uint8_t box_r10u16_chars[] = { 1, 1, 1, 1, 3, 4, 5, 13, 14, 15, 16, 1, 1, 1, 1, 6, 7, 17, 18, 2, 19, 0, 1, 1, 1, 8, 4, 20, 21, 14, 22, 0, 0, 1, 1, 3, 4, 5, 13, 23, 2, 24, 0, 0, 1, 1, 6, 9, 17, 18, 2, 19, 0, 0, 0, 1, 8, 4, 10, 25, 14, 26, 27, 0, 0, 0, 1, 11, 7, 28, 23, 2, 29, 0, 0, 0, 0, 12, 9, 20, 18, 14, 30, 0, 0, 0, 0, 0 };
static const boxdef_t box_r10u16_def = {
    11, // w
    8, // h
    88, // total_size
    5, // step_x
    -8, // step_y
    -6, // rel_x
    -7, // rel_y
    10, // grad1_color_start
    28, // char_count
    6, // char_offset
    box_r10u16_idx, // char_idx
    box_r10u16_chars // box_chars
};

static const uint8_t box_r16d1_idx[] = { 89, 80, 88, 61, 81, 90, 33, 84, 85, 91, 93, 82, 92, 54, 83, 94, 86, 131, 95, 101, 100, 99, 87, 98, 97, 96 };
static const uint8_t box_r16d1_chars[] = { 3, 4, 4, 4, 4, 5, 5, 5, 5, 5, 1, 1, 1, 1, 1, 1, 6, 7, 7, 7, 7, 8, 8, 9, 9, 10, 10, 10, 10, 3, 3, 3, 13, 14, 14, 14, 15, 15, 15, 15, 15, 11, 11, 11, 11, 12, 12, 6, 16, 17, 17, 17, 18, 18, 18, 18, 18, 19, 19, 19, 13, 13, 13, 13, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 20, 21, 21, 21, 16, 0, 22, 22, 23, 23, 24, 24, 25, 25, 26, 26, 27, 27, 28, 28, 2 };
static const boxdef_t box_r16d1_def = {
    16, // w
    6, // h
    96, // total_size
    16, // step_x
    1, // step_y
    0, // rel_x
    -5, // rel_y
    10, // grad1_color_start
    26, // char_count
    255, // char_offset
    box_r16d1_idx, // char_idx
    box_r16d1_chars // box_chars
};

static const uint8_t box_r16u1_idx[] = { 8, 0, 9, 4, 10, 1, 11, 5, 5, 12, 2, 13, 6, 14, 3, 15, 16, 17, 18, 7, 19, 20, 21 };
static const uint8_t box_r16u1_chars[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 11, 12, 12, 13, 13, 13, 13, 13, 14, 14, 14, 15, 15, 15, 15, 15, 15, 16, 16, 16, 17, 17, 17, 17, 17, 17, 18, 18, 18, 18, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 24, 25, 25, 0 };
static const boxdef_t box_r16u1_def = {
    16, // w
    6, // h
    96, // total_size
    16, // step_x
    -1, // step_y
    0, // rel_x
    -6, // rel_y
    8, // grad1_color_start
    23, // char_count
    2, // char_offset
    box_r16u1_idx, // char_idx
    box_r16u1_chars // box_chars
};

static const uint8_t box_r2d16_idx[] = { 57, 0, 58, 10, 59, 51, 41, 19, 44, 55, 56, 30, 50, 37, 49, 18, 52, 54, 43, 53 };
static const uint8_t box_r2d16_chars[] = { 2, 13, 14, 3, 4, 1, 15, 13, 14, 5, 4, 6, 15, 2, 16, 7, 8, 6, 17, 2, 16, 18, 9, 6, 17, 2, 16, 18, 9, 6, 19, 2, 20, 18, 10, 6, 19, 2, 21, 22, 11, 12, 0, 2, 21, 22, 11, 4 };
static const boxdef_t box_r2d16_def = {
    6, // w
    8, // h
    48, // total_size
    1, // step_x
    8, // step_y
    0, // rel_x
    0, // rel_y
    10, // grad1_color_start
    20, // char_count
    214, // char_offset
    box_r2d16_idx, // char_idx
    box_r2d16_chars // box_chars
};

static const uint8_t box_r2d8_idx[] = { 40, 33, 111, 45, 37, 39, 47, 42, 38, 12, 43, 19, 41, 44, 36, 0, 46, 25 };
static const uint8_t box_r2d8_chars[] = { 10, 11, 12, 3, 4, 1, 13, 2, 14, 15, 4, 5, 16, 2, 17, 18, 6, 7, 19, 2, 20, 12, 8, 9 };
static const boxdef_t box_r2d8_def = {
    6, // w
    4, // h
    24, // total_size
    1, // step_x
    4, // step_y
    0, // rel_x
    0, // rel_y
    7, // grad1_color_start
    18, // char_count
    232, // char_offset
    box_r2d8_idx, // char_idx
    box_r2d8_chars // box_chars
};

static const uint8_t box_r2u16_idx[] = { 53, 17, 55, 22, 0, 49, 38, 52, 7, 54, 34, 48, 18, 40, 50, 51, 26, 4 };
static const uint8_t box_r2u16_chars[] = { 1, 3, 4, 10, 11, 12, 1, 5, 4, 13, 11, 12, 1, 5, 6, 13, 11, 14, 1, 7, 6, 15, 2, 14, 8, 7, 16, 15, 2, 17, 8, 7, 18, 15, 2, 17, 9, 7, 19, 15, 2, 0, 9, 4, 10, 20, 2, 0 };
static const boxdef_t box_r2u16_def = {
    6, // w
    8, // h
    48, // total_size
    1, // step_x
    -8, // step_y
    -5, // rel_x
    -7, // rel_y
    7, // grad1_color_start
    18, // char_count
    46, // char_offset
    box_r2u16_idx, // char_idx
    box_r2u16_chars // box_chars
};

static const uint8_t box_r2u8_idx[] = { 0, 22, 5, 38, 1, 12, 17, 34, 7, 45, 40, 18, 46, 26, 47, 23, 44 };
static const uint8_t box_r2u8_chars[] = { 1, 3, 4, 10, 11, 12, 5, 3, 13, 14, 2, 15, 6, 7, 16, 14, 2, 17, 8, 9, 18, 11, 19, 0 };
static const boxdef_t box_r2u8_def = {
    6, // w
    4, // h
    24, // total_size
    1, // step_x
    -4, // step_y
    -5, // rel_x
    -3, // rel_y
    7, // grad1_color_start
    17, // char_count
    46, // char_offset
    box_r2u8_idx, // char_idx
    box_r2u8_chars // box_chars
};

static const uint8_t box_r4d8_idx[] = { 45, 37, 39, 33, 52, 19, 0, 53, 25, 12 };
static const uint8_t box_r4d8_chars[] = { 7, 2, 8, 9, 3, 4, 1, 10, 2, 11, 12, 5, 6, 1, 0, 7, 2, 8, 9, 3, 4, 0, 10, 2, 11, 12, 5, 6 };
static const boxdef_t box_r4d8_def = {
    7, // w
    4, // h
    28, // total_size
    2, // step_x
    4, // step_y
    0, // rel_x
    0, // rel_y
    4, // grad1_color_start
    10, // char_count
    232, // char_offset
    box_r4d8_idx, // char_idx
    box_r4d8_chars // box_chars
};

static const uint8_t box_r4u8_idx[] = { 51, 56, 44, 39, 0, 65, 43, 76, 73, 46, 75 };
static const uint8_t box_r4u8_chars[] = { 1, 1, 3, 4, 8, 9, 2, 10, 1, 5, 6, 7, 11, 12, 13, 0, 1, 3, 4, 8, 9, 2, 10, 0, 5, 6, 7, 11, 12, 13, 0, 0 };
static const boxdef_t box_r4u8_def = {
    8, // w
    4, // h
    32, // total_size
    2, // step_x
    -4, // step_y
    -6, // rel_x
    -3, // rel_y
    5, // grad1_color_start
    11, // char_count
    7, // char_offset
    box_r4u8_idx, // char_idx
    box_r4u8_chars // box_chars
};

static const uint8_t box_r6d16_idx[] = { 41, 119, 53, 55, 48, 0, 45, 47, 49, 58, 46, 20, 49, 52, 44, 43, 59, 33, 51, 27, 8, 56, 57 };
static const uint8_t box_r6d16_chars[] = { 12, 13, 14, 15, 3, 4, 1, 1, 16, 2, 17, 18, 5, 6, 1, 1, 19, 2, 20, 14, 7, 3, 8, 1, 0, 21, 2, 22, 23, 5, 9, 1, 0, 16, 2, 20, 14, 10, 6, 1, 0, 0, 24, 13, 14, 11, 3, 4, 0, 0, 21, 2, 17, 23, 5, 9, 0, 0, 25, 2, 20, 14, 10, 3 };
static const boxdef_t box_r6d16_def = {
    8, // w
    8, // h
    64, // total_size
    3, // step_x
    8, // step_y
    0, // rel_x
    0, // rel_y
    9, // grad1_color_start
    23, // char_count
    224, // char_offset
    box_r6d16_idx, // char_idx
    box_r6d16_chars // box_chars
};

static const uint8_t box_r6d8_idx[] = { 27, 21, 99, 46, 101, 35, 103, 44, 25, 51, 49, 0, 52, 53, 105, 23, 54, 55, 26, 56, 57, 43 };
static const uint8_t box_r6d8_chars[] = { 12, 2, 13, 14, 3, 4, 5, 1, 1, 15, 16, 2, 17, 18, 6, 4, 1, 1, 0, 19, 20, 21, 22, 18, 7, 8, 1, 0, 0, 23, 2, 24, 14, 9, 10, 11 };
static const boxdef_t box_r6d8_def = {
    9, // w
    4, // h
    36, // total_size
    3, // step_x
    4, // step_y
    0, // rel_x
    0, // rel_y
    9, // grad1_color_start
    22, // char_count
    244, // char_offset
    box_r6d8_idx, // char_idx
    box_r6d8_chars // box_chars
};

static const uint8_t box_r6u16_idx[] = { 78, 40, 52, 57, 45, 62, 41, 0, 42, 80, 58, 79, 63, 44, 73, 74, 47, 81, 66, 68, 82, 83 };
static const uint8_t box_r6u16_chars[] = { 1, 1, 3, 4, 12, 13, 2, 14, 1, 1, 5, 6, 15, 16, 2, 17, 1, 7, 4, 8, 18, 19, 20, 0, 1, 3, 9, 21, 13, 2, 14, 0, 1, 10, 6, 15, 19, 22, 0, 0, 7, 4, 12, 18, 2, 20, 0, 0, 11, 6, 21, 16, 2, 23, 0, 0, 4, 8, 15, 19, 24, 0, 0, 0 };
static const boxdef_t box_r6u16_def = {
    8, // w
    8, // h
    64, // total_size
    3, // step_x
    -8, // step_y
    -5, // rel_x
    -7, // rel_y
    9, // grad1_color_start
    22, // char_count
    6, // char_offset
    box_r6u16_idx, // char_idx
    box_r6u16_chars // box_chars
};

static const uint8_t box_r6u8_idx[] = { 52, 41, 42, 40, 1, 45, 62, 0, 57, 43, 58, 47, 60, 39, 2, 30, 61, 63, 44, 64, 46, 59 };
static const uint8_t box_r6u8_chars[] = { 1, 1, 3, 4, 12, 13, 14, 15, 16, 1, 5, 6, 7, 17, 14, 18, 19, 0, 8, 6, 9, 20, 21, 2, 22, 0, 0, 10, 11, 23, 13, 2, 24, 0, 0, 0 };
static const boxdef_t box_r6u8_def = {
    9, // w
    4, // h
    36, // total_size
    3, // step_x
    -4, // step_y
    -6, // rel_x
    -3, // rel_y
    9, // grad1_color_start
    22, // char_count
    6, // char_offset
    box_r6u8_idx, // char_idx
    box_r6u8_chars // box_chars
};

static const uint8_t box_r8_idx[] = { 0, 1, 2, 3 };
static const uint8_t box_r8_chars[] = { 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 2, 2, 2, 2 };
static const boxdef_t box_r8_def = {
    4, // w
    5, // h
    20, // total_size
    4, // step_x
    0, // step_y
    0, // rel_x
    -5, // rel_y
    2, // grad1_color_start
    4, // char_count
    2, // char_offset
    box_r8_idx, // char_idx
    box_r8_chars // box_chars
};

static const uint8_t box_r8d1_idx[] = { 80, 88, 81, 90, 33, 84, 89, 85, 91, 61, 82, 92, 83, 94, 86, 93, 131, 95, 54, 74, 75, 76, 77, 70, 71, 72, 73 };
static const uint8_t box_r8d1_chars[] = { 3, 4, 4, 4, 4, 1, 1, 1, 5, 5, 6, 7, 8, 8, 9, 9, 13, 13, 14, 14, 14, 10, 11, 12, 15, 15, 16, 16, 17, 17, 18, 18, 2, 2, 2, 2, 2, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29 };
static const boxdef_t box_r8d1_def = {
    8, // w
    6, // h
    48, // total_size
    8, // step_x
    1, // step_y
    0, // rel_x
    -5, // rel_y
    10, // grad1_color_start
    27, // char_count
    255, // char_offset
    box_r8d1_idx, // char_idx
    box_r8d1_chars // box_chars
};

static const uint8_t box_r8d2_idx[] = { 80, 88, 61, 90, 33, 89, 91, 82, 92, 54, 94, 86, 93, 95, 66, 67, 68, 69 };
static const uint8_t box_r8d2_chars[] = { 3, 4, 4, 1, 5, 6, 7, 8, 10, 11, 11, 9, 12, 13, 14, 15, 2, 2, 2, 16, 17, 18, 19, 20 };
static const boxdef_t box_r8d2_def = {
    4, // w
    6, // h
    24, // total_size
    4, // step_x
    1, // step_y
    0, // rel_x
    -5, // rel_y
    7, // grad1_color_start
    18, // char_count
    255, // char_offset
    box_r8d2_idx, // char_idx
    box_r8d2_chars // box_chars
};

static const uint8_t box_r8d3_idx[] = { 89, 88, 91, 90, 61, 80, 33, 50, 93, 92, 54, 94, 65, 95, 38, 63, 62, 60, 86, 41, 59, 58, 44, 57, 56, 64 };
static const uint8_t box_r8d3_chars[] = { 3, 4, 4, 1, 1, 1, 1, 1, 5, 6, 3, 3, 4, 1, 1, 1, 11, 12, 5, 7, 6, 3, 8, 4, 13, 14, 11, 15, 12, 5, 6, 9, 2, 2, 16, 17, 14, 11, 12, 10, 18, 19, 20, 2, 2, 13, 14, 21, 0, 0, 22, 23, 24, 25, 2, 16, 0, 0, 0, 0, 0, 26, 27, 28 };
static const boxdef_t box_r8d3_def = {
    8, // w
    8, // h
    64, // total_size
    8, // step_x
    3, // step_y
    0, // rel_x
    -5, // rel_y
    8, // grad1_color_start
    26, // char_count
    255, // char_offset
    box_r8d3_idx, // char_idx
    box_r8d3_chars // box_chars
};

static const uint8_t box_r8d4_idx[] = { 89, 88, 91, 90, 93, 92, 54, 94, 59, 64 };
static const uint8_t box_r8d4_chars[] = { 3, 4, 1, 1, 5, 6, 3, 4, 7, 8, 5, 6, 9, 10, 7, 8, 2, 2, 9, 10, 11, 12, 2, 2, 0, 0, 11, 12 };
static const boxdef_t box_r8d4_def = {
    4, // w
    7, // h
    28, // total_size
    4, // step_x
    2, // step_y
    0, // rel_x
    -5, // rel_y
    4, // grad1_color_start
    10, // char_count
    255, // char_offset
    box_r8d4_idx, // char_idx
    box_r8d4_chars // box_chars
};

static const uint8_t box_r8d5_idx[] = { 99, 44, 35, 102, 101, 100, 61, 21, 72, 104, 103, 65, 97, 0, 106, 49, 70, 71, 105, 52, 73, 56, 74, 75, 67, 55, 68, 69 };
static const uint8_t box_r8d5_chars[] = { 3, 1, 1, 1, 1, 1, 1, 1, 4, 5, 1, 1, 1, 1, 1, 1, 6, 7, 8, 3, 1, 1, 1, 1, 12, 13, 6, 4, 8, 3, 1, 1, 14, 15, 16, 9, 7, 10, 3, 1, 2, 17, 18, 12, 13, 6, 7, 8, 19, 20, 2, 17, 21, 12, 13, 11, 0, 22, 23, 2, 2, 18, 24, 13, 0, 0, 0, 25, 26, 2, 17, 21, 0, 0, 0, 0, 0, 27, 28, 2, 0, 0, 0, 0, 0, 0, 29, 30 };
static const boxdef_t box_r8d5_def = {
    8, // w
    11, // h
    88, // total_size
    8, // step_x
    5, // step_y
    0, // rel_x
    -6, // rel_y
    9, // grad1_color_start
    28, // char_count
    244, // char_offset
    box_r8d5_idx, // char_idx
    box_r8d5_chars // box_chars
};

static const uint8_t box_r8d6_idx[] = { 99, 44, 100, 61, 101, 102, 21, 104, 103, 65, 97, 106, 105, 0, 62, 55, 49, 63, 64, 52, 66 };
static const uint8_t box_r8d6_chars[] = { 3, 1, 1, 1, 4, 5, 1, 1, 6, 7, 5, 3, 10, 11, 8, 9, 12, 13, 11, 8, 2, 14, 15, 16, 17, 18, 2, 19, 0, 20, 21, 2, 0, 0, 22, 23 };
static const boxdef_t box_r8d6_def = {
    4, // w
    9, // h
    36, // total_size
    4, // step_x
    3, // step_y
    0, // rel_x
    -6, // rel_y
    7, // grad1_color_start
    21, // char_count
    244, // char_offset
    box_r8d6_idx, // char_idx
    box_r8d6_chars // box_chars
};

static const uint8_t box_r8d8_idx[] = { 88, 10, 35, 92, 45, 95, 47 };
static const uint8_t box_r8d8_chars[] = { 3, 1, 1, 1, 4, 3, 1, 1, 5, 4, 3, 1, 6, 5, 4, 3, 7, 6, 5, 4, 8, 7, 6, 5, 2, 8, 7, 6, 9, 2, 8, 7, 0, 9, 2, 8, 0, 0, 9, 2, 0, 0, 0, 9 };
static const boxdef_t box_r8d8_def = {
    4, // w
    11, // h
    44, // total_size
    4, // step_x
    4, // step_y
    0, // rel_x
    -7, // rel_y
    3, // grad1_color_start
    7, // char_count
    255, // char_offset
    box_r8d8_idx, // char_idx
    box_r8d8_chars // box_chars
};

static const uint8_t box_r8d8_alt_idx[] = { 35, 44, 61, 0, 49, 60, 59 };
static const uint8_t box_r8d8_alt_chars[] = { 3, 1, 1, 1, 4, 3, 1, 1, 5, 4, 3, 1, 6, 5, 4, 3, 7, 6, 5, 4, 2, 7, 6, 5, 8, 2, 7, 6, 9, 8, 2, 7, 0, 9, 8, 2, 0, 0, 9, 8, 0, 0, 0, 9 };
static const boxdef_t box_r8d8_alt_def = {
    4, // w
    11, // h
    44, // total_size
    4, // step_x
    4, // step_y
    0, // rel_x
    -6, // rel_y
    3, // grad1_color_start
    7, // char_count
    244, // char_offset
    box_r8d8_alt_idx, // char_idx
    box_r8d8_alt_chars // box_chars
};

static const uint8_t box_r8u1_idx[] = { 8, 0, 9, 4, 10, 1, 11, 5, 12, 2, 13, 6, 14, 3, 15, 22, 23, 24, 25, 26, 27, 28, 29 };
static const uint8_t box_r8u1_chars[] = { 1, 1, 1, 1, 3, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 8, 9, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 16, 17, 17, 2, 2, 2, 2, 2, 18, 19, 20, 21, 22, 23, 24, 25 };
static const boxdef_t box_r8u1_def = {
    8, // w
    6, // h
    48, // total_size
    8, // step_x
    -1, // step_y
    0, // rel_x
    -6, // rel_y
    8, // grad1_color_start
    23, // char_count
    2, // char_offset
    box_r8u1_idx, // char_idx
    box_r8u1_chars // box_chars
};

static const uint8_t box_r8u2_idx[] = { 0, 4, 1, 5, 12, 2, 13, 6, 14, 3, 15, 30, 31, 32, 33 };
static const uint8_t box_r8u2_chars[] = { 1, 1, 3, 3, 4, 4, 5, 5, 6, 6, 7, 8, 9, 10, 11, 12, 13, 13, 2, 2, 14, 15, 16, 17 };
static const boxdef_t box_r8u2_def = {
    4, // w
    6, // h
    24, // total_size
    4, // step_x
    -1, // step_y
    0, // rel_x
    -6, // rel_y
    4, // grad1_color_start
    15, // char_count
    2, // char_offset
    box_r8u2_idx, // char_idx
    box_r8u2_chars // box_chars
};

static const uint8_t box_r8u3_idx[] = { 0, 9, 8, 4, 10, 1, 11, 5, 12, 2, 13, 14, 3, 6, 15, 39, 40, 41, 34, 35, 42, 43, 36, 37, 38 };
static const uint8_t box_r8u3_chars[] = { 1, 1, 1, 1, 1, 1, 3, 4, 1, 1, 1, 5, 3, 6, 7, 8, 1, 3, 6, 7, 8, 9, 11, 12, 6, 8, 9, 10, 12, 13, 14, 15, 10, 11, 13, 16, 15, 17, 2, 2, 16, 14, 15, 17, 2, 18, 19, 20, 17, 2, 21, 22, 23, 24, 0, 0, 25, 26, 27, 0, 0, 0, 0, 0 };
static const boxdef_t box_r8u3_def = {
    8, // w
    8, // h
    64, // total_size
    8, // step_x
    -3, // step_y
    0, // rel_x
    -8, // rel_y
    8, // grad1_color_start
    25, // char_count
    2, // char_offset
    box_r8u3_idx, // char_idx
    box_r8u3_chars // box_chars
};

static const uint8_t box_r8u4_idx[] = { 0, 4, 44, 45, 5, 2, 6, 3, 15, 36, 42 };
static const uint8_t box_r8u4_chars[] = { 1, 1, 3, 4, 3, 4, 5, 6, 5, 6, 7, 8, 7, 8, 9, 10, 9, 10, 11, 2, 11, 2, 12, 13, 12, 13, 0, 0 };
static const boxdef_t box_r8u4_def = {
    4, // w
    7, // h
    28, // total_size
    4, // step_x
    -2, // step_y
    0, // rel_x
    -7, // rel_y
    5, // grad1_color_start
    11, // char_count
    2, // char_offset
    box_r8u4_idx, // char_idx
    box_r8u4_chars // box_chars
};

static const uint8_t box_r8u5_idx[] = { 0, 4, 49, 1, 5, 45, 46, 44, 50, 2, 6, 3, 47, 51, 48, 39, 42, 40, 43, 14, 36, 41, 34, 37, 35, 38 };
static const uint8_t box_r8u5_chars[] = { 1, 1, 1, 1, 1, 1, 3, 4, 1, 1, 1, 1, 5, 4, 6, 7, 1, 1, 1, 3, 4, 8, 7, 11, 1, 3, 4, 6, 7, 12, 13, 14, 9, 10, 8, 15, 13, 14, 16, 2, 6, 7, 12, 13, 17, 2, 18, 19, 12, 13, 14, 16, 2, 20, 21, 0, 22, 16, 2, 23, 24, 0, 0, 0, 16, 25, 26, 0, 0, 0, 0, 0, 27, 28, 0, 0, 0, 0, 0, 0 };
static const boxdef_t box_r8u5_def = {
    8, // w
    10, // h
    80, // total_size
    8, // step_x
    -5, // step_y
    0, // rel_x
    -10, // rel_y
    8, // grad1_color_start
    26, // char_count
    2, // char_offset
    box_r8u5_idx, // char_idx
    box_r8u5_chars // box_chars
};

static const uint8_t box_r8u6_idx[] = { 0, 46, 44, 49, 4, 1, 5, 45, 13, 2, 6, 48, 14, 51, 3, 34, 52, 53, 54, 55, 43 };
static const uint8_t box_r8u6_chars[] = { 1, 1, 1, 3, 1, 1, 4, 5, 6, 7, 8, 9, 7, 10, 9, 11, 10, 12, 13, 14, 12, 15, 16, 2, 17, 16, 18, 19, 2, 20, 21, 0, 22, 23, 0, 0 };
static const boxdef_t box_r8u6_def = {
    4, // w
    9, // h
    36, // total_size
    4, // step_x
    -3, // step_y
    0, // rel_x
    -9, // rel_y
    8, // grad1_color_start
    21, // char_count
    2, // char_offset
    box_r8u6_idx, // char_idx
    box_r8u6_chars // box_chars
};

static const uint8_t box_r8u8_idx[] = { 50, 39, 41, 0, 42, 51 };
static const uint8_t box_r8u8_chars[] = { 1, 1, 1, 3, 1, 1, 3, 4, 1, 3, 4, 5, 3, 4, 5, 6, 4, 5, 6, 7, 5, 6, 7, 2, 6, 7, 2, 8, 7, 2, 8, 0, 2, 8, 0, 0, 8, 0, 0, 0 };
static const boxdef_t box_r8u8_def = {
    4, // w
    10, // h
    40, // total_size
    4, // step_x
    -4, // step_y
    0, // rel_x
    -10, // rel_y
    2, // grad1_color_start
    6, // char_count
    8, // char_offset
    box_r8u8_idx, // char_idx
    box_r8u8_chars // box_chars
};

static const uint8_t box_r8u8_alt_idx[] = { 44, 30, 31, 36, 0, 45, 46 };
static const uint8_t box_r8u8_alt_chars[] = { 1, 1, 1, 3, 1, 1, 3, 4, 1, 3, 4, 5, 3, 4, 5, 6, 4, 5, 6, 7, 5, 6, 7, 2, 6, 7, 2, 8, 7, 2, 8, 9, 2, 8, 9, 0, 8, 9, 0, 0, 9, 0, 0, 0 };
static const boxdef_t box_r8u8_alt_def = {
    4, // w
    11, // h
    44, // total_size
    4, // step_x
    -4, // step_y
    0, // rel_x
    -10, // rel_y
    3, // grad1_color_start
    7, // char_count
    16, // char_offset
    box_r8u8_alt_idx, // char_idx
    box_r8u8_alt_chars // box_chars
};

static const uint8_t box_r8_alt_idx[] = { 0, 1, 2, 3 };
static const uint8_t box_r8_alt_chars[] = { 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 2, 2, 2, 2, 6, 6, 6, 6 };
static const boxdef_t box_r8_alt_def = {
    4, // w
    5, // h
    20, // total_size
    4, // step_x
    0, // step_y
    0, // rel_x
    -4, // rel_y
    2, // grad1_color_start
    4, // char_count
    6, // char_offset
    box_r8_alt_idx, // char_idx
    box_r8_alt_chars // box_chars
};

static const uint8_t box_u8_idx[] = { 38, 0, 52, 18 };
static const uint8_t box_u8_chars[] = { 3, 4, 5, 6, 2, 3, 4, 5, 6, 2, 3, 4, 5, 6, 2, 3, 4, 5, 6, 2 };
static const boxdef_t box_u8_def = {
    5, // w
    4, // h
    20, // total_size
    0, // step_x
    -4, // step_y
    -5, // rel_x
    -3, // rel_y
    2, // grad1_color_start
    4, // char_count
    46, // char_offset
    box_u8_idx, // char_idx
    box_u8_chars // box_chars
};

static const uint8_t box_u8_alt_idx[] = { 33, 0, 12, 26 };
static const uint8_t box_u8_alt_chars[] = { 3, 4, 5, 2, 6, 3, 4, 5, 2, 6, 3, 4, 5, 2, 6, 3, 4, 5, 2, 6 };
static const boxdef_t box_u8_alt_def = {
    5, // w
    4, // h
    20, // total_size
    0, // step_x
    -4, // step_y
    -4, // rel_x
    -3, // rel_y
    2, // grad1_color_start
    4, // char_count
    68, // char_offset
    box_u8_alt_idx, // char_idx
    box_u8_alt_chars // box_chars
};

const boxdef_t* const main_boxes[60] = {
    &box_r8_def, // 0: BOX_R8
    &box_r16u1_def, // 1: BOX_R16U1
    &box_r8u1_def, // 2: BOX_R8U1
    &box_r8u2_def, // 3: BOX_R8U2
    &box_r8u3_def, // 4: BOX_R8U3
    &box_r8u4_def, // 5: BOX_R8U4
    &box_r8u5_def, // 6: BOX_R8U5
    &box_r8u6_def, // 7: BOX_R8U6
    &box_r8u8_def, // 8: BOX_R8U8
    &box_r6u8_def, // 9: BOX_R6U8
    &box_r10u16_def, // 10: BOX_R10U16
    &box_r4u8_def, // 11: BOX_R4U8
    &box_r6u16_def, // 12: BOX_R6U16
    &box_r2u8_def, // 13: BOX_R2U8
    &box_r2u16_def, // 14: BOX_R2U16
    &box_u8_def, // 15: BOX_U8
    &box_l2u16_def, // 16: BOX_L2U16
    &box_l2u8_def, // 17: BOX_L2U8
    &box_l6u16_def, // 18: BOX_L6U16
    &box_l4u8_def, // 19: BOX_L4U8
    &box_l10u16_def, // 20: BOX_L10U16
    &box_l6u8_def, // 21: BOX_L6U8
    &box_l8u8_def, // 22: BOX_L8U8
    &box_l8u6_def, // 23: BOX_L8U6
    &box_l8u5_def, // 24: BOX_L8U5
    &box_l8u4_def, // 25: BOX_L8U4
    &box_l8u3_def, // 26: BOX_L8U3
    &box_l8u2_def, // 27: BOX_L8U2
    &box_l8u1_def, // 28: BOX_L8U1
    &box_l16u1_def, // 29: BOX_L16U1
    &box_l8_def, // 30: BOX_L8
    &box_l16d1_def, // 31: BOX_L16D1
    &box_l8d1_def, // 32: BOX_L8D1
    &box_l8d2_def, // 33: BOX_L8D2
    &box_l8d3_def, // 34: BOX_L8D3
    &box_l8d4_def, // 35: BOX_L8D4
    &box_l8d5_def, // 36: BOX_L8D5
    &box_l8d6_def, // 37: BOX_L8D6
    &box_l8d8_def, // 38: BOX_L8D8
    &box_l6d8_def, // 39: BOX_L6D8
    &box_l10d16_def, // 40: BOX_L10D16
    &box_l4d8_def, // 41: BOX_L4D8
    &box_l6d16_def, // 42: BOX_L6D16
    &box_l2d8_def, // 43: BOX_L2D8
    &box_l2d16_def, // 44: BOX_L2D16
    &box_d8_def, // 45: BOX_D8
    &box_r2d16_def, // 46: BOX_R2D16
    &box_r2d8_def, // 47: BOX_R2D8
    &box_r6d16_def, // 48: BOX_R6D16
    &box_r4d8_def, // 49: BOX_R4D8
    &box_r10d16_def, // 50: BOX_R10D16
    &box_r6d8_def, // 51: BOX_R6D8
    &box_r8d8_def, // 52: BOX_R8D8
    &box_r8d6_def, // 53: BOX_R8D6
    &box_r8d5_def, // 54: BOX_R8D5
    &box_r8d4_def, // 55: BOX_R8D4
    &box_r8d3_def, // 56: BOX_R8D3
    &box_r8d2_def, // 57: BOX_R8D2
    &box_r8d1_def, // 58: BOX_R8D1
    &box_r16d1_def, // 59: BOX_R16D1
};

const boxdef_t* const alt_boxes[60] = {
    &box_r8_alt_def, // 0: BOX_R8_ALT
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    &box_r8u8_alt_def, // 8: BOX_R8U8_ALT
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    &box_u8_alt_def, // 15: BOX_U8_ALT
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    &box_l8u8_alt_def, // 22: BOX_L8U8_ALT
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    &box_l8_alt_def, // 30: BOX_L8_ALT
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    &box_l8d8_alt_def, // 38: BOX_L8D8_ALT
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    &box_d8_alt_def, // 45: BOX_D8_ALT
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    &box_r8d8_alt_def, // 52: BOX_R8D8_ALT
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

#pragma data(data)

boxdef_t boxdef;

const boxdef_t *boxdef_set_main() {
  if (roll_angle >= kRollMax) {
    return NULL;
  }
  const boxdef_t *src = main_boxes[roll_angle];
  memcpy(&boxdef, src, sizeof(boxdef_t));
  return src;
}

const boxdef_t *boxdef_set_alt() {
  if (roll_angle >= kRollMax) {
    return NULL;
  }
  const boxdef_t *src = alt_boxes[roll_angle];
  if (src == NULL) {
    return boxdef_set_main();
  }
  memcpy(&boxdef, src, sizeof(boxdef_t));
  return src;
}

