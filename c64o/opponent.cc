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
#include "gun.h"
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

// Two slots (opponent.h kOpponentMax), each with every sprite it can use: two
// sets of four blocks fill $D000-$D3FF, and the dot has a block of its own
// below it (mem.h). The rendering loop below takes them in turns, which is
// written for two.

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

// Opponents are drawn bigger than the model: kn 156 against its own 64, so
// the model's 8 m span (docs/planes.md §1) draws as ~20 m, and the 128 m
// runway is ~6.4 spans across. Every distance the size sets goes out with
// it, by 2.4: a silhouette rather than the dot from ~1.2 km, not ~500 m, and
// back to the dot past ~1.7 km. A multiple of four for kViewFar.
static const uint8_t kOpponentKn = 156;
static_assert(kOpponentKn % 4 == 0, "kViewFar divides it by four");

// The view an aircraft is drawn in: the viewport's centre and the last line a
// sprite may start on, plain and Y-expanded (mem.h), with the cap and the
// numerator _opponent_set_view() picks for each slot. One view written over
// rather than three that differ in those two.
static planes_view_t _opponent_view = {kScreenWidthPixels / 2,
                                       kViewportEndYPixels / 2,
                                       kSpriteVisibleEndYPixels - 1,
                                       kViewportEndYPixels -
                                           kSpritesOffLeadExpandY - 1,
                                       kPlaneKMax,
                                       kOpponentKn};
static_assert(kSpriteVisibleEndYPixels - 1 == 89 &&
                  kViewportEndYPixels - kSpritesOffLeadExpandY - 1 == 68,
              "the DMA cuts moved: check planes.h's description of them");

// kView: the model's own size cap, so the aircraft grows to 2 x 2 sprites
// and holds its size from ~66 m.
//
// kViewTwo (`two`): held to two sprites at every attitude, so it stops
// growing at ~115 m (planes.h kPlaneKMaxTwoSprites). For an aircraft that
// has to share the stack's seven sprites with another (_opponent_cap()).
// Farther than ~115 m it draws exactly what kView does, k never reaching
// either cap, so it can be handed to an aircraft at range for nothing - not
// even a redraw.
//
// kViewFar (`far`): for an aircraft from 16 units, ~2 km, out. Its
// camera-space position is in half metres rather than eighth metres
// (_opponent_camera()), so a quarter of the numerator gives the same k, and
// the same picture - the dot, or a silhouette the size of one. The dot needs
// no cap; two sprites' is the safe one.
static inline void _opponent_set_view(bool far, bool two) {
  _opponent_view.kmax = far || two ? kPlaneKMaxTwoSprites : kPlaneKMax;
  _opponent_view.kn = far ? kOpponentKn / 4 : kOpponentKn;
}

// --- Paths ------------------------------------------------------------------
//
// Each aircraft holds its path's height, and in level flight its path's
// speed (the flight model below). It flies straight at its next waypoint,
// and turns at one fixed rate when the waypoint is off its nose. Once the
// waypoint is within kOpponentReach it heads for the next. After the last, a
// loop starts again at the first and a one-way path's aircraft is gone: it has
// left the map. It steers at a point, not along the line between two: reacting
// to the player would only change the point.
//
// Positions in 8.8 world units, 128 m each, so that flight_eye_x = x << 8;
// the height likewise, flight_eye_z = z << 8. x is north and +y west, as
// everywhere. No leg, the start to the first waypoint included, may be 64
// units or more along either axis (_opponent_toward()).
enum opponent_kind_t {
  kOpponentNone = 0,
  kOpponentLoop,
  kOpponentOneWay,
  // On the ground where it starts, and stays: no waypoints, no airspeed.
  kOpponentParked,
};
// opponent_path_t::kind holds the above in its low two bits, and above them
// the aircraft's role, its behaviour (docs/opponent_ai.md §3) and its starting
// heading. kOpponentNone is the whole byte 0: a slot with no aircraft has none
// of them.
static const uint8_t kOpponentKindMask = 0x03;
// The role: what the aircraft does once it attacks. A fighter goes for the
// player with its forward guns; a bomber keeps its path, and its rear gunner
// fires - whether it attacks or not.
static const uint8_t kOpponentFighter = 0 << 2;
static const uint8_t kOpponentBomber = 1 << 2;
// The behaviour: when it attacks. Never on a path; once fired upon or once
// the player is within kOpponentSpot, provoked; from the start, hunting; or
// after its last waypoint, a pass.
static const uint8_t kOpponentPath = 0 << 3;
static const uint8_t kOpponentProvoked = 1 << 3;
static const uint8_t kOpponentHunt = 2 << 3;
static const uint8_t kOpponentPass = 3 << 3;
static const uint8_t kOpponentBehaviourMask = 3 << 3;
// Bit 5: it gives up, and is gone, once it is attacking and the player is
// more than kOpponentGiveUp off on either axis across the ground - 09's
// hunters, outrun, and 07's patrols, evaded.
static const uint8_t kOpponentStray = 1 << 5;
// The heading it starts on, level and always along an axis: quarter turns
// left of north, as fmission.cc's kMissionStartTurns, in the top two bits.
static const uint8_t kOpponentHeadShift = 6;
static const uint8_t kOpponentNorth = 0 << kOpponentHeadShift;
static const uint8_t kOpponentWest = 1 << kOpponentHeadShift;
static const uint8_t kOpponentSouth = 2 << kOpponentHeadShift;
static const uint8_t kOpponentEast = 3 << kOpponentHeadShift;
// No waypoint fails the mission.
static const uint8_t kOpponentNoFail = 0xFF;
// The navpoint of the missions' WP_TARGETS_DOWN waypoint, which follows the
// targets: navpoint 1. Every mission has it first, then home (fmission.cc),
// and flight.cc names them TARGET and HOME BASE by number for the same reason
// (test/fpilot_test.cc checks the order).
static const uint8_t kOpponentNav = 0;
// What a pair's two aircraft share, a row of kOpponentPairs: the number of
// waypoints, the one that fails the mission and the airspeed. Both slots'
// paths are alike in them, so they are stored once.
struct opponent_pair_t {
  uint8_t count;
  // The waypoint whose reaching fails the mission, or kOpponentNoFail.
  uint8_t fail;
  // Airspeed in the model's units (flight.md): the player flies level at
  // 1,476 on half throttle and ~2,500 on full below 4,000 ft, 2,350 at
  // 5,000; it climbs at 1,450-1,600 and stalls at 1,024.
  int16_t speed;
};
// And what is each slot's own.
struct opponent_path_t {
  // opponent_kind_t, role, behaviour and starting heading.
  uint8_t kind;
  // Its first waypoint's index in kOpponentWpX/Y: a path's waypoints are
  // there in order, its pair's count of them, so no path pays for a longer
  // one's.
  uint8_t wp;
  uint16_t start_x, start_y;
  uint16_t z;
};

// Path p's starting heading, into h: the level unit vector, 8.8, of the
// quarter turns in its kind's top bits.
static inline void _opponent_start_head(const opponent_path_t *p, vec3_t *h) {
  uint8_t t = p->kind >> kOpponentHeadShift;
  h->x = kMissionQuarterCos[t] << 8;
  h->y = kMissionQuarterCos[t + 3] << 8;
  // z stays the 0 _opponent_send() cleared it to: nothing sets it.
}

// Heights on flight.cc's scale, 1,000 ft to 0x0200; the ground is at 0.
static const uint16_t kOpponent5000Ft = 0x0A00;
static const uint16_t kOpponent3000Ft = 0x0600;
static const uint16_t kOpponent3500Ft = 0x0700;
static const uint16_t kOpponent2000Ft = 0x0400;

// The missions (fmission.cc). Runway 1, home, is at (0x20, 0x3F), runway 2 at
// (0x60, 0xBF) and the city at (0x10, 0x68). Each puts its targets on screen
// within 10 s of the start - 20 s of the lift-off in 02 - and the player
// within 1 km of them inside ~90 s (test/mission_test.cc), but for those
// that come from behind (04, 09, 10) or are to be avoided (07).
//
// 01, Recon: a bomber, so brown when near, on a path, whose rear gunner
// fires at a player on its tail. At 5,000 ft and 1,750, faster than the player
// climbs and slower than it flies level up there. It comes in from the north
// edge head-on to the player, 2,500 ft below, passing 64 m to one side after
// ~45 s; then on south over the city at ~5.4 min and out over the south edge at
// ~6.2 min, gone with the mission failed. The climb is the challenge: in level
// flight the recon is on screen, as the dot, only from 2.6 s to 19 s, when it
// rises out of the top of the viewport 2.45 km off. A pilot who turns after it
// at the pass and only then climbs is within 250 m of it at ~3.8 min, 2.4 min
// before it leaves (test/mission_test.cc). 2,000 at first, from the runway,
// which play found too hard.
//
// 02, Bombers: two at 1,500, in loose formation - the second 75 m to the
// left of, 100 m behind and 500 ft above the first, at 3,500 ft to its
// 3,000 - come south towards
// runway 1 from 5.5 km as the player scrambles from it, pass 500 m north of
// it, turn for the city and reach it after ~4.4 min, which fails the
// mission. Then they leave over the south edge. Their rear gunners fire
// at a player behind them from the start; from more than ~10 degrees below
// they cannot.
//
// 03, Ambush: two on the ground beside the south end of runway 2, side by
// side 100 m apart, both 75 m off its west edge at y 0xC0 and facing it, and
// the player coming in from the west between them, 1.8 km out. Their
// centres are 5 m up, about where a ~20 m aircraft's (kOpponentKn) is on
// its wheels.
//
// 04, Scramble: a pass, then a hunt, from the player's start on runway 1.
// Both at 2,000, as fast as 05's hunters, and 48 m up: behind the player
// parked on runway 1, low and level and heading north, so they overtake
// alongside the runway. The first 200 m behind the player's 0x1C and 48 m
// left of its centreline at 0x3F and a half - 32 m beyond the runway's left
// edge; the second on the other side, 48 m right of it, and 384 m behind.
// Each flies on north to its one waypoint, 8 units from its start and
// ~600 m past the player, ~16 s after the start, and there turns back for
// the player, following and shooting at it: strafing it if it is still on
// the ground, ~50 s after the start. 1,500 and ~2.5 km out at first, which
// took them ~90 s to turn and left a player who had taken off and flown
// north far ahead, never to be seen again.
//
// 05, Head on: two fighters hunting from the start, at 2,000, side by side
// 128 m apart, 2.3 km north of the player and 50 m above its 3,000 ft,
// heading south at it. No waypoints: hunters steer for the player.
//
// 04 and 05 send a second pair once the first is down: 04's from the west,
// 4 km off the field at 2,000 ft heading east, and 05's from the south, at
// 3,000 ft heading north; both hunt from the start.
//
// 06, Big week: a bomber pair three times over (kOpponentWaves), on 02's
// last two waypoints. They come south at 1,750 from 3.3 km north of the city,
// 02's formation, cross it after ~30 s and leave over the south edge after
// ~80 s. The player starts 2.6 km south of them, 320 m to the side and
// 1,000 ft under them, heading north. Half of them, three, must go down.
//
// 07, Way home: two patrols in turn, each a pair in trail at 1,700 on a line
// it flies back and forth, Provoked: attacking once fired upon or once the
// player is within kOpponentSpot, and giving up out of kOpponentGiveUp
// (kOpponentStray). The first across the way home from runway 2's side, 6 km
// of x 0x48, the second 1.2 km west of runway 1, on 4 km of y 0x48 - where
// the approach passes it. Flown straight home, the first sees the player at
// ~1 min and gives up at ~3.7, and the second sees it on the approach. The
// goal is to land: fmission.cc starts it at that.
//
// 08, Turkey shoot: a fighter pair in trail, 400 m apart, flying runway 2's
// circuit, 4 km by 1.8 km, at 1,000 ft and 1,600, on a path they never
// leave: three pairs in turn, five of the six to go down. 60 rounds are not
// enough for 25 hits; landing on runway 2 rearms.
//
// 09, Unarmed escape: a pair hunting from either side, 1 km north and 1 km
// south of the player and 2 km ahead of it, the one heading south and the
// other north, at 1,800 against its 2,176 and full throttle's ~2,500. They
// turn in across its way: flown straight home at full throttle it takes one
// to four hits, at cruise doing nothing it is shot down. Outrun by
// kOpponentGiveUp they give up: ~2.7 min at full throttle, under a minute
// short of home. Farther than that ahead, they would give up at the start.
// The player has no rounds (gun.cc). *Until 2026-11-07* the pair started
// 450 m behind it, inside their guns' reach, and hit it at once.
//
// 10, Combat patrol: at 6,000 ft, a pair at 2,400 hunting from 1.2 km behind:
// just under full throttle's ~2,500, as the arcade flight model has no dive
// past it to extend in. At 2,700 they caught a player running from them.
//
// The pairs are rows, of kOpponentPairs and kOpponentPaths alike, and
// kOpponentWaves says which each mission sends.
static const uint8_t kOpponentRows = 13;
static const opponent_pair_t kOpponentPairs[kOpponentRows] = {
    {4, 3, 1750},               // 0: 01's recon.
    {3, 1, 1500},               // 1: 02's bombers.
    {0, kOpponentNoFail, 0},    // 2: 03's parked pair.
    {1, kOpponentNoFail, 2000}, // 3: 04's pass.
    {0, kOpponentNoFail, 2000}, // 4: 04's second pair, from the west.
    {0, kOpponentNoFail, 2000}, // 5: 05's head-on pair.
    {0, kOpponentNoFail, 2000}, // 6: 05's second pair, from the south.
    {2, kOpponentNoFail, 1750}, // 7: 06's bombers.
    {2, kOpponentNoFail, 1700}, // 8: 07's first patrol.
    {2, kOpponentNoFail, 1700}, // 9: 07's second patrol.
    {4, kOpponentNoFail, 1600}, // 10: 08's square.
    {0, kOpponentNoFail, 1800}, // 11: 09's hunters.
    {0, kOpponentNoFail, 2400}, // 12: 10's hunters.
};
static const opponent_path_t kOpponentPaths[kOpponentRows][kOpponentMax] = {
    // 0: 01's recon.
    {{kOpponentOneWay | kOpponentBomber | kOpponentPath | kOpponentSouth, 0,
      0x7E00, 0x6000, kOpponent5000Ft},
     {kOpponentNone}},
    // 1: 02's bombers.
    {{kOpponentOneWay | kOpponentBomber | kOpponentHunt | kOpponentSouth, 4,
      0x4800, 0x4000, kOpponent3000Ft},
     {kOpponentOneWay | kOpponentBomber | kOpponentHunt | kOpponentSouth, 7,
      0x48C8, 0x3F6A, kOpponent3500Ft}},
    // 2: 03's parked pair.
    {{kOpponentParked | kOpponentFighter | kOpponentPath | kOpponentEast, 0,
      0x5C9C, 0xC096, 0x000A},
     {kOpponentParked | kOpponentFighter | kOpponentPath | kOpponentEast, 0,
      0x5D64, 0xC096, 0x000A}},
    // 3: 04's pass.
    {{kOpponentOneWay | kOpponentFighter | kOpponentPass | kOpponentNorth, 10,
      0x1A70, 0x3FE0, 0x0060},
     {kOpponentOneWay | kOpponentFighter | kOpponentPass | kOpponentNorth, 11,
      0x1900, 0x3F20, 0x0060}},
    // 4: 04's second pair, from the west.
    {{kOpponentLoop | kOpponentFighter | kOpponentHunt | kOpponentEast, 0,
      0x2400, 0x6000, kOpponent2000Ft},
     {kOpponentLoop | kOpponentFighter | kOpponentHunt | kOpponentEast, 0,
      0x2400, 0x6100, kOpponent2000Ft}},
    // 5: 05's head-on pair.
    {{kOpponentLoop | kOpponentFighter | kOpponentHunt | kOpponentSouth, 0,
      0x4200, 0x9000, 0x0660},
     {kOpponentLoop | kOpponentFighter | kOpponentHunt | kOpponentSouth, 0,
      0x4200, 0x9100, 0x0660}},
    // 6: 05's second pair, from the south.
    {{kOpponentLoop | kOpponentFighter | kOpponentHunt | kOpponentNorth, 0,
      0x1C00, 0x9000, 0x0660},
     {kOpponentLoop | kOpponentFighter | kOpponentHunt | kOpponentNorth, 0,
      0x1C00, 0x9100, 0x0660}},
    // 7: 06's bombers, on 02's last two waypoints.
    {{kOpponentOneWay | kOpponentBomber | kOpponentPath | kOpponentSouth, 5,
      0x1A00, 0x6800, kOpponent3000Ft},
     {kOpponentOneWay | kOpponentBomber | kOpponentPath | kOpponentSouth, 8,
      0x1AC8, 0x676A, kOpponent3500Ft}},
    // 8: 07's first patrol.
    {{kOpponentLoop | kOpponentFighter | kOpponentProvoked | kOpponentStray |
          kOpponentWest,
      12, 0x4800, 0x7400, kOpponent3000Ft},
     {kOpponentLoop | kOpponentFighter | kOpponentProvoked | kOpponentStray |
          kOpponentWest,
      12, 0x4800, 0x7200, kOpponent3000Ft}},
    // 9: 07's second patrol.
    {{kOpponentLoop | kOpponentFighter | kOpponentProvoked | kOpponentStray |
          kOpponentSouth,
      14, 0x3800, 0x4800, kOpponent2000Ft},
     {kOpponentLoop | kOpponentFighter | kOpponentProvoked | kOpponentStray |
          kOpponentSouth,
      14, 0x3A00, 0x4800, kOpponent2000Ft}},
    // 10: 08's square.
    {{kOpponentLoop | kOpponentFighter | kOpponentPath | kOpponentNorth, 16,
      0x6000, 0xB800, 0x0200},
     {kOpponentLoop | kOpponentFighter | kOpponentPath | kOpponentNorth, 16,
      0x5D00, 0xB800, 0x0200}},
    // 11: 09's hunters, from the north and from the south.
    {{kOpponentLoop | kOpponentFighter | kOpponentHunt | kOpponentStray |
          kOpponentSouth,
      0, 0x2800, 0x9000, kOpponent2000Ft + 0x20},
     {kOpponentLoop | kOpponentFighter | kOpponentHunt | kOpponentStray |
          kOpponentNorth,
      0, 0x1800, 0x9000, kOpponent2000Ft + 0x20}},
    // 12: 10's hunters.
    {{kOpponentLoop | kOpponentFighter | kOpponentHunt | kOpponentNorth, 0,
      0x3700, 0xA000, 0x0C40},
     {kOpponentLoop | kOpponentFighter | kOpponentHunt | kOpponentNorth, 0,
      0x3700, 0xA100, 0x0C40}},
};

// The waves: each mission's rows in the order sent, one byte a pair - its
// row - with kOpponentLastWave on the mission's last, and on its first the
// aircraft that must go down, kOpponentNeedShift up, where it is not all of
// them (opponent_targets_down()). The next pair comes once none of the last
// is flying or falling and its smoke is gone (opponent_step()).
static const uint8_t kOpponentLastWave = 0x80;
static const uint8_t kOpponentNeedShift = 4;
static const uint8_t kOpponentRowMask = 0x0F;
static const uint8_t kOpponentWaves[] = {
    0 | kOpponentLastWave, // 01
    1 | kOpponentLastWave, // 02
    2 | kOpponentLastWave, // 03
    3,
    4 | kOpponentLastWave, // 04
    5,
    6 | kOpponentLastWave, // 05
    7 | 3 << kOpponentNeedShift,
    7,
    7 | kOpponentLastWave, // 06
    8,
    9 | kOpponentLastWave, // 07
    10 | 5 << kOpponentNeedShift,
    10,
    10 | kOpponentLastWave, // 08
    11 | kOpponentLastWave, // 09
    12 | kOpponentLastWave, // 10
};
static_assert(kOpponentRows <= kOpponentRowMask + 1, "a row fits its bits");

// Every path's waypoints, at its opponent_path_t::wp: 01's recon, 02's two
// bombers - whose last two each 06's take - 04's one each, 07's patrols' two
// each and 08's square. A pair in trail shares its leader's.
// test/opponent_test.cc appends its own paths' after the missions', through
// OPPONENT_TEST_WP_N/X/Y; nothing else does.
static const uint8_t kOpponentWpCount = 20;
#ifndef OPPONENT_TEST_WP_N
#define OPPONENT_TEST_WP_N 0
#define OPPONENT_TEST_WP_X
#define OPPONENT_TEST_WP_Y
#endif
__striped static const uint16_t
    kOpponentWpX[kOpponentWpCount + OPPONENT_TEST_WP_N] = {
        0x5C00, 0x3000, 0x1000, 0x0000, 0x2800,
        0x1000, 0x0000, 0x28C8, 0x10C8, 0x00C8,
        0x2270, 0x2100, 0x4800, 0x4800, 0x1800,
        0x3800, 0x7000, 0x7000, 0x5000, 0x5000 OPPONENT_TEST_WP_X};
__striped static const uint16_t
    kOpponentWpY[kOpponentWpCount + OPPONENT_TEST_WP_N] = {
        0x6000, 0x6400, 0x6800, 0x6800, 0x4000,
        0x6800, 0x6800, 0x3F6A, 0x676A, 0x676A,
        0x3FE0, 0x3F20, 0x9000, 0x6000, 0x4800,
        0x4800, 0xB800, 0xC600, 0xC600, 0xB800 OPPONENT_TEST_WP_Y};

// What the message says when the mission fails: 01's recon gets away, or
// the city is bombed - 02's reach it, or too few of 06's go down.
static const char *const kOpponentFailText[2] = {
    "MISSION FAILED - RECON ESCAPED",
    "MISSION FAILED - CITY BOMBED",
};

// Within this of the player on both axes across the ground, in half metres,
// a Provoked aircraft sees it and attacks: 2 km. Farther than this on
// either, one that strays gives up: 2.5 km.
static const uint16_t kOpponentSpot = 4000;
static const uint16_t kOpponentGiveUp = 5000;

// The turn: vec_turn3_xy()'s small angle, 8 / 256 rad, 1.8 degrees a model
// step at flight_step_shift 0. At 5.9 m a step that is a turn of ~190 m
// radius, a quarter circle in 50 steps. At a larger shift the same angle
// once every 1 << shift steps, not a smaller one every step: below 8 the
// angle's product with a small component truncates to nothing, and the turn
// would come out slower the faster the machine. A fighter in ATTACK turns
// twice as fast, `turn` 2 rather than 1: a turn of about half the radius.
static const int16_t kOpponentTurn = 8;
// Closer than this on both axes, in half metres, and the waypoint is
// reached. No less than the turn's radius: a waypoint inside the circle the
// aircraft is turning on would be circled for ever, and every such circle
// passes within a radius of it.
static const int16_t kOpponentReach = 512;
// --- The flight model (docs/opponent_ai.md §4) -----------------------------
//
// Level, or climbing or descending at one of three slopes, a level each:
// 1/8, 1/4 and 1/2, ~7, ~14 and ~27 degrees, so that the climb rate is the
// speed shifted. The speed keeps to a range by role, and is pulled back to
// the path's speed, the default, by kOpponentLevelAccel a step - the thrust
// and the drag - while a climb takes kOpponentClimbCost off it at level 1,
// twice that a level steeper, and a descent adds as much: gravity. Level 1
// is therefore sustained, settling a step's cost below or above the
// default; the steeper levels run the speed to the end of its range, where
// too slow it cannot climb, nor too fast descend.
//
// The costs are about g sin(slope) at 6.25 steps a second, the speed's
// units being ~41 to 1 m/s: 1.2 m/s2 at level 1 is 8 a step.
static const int16_t kOpponentClimbCost = 8;
static const int16_t kOpponentLevelAccel = 8;
// By climb level + 3, -3 to 3: what a whole step of it does to the speed;
// -4 times this is the 8.8 slope.
static const int8_t kOpponentClimbSpeed[7] = {
    4 * kOpponentClimbCost, 2 * kOpponentClimbCost,  kOpponentClimbCost,     0,
    -kOpponentClimbCost,    -2 * kOpponentClimbCost, -4 * kOpponentClimbCost};
static_assert(kOpponentClimbCost == 8, "8.8 slope is -4 * kOpponentClimbSpeed");
// Within this of its path's height, in half metres, an aircraft on its path
// flies level: 20 m.
static const int16_t kOpponentPathBand = 40;
// The speed's range by role (kOpponentFighter, kOpponentBomber >> 2). The
// fighter's floor is above the player's 1,024 stall.
__striped static const uint16_t kOpponentMinSpeed[2] = {1300, 1100};
__striped static const uint16_t kOpponentMaxSpeed[2] = {2800, 2000};
static_assert(kOpponentClimbCost % kOpponentLevelAccel == 0,
              "the speed moves in whole kOpponentLevelAccel (_opponent_fly())");

// The pitch drawn by climb level + 3: the sine and cosine of the slope's
// angle, atan(1/8), atan(1/4) and atan(1/2), 8.8. Level's cosine (256) is
// unused: _opponent_axes() skips _opponent_rot() there.
// And after them, kOpponentDive's: ~37 degrees nose down, shot down - a
// slope of 3/4, the one _opponent_fall() descends at, so sine 0.6 and
// cosine 0.8.
static const int16_t kOpponentPitchSin[8] = {-114, -62, -32, 0,
                                             32,   62,  114, -154};
static const uint8_t kOpponentPitchCos[8] = {229, 248, 254, 0,
                                             254, 248, 229, 205};
static const int8_t kOpponentDive = 4;

// The bank while turning, drawn only, by the turn + 2: cos and sin of 30
// degrees for a path's turn, 60 for a hunter's, 8.8. The sine is the one
// _opponent_rot() takes, turning left and up towards the turn; wings-level's
// cosine (256) is unused.
static const uint8_t kOpponentBankCos[5] = {128, 222, 0, 222, 128};
__striped static const int16_t kOpponentBankSin[5] = {222, 128, 0, -128, -222};

// ATTACK's floors (docs/opponent_ai.md §5), in half metres: a hunter climbs
// back above kOpponentFloor, and stops descending within kOpponentFloorMargin
// of it - more than a step's steepest descent, ~11 at the fighter's
// greatest speed - so it never goes under.
static const int16_t kOpponentFloor = 120;
static const int16_t kOpponentFloorMargin = 16;
// The temporary floor of a strafing run at a player on the ground, 30 m.
static const int16_t kOpponentStrafeFloor = 60;

// The guns (docs/opponent_ai.md §6): a fighter in ATTACK fires a burst of
// three rounds, one every second whole step, from the start of each
// kOpponentGunCycle whole steps that finds the player in front of its nose:
// ~0.8 s of fire and ~3.4 s to the next burst. A bomber's rear gunner keeps
// the same cycle, and fires its burst's rounds at a player behind it.
static const uint8_t kOpponentGunCycle = 26;
// The rear gunner's reach, in half metres on each axis: ~600 m.
static const int16_t kOpponentGunnerReach = 1200;
// Every round's scatter, in the 8.8 of the direction it is fired along, 4
// being ~0.9 degrees: by three random bits for x and for y, so that across
// the line of fire it is up to ~6 degrees, and two for z. At 300 m about
// one round in eight lands, closer in more (test/mission_test.cc).
__striped static const int16_t kOpponentScatter[8] = {-28, -20, -12, -4,
                                                      4,   12,  20,  28};

// The pixel size, planes_frame_t::d, from which an aircraft is drawn in
// kColorOpponentNear: about 620 m at kOpponentKn, and below
// kOpponentNearD - 1, ~720 m, back to kColorOpponentFar.
static const uint8_t kOpponentNearD = 8;

// Smoke: one sprite, behind the last aircraft hit, which gun.cc draws
// (docs/projectiles.md §12). kOpponentNoSmoke when none has been: 0, so that
// the zeroed bss means none.
static const uint8_t kOpponentNoSmoke = 0;
// Frames the smoke lasts: ~2 s after a hit, ~5 s where a downed aircraft
// fell, at the 6-8 a second a stock C64 renders. Frames rather than model
// steps, so a faster machine shows it for less time, not for more frames.
static const uint8_t kSmokeHit = 14;
static const uint8_t kSmokeDown = 35;

// --- State ------------------------------------------------------------------

#pragma bss(bss2)
static bool _opponent_on[kOpponentMax];
static const opponent_path_t *_opponent_path[kOpponentMax];
// Both slots' row of kOpponentPairs: a pair is sent whole.
static const opponent_pair_t *_opponent_pair;
// The mission, whether it has failed, and bit i: slot i was shot down, after
// kOpponentHitsToDown hits (gun.cc). A downed slot is neither drawn nor a
// target.
static uint8_t _opponent_mission;
static bool _opponent_failed;
// The mission's next pair, its index in kOpponentWaves; whether the pair up
// is its last; the aircraft that must go down, or 0 for every one it sends;
// and how many have.
static uint8_t _opponent_wave;
static bool _opponent_last;
static uint8_t _opponent_need;
static uint8_t _opponent_kills;
uint8_t opponent_shot_down;
//
// And above those, in the same byte rather than two more: bit i <<
// kOpponentFalling, slot i has been shot down and is falling, in a steep
// dive - and with bit i << kOpponentSpin, spinning - until it hits the
// ground, and only then down. Neither flying nor firing meanwhile, and no
// more hits counted.
static uint8_t _opponent_down;
static const uint8_t kOpponentFalling = 2;
static const uint8_t kOpponentSpin = 4;
// Bit i: slot i is in ATTACK, its mode against the player; clear, it is in
// PATH (docs/opponent_ai.md §3). A hunting slot starts in it.
static uint8_t _opponent_attack;
// Bit i: slot i, a hunter, is pulling up - from a strafing run, or from
// under kOpponentFloor - climbing straight ahead until it is back above the
// floor, and while the player is on the ground ~1 km off, before it turns
// for another run (_opponent_strafe()).
static uint8_t _opponent_pullup;
// Bit i: slot i fires a round this step (opponent_fires()).
static uint8_t _opponent_firing;
// An 8-bit xorshift, never 0 once opponent_reset() has run: the rear
// gunners' scatter, and the player's damage (damage.cc).
static uint8_t _opponent_lfsr;
// 1 + the slot trailing smoke - the last one hit - or kOpponentNoSmoke; and
// the frames it has left.
static uint8_t _opponent_smoke;
static uint8_t _opponent_smoke_t;
// What a pair starts from at 0, together, so that _opponent_send() clears it
// in one loop rather than a store an array.
static struct {
  // The heading: the front, a unit vector in 8.8. Always level, so z is 0,
  // the left is (-y, x, 0) and the up is the world's.
  vec3_t head[kOpponentMax];
  // The waypoint it is flying to.
  uint8_t next[kOpponentMax];
  // Which way it turned on the last step: +1 left, -1 right, 0 straight. And
  // the bank drawn: the same, once a turn has lasted two steps, so that the
  // one-step corrections on a leg do not flick the wings.
  int8_t turn[kOpponentMax];
  int8_t bank[kOpponentMax];
  // The climb level, -3 to 3: below 0 a descent.
  int8_t climb[kOpponentMax];
  // The climb level drawn: the climb, once it has lasted two whole steps, as
  // the bank holds a turn, so that a one-step correction does not flick the
  // nose. Held, not levelled, in between.
  int8_t pitch[kOpponentMax];
  uint8_t hits[kOpponentMax];
  // Whole steps left of the gun's cycle, 0 when it is ready for a burst.
  uint8_t gun[kOpponentMax];
} _opponent_fresh;
#define _opponent_head (_opponent_fresh.head)
#define _opponent_next (_opponent_fresh.next)
#define _opponent_turn (_opponent_fresh.turn)
#define _opponent_bank (_opponent_fresh.bank)
#define _opponent_climb (_opponent_fresh.climb)
#define _opponent_pitch (_opponent_fresh.pitch)
#define _opponent_hits (_opponent_fresh.hits)
#define _opponent_gun (_opponent_fresh.gun)
static_assert(sizeof(_opponent_fresh) < 256,
              "_opponent_send() clears it by a byte count");
// Position in flight_eye_*'s 24.8 half metres: x, y, then z, packed for
// _opponent_pos() and _opponent_move().
static int32_t _opponent_xyz[3 * kOpponentMax];
#define _opponent_x (_opponent_xyz)
#define _opponent_y (_opponent_xyz + kOpponentMax)
#define _opponent_z (_opponent_xyz + 2 * kOpponentMax)
// The airspeed, which the path's speed is now only the default of.
__striped static int16_t _opponent_speed[kOpponentMax];
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
// Bit i: slot i is near enough for its near colour; and that colour, by
// role: a bomber's kColorBrown, a fighter's kColorOpponentNear. Far away
// both are kColorOpponentFar.
static uint8_t _opponent_near;
static uint8_t _opponent_color[kOpponentMax];
// The layout of what is in slot i's front set.
static planes_layout_t _opponent_shown[kOpponentMax];
// The slot taken for the nearer while both are in view, and bit i: slot i is
// drawn under kViewTwo this frame.
static uint8_t _opponent_nearer;
static uint8_t _opponent_capped;
#pragma bss(tscratch)
// The aircraft being culled or drawn, in camera space and eighth metres,
// and each slot's once it is in view. Statics
// rather than locals, like the two below and the slot's state in
// _opponent_draw(), read through _opponent_planes[i] rather than a pointer:
// oscar64's zero page temporaries grow with the call graph and the
// renderer's chain is the deepest, with 4 bytes to spare (mem.h,
// tools/check_zeropage.py).
static vec3_t _opponent_c;
static vec3_t _opponent_cs[kOpponentMax];
// opponent_add_sprites()'s: which slots are in view, which of those are in
// the far range (_opponent_camera()), and whether a redraw is in flight this
// frame.
static uint8_t _opponent_seen;
static uint8_t _opponent_far;
static bool _opponent_busy;
#pragma bss(bss)

void opponent_invalidate(void) {
  for (uint8_t i = 0; i < kOpponentMax; ++i) {
    planes_state_init(&_opponent_planes[i]);
    _opponent_shown[i].level = kPlaneLevelDot;
  }
  _opponent_front = 0;
  _opponent_pending = 0;
  _opponent_first = 0;
  _opponent_near = 0;
  _opponent_nearer = 0;
  _opponent_capped = 0;
  // Below I/O, so no banking. Written here all the same, not once at boot:
  // it is in no linker region, so nothing loads or clears it.
  planes_dot_bitmap(_opponent_block(kOpponentBlockDot));
}

static_assert(kOpponentMax == 2, "slot i's mask bit is i + 1");

static inline uint8_t _opponent_bit(uint8_t i) { return (uint8_t)(i + 1); }

static __noinline bool _opponent_live(uint8_t i) {
  return _opponent_on[i] && !(_opponent_down & _opponent_bit(i));
}

static __noinline void _opponent_pos(uint8_t i, int32_t *p) {
  uint8_t *d = (uint8_t *)p;
  uint8_t o = (uint8_t)(i << 2);
  for (uint8_t c = 0; c < 12; ++c, ++o) {
    if (c == 4 || c == 8) {
      o += 4;
    }
    d[c] = ((const uint8_t *)_opponent_xyz)[o];
  }
}

bool opponent_target(uint8_t i, int32_t *p) {
  if (!_opponent_live(i)) {
    return false;
  }
  _opponent_pos(i, p);
  return true;
}

// Slot i is down: shot down, into the ground, or into the player. It stops
// where it is, is neither drawn nor a target, counts towards
// WP_TARGETS_DOWN, and its smoke hangs there kSmokeDown frames. `text` is
// the message, or null for none.
static __noinline void _opponent_destroy(uint8_t i, const char *text) {
  uint8_t bit = _opponent_bit(i);
  _opponent_down |= bit;
  ++_opponent_kills;
  _opponent_smoke = bit;
  _opponent_smoke_t = kSmokeDown;
  msg_show(text);
}

static inline bool _opponent_provoked(uint8_t i) {
  return (_opponent_path[i]->kind & kOpponentBehaviourMask) ==
         kOpponentProvoked;
}

bool opponent_calm(uint8_t i) {
  return _opponent_provoked(i) && !(_opponent_attack & _opponent_bit(i));
}

void opponent_provoke(uint8_t i) {
  if (_opponent_provoked(i)) {
    _opponent_attack |= _opponent_bit(i);
  }
}

void opponent_collide(uint8_t i) {
  flight_status = FLIGHT_CRASH_COLLISION;
  _opponent_destroy(i, nullptr);
}

void opponent_strike(uint8_t i) {
  uint8_t bit = _opponent_bit(i);
  if (_opponent_down & (uint8_t)(bit | bit << kOpponentFalling)) {
    return;
  }
  opponent_provoke(i);
  _opponent_smoke = bit;
  _opponent_smoke_t = kSmokeHit;
  if (++_opponent_hits[i] == kOpponentHitsToDown) {
    // Shot down: it falls (_opponent_fall()), and is down when it hits the
    // ground - parked, at once. Half of them spin.
    _opponent_down |= (uint8_t)(bit << kOpponentFalling);
    if (opponent_random() & 1) {
      _opponent_down |= (uint8_t)(bit << kOpponentSpin);
    }
    _opponent_firing &= (uint8_t)~bit;
    ++opponent_shot_down;
    msg_show("OPPONENT SHOT DOWN");
  }
}

bool opponent_smoke(int32_t *p, int16_t *h) {
  if (_opponent_smoke == kOpponentNoSmoke) {
    return false;
  }
  uint8_t i = (uint8_t)(_opponent_smoke - 1);
  _opponent_pos(i, p);
  h[0] = _opponent_head[i].x;
  h[1] = _opponent_head[i].y;
  return true;
}

// Neither slot flying, parked or falling.
static __noinline bool _opponent_none(void) {
  return !_opponent_live(0) && !_opponent_live(1);
}

bool opponent_none_left(void) { return _opponent_last && _opponent_none(); }

bool opponent_targets_down(void) {
  if (_opponent_failed) {
    return false;
  }
  if (_opponent_need != 0) {
    return _opponent_kills >= _opponent_need;
  }
  return opponent_none_left();
}

static __noinline uint16_t _opponent_mid(uint8_t a, uint8_t b) {
  uint16_t pa = (uint16_t)(_opponent_xyz[a] >> 8);
  uint16_t pb = (uint16_t)(_opponent_xyz[b] >> 8);
  return pa + (uint16_t)((int16_t)(pb - pa) >> 1);
}

// Moves the targets' navpoint onto them: the one still flying or parked, or
// midway between two - half their difference, which is the short way round
// across the world's edges as long as they are close, as a formation is. Where
// there are none left it stays where they were last.
static void _opponent_track(void) {
  bool l0 = _opponent_live(0), l1 = _opponent_live(1);
  if (l0 || l1) {
    // Navpoints are 8.8 world units, positions 24.8 half metres.
    uint8_t a = l0 ? 0 : 1, b = l1 ? 1 : 0;
    flight_nav_point_x[kOpponentNav] = _opponent_mid(a, b);
    flight_nav_point_y[kOpponentNav] =
        _opponent_mid((uint8_t)(a + kOpponentMax), (uint8_t)(b + kOpponentMax));
  }
}

static __noinline void _opponent_set_pos(uint8_t idx, uint16_t w) {
  _opponent_xyz[idx] = (int32_t)w << 8;
}

// The mission's next pair, from kOpponentWaves[_opponent_wave], into both
// slots at their start: everything about the last pair is forgotten.
static __noinline void _opponent_send(void) {
  uint8_t w = kOpponentWaves[_opponent_wave++];
  _opponent_last = (w & kOpponentLastWave) != 0;
  _opponent_down = 0;
  _opponent_attack = 0;
  _opponent_pullup = 0;
  _opponent_firing = 0;
  w &= kOpponentRowMask;
  const opponent_path_t *p = &kOpponentPaths[w][0];
  const opponent_pair_t *q = &kOpponentPairs[w];
  _opponent_pair = q;
  uint8_t k = (uint8_t)sizeof(_opponent_fresh);
  do {
    ((uint8_t *)&_opponent_fresh)[--k] = 0;
  } while (k != 0);
  for (uint8_t i = 0; i < kOpponentMax; ++i, ++p) {
    _opponent_on[i] = p->kind != kOpponentNone;
    if ((p->kind & kOpponentBehaviourMask) == kOpponentHunt) {
      _opponent_attack |= _opponent_bit(i);
    }
    _opponent_path[i] = p;
    _opponent_set_pos(i, p->start_x);
    _opponent_set_pos((uint8_t)(i + kOpponentMax), p->start_y);
    _opponent_set_pos((uint8_t)(i + 2 * kOpponentMax), p->z);
    _opponent_start_head(p, &_opponent_head[i]);
    _opponent_speed[i] = q->speed;
    _opponent_color[i] =
        (p->kind & kOpponentBomber) ? kColorBrown : kColorOpponentNear;
  }
  _opponent_track();
  opponent_invalidate();
}

void opponent_reset(uint8_t mission) {
  _opponent_mission = mission;
  _opponent_failed = false;
  _opponent_lfsr |= 1;
  _opponent_smoke = kOpponentNoSmoke;
  _opponent_substep = 0;
  _opponent_kills = 0;
  opponent_shot_down = 0;
  // Past every earlier mission's waves to its own first.
  uint8_t w = 0;
  while (mission != 0) {
    if (kOpponentWaves[w++] & kOpponentLastWave) {
      --mission;
    }
  }
  _opponent_wave = w;
  _opponent_need =
      (uint8_t)((kOpponentWaves[w] & ~kOpponentLastWave) >> kOpponentNeedShift);
  _opponent_send();
}

// Not outlined, so that oscar64 folds it, gun.cc's _gun_abs() and planes.cc's
// and planes_asm.cc's into one: outlined, its negation was a copy of its own.
#pragma optimize(push, nooutline)
static __noinline uint16_t _opponent_abs(int16_t v) {
  return (uint16_t)(v < 0 ? -v : v);
}
#pragma optimize(pop)

// |d| < r, as one unsigned 16-bit compare in line: in opponent_step(), whose
// waypoint reach and cut test it on both axes every step, faster and smaller
// than two _opponent_abs() calls. d + r - 1 stays inside int16: d is
// _opponent_toward()'s, within +-16,384.
static inline bool _opponent_within(int16_t d, uint16_t r) {
  return (uint16_t)(d + (int16_t)(r - 1)) < (uint16_t)(2 * r - 1);
}

// a * b + c * d, each in 8.8 with b and d no more than 256: every product
// below, which are all two-term dot products, called rather than written out.
static __noinline int16_t _opponent_dot(int16_t a, int16_t b, int16_t c,
                                        int16_t d) {
  return vec_fastmul8p8(a, b) + vec_fastmul8p8(c, d);
}

// A waypoint coordinate, 8.8 units, less a position, 24.8, in half metres
// and wrapped to 32,768 of them, the world's 128 units along x: the half
// metres' two whole bytes, bits 8-23, with bit 23 dropped and bit 22 taken
// as the sign - a byte move and two 16-bit shifts, not seven 32-bit ones.
// Good to 64 units either way, which is why no leg is that long. Along y the
// world is 256 units, and the same 64 holds. The position is
// _opponent_xyz[idx], read here: passed by value, every call copied its four
// bytes into the parameters, and its top one is never read.
static __noinline int16_t _opponent_toward(uint16_t w, uint8_t idx) {
  int16_t d = (int16_t)((((int32_t)w << 8) - _opponent_xyz[idx]) >> 8);
  return (int16_t)(d << 1) >> 1;
}

// What flight.cc's _flight_move_forward() adds along one axis for a level
// aircraft: the airspeed doubled times the heading's component, shifted by
// the step size after the multiply.
static __noinline void _opponent_move(uint8_t i, uint8_t idx, int16_t h) {
  _opponent_xyz[idx] +=
      flight_step_s(vec_fastmul8p8(_opponent_speed[i] << 1, h));
}

// Both axes across the ground, along slot i's heading.
static __noinline void _opponent_ahead(uint8_t i) {
  _opponent_move(i, i, _opponent_head[i].x);
  _opponent_move(i, (uint8_t)(i + kOpponentMax), _opponent_head[i].y);
}

// One step of the flight model for slot i, asked to climb at level `want`,
// descend below 0, or fly level: on a whole step the limits and the speed,
// then on every step the height, the slope's share of the speed. The move
// across the ground is opponent_step()'s, at the speed this leaves. Tables
// rather than shifts by the level: oscar64 shifts by a variable through a
// table of its own and two copies of the loop.
static __noinline void _opponent_fly(uint8_t i, int8_t want, bool whole) {
  int8_t climb = _opponent_climb[i];
  if (whole) {
    // Unsigned: a speed is never below 0, and oscar64's signed compares
    // cost twice the bytes.
    uint16_t speed = (uint16_t)_opponent_speed[i];
    uint16_t cruise = (uint16_t)_opponent_pair->speed;
    uint8_t role = (uint8_t)((_opponent_path[i]->kind & kOpponentBomber) >> 2);
    climb = want;
    if (climb > 0 ? speed <= kOpponentMinSpeed[role]
                  : climb < 0 && speed >= kOpponentMaxSpeed[role]) {
      climb = 0;
    }
    // Back towards the default a whole kOpponentLevelAccel, never part of
    // one: every change to the speed is a multiple of it, starting from the
    // default, so it lands there exactly.
    if (speed < cruise) {
      speed += kOpponentLevelAccel;
    } else if (speed > cruise) {
      speed -= kOpponentLevelAccel;
    }
    _opponent_speed[i] = (int16_t)speed + kOpponentClimbSpeed[climb + 3];
    if (climb == _opponent_climb[i]) {
      _opponent_pitch[i] = climb;
    }
    _opponent_climb[i] = climb;
  }
  if (climb != 0) {
    _opponent_move(i, (uint8_t)(i + 2 * kOpponentMax),
                   (int16_t)(-kOpponentClimbSpeed[climb + 3] * 4));
  }
}

// A hunter's guns' cone, and whether it breaks off: _opponent_strafe()'s.
#pragma bss(tscratch)
static bool _opponent_aim;
static bool _opponent_break;
#pragma bss(bss)

static_assert(kOpponentFloor + kOpponentFloorMargin < 256,
              "floor heights fit in uint8_t");

static __noinline bool _opponent_below(uint8_t i, uint8_t lim) {
  return (int16_t)(_opponent_z[i] >> 8) < (int16_t)lim;
}

// The climb level a hunting slot i wants, for the player dz above it and
// dx, dy along the ground, all cut down as opponent_step() cuts them for the
// turn, dx and dy to six bits (docs/opponent_ai.md §5): the level whose
// slope comes nearest the player's, from dz against the distance along the
// ground, max + min / 2 of |dx| and |dy|, all in bytes; or, breaking off,
// the steepest away from it. And above `floor`, in half metres.
static __noinline int8_t _opponent_want(uint8_t i, int8_t dx, int8_t dy,
                                        int16_t dz, uint8_t floor) {
  uint16_t az16 = _opponent_abs(dz);
  uint8_t az = az16 > 255 ? 255 : (uint8_t)az16;
  uint8_t ax = (uint8_t)(dx < 0 ? -dx : dx), ay = (uint8_t)(dy < 0 ? -dy : dy);
  // At most 94: 63 + 31.
  uint8_t h = ax > ay ? (uint8_t)(ax + (ay >> 1)) : (uint8_t)(ay + (ax >> 1));
  uint8_t h16 = h >> 4, h8 = h >> 3;
  // From a slope of 1/16, between level 1's 1/8 and none; 3/16, between
  // 1/8 and 1/4; and 3/8, between 1/4 and 1/2.
  int8_t want = 0;
  if (az > h16) {
    want = 1;
    if (az > (uint8_t)(h8 + h16)) {
      want = 2;
      if (az > (uint8_t)((h >> 2) + h8)) {
        want = 3;
      }
    }
  }
  // Breaking off (_opponent_strafe()): away from the player's height at the
  // steepest, over a player level with it or below.
  if (_opponent_break) {
    want = 3;
    dz = -dz;
  }
  if (dz < 0) {
    want = -want;
  }
  if (_opponent_below(i, floor)) {
    if (want < 1) {
      want = 1;
    }
  } else if (want < 0 &&
             _opponent_below(i, (uint8_t)(floor + kOpponentFloorMargin))) {
    want = 0;
  }
  return want;
}

// A hunting slot i's floor, in half metres, and whether it is pulling up
// (docs/opponent_ai.md §5): `dot` and `across` are the player's along and
// across its nose, and `cut` the halvings that took the player's distance
// along the ground under 64 half metres - under 3, nearer than ~130 m; over
// 5, farther than ~1 km. While the player is on the ground and dead ahead,
// within ~14 degrees, a run, down to kOpponentStrafeFloor. It ends at that
// floor, near the player, or once the player is no longer ahead, and the
// slot pulls up: straight on, up to kOpponentFloor and out to ~1 km, then
// round for the next. Under kOpponentFloor for any other reason it pulls up
// too, and in the air that ends at the floor.
//
// Also whether the player is in its guns' cone, in _opponent_aim: within ~7
// degrees of the nose across, and within ~500 m, the cut no more than 4
// halvings; not while pulling up. _opponent_guns() adds the height. And
// whether it breaks off, in _opponent_break: the player in the air and
// ahead, within ~127 m along the ground, the cut no more than 2, if it is
// coming the other way, or ~63 m if not. A collision is a crash (gun.cc),
// and pure pursuit flies a head-on pass into the player; closing at up to
// ~20 m a step head-on, it needs the room, and in a tail chase, at 2-5, it
// would climb away from a player it should follow.
static __noinline uint8_t _opponent_strafe(uint8_t i, int16_t dot,
                                           uint16_t across, uint8_t cut) {
  uint8_t bit = _opponent_bit(i);
  bool ground = flight_on_ground();
  uint8_t floor = (uint8_t)kOpponentFloor;
  if (_opponent_pullup & bit) {
    if (!_opponent_below(i, kOpponentFloor) && (cut > 5 || !ground)) {
      _opponent_pullup &= (uint8_t)~bit;
    }
  } else if (ground && dot > 0 && across < ((uint16_t)dot >> 2) && cut > 2 &&
             !_opponent_below(i, (uint8_t)(kOpponentStrafeFloor +
                                           kOpponentFloorMargin + 1))) {
    floor = (uint8_t)kOpponentStrafeFloor;
  } else if (_opponent_below(i, kOpponentFloor)) {
    _opponent_pullup |= bit;
  }
  _opponent_aim = !(_opponent_pullup & bit) && cut <= 4 && dot > 0 &&
                  across < ((uint16_t)dot >> 3);
  _opponent_break =
      !ground && dot > 0 &&
      (cut <= 1 ||
       (cut == 2 &&
        _opponent_dot(flight_cam.front.x, _opponent_head[i].x,
                      flight_cam.front.y, _opponent_head[i].y) < 0));
  return floor;
}

// Hunting slot i's gun, on a whole step, wanting climb level `want`: the
// cycle counted down, and _opponent_firing's bit set if a round is due and
// the player is in front of the nose - in _opponent_strafe()'s cone across,
// and in height too, the level it flies being the one that points at the
// player. The guns do not aim. gun.cc fires the round (opponent_fires()):
// a call from here, this deep in opponent_step()'s call graph, cost it its
// zero page temporaries.
static __noinline void _opponent_guns(uint8_t i, int8_t want) {
  uint8_t g = _opponent_gun[i];
  if (g != 0) {
    --g;
  }
  if (_opponent_aim && want == _opponent_climb[i]) {
    if (g == 0) {
      g = kOpponentGunCycle;
    }
    if (g >= kOpponentGunCycle - 4 && !(g & 1)) {
      _opponent_firing |= _opponent_bit(i);
    }
  }
  _opponent_gun[i] = g;
}

// The next of _opponent_lfsr's 255 values: xorshift 7, 5, 3. A Galois
// LFSR's next is its last shifted by a bit, and a burst's rounds walked.
__noinline uint8_t opponent_random(void) {
  uint8_t r = _opponent_lfsr;
  r ^= (uint8_t)(r << 7);
  r ^= (uint8_t)(r >> 5);
  r ^= (uint8_t)(r << 3);
  _opponent_lfsr = r;
  return r;
}

// Bomber i's rear gunner (docs/opponent_ai.md §6): whether the player is
// behind it and less than kOpponentGunnerReach back, within 45 degrees of
// the tail to either side, and from 45 degrees above the plane of its wings
// to ~10 below: the gun cannot bear on a player well under it. If so, the
// line to the player, in half metres, in _opponent_aim_d, which
// opponent_fires() aims along. Asked from opponent_fires(), under
// gun_step(): under opponent_step() its multiplies cost that a software
// stack frame.
//
// The eye's three axes come through flight.h's kFlightEye, so that a loop can
// take each off in turn: written out, _opponent_camera()'s three 32-bit
// differences were 104 B.
static vec3_t _opponent_aim_d;
static __noinline bool _opponent_gunner(uint8_t i) {
  // The line to the player, an axis at a time, counted down: oscar64
  // unrolls a for loop over three axes.
  int16_t *d = &_opponent_aim_d.x;
  uint8_t c = 3, k = (uint8_t)(i + 2 * kOpponentMax);
  do {
    --c;
    d[c] = _opponent_toward((uint16_t)(*kFlightEye[c] >> 8), k);
    k -= kOpponentMax;
  } while (c != 0);
  int16_t dx = d[0], dy = d[1];
  int16_t hx = _opponent_head[i].x, hy = _opponent_head[i].y;
  // Behind, 1 to kOpponentGunnerReach - 1 back, as one unsigned compare.
  uint16_t back = (uint16_t)(-_opponent_dot(dx, hx, dy, hy));
  if ((uint16_t)(back - 1) >= (uint16_t)(kOpponentGunnerReach - 1)) {
    return false;
  }
  // tan 10 degrees, 0.18, as 3/16; then -under <= dz < back as one compare.
  uint16_t under = (back >> 3) + (back >> 4);
  if (_opponent_abs(_opponent_dot(dy, hx, -dx, hy)) >= back ||
      (uint16_t)(d[2] + under) >= back + under) {
    return false;
  }
  // Inside the cone no component is more than 1.42 times `back`, so over
  // twice it each is a fraction under 0.71, 181 in 8.8: the line well under
  // the 512 long vec_normalize() takes, and exact to the eight bits.
  back <<= 1;
  c = 3;
  do {
    --c;
    d[c] = vec_div8p8(d[c], (int16_t)back);
  } while (c != 0);
  return true;
}

// The round leaves at the muzzle velocity, 3 x 256 in gun.cc's sixteenths
// of a metre a step: three times a unit vector, scattered by
// kOpponentScatter on each axis. A fighter's along its nose, the heading
// with the climb's sine for its z; a bomber's gunner's at the player, along
// the line _opponent_gunner() found, vec_normalize()d. Unscattered, a fighter's
// pure pursuit put every round on a player it was chasing, who moves along the
// line of fire. No multiply: the aircraft's own speed, 3-11 m a step against
// the round's 48, and the climb's cosine, 0.89 at the steepest, are left out,
// which the guns not aiming makes nothing of.
static_assert(kGunMuzzle == 3 * 256, "the round is three times the nose");
bool opponent_fires(uint8_t i, int16_t *v) {
  uint8_t bit = _opponent_bit(i);
  if (!(_opponent_firing & bit)) {
    return false;
  }
  _opponent_firing &= (uint8_t)~bit;
  uint8_t r = opponent_random();
  v[0] = kOpponentScatter[r & 7];
  v[1] = kOpponentScatter[(r >> 3) & 7];
  v[2] = kOpponentScatter[(r >> 6) + 2];
  // The direction the scatter is added to, then the sum tripled an axis at a
  // time, counted down: written out, the three took 31 B more. A fighter's
  // heading has no z (it is level), so the climb's sine goes on its own.
  const int16_t *d = &_opponent_head[i].x;
  if (_opponent_path[i]->kind & kOpponentBomber) {
    if (!_opponent_gunner(i)) {
      return false;
    }
    vec_normalize(&_opponent_aim_d);
    d = &_opponent_aim_d.x;
  } else {
    v[2] += kOpponentPitchSin[(uint8_t)(_opponent_climb[i] + 3)];
  }
  uint8_t c = 3;
  do {
    --c;
    int16_t a = v[c] + d[c];
    v[c] = (a << 1) + a;
  } while (c != 0);
  return true;
}

static void _opponent_forget(uint8_t i);

// Slot i is gone: off the map at the end of a one-way path, or given up
// (kOpponentStray). Not down, and nothing of it on screen.
static __noinline void _opponent_gone(uint8_t i) {
  _opponent_on[i] = false;
  if (_opponent_smoke == _opponent_bit(i)) {
    _opponent_smoke = kOpponentNoSmoke;
  }
  _opponent_forget(i);
}

// Whether the player is within r of slot i on both axes across the ground,
// in half metres. An axis at a time, through kFlightEye: written out, the
// two were 99 B.
static __noinline bool _opponent_close(uint8_t i, uint16_t r) {
  uint8_t c = 0;
  do {
    if (_opponent_abs(_opponent_toward((uint16_t)(*kFlightEye[c] >> 8), i)) >=
        r) {
      return false;
    }
    i += kOpponentMax;
  } while (++c < 2);
  return true;
}

// The mission has failed: said once, and WP_TARGETS_DOWN never comes.
static void _opponent_fail(void) {
  if (!_opponent_failed) {
    _opponent_failed = true;
    msg_show(kOpponentFailText[_opponent_mission != 0],
             3 * MSG_DEFAULT_DURATION);
  }
}

// One step of slot i falling, drawn at kOpponentDive, ~37 degrees nose
// down: along its heading at its speed, and down at three quarters of it -
// that same ~37 degrees, and from 5,000 ft ~50 s. At one and a half times
// it, ~56 degrees and drawn at 60, it was gone from sight too quickly
// (2026-11-05); spinning,
// its heading turned a quarter each whole step, ~1.5 turns a second, and
// the wings drawn at 60 degrees. Its smoke trails it all the way, and where
// it hits the ground it is down.
static const int16_t kOpponentFallZ = -192;
static __noinline void _opponent_fall(uint8_t i, bool whole) {
  uint8_t bit = _opponent_bit(i);
  vec3_t *h = &_opponent_head[i];
  _opponent_pitch[i] = kOpponentDive;
  _opponent_smoke = bit;
  _opponent_smoke_t = kSmokeHit;
  if ((_opponent_down & (uint8_t)(bit << kOpponentSpin)) && whole) {
    int16_t hx = h->x;
    h->x = -h->y;
    h->y = hx;
    _opponent_bank[i] = 2;
  }
  _opponent_move(i, (uint8_t)(i + 2 * kOpponentMax), kOpponentFallZ);
  // Parked, the only aircraft with no speed, it is down where it stands.
  if (_opponent_z[i] <= 0 || _opponent_speed[i] == 0) {
    _opponent_down &= (uint8_t)~(bit << kOpponentFalling);
    _opponent_destroy(i, nullptr);
    return;
  }
  _opponent_ahead(i);
}

void opponent_step(void) {
  if (flight_paused || flight_crashed()) {
    return;
  }
  bool turns = (++_opponent_substep & kFlightSubstepMask) == 0;
  // The smoke runs out: this is one call a frame.
  if (_opponent_smoke != kOpponentNoSmoke && --_opponent_smoke_t == 0) {
    _opponent_smoke = kOpponentNoSmoke;
  }
  // The pair up is gone: the next, once its smoke has cleared; or, after the
  // last, too few of them down fails the mission.
  if (_opponent_none()) {
    if (!_opponent_last) {
      if (_opponent_smoke == kOpponentNoSmoke) {
        _opponent_send();
      }
    } else if (_opponent_kills < _opponent_need) {
      _opponent_fail();
    }
  }
  for (uint8_t i = 0; i < kOpponentMax; ++i) {
    if (!_opponent_live(i)) {
      continue;
    }
    const opponent_path_t *p = _opponent_path[i];
    uint8_t bit = _opponent_bit(i);
    if (_opponent_down & (uint8_t)(bit << kOpponentFalling)) {
      _opponent_fall(i, turns);
      continue;
    }
    // Parked: it stays where it is.
    if ((p->kind & kOpponentKindMask) == kOpponentParked) {
      continue;
    }
    vec3_t *h = &_opponent_head[i];
    // A Provoked aircraft that sees the player attacks; one that strays and
    // has lost it gives up.
    if (!(_opponent_attack & bit)) {
      if (_opponent_provoked(i) && _opponent_close(i, kOpponentSpot)) {
        _opponent_attack |= bit;
      }
    } else if ((p->kind & kOpponentStray) &&
               !_opponent_close(i, kOpponentGiveUp)) {
      _opponent_gone(i);
      continue;
    }

    // What it steers for, relative, in half metres: a fighter in ATTACK, the
    // player; anything else its waypoint at its path's height.
    bool hunt = (_opponent_attack & bit) && !(p->kind & kOpponentBomber);
    uint16_t tx = (uint16_t)(flight_eye_x >> 8);
    uint16_t ty = (uint16_t)(flight_eye_y >> 8);
    int16_t dz = (int16_t)(flight_eye_z >> 8);
    if (!hunt) {
      uint8_t k = (uint8_t)(p->wp + _opponent_next[i]);
      tx = kOpponentWpX[k];
      ty = kOpponentWpY[k];
      dz = (int16_t)p->z;
    }
    int16_t dx = _opponent_toward(tx, i);
    int16_t dy = _opponent_toward(ty, (uint8_t)(i + kOpponentMax));
    if (!hunt && _opponent_within(dx, kOpponentReach) &&
        _opponent_within(dy, kOpponentReach)) {
      // Reached, and the next one is steered for from the next step on.
      uint8_t w = _opponent_next[i];
      if (w == _opponent_pair->fail) {
        _opponent_fail();
      }
      if (++w == _opponent_pair->count) {
        if ((p->kind & kOpponentBehaviourMask) == kOpponentPass) {
          // The pass is over: the player, from the next step on.
          _opponent_attack |= bit;
        } else if ((p->kind & kOpponentKindMask) == kOpponentOneWay) {
          // Off the map.
          _opponent_gone(i);
          continue;
        }
        w = 0;
      }
      _opponent_next[i] = w;
    }
    dz -= (int16_t)(_opponent_z[i] >> 8);
    // On a path, up or down at level 1 to the path's height.
    int8_t want = 0;
    if (dz > kOpponentPathBand) {
      want = 1;
    } else if (dz < -kOpponentPathBand) {
      want = -1;
    }

    // Steer: the waypoint's side of the nose, from the cross product of the
    // heading and the direction to it, and whether it is near enough dead
    // ahead to fly straight, from that against the dot product: within
    // 1 / 32 rad, 1.8 degrees, wider than one step's turn, so a straight leg
    // stays straight. The direction is cut down to six bits a component
    // first, so both products fit 16 bits, and taken as 8.8 so that
    // vec_fastmul8p8() forms them exactly, with no multiply linked for them.
    uint8_t cut = 0;
    while (!_opponent_within(dx, 64) || !_opponent_within(dy, 64)) {
      dx >>= 1;
      dy >>= 1;
      dz >>= 1;
      ++cut;
    }
    int8_t dx8 = (int8_t)dx, dy8 = (int8_t)dy;
    dx <<= 8;
    dy <<= 8;
    int16_t hx = h->x, hy = h->y;
    int16_t cross = _opponent_dot(dy, hx, -dx, hy);
    int16_t dot = _opponent_dot(dx, hx, dy, hy);
    uint16_t across = _opponent_abs(cross);
    // A hunter's height, whether it pulls up, straight ahead, and its guns.
    bool straight = false;
    if (hunt) {
      uint8_t floor = _opponent_strafe(i, dot, across, cut);
      straight = (_opponent_pullup & bit) != 0;
      want = straight ? 1 : _opponent_want(i, dx8, dy8, dz, floor);
      // Breaking off, it also turns away from the player, whichever way it
      // is - the other side, and never straight on: on its tail near the
      // ground the floor stops it diving clear, and pure pursuit at the
      // player's own speed held it there until they touched.
      if (_opponent_break) {
        cross = -cross;
        dot = -1;
      }
    } else {
      // A bomber's rear gunner, whatever its behaviour: its cone is
      // opponent_fires()'s. Not a fighter's guns: one whose pass ended this
      // step is in ATTACK already but steered for its waypoint, and the
      // player is behind it.
      _opponent_aim = (p->kind & kOpponentBomber) != 0;
    }
    if (turns) {
      _opponent_guns(i, want);
    }
    int8_t turn = 0;
    if (!straight && (dot < 0 || across > ((uint16_t)dot >> 5))) {
      turn = (int8_t)(hunt ? 2 : 1);
      if (cross < 0) {
        turn = -turn;
      }
      // The small-angle turn of vec_turn3_xy(), anticlockwise for a left
      // turn: h += s * left, which is vec_turn_xy()'s clockwise turn by -s,
      // exactly (vec_fastmul8p8() applies the sign last). The step lengthens
      // the vector a little, and the normalisation puts it back.
      if (turns) {
        vec_turn_xy((int16_t)(-turn * kOpponentTurn), h);
        vec_normalize(h);
      }
    }
    _opponent_bank[i] = turn == _opponent_turn[i] ? turn : 0;
    _opponent_turn[i] = turn;

    _opponent_fly(i, want, turns);
    // Into the ground, however unlikely: down, as if shot down.
    if (_opponent_z[i] <= 0) {
      _opponent_destroy(i, "ENEMY CRASHED");
      continue;
    }

    _opponent_ahead(i);
  }
  _opponent_track();
}

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

// Slot i is off screen, or only the dot, so its block sets no longer show
// what it looks like. Until a new silhouette is filled it is shown as the
// dot, never as the bitmap from the last time it was in view: that can be
// any size, and a chase that looked away and back saw one frame of the
// aircraft as close as it had been.
static void _opponent_forget(uint8_t i) {
  uint8_t bit = _opponent_bit(i);
  _opponent_shown[i].level = kPlaneLevelDot;
  // A projection still to be filled is a bitmap that was never drawn, so
  // nothing may come from the cache against it.
  if (_opponent_pending & bit) {
    _opponent_pending &= (uint8_t)~bit;
    _opponent_planes[i].key_valid = false;
  }
}

// Where slot i is in camera space, in _opponent_c, by its distance along
// the world's axes: inside 16 units, ~2 km, on all three, kOpponentNear and
// in eighth metres (docs/planes.md §2); out to 32, ~4 km, kOpponentFar and
// in half metres, which keeps 4 km inside int16 and vec_transform_inv()'s
// sums; farther on any, kOpponentOut and nothing. The two ranges' units are
// the two views' numerators (kViewFar), so nothing on screen changes
// between them.
enum opponent_range_t { kOpponentNear, kOpponentFar, kOpponentOut };
// Static, out of the zero page's temporaries (mem.h).
#pragma bss(tscratch)
static uint16_t _opponent_d[3];
static int8_t _opponent_s[3];
#pragma bss(bss)
static __noinline uint8_t _opponent_camera(uint8_t i) {
  uint8_t range = kOpponentNear;
  uint8_t k = i;
  for (uint8_t c = 0; c < 3; ++c, k += kOpponentMax) {
    // The difference's fraction, for the loop below, and its whole units,
    // wrapped to the world: x repeats every 128 units, so its seven bits are
    // sign-extended; y every 256, and z never gets there.
    int32_t d = _opponent_xyz[k] - *kFlightEye[c];
    _opponent_d[c] = (uint16_t)d;
    uint8_t u = (uint8_t)(d >> 16);
    if (c == 0) {
      u &= 0x7F;
      if (u & 0x40) {
        u |= 0x80;
      }
    }
    // Signed range tests, not (uint8_t)(u + 16) >= 32: oscar64 folds that
    // to u >= 16, wraparound dropped, and culled everything behind on x.
    int8_t s = (int8_t)u;
    _opponent_s[c] = s;
    if (s >= 32 || s < -31) {
      return kOpponentOut;
    }
    if (s >= 16 || s < -15) {
      range = kOpponentFar;
    }
  }
  for (uint8_t c = 0; c < 3; ++c) {
    // The far range's half metres, and the near range's eighths from them:
    // two bits more, the next two from the fraction's byte.
    uint16_t f = _opponent_d[c];
    uint16_t v = ((uint16_t)(uint8_t)_opponent_s[c] << 8) | (f >> 8);
    if (range == kOpponentNear) {
      v = (v << 2) | ((uint8_t)f >> 6);
    }
    (&_opponent_c.x)[c] = (int16_t)v;
  }
  vec_transform_inv(&world_cam, &_opponent_c, &_opponent_c);
  return range;
}

// Whether the aircraft at _opponent_c is wholly outside the viewport.
//
// Every part of it is within r = kPlaneMaxRadius / 2 eighth metres of c, so
// no part is inside an edge at tangent t if |y| - r > t * (x + r), that is
// |y| - r * (1 + t) > t * x. The edges are at t = 160 / 256 = 1/2 + 1/8
// across and 56 / 256 = 1/4 - 1/32 up and down (vec_project()), and each
// gets another 4 pixels, x / 64, for expanded pixels' rounding.
//
// Unsigned: x is positive here, and oscar64's signed shifts cost twice the
// bytes. On _opponent_c rather than three parameters, for the same reason.
static const uint16_t kOpponentRadius = kPlaneMaxRadius * kOpponentKn / 128;
static const uint16_t kOpponentReachX = kOpponentRadius * 13 / 8 + 2;
static const uint16_t kOpponentReachZ = kOpponentRadius * 39 / 32 + 2;

static __noinline bool _opponent_off_screen(void) {
  uint16_t half = (uint16_t)_opponent_c.x >> 1;
  uint16_t eighth = half >> 2;
  uint16_t px4 = eighth >> 3;
  uint16_t ay = _opponent_abs(_opponent_c.y);
  uint16_t az = _opponent_abs(_opponent_c.z);
  return ay > half + eighth + px4 + kOpponentReachX ||
         az > (half >> 1) - (eighth >> 2) + px4 + kOpponentReachZ;
}

// Slot i's body axes in camera space, in _opponent_axes_m, as planes_project()
// reads them: only their screen components, y and z, which are each a dot
// product with world_cam's left and up. The heading is level, so the front and
// left have no z and the up is the world's, whose products are world_cam's own
// z components. Then the pitch, while climbing or descending: front and up
// turned about the left by the slope's angle (kOpponentPitchSin/Cos), the
// left staying level. Then the bank, while turning: left and up turned about
// the front by 30 degrees, towards the turn.
#pragma bss(tscratch)
static mat3_t _opponent_axes_m;
static planes_frame_t _opponent_f;
// The pair _opponent_rot() turns, statics rather than parameters or locals:
// oscar64's zero page temporaries have no room to spare on this chain.
static int16_t _opponent_ra, _opponent_rb;
#pragma bss(bss)

// Turns the pair (_opponent_ra, _opponent_rb) by the angle whose cosine and
// sine, 8.8, are c (< 256) and s: a' = c a + s b, b' = c b - s a. The pitch
// turns front and up so, and the bank left and up.
static __noinline void _opponent_rot(uint8_t c, int16_t s) {
  int16_t a = _opponent_ra;
  _opponent_ra = _opponent_dot(a, c, _opponent_rb, s);
  _opponent_rb = _opponent_dot(_opponent_rb, c, a, -s);
}

static void _opponent_axes(uint8_t i) {
  int16_t hx = _opponent_head[i].x, hy = _opponent_head[i].y;
  uint8_t turn = (uint8_t)(_opponent_bank[i] + 2);
  uint8_t pitch = (uint8_t)(_opponent_pitch[i] + 3);
  // y, then z: the products with world_cam's left, then with its up.
  const vec3_t *row = &world_cam.left;
  int16_t *out = &_opponent_axes_m.front.y;
  for (uint8_t c = 0; c < 2; ++c, ++row, ++out) {
    // Nose up, the front rises and the top tips back: front (c h, s), up
    // (-s h, c), h the level heading.
    _opponent_ra = _opponent_dot(row->x, hx, row->y, hy);
    _opponent_rb = row->z;
    if (pitch != 3) {
      _opponent_rot(kOpponentPitchCos[pitch], kOpponentPitchSin[pitch]);
    }
    out[0] = _opponent_ra;
    // Banked left, the left wing goes down and the top towards the left.
    _opponent_ra = _opponent_dot(row->x, -hy, row->y, hx);
    if (turn != 2) {
      _opponent_rot(kOpponentBankCos[turn], kOpponentBankSin[turn]);
    }
    out[3] = _opponent_ra;
    out[6] = _opponent_rb;
  }
}

#ifndef __OSCAR64__
static inline void _opponent_axes(uint8_t i, mat3_t *axes) {
  _opponent_axes(i);
  *axes = _opponent_axes_m;
}
#endif

static_assert(kOpponentMax == 2, "the slots' turns below are written for two");

// Which aircraft are in view, and where: bit i of the result for slot i, and
// its camera-space position in _opponent_cs[i]. This, _opponent_cap() and
// _opponent_draw() are functions of their own, out of
// opponent_add_sprites(): in one they pushed oscar64's zero page
// temporaries past the end of its area (mem.h).
static __noinline uint8_t _opponent_cull(void) {
  uint8_t seen = 0, far = 0;
  for (uint8_t i = 0; i < kOpponentMax; ++i) {
    if (!_opponent_on[i]) {
      continue;
    }
    uint8_t bit = _opponent_bit(i);
    // Down, and gone: only its smoke stays (opponent_smoke()).
    uint8_t range =
        (_opponent_down & bit) ? (uint8_t)kOpponentOut : _opponent_camera(i);
    // planes_render() says the same about x, but only after the axes
    // below. Then wholly outside the viewport: planes_render() only gives
    // up 45 degrees off the view axis, so an aircraft just over the top on
    // the pass, or just off to one side, would be projected, and drawn or
    // looked up in the cache, for nothing: 7,000-38,000 cycles a frame. In
    // the far range's half metres the margins are four times what they need be.
    if (range != kOpponentOut && _opponent_c.x > 64 &&
        !_opponent_off_screen()) {
      seen |= bit;
      if (range == kOpponentFar) {
        far |= bit;
      }
      uint8_t o = i ? (uint8_t)sizeof(vec3_t) : 0;
      for (uint8_t k = 0; k < sizeof(vec3_t); ++k, ++o) {
        ((uint8_t *)_opponent_cs)[o] = ((const uint8_t *)&_opponent_c)[k];
      }
      continue;
    }
    _opponent_forget(i);
  }
  _opponent_far = far;
  return seen;
}

// Seven sprites for the stack (sprites.h kSpriteTerrainSlots), and two
// aircraft at 2 x 2 would take eight: the farther would be dropped whole.
// So while both are in view the farther is held to two sprites, which
// changes nothing unless it is within ~115 m (kViewTwo). The back view's fin
// takes 0 and 1 (sprites.cc kSpriteFinCount), which leaves the stack six.
// The smoke - an aircraft's, or where a round hit the ground - and a tracer
// (gun.cc) take one each: four and two still fit in front with one of them,
// not with both, nor behind with either - and then both aircraft are held
// to two. Which is nearer goes by depth, and changes
// only once the other is an eighth nearer, so that two at about the same
// range do not trade sizes every frame; one in the far range is the farther.
// Sets _opponent_capped when both slots are in view.
static __noinline void _opponent_cap(void) {
  if (_opponent_far == 1 || _opponent_far == 2) {
    _opponent_nearer = _opponent_far & 1;
  } else {
    uint8_t n = _opponent_nearer, o = n ^ 1;
    uint16_t dn = (uint16_t)_opponent_cs[n].x;
    if ((uint16_t)_opponent_cs[o].x < dn - (dn >> 3)) {
      _opponent_nearer = o;
    }
  }
  uint8_t extra =
      (uint8_t)(_opponent_smoke != kOpponentNoSmoke || gun_puff_up()) +
      (uint8_t)gun_tracer_up();
  uint8_t capped = extra > (view_state == VIEW_BACK ? 0 : 1)
                       ? 3
                       : _opponent_bit(_opponent_nearer ^ 1);
  for (uint8_t i = 0; i < kOpponentMax; ++i) {
    uint8_t bit = _opponent_bit(i);
    // Newly held to two with a layout of more than one sprite, on screen,
    // being filled or only latched: the dot until its turn to redraw comes
    // round, and that from scratch. The layout's hysteresis, carried from
    // the larger view, could otherwise hold it at 2 x 2 under kViewTwo -
    // two hold only for an aircraft drawn under it all along.
    if ((capped & bit) && !(_opponent_capped & bit)) {
      planes_state_t *st = &_opponent_planes[i];
      if (st->lay.level != kPlaneLevelDot && st->lay.cols + st->lay.rows > 2) {
        _opponent_forget(i);
        st->lay.level = kPlaneLevelDot;
        st->key_valid = false;
      }
    }
  }
  _opponent_capped = capped;
}

static __noinline void _opponent_latch_shown(uint8_t i) {
  uint8_t si = i ? (uint8_t)sizeof(planes_state_t) : 0;
  uint8_t di = i ? (uint8_t)sizeof(planes_layout_t) : 0;
  for (uint8_t k = 0; k < sizeof(planes_layout_t); ++k, ++si, ++di) {
    ((uint8_t *)_opponent_shown)[di] =
        ((const uint8_t *)&_opponent_planes[0].lay)[si];
  }
}

// Slot i, in view at _opponent_cs[i]: its redraw, or the half of one that is
// due, and its offer to the stack. `busy` if another slot has a redraw in
// flight this frame; true if this one starts one.
static __noinline bool _opponent_draw(uint8_t i, bool busy) {
  uint8_t bit = _opponent_bit(i);
  bool started = false;
  bool far = (_opponent_far & bit) != 0;
  _opponent_set_view(far, (_opponent_capped & bit) != 0);
  uint8_t o = i ? (uint8_t)sizeof(vec3_t) : 0;
  for (uint8_t k = 0; k < sizeof(vec3_t); ++k, ++o) {
    ((uint8_t *)&_opponent_c)[k] = ((const uint8_t *)_opponent_cs)[o];
  }
  // A redraw takes two frames, the projection in one and the fill in the
  // next (docs/planes.md §13, "The silhouette at half rate"), so neither
  // frame pays for both. The position is this frame's either way: the
  // bitmap on screen is placed at this frame's centre.
  if (_opponent_pending & bit) {
    _opponent_front ^= bit;
    busy = true;
  }
  // The first block of the set on screen.
  uint8_t front = kOpponentBlockFirst[i];
  if (_opponent_front & bit) {
    front += kOpponentSetBlocks;
  }
  if (_opponent_pending & bit) {
    // The fill the last frame projected. It writes under I/O; the raster
    // handlers bank I/O in for themselves (gfx.cc _gfx_isr), and nothing
    // in planes_fill() touches it. Then the new bitmap is the front one:
    // it goes on screen with this frame's characters, at
    // mem_switch_buffer(), and by the time a later frame fills the other
    // set the raster handler has stopped showing it - mem_switch_buffer()
    // runs past raster 250 in its colour copy before it returns.
    mmap_set(MMAP_RAM);
    planes_fill(&_opponent_planes[i], _opponent_block(front));
    mmap_set(MMAP_NO_ROM);
    _opponent_pending &= (uint8_t)~bit;
    _opponent_latch_shown(i);
  }
  if (!planes_centre(&_opponent_view, &_opponent_c, &_opponent_f)) {
    if (!busy) {
      _opponent_forget(i);
    }
    return started;
  }
  // While `busy` is set (just filled, or another slot owns this frame's
  // redraw), planes_centre() alone places what is already on screen where it
  // is now.
  if (!busy) {
    if (!planes_begin(&_opponent_planes[i], &_opponent_f)) {
      _opponent_forget(i);
      planes_place(&_opponent_planes[i].lay, &_opponent_view, &_opponent_f);
    } else {
      _opponent_axes(i);
      planes_project_axes(&_opponent_planes[i], &_opponent_view,
                          &_opponent_axes_m, &_opponent_f);
      if (_opponent_f.cached) {
        // The front set holds the state's silhouette - after a forget too,
        // as the state has not been projected since it was filled.
        _opponent_latch_shown(i);
      } else {
        // A new silhouette, filled next frame; until then the old bitmap,
        // or the dot if there is none.
        _opponent_pending |= bit;
        started = true;
      }
    }
  }
  if (busy || started) {
    planes_place(&_opponent_shown[i], &_opponent_view, &_opponent_f);
  }
  if (_opponent_f.hidden != kPlaneShown) {
    return started;
  }

  uint8_t bitmap = kOpponentBlockDot;
  uint8_t bitmap2 = kSpriteNoBitmap;
  uint8_t flags = 0;
  if (_opponent_f.level != kPlaneLevelDot) {
    bitmap = front;
    if (_opponent_f.rows == 2) {
      bitmap2 = (uint8_t)(front + _opponent_f.cols);
    }
    if (_opponent_f.cols == 2) {
      flags |= kSpriteFlagWide;
    }
    if (_opponent_f.xs == 2) {
      flags |= kSpriteFlagExpandX;
    }
    if (_opponent_f.ys == 2) {
      flags |= kSpriteFlagExpandY;
    }
  }
  // The colour, from the pixel size: near from about 8 pixels across, and
  // back to far below 7, so that a chase holding the range on the line does
  // not flicker.
  if (_opponent_f.d >= kOpponentNearD) {
    _opponent_near |= bit;
  } else if (_opponent_f.d < kOpponentNearD - 1) {
    _opponent_near &= (uint8_t)~bit;
  }
  uint8_t color =
      (_opponent_near & bit) ? _opponent_color[i] : kColorOpponentFar;
  // Depth in the clouds' 1 m units, so that the two sort together.
  sprites_stack_add(far ? _opponent_c.x >> 1 : _opponent_c.x >> 3,
                    _opponent_f.x, _opponent_f.y, 0, 0, bitmap, bitmap2, color,
                    flags);
  return started;
}

void opponent_add_sprites(void) {
  _opponent_seen = _opponent_cull();
  if (_opponent_seen == 3) {
    _opponent_cap();
  } else {
    _opponent_capped = 0;
    if (_opponent_seen == 0) {
      return;
    }
  }

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
    if ((_opponent_seen & _opponent_bit(i)) &&
        _opponent_draw(i, _opponent_busy)) {
      _opponent_busy = true;
      _opponent_first = i ^ 1;
    }
  }
}

#pragma optimize(pop)
