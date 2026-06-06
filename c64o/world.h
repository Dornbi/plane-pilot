#ifndef WORLD_H
#define WORLD_H

#include "gfx.h"
#include "vec.h"
#include <stdint.h>

// Roughly 24.8 fixed point in meters
extern int32_t world_eye_x;
extern int32_t world_eye_y;
extern int32_t world_eye_z;
// View matrix.
extern mat3_t world_cam;

// Renders the world gird and objects.
void world_render_grid();

enum WorldMapType {
  MAP_NOTHING = 0,
  MAP_DOT_GROUND = 1,
  MAP_DOT_BLACK = 2,
  MAP_DOT_WHITE = 3,
  MAP_DOT_CYAN = 4,
  MAP_DOT_BLUE = 5,
  MAP_DOT_YELLOW = 6,
  // Polygon objects
  MAP_OBJ_RUNWAY = 16,
  MAP_OBJ_FIELD = 17,
  MAP_OBJ_FIELD_SPARSE = 18,
  MAP_OBJ_FIELD_BLACK = 19,
  MAP_OBJ_FIELD_BLACK_SPARSE = 20,
  MAP_OBJ_FIELD_MIXED_BLACK = 21,
  MAP_OBJ_FIELD_MIXED_BLACK_SPARSE = 22,
  MAP_OBJ_FIELD_YELLOW = 23,
  MAP_OBJ_FIELD_YELLOW_SPARSE = 24,
  MAP_OBJ_POND = 25,
  MAP_OBJ_LAKE = 26,
};

extern const uint8_t KWorldDotColors[7];

static const uint8_t kWorldMapDim = 16;
static const uint8_t kWorldMapMask = 0xF;
extern const WorldMapType kWorldMap[kWorldMapDim][kWorldMapDim];
static const uint8_t kWorldMapObjStart = MAP_OBJ_RUNWAY;

static const uint8_t kWorldObjDim = 11;
// kPolyMaxVertices is 6 but we do 8 for easier math.
extern const uint8_t kWorldObjX[kWorldObjDim][8];
extern const uint8_t kWorldObjY[kWorldObjDim][8];
extern const uint8_t kWorldObjNumVerts[kWorldObjDim];
extern const uint8_t kWorldObjChars[kWorldObjDim];
extern const uint8_t kWorldObjColors[kWorldObjDim];

#pragma compile("world.cc")
#pragma compile("world_map.cc")

#endif