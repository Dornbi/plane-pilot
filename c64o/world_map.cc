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

// The 9 object types use 5 outlines, stored once each. A vertex holds the
// byte offset of its _world_dx4 / _world_dy4 entry (world.cc), so the render
// loop adds it to the table base instead of multiplying the index by 6.
#define WORLD_OBJ_V(n) ((uint8_t)((n) * sizeof(vec3_t)))

// clang-format off
const uint8_t kWorldObjX[kWorldObjVerts] = {
    WORLD_OBJ_V(0), WORLD_OBJ_V(8), WORLD_OBJ_V(8), WORLD_OBJ_V(0), //  0 runway
    WORLD_OBJ_V(0), WORLD_OBJ_V(4), WORLD_OBJ_V(8), WORLD_OBJ_V(2), //  4 field
    WORLD_OBJ_V(0), WORLD_OBJ_V(2), WORLD_OBJ_V(8), WORLD_OBJ_V(3), //  8 yellow field, town
    WORLD_OBJ_V(1), WORLD_OBJ_V(5), WORLD_OBJ_V(7), WORLD_OBJ_V(6), // 12 pond
    WORLD_OBJ_V(2),
    WORLD_OBJ_V(0), WORLD_OBJ_V(2), WORLD_OBJ_V(6), WORLD_OBJ_V(8), // 17 lake, city
    WORLD_OBJ_V(5), WORLD_OBJ_V(2),
};

const uint8_t kWorldObjY[kWorldObjVerts] = {
    WORLD_OBJ_V(4), WORLD_OBJ_V(4), WORLD_OBJ_V(3), WORLD_OBJ_V(3), //  0 runway
    WORLD_OBJ_V(2), WORLD_OBJ_V(0), WORLD_OBJ_V(5), WORLD_OBJ_V(8), //  4 field
    WORLD_OBJ_V(6), WORLD_OBJ_V(0), WORLD_OBJ_V(3), WORLD_OBJ_V(8), //  8 yellow field, town
    WORLD_OBJ_V(3), WORLD_OBJ_V(1), WORLD_OBJ_V(4), WORLD_OBJ_V(6), // 12 pond
    WORLD_OBJ_V(7),
    WORLD_OBJ_V(3), WORLD_OBJ_V(0), WORLD_OBJ_V(1), WORLD_OBJ_V(4), // 17 lake, city
    WORLD_OBJ_V(7), WORLD_OBJ_V(8),
};
// clang-format on

// First vertex of each type's outline in kWorldObjX / kWorldObjY.
const uint8_t kWorldObjFirstVert[kWorldObjDim] = {
    0,  // MAP_OBJ_RUNWAY
    4,  // MAP_OBJ_FIELD
    4,  // MAP_OBJ_FIELD_SPARSE
    8,  // MAP_OBJ_FIELD_YELLOW
    8,  // MAP_OBJ_FIELD_YELLOW_SPARSE
    12, // MAP_OBJ_POND
    17, // MAP_OBJ_LAKE
    8,  // MAP_OBJ_TOWN
    17, // MAP_OBJ_CITY
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
