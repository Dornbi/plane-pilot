// Host test for gun.cc (docs/projectiles.md): the rounds - where they leave,
// where they go, what they hit - and the one tracer drawn of them.
//
// gun.cc is #included rather than linked, for its statics. What it asks of
// opponent.cc is answered by the fakes below, which put targets where a case
// wants them and count the strikes; the flight globals it reads are defined
// here, so a case sets the eye, the attitude and the speed directly.
//
// Lengths in the cases are metres on the game's scale (docs/map.md §4) and
// world positions flight_eye_*'s 24.8 half metres: a metre is 512 of them.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../flight.h"
#include "../gun.h"
#include "../sprites.h"
#include "../vec.h"
#include "../world.h"

bool flight_paused = false;
enum FlightStatus flight_status;
uint8_t flight_step_shift = 0;
uint8_t kFlightSubstepMask = 0;
int32_t flight_eye_x, flight_eye_y, flight_eye_z;
mat3_t flight_cam;
mat3_t world_cam;
int16_t flight_speed;
int16_t flight_vspeed;

// The opponents, as gun.cc sees them.
static bool _target_on[kOpponentMax];
static int32_t _target[kOpponentMax][3];
static int _strikes[kOpponentMax];
bool opponent_target(uint8_t i, int32_t *p) {
  if (!_target_on[i]) return false;
  for (int c = 0; c < 3; ++c) p[c] = _target[i][c];
  return true;
}
void opponent_strike(uint8_t i) { ++_strikes[i]; }
// The smoking aircraft, when a case puts one up.
static bool _smoke_on;
static int32_t _smoke_p[3];
static int16_t _smoke_h[2];
bool opponent_smoke(int32_t *p, int16_t *h) {
  if (!_smoke_on) return false;
  for (int c = 0; c < 3; ++c) p[c] = _smoke_p[c];
  h[0] = _smoke_h[0];
  h[1] = _smoke_h[1];
  return true;
}

// The last sprite offered, and how many were.
struct offer_t {
  int16_t depth, x, y;
  int8_t px, py;
  uint8_t bitmap, color, flags;
};
static offer_t _offer;
static int _offers;
bool sprites_stack_add(int16_t depth, int16_t x, int16_t y, int8_t px, int8_t py,
                       uint8_t bitmap, uint8_t, uint8_t color, uint8_t flags) {
  _offer = {depth, x, y, px, py, bitmap, color, flags};
  ++_offers;
  return true;
}

#include "../gun.cc"

static int _fails = 0;
#define CHECK(cond, ...)           \
  do {                             \
    if (!(cond)) {                 \
      if (++_fails <= 20) {        \
        printf("gun_test: ");      \
        printf(__VA_ARGS__);       \
        printf("\n");              \
      }                            \
    }                              \
  } while (0)

static const double kHalfMetre = 256.0;  // 24.8 half metres, per half metre
static const int32_t kMetre = 512;

// Level, heading north, looking ahead: front +x, left +y, up +z.
static void level_north(void) {
  flight_cam.front = make_vector(256, 0, 0);
  flight_cam.left = make_vector(0, 256, 0);
  flight_cam.up = make_vector(0, 0, 256);
  world_cam = flight_cam;
}

// gun_reset(), and one step with the trigger released: the guns do not fire
// until it has been let go once (gun.cc _gun_armed).
static void reset_guns(void) {
  gun_reset();
  gun_step(false);
}

static void start(int16_t speed) {
  flight_eye_x = 40L << 16;
  // Half a unit in, as flight_init_from_mission() puts the eye.
  flight_eye_y = (100L << 16) + 0x8000;
  flight_eye_z = 4L << 16;
  flight_speed = speed;
  flight_vspeed = 0;
  flight_paused = false;
  flight_status = FLIGHT_ONGOING;
  level_north();
  memset(_target_on, 0, sizeof(_target_on));
  memset(_strikes, 0, sizeof(_strikes));
  _smoke_on = false;
  reset_guns();
}

// One model step: the eye moves as flight.cc moves it, then the guns.
static void step(bool trigger) {
  flight_eye_x += vec_fastmul8p8(flight_cam.front.x, flight_speed << 1);
  flight_eye_y += vec_fastmul8p8(flight_cam.front.y, flight_speed << 1);
  flight_eye_z += flight_vspeed;
  gun_step(trigger);
}

// Round r's world position, in metres from the eye.
static double ahead_m(uint8_t r) { return _round_p[r] / 16.0; }
static double left_m(uint8_t r) { return _round_p[kRoundMax + r] / 16.0; }
static double up_m(uint8_t r) { return _round_p[2 * kRoundMax + r] / 16.0; }

static void test_wings(void) {
  printf("  rounds leave the wings in turn, 20 m ahead and below\n");
  start(1500);
  step(true);
  uint8_t a = _gun_last;
  CHECK(a != kRoundNone, "no round after the trigger");
  CHECK(fabs(ahead_m(a) - 20) < 0.2 && fabs(left_m(a) - 4) < 0.1 && fabs(up_m(a) + 1.5) < 0.1,
        "first round at %.1f, %.1f, %.1f", ahead_m(a), left_m(a), up_m(a));
  step(true);
  CHECK(_gun_last == a, "fired again on the next step");
  step(true);
  uint8_t b = _gun_last;
  CHECK(b != a && fabs(left_m(b) + 4) < 0.1, "second round from %.1f, not the right wing", left_m(b));
  // Released, nothing more goes.
  int live = 0;
  for (int n = 0; n < 4; ++n) step(false);
  for (uint8_t r = 0; r < kRoundMax; ++r) live += _round_age[r] != 0;
  CHECK(live == 2, "%d rounds in flight after two shots", live);
}

static void test_converge(void) {
  printf("  the two streams cross the boresight about 200 m ahead\n");
  for (int side = 0; side < 2; ++side) {
    start(0);
    _gun_side = (uint8_t)side;
    step(true);
    uint8_t r = _gun_last;
    // Between the two steps either side of it, by the sideways offsets.
    double crossed = -1, prev = left_m(r), prev_ahead = ahead_m(r);
    for (int n = 0; n < 10 && crossed < 0; ++n) {
      step(false);
      if ((prev > 0) != (left_m(r) > 0)) {
        crossed = prev_ahead + (ahead_m(r) - prev_ahead) * prev / (prev - left_m(r));
      }
      prev = left_m(r);
      prev_ahead = ahead_m(r);
    }
    CHECK(crossed > 190 && crossed < 250, "side %d crosses at %.0f m", side, crossed);
  }
}

static void test_life(void) {
  printf("  a round flies ~600 m, then its slot is free\n");
  start(0);
  step(true);
  uint8_t r = _gun_last;
  double last = 0;
  int steps = 0;
  while (_round_age[r] != 0 && steps < 40) {
    last = ahead_m(r);
    step(false);
    ++steps;
  }
  CHECK(steps == kRoundLife, "lived %d steps", steps);
  CHECK(last > 500 && last < 650, "last seen %.0f m out", last);
  CHECK(_gun_last == kRoundNone, "still the newest round once gone");
}

// The world position of round r, from the eye and its offset.
static void world_of(uint8_t r, double w[3]) {
  w[0] = flight_eye_x / kHalfMetre / 2 + _round_p[r] / 16.0;
  w[1] = flight_eye_y / kHalfMetre / 2 + _round_p[kRoundMax + r] / 16.0;
  w[2] = flight_eye_z / kHalfMetre / 2 + _round_p[2 * kRoundMax + r] / 16.0;
}

static void test_world_frame(void) {
  printf("  rounds hold their world course while the eye moves\n");
  start(2400);
  step(true);
  uint8_t r = _gun_last;
  double w0[3], w1[3];
  world_of(r, w0);
  double v = (48 + 2400 * 2 / 512.0);  // muzzle plus the aircraft's metres a step
  for (int n = 1; n <= 10; ++n) {
    step(false);
    world_of(r, w1);
    CHECK(fabs(w1[0] - w0[0] - v * n) < 1.0, "after %d steps %.2f m on, not %.2f", n, w1[0] - w0[0],
          v * n);
  }
}

static void place_target(int i, double ahead, double left, double up) {
  _target_on[i] = true;
  _target[i][0] = flight_eye_x + (int32_t)(ahead * kMetre);
  _target[i][1] = flight_eye_y + (int32_t)(left * kMetre);
  _target[i][2] = flight_eye_z + (int32_t)(up * kMetre);
}

static void test_hits(void) {
  printf("  a target on the boresight is hit, one 30 m off is not\n");
  start(1500);
  place_target(0, 250, 0, -1.5);
  place_target(1, 300, 30, 0);
  // The targets fly along with the player, so the range holds.
  for (int n = 0; n < 40; ++n) {
    step(true);
    for (int i = 0; i < 2; ++i) _target[i][0] += vec_fastmul8p8(256, 1500 << 1);
  }
  CHECK(_strikes[0] >= kOpponentHitsToDown, "%d hits on the boresight", _strikes[0]);
  CHECK(_strikes[1] == 0, "%d hits 30 m off", _strikes[1]);

  // At a larger flight_step_shift a hit on a substep clears the tracer at once.
  start(0);
  flight_step_shift = 2;
  kFlightSubstepMask = 3;
  // Fire on the fourth substep, which completes a whole step.
  for (int n = 0; n < 4; ++n) step(true);
  CHECK(gun_tracer_up(), "no tracer after firing at shift 2");
  place_target(0, 25, 0, -1.5);
  step(false);
  CHECK(_strikes[0] == 1 && !gun_tracer_up(),
        "substep hit: strikes %d, tracer %d", _strikes[0], gun_tracer_up());
  flight_step_shift = 0;
  kFlightSubstepMask = 0;

  printf("  and head-on at full closing speed, no round jumps it\n");
  // kMaxSpeed against a target flying straight at the player at 1,500: the
  // round's step and the target's between them, at every offset from the
  // round's own path inside the box. The streams have crossed and part by
  // 500 m, so the target keeps to the round's line, `off` to one side.
  for (int off = 0; off <= 8; ++off) {
    start((int16_t)kMaxSpeed);
    place_target(0, 500, 0, 0);
    step(true);
    uint8_t r = _gun_last;
    for (int n = 0; n < 20; ++n) {
      if (_round_age[r] != 0) {
        _target[0][1] = flight_eye_y + (int32_t)((left_m(r) + off) * kMetre);
        _target[0][2] = flight_eye_z + (int32_t)(up_m(r) * kMetre);
      }
      _target[0][0] -= vec_fastmul8p8(256, 1500 << 1);
      step(false);
    }
    CHECK(_strikes[0] == 1, "offset %d m: %d hits", off, _strikes[0]);
  }
}

static void test_wrap(void) {
  printf("  a target across the world's x seam is still hit\n");
  start(1500);
  // The eye just south of the seam at x 0, the target one world (128
  // units) on: the same place.
  flight_eye_x = (128L << 16) - 50 * kMetre;
  reset_guns();
  place_target(0, 200, 0, -1.5);
  _target[0][0] += 128L << 16;
  for (int n = 0; n < 20; ++n) {
    step(n < 6);
    _target[0][0] += vec_fastmul8p8(256, 1500 << 1);
  }
  CHECK(_strikes[0] >= 1, "%d hits across the seam", _strikes[0]);
}

static void test_incoming(void) {
  printf("  an opponent's round into the player counts a hit on the player\n");
  start(1500);
  // By hand, as no opponent fires yet: 300 m ahead, coming straight back.
  uint8_t r = _gun_slot();
  _round_p[r] = 300 * 16;
  _round_p[kRoundMax + r] = 2 * 16;
  _round_p[2 * kRoundMax + r] = 0;
  _round_v[r] = -48 * 16;
  _round_v[kRoundMax + r] = 0;
  _round_v[2 * kRoundMax + r] = 0;
  _round_age[r] = 1;
  _round_owner[r] = 1;
  for (int n = 0; n < 12; ++n) step(false);
  CHECK(gun_player_hits == 1, "%d hits on the player", gun_player_hits);
}

static void test_dir(void) {
  printf("  the tracer's needle follows the line at all 16 angles\n");
  for (int k = 0; k < 32; ++k) {
    double a = k * M_PI / 16;
    // y down: clockwise from up is +x, -y.
    int16_t dx = (int16_t)lround(100 * sin(a)), dy = (int16_t)lround(-100 * cos(a));
    uint8_t got = _gun_dir(dx, dy);
    CHECK(got == (k & 15), "angle %d: needle %d", k, got);
    // Scaled down past the fold, and up past its 128 limit, the same.
    CHECK(_gun_dir(dx / 10, dy / 10) == (k & 15) || k % 4 != 0, "angle %d, small", k);
    CHECK(_gun_dir(dx * 3, dy * 3) == (k & 15), "angle %d, large", k);
  }
}

static void test_tracer(void) {
  printf("  the newest round is drawn: from a wing, low, stretched, yellow\n");
  start(1500);
  _offers = 0;
  gun_add_sprites();
  CHECK(_offers == 0, "a tracer with no round in flight");
  step(true);
  gun_add_sprites();
  CHECK(_offers == 1, "%d offers for one round", _offers);
  // 20 m ahead, 4 m left and 1.5 m down: ~51 px left of the centre, ~19 below.
  CHECK(_offer.x > 160 - 60 && _offer.x < 160 - 40 && _offer.y > 56 + 12 && _offer.y < 56 + 25,
        "first round drawn at %d, %d", _offer.x, _offer.y);
  CHECK(_offer.flags == kSpriteFlagExpandX && _offer.bitmap >= kSpriteDefLongArmBitmap &&
            _offer.bitmap < kSpriteDefLongArmBitmap + 16,
        "near round flags %d bitmap %d", _offer.flags, _offer.bitmap);
  CHECK(_offer.color == kColorYellow, "colour %d", _offer.color);
  CHECK(_offer.px == kSpriteDefArmCenterX && _offer.py == kSpriteDefArmCenterY,
        "pivot %d, %d", _offer.px, _offer.py);
  // It runs in and up towards the middle: from the lower left, up and right,
  // so between 0 and 90 degrees clockwise from up - but expanded, the
  // bitmap's own angle is steeper.
  uint8_t dir = (uint8_t)(_offer.bitmap - kSpriteDefLongArmBitmap);
  CHECK(dir >= 1 && dir <= 7, "first round's needle %d", dir);
  int16_t x0 = _offer.x;
  step(true);
  step(true);
  gun_add_sprites();
  CHECK(_offer.x > 160 + 40, "second round, the right wing's, drawn at %d", _offer.x);
  (void)x0;

  printf("  and far off it is the short needle, unexpanded\n");
  start(0);
  step(true);
  for (int n = 0; n < 8; ++n) step(false);
  gun_add_sprites();
  CHECK(_offer.flags == 0 && _offer.bitmap >= kSpriteDefShortArmBitmap,
        "far round flags %d bitmap %d", _offer.flags, _offer.bitmap);

  printf("  near or far, the needle follows the round's motion on screen\n");
  // The exact direction, from the round's position and velocity in metres
  // (level and north, so camera space is world space), against the needle
  // drawn, at every age of a round from each wing: within one of the 16
  // steps. Not from the difference of two whole-pixel positions, which a
  // far round barely moves, and which once stood its needle upright.
  for (int side = 0; side < 2; ++side) {
    start(1500);
    _gun_side = (uint8_t)side;
    step(true);
    uint8_t r = _gun_last;
    for (int age = 0; _round_age[r] != 0; ++age) {
      _offers = 0;
      gun_add_sprites();
      if (_offers == 1) {
        double x = ahead_m(r), y = left_m(r), z = up_m(r);
        double vx = _round_v[r] / 16.0, vy = _round_v[kRoundMax + r] / 16.0,
               vz = _round_v[2 * kRoundMax + r] / 16.0;
        double dx = y * vx - vy * x, dy = z * vx - vz * x;
        bool expanded = _offer.flags & kSpriteFlagExpandX;
        if (expanded) dx /= 2;
        double a = atan2(dx, -dy) * 180 / M_PI;  // clockwise from up
        int want = (int)lround(fmod(a + 360, 180) / 11.25) % 16;
        int base = _offer.bitmap >= kSpriteDefShortArmBitmap ? kSpriteDefShortArmBitmap
                                                             : kSpriteDefLongArmBitmap;
        int got = _offer.bitmap - base;
        int diff = (got - want + 16) % 16;
        CHECK(diff <= 1 || diff >= 15, "side %d age %d, %.0f m out: needle %d, not %d", side,
              age, x, got, want);
      }
      step(false);
    }
  }

  printf("  looking back, the player's rounds are not drawn\n");
  start(1500);
  step(true);
  world_cam.front = make_vector(-256, 0, 0);
  world_cam.left = make_vector(0, -256, 0);
  _offers = 0;
  gun_add_sprites();
  CHECK(_offers == 0, "a round behind the camera drawn");
}

static void test_ground(void) {
  printf("  rounds stop at the ground\n");
  // 10 m up, nose 20 degrees down: the rounds go into the ground ~30 m out.
  start(1500);
  flight_eye_z = 20L << 8;
  reset_guns();
  flight_cam.front = make_vector(241, 0, -88);
  flight_cam.up = make_vector(88, 0, 241);
  world_cam = flight_cam;
  step(true);
  uint8_t r = _gun_last;
  int steps = 0;
  while (_round_age[r] != 0 && steps < 20) {
    CHECK(flight_eye_z / 512.0 + up_m(r) > -15, "round %.1f m below the ground",
          -(flight_eye_z / 512.0 + up_m(r)));
    step(false);
    ++steps;
  }
  CHECK(steps < 4, "a round into the ground flew %d steps", steps);
  // Level, a round 10 m up flies its whole life.
  start(1500);
  flight_eye_z = 20L << 8;
  reset_guns();
  step(true);
  r = _gun_last;
  for (steps = 0; _round_age[r] != 0 && steps < 20; ++steps) step(false);
  CHECK(steps == kRoundLife, "a level round 10 m up flew %d steps", steps);
}

static void test_smoke(void) {
  printf("  smoke: one black cloud blob, 25 m behind the smoking aircraft\n");
  start(1500);
  _offers = 0;
  gun_add_sprites();
  CHECK(_offers == 0, "smoke with none up");
  // 200 m ahead, flying away north: its smoke is 175 m out, 4 m up.
  _smoke_on = true;
  _smoke_p[0] = flight_eye_x + 200 * kMetre;
  _smoke_p[1] = flight_eye_y;
  _smoke_p[2] = flight_eye_z;
  _smoke_h[0] = 256;
  _smoke_h[1] = 0;
  gun_add_sprites();
  CHECK(_offers == 1, "%d offers for the smoke", _offers);
  CHECK(_offer.color == kColorBlack &&
            _offer.flags == (kSpriteFlagExpandX | kSpriteFlagAlignDither),
        "smoke colour %d flags %d", _offer.color, _offer.flags);
  CHECK(_offer.depth > 170 && _offer.depth < 180, "smoke at depth %d", _offer.depth);
  // The largest single-sprite rung inside ~186 m.
  CHECK(_offer.bitmap == kSpriteDefCloudRung[4].bitmap, "smoke bitmap %d", _offer.bitmap);
  CHECK(_offer.x == 160 && _offer.y < 56, "smoke drawn at %d, %d", _offer.x, _offer.y);
  // 600 m out, the smallest; and with a tracer as well, two offers.
  _smoke_p[0] = flight_eye_x + 625 * kMetre;
  step(true);
  _offers = 0;
  gun_add_sprites();
  CHECK(_offers == 2 && gun_tracer_up(), "%d offers for smoke and a tracer", _offers);
}

static void test_armed(void) {
  printf("  the trigger held from the start fires nothing until let go\n");
  start(1500);
  gun_reset();
  for (int n = 0; n < 4; ++n) step(true);
  CHECK(_gun_last == kRoundNone, "fired with the trigger held since the start");
  step(false);
  step(true);
  CHECK(_gun_last != kRoundNone, "no round once let go and pressed again");
}

static void test_eye(void) {
  printf("  the rounds' eye keeps up with flight_eye_* on every axis\n");
  start(2400);
  flight_vspeed = 300;
  flight_cam.front = make_vector(181, 181, 0);
  flight_cam.left = make_vector(-181, 181, 0);
  for (int n = 0; n < 50; ++n) {
    step(n % 3 == 0);
    int32_t e[3] = {flight_eye_x, flight_eye_y, flight_eye_z};
    for (int c = 0; c < 3; ++c) {
      int32_t d = e[c] - _gun_eye[c];
      CHECK(d >= 0 && d < 32, "step %d axis %d: the rounds' eye %ld behind", n, c, (long)d);
    }
  }
}

static void test_paused(void) {
  printf("  paused or crashed, nothing moves and nothing fires\n");
  start(1500);
  flight_paused = true;
  step(true);
  CHECK(_gun_last == kRoundNone, "fired while paused");
  flight_paused = false;
  flight_status = FLIGHT_CRASH_ROLL;
  step(true);
  CHECK(_gun_last == kRoundNone, "fired after a crash");
}

int main() {
  printf("Running gun_test...\n");
  test_wings();
  test_converge();
  test_life();
  test_world_frame();
  test_hits();
  test_wrap();
  test_incoming();
  test_dir();
  test_tracer();
  test_ground();
  test_smoke();
  test_armed();
  test_eye();
  test_paused();
  if (_fails) {
    printf("gun_test: %d checks FAILED\n", _fails);
    return 1;
  }
  printf("gun_test: passed\n");
  return 0;
}
