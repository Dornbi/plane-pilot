#include "gfx.h"
#include "world.h"

#include "color.h"

// Local abbreviations to make formatting below more compact.
static const auto ___ = MAP_NOTHING;
static const auto D__ = MAP_DOT_GROUND;
static const auto DK_ = MAP_DOT_BLACK;
static const auto DW_ = MAP_DOT_WHITE;
static const auto DC_ = MAP_DOT_CYAN;
static const auto DB_ = MAP_DOT_BLUE;
static const auto DY_ = MAP_DOT_YELLOW;

// clang-format off
const uint8_t KWorldDotColors[7] = {
    kColorBlack,
    kColorOrange,
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

const uint8_t kWorldObjX[kWorldObjDim][8] = {
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

const uint8_t kWorldObjY[kWorldObjDim][8] = {
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

// On this map N is down, W is right, S is up and E is left.
// clang-format off
const WorldMapType kWorldMap[kWorldMapHeight][kWorldMapWidth] = {
    {D__, D__, D__, DB_, PND, DB_, D__, D__, DY_, DY_, DY_, D__, D__, DK_, D__, D__, D__, D__, D__, DY_, DY_, DY_, D__, D__, D__, DC_, DC_, DC_, D__, D__, D__, D__},
    {D__, D__, D__, DB_, DB_, DB_, D__, D__, DW_, D__, D__, D__, DK_, DK_, DK_, D__, D__, D__, D__, DY_, FYS, DY_, D__, D__, DC_, DB_, DB_, DB_, DC_, D__, D__, D__},
    {D__, D__, D__, D__, D__, D__, D__, D__, DW_, D__, D__, DK_, DK_, CTY, DK_, DK_, D__, D__, D__, DY_, DY_, DY_, D__, D__, DC_, DB_, LAK, DB_, DC_, D__, D__, D__},
    {D__, D__, D__, D__, D__, D__, D__, D__, DW_, D__, D__, D__, DK_, DK_, DK_, D__, D__, D__, D__, D__, D__, D__, D__, D__, DC_, DB_, DB_, DB_, DC_, D__, D__, D__},
    {D__, D__, D__, D__, D__, D__, D__, D__, RWY, D__, D__, D__, D__, DK_, DK_, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, DC_, DC_, DC_, D__, D__, D__, D__},
    {D__, D__, DY_, DY_, DY_, D__, D__, D__, DW_, D__, D__, D__, D__, D__, DK_, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__},
    {D__, D__, DY_, FYS, DY_, D__, D__, D__, DW_, D__, D__, D__, D__, D__, DK_, D__, D__, FLS, D__, D__, D__, D__, D__, D__, D__, D__, D__, DC_, DC_, DC_, D__, D__},
    {D__, D__, DY_, DY_, DY_, D__, D__, DY_, DW_, D__, D__, D__, D__, D__, D__, DK_, D__, D__, D__, D__, D__, D__, FLD, D__, D__, D__, DC_, DB_, DB_, DB_, DC_, D__},
    {D__, D__, D__, D__, D__, D__, DY_, DY_, DY_, D__, D__, D__, FLS, D__, D__, DK_, D__, D__, D__, D__, D__, D__, D__, D__, DW_, D__, DC_, DB_, LAK, DB_, DC_, D__},
    {D__, DC_, DC_, DC_, D__, DY_, DY_, FYW, DY_, DY_, D__, D__, D__, D__, D__, D__, DK_, D__, D__, D__, D__, D__, D__, D__, DW_, D__, DC_, DB_, DB_, DB_, DC_, D__},
    {DC_, DB_, DB_, DB_, DC_, D__, DY_, DY_, DY_, D__, D__, D__, D__, D__, D__, D__, DK_, D__, D__, D__, D__, D__, D__, D__, DW_, D__, D__, DC_, DC_, DC_, D__, D__},
    {DC_, DB_, LAK, DB_, DC_, D__, D__, DY_, D__, D__, D__, D__, D__, D__, D__, D__, D__, DK_, D__, D__, D__, D__, DK_, D__, DW_, D__, D__, D__, D__, D__, D__, D__},
    {DC_, DB_, DB_, DB_, DC_, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, DK_, DK_, DK_, DK_, D__, DK_, RWY, D__, D__, D__, D__, D__, D__, D__},
    {D__, DC_, DC_, DC_, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, FLD, D__, D__, D__, DK_, TWN, DK_, D__, D__, D__, DW_, DK_, DK_, D__, DK_, DK_, DK_, D__},
    {D__, D__, D__, D__, D__, D__, D__, D__, DY_, DY_, DY_, D__, D__, D__, D__, D__, D__, D__, DK_, DK_, DK_, D__, D__, D__, DW_, D__, D__, DK_, DK_, TWN, DK_, D__},
    {D__, D__, D__, DB_, DB_, DB_, D__, D__, DY_, FYS, DY_, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, D__, DW_, D__, D__, D__, DK_, DK_, DK_, D__},
};
