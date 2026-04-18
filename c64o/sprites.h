#ifndef SPRITES_H
#define SPRITES_H

#include "bool.h"
#include <stdint.h>

extern const char kSpriteDataCompressed[];

void sprites_init(void);

void sprites_show_sun_only();
void sprites_show_all();

void sprites_set_speed(uint8_t speed);
void sprites_set_alt(uint16_t alt);
void sprites_set_vspeed(int16_t vspeed);
void sprites_set_roll(uint8_t roll_angle);
void sprites_set_throttle(uint8_t throttle);
void sprites_set_fuel(uint32_t fuel);
void sprites_set_heading_bitmap(uint8_t heading);
void sprites_set_sun(int16_t x, int16_t y);
void sprites_update_sun_hw();
void sprites_hide_sun_hw();

static const uint8_t kHeadingMax = 48;

#pragma compile("sprites.cc")

#endif