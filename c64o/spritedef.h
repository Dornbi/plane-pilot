#ifndef SPRITEDEF_H
#define SPRITEDEF_H

#include <stdint.h>

static const uint16_t kSpriteDefMetaCount = 32;
static const uint16_t kSpriteDefBitmapCount = 48;
static const uint8_t kSpriteDefCloud1Count = 5;
static const uint8_t kSpriteDefCloud2Count = 5;

struct sprite_meta_t {
    int8_t pivot_x;
    int8_t pivot_y;
    uint8_t bitmap_idx;
};

struct sprite_cloud1_meta_t {
    uint8_t width;
    uint8_t height;
    uint8_t bitmap_idx;
    int8_t pivot_x;
    int8_t pivot_y;
};

struct sprite_cloud2_meta_t {
    uint8_t width;
    uint8_t height;
    uint8_t top_bitmap_idx;
    uint8_t bot_bitmap_idx;
    int8_t pivot_x;
    int8_t pivot_y;
};

static const uint8_t kSpriteDefCloudRungCount = 10;

struct sprite_cloud_rung_t {
    uint8_t bitmap;
    uint8_t bitmap2;   // 0xFF when the rung is one sprite
    int8_t pivot_x;
    int8_t pivot_y;
};

extern const sprite_cloud_rung_t kSpriteDefCloudRung[kSpriteDefCloudRungCount];
extern const sprite_cloud1_meta_t kSpriteDefCloud1Sprite[kSpriteDefCloud1Count];
extern const sprite_cloud2_meta_t kSpriteDefCloud2Sprite[kSpriteDefCloud2Count];
extern const sprite_meta_t kSpriteDefMetaLongArm[kSpriteDefMetaCount];
extern const sprite_meta_t kSpriteDefMetaShortArm[kSpriteDefMetaCount];
extern const sprite_meta_t kSpriteDefSun;

#pragma compile("spritedef.cc")

#endif
