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
static const auto FBK = MAP_OBJ_FIELD_BLACK;
static const auto FBS = MAP_OBJ_FIELD_BLACK_SPARSE;
static const auto FMB = MAP_OBJ_FIELD_MIXED_BLACK;
static const auto FMS = MAP_OBJ_FIELD_MIXED_BLACK_SPARSE;
static const auto FYW = MAP_OBJ_FIELD_YELLOW;
static const auto FYS = MAP_OBJ_FIELD_YELLOW_SPARSE;
static const auto PND = MAP_OBJ_POND;
static const auto LAK = MAP_OBJ_LAKE;

const uint8_t kWorldObjX[kWorldObjDim][8] = {
    {0, 8, 8, 0},       // MAP_OBJ_RUNWAY
    {0, 4, 8, 2},       // MAP_OBJ_FIELD
    {0, 4, 8, 2},       // MAP_OBJ_FIELD_SPARSE
    {0, 2, 8, 3},       // MAP_OBJ_FIELD_BLACK
    {0, 2, 8, 3},       // MAP_OBJ_FIELD_BLACK_SPARSE
    {0, 2, 8, 3},       // MAP_OBJ_FIELD_MIXED_BLACK
    {0, 2, 8, 3},       // MAP_OBJ_FIELD_MIXED_BLACK_SPARSE
    {0, 2, 8, 3},       // MAP_OBJ_FIELD_YELLOW
    {0, 2, 8, 3},       // MAP_OBJ_FIELD_YELLOW_DENSE
    {1, 5, 7, 6, 2},    // MAP_OBJ_POND
    {0, 2, 6, 8, 5, 2}, // MAP_OBJ_LAKE
};

const uint8_t kWorldObjY[kWorldObjDim][8] = {
    {4, 4, 3, 3},       // MAP_OBJ_RUNWAY
    {2, 0, 5, 8},       // MAP_OBJ_FIELD
    {2, 0, 5, 8},       // MAP_OBJ_FIELD_SPARSE
    {6, 0, 3, 8},       // MAP_OBJ_FIELD_BLACK
    {6, 0, 3, 8},       // MAP_OBJ_FIELD_BLACK_SPARSE
    {6, 0, 3, 8},       // MAP_OBJ_FIELD_MIXED_BLACK
    {6, 0, 3, 8},       // MAP_OBJ_FIELD_MIXED_BLACK_SPARSE
    {6, 0, 3, 8},       // MAP_OBJ_FIELD_YELLOW
    {6, 0, 3, 8},       // MAP_OBJ_FIELD_YELLOW_SPARSE
    {3, 1, 4, 6, 7},    // MAP_OBJ_POND
    {3, 0, 1, 4, 7, 8}, // MAP_OBJECT_LAKE
};

const uint8_t kWorldObjNumVerts[kWorldObjDim] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 6,
};

const uint8_t kWorldObjChars[kWorldObjDim] = {
    kGfxQuad11,           // MAP_OBJ_RUNWAY
    kGfxQuadGround,       // MAP_OBJ_FIELD
    kGfxQuadGroundSparse, // MAP_OBJ_FIELD_SPARSE
    kGfxQuad11,           // MAP_OBJ_FIELD_BLACK
    kGfxQuad11Sparse,     // MAP_OBJ_FIELD_BLACK_SPARSE
    kGfxQuadMixed,        // MAP_OBJ_FIELD_MIXED_BLACK
    kGfxQuadMixedSparse,  // MAP_OBJ_FIELD_MIXED_BLACK_SPARSE
    kGfxQuad11,           // MAP_OBJ_FIELD_YELLOW
    kGfxQuad11Sparse,     // MAP_OBJ_FIELD_YELLOW_SPARSE
    kGfxQuad11Sparse,     // MAP_OBJ_POND
    kGfxQuad11,           // MAP_OBJ_LAKE
};

const uint8_t kWorldObjColors[kWorldObjDim] = {
    kColorBlack,  // MAP_OBJ_RUNWAY
    kColorBlack,  // MAP_OBJ_FIELD
    kColorBlack,  // MAP_OBJ_FIELD_SPARSE
    kColorBlack,  // MAP_OBJ_FIELD_BLACK
    kColorBlack,  // MAP_OBJ_FIELD_BLACK_SPARSE
    kColorBlack,  // MAP_OBJ_FIELD_MIXED_BLACK
    kColorBlack,  // MAP_OBJ_FIELD_MIXED_BLACK_SPARSE
    kColorYellow, // MAP_OBJ_FIELD_YELLOW
    kColorYellow, // MAP_OBJ_FIELD_YELLOW_SPARSE
    kColorBlue,   // MAP_OBJ_POND
    kColorBlue,   // MAP_OBJ_LAKE
};

// The default model starting position is rotated 180 degrees compared
// to how it looks here. I.e. the plane is starting upside down on this map.
// clang-format off
const WorldMapType kWorldMap[kWorldMapDim][kWorldMapDim] = {
    {D__, D__, D__, D__, D__, DK_, D__, D__, D__, D__, DY_, D__, D__, D__, D__, D__},
    {D__, DY_, FYW, DY_, D__, D__, DK_, D__, D__, D__, D__, D__, DK_, D__, DC_, D__},
    {D__, D__, DY_, D__, D__, D__, FMB, D__, DB_, DB_, DC_, D__, D__, D__, PND, DB_},
    {D__, D__, D__, D__, DK_, D__, D__, DW_, D__, PND, D__, D__, D__, DB_, DC_, D__},
    {D__, D__, D__, DK_, D__, D__, D__, DW_, D__, D__, DB_, DC_, D__, D__, D__, D__},
    {D__, D__, D__, FBK, DK_, D__, DK_, DW_, D__, D__, D__, DB_, DB_, DB_, D__, DC_},
    {D__, DK_, DK_, D__, D__, D__, D__, D__, DB_, D__, DB_, D__, LAK, D__, DB_, D__},
    {DK_, D__, D__, DK_, D__, D__, D__, RWY, D__, DC_, DC_, DC_, D__, D__, DB_, DB_},
    {FMS, D__, DK_, D__, D__, D__, D__, D__, D__, DB_, DB_, DB_, DC_, DB_, DC_, D__},
    {D__, D__, D__, DY_, D__, D__, D__, DW_, D__, D__, DC_, DB_, D__, DC_, DB_, D__},
    {D__, D__, D__, D__, FYS, DY_, D__, DW_, D__, D__, DB_, LAK, DB_, DC_, D__, D__},
    {D__, D__, D__, D__, DY_, D__, D__, DW_, D__, D__, D__, DB_, DC_, D__, D__, D__},
    {D__, DB_, D__, D__, D__, D__, DK_, D__, D__, D__, D__, D__, DB_, D__, D__, D__},
    {DB_, LAK, DC_, D__, DY_, DY_, D__, D__, D__, FMS, D__, D__, D__, DK_, D__, D__},
    {DC_, D__, DB_, D__, D__, FYW, D__, D__, D__, D__, D__, D__, DK_, FBK, D__, D__},
    {D__, D__, D__, D__, DY_, DY_, D__, D__, D__, D__, D__, D__, D__, D__, DK_, D__},
};
