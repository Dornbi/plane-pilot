#ifndef WORLD_H
#define WORLD_H

#include "gfx.h"
#include "vec.h"
#include <stdint.h>

// Roughly 24.8 fixed point in meters
extern int32_t model_eye_x;
extern int32_t model_eye_y;
extern int32_t model_eye_z;
// View matrix.
extern mat3_t world_cam;

// Renders the world gird and objects.
void world_render_grid();

// Updates the roll state.
void world_update_roll_state();

// Updates the sun position.
void world_update_sun_pos();

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
  MAP_OBJ_FIELD_YELLOW = 19,
  MAP_OBJ_FIELD_YELLOW_SPARSE = 20,
  MAP_OBJ_POND = 21,
  MAP_OBJ_LAKE = 22,
  MAP_OBJ_TOWN = 23,
  MAP_OBJ_CITY = 24,
};

extern const uint8_t KWorldDotColors[7];

static const uint8_t kWorldMapWidth = 32;
static const uint8_t kWorldMapWidthMask = 0x1F;
static const uint8_t kWorldMapHeight = 16;
static const uint8_t kWorldMapHeightMask = 0xF;
extern const WorldMapType kWorldMap[kWorldMapHeight][kWorldMapWidth];
static const uint8_t kWorldMapObjStart = MAP_OBJ_RUNWAY;

static const uint8_t kWorldObjDim = 9;
// Matches kPolyMaxVertices in poly.h.
static const uint8_t kWorldObjMaxVerts = 6;
extern const uint8_t kWorldObjX[kWorldObjDim][kWorldObjMaxVerts];
extern const uint8_t kWorldObjY[kWorldObjDim][kWorldObjMaxVerts];
extern const uint8_t kWorldObjNumVerts[kWorldObjDim];
extern const uint8_t kWorldObjChars[kWorldObjDim];
extern const uint8_t kWorldObjColors[kWorldObjDim];

#pragma compile("world.cc")
#pragma compile("world_map.cc")

#endif