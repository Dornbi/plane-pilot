#ifndef FLIGHT_H
#define FLIGHT_H

#include <stdint.h>

#include "bool.h"
#include "mission.h"
#include "vec.h"

// Raster frames between model steps - the other half of flight_step_shift, and
// the reason scaling the step does not change how the aeroplane flies. Eight is a little above the slowest render measured (docs/framerate.md
// has 8.00 raster frames at cruise, 7.12 on the runway), so the model gets its
// step in on every pass and the surplus is waited out rather than flown.
//
// That waiting is the point. Before this the model advanced once per render,
// so the aircraft covered the same ground per frame however long the frame
// took - and airspeed through the world changed by 13% with what was on
// screen. Now the step rate is the raster's, which nothing on screen can move.
// Set once at boot from cpu_step_shift (cpu.h). Both derive from the same
// shift, so their product - the aircraft's real rates - does not move.
extern uint8_t flight_step_shift;
extern uint8_t kFlightFramesPerStep;
extern uint8_t kFlightSubstepMask;

// Sets all three, and rescales the six control rotations to match. Call once,
// after cpu_probe() and before the first flight_advance().
void flight_set_step_shift(uint8_t shift);

// Everything from FLIGHT_CRASH_ROLL on is a crash, and only a crash ends the
// flight. FLIGHT_MISSION_COMPLETED is a record of what the pilot achieved,
// not a stop state: the simulation keeps running so they can fly on, and
// flight_crashed() below - not a plain truth test on flight_status - is what
// the flight loop asks before freezing.
// Keep the crash statuses last, and keep kFaultText in flight.cc in sync.
enum FlightStatus {
  FLIGHT_ONGOING = 0,
  FLIGHT_MISSION_COMPLETED,
  FLIGHT_CRASH_ROLL,
  FLIGHT_CRASH_INVERTED,
  FLIGHT_CRASH_PITCH_LOW,
  FLIGHT_CRASH_PITCH_HIGH,
  FLIGHT_CRASH_VSPEED,
  FLIGHT_CRASH_SPEED,
  FLIGHT_CRASH_GEAR,
  FLIGHT_CRASH_NOT_ON_RUNWAY,
};

// Message for a status: "WARNING: ..." on the approach, "CRASHED: ..." after
// touchdown, from one shared set of strings. The returned pointer is a shared
// buffer, valid until the next call.
const char *flight_status_text(enum FlightStatus status, bool crashed);

extern bool flight_paused;
extern enum FlightStatus flight_status;

// True once the aircraft is wrecked, which is the only thing that stops the
// physics and locks out the controls.
inline bool flight_crashed(void) { return flight_status >= FLIGHT_CRASH_ROLL; }

extern mat3_t flight_cam;

// Airspeed runs 0 .. kMaxSpeed and is clamped to it every step. Exported for
// the same reason as kMaxThrottle below: sound.cc sizes its wind table by it,
// and a table that did not cover the whole range would read past its end at
// the top of the envelope.
static const uint16_t kMaxSpeed = 0x0F00;
extern int16_t flight_speed;
extern int16_t flight_vspeed;

#ifdef __FLIGHT_AOA__
// sin(flight path angle), at 4096 = 1.0 rather than the 256 the direction
// cosines use. Where the aircraft is actually *going*, as against `flight_cam.
// front`, which is where it is pointing; the angle between them is the angle
// of attack, and having the two be different things is the whole of the model
// (docs/flight.md 2).
//
// The extra four bits over a direction cosine are load bearing, not decorative.
// The flight path is integrated from a force, so a lift imbalance too small to
// move one unit of it is an imbalance the aircraft never feels; at the 256
// scale that dead band is 6% of the weight, and the flight path could not
// settle, only hunt across it. At 4096 the smallest step is under 0.4%, and
// the steady states really are steady.
extern int16_t flight_gamma;
inline int16_t flight_gamma_z(void) { return flight_gamma >> 4; }

// Angle of attack, at sixteen units to one of `front.z` - the same scale as
// flight_gamma, because it is a difference of two things carried at that scale.
// Positive is nose above the flight path.
//
// Also the lift coefficient: the lift slope is one unit of C_L per unit of
// this, which is why the model has no lift-curve table (see _flight_cl16).
extern int16_t flight_alpha16;
// The same angle in the units front.z uses, for display.
inline int16_t flight_alpha(void) { return flight_alpha16 >> 4; }
#endif // __FLIGHT_AOA__

// Throttle runs 0 .. kMaxThrottle inclusive, so 25 discrete steps. Exported
// rather than kept private to flight.cc because sound.cc sizes its engine
// pitch table by it - a table one entry short of the throttle range would
// read past its end at full power.
static const uint8_t kMaxThrottle = 0x18;
extern uint8_t flight_throttle;
extern uint32_t flight_fuel;
extern uint8_t flight_flap;
extern uint8_t flight_gear;

// True while the aircraft is below its stall speed - the same condition that
// drives the nose-down break in flight_advance(), published rather than
// recomputed so the panel lamp and the stall warning in sound.cc cannot drift
// from the physics. Always false in ground mode (there is no stall on the
// runway), and false on reset. It holds its last value while paused or
// crashed, since flight_advance() does no physics in either case.
extern uint8_t flight_stall;

// One-shot events, for the audio driver. Set during a step of
// flight_advance(), published as a complete set at the end of it.
#define FLIGHT_EV_TOUCHDOWN 0x01
#define FLIGHT_EV_GEAR 0x02
#define FLIGHT_EV_FLAP 0x04
// Set on the step in which the aircraft is wrecked, and never again: the guard
// at the top of flight_advance() returns early on every later frame, so
// reaching the end of a step while crashed means it happened during that step.
#define FLIGHT_EV_CRASH 0x08

// What happened during the step that flight_gen counts. Unlike flight_stall
// this is an edge, not a level: it is true for exactly the one step in which
// the thing occurred.
extern uint8_t flight_events;

// Incremented once per completed step, and always *after* flight_events has
// been written, so an observer that sees a new generation is guaranteed to see
// the complete event set that goes with it.
//
// The counter is what makes each set consumable exactly once, and that is not
// merely tidy. flight_advance() returns early once the aircraft is wrecked, so
// flight_events keeps its last value forever afterwards; without the
// generation a consumer would retrigger that final event on every frame for
// the rest of the flight.
extern uint8_t flight_gen;

// Navigation state
extern uint8_t flight_nav;
extern int16_t flight_nav_x;
extern int16_t flight_nav_y;
extern uint8_t flight_true_heading;
extern uint8_t flight_nav_heading;

// The most navpoints any mission produces. Walking kMissionWpBegin/End
// against the "skip a (0, 0) waypoint" rule, missions 06, 08 and 09 reach
// four and nothing reaches five, so this is a declared limit rather than an
// observation: flight_init_from_mission() clamps to it, and the map view
// only has digit stencils for '1'..'4'.
static const uint8_t kMaxNavPoints = 4;

// Navpoint positions in world coordinates, high byte = world unit. Read by
// the map view to place the navpoint digits; flight_num_nav_points is how
// many of the arrays are live.
extern uint16_t flight_nav_point_x[kMaxNavPoints];
extern uint16_t flight_nav_point_y[kMaxNavPoints];
extern uint8_t flight_num_nav_points;

// Recent flight path, in the map view's pixel space: 0..127 on both axes over
// the 128 x 128 map area, px across and py down, already rotated to N up and
// W left. Sampled once per flight_advance() and reset by
// flight_init_from_mission(), so the trail is per attempt and R wipes it.
//
// A sample is only appended when it differs from the previous one. At
// kMaxSpeed the aircraft covers about 15 m per step and the smallest map
// pixel is 256 m, so the position advances by at most one pixel per step and
// consecutive entries are always 4-neighbours. The stored points therefore
// already form a connected path: no line drawing, no interpolation, and no
// wrap-seam special case.
//
// At cruise a vertical pixel takes about 1.7 s, so 128 samples is roughly
// 3.5 minutes of flight, about one full traverse of the map.
static const uint8_t kFlightPathLen = 128;
// Pointers, not arrays: on target these two live in the scratch map that
// sprites.h lays over the compressed sprite blob. See flight.cc.
extern uint8_t *const flight_path_px;
extern uint8_t *const flight_path_py;
// Where the aircraft is now, in the same pixel space. Updated every sample,
// including the ones the ring drops as a repeat, so the map can place the
// aircraft marker without repeating the conversion.
extern uint8_t flight_map_px;
extern uint8_t flight_map_py;
// Entries 0 .. flight_path_count - 1 are live in both cases: before the ring
// wraps, count is the write position; after it, every slot is live.
extern uint8_t flight_path_count;

// Aircraft position in world coordinates (24.8 fixed point)
extern int32_t flight_eye_x;
extern int32_t flight_eye_y;
extern int32_t flight_eye_z;

enum flight_input_t {
  FLIGHT_INPUT_NONE,
  FLIGHT_INPUT_ROLL_LEFT,
  FLIGHT_INPUT_ROLL_RIGHT,
  FLIGHT_INPUT_PITCH_UP,
  FLIGHT_INPUT_PITCH_DOWN,
  FLIGHT_INPUT_YAW_LEFT,
  FLIGHT_INPUT_YAW_RIGHT,
  FLIGHT_INPUT_THROTTLE_UP,
  FLIGHT_INPUT_THROTTLE_DOWN,
  FLIGHT_INPUT_TOGGLE_FLAP,
  FLIGHT_INPUT_TOGGLE_GEAR,
  FLIGHT_INPUT_TOGGLE_NAV,
  FLIGHT_INPUT_BRAKE,
};

// Mission waypoint tracking state
extern uint8_t flight_current_wp;
extern uint8_t flight_active_mission_idx;

// Free flight start state. Nothing in the game uses it any more - every
// flight now starts from a mission - but it is the fixture the host tests
// build their scenarios on, so it stays out of the C64 build only.
void flight_init();
void flight_init_from_mission(uint8_t mission_idx = 0);

void flight_advance();
void flight_input(enum flight_input_t input);

#pragma compile("flight.cc")

#endif // FLIGHT_H
