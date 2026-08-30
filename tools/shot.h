#ifndef SHOT_H
#define SHOT_H

// Scripted keyboard input for the screenshot capture. See tools/shot.cc.

#include <stdint.h>

#include "keys.h"

void shot_poll(void);

#pragma compile("shot.cc")

#endif
