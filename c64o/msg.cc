#include "msg.h"

#include <stdint.h>
#include <string.h>

#include "color.h"
#include "mem.h"

static const char *msg_text = nullptr;
static uint16_t msg_duration = 0;
static bool msg_is_permanent = false;

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

void msg_render(void) {
  if (msg_text == nullptr) {
    return;
  }
  uint8_t len = (uint8_t)strlen(msg_text);
  if (len == 0) {
    return;
  }
  if (len > kScreenWidth) {
    len = kScreenWidth;
  }
  uint8_t col = (kScreenWidth - len) >> 1;

  memcpy(mem_screen_row_ptrs[0] + col, msg_text, len);
  memset(mem_color_buffer + col, kColorBlack, len);
  msg_color_col = col;
  msg_color_len = len;
}
