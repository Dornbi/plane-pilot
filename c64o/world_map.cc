#include "world.h"

#include "color.h"
#include "gfx.h"

// Local abbreviations to make formatting below more compact.
static const auto D__ = MAP_DOT_GROUND;
static const auto DK_ = MAP_DOT_BLACK;
static const auto DW_ = MAP_DOT_WHITE;
static const auto DC_ = MAP_DOT_CYAN;
static const auto DB_ = MAP_DOT_BLUE;
static const auto DY_ = MAP_DOT_YELLOW;

// clang-format off
const uint8_t KWorldDotColors[7] = {
    kColorBlack,
    kColorGround,
    kColorBlack,
    kColorWhite,
    kColorCyan,
    kColorBlue,
    kColorYellow,
};
// clang-format on

static const auto RWY = MAP_OBJ_RUNWAY;
static const auto FLD = MAP_OBJ_FIELD;
static const auto FLS = MAP_OBJ_FIELD_SPARSE;
static const auto FYW = MAP_OBJ_FIELD_YELLOW;
static const auto FYS = MAP_OBJ_FIELD_YELLOW_SPARSE;
static const auto PND = MAP_OBJ_POND;
static const auto LAK = MAP_OBJ_LAKE;
static const auto TWN = MAP_OBJ_TOWN;
static const auto CTY = MAP_OBJ_CITY;

const uint8_t kWorldObjX[kWorldObjDim][kWorldObjMaxVerts] = {
    {0, 8, 8, 0},       // MAP_OBJ_RUNWAY
    {0, 4, 8, 2},       // MAP_OBJ_FIELD
    {0, 4, 8, 2},       // MAP_OBJ_FIELD_SPARSE
    {0, 2, 8, 3},       // MAP_OBJ_FIELD_YELLOW
    {0, 2, 8, 3},       // MAP_OBJ_FIELD_YELLOW_DENSE
    {1, 5, 7, 6, 2},    // MAP_OBJ_POND
    {0, 2, 6, 8, 5, 2}, // MAP_OBJ_LAKE
    {0, 2, 8, 3},       // MAP_OBJ_TOWN
    {0, 2, 6, 8, 5, 2}, // MAP_OBJ_CITY
};

const uint8_t kWorldObjY[kWorldObjDim][kWorldObjMaxVerts] = {
    {4, 4, 3, 3},       // MAP_OBJ_RUNWAY
    {2, 0, 5, 8},       // MAP_OBJ_FIELD
    {2, 0, 5, 8},       // MAP_OBJ_FIELD_SPARSE
    {6, 0, 3, 8},       // MAP_OBJ_FIELD_YELLOW
    {6, 0, 3, 8},       // MAP_OBJ_FIELD_YELLOW_SPARSE
    {3, 1, 4, 6, 7},    // MAP_OBJ_POND
    {3, 0, 1, 4, 7, 8}, // MAP_OBJ_LAKE
    {6, 0, 3, 8},       // MAP_OBJ_TOWN
    {3, 0, 1, 4, 7, 8}, // MAP_OBJ_CITY
};

const uint8_t kWorldObjNumVerts[kWorldObjDim] = {4, 4, 4, 4, 4, 5, 6, 4, 6};

const uint8_t kWorldObjChars[kWorldObjDim] = {
    kGfxQuad11,           // MAP_OBJ_RUNWAY
    kGfxQuadGround,       // MAP_OBJ_FIELD
    kGfxQuadGroundSparse, // MAP_OBJ_FIELD_SPARSE
    kGfxQuad11,           // MAP_OBJ_FIELD_YELLOW
    kGfxQuad11Sparse,     // MAP_OBJ_FIELD_YELLOW_SPARSE
    kGfxQuad11Sparse,     // MAP_OBJ_POND
    kGfxQuad11,           // MAP_OBJ_LAKE
    kGfxQuadMixedSparse,  // MAP_OBJ_TOWN
    kGfxQuadMixed,        // MAP_OBJ_CITY
};

const uint8_t kWorldObjColors[kWorldObjDim] = {
    kColorBlack,  // MAP_OBJ_RUNWAY
    kColorBlack,  // MAP_OBJ_FIELD
    kColorBlack,  // MAP_OBJ_FIELD_SPARSE
    kColorYellow, // MAP_OBJ_FIELD_YELLOW
    kColorYellow, // MAP_OBJ_FIELD_YELLOW_SPARSE
    kColorBlue,   // MAP_OBJ_POND
    kColorBlue,   // MAP_OBJ_LAKE
    kColorBlack,  // MAP_OBJ_TOWN
    kColorBlack,  // MAP_OBJ_CITY
};

// Two cells per byte, the even column in the low nibble (world.h
// world_map_cell()). The rows below stay one entry per cell.
#define WORLD_MAP_PAIR(lo, hi) ((uint8_t)((lo) | ((hi) << 4)))
#define WORLD_MAP_ROW(c00, c01, c02, c03, c04, c05, c06, c07, c08, c09, c10, \
                      c11, c12, c13, c14, c15, c16, c17, c18, c19, c20, c21, \
                      c22, c23, c24, c25, c26, c27, c28, c29, c30, c31) \
    {WORLD_MAP_PAIR(c00, c01), WORLD_MAP_PAIR(c02, c03), \
     WORLD_MAP_PAIR(c04, c05), WORLD_MAP_PAIR(c06, c07), \
     WORLD_MAP_PAIR(c08, c09), WORLD_MAP_PAIR(c10, c11), \
     WORLD_MAP_PAIR(c12, c13), WORLD_MAP_PAIR(c14, c15), \
     WORLD_MAP_PAIR(c16, c17), WORLD_MAP_PAIR(c18, c19), \
     WORLD_MAP_PAIR(c20, c21), WORLD_MAP_PAIR(c22, c23), \
     WORLD_MAP_PAIR(c24, c25), WORLD_MAP_PAIR(c26, c27), \
     WORLD_MAP_PAIR(c28, c29), WORLD_MAP_PAIR(c30, c31)}

// On this map N is down, W is right, S is up and E is left.
// clang-format off
const uint8_t kWorldMap[kWorldMapHeight][kWorldMapWidth / 2] = {
    // 00       10        20        30        40        50        60        70        80        90        A0        B0        C0        D0        E0        F0
    WORLD_MAP_ROW(D__, D__, D__, DB_, PND, DB_, D__, D__, DY_, DY_, DY_, D__, D__, DK_, D__, D__, D__, D__, D__, DY_, DY_, DY_, D__, D__, D__, DC_, DC_, DC_, D__, D__, D__, D__), // 00
    WORLD_MAP_ROW(D__, D__, D__, DB_, DB_, DB_, D__, D__, DW_, D__, D__, D__, DK_, DK_, DK_, D__, D__, D__, D__, DY_, FYS, DY_, D__, D__, DC_, DB_, DB_, DB_, DC_, D__, D__, D__),
    WORLD_MAP_ROW(D__, D__, D__, D__, D__, D__, D__, D__, DW_, D__, D__, DK_, DK_, CTY, DK_, DK_, D__, D__, D__, DY_, DY_, DY_, D__, D__, DC_, DB_, LAK, DB_, DC_, D__, D__, D__), // 10
    WORLD_MAP_ROW(D__, D__, D__, D__, D__, D__, D__, D__, DW_, D__, D__, D__, DK_, DK_, DK_, D__, D__, D__, D__, D__, D__, D__, D__, D__, DC_, DB_, DB_, DB_, DC_, D__, D__, D__),
    WORLD_MAP_ROW(D__, D__, D__, D__, D__, D__, D__, D__, RWY, D__, D__, D__, D__, DK_, DK_, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, DC_, DC_, DC_, D__, D__, D__, D__), // 20
    WORLD_MAP_ROW(D__, D__, DY_, DY_, DY_, D__, D__, D__, DW_, D__, D__, D__, D__, D__, DK_, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__),
    WORLD_MAP_ROW(D__, D__, DY_, FYS, DY_, D__, D__, D__, DW_, D__, D__, D__, D__, D__, DK_, D__, D__, FLS, D__, D__, D__, D__, D__, D__, D__, D__, D__, DC_, DC_, DC_, D__, D__), // 30
    WORLD_MAP_ROW(D__, D__, DY_, DY_, DY_, D__, D__, DY_, DW_, D__, D__, D__, D__, D__, D__, DK_, D__, D__, D__, D__, D__, D__, FLD, D__, D__, D__, DC_, DB_, DB_, DB_, DC_, D__),
    WORLD_MAP_ROW(D__, D__, D__, D__, D__, D__, DY_, DY_, DY_, D__, D__, D__, FLS, D__, D__, DK_, D__, D__, D__, D__, D__, D__, D__, D__, DW_, D__, DC_, DB_, LAK, DB_, DC_, D__), // 40
    WORLD_MAP_ROW(D__, DC_, DC_, DC_, D__, DY_, DY_, FYW, DY_, DY_, D__, D__, D__, D__, D__, D__, DK_, D__, D__, D__, D__, D__, D__, D__, DW_, D__, DC_, DB_, DB_, DB_, DC_, D__),
    WORLD_MAP_ROW(DC_, DB_, DB_, DB_, DC_, D__, DY_, DY_, DY_, D__, D__, D__, D__, D__, D__, D__, DK_, D__, D__, D__, D__, D__, D__, D__, DW_, D__, D__, DC_, DC_, DC_, D__, D__), // 50
    WORLD_MAP_ROW(DC_, DB_, LAK, DB_, DC_, D__, D__, DY_, D__, D__, D__, D__, D__, D__, D__, D__, D__, DK_, D__, D__, D__, D__, DK_, D__, DW_, D__, D__, D__, D__, D__, D__, D__),
    WORLD_MAP_ROW(DC_, DB_, DB_, DB_, DC_, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, DK_, DK_, DK_, DK_, D__, DK_, RWY, D__, D__, D__, D__, D__, D__, D__), // 60
    WORLD_MAP_ROW(D__, DC_, DC_, DC_, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, FLD, D__, D__, D__, DK_, TWN, DK_, D__, D__, D__, DW_, DK_, DK_, D__, DK_, DK_, DK_, D__),
    WORLD_MAP_ROW(D__, D__, D__, D__, D__, D__, D__, D__, DY_, DY_, DY_, D__, D__, D__, D__, D__, D__, D__, DK_, DK_, DK_, D__, D__, D__, DW_, D__, D__, DK_, DK_, TWN, DK_, D__), // 70
    WORLD_MAP_ROW(D__, D__, D__, DB_, DB_, DB_, D__, D__, DY_, FYS, DY_, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, DW_, D__, D__, D__, DK_, DK_, DK_, D__),
};
