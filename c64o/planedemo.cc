// planedemo -- the traffic-sprite renderer of docs/planes.md on its own.
//
// Default character mode, the screen and the ROM font where the C64 starts
// them, and one aircraft drawn with up to four hardware sprites by planes.cc.
// Nothing of ppilot is here but the renderer and the vector maths it rides on:
// no terrain, no panel, no flight model. The camera looks straight ahead and
// the aircraft sits in front of it; the keys fly the aircraft, not the camera.
//
//   + -        closer, farther
//   A D        yaw         W S   pitch        Q E   roll
//   SPACE      spin on/off
//   M          approach and recede, over and over
//   1 - 6      head-on, three-quarter, side, banked, top, climbing away
//   R          reset
//
// The bottom lines show what the renderer chose and what it cost: the
// distance, the pixel size and layout, whether the vertex cache hit, and the
// cycles planes_render() took, measured on CIA 2's timers.
//
// Memory. The VIC looks at bank 2, $8000-$BFFF, rather than bank 0: the ROM
// font shows through at $9000 there exactly as it does at $1000 in bank 0, so
// the screen looks the same, and $8400-$8BFF is out of the program's way. The
// screen is at $8400 and the sprite blocks follow at $8800 -- two sets of four,
// front and back, and the dot.

// Keep the program, its data, heap and stack below the VIC's bank.
#pragma region(main, 0x0880, 0x8000, , , {code, data, bss, heap, stack})

#include <stdint.h>
#include <string.h>

#include "benchmark.h"
#include "cia.h"
#include "keys.h"
#include "planes.h"
#include "print.h"
#include "vec.h"
#include "vic.h"

static uint8_t *const kScreen = (uint8_t *)0x8400;
static uint8_t *const kSpriteBase = (uint8_t *)0x8800;
static uint8_t *const kColorRam = (uint8_t *)0xD800;
// Sprite block numbers, counted from the start of the VIC's bank.
static const uint8_t kBlockSets = 0x0800 / 64;   // sets 0 and 1: 32-35, 36-39
static const uint8_t kBlockDot = kBlockSets + 8; // 40

// print.cc and benchmark.cc find the screen through these.
uint8_t *mem_screen_ram;
__striped uint8_t *mem_screen_row_ptrs[25];
bool mem_debug_enabled = true;

// The whole screen is the viewport: the aircraft is centred on it, and there
// is no panel under it, so the DMA cuts are below the bottom and nothing
// slides.
static const planes_view_t kView = {160, 100, 250, 250};

// Where it starts. Build with -DDEMO_DISTANCE=40 -DDEMO_PRESET=3, say, to
// screenshot one case without a keyboard (tools/vice_shot.sh cannot type);
// -DDEMO_SPIN=1 to time frames whose attitude changes, -DDEMO_APPROACH=1 ones
// whose distance does.
#ifndef DEMO_DISTANCE
#define DEMO_DISTANCE 150
#endif
#ifndef DEMO_PRESET
#define DEMO_PRESET 1
#endif
#ifndef DEMO_SPIN
#define DEMO_SPIN 0
#endif
#ifndef DEMO_APPROACH
#define DEMO_APPROACH 0
#endif

static const uint8_t kColorPlane = 15;   // light grey
static const uint8_t kColorText = 14;    // light blue

// Presets, from lib/planes.py: q88(orient(heading, pitch, bank)).
static const mat3_t kPresets[6] = {
    // head-on: heading 180
    {{-256, 0, 0}, {0, -256, 0}, {0, 0, 256}},
    // three-quarter: heading 145
    {{-210, 147, 0}, {-147, -210, 0}, {0, 0, 256}},
    // side: heading 90
    {{0, 256, 0}, {-256, 0, 0}, {0, 0, 256}},
    // banked: heading 110, bank 45
    {{-88, 241, 0}, {-170, -62, 181}, {170, 62, 181}},
    // top: heading 90, bank 70 -- the planform turned to face us
    {{0, 256, 0}, {-88, 0, 241}, {241, 0, 88}},
    // climbing away: heading 20, pitch 25
    {{218, 79, 108}, {-88, 241, 0}, {-102, -37, 232}},
};

static mat3_t _target;
static uint16_t _distance;       // metres
static bool _spin, _approach;
static int8_t _approach_dir;
static uint8_t _rotations;       // since the last orthonormalisation

static planes_state_t _state;
static uint8_t _front;           // which block set is on screen

static void _screen_init(void) {
  // VIC bank 2: CIA 2 port A bits 0-1 are the inverted bank number.
  cia2.pra = (uint8_t)((cia2.pra & 0xFC) | 0x01);
  // Screen at bank + $0400, font at bank + $1000 -- the ROM image.
  vic.memptr = 0x14;
  vic.color_border = 14;
  vic.color_back = 6;
  mem_screen_ram = kScreen;
  uint8_t *line = kScreen;
  for (uint8_t row = 0; row < 25; ++row) {
    mem_screen_row_ptrs[row] = line;
    line += 40;
  }
  memset(kScreen, ' ', 1000);
  memset(kColorRam, kColorText, 1000);

  memset(kSpriteBase, 0, 9 * 64);
  planes_dot_bitmap(kSpriteBase + 8 * 64);
  vic.spr_enable = 0;
  vic.spr_multi = 0;
  vic.spr_priority = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    vic.spr_color[i] = kColorPlane;
  }

  print_str(0, 11, STRL(SCREEN_STR("traffic sprite demo")));
  print_str(22, 0, STRL(SCREEN_STR("+/- dist  a/d yaw  w/s pitch  q/e roll")));
  print_str(23, 0, STRL(SCREEN_STR("space spin m approach 1-6 view r reset")));
}

static void _reset(uint8_t preset) {
  _target = kPresets[preset];
  _rotations = 0;
}

static void _rotate(const mat3_t *step) {
  vec_transform3(step, &_target);
  // Small rotations in 8.8 drift; bring the axes back every so often.
  if (++_rotations >= 32) {
    vec_orthonormalize(&_target);
    _rotations = 0;
  }
}

static void _keys(void) {
  keyb_poll();
  if (key_pressed(KSCAN_PLUS) && _distance > 20) {
    _distance -= _distance > 300 ? 20 : _distance > 100 ? 5 : 1;
  }
  if (key_pressed(KSCAN_MINUS) && _distance < 1500) {
    _distance += _distance >= 300 ? 20 : _distance >= 100 ? 5 : 1;
  }
  if (key_pressed(KSCAN_A)) _rotate(&kVecYawLeft);
  if (key_pressed(KSCAN_D)) _rotate(&kVecYawRight);
  if (key_pressed(KSCAN_W)) _rotate(&kVecPitchDown);
  if (key_pressed(KSCAN_S)) _rotate(&kVecPitchUp);
  if (key_pressed(KSCAN_Q)) _rotate(&kVecRollLeft);
  if (key_pressed(KSCAN_E)) _rotate(&kVecRollRight);

  static uint8_t prev;
  uint8_t now = (key_pressed(KSCAN_SPACE) ? 1 : 0) | (key_pressed(KSCAN_M) ? 2 : 0) |
                (key_pressed(KSCAN_R) ? 4 : 0);
  uint8_t edges = keys_edges(now, &prev);
  if (edges & 1) _spin = !_spin;
  if (edges & 2) _approach = !_approach;
  if (edges & 4) {
    _reset(1);
    _distance = 150;
    _spin = _approach = false;
  }
  static const enum KeyScanCode kPresetKeys[6] = {KSCAN_1, KSCAN_2, KSCAN_3,
                                                  KSCAN_4, KSCAN_5, KSCAN_6};
  for (uint8_t i = 0; i < 6; ++i) {
    if (key_pressed(kPresetKeys[i])) _reset(i);
  }
}

static void _animate(void) {
  if (_spin) {
    _rotate(&kVecYawLeft);
    _rotate(&kVecYawLeft);
  }
  if (_approach) {
    if (_distance <= 25) _approach_dir = 1;
    if (_distance >= 600) _approach_dir = -1;
    uint8_t step = _distance > 150 ? 8 : 2;
    if (_approach_dir < 0) {
      _distance -= step;
    } else {
      _distance += step;
    }
  }
}

// Puts the frame on the hardware sprites. Called in the lower border, so
// that nothing changes while the sprites are being displayed.
static void _show(const planes_frame_t *f) {
  if (f->hidden != kPlaneShown) {
    vic.spr_enable = 0;
    return;
  }
  uint8_t *pointers = kScreen + 0x3F8;
  uint8_t count = (uint8_t)(f->rows << (f->cols - 1));
  uint8_t first = f->level == kPlaneLevelDot ? kBlockDot : (uint8_t)(kBlockSets + 4 * _front);
  uint8_t enable = 0, msb = 0, expand_x = 0, expand_y = 0;
  for (uint8_t i = 0; i < count; ++i) {
    uint8_t col = f->cols == 2 ? (i & 1) : 0;
    uint8_t row = f->cols == 2 ? (i >> 1) : i;
    int16_t x = (int16_t)(24 + f->x + (col ? (24 << (f->xs - 1)) : 0));
    int16_t y = (int16_t)(50 + f->y + (row ? (21 << (f->ys - 1)) : 0));
    uint8_t bit = (uint8_t)(1 << i);
    if (x < 0 || x > 400 || y < 0 || y > 255) {
      continue;   // no register value reaches it: leave this one out
    }
    vic.spr_pos[i].x = (uint8_t)x;
    vic.spr_pos[i].y = (uint8_t)y;
    if (x > 255) msb |= bit;
    pointers[i] = f->level == kPlaneLevelDot ? kBlockDot : (uint8_t)(first + i);
    enable |= bit;
    if (f->xs == 2) expand_x |= bit;
    if (f->ys == 2) expand_y |= bit;
  }
  vic.spr_msbx = msb;
  vic.spr_expand_x = expand_x;
  vic.spr_expand_y = expand_y;
  vic.spr_enable = enable;
}

// Lowercase on purpose: s"..." maps lowercase to screen codes 1-26, which
// the ROM font draws as capitals. Uppercase would come out as graphics.
static const char *const kPixelNames[4] = {SCREEN_STR("dot"), SCREEN_STR("1:1"),
                                            SCREEN_STR("x  "), SCREEN_STR("x+y")};
static const char *const kBitmapNames[4] = {SCREEN_STR("drawn "), SCREEN_STR("cached"),
                                            SCREEN_STR("static"), SCREEN_STR("hidden")};

static void _readout(const planes_frame_t *f) {
  print_labeled_bcd(19 * 40 + 0, SCREEN_STR("dist "), _distance, 4);
  print_str(19, 9, STRL(SCREEN_STR("m   d ")));
  print_bcd(19 * 40 + 15, f->d, 2);
  print_str(19, 19, STRL(SCREEN_STR("pixel ")));
  uint8_t pixel = f->level == kPlaneLevelX && f->ys == 2 ? 3 : f->level;
  print_str(19, 25, kPixelNames[pixel], 3);

  print_str(20, 0, STRL(SCREEN_STR("sprites ")));
  kScreen[20 * 40 + 8] = (uint8_t)('0' + f->cols);
  kScreen[20 * 40 + 9] = 24;   // X, in screen codes
  kScreen[20 * 40 + 10] = (uint8_t)('0' + f->rows);
  uint8_t bitmap = f->hidden != kPlaneShown ? 3 : f->level == kPlaneLevelDot ? 2 : f->cached ? 1 : 0;
  print_str(20, 12, kBitmapNames[bitmap], 6);
  if (f->clamped) {
    print_str(20, 20, STRL(SCREEN_STR("size held")));
  } else {
    print_str(20, 20, STRL(SCREEN_STR("         ")));
  }
}

int main(void) {
  cia_init();
  bm_init();
  _screen_init();
  planes_state_init(&_state);
  _reset(DEMO_PRESET);
  _distance = DEMO_DISTANCE;
  _spin = DEMO_SPIN != 0;
  _approach = DEMO_APPROACH != 0;
  _approach_dir = -1;

  for (;;) {
    _keys();
    _animate();

    // The camera looks along +x, so the aircraft straight ahead is at
    // (distance, 0, 0), in quarter metres, and camera space is world space.
    vec3_t c = make_vector((int16_t)(_distance << 2), 0, 0);
    uint8_t *back = kSpriteBase + ((uint16_t)(_front ^ 1) << 8);
    planes_frame_t frame;
    bm_start();
    planes_render(&_state, &kView, &c, &_target, back, &frame);
    bm_end(21 * 40 + 0, SCREEN_STR("render cycles "));
    if (frame.hidden == kPlaneShown && frame.level != kPlaneLevelDot && !frame.cached) {
      _front ^= 1;
    }

    while (vic.raster != 255) {
    }
    _show(&frame);
    _readout(&frame);
  }
  return 0;
}
