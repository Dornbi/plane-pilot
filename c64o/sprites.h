#ifndef SPRITES_H
#define SPRITES_H

#include "bool.h"
#include <stdint.h>

extern const char kSpriteDataCompressed[];

void sprites_init(void);

// Update instrument panel sprites.
void sprites_set_speed(uint8_t speed);
void sprites_set_alt(uint16_t alt);
void sprites_set_vspeed(int16_t vspeed);
void sprites_set_roll(uint8_t roll_angle);
void sprites_set_pitch(int8_t pitch_angle);
void sprites_set_throttle(uint8_t throttle);
void sprites_set_fuel(uint32_t fuel);

void sprites_show_terrain_sprites();
void sprites_show_no_sprites();
void sprites_show_panel_top_sprites();
void sprites_show_panel_bottom_sprites();
void sprites_set_sun_position(int16_t x, int16_t y);

static const uint8_t kHeadingMax = 48;
void sprites_set_heading_bitmap(uint8_t heading);

#pragma compile("sprites.cc")

#endif