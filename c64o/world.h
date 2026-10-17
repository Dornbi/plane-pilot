#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>

#include "gfx.h"
#include "vec.h"

// Roughly 24.8 fixed point in meters
extern int32_t flight_eye_x;
extern int32_t flight_eye_y;
extern int32_t flight_eye_z;
// View matrix.
extern mat3_t world_cam;

// Renders the world gird and objects.
void world_render_grid();

// Updates the roll state.
void world_update_roll_state();

// Rebuilds the viewport sprite frame: resets the sprite stack, offers every
// world object to it, and commits. See sprites.h and docs/clouds.md §1.
void world_update_objects();

enum WorldMapType {
  MAP_NOTHING = 0,
  MAP_DOT_GROUND = 1,
  MAP_DOT_BLACK = 2,
  MAP_DOT_WHITE = 3,
  MAP_DOT_CYAN = 4,
  MAP_DOT_BLUE = 5,
  MAP_DOT_YELLOW = 6,
  // Polygon objects. They follow the dots directly so that every type fits
  // the nibble kWorldMap stores it in.
  MAP_OBJ_RUNWAY = 7,
  MAP_OBJ_FIELD = 8,
  MAP_OBJ_FIELD_SPARSE = 9,
  MAP_OBJ_FIELD_YELLOW = 10,
  MAP_OBJ_FIELD_YELLOW_SPARSE = 11,
  MAP_OBJ_POND = 12,
  MAP_OBJ_LAKE = 13,
  MAP_OBJ_TOWN = 14,
  MAP_OBJ_CITY = 15,
};

extern const uint8_t KWorldDotColors[7];

static const uint8_t kWorldMapWidth = 32;
static const uint8_t kWorldMapWidthMask = 0x1F;
static const uint8_t kWorldMapHeight = 16;
static const uint8_t kWorldMapHeightMask = 0xF;
// Two cells per byte, the even column in the low nibble. Read it through
// world_map_row() and world_map_cell().
extern const uint8_t kWorldMap[kWorldMapHeight][kWorldMapWidth / 2];
static const uint8_t kWorldMapObjStart = MAP_OBJ_RUNWAY;

// Rows are 16 B, so shift rather than index: kWorldMap[row] links oscar64's
// __multab tables.
static inline const uint8_t *world_map_row(uint8_t row) {
  return kWorldMap[0] + ((uint16_t)row << 4);
}

static inline WorldMapType world_map_cell(const uint8_t *map_row,
                                          uint8_t col) {
  uint8_t pair = map_row[col >> 1];
  return (WorldMapType)((col & 1) ? pair >> 4 : pair & 0x0F);
}

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