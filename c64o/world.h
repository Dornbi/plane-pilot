#ifndef WORLD_H
#define WORLD_H

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

enum WorldObjectType { WORLD_OBJECT_NOTHING = 0, WORLD_OBJECT_RUNWAY = 1 };
static const uint8_t kWorldObjectNumRows = 5;

#pragma compile("world.cc")

#endif