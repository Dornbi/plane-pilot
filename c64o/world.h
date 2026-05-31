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
  MAP_DOT_BLUE = 3,
  MAP_DOT_YELLOW = 4,
  MAP_OBJ_RUNWAY = 16,
};

static const uint8_t kWorldMapDim = 8;
static const uint8_t kWorldMapMask = 0x7;
extern const WorldMapType kWorldMap[kWorldMapDim][kWorldMapDim];
static const uint8_t kWorldMapObjStart = MAP_OBJ_RUNWAY;

struct world_obj_t {
  uint8_t x[4];
  uint8_t y[4];
};

static const uint8_t kWorldObjDim = 1;
extern const world_obj_t kWorldObjects[kWorldObjDim];
extern const uint8_t kWorldObjectChars[kWorldObjDim];

#pragma compile("world.cc")
#pragma compile("world_map.cc")

#endif