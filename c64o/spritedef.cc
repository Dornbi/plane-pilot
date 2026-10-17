#include "spritedef.h"

const sprite_cloud1_meta_t kSpriteDefCloud1Sprite[kSpriteDefCloud1Count] = {
    { 5, 9, 81, 12, 10 }, // Cloud 1-Sprite 5x9
    { 7, 13, 82, 12, 10 }, // Cloud 1-Sprite 7x13
    { 9, 17, 83, 12, 10 }, // Cloud 1-Sprite 9x17
    { 11, 21, 84, 12, 10 }, // Cloud 1-Sprite 11x21
};

const sprite_cloud2_meta_t kSpriteDefCloud2Sprite[kSpriteDefCloud2Count] = {
    { 13, 25, 85, 86, 12, 20 }, // Cloud 2-Sprite 13x25
    { 15, 29, 87, 88, 12, 20 }, // Cloud 2-Sprite 15x29
    { 17, 33, 89, 90, 12, 20 }, // Cloud 2-Sprite 17x33
    { 19, 37, 91, 92, 12, 20 }, // Cloud 2-Sprite 19x37
    { 21, 41, 93, 94, 12, 20 }, // Cloud 2-Sprite 21x41
};

const sprite_cloud_rung_t kSpriteDefCloudRung[kSpriteDefCloudRungCount] = {
    { 81, 0xFF, 12, 10 }, // rung 0: never selected; its block is the orientation indicator
    { 81, 0xFF, 12, 10 }, // Cloud 1-Sprite 5x9
    { 82, 0xFF, 12, 10 }, // Cloud 1-Sprite 7x13
    { 83, 0xFF, 12, 10 }, // Cloud 1-Sprite 9x17
    { 84, 0xFF, 12, 10 }, // Cloud 1-Sprite 11x21
    { 85, 86, 12, 20 }, // Cloud 2-Sprite 13x25
    { 87, 88, 12, 20 }, // Cloud 2-Sprite 15x29
    { 89, 90, 12, 20 }, // Cloud 2-Sprite 17x33
    { 91, 92, 12, 20 }, // Cloud 2-Sprite 19x37
    { 93, 94, 12, 20 }, // Cloud 2-Sprite 21x41
};

const sprite_arm_t kSpriteDefLongArm[kSpriteDefArmCount] = {
    { 12, 17 }, // 0.0 deg
    { 11, 17 }, // 11.25 deg
    { 9, 16 }, // 22.5 deg
    { 8, 16 }, // 33.75 deg
    { 7, 15 }, // 45.0 deg
    { 6, 14 }, // 56.25 deg
    { 6, 13 }, // 67.5 deg
    { 5, 11 }, // 78.75 deg
    { 5, 10 }, // 90.0 deg
    { 5, 9 }, // 101.25 deg
    { 6, 7 }, // 112.5 deg
    { 6, 6 }, // 123.75 deg
    { 7, 5 }, // 135.0 deg
    { 8, 4 }, // 146.25 deg
    { 9, 4 }, // 157.5 deg
    { 11, 3 }, // 168.75 deg
};

const sprite_arm_t kSpriteDefShortArm[kSpriteDefArmCount] = {
    { 12, 15 }, // 0.0 deg
    { 11, 15 }, // 11.25 deg
    { 10, 15 }, // 22.5 deg
    { 9, 14 }, // 33.75 deg
    { 8, 14 }, // 45.0 deg
    { 8, 13 }, // 56.25 deg
    { 7, 12 }, // 67.5 deg
    { 7, 11 }, // 78.75 deg
    { 7, 10 }, // 90.0 deg
    { 7, 9 }, // 101.25 deg
    { 7, 8 }, // 112.5 deg
    { 8, 7 }, // 123.75 deg
    { 8, 6 }, // 135.0 deg
    { 9, 6 }, // 146.25 deg
    { 10, 5 }, // 157.5 deg
    { 11, 5 }, // 168.75 deg
};

const sprite_meta_t kSpriteDefSun = { 12, 10, 95 };
const sprite_meta_t kSpriteDefOrient = { 12, 10, 80 };
