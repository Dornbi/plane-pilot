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

static const uint8_t box_d8_idx[] = { 169, 186, 148, 129 };
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
    0x00, // flip_around: none flipped
    box_d8_idx, // char_idx
    box_d8_chars // box_chars
};

static const uint8_t box_d8_alt_idx[] = { 158, 142, 216, 9, 133 };
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
    0x01, // flip_around: chars 1..1 flipped
    box_d8_alt_idx, // char_idx
    box_d8_alt_chars // box_chars
};

static const uint8_t box_l10d16_idx[] = { 183, 186, 136, 151, 184, 137, 185, 142, 166, 202, 3, 129, 123, 209, 2, 4, 133, 204, 75, 179, 201, 207, 177, 38, 100, 163, 206 };
static const uint8_t box_l10d16_chars[] = { 0, 0, 0, 0, 12, 2, 13, 14, 3, 4, 10, 0, 0, 0, 15, 16, 17, 18, 19, 5, 6, 1, 0, 0, 0, 20, 2, 21, 22, 7, 4, 10, 1, 0, 0, 23, 2, 13, 14, 3, 8, 10, 1, 1, 0, 0, 24, 2, 18, 25, 5, 4, 1, 1, 1, 0, 20, 2, 21, 14, 11, 4, 10, 1, 1, 1, 26, 27, 28, 18, 3, 9, 6, 1, 1, 1, 1, 29, 2, 21, 22, 7, 4, 10, 1, 1, 1, 1 };
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
    0x02, // flip_around: chars 7..8 flipped
    box_l10d16_idx, // char_idx
    box_l10d16_chars // box_chars
};

static const uint8_t box_l10u16_idx[] = { 196, 79, 182, 81, 176, 18, 132, 78, 80, 162, 27, 199, 35, 29, 210, 37, 90, 212, 146, 142, 165, 173, 1, 135, 153, 182, 172, 166 };
static const uint8_t box_l10u16_chars[] = { 3, 4, 5, 21, 22, 13, 14, 0, 0, 0, 0, 1, 6, 7, 23, 24, 22, 25, 0, 0, 0, 0, 1, 8, 4, 9, 26, 27, 2, 15, 0, 0, 0, 1, 1, 3, 10, 28, 24, 22, 16, 0, 0, 0, 1, 1, 8, 11, 7, 23, 29, 22, 25, 0, 0, 1, 1, 1, 12, 4, 5, 21, 27, 17, 18, 0, 1, 1, 1, 1, 6, 10, 30, 24, 22, 19, 0, 1, 1, 1, 1, 8, 11, 7, 26, 27, 2, 20 };
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
    0x80, // flip_around: chars 10..17 flipped
    box_l10u16_idx, // char_idx
    box_l10u16_chars // box_chars
};

static const uint8_t box_l16d1_idx[] = { 190, 178, 88, 8, 16, 89, 11, 18, 50, 49, 48, 47, 46, 45, 44, 2, 12, 3, 187, 154, 4, 165, 155, 189, 15 };
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
    0x70, // flip_around: chars 8..14 flipped
    box_l16d1_idx, // char_idx
    box_l16d1_chars // box_chars
};

static const uint8_t box_l16u1_idx[] = { 178, 190, 89, 16, 8, 88, 78, 18, 11, 44, 45, 46, 47, 48, 49, 50, 12, 2, 165, 188, 154, 187, 15, 189, 155 };
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
    0x70, // flip_around: chars 9..15 flipped
    box_l16u1_idx, // char_idx
    box_l16u1_chars // box_chars
};

static const uint8_t box_l2d16_idx[] = { 169, 193, 151, 136, 158, 183, 142, 166, 148, 129, 41, 131, 144, 133, 216, 9, 159, 163 };
static const uint8_t box_l2d16_chars[] = { 0, 2, 11, 12, 3, 4, 13, 2, 11, 14, 3, 5, 13, 2, 15, 16, 6, 9, 17, 2, 18, 16, 7, 9, 17, 2, 18, 16, 7, 9, 19, 20, 12, 8, 4, 9, 19, 20, 12, 10, 4, 9, 2, 11, 12, 3, 4, 1 };
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
    0x02, // flip_around: chars 6..7 flipped
    box_l2d16_idx, // char_idx
    box_l2d16_chars // box_chars
};

static const uint8_t box_l2d8_idx[] = { 169, 186, 136, 158, 142, 166, 39, 148, 129, 214, 144, 133, 217, 9, 220, 163 };
static const uint8_t box_l2d8_chars[] = { 9, 2, 10, 11, 3, 4, 12, 2, 13, 14, 5, 7, 15, 2, 16, 14, 6, 7, 17, 18, 11, 8, 4, 7 };
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
    0x02, // flip_around: chars 4..5 flipped
    box_l2d8_idx, // char_idx
    box_l2d8_chars // box_chars
};

static const uint8_t box_l2u16_idx[] = { 74, 128, 162, 180, 76, 132, 81, 168, 172, 0, 181, 142, 93, 171, 146, 223, 135, 153 };
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
    0x00, // flip_around: none flipped
    box_l2u16_idx, // char_idx
    box_l2u16_chars // box_chars
};

static const uint8_t box_l2u8_idx[] = { 162, 128, 76, 180, 81, 132, 74, 94, 91, 221, 97, 181, 172, 142, 146, 135, 153 };
static const uint8_t box_l2u8_chars[] = { 3, 4, 14, 15, 16, 10, 0, 1, 5, 6, 17, 16, 11, 0, 1, 7, 8, 18, 19, 12, 0, 1, 9, 4, 18, 15, 2, 13 };
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
    0x40, // flip_around: chars 7..10 flipped
    box_l2u8_idx, // char_idx
    box_l2u8_chars // box_chars
};

static const uint8_t box_l4d8_idx[] = { 186, 185, 151, 166, 142, 213, 3, 129, 125, 4, 133 };
static const uint8_t box_l4d8_chars[] = { 0, 8, 2, 9, 10, 6, 3, 7, 0, 11, 2, 12, 13, 4, 5, 1, 8, 2, 9, 10, 6, 3, 7, 1, 11, 2, 12, 13, 4, 5, 1, 1 };
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
    0x02, // flip_around: chars 3..4 flipped
    box_l4d8_idx, // char_idx
    box_l4d8_chars // box_chars
};

static const uint8_t box_l4u8_idx[] = { 162, 79, 182, 81, 176, 90, 212, 173, 142, 135, 172 };
static const uint8_t box_l4u8_chars[] = { 3, 4, 5, 10, 11, 8, 0, 0, 1, 6, 7, 12, 13, 2, 9, 0, 1, 3, 4, 5, 10, 11, 8, 0, 1, 1, 6, 7, 12, 13, 2, 9 };
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
    0x20, // flip_around: chars 5..6 flipped
    box_l4u8_idx, // char_idx
    box_l4u8_chars // box_chars
};

static const uint8_t box_l6d16_idx[] = { 186, 136, 151, 183, 169, 185, 158, 166, 142, 40, 148, 129, 218, 9, 133, 209, 163, 215, 179, 38, 3, 144, 219 };
static const uint8_t box_l6d16_chars[] = { 0, 0, 12, 2, 13, 14, 10, 3, 11, 0, 0, 15, 2, 16, 17, 4, 5, 1, 0, 0, 18, 19, 14, 6, 3, 11, 1, 0, 20, 2, 13, 21, 7, 3, 1, 1, 0, 15, 2, 16, 17, 8, 11, 1, 1, 22, 2, 23, 14, 10, 3, 11, 1, 1, 20, 2, 24, 21, 4, 5, 1, 1, 1, 25, 19, 16, 6, 9, 11, 1, 1, 1 };
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
    0x02, // flip_around: chars 7..8 flipped
    box_l6d16_idx, // char_idx
    box_l6d16_chars // box_chars
};

static const uint8_t box_l6d8_idx[] = { 183, 185, 151, 136, 186, 184, 142, 166, 203, 3, 77, 200, 43, 163, 4, 133, 67, 208, 143, 155, 205, 75 };
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
    0x02, // flip_around: chars 6..7 flipped
    box_l6d8_idx, // char_idx
    box_l6d8_chars // box_chars
};

static const uint8_t box_l6u16_idx[] = { 162, 79, 182, 76, 132, 74, 81, 176, 196, 91, 27, 94, 37, 222, 96, 173, 142, 146, 153, 166, 172, 135 };
static const uint8_t box_l6u16_chars[] = { 3, 4, 5, 18, 19, 12, 0, 0, 0, 1, 6, 7, 20, 21, 13, 0, 0, 0, 1, 8, 4, 22, 23, 19, 14, 0, 0, 1, 3, 4, 5, 20, 19, 12, 0, 0, 1, 1, 9, 10, 24, 21, 2, 15, 0, 1, 1, 11, 4, 22, 23, 19, 14, 0, 1, 1, 1, 4, 7, 20, 19, 16, 0, 1, 1, 1, 9, 10, 24, 23, 2, 17 };
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
    0x60, // flip_around: chars 9..14 flipped
    box_l6u16_idx, // char_idx
    box_l6u16_chars // box_chars
};

static const uint8_t box_l6u8_idx[] = { 196, 78, 81, 18, 80, 176, 162, 79, 132, 117, 36, 28, 122, 31, 33, 182, 164, 153, 166, 173, 142, 165, 172, 135 };
static const uint8_t box_l6u8_chars[] = { 3, 4, 18, 19, 20, 12, 13, 0, 0, 1, 5, 4, 21, 22, 23, 14, 15, 0, 1, 6, 7, 8, 24, 25, 23, 16, 0, 1, 1, 9, 10, 11, 26, 20, 2, 17 };
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
    0x60, // flip_around: chars 9..14 flipped
    box_l6u8_idx, // char_idx
    box_l6u8_chars // box_chars
};

static const uint8_t box_l8_idx[] = { 88, 11, 187, 155 };
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
    0x00, // flip_around: none flipped
    box_l8_idx, // char_idx
    box_l8_chars // box_chars
};

static const uint8_t box_l8d1_idx[] = { 190, 178, 88, 8, 16, 89, 11, 18, 51, 52, 53, 54, 55, 56, 57, 58, 2, 12, 3, 187, 154, 4, 155, 177, 15 };
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
    0x00, // flip_around: none flipped
    box_l8d1_idx, // char_idx
    box_l8d1_chars // box_chars
};

static const uint8_t box_l8d2_idx[] = { 190, 88, 16, 11, 18, 68, 69, 70, 42, 2, 3, 154, 4, 77, 155, 177 };
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
    0x00, // flip_around: none flipped
    box_l8d2_idx, // char_idx
    box_l8d2_chars // box_chars
};

static const uint8_t box_l8d3_idx[] = { 190, 178, 88, 137, 186, 11, 18, 110, 109, 112, 115, 67, 111, 114, 43, 2, 3, 113, 116, 154, 4, 155, 12, 77, 15, 177 };
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
    0x00, // flip_around: none flipped
    box_l8d3_idx, // char_idx
    box_l8d3_chars // box_chars
};

static const uint8_t box_l8d4_idx[] = { 178, 8, 137, 11, 111, 115, 2, 3, 4, 77, 177 };
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
    0x00, // flip_around: none flipped
    box_l8d4_idx, // char_idx
    box_l8d4_chars // box_chars
};

static const uint8_t box_l8d5_idx[] = { 183, 88, 137, 190, 185, 186, 18, 11, 184, 111, 109, 114, 112, 43, 2, 3, 110, 115, 143, 77, 67, 113, 4, 155, 116, 177 };
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
    0x00, // flip_around: none flipped
    box_l8d5_idx, // char_idx
    box_l8d5_chars // box_chars
};

static const uint8_t box_l8d6_idx[] = { 183, 185, 184, 137, 11, 88, 186, 18, 24, 67, 25, 23, 26, 3, 43, 2, 77, 143, 155, 4, 177 };
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
    0x00, // flip_around: none flipped
    box_l8d6_idx, // char_idx
    box_l8d6_chars // box_chars
};

static const uint8_t box_l8d8_idx[] = { 184, 186, 142, 98, 3, 4, 177 };
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
    0x01, // flip_around: chars 2..2 flipped
    box_l8d8_idx, // char_idx
    box_l8d8_chars // box_chars
};

static const uint8_t box_l8d8_alt_idx[] = { 183, 185, 151, 124, 99, 2, 75, 77 };
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
    0x00, // flip_around: none flipped
    box_l8d8_alt_idx, // char_idx
    box_l8d8_alt_chars // box_chars
};

static const uint8_t box_l8u1_idx[] = { 178, 190, 89, 16, 8, 88, 78, 18, 11, 60, 61, 62, 63, 64, 65, 66, 59, 12, 2, 165, 188, 154, 187, 15, 189, 155 };
static const uint8_t box_l8u1_chars[] = { 12, 13, 14, 15, 16, 17, 18, 0, 20, 21, 21, 2, 2, 2, 2, 19, 22, 23, 24, 24, 24, 25, 25, 20, 3, 4, 4, 26, 26, 27, 28, 22, 5, 6, 7, 7, 7, 8, 9, 9, 1, 1, 10, 10, 10, 11, 11, 11 };
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
    0x80, // flip_around: chars 9..16 flipped
    box_l8u1_idx, // char_idx
    box_l8u1_chars // box_chars
};

static const uint8_t box_l8u2_idx[] = { 15, 16, 8, 178, 18, 11, 89, 119, 120, 121, 118, 2, 154, 12, 15, 155, 165 };
static const uint8_t box_l8u2_chars[] = { 10, 11, 12, 0, 14, 2, 2, 13, 15, 15, 16, 16, 3, 17, 18, 19, 4, 5, 6, 6, 7, 7, 8, 9 };
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
    0x40, // flip_around: chars 7..10 flipped
    box_l8u2_idx, // char_idx
    box_l8u2_chars // box_chars
};

static const uint8_t box_l8u3_idx[] = { 78, 176, 11, 89, 8, 18, 196, 16, 190, 105, 108, 117, 103, 106, 122, 101, 104, 107, 102, 187, 12, 2, 189, 165, 173, 15, 188 };
static const uint8_t box_l8u3_chars[] = { 12, 13, 0, 0, 0, 0, 0, 0, 2, 14, 15, 16, 17, 0, 0, 0, 22, 23, 24, 2, 18, 19, 20, 0, 25, 26, 27, 22, 23, 2, 2, 21, 3, 4, 28, 25, 26, 27, 23, 24, 5, 6, 7, 3, 4, 28, 26, 29, 1, 1, 8, 9, 10, 7, 3, 11, 1, 1, 1, 1, 8, 8, 9, 7, 1, 1, 1, 1, 1, 1, 1, 8 };
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
    0xa0, // flip_around: chars 9..18 flipped
    box_l8u3_idx, // char_idx
    box_l8u3_chars // box_chars
};

static const uint8_t box_l8u4_idx[] = { 176, 89, 79, 18, 106, 102, 153, 142, 165, 173, 182 };
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
    0x20, // flip_around: chars 4..5 flipped
    box_l8u4_idx, // char_idx
    box_l8u4_chars // box_chars
};

static const uint8_t box_l8u5_idx[] = { 79, 176, 18, 80, 78, 175, 196, 108, 117, 105, 102, 107, 104, 122, 101, 106, 103, 153, 164, 142, 182, 165, 173, 166 };
static const uint8_t box_l8u5_chars[] = { 10, 0, 0, 0, 0, 0, 0, 0, 11, 12, 0, 0, 0, 0, 0, 0, 20, 2, 13, 14, 0, 0, 0, 0, 21, 20, 22, 2, 15, 16, 0, 0, 23, 24, 25, 20, 2, 17, 18, 0, 3, 4, 23, 24, 25, 22, 2, 19, 5, 6, 7, 8, 24, 21, 20, 22, 1, 5, 9, 3, 4, 23, 24, 25, 1, 1, 1, 5, 9, 3, 4, 26, 1, 1, 1, 1, 1, 9, 6, 7, 1, 1, 1, 1, 1, 1, 5, 9 };
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
    0xa0, // flip_around: chars 7..16 flipped
    box_l8u5_idx, // char_idx
    box_l8u5_chars // box_chars
};

static const uint8_t box_l8u6_idx[] = { 78, 196, 79, 175, 176, 18, 80, 122, 19, 22, 117, 21, 20, 142, 173, 153, 166, 164, 182, 165 };
static const uint8_t box_l8u6_chars[] = { 10, 0, 0, 0, 11, 12, 0, 0, 16, 13, 14, 0, 17, 18, 2, 15, 19, 20, 18, 16, 3, 21, 22, 17, 4, 5, 6, 22, 1, 4, 5, 7, 1, 1, 8, 9, 1, 1, 1, 8 };
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
    0x60, // flip_around: chars 7..12 flipped
    box_l8u6_idx, // char_idx
    box_l8u6_chars // box_chars
};

static const uint8_t box_l8u8_idx[] = { 78, 80, 18, 197, 153, 164, 182 };
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
    0x10, // flip_around: chars 3..3 flipped
    box_l8u8_idx, // char_idx
    box_l8u8_chars // box_chars
};

static const uint8_t box_l8u8_alt_idx[] = { 176, 79, 196, 198, 211, 142, 173, 165 };
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
    0x20, // flip_around: chars 3..4 flipped
    box_l8u8_alt_idx, // char_idx
    box_l8u8_alt_chars // box_chars
};

static const uint8_t box_l8_alt_idx[] = { 190, 16, 18, 47, 2, 188 };
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
    0x10, // flip_around: chars 3..3 flipped
    box_l8_alt_idx, // char_idx
    box_l8_alt_chars // box_chars
};

static const uint8_t box_r10d16_idx[] = { 139, 13, 152, 193, 83, 87, 167, 134, 168, 170, 125, 38, 100, 207, 201, 123, 209, 213, 145, 131, 161, 129, 174, 160, 73, 87, 194, 9, 133 };
static const uint8_t box_r10d16_chars[] = { 13, 2, 21, 22, 10, 3, 1, 1, 1, 1, 1, 14, 15, 23, 24, 25, 4, 5, 1, 1, 1, 1, 0, 26, 2, 27, 24, 11, 6, 7, 1, 1, 1, 0, 0, 16, 2, 21, 28, 12, 3, 1, 1, 1, 0, 0, 17, 2, 29, 24, 8, 4, 7, 1, 1, 0, 0, 0, 26, 2, 21, 24, 10, 6, 1, 1, 0, 0, 0, 18, 19, 23, 30, 31, 12, 3, 1, 0, 0, 0, 0, 20, 2, 29, 24, 9, 4, 7 };
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
    0x83, // flip_around: chars 7..17 flipped
    box_r10d16_idx, // char_idx
    box_r10d16_chars // box_chars
};

static const uint8_t box_r10u16_idx[] = { 71, 128, 180, 149, 132, 195, 130, 6, 127, 72, 170, 141, 210, 37, 134, 172, 32, 156, 171, 29, 140, 35, 150, 27, 199, 157, 34, 30 };
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
    0x00, // flip_around: none flipped
    box_r10u16_idx, // char_idx
    box_r10u16_chars // box_chars
};

static const uint8_t box_r16d1_idx[] = { 84, 126, 83, 14, 5, 85, 13, 127, 6, 86, 191, 7, 87, 17, 138, 192, 150, 141, 82, 50, 49, 48, 47, 46, 45, 44 };
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
    0x00, // flip_around: none flipped
    box_r16d1_idx, // char_idx
    box_r16d1_chars // box_chars
};

static const uint8_t box_r16u1_idx[] = { 83, 126, 84, 127, 85, 5, 86, 6, 6, 87, 7, 191, 150, 192, 138, 82, 44, 45, 46, 47, 48, 49, 50 };
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
    0x00, // flip_around: none flipped
    box_r16u1_idx, // char_idx
    box_r16u1_chars // box_chars
};

static const uint8_t box_r2d16_idx[] = { 186, 167, 174, 193, 158, 136, 169, 152, 168, 142, 161, 129, 159, 9, 216, 133, 41, 145, 148, 131 };
static const uint8_t box_r2d16_chars[] = { 2, 13, 14, 11, 3, 1, 15, 13, 14, 4, 3, 12, 15, 2, 16, 5, 6, 12, 17, 2, 16, 18, 7, 12, 17, 2, 16, 18, 7, 12, 19, 2, 20, 18, 8, 12, 19, 2, 21, 22, 9, 10, 0, 2, 21, 22, 9, 3 };
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
    0x02, // flip_around: chars 8..9 flipped
    box_r2d16_idx, // char_idx
    box_r2d16_chars // box_chars
};

static const uint8_t box_r2d8_idx[] = { 167, 193, 83, 152, 139, 170, 168, 220, 217, 214, 39, 161, 129, 9, 174, 145, 133, 148 };
static const uint8_t box_r2d8_chars[] = { 10, 14, 15, 3, 4, 1, 11, 2, 16, 17, 4, 5, 12, 2, 18, 19, 8, 6, 13, 2, 20, 15, 9, 7 };
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
    0x42, // flip_around: chars 5..10 flipped
    box_r2d8_idx, // char_idx
    box_r2d8_chars // box_chars
};

static const uint8_t box_r2u16_idx[] = { 81, 132, 76, 180, 128, 162, 74, 135, 141, 223, 171, 93, 172, 181, 0, 168, 134, 140 };
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
    0x00, // flip_around: none flipped
    box_r2u16_idx, // char_idx
    box_r2u16_chars // box_chars
};

static const uint8_t box_r2u8_idx[] = { 128, 180, 71, 74, 130, 149, 132, 171, 141, 91, 181, 172, 94, 134, 97, 170, 221 };
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
    0x00, // flip_around: none flipped
    box_r2u8_idx, // char_idx
    box_r2u8_chars // box_chars
};

static const uint8_t box_r4d8_idx[] = { 152, 193, 170, 168, 125, 213, 9, 133, 148, 129 };
static const uint8_t box_r4d8_chars[] = { 7, 2, 9, 10, 5, 3, 1, 8, 2, 11, 12, 6, 4, 1, 0, 7, 2, 9, 10, 5, 3, 0, 8, 2, 11, 12, 6, 4 };
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
    0x22, // flip_around: chars 2..5 flipped
    box_r4d8_idx, // char_idx
    box_r4d8_chars // box_chars
};

static const uint8_t box_r4u8_idx[] = { 149, 132, 71, 128, 6, 134, 140, 212, 171, 141, 90 };
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
    0x00, // flip_around: none flipped
    box_r4u8_idx, // char_idx
    box_r4u8_chars // box_chars
};

static const uint8_t box_r6d16_idx[] = { 193, 83, 139, 167, 152, 174, 170, 142, 168, 219, 214, 38, 217, 209, 40, 161, 129, 174, 145, 131, 148, 9, 133 };
static const uint8_t box_r6d16_chars[] = { 12, 18, 19, 20, 3, 4, 1, 1, 13, 2, 21, 22, 9, 5, 1, 1, 14, 2, 23, 19, 6, 3, 10, 1, 0, 15, 2, 24, 25, 9, 7, 1, 0, 13, 2, 23, 19, 11, 5, 1, 0, 0, 16, 18, 19, 8, 3, 4, 0, 0, 15, 2, 21, 25, 9, 7, 0, 0, 17, 2, 23, 19, 11, 3 };
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
    0x63, // flip_around: chars 6..14 flipped
    box_r6d16_idx, // char_idx
    box_r6d16_chars // box_chars
};

static const uint8_t box_r6d8_idx[] = { 193, 83, 85, 139, 87, 13, 152, 168, 134, 205, 67, 208, 200, 43, 203, 73, 129, 192, 131, 161, 10, 194 };
static const uint8_t box_r6d8_chars[] = { 12, 2, 18, 19, 10, 3, 4, 1, 1, 13, 14, 2, 20, 21, 11, 3, 1, 1, 0, 15, 16, 22, 23, 21, 5, 6, 1, 0, 0, 17, 2, 24, 19, 7, 8, 9 };
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
    0x62, // flip_around: chars 7..14 flipped
    box_r6d8_idx, // char_idx
    box_r6d8_chars // box_chars
};

static const uint8_t box_r6u16_idx[] = { 74, 128, 149, 132, 71, 180, 130, 127, 72, 181, 172, 95, 170, 140, 37, 171, 141, 92, 134, 27, 96, 222 };
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
    0x00, // flip_around: none flipped
    box_r6u16_idx, // char_idx
    box_r6u16_chars // box_chars
};

static const uint8_t box_r6u8_idx[] = { 149, 130, 72, 128, 6, 71, 180, 127, 132, 156, 172, 141, 28, 122, 150, 117, 36, 170, 140, 33, 157, 31 };
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
    0x00, // flip_around: none flipped
    box_r6u8_idx, // char_idx
    box_r6u8_chars // box_chars
};

static const uint8_t box_r8_idx[] = { 126, 5, 7, 138 };
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
    0x00, // flip_around: none flipped
    box_r8_idx, // char_idx
    box_r8_chars // box_chars
};

static const uint8_t box_r8d1_idx[] = { 126, 83, 5, 85, 13, 127, 84, 6, 86, 14, 51, 52, 53, 54, 55, 56, 57, 58, 7, 87, 138, 192, 150, 191, 141, 82, 17 };
static const uint8_t box_r8d1_chars[] = { 3, 4, 4, 4, 4, 1, 1, 1, 5, 5, 6, 7, 8, 8, 9, 9, 21, 21, 22, 22, 22, 10, 11, 12, 23, 23, 24, 24, 25, 25, 26, 26, 2, 2, 2, 2, 2, 27, 28, 29, 13, 14, 15, 16, 17, 18, 19, 20 };
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
    0x80, // flip_around: chars 10..17 flipped
    box_r8d1_idx, // char_idx
    box_r8d1_chars // box_chars
};

static const uint8_t box_r8d2_idx[] = { 126, 83, 14, 85, 13, 84, 86, 68, 69, 70, 42, 7, 87, 17, 192, 150, 191, 82 };
static const uint8_t box_r8d2_chars[] = { 3, 4, 4, 1, 5, 6, 7, 8, 14, 15, 15, 9, 16, 17, 18, 19, 2, 2, 2, 20, 10, 11, 12, 13 };
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
    0x40, // flip_around: chars 7..10 flipped
    box_r8d2_idx, // char_idx
    box_r8d2_chars // box_chars
};

static const uint8_t box_r8d3_idx[] = { 84, 83, 86, 85, 14, 126, 13, 147, 130, 110, 113, 116, 67, 111, 114, 43, 109, 112, 115, 191, 87, 17, 192, 82, 73, 150 };
static const uint8_t box_r8d3_chars[] = { 3, 4, 4, 1, 1, 1, 1, 1, 5, 6, 3, 3, 4, 1, 1, 1, 22, 23, 5, 7, 6, 3, 8, 4, 24, 25, 22, 11, 23, 5, 6, 9, 2, 2, 26, 27, 25, 22, 23, 10, 12, 13, 14, 2, 2, 24, 25, 28, 0, 0, 15, 16, 17, 18, 2, 26, 0, 0, 0, 0, 0, 19, 20, 21 };
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
    0xb0, // flip_around: chars 8..18 flipped
    box_r8d3_idx, // char_idx
    box_r8d3_chars // box_chars
};

static const uint8_t box_r8d4_idx[] = { 84, 83, 86, 85, 111, 115, 191, 87, 17, 192 };
static const uint8_t box_r8d4_chars[] = { 3, 4, 1, 1, 5, 6, 3, 4, 9, 10, 5, 6, 11, 12, 9, 10, 2, 2, 11, 12, 7, 8, 2, 2, 0, 0, 7, 8 };
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
    0x20, // flip_around: chars 4..5 flipped
    box_r8d4_idx, // char_idx
    box_r8d4_chars // box_chars
};

static const uint8_t box_r8d5_idx[] = { 83, 13, 139, 86, 85, 84, 147, 193, 14, 111, 116, 67, 113, 110, 115, 112, 43, 109, 114, 191, 87, 17, 150, 129, 82, 73, 192, 10 };
static const uint8_t box_r8d5_chars[] = { 3, 1, 1, 1, 1, 1, 1, 1, 4, 5, 1, 1, 1, 1, 1, 1, 6, 7, 8, 3, 1, 1, 1, 1, 22, 23, 6, 4, 8, 3, 1, 1, 24, 25, 26, 9, 7, 10, 3, 1, 2, 27, 28, 22, 23, 6, 7, 8, 12, 13, 2, 27, 29, 22, 23, 11, 0, 14, 15, 2, 2, 28, 30, 23, 0, 0, 0, 16, 17, 2, 27, 29, 0, 0, 0, 0, 0, 18, 19, 2, 0, 0, 0, 0, 0, 0, 20, 21 };
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
    0xa0, // flip_around: chars 9..18 flipped
    box_r8d5_idx, // char_idx
    box_r8d5_chars // box_chars
};

static const uint8_t box_r8d6_idx[] = { 83, 13, 84, 147, 85, 86, 193, 24, 43, 23, 26, 67, 25, 191, 87, 17, 150, 82, 192, 129, 73 };
static const uint8_t box_r8d6_chars[] = { 3, 1, 1, 1, 4, 5, 1, 1, 6, 7, 5, 3, 16, 17, 8, 9, 18, 19, 17, 8, 2, 20, 21, 22, 10, 11, 2, 23, 0, 12, 13, 2, 0, 0, 14, 15 };
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
    0x60, // flip_around: chars 7..12 flipped
    box_r8d6_idx, // char_idx
    box_r8d6_chars // box_chars
};

static const uint8_t box_r8d8_idx[] = { 83, 193, 134, 98, 87, 10, 82 };
static const uint8_t box_r8d8_chars[] = { 3, 1, 1, 1, 4, 3, 1, 1, 5, 4, 3, 1, 7, 5, 4, 3, 8, 7, 5, 4, 9, 8, 7, 5, 2, 9, 8, 7, 6, 2, 9, 8, 0, 6, 2, 9, 0, 0, 6, 2, 0, 0, 0, 6 };
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
    0x11, // flip_around: chars 2..3 flipped
    box_r8d8_idx, // char_idx
    box_r8d8_chars // box_chars
};

static const uint8_t box_r8d8_alt_idx[] = { 139, 13, 147, 99, 124, 129, 73 };
static const uint8_t box_r8d8_alt_chars[] = { 3, 1, 1, 1, 4, 3, 1, 1, 5, 4, 3, 1, 8, 5, 4, 3, 9, 8, 5, 4, 2, 9, 8, 5, 6, 2, 9, 8, 7, 6, 2, 9, 0, 7, 6, 2, 0, 0, 7, 6, 0, 0, 0, 7 };
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
    0x20, // flip_around: chars 3..4 flipped
    box_r8d8_alt_idx, // char_idx
    box_r8d8_alt_chars // box_chars
};

static const uint8_t box_r8u1_idx[] = { 83, 126, 84, 127, 85, 5, 86, 6, 87, 7, 191, 150, 192, 138, 82, 59, 60, 61, 62, 63, 64, 65, 66 };
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
    0x00, // flip_around: none flipped
    box_r8u1_idx, // char_idx
    box_r8u1_chars // box_chars
};

static const uint8_t box_r8u2_idx[] = { 126, 127, 5, 6, 87, 7, 191, 150, 192, 138, 82, 118, 119, 120, 121 };
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
    0x00, // flip_around: none flipped
    box_r8u2_idx, // char_idx
    box_r8u2_chars // box_chars
};

static const uint8_t box_r8u3_idx[] = { 126, 84, 83, 127, 85, 5, 86, 6, 87, 7, 191, 192, 138, 150, 82, 101, 104, 107, 117, 103, 106, 122, 102, 105, 108 };
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
    0x00, // flip_around: none flipped
    box_r8u3_idx, // char_idx
    box_r8u3_chars // box_chars
};

static const uint8_t box_r8u4_idx[] = { 126, 127, 128, 130, 6, 7, 150, 138, 82, 102, 106 };
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
    0x00, // flip_around: none flipped
    box_r8u4_idx, // char_idx
    box_r8u4_chars // box_chars
};

static const uint8_t box_r8u5_idx[] = { 126, 127, 71, 5, 6, 130, 72, 128, 157, 7, 150, 138, 156, 141, 140, 101, 106, 104, 122, 192, 102, 107, 117, 105, 103, 108 };
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
    0x00, // flip_around: none flipped
    box_r8u5_idx, // char_idx
    box_r8u5_chars // box_chars
};

static const uint8_t box_r8u6_idx[] = { 126, 72, 128, 71, 127, 5, 6, 130, 191, 7, 150, 140, 192, 141, 138, 117, 21, 19, 22, 20, 122 };
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
    0x00, // flip_around: none flipped
    box_r8u6_idx, // char_idx
    box_r8u6_chars // box_chars
};

static const uint8_t box_r8u8_idx[] = { 149, 130, 156, 150, 140, 197 };
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
    0x00, // flip_around: none flipped
    box_r8u8_idx, // char_idx
    box_r8u8_chars // box_chars
};

static const uint8_t box_r8u8_alt_idx[] = { 195, 128, 130, 157, 192, 211, 198 };
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
    0x00, // flip_around: none flipped
    box_r8u8_alt_idx, // char_idx
    box_r8u8_alt_chars // box_chars
};

static const uint8_t box_r8_alt_idx[] = { 127, 6, 150, 47 };
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
    0x00, // flip_around: none flipped
    box_r8_alt_idx, // char_idx
    box_r8_alt_chars // box_chars
};

static const uint8_t box_u8_idx[] = { 74, 128, 135, 172 };
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
    0x00, // flip_around: none flipped
    box_u8_idx, // char_idx
    box_u8_chars // box_chars
};

static const uint8_t box_u8_alt_idx[] = { 76, 180, 171, 93 };
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
    0x00, // flip_around: none flipped
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

static const uint8_t kAltBoxCount = 8;

static const uint8_t alt_box_angles[kAltBoxCount] = {
    0, // BOX_R8_ALT
    8, // BOX_R8U8_ALT
    15, // BOX_U8_ALT
    22, // BOX_L8U8_ALT
    30, // BOX_L8_ALT
    38, // BOX_L8D8_ALT
    45, // BOX_D8_ALT
    52, // BOX_R8D8_ALT
};

static const boxdef_t* const alt_boxes[kAltBoxCount] = {
    &box_r8_alt_def, // 0: BOX_R8_ALT
    &box_r8u8_alt_def, // 8: BOX_R8U8_ALT
    &box_u8_alt_def, // 15: BOX_U8_ALT
    &box_l8u8_alt_def, // 22: BOX_L8U8_ALT
    &box_l8_alt_def, // 30: BOX_L8_ALT
    &box_l8d8_alt_def, // 38: BOX_L8D8_ALT
    &box_d8_alt_def, // 45: BOX_D8_ALT
    &box_r8d8_alt_def, // 52: BOX_R8D8_ALT
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
  for (uint8_t i = 0; i < kAltBoxCount; ++i) {
    if (alt_box_angles[i] == roll_angle) {
      const boxdef_t *src = alt_boxes[i];
      memcpy(&boxdef, src, sizeof(boxdef_t));
      return src;
    }
  }
  return boxdef_set_main();
}

