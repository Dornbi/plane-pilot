#ifndef MSG_H
#define MSG_H

#include <stdint.h>

#include "bool.h"

static const uint16_t MSG_FOREVER = 0;
static const uint16_t MSG_DEFAULT_DURATION = 30;

void msg_clear(void);
void msg_show(const char *text, uint16_t duration = MSG_DEFAULT_DURATION,
              bool permanent = false);
void msg_update(void);
void msg_render(void);

#pragma compile("msg.cc")

#endif // MSG_H
