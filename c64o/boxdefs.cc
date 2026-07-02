#include "boxdefs.h"
#include "chardefs.h"

#include <stddef.h>
#include <string.h>

#include "roll.h"

static const uint8_t *box_d8_addr[] = { chardefs[258], chardefs[214], chardefs[257], chardefs[244] };
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
    box_d8_addr,
    box_d8_chars // box_chars
};

static const uint8_t *box_d8_alt_addr[] = { chardefs[255], chardefs[224], chardefs[263], chardefs[251], chardefs[232] };
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
    box_d8_alt_addr,
    box_d8_alt_chars // box_chars
};

static const uint8_t *box_l10d16_addr[] = { chardefs[217], chardefs[214], chardefs[224], chardefs[233], chardefs[228], chardefs[216], chardefs[213], chardefs[236], chardefs[218], chardefs[243], chardefs[187], chardefs[244], chardefs[245], chardefs[246], chardefs[158], chardefs[188], chardefs[232], chardefs[238], chardefs[227], chardefs[242], chardefs[247], chardefs[237], chardefs[197], chardefs[239], chardefs[240], chardefs[231], chardefs[241] };
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
    box_l10d16_addr,
    box_l10d16_chars // box_chars
};

static const uint8_t *box_l10u16_addr[] = { chardefs[113], chardefs[109], chardefs[110], chardefs[99], chardefs[111], chardefs[128], chardefs[63], chardefs[126], chardefs[122], chardefs[95], chardefs[103], chardefs[102], chardefs[118], chardefs[127], chardefs[123], chardefs[117], chardefs[124], chardefs[98], chardefs[104], chardefs[129], chardefs[110], chardefs[121], chardefs[64], chardefs[125], chardefs[112], chardefs[114], chardefs[120], chardefs[119] };
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
    box_l10u16_addr,
    box_l10u16_chars // box_chars
};

static const uint8_t *box_l16d1_addr[] = { chardefs[161], chardefs[169], chardefs[179], chardefs[160], chardefs[159], chardefs[153], chardefs[163], chardefs[128], chardefs[181], chardefs[180], chardefs[186], chardefs[185], chardefs[184], chardefs[183], chardefs[182], chardefs[158], chardefs[157], chardefs[187], chardefs[154], chardefs[168], chardefs[188], chardefs[123], chardefs[170], chardefs[155], chardefs[162] };
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
    box_l16d1_addr,
    box_l16d1_chars // box_chars
};

static const uint8_t *box_l16u1_addr[] = { chardefs[169], chardefs[161], chardefs[153], chardefs[159], chardefs[160], chardefs[179], chardefs[126], chardefs[128], chardefs[163], chardefs[182], chardefs[183], chardefs[184], chardefs[185], chardefs[186], chardefs[180], chardefs[181], chardefs[157], chardefs[158], chardefs[123], chardefs[156], chardefs[168], chardefs[154], chardefs[162], chardefs[155], chardefs[170] };
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
    box_l16u1_addr,
    box_l16u1_chars // box_chars
};

static const uint8_t *box_l2d16_addr[] = { chardefs[258], chardefs[265], chardefs[228], chardefs[233], chardefs[224], chardefs[255], chardefs[217], chardefs[236], chardefs[257], chardefs[244], chardefs[266], chardefs[267], chardefs[253], chardefs[232], chardefs[263], chardefs[251], chardefs[264], chardefs[231] };
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
    box_l2d16_addr,
    box_l2d16_chars // box_chars
};

static const uint8_t *box_l2d8_addr[] = { chardefs[258], chardefs[214], chardefs[233], chardefs[224], chardefs[255], chardefs[236], chardefs[259], chardefs[257], chardefs[244], chardefs[260], chardefs[253], chardefs[232], chardefs[261], chardefs[251], chardefs[262], chardefs[231] };
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
    box_l2d8_addr,
    box_l2d8_chars // box_chars
};

static const uint8_t *box_l2u16_addr[] = { chardefs[84], chardefs[46], chardefs[95], chardefs[68], chardefs[101], chardefs[63], chardefs[99], chardefs[97], chardefs[64], chardefs[96], chardefs[86], chardefs[102], chardefs[94], chardefs[80], chardefs[103], chardefs[100], chardefs[98], chardefs[104] };
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
    box_l2u16_addr,
    box_l2u16_chars // box_chars
};

static const uint8_t *box_l2u8_addr[] = { chardefs[95], chardefs[46], chardefs[101], chardefs[68], chardefs[99], chardefs[63], chardefs[84], chardefs[86], chardefs[64], chardefs[102], chardefs[106], chardefs[103], chardefs[107], chardefs[98], chardefs[104], chardefs[108], chardefs[105] };
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
    box_l2u8_addr,
    box_l2u8_chars // box_chars
};

static const uint8_t *box_l4d8_addr[] = { chardefs[236], chardefs[214], chardefs[224], chardefs[218], chardefs[228], chardefs[248], chardefs[187], chardefs[244], chardefs[249], chardefs[188], chardefs[232] };
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
    box_l4d8_addr,
    box_l4d8_chars // box_chars
};

static const uint8_t *box_l4u8_addr[] = { chardefs[95], chardefs[109], chardefs[110], chardefs[99], chardefs[111], chardefs[117], chardefs[102], chardefs[120], chardefs[98], chardefs[64], chardefs[119] };
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
    box_l4u8_addr,
    box_l4u8_chars // box_chars
};

static const uint8_t *box_l6d16_addr[] = { chardefs[236], chardefs[214], chardefs[224], chardefs[233], chardefs[228], chardefs[217], chardefs[258], chardefs[218], chardefs[255], chardefs[256], chardefs[257], chardefs[244], chardefs[250], chardefs[251], chardefs[232], chardefs[246], chardefs[231], chardefs[252], chardefs[242], chardefs[239], chardefs[187], chardefs[253], chardefs[254] };
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
    box_l6d16_addr,
    box_l6d16_chars // box_chars
};

static const uint8_t *box_l6d8_addr[] = { chardefs[217], chardefs[218], chardefs[228], chardefs[233], chardefs[214], chardefs[216], chardefs[224], chardefs[236], chardefs[229], chardefs[187], chardefs[202], chardefs[230], chardefs[212], chardefs[231], chardefs[188], chardefs[232], chardefs[203], chardefs[234], chardefs[215], chardefs[170], chardefs[235], chardefs[227] };
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
    box_l6d8_addr,
    box_l6d8_chars // box_chars
};

static const uint8_t *box_l6u16_addr[] = { chardefs[95], chardefs[109], chardefs[110], chardefs[101], chardefs[63], chardefs[84], chardefs[99], chardefs[111], chardefs[113], chardefs[117], chardefs[102], chardefs[107], chardefs[103], chardefs[104], chardefs[118], chardefs[114], chardefs[64], chardefs[106], chardefs[98], chardefs[112], chardefs[115], chardefs[116] };
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
    box_l6u16_addr,
    box_l6u16_chars // box_chars
};

static const uint8_t *box_l6u8_addr[] = { chardefs[113], chardefs[126], chardefs[99], chardefs[128], chardefs[122], chardefs[111], chardefs[95], chardefs[109], chardefs[63], chardefs[110], chardefs[131], chardefs[104], chardefs[132], chardefs[133], chardefs[114], chardefs[117], chardefs[102], chardefs[134], chardefs[135], chardefs[123], chardefs[64], chardefs[136], chardefs[98], chardefs[130] };
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
    box_l6u8_addr,
    box_l6u8_chars // box_chars
};

static const uint8_t *box_l8_addr[] = { chardefs[179], chardefs[163], chardefs[154], chardefs[170] };
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
    box_l8_addr,
    box_l8_chars // box_chars
};

static const uint8_t *box_l8d1_addr[] = { chardefs[161], chardefs[169], chardefs[179], chardefs[160], chardefs[159], chardefs[153], chardefs[163], chardefs[128], chardefs[189], chardefs[190], chardefs[191], chardefs[192], chardefs[193], chardefs[194], chardefs[195], chardefs[196], chardefs[158], chardefs[157], chardefs[187], chardefs[154], chardefs[168], chardefs[188], chardefs[170], chardefs[197], chardefs[162] };
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
    box_l8d1_addr,
    box_l8d1_chars // box_chars
};

static const uint8_t *box_l8d2_addr[] = { chardefs[161], chardefs[179], chardefs[159], chardefs[163], chardefs[128], chardefs[198], chardefs[199], chardefs[200], chardefs[201], chardefs[158], chardefs[187], chardefs[168], chardefs[188], chardefs[202], chardefs[170], chardefs[197] };
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
    box_l8d2_addr,
    box_l8d2_chars // box_chars
};

static const uint8_t *box_l8d3_addr[] = { chardefs[161], chardefs[169], chardefs[179], chardefs[213], chardefs[214], chardefs[163], chardefs[128], chardefs[205], chardefs[208], chardefs[209], chardefs[210], chardefs[203], chardefs[204], chardefs[211], chardefs[212], chardefs[158], chardefs[187], chardefs[206], chardefs[207], chardefs[168], chardefs[188], chardefs[170], chardefs[157], chardefs[202], chardefs[162], chardefs[197] };
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
    box_l8d3_addr,
    box_l8d3_chars // box_chars
};

static const uint8_t *box_l8d4_addr[] = { chardefs[169], chardefs[160], chardefs[213], chardefs[163], chardefs[204], chardefs[210], chardefs[158], chardefs[187], chardefs[188], chardefs[202], chardefs[197] };
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
    box_l8d4_addr,
    box_l8d4_chars // box_chars
};

static const uint8_t *box_l8d5_addr[] = { chardefs[217], chardefs[179], chardefs[213], chardefs[161], chardefs[218], chardefs[214], chardefs[128], chardefs[163], chardefs[216], chardefs[204], chardefs[208], chardefs[211], chardefs[209], chardefs[212], chardefs[158], chardefs[187], chardefs[205], chardefs[210], chardefs[215], chardefs[202], chardefs[203], chardefs[206], chardefs[188], chardefs[170], chardefs[207], chardefs[197] };
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
    box_l8d5_addr,
    box_l8d5_chars // box_chars
};

static const uint8_t *box_l8d6_addr[] = { chardefs[217], chardefs[218], chardefs[216], chardefs[213], chardefs[163], chardefs[179], chardefs[214], chardefs[128], chardefs[222], chardefs[203], chardefs[219], chardefs[220], chardefs[221], chardefs[187], chardefs[212], chardefs[158], chardefs[202], chardefs[215], chardefs[170], chardefs[188], chardefs[197] };
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
    box_l8d6_addr,
    box_l8d6_chars // box_chars
};

static const uint8_t *box_l8d8_addr[] = { chardefs[216], chardefs[214], chardefs[224], chardefs[223], chardefs[187], chardefs[188], chardefs[197] };
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
    box_l8d8_addr,
    box_l8d8_chars // box_chars
};

static const uint8_t *box_l8d8_alt_addr[] = { chardefs[217], chardefs[218], chardefs[228], chardefs[225], chardefs[226], chardefs[158], chardefs[227], chardefs[202] };
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
    box_l8d8_alt_addr,
    box_l8d8_alt_chars // box_chars
};

static const uint8_t *box_l8u1_addr[] = { chardefs[169], chardefs[161], chardefs[153], chardefs[159], chardefs[160], chardefs[179], chardefs[126], chardefs[128], chardefs[163], chardefs[176], chardefs[177], chardefs[178], chardefs[171], chardefs[172], chardefs[173], chardefs[174], chardefs[157], chardefs[158], chardefs[175], chardefs[123], chardefs[156], chardefs[168], chardefs[154], chardefs[162], chardefs[155], chardefs[170] };
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
    box_l8u1_addr,
    box_l8u1_chars // box_chars
};

static const uint8_t *box_l8u2_addr[] = { chardefs[162], chardefs[159], chardefs[160], chardefs[169], chardefs[128], chardefs[163], chardefs[153], chardefs[165], chardefs[166], chardefs[167], chardefs[158], chardefs[164], chardefs[168], chardefs[157], chardefs[162], chardefs[170], chardefs[123] };
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
    box_l8u2_addr,
    box_l8u2_chars // box_chars
};

static const uint8_t *box_l8u3_addr[] = { chardefs[126], chardefs[111], chardefs[163], chardefs[153], chardefs[160], chardefs[128], chardefs[113], chardefs[159], chardefs[161], chardefs[150], chardefs[149], chardefs[132], chardefs[148], chardefs[147], chardefs[135], chardefs[154], chardefs[157], chardefs[158], chardefs[146], chardefs[145], chardefs[152], chardefs[155], chardefs[123], chardefs[117], chardefs[151], chardefs[162], chardefs[156] };
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
    box_l8u3_addr,
    box_l8u3_chars // box_chars
};

static const uint8_t *box_l8u4_addr[] = { chardefs[111], chardefs[153], chardefs[109], chardefs[128], chardefs[147], chardefs[151], chardefs[104], chardefs[102], chardefs[123], chardefs[117], chardefs[110] };
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
    box_l8u4_addr,
    box_l8u4_chars // box_chars
};

static const uint8_t *box_l8u5_addr[] = { chardefs[109], chardefs[111], chardefs[128], chardefs[122], chardefs[126], chardefs[144], chardefs[113], chardefs[149], chardefs[132], chardefs[150], chardefs[104], chardefs[151], chardefs[152], chardefs[131], chardefs[102], chardefs[145], chardefs[135], chardefs[110], chardefs[123], chardefs[117], chardefs[146], chardefs[147], chardefs[148], chardefs[114] };
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
    box_l8u5_addr,
    box_l8u5_chars // box_chars
};

static const uint8_t *box_l8u6_addr[] = { chardefs[126], chardefs[113], chardefs[109], chardefs[144], chardefs[111], chardefs[128], chardefs[122], chardefs[135], chardefs[141], chardefs[142], chardefs[102], chardefs[132], chardefs[143], chardefs[117], chardefs[104], chardefs[140], chardefs[114], chardefs[131], chardefs[110], chardefs[123] };
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
    box_l8u6_addr,
    box_l8u6_chars // box_chars
};

static const uint8_t *box_l8u8_addr[] = { chardefs[126], chardefs[122], chardefs[128], chardefs[137], chardefs[104], chardefs[131], chardefs[110] };
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
    box_l8u8_addr,
    box_l8u8_chars // box_chars
};

static const uint8_t *box_l8u8_alt_addr[] = { chardefs[111], chardefs[109], chardefs[113], chardefs[139], chardefs[138], chardefs[102], chardefs[117], chardefs[123] };
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
    box_l8u8_alt_addr,
    box_l8u8_alt_chars // box_chars
};

static const uint8_t *box_l8_alt_addr[] = { chardefs[161], chardefs[159], chardefs[128], chardefs[185], chardefs[158], chardefs[156] };
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
    box_l8_alt_addr,
    box_l8_alt_chars // box_chars
};

static const uint8_t *box_r10d16_addr[] = { chardefs[290], chardefs[279], chardefs[288], chardefs[269], chardefs[271], chardefs[265], chardefs[10], chardefs[277], chardefs[14], chardefs[272], chardefs[284], chardefs[268], chardefs[267], chardefs[283], chardefs[292], chardefs[270], chardefs[244], chardefs[273], chardefs[289], chardefs[293], chardefs[294], chardefs[14], chardefs[286], chardefs[287], chardefs[291], chardefs[280], chardefs[251], chardefs[232], chardefs[285] };
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
    box_r10d16_addr,
    box_r10d16_chars // box_chars
};

static const uint8_t *box_r10u16_addr[] = { chardefs[51], chardefs[46], chardefs[68], chardefs[58], chardefs[63], chardefs[60], chardefs[47], chardefs[7], chardefs[6], chardefs[48], chardefs[69], chardefs[53], chardefs[78], chardefs[79], chardefs[72], chardefs[64], chardefs[73], chardefs[49], chardefs[80], chardefs[81], chardefs[50], chardefs[71], chardefs[8], chardefs[74], chardefs[75], chardefs[52], chardefs[76], chardefs[77] };
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
    box_r10u16_addr,
    box_r10u16_chars // box_chars
};

static const uint8_t *box_r16d1_addr[] = { chardefs[11], chardefs[2], chardefs[10], chardefs[316], chardefs[3], chardefs[12], chardefs[288], chardefs[6], chardefs[7], chardefs[13], chardefs[15], chardefs[4], chardefs[14], chardefs[309], chardefs[5], chardefs[16], chardefs[8], chardefs[53], chardefs[17], chardefs[23], chardefs[22], chardefs[21], chardefs[9], chardefs[20], chardefs[19], chardefs[18] };
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
    box_r16d1_addr,
    box_r16d1_chars // box_chars
};

static const uint8_t *box_r16u1_addr[] = { chardefs[10], chardefs[2], chardefs[11], chardefs[6], chardefs[12], chardefs[3], chardefs[13], chardefs[7], chardefs[7], chardefs[14], chardefs[4], chardefs[15], chardefs[8], chardefs[16], chardefs[5], chardefs[17], chardefs[18], chardefs[19], chardefs[20], chardefs[9], chardefs[21], chardefs[22], chardefs[23] };
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
    box_r16u1_addr,
    box_r16u1_chars // box_chars
};

static const uint8_t *box_r2d16_addr[] = { chardefs[271], chardefs[214], chardefs[272], chardefs[224], chardefs[273], chardefs[265], chardefs[255], chardefs[233], chardefs[258], chardefs[269], chardefs[270], chardefs[244], chardefs[264], chardefs[251], chardefs[263], chardefs[232], chardefs[266], chardefs[268], chardefs[257], chardefs[267] };
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
    box_r2d16_addr,
    box_r2d16_chars // box_chars
};

static const uint8_t *box_r2d8_addr[] = { chardefs[272], chardefs[265], chardefs[10], chardefs[277], chardefs[269], chardefs[271], chardefs[279], chardefs[274], chardefs[270], chardefs[244], chardefs[275], chardefs[251], chardefs[273], chardefs[276], chardefs[268], chardefs[232], chardefs[278], chardefs[257] };
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
    box_r2d8_addr,
    box_r2d8_chars // box_chars
};

static const uint8_t *box_r2u16_addr[] = { chardefs[99], chardefs[63], chardefs[101], chardefs[68], chardefs[46], chardefs[95], chardefs[84], chardefs[98], chardefs[53], chardefs[100], chardefs[80], chardefs[94], chardefs[64], chardefs[86], chardefs[96], chardefs[97], chardefs[72], chardefs[50] };
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
    box_r2u16_addr,
    box_r2u16_chars // box_chars
};

static const uint8_t *box_r2u8_addr[] = { chardefs[46], chardefs[68], chardefs[51], chardefs[84], chardefs[47], chardefs[58], chardefs[63], chardefs[80], chardefs[53], chardefs[91], chardefs[86], chardefs[64], chardefs[92], chardefs[72], chardefs[93], chardefs[69], chardefs[90] };
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
    box_r2u8_addr,
    box_r2u8_chars // box_chars
};

static const uint8_t *box_r4d8_addr[] = { chardefs[277], chardefs[269], chardefs[271], chardefs[265], chardefs[284], chardefs[251], chardefs[232], chardefs[285], chardefs[257], chardefs[244] };
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
    box_r4d8_addr,
    box_r4d8_chars // box_chars
};

static const uint8_t *box_r4u8_addr[] = { chardefs[58], chardefs[63], chardefs[51], chardefs[46], chardefs[7], chardefs[72], chardefs[50], chardefs[83], chardefs[80], chardefs[53], chardefs[82] };
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
    box_r4u8_addr,
    box_r4u8_chars // box_chars
};

static const uint8_t *box_r6d16_addr[] = { chardefs[265], chardefs[10], chardefs[277], chardefs[279], chardefs[272], chardefs[224], chardefs[269], chardefs[271], chardefs[273], chardefs[282], chardefs[270], chardefs[244], chardefs[273], chardefs[276], chardefs[268], chardefs[267], chardefs[283], chardefs[257], chardefs[275], chardefs[251], chardefs[232], chardefs[280], chardefs[281] };
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
    box_r6d16_addr,
    box_r6d16_chars // box_chars
};

static const uint8_t *box_r6d8_addr[] = { chardefs[271], chardefs[265], chardefs[10], chardefs[290], chardefs[12], chardefs[279], chardefs[14], chardefs[288], chardefs[269], chardefs[295], chardefs[293], chardefs[244], chardefs[296], chardefs[297], chardefs[16], chardefs[267], chardefs[298], chardefs[299], chardefs[270], chardefs[300], chardefs[301], chardefs[287] };
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
    box_r6d8_addr,
    box_r6d8_chars // box_chars
};

static const uint8_t *box_r6u16_addr[] = { chardefs[84], chardefs[46], chardefs[58], chardefs[63], chardefs[51], chardefs[68], chardefs[47], chardefs[6], chardefs[48], chardefs[86], chardefs[64], chardefs[85], chardefs[69], chardefs[50], chardefs[79], chardefs[80], chardefs[53], chardefs[87], chardefs[72], chardefs[74], chardefs[88], chardefs[89] };
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
    box_r6u16_addr,
    box_r6u16_chars // box_chars
};

static const uint8_t *box_r6u8_addr[] = { chardefs[58], chardefs[47], chardefs[48], chardefs[46], chardefs[7], chardefs[51], chardefs[68], chardefs[6], chardefs[63], chardefs[49], chardefs[64], chardefs[53], chardefs[66], chardefs[45], chardefs[8], chardefs[36], chardefs[67], chardefs[69], chardefs[50], chardefs[70], chardefs[52], chardefs[65] };
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
    box_r6u8_addr,
    box_r6u8_chars // box_chars
};

static const uint8_t *box_r8_addr[] = { chardefs[2], chardefs[3], chardefs[4], chardefs[5] };
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
    box_r8_addr,
    box_r8_chars // box_chars
};

static const uint8_t *box_r8d1_addr[] = { chardefs[2], chardefs[10], chardefs[3], chardefs[12], chardefs[288], chardefs[6], chardefs[11], chardefs[7], chardefs[13], chardefs[316], chardefs[4], chardefs[14], chardefs[5], chardefs[16], chardefs[8], chardefs[15], chardefs[53], chardefs[17], chardefs[309], chardefs[329], chardefs[330], chardefs[331], chardefs[332], chardefs[325], chardefs[326], chardefs[327], chardefs[328] };
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
    box_r8d1_addr,
    box_r8d1_chars // box_chars
};

static const uint8_t *box_r8d2_addr[] = { chardefs[2], chardefs[10], chardefs[316], chardefs[12], chardefs[288], chardefs[11], chardefs[13], chardefs[4], chardefs[14], chardefs[309], chardefs[16], chardefs[8], chardefs[15], chardefs[17], chardefs[321], chardefs[322], chardefs[323], chardefs[324] };
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
    box_r8d2_addr,
    box_r8d2_chars // box_chars
};

static const uint8_t *box_r8d3_addr[] = { chardefs[11], chardefs[10], chardefs[13], chardefs[12], chardefs[316], chardefs[2], chardefs[288], chardefs[305], chardefs[15], chardefs[14], chardefs[309], chardefs[16], chardefs[320], chardefs[17], chardefs[293], chardefs[318], chardefs[317], chardefs[315], chardefs[8], chardefs[296], chardefs[314], chardefs[313], chardefs[299], chardefs[312], chardefs[311], chardefs[319] };
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
    box_r8d3_addr,
    box_r8d3_chars // box_chars
};

static const uint8_t *box_r8d4_addr[] = { chardefs[11], chardefs[10], chardefs[13], chardefs[12], chardefs[15], chardefs[14], chardefs[309], chardefs[16], chardefs[314], chardefs[319] };
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
    box_r8d4_addr,
    box_r8d4_chars // box_chars
};

static const uint8_t *box_r8d5_addr[] = { chardefs[10], chardefs[288], chardefs[279], chardefs[13], chardefs[12], chardefs[11], chardefs[305], chardefs[265], chardefs[316], chardefs[15], chardefs[14], chardefs[309], chardefs[8], chardefs[244], chardefs[17], chardefs[293], chardefs[314], chardefs[315], chardefs[16], chardefs[296], chardefs[317], chardefs[300], chardefs[318], chardefs[319], chardefs[311], chardefs[299], chardefs[312], chardefs[313] };
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
    box_r8d5_addr,
    box_r8d5_chars // box_chars
};

static const uint8_t *box_r8d6_addr[] = { chardefs[10], chardefs[288], chardefs[11], chardefs[305], chardefs[12], chardefs[13], chardefs[265], chardefs[15], chardefs[14], chardefs[309], chardefs[8], chardefs[17], chardefs[16], chardefs[244], chardefs[306], chardefs[299], chardefs[293], chardefs[307], chardefs[308], chardefs[296], chardefs[310] };
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
    box_r8d6_addr,
    box_r8d6_chars // box_chars
};

static const uint8_t *box_r8d8_addr[] = { chardefs[10], chardefs[265], chardefs[290], chardefs[14], chardefs[300], chardefs[17], chardefs[302] };
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
    box_r8d8_addr,
    box_r8d8_chars // box_chars
};

static const uint8_t *box_r8d8_alt_addr[] = { chardefs[279], chardefs[288], chardefs[305], chardefs[244], chardefs[293], chardefs[304], chardefs[303] };
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
    box_r8d8_alt_addr,
    box_r8d8_alt_chars // box_chars
};

static const uint8_t *box_r8u1_addr[] = { chardefs[10], chardefs[2], chardefs[11], chardefs[6], chardefs[12], chardefs[3], chardefs[13], chardefs[7], chardefs[14], chardefs[4], chardefs[15], chardefs[8], chardefs[16], chardefs[5], chardefs[17], chardefs[24], chardefs[25], chardefs[26], chardefs[27], chardefs[28], chardefs[29], chardefs[30], chardefs[31] };
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
    box_r8u1_addr,
    box_r8u1_chars // box_chars
};

static const uint8_t *box_r8u2_addr[] = { chardefs[2], chardefs[6], chardefs[3], chardefs[7], chardefs[14], chardefs[4], chardefs[15], chardefs[8], chardefs[16], chardefs[5], chardefs[17], chardefs[32], chardefs[33], chardefs[34], chardefs[35] };
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
    box_r8u2_addr,
    box_r8u2_chars // box_chars
};

static const uint8_t *box_r8u3_addr[] = { chardefs[2], chardefs[11], chardefs[10], chardefs[6], chardefs[12], chardefs[3], chardefs[13], chardefs[7], chardefs[14], chardefs[4], chardefs[15], chardefs[16], chardefs[5], chardefs[8], chardefs[17], chardefs[41], chardefs[42], chardefs[43], chardefs[36], chardefs[37], chardefs[44], chardefs[45], chardefs[38], chardefs[39], chardefs[40] };
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
    box_r8u3_addr,
    box_r8u3_chars // box_chars
};

static const uint8_t *box_r8u4_addr[] = { chardefs[2], chardefs[6], chardefs[46], chardefs[47], chardefs[7], chardefs[4], chardefs[8], chardefs[5], chardefs[17], chardefs[38], chardefs[44] };
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
    box_r8u4_addr,
    box_r8u4_chars // box_chars
};

static const uint8_t *box_r8u5_addr[] = { chardefs[2], chardefs[6], chardefs[51], chardefs[3], chardefs[7], chardefs[47], chardefs[48], chardefs[46], chardefs[52], chardefs[4], chardefs[8], chardefs[5], chardefs[49], chardefs[53], chardefs[50], chardefs[41], chardefs[44], chardefs[42], chardefs[45], chardefs[16], chardefs[38], chardefs[43], chardefs[36], chardefs[39], chardefs[37], chardefs[40] };
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
    box_r8u5_addr,
    box_r8u5_chars // box_chars
};

static const uint8_t *box_r8u6_addr[] = { chardefs[2], chardefs[48], chardefs[46], chardefs[51], chardefs[6], chardefs[3], chardefs[7], chardefs[47], chardefs[15], chardefs[4], chardefs[8], chardefs[50], chardefs[16], chardefs[53], chardefs[5], chardefs[36], chardefs[54], chardefs[55], chardefs[56], chardefs[57], chardefs[45] };
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
    box_r8u6_addr,
    box_r8u6_chars // box_chars
};

static const uint8_t *box_r8u8_addr[] = { chardefs[58], chardefs[47], chardefs[49], chardefs[8], chardefs[50], chardefs[59] };
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
    box_r8u8_addr,
    box_r8u8_chars // box_chars
};

static const uint8_t *box_r8u8_alt_addr[] = { chardefs[60], chardefs[46], chardefs[47], chardefs[52], chardefs[16], chardefs[61], chardefs[62] };
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
    box_r8u8_alt_addr,
    box_r8u8_alt_chars // box_chars
};

static const uint8_t *box_r8_alt_addr[] = { chardefs[6], chardefs[7], chardefs[8], chardefs[9] };
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
    box_r8_alt_addr,
    box_r8_alt_chars // box_chars
};

static const uint8_t *box_u8_addr[] = { chardefs[84], chardefs[46], chardefs[98], chardefs[64] };
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
    box_u8_addr,
    box_u8_chars // box_chars
};

static const uint8_t *box_u8_alt_addr[] = { chardefs[101], chardefs[68], chardefs[80], chardefs[94] };
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
    box_u8_alt_addr,
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

