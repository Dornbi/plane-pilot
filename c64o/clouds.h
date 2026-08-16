#ifndef CLOUDS_H
#define CLOUDS_H

#include <stdint.h>

// Offers every visible cloud to the sprite stack.
//
// Call between sprites_stack_reset() and sprites_stack_commit(); world.cc's
// world_update_objects() is the one caller. Positions are procedural - a hash
// over the cloud cell coordinate, with no per-cloud storage anywhere and no
// bound on how many clouds the world contains. See docs/clouds.md §2.
void clouds_add_candidates(void);

#pragma compile("clouds.cc")

#endif
