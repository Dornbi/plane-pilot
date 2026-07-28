#include "msg.h"

#include <stdint.h>
#include <string.h>

#include "color.h"
#include "mem.h"

static const char *msg_text = nullptr;
static uint16_t msg_duration = 0;
static bool msg_is_permanent = false;

// Placement of the message on row 0. Computed once in msg_show() rather than
// per frame, because the sprite hiding test needs it every frame and strlen()
// on a 20 character message is not something to pay for 50 times a second.
static uint8_t msg_col = 0;
static uint8_t msg_len = 0;

uint16_t msg_span_x0 = 0;
uint16_t msg_span_x1 = 0;

// Span of the color buffer that the last msg_render() overwrote with the
// hires text color. Needs to be restored to a multicolor value, because
// nothing else does: sky and box rendering rewrite the color buffer, but
// ground rendering leaves it alone (ground uses only background registers).
static uint8_t msg_color_col = 0;
static uint8_t msg_color_len = 0;

void msg_clear(void) {
  msg_text = nullptr;
  msg_duration = 0;
  msg_is_permanent = false;
  msg_col = 0;
  msg_len = 0;
  msg_span_x0 = 0;
  msg_span_x1 = 0;
}

void msg_show(const char *text, uint16_t duration, bool permanent) {
  if (text == nullptr) {
    return;
  }
  if (msg_is_permanent && !permanent && msg_text != nullptr) {
    return;
  }
  msg_text = text;
  msg_duration = duration;
  msg_is_permanent = permanent;

  uint8_t len = (uint8_t)strlen(text);
  if (len > kScreenWidth) {
    len = kScreenWidth;
  }
  msg_len = len;
  msg_col = (kScreenWidth - len) >> 1;
  msg_span_x0 = (uint16_t)msg_col * 8;
  msg_span_x1 = msg_span_x0 + (uint16_t)len * 8;
}

void msg_update(void) {
  if (!msg_is_permanent && msg_text != nullptr && msg_duration > 0) {
    --msg_duration;
    if (msg_duration == 0) {
      msg_text = nullptr;
    }
  }
}

void msg_restore_color(void) {
  if (msg_color_len != 0) {
    memset(mem_color_buffer + msg_color_col, kColorSky | 0x08, msg_color_len);
    msg_color_len = 0;
  }
}

bool msg_active(void) { return msg_text != nullptr && msg_len != 0; }

void msg_render(void) {
  if (msg_text == nullptr || msg_len == 0) {
    return;
  }

  memcpy(mem_screen_row_ptrs[0] + msg_col, msg_text, msg_len);
  memset(mem_color_buffer + msg_col, kColorBlack, msg_len);
  msg_color_col = msg_col;
  msg_color_len = msg_len;
}
