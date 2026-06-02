#include "world.h"

#include "color.h"

const world_obj_t kWorldObjects[kWorldObjDim] = {
    // MAP_OBJECT_RUNWAY
    {{0, 8, 8, 0}, {4, 4, 3, 3}}};

const uint8_t kWorldObjectChars[kWorldObjDim] = {
    // MAP_OBJECT_RUNWAY
    kGfxQuadGround};

const uint8_t kWorldObjectColors[kWorldObjDim] = {
    // MAP_OBJECT_RUNWAY
    kColorBlack};

// Local abbreviations to make formatting below more compact.
static const auto ___ = MAP_NOTHING;
static const auto D__ = MAP_DOT_GROUND;
static const auto DK_ = MAP_DOT_BLACK;
static const auto DB_ = MAP_DOT_BLUE;
static const auto DY_ = MAP_DOT_YELLOW;
static const auto ORW = MAP_OBJ_RUNWAY;

// clang-format off
const WorldMapType kWorldMap[kWorldMapDim][kWorldMapDim] = {
    {D__, D__, D__, D__, D__, D__, D__, D__},
    {D__, D__, D__, D__, D__, DY_, D__, D__},
    {D__, D__, D__, DK_, D__, D__, D__, D__},
    {D__, D__, D__, ORW, D__, D__, D__, D__},
    {D__, D__, D__, DK_, D__, D__, D__, D__},
    {D__, D__, D__, D__, D__, DY_, D__, D__},
    {D__, D__, D__, D__, DY_, D__, DY_, D__},
    {D__, D__, D__, D__, D__, D__, D__, D__},
};
