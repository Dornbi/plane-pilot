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

// Restores the color buffer span painted by the previous msg_render().
// Must be called before the sky/ground fill of the next frame, since ground
// rows never rewrite the color buffer and would keep the hires text color.
void msg_restore_color(void);

void msg_render(void);

#pragma compile("msg.cc")

#endif // MSG_H
