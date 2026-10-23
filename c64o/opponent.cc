#include "opponent.h"

#ifdef __OSCAR64__
#include <c64/memmap.h>
#else
// Host builds have no banking: test/opponent_test.cc's array stands in for
// bank 3, RAM under I/O included.
#define mmap_set(m)
#endif

#include "color.h"
#include "flight.h"
#include "mem.h"
#include "mission.h"
#include "msg.h"
#include "planedef.h"
#include "planes.h"
#include "sprites.h"
#include "vec.h"
#include "view.h"
#include "world.h"

// docs/planes.md §13: two aircraft, each flying a fixed path.

// Two slots, each with every sprite it can use: two sets of four blocks fill
// $D000-$D3FF, and the dot has a block of its own below it (mem.h). The
// rendering loop below takes them in turns, which is written for two.
static const uint8_t kOpponentMax = 2;

// --- Sprite blocks ----------------------------------------------------------
//
// The dot at $CFC0, plain RAM off the end of the main region (mem.h
// kOpponentDotBlock). Then $D000-$D3FF, RAM under I/O that the VIC in bank 3
// reads, time-shared with the map view's screen RAM and the title aircraft
// (mem.h, docs/planes.md §5): each slot's two sets of four, front and back,
// for up to 2 x 2 sprites (kView below).
static const uint8_t kOpponentBlockDot = kOpponentDotBlock;
static const uint8_t kOpponentSetBlocks = 4;
static const uint8_t kOpponentBlockFirst[kOpponentMax] = {
    kTitleSpriteBlock, kTitleSpriteBlock + 2 * kOpponentSetBlocks};
static_assert(kTitleSpriteBlock + kOpponentMax * 2 * kOpponentSetBlocks <=
                  (0xD400 - 0xC000) / 64,
              "opponent blocks run past $D3FF into the sprite bitmaps");

#ifdef __OSCAR64__
static uint8_t *const kOpponentBank = (uint8_t *)0xC000;
#else
// Host builds point this at an ordinary array, like mem.h's screen pointers.
extern uint8_t *kOpponentBank;
#endif

static inline uint8_t *_opponent_block(uint8_t block) {
  return kOpponentBank + ((uint16_t)block << 6);
}

// The viewport's centre, the last line a sprite may start on, plain and
// Y-expanded (mem.h), and the model's own size cap: the aircraft grows to
// 2 x 2 sprites and holds its size from ~54 m.
static const planes_view_t kView = {
    kScreenWidthPixels / 2, kViewportEndYPixels / 2,
    kSpriteVisibleEndYPixels - 1,
    kViewportEndYPixels - kSpritesOffLeadExpandY - 1, kPlaneKMax};
// The same, held to two sprites at every attitude: it stops growing at ~93 m
// (planes.h kPlaneKMaxTwoSprites). For an aircraft that has to share the
// stack's seven sprites with another (_opponent_cap()). Farther than
// ~93 m it draws exactly what kView does, k never reaching either cap, so
// it can be handed to an aircraft at range for nothing - not even a redraw.
static const planes_view_t kViewTwo = {
    kScreenWidthPixels / 2, kViewportEndYPixels / 2,
    kSpriteVisibleEndYPixels - 1,
    kViewportEndYPixels - kSpritesOffLeadExpandY - 1, kPlaneKMaxTwoSprites};
static_assert(kSpriteVisibleEndYPixels - 1 == 89 &&
                  kViewportEndYPixels - kSpritesOffLeadExpandY - 1 == 68,
              "the DMA cuts moved: check planes.h's description of them");

// --- Paths ------------------------------------------------------------------
//
// No flight model: each aircraft flies level at one height and its one
// airspeed, straight at its next waypoint, and turns at one fixed rate when
// the waypoint is off its nose. Once the waypoint is within kOpponentReach it
// heads for the next. After the last, a loop starts again at the first and a
// one-way path's aircraft is gone: it has left the map. It steers at a point,
// not along the line between two: reacting to the player would only change
// the point.
//
// Positions in 8.8 world units, 256 m each, so that flight_eye_x = x << 8;
// the height likewise, flight_eye_z = z << 8. x is north and +y west, as
// everywhere. No leg, the start to the first waypoint included, may be 64
// units or more along either axis (_opponent_toward()).
static const uint8_t kOpponentWaypoints = 4;
enum opponent_kind_t {
  kOpponentNone = 0,
  kOpponentLoop,
  kOpponentOneWay,
  // On the ground where it starts, and stays: no waypoints, no airspeed.
  kOpponentParked,
};
// No waypoint fails the mission.
static const uint8_t kOpponentNoFail = 0xFF;
struct opponent_path_t {
  uint8_t kind;
  uint8_t count;
  // The waypoint whose reaching fails the mission, or kOpponentNoFail.
  uint8_t fail;
  // Airspeed in the model's units (flight.md): the player flies level at
  // 1,476 on half throttle and ~2,500 on full below 4,000 ft, 2,350 at
  // 5,000; it climbs at 1,450-1,600 and stalls at 1,024.
  int16_t speed;
  uint16_t start_x, start_y;
  // The heading it starts on: a level unit vector, 8.8.
  int16_t head_x, head_y;
  uint16_t z;
  uint16_t x[kOpponentWaypoints], y[kOpponentWaypoints];
};

// Heights on flight.cc's scale, 1,000 ft to 0x0200; the ground is at 0.
static const uint16_t kOpponent5000Ft = 0x0A00;
static const uint16_t kOpponent3000Ft = 0x0600;

// The missions (fmission.cc). Runway 1, home, is at (0x20, 0x3F), runway 2 at
// (0x60, 0xBF) and the city at (0x10, 0x68). The player starts on runway 1
// and takes ~11 s to get airborne, then ~3.3 min to climb to 4,000 ft at
// full throttle, ~4.3 min to 5,000, where the thin air slows it.
//
// 01, Recon: at 5,000 ft and 2,000, faster than the player climbs and slower
// than it flies level up there. It comes in from the north edge of the map,
// passes 6 km north-west of runway 1 at ~3.5 min, turns for the city, over it
// at ~5 min, and leaves over the south edge at ~6.3 min: gone, and the
// mission failed. A player who climbs out towards the city can be at its
// height there a minute before it; one who chases it from the runway falls
// behind while climbing.
//
// 02, Bombers: two at 3,000 ft and 1,500, in loose formation - the second
// 150 m to the left of and 200 m behind the first - come from the west along
// the city's row and reach it after ~5.5 min, which fails the mission. Then
// they fly on east and leave over the east edge. The player can be at
// 3,000 ft over the city in ~2.5 min.
//
// 03, Ambush: two on the ground beside the south end of runway 2, 37 km
// away, 24 m and 72 m off its west edge at y 0xC0, facing up it. Their
// centres are 4 m up, about where an 11 m aircraft's is on its wheels.
static const opponent_path_t kOpponentPaths[kMissionCount][kOpponentMax] = {
    {{kOpponentOneWay, 4, 3, 2000, 0x7E00, 0x5000, -256, 0, kOpponent5000Ft,
      {0x5000, 0x3000, 0x1000, 0x0000}, {0x5000, 0x5000, 0x6800, 0x8000}},
     {kOpponentNone}},
    {{kOpponentOneWay, 4, 1, 1500, 0x1000, 0xC800, 0, -256, kOpponent3000Ft,
      {0x1000, 0x1000, 0x1000, 0x1000}, {0x9800, 0x6800, 0x3400, 0x0000}},
     {kOpponentOneWay, 4, 1, 1500, 0x1096, 0xC8C8, 0, -256, kOpponent3000Ft,
      {0x1096, 0x1096, 0x1096, 0x1096}, {0x98C8, 0x68C8, 0x34C8, 0x00C8}}},
    {{kOpponentParked, 0, kOpponentNoFail, 0, 0x5D00, 0xC018, 256, 0, 0x0004},
     {kOpponentParked, 0, kOpponentNoFail, 0, 0x5D00, 0xC048, 256, 0, 0x0004}},
};

// What the message says when the mission fails, per mission.
static const char *const kOpponentFailText[kMissionCount] = {
    "MISSION FAILED - RECON ESCAPED",
    "MISSION FAILED - CITY BOMBED",
    0,
};

// The turn: vec_turn3_xy()'s small angle, 8 / 256 rad, 1.8 degrees a model
// step at flight_step_shift 0. At 11.7 m a step that is a turn of ~375 m
// radius, a quarter circle in 50 steps. At a larger shift the same angle
// once every 1 << shift steps, not a smaller one every step: below 8 the
// angle's product with a small component truncates to nothing, and the turn
// would come out slower the faster the machine.
static const int16_t kOpponentTurn = 8;
// Closer than this on both axes, in metres, and the waypoint is reached. No
// less than the turn's radius: a waypoint inside the circle the aircraft is
// turning on would be circled for ever, and every such circle passes within
// a radius of it.
static const int16_t kOpponentReach = 512;
// The bank while turning, drawn only: cos and sin of 30 degrees, 8.8.
static const int16_t kOpponentBankCos = 222;
static const int16_t kOpponentBankSin = 128;

// The pixel size, planes_frame_t::d, from which an aircraft is drawn in
// kColorOpponentNear: about 540 m for the default model, and below
// kOpponentNearD - 1, ~620 m, back to kColorOpponentFar.
static const uint8_t kOpponentNearD = 8;

// --- State ------------------------------------------------------------------

#pragma bss(bss2)
static bool _opponent_on[kOpponentMax];
static const opponent_path_t *_opponent_path[kOpponentMax];
// The mission, whether it has failed, and bit i: slot i was shot down - by
// the guns still to come, so for now never.
static uint8_t _opponent_mission;
static bool _opponent_failed;
static uint8_t _opponent_down;
// The waypoint it is flying to.
static uint8_t _opponent_next[kOpponentMax];
// Position in flight_eye_*'s 24.8 metres.
static int32_t _opponent_x[kOpponentMax];
static int32_t _opponent_y[kOpponentMax];
static int32_t _opponent_z[kOpponentMax];
// The heading: the front, a unit vector in 8.8. Always level, so z is 0, the
// left is (-y, x, 0) and the up is the world's.
static vec3_t _opponent_head[kOpponentMax];
// Which way it turned on the last step: +1 left, -1 right, 0 straight. And
// the bank drawn: the same, once a turn has lasted two steps, so that the
// one-step corrections on a leg do not flick the wings.
static int8_t _opponent_turn[kOpponentMax];
static int8_t _opponent_bank[kOpponentMax];
// Counts model steps, for the turns at a larger flight_step_shift.
static uint8_t _opponent_substep;
static planes_state_t _opponent_planes[kOpponentMax];
// Bit i: which of slot i's two block sets is on screen.
static uint8_t _opponent_front;
// Bit i: slot i was projected anew last frame and is filled this one.
static uint8_t _opponent_pending;
// The slot that goes first next frame: the one that did not start the last
// redraw (opponent_add_sprites()).
static uint8_t _opponent_first;
// Bit i: slot i is near enough for kColorOpponentNear.
static uint8_t _opponent_near;
// The layout of what is in slot i's front set.
static planes_layout_t _opponent_shown[kOpponentMax];
// The slot taken for the nearer while both are in view, and bit i: slot i is
// drawn under kViewTwo this frame.
static uint8_t _opponent_nearer;
static uint8_t _opponent_capped;
// The aircraft being culled or drawn, in camera space and quarter metres,
// each slot's once it is in view, and the view it is drawn in. Statics
// rather than locals, like the two below and the slot's state in
// _opponent_draw(), read through _opponent_planes[i] rather than a pointer:
// oscar64's zero page temporaries grow with the call graph and the
// renderer's chain is the deepest, with 4 bytes to spare (mem.h,
// tools/check_zeropage.py).
static vec3_t _opponent_c;
static vec3_t _opponent_cs[kOpponentMax];
static const planes_view_t *_opponent_view;
// opponent_add_sprites()'s: which slots are in view, and whether a redraw is
// in flight this frame.
static uint8_t _opponent_seen;
static bool _opponent_busy;
#pragma bss(bss)

void opponent_invalidate(void) {
  for (uint8_t i = 0; i < kOpponentMax; ++i) {
    planes_state_init(&_opponent_planes[i]);
    _opponent_shown[i] = _opponent_planes[i].lay;
  }
  _opponent_front = 0;
  _opponent_pending = 0;
  _opponent_near = 0;
  // Below I/O, so no banking. Written here all the same, not once at boot:
  // it is in no linker region, so nothing loads or clears it.
  planes_dot_bitmap(_opponent_block(kOpponentBlockDot));
}

bool opponent_targets_down(void) {
  if (_opponent_failed) {
    return false;
  }
  for (uint8_t i = 0; i < kOpponentMax; ++i) {
    if (kOpponentPaths[_opponent_mission][i].kind != kOpponentNone &&
        !(_opponent_down & (uint8_t)(1 << i))) {
      return false;
    }
  }
  return true;
}

void opponent_reset(uint8_t mission) {
  _opponent_mission = mission;
  _opponent_failed = false;
  _opponent_down = 0;
  for (uint8_t i = 0; i < kOpponentMax; ++i) {
    const opponent_path_t *p = &kOpponentPaths[mission][i];
    _opponent_on[i] = p->kind != kOpponentNone;
    _opponent_path[i] = p;
    _opponent_next[i] = 0;
    _opponent_x[i] = (int32_t)p->start_x << 8;
    _opponent_y[i] = (int32_t)p->start_y << 8;
    _opponent_z[i] = (int32_t)p->z << 8;
    _opponent_head[i] = make_vector(p->head_x, p->head_y, 0);
    _opponent_turn[i] = 0;
    _opponent_bank[i] = 0;
  }
  opponent_invalidate();
}

// a * b + c * d, each in 8.8 with b and d no more than 256: every product
// below, which are all two-term dot products, called rather than written out.
static __noinline int16_t _opponent_dot(int16_t a, int16_t b, int16_t c, int16_t d) {
  return vec_fastmul8p8(a, b) + vec_fastmul8p8(c, d);
}

// (v >> flight_step_shift), as flight.cc's _flight_step_s(): a loop, since
// the 6502 has no variable shift.
static __noinline int16_t _opponent_step_s(int16_t v) {
  for (uint8_t n = flight_step_shift; n != 0; --n) {
    v >>= 1;
  }
  return v;
}

// A waypoint coordinate, 8.8 units, less a position, 24.8, in metres and
// wrapped to 32,768 m, the world's 128 units along x: taken in half metres,
// which the int16_t wraps at 65,536, and halved. Good to 64 units either way,
// which is why no leg is that long. Along y the world is 256 units, and the
// same 64 holds.
static __noinline int16_t _opponent_toward(uint16_t w, int32_t pos) {
  int16_t d = (int16_t)((((int32_t)w << 8) - pos) >> 7);
  return d >> 1;
}

// What flight.cc's _flight_move_forward() adds along one axis for a level
// aircraft: the airspeed doubled times the heading's component, shifted by
// the step size after the multiply.
static __noinline void _opponent_move(int32_t *pos, int16_t speed2, int16_t h) {
  *pos += _opponent_step_s(vec_fastmul8p8(speed2, h));
}

static void _opponent_forget(uint8_t i, uint8_t bit);

// The mission has failed: said once, and WP_TARGETS_DOWN never comes.
static void _opponent_fail(void) {
  if (!_opponent_failed) {
    _opponent_failed = true;
    msg_show(kOpponentFailText[_opponent_mission], 3 * MSG_DEFAULT_DURATION);
  }
}

void opponent_step(void) {
  if (flight_paused || flight_crashed()) {
    return;
  }
  bool turns = (++_opponent_substep & kFlightSubstepMask) == 0;
  for (uint8_t i = 0; i < kOpponentMax; ++i) {
    if (!_opponent_on[i]) {
      continue;
    }
    const opponent_path_t *p = _opponent_path[i];
    if (p->kind == kOpponentParked) {
      continue;
    }
    vec3_t *h = &_opponent_head[i];

    // The waypoint, relative, in metres. Reached, and the next one is
    // steered for from the next step on.
    uint8_t w = _opponent_next[i];
    int16_t dx = _opponent_toward(p->x[w], _opponent_x[i]);
    int16_t dy = _opponent_toward(p->y[w], _opponent_y[i]);
    if (dx < kOpponentReach && dx > -kOpponentReach && dy < kOpponentReach &&
        dy > -kOpponentReach) {
      if (w == p->fail) {
        _opponent_fail();
      }
      if (++w == p->count) {
        if (p->kind == kOpponentOneWay) {
          // Off the map: gone, and nothing of it on screen.
          _opponent_on[i] = false;
          _opponent_forget(i, (uint8_t)(1 << i));
          continue;
        }
        w = 0;
      }
      _opponent_next[i] = w;
    }

    // Steer: the waypoint's side of the nose, from the cross product of the
    // heading and the direction to it, and whether it is near enough dead
    // ahead to fly straight, from that against the dot product: within
    // 1 / 32 rad, 1.8 degrees, wider than one step's turn, so a straight leg
    // stays straight. The direction is cut down to six bits a component
    // first, so both products fit 16 bits, and taken as 8.8 so that
    // vec_fastmul8p8() forms them exactly, with no multiply linked for them.
    while (dx > 63 || dx < -63 || dy > 63 || dy < -63) {
      dx >>= 1;
      dy >>= 1;
    }
    dx <<= 8;
    dy <<= 8;
    int16_t hx = h->x, hy = h->y;
    int16_t cross = _opponent_dot(dy, hx, -dx, hy);
    int16_t dot = _opponent_dot(dx, hx, dy, hy);
    int16_t across = cross < 0 ? -cross : cross;
    int8_t turn = 0;
    if (dot <= 0 || across > (dot >> 5)) {
      turn = cross >= 0 ? 1 : -1;
      // The small-angle turn of vec_turn3_xy(), anticlockwise for a left
      // turn: h += s * left. The step lengthens the vector a little, and the
      // normalisation puts it back.
      if (turns) {
        int16_t s = turn > 0 ? kOpponentTurn : -kOpponentTurn;
        h->x = hx - vec_fastmul8p8(s, hy);
        h->y = hy + vec_fastmul8p8(s, hx);
        vec_normalize(h);
      }
    }
    _opponent_bank[i] = turn == _opponent_turn[i] ? turn : 0;
    _opponent_turn[i] = turn;

    int16_t speed2 = p->speed << 1;
    _opponent_move(&_opponent_x[i], speed2, h->x);
    _opponent_move(&_opponent_y[i], speed2, h->y);
  }
}

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

// Slot i is off screen, or only the dot, so its block sets no longer show
// what it looks like. Until a new silhouette is filled it is shown as the
// dot, never as the bitmap from the last time it was in view: that can be
// any size, and a chase that looked away and back saw one frame of the
// aircraft as close as it had been.
static void _opponent_forget(uint8_t i, uint8_t bit) {
  _opponent_shown[i].level = kPlaneLevelDot;
  // A projection still to be filled is a bitmap that was never drawn, so
  // nothing may come from the cache against it.
  if (_opponent_pending & bit) {
    _opponent_pending &= (uint8_t)~bit;
    _opponent_planes[i].key_valid = false;
  }
}

// d, a difference of two 24.8 positions, wrapped to the world - mask 0x7F for
// x, which repeats every 128 units, 0xFF for y and z - in quarter metres
// (docs/planes.md §2). False if it is 16 units, 4 km, or more: the cull.
static bool _opponent_quarter(int32_t d, uint8_t mask, int16_t *q) {
  uint8_t u = (uint8_t)(d >> 16) & mask;
  // Sign-extend x's seven bits. For y and z this sets bit 7 only on values
  // from 64 units up, which the cull rejects either way.
  if (u & 0x40) {
    u |= 0x80;
  }
  // A signed range test, not (uint8_t)(u + 16) >= 32: oscar64 folds that to
  // u >= 16, wraparound dropped, and culled everything behind on x.
  int8_t s = (int8_t)u;
  if (s >= 16 || s < -15) {
    return false;
  }
  *q = (int16_t)(((uint16_t)u << 10) + ((uint16_t)d >> 6));
  return true;
}

// Whether the aircraft at _opponent_c is wholly outside the viewport.
//
// Every part of it is within r = kPlaneMaxRadius / 2 quarter metres of c, so
// no part is inside an edge at tangent t if |y| - r > t * (x + r), that is
// |y| - r * (1 + t) > t * x. The edges are at t = 160 / 256 = 1/2 + 1/8
// across and 56 / 256 = 1/4 - 1/32 up and down (vec_project()), and each
// gets another 4 pixels, x / 64, for expanded pixels' rounding.
//
// Unsigned: x is positive here, and oscar64's signed shifts cost twice the
// bytes. On _opponent_c rather than three parameters, for the same reason.
static const uint16_t kOpponentReachX = (kPlaneMaxRadius / 2) * 13 / 8 + 2;
static const uint16_t kOpponentReachZ = (kPlaneMaxRadius / 2) * 39 / 32 + 2;

static __noinline bool _opponent_off_screen(void) {
  uint16_t half = (uint16_t)_opponent_c.x >> 1;
  uint16_t eighth = half >> 2;
  uint16_t px4 = eighth >> 3;
  int16_t y = _opponent_c.y, z = _opponent_c.z;
  uint16_t ay = (uint16_t)(y < 0 ? -y : y);
  uint16_t az = (uint16_t)(z < 0 ? -z : z);
  return ay > half + eighth + px4 + kOpponentReachX ||
         az > (half >> 1) - (eighth >> 2) + px4 + kOpponentReachZ;
}

// Slot i's body axes in camera space, as planes_project() reads them: only
// their screen components, y and z, which are each a dot product with
// world_cam's left and up. The heading is level, so the front and left have
// no z and the up is the world's, whose products are world_cam's own z
// components. Then the bank, while turning: left and up turned about the
// front by 30 degrees, towards the turn.
static void _opponent_axes(uint8_t i, mat3_t *axes) {
  int16_t hx = _opponent_head[i].x, hy = _opponent_head[i].y;
  int8_t turn = _opponent_bank[i];
  int16_t sn = turn > 0 ? kOpponentBankSin : -kOpponentBankSin;
  // y, then z: the products with world_cam's left, then with its up.
  const vec3_t *row = &world_cam.left;
  for (uint8_t c = 0; c < 2; ++c) {
    int16_t l = _opponent_dot(row->x, -hy, row->y, hx);
    int16_t u = row->z;
    (&axes->front.y)[c] = _opponent_dot(row->x, hx, row->y, hy);
    if (turn != 0) {
      // Banked left, the left wing goes down and the top towards the left.
      int16_t l0 = l;
      l = _opponent_dot(l0, kOpponentBankCos, u, -sn);
      u = _opponent_dot(u, kOpponentBankCos, l0, sn);
    }
    (&axes->left.y)[c] = l;
    (&axes->up.y)[c] = u;
    row = &world_cam.up;
  }
}

static_assert(kOpponentMax == 2, "the slots' turns below are written for two");

// Which aircraft are in view, and where: bit i of the result for slot i, and
// its camera-space position in _opponent_cs[i]. This, _opponent_cap() and
// _opponent_draw() are functions of their own, out of
// opponent_add_sprites(): in one they pushed oscar64's zero page
// temporaries past the end of its area (mem.h).
static __noinline uint8_t _opponent_cull(void) {
  uint8_t seen = 0;
  for (uint8_t i = 0; i < kOpponentMax; ++i) {
    if (!_opponent_on[i]) {
      continue;
    }
    uint8_t bit = (uint8_t)(1 << i);
    vec3_t rel;
    if (_opponent_quarter(_opponent_x[i] - flight_eye_x, 0x7F, &rel.x) &&
        _opponent_quarter(_opponent_y[i] - flight_eye_y, 0xFF, &rel.y) &&
        _opponent_quarter(_opponent_z[i] - flight_eye_z, 0xFF, &rel.z)) {
      vec_transform_inv(&world_cam, &rel, &_opponent_c);
      // planes_render() says the same about x, but only after the axes
      // below. Then wholly outside the viewport: planes_render() only gives
      // up 45 degrees off the view axis, so an aircraft just over the top on
      // the pass, or just off to one side, would be projected, and drawn or
      // looked up in the cache, for nothing: 7,000-38,000 cycles a frame.
      if (_opponent_c.x > 64 && !_opponent_off_screen()) {
        seen |= bit;
        _opponent_cs[i] = _opponent_c;
        continue;
      }
    }
    _opponent_forget(i, bit);
  }
  return seen;
}

// Seven sprites for the stack (sprites.h kSpriteTerrainSlots), and two
// aircraft at 2 x 2 would take eight: the farther would be dropped whole.
// So while both are in view the farther is held to two sprites, which
// changes nothing unless it is within ~93 m (kViewTwo); and in the back
// view, where the fin takes two of the seven (sprites.cc kSpriteFinCount),
// the nearer as well. Which is nearer goes by depth, and changes only once
// the other is an eighth nearer, so that two at about the same range do
// not trade sizes every frame. Sets _opponent_capped.
static __noinline void _opponent_cap(uint8_t seen) {
  uint8_t capped = 0;
  if (seen == 3) {
    int16_t dn = _opponent_cs[_opponent_nearer].x;
    if (_opponent_cs[_opponent_nearer ^ 1].x < dn - (dn >> 3)) {
      _opponent_nearer ^= 1;
    }
    capped = view_state == VIEW_BACK ? 3 : (uint8_t)(2 >> _opponent_nearer);
  }
  for (uint8_t i = 0; i < kOpponentMax; ++i) {
    uint8_t bit = (uint8_t)(1 << i);
    // Newly held to two, with more on screen or being filled: the dot until
    // its turn to redraw comes round, never more than its share.
    if ((capped & bit) && !(_opponent_capped & bit)) {
      const planes_layout_t *lay = (_opponent_pending & bit)
                                       ? &_opponent_planes[i].lay
                                       : &_opponent_shown[i];
      if (lay->level != kPlaneLevelDot && lay->cols + lay->rows > 3) {
        _opponent_forget(i, bit);
      }
    }
  }
  _opponent_capped = capped;
}

// Slot i, in view at _opponent_cs[i]: its redraw, or the half of one that is
// due, and its offer to the stack. `busy` if another slot has a redraw in
// flight this frame; true if this one starts one.
static __noinline bool _opponent_draw(uint8_t i, bool busy) {
  uint8_t bit = (uint8_t)(1 << i);
  bool started = false;
  _opponent_view = (_opponent_capped & bit) ? &kViewTwo : &kView;
  _opponent_c = _opponent_cs[i];
  // The first block of the set on screen.
  uint8_t front = kOpponentBlockFirst[i];
  if (_opponent_front & bit) {
    front += kOpponentSetBlocks;
  }

  // A redraw takes two frames, the projection in one and the fill in the
  // next (docs/planes.md §13, "The silhouette at half rate"), so neither
  // frame pays for both. The position is this frame's either way: the
  // bitmap on screen is placed at this frame's centre.
  planes_frame_t f;
  if (_opponent_pending & bit) {
    // The fill the last frame projected. It writes under I/O; the raster
    // handlers bank I/O in for themselves (gfx.cc _gfx_isr), and nothing
    // in planes_fill() touches it. Then the new bitmap is the front one:
    // it goes on screen with this frame's characters, at
    // mem_switch_buffer(), and by the time a later frame fills the other
    // set the raster handler has stopped showing it - mem_switch_buffer()
    // runs past raster 250 in its colour copy before it returns.
    bool seen = planes_centre(_opponent_view, &_opponent_c, &f);
    _opponent_front ^= bit;
    front = kOpponentBlockFirst[i];
    if (_opponent_front & bit) {
      front += kOpponentSetBlocks;
    }
    mmap_set(MMAP_RAM);
    planes_fill(&_opponent_planes[i], _opponent_block(front));
    mmap_set(MMAP_NO_ROM);
    _opponent_pending &= (uint8_t)~bit;
    _opponent_shown[i] = _opponent_planes[i].lay;
    if (!seen) {
      return started;
    }
    planes_place(&_opponent_shown[i], _opponent_view, &f);
  } else if (busy) {
    // The other slot's turn: what is on screen, where it is now.
    if (!planes_centre(_opponent_view, &_opponent_c, &f)) {
      return started;
    }
    planes_place(&_opponent_shown[i], _opponent_view, &f);
  } else {
    mat3_t axes;
    _opponent_axes(i, &axes);
    planes_project(&_opponent_planes[i], _opponent_view, &_opponent_c, &axes, &f);
    if (f.hidden != kPlaneShown || f.level == kPlaneLevelDot) {
      _opponent_forget(i, bit);
    } else if (f.cached) {
      // The front set holds the state's silhouette - after a forget too,
      // as the state has not been projected since it was filled.
      _opponent_shown[i] = _opponent_planes[i].lay;
    } else {
      // A new silhouette, filled next frame; until then the old bitmap,
      // or the dot if there is none.
      _opponent_pending |= bit;
      started = true;
      planes_place(&_opponent_shown[i], _opponent_view, &f);
    }
  }
  if (f.hidden != kPlaneShown) {
    return started;
  }

  uint8_t bitmap = kOpponentBlockDot;
  uint8_t bitmap2 = kSpriteNoBitmap;
  uint8_t flags = 0;
  if (f.level != kPlaneLevelDot) {
    bitmap = front;
    if (f.rows == 2) {
      bitmap2 = (uint8_t)(front + f.cols);
    }
    if (f.cols == 2) {
      flags |= kSpriteFlagWide;
    }
    if (f.xs == 2) {
      flags |= kSpriteFlagExpandX;
    }
    if (f.ys == 2) {
      flags |= kSpriteFlagExpandY;
    }
  }
  // The colour, from the pixel size: near from about 8 pixels across, and
  // back to far below 7, so that a chase holding the range on the line does
  // not flicker.
  if (f.d >= kOpponentNearD) {
    _opponent_near |= bit;
  } else if (f.d < kOpponentNearD - 1) {
    _opponent_near &= (uint8_t)~bit;
  }
  uint8_t color =
      (_opponent_near & bit) ? kColorOpponentNear : kColorOpponentFar;
  // Depth in the clouds' 2 m units, so that the two sort together.
  sprites_stack_add(_opponent_c.x >> 3, f.x, f.y, 0, 0, bitmap, bitmap2,
                    color, flags);
  return started;
}

void opponent_add_sprites(void) {
  _opponent_seen = _opponent_cull();
  _opponent_cap(_opponent_seen);

  // One redraw at a time, across both slots: a slot may start one only on a
  // frame that fills nothing and has not started another, so that the frame
  // pays for one projection or one fill, as it does with one aircraft. A slot
  // kept waiting shows its last bitmap at this frame's centre, as a slot
  // waiting for its fill does. The slot that did not start the last redraw
  // goes first, so the two take turns while both change. The order is fixed
  // before the loop, which sets the next frame's.
  _opponent_busy = _opponent_pending != 0;
  uint8_t first = _opponent_first;
  for (uint8_t n = 0; n < kOpponentMax; ++n) {
    uint8_t i = n ^ first;
    if ((_opponent_seen & (uint8_t)(1 << i)) && _opponent_draw(i, _opponent_busy)) {
      _opponent_busy = true;
      _opponent_first = i ^ 1;
    }
  }
}

#pragma optimize(pop)
