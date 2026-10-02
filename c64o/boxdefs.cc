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

static const uint8_t box_d8_idx[] = { 200, 158, 199, 186 };
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

static const uint8_t box_d8_alt_idx[] = { 197, 100, 205, 193, 175 };
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

static const uint8_t box_l10d16_idx[] = { 161, 158, 176, 171, 160, 157, 162, 100, 107, 185, 131, 186, 187, 188, 121, 132, 175, 180, 170, 184, 189, 179, 141, 181, 182, 174, 183 };
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

static const uint8_t box_l10u16_idx[] = { 106, 103, 104, 97, 105, 113, 61, 112, 109, 93, 72, 73, 69, 79, 76, 77, 80, 81, 101, 100, 110, 108, 111, 96, 102, 104, 62, 107 };
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

static const uint8_t box_l16d1_idx[] = { 124, 128, 130, 123, 122, 116, 126, 113, 21, 20, 19, 7, 18, 17, 16, 121, 120, 131, 117, 127, 132, 110, 129, 118, 125 };
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

static const uint8_t box_l16u1_idx[] = { 128, 124, 116, 122, 123, 130, 112, 113, 126, 16, 17, 18, 7, 19, 20, 21, 120, 121, 110, 119, 127, 117, 125, 118, 129 };
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

static const uint8_t box_l2d16_idx[] = { 200, 207, 171, 176, 197, 161, 100, 107, 199, 186, 208, 209, 195, 175, 205, 193, 206, 174 };
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

static const uint8_t box_l2d8_idx[] = { 200, 158, 176, 197, 100, 107, 201, 199, 186, 202, 195, 175, 203, 193, 204, 174 };
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

static const uint8_t box_l2u16_idx[] = { 82, 44, 93, 66, 99, 61, 97, 95, 62, 94, 84, 100, 92, 78, 101, 98, 96, 102 };
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

static const uint8_t box_l2u8_idx[] = { 93, 44, 99, 66, 97, 61, 82, 90, 89, 88, 91, 84, 62, 100, 101, 96, 102 };
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

static const uint8_t box_l4d8_idx[] = { 158, 162, 171, 107, 100, 190, 131, 186, 191, 132, 175 };
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

static const uint8_t box_l4u8_idx[] = { 93, 103, 104, 97, 105, 80, 81, 108, 100, 96, 62 };
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

static const uint8_t box_l6d16_idx[] = { 158, 176, 171, 161, 200, 162, 197, 107, 100, 198, 199, 186, 192, 193, 175, 188, 174, 194, 184, 181, 131, 195, 196 };
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

static const uint8_t box_l6d8_idx[] = { 161, 162, 171, 176, 158, 160, 100, 107, 172, 131, 146, 173, 156, 174, 132, 175, 147, 177, 159, 129, 178, 170 };
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

static const uint8_t box_l6u16_idx[] = { 93, 103, 104, 99, 61, 82, 97, 105, 106, 89, 72, 90, 77, 87, 86, 108, 100, 101, 102, 107, 62, 96 };
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

static const uint8_t box_l6u8_idx[] = { 106, 112, 97, 113, 109, 105, 93, 103, 61, 34, 65, 64, 43, 63, 68, 104, 114, 102, 107, 108, 100, 110, 62, 96 };
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

static const uint8_t box_l8_idx[] = { 130, 126, 117, 129 };
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

static const uint8_t box_l8d1_idx[] = { 124, 128, 130, 123, 122, 116, 126, 113, 133, 134, 135, 136, 137, 138, 139, 140, 121, 120, 131, 117, 127, 132, 129, 141, 125 };
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

static const uint8_t box_l8d2_idx[] = { 124, 130, 122, 126, 113, 142, 143, 144, 145, 121, 131, 127, 132, 146, 129, 141 };
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

static const uint8_t box_l8d3_idx[] = { 124, 128, 130, 157, 158, 126, 113, 149, 152, 153, 154, 147, 148, 155, 156, 121, 131, 150, 151, 127, 132, 129, 120, 146, 125, 141 };
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

static const uint8_t box_l8d4_idx[] = { 128, 123, 157, 126, 148, 154, 121, 131, 132, 146, 141 };
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

static const uint8_t box_l8d5_idx[] = { 161, 130, 157, 124, 162, 158, 113, 126, 160, 148, 152, 155, 153, 156, 121, 131, 149, 154, 159, 146, 147, 150, 132, 129, 151, 141 };
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

static const uint8_t box_l8d6_idx[] = { 161, 162, 160, 157, 126, 130, 158, 113, 166, 147, 163, 164, 165, 131, 156, 121, 146, 159, 129, 132, 141 };
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

static const uint8_t box_l8d8_idx[] = { 160, 158, 100, 167, 131, 132, 141 };
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

static const uint8_t box_l8d8_alt_idx[] = { 161, 162, 171, 168, 169, 121, 170, 146 };
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

static const uint8_t box_l8u1_idx[] = { 128, 124, 116, 122, 123, 130, 112, 113, 126, 23, 24, 25, 26, 27, 28, 29, 22, 120, 121, 110, 119, 127, 117, 125, 118, 129 };
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

static const uint8_t box_l8u2_idx[] = { 125, 122, 123, 128, 113, 126, 116, 31, 32, 33, 30, 121, 127, 120, 125, 129, 110 };
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

static const uint8_t box_l8u3_idx[] = { 112, 105, 126, 116, 123, 113, 106, 122, 124, 37, 38, 34, 35, 42, 43, 39, 40, 41, 36, 117, 120, 121, 118, 110, 108, 125, 119 };
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

static const uint8_t box_l8u4_idx[] = { 105, 116, 103, 113, 42, 36, 102, 100, 110, 108, 104 };
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

static const uint8_t box_l8u5_idx[] = { 103, 105, 113, 109, 112, 115, 106, 38, 34, 37, 36, 41, 40, 43, 39, 42, 35, 102, 114, 100, 104, 110, 108, 107 };
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

static const uint8_t box_l8u6_idx[] = { 112, 106, 103, 115, 105, 113, 109, 43, 53, 54, 34, 52, 55, 100, 108, 102, 107, 114, 104, 110 };
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

static const uint8_t box_l8u8_idx[] = { 112, 109, 113, 57, 102, 114, 104 };
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

static const uint8_t box_l8u8_alt_idx[] = { 105, 103, 106, 60, 59, 100, 108, 110 };
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

static const uint8_t box_l8_alt_idx[] = { 124, 122, 113, 7, 121, 119 };
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

static const uint8_t box_r10d16_idx[] = { 215, 217, 211, 207, 8, 12, 213, 70, 95, 67, 191, 181, 182, 179, 189, 187, 188, 190, 210, 209, 212, 186, 214, 218, 219, 12, 216, 193, 175 };
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

static const uint8_t box_r10u16_idx[] = { 49, 44, 66, 56, 61, 58, 45, 5, 4, 46, 67, 51, 76, 77, 70, 62, 71, 47, 78, 79, 48, 69, 6, 72, 73, 50, 74, 75 };
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

static const uint8_t box_r16d1_idx[] = { 9, 0, 8, 223, 1, 10, 217, 4, 5, 11, 13, 2, 12, 222, 3, 14, 6, 51, 15, 21, 20, 19, 7, 18, 17, 16 };
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
    0x00, // flip_around: none flipped
    box_r16u1_idx, // char_idx
    box_r16u1_chars // box_chars
};

static const uint8_t box_r2d16_idx[] = { 158, 213, 214, 207, 197, 176, 200, 211, 95, 100, 212, 186, 206, 193, 205, 175, 208, 210, 199, 209 };
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

static const uint8_t box_r2d8_idx[] = { 213, 207, 8, 211, 215, 67, 95, 204, 203, 202, 201, 212, 186, 193, 214, 210, 175, 199 };
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

static const uint8_t box_r2u16_idx[] = { 97, 61, 99, 66, 44, 93, 82, 96, 51, 98, 78, 92, 62, 84, 94, 95, 70, 48 };
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

static const uint8_t box_r2u8_idx[] = { 44, 66, 49, 82, 45, 56, 61, 78, 51, 89, 84, 62, 90, 70, 91, 67, 88 };
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

static const uint8_t box_r4d8_idx[] = { 211, 207, 67, 95, 191, 190, 193, 175, 199, 186 };
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

static const uint8_t box_r4u8_idx[] = { 56, 61, 49, 44, 5, 70, 48, 81, 78, 51, 80 };
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

static const uint8_t box_r6d16_idx[] = { 207, 8, 215, 213, 211, 214, 67, 100, 95, 196, 202, 181, 203, 188, 198, 212, 186, 214, 210, 209, 199, 193, 175 };
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

static const uint8_t box_r6d8_idx[] = { 207, 8, 10, 215, 12, 217, 211, 95, 70, 178, 147, 177, 173, 156, 172, 219, 186, 14, 209, 212, 220, 216 };
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

static const uint8_t box_r6u16_idx[] = { 82, 44, 56, 61, 49, 66, 45, 4, 46, 84, 62, 83, 67, 48, 77, 78, 51, 85, 70, 72, 86, 87 };
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

static const uint8_t box_r6u8_idx[] = { 56, 45, 46, 44, 5, 49, 66, 4, 61, 47, 62, 51, 64, 43, 6, 34, 65, 67, 48, 68, 50, 63 };
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
    0x00, // flip_around: none flipped
    box_r8_idx, // char_idx
    box_r8_chars // box_chars
};

static const uint8_t box_r8d1_idx[] = { 0, 8, 1, 10, 217, 4, 9, 5, 11, 223, 133, 134, 135, 136, 137, 138, 139, 140, 2, 12, 3, 14, 6, 13, 51, 15, 222 };
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

static const uint8_t box_r8d2_idx[] = { 0, 8, 223, 10, 217, 9, 11, 142, 143, 144, 145, 2, 12, 222, 14, 6, 13, 15 };
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

static const uint8_t box_r8d3_idx[] = { 9, 8, 11, 10, 223, 0, 217, 221, 45, 149, 150, 151, 147, 148, 155, 156, 152, 153, 154, 13, 12, 222, 14, 15, 219, 6 };
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

static const uint8_t box_r8d4_idx[] = { 9, 8, 11, 10, 148, 154, 13, 12, 222, 14 };
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

static const uint8_t box_r8d5_idx[] = { 8, 217, 215, 11, 10, 9, 221, 207, 223, 148, 151, 147, 150, 149, 154, 153, 156, 152, 155, 13, 12, 222, 6, 186, 15, 219, 14, 220 };
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

static const uint8_t box_r8d6_idx[] = { 8, 217, 9, 221, 10, 11, 207, 166, 156, 164, 165, 147, 163, 13, 12, 222, 6, 15, 14, 186, 219 };
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

static const uint8_t box_r8d8_idx[] = { 8, 207, 70, 167, 12, 220, 15 };
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

static const uint8_t box_r8d8_alt_idx[] = { 215, 217, 221, 169, 168, 186, 219 };
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
    0x00, // flip_around: none flipped
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
    0x00, // flip_around: none flipped
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
    0x00, // flip_around: none flipped
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
    0x00, // flip_around: none flipped
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
    0x00, // flip_around: none flipped
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
    0x00, // flip_around: none flipped
    box_r8u6_idx, // char_idx
    box_r8u6_chars // box_chars
};

static const uint8_t box_r8u8_idx[] = { 56, 45, 47, 6, 48, 57 };
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

static const uint8_t box_r8u8_alt_idx[] = { 58, 44, 45, 50, 14, 59, 60 };
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

static const uint8_t box_r8_alt_idx[] = { 4, 5, 6, 7 };
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

static const uint8_t box_u8_idx[] = { 82, 44, 96, 62 };
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

static const uint8_t box_u8_alt_idx[] = { 99, 66, 78, 92 };
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

__striped const boxdef_t* const main_boxes[60] = {
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

__striped const boxdef_t* const alt_boxes[60] = {
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

