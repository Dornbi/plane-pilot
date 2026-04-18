#ifndef SPRITEDEF_H
#define SPRITEDEF_H

#include <stdint.h>

static const uint16_t kSpriteDefMetaCount = 32;
static const uint16_t kSpriteDefBitmapCount = 33;

struct sprite_meta_t {
    int8_t pivot_x;
    int8_t pivot_y;
    uint8_t bitmap_idx;
};

extern const sprite_meta_t kSpriteDefMetaLongArm[kSpriteDefMetaCount];
extern const sprite_meta_t kSpriteDefMetaShortArm[kSpriteDefMetaCount];
extern const sprite_meta_t kSpriteDefSun;

#pragma compile("spritedef.cc")

#endif
