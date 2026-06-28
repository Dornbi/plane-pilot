#ifndef MAP_H
#define MAP_H

#include "bool.h"

extern bool map_mode;

void map_enter();
void map_exit();

#pragma compile("map.cc")

#endif // MAP_H
