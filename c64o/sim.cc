#include "sim.h"

#include <string.h>

#include "benchmark.h"
#include "box.h"
#include "chardefs.h"
#include "damage.h"
#include "debrief.h"
#include "flight.h"
#include "gfx.h"
#include "help.h"
#include "keys.h"
#include "map.h"
#include "mem.h"
#include "mission.h"
#include "msg.h"
#include "panel.h"
#include "render.h"
#include "screen.h"
#include "sound.h"
#include "sprites.h"
#include "gun.h"
#include "opponent.h"
#include "view.h"
#include "world.h"

// Only reached on screen transitions and in the debug view, never from the
// per-frame render path, so the outliner's size-for-a-JSR trade is free here.
#pragma optimize(push, outline)

// Time compression (T): model steps per rendered frame, 1, 2 or 4. Every
// start of a mission, R included, sets it back to 1, and so does pausing
// (see the P handler in sim_run()).
static uint8_t sim_time_steps;

// The mission from its start, for entering the simulation and for R: the
// message line, the aircraft, the opponents and the guns.
static __noinline void _start_mission(uint8_t selected_mission) {
  msg_clear();
  sim_time_steps = 1;
#ifdef __ENABLE_COLOR_SCHEMES__
  // Before mem_init_mccm(), which puts the ground colours in $D022/$D023, and
  // with the horizon boxes' colour caches dropped: they hold the old sky.
  memcpy(&color_scheme,
         (const uint8_t *)kColorSchemes + kMissionScheme[selected_mission],
         sizeof(ColorScheme));
  box_invalidate();
#endif
  flight_init_from_mission(selected_mission);
  // Also rewrites the opponents' dot block, which the title's sprites were
  // over until the simulation was entered.
  opponent_reset(selected_mission);
  gun_reset();
  msg_show(kMissionTitles[selected_mission]);
}

static void _enter_simulation(uint8_t selected_mission) {
  // The menu is still on display and everything below rewrites what is under
  // it: both screen buffers, the character set, the panel bitmap, the sprite
  // pointers. Blank until gfx_init_raster_irqs() near the end of this function
  // starts the split, whose own $d011 writes bring the display back with a
  // finished screen behind it.
  screen_blank();

  memset(kScreenRamMain, kCharSolid11, 1000);
  memset(kScreenRamAlt, kCharSolid11, 1000);

  view_state = VIEW_CENTER;
  _start_mission(selected_mission);

  mem_switch_debug(false);
  mem_init_mccm();
  view_refresh_panel();
  mem_use_main_buffer();

  sprites_init();
  // Before the interrupts start, so the first sound_blit() has a defined
  // shadow to push rather than whatever the chip was left holding.
  sound_init();
  gfx_init_raster_irqs();
  render_snap_center_chars();
}

// The keys that close the map: M because it is the toggle that opened it,
// and RETURN, SPACE or Q to match the help screen's way out. Nothing else on
// the map does anything at all.
//
// Keep in sync with the "Controls" table in help.cc and README.md.
static const enum KeyScanCode kMapExitKeys[] = {KSCAN_M, KSCAN_SPACE,
                                                KSCAN_RETURN, KSCAN_Q};
static const uint8_t kMapExitKeyCount =
    sizeof(kMapExitKeys) / sizeof(kMapExitKeys[0]);

// Closes the map if one of those keys is down, and reports whether it did.
static inline bool _map_poll_exit(void) {
  for (uint8_t i = 0; i < kMapExitKeyCount; ++i) {
    if (keys_pressed(kMapExitKeys[i])) {
      map_exit();
      // Every one of these means something else in the simulation loop --
      // M would reopen the map on the very next iteration and Q would quit
      // to the menu -- so the press has to be spent here. Closing the map
      // is all Q does; quitting takes a second, deliberate press.
      keys_wait_release(kMapExitKeys[i]);
      return true;
    }
  }
  return false;
}

#pragma optimize(pop)

// The edge-triggered keys, in the bit order of the kToggleKey* masks below.
static const uint8_t kToggleKeys[] = {
    KSCAN_P,
#ifdef __ENABLE_DEBUG__
    KSCAN_D,
#endif
    KSCAN_F, KSCAN_G, KSCAN_N,
#ifdef __ENABLE_SOUND__
    KSCAN_V,
#endif
    KSCAN_T,
};
static const uint8_t kToggleKeyCount = sizeof(kToggleKeys);

// One bit per entry of kToggleKeys, in the same order. Keys that toggle state
// only act on their rising edge, otherwise holding the key would flip the
// state on every loop iteration.
//
// Each mask is the bit of the entry before it, shifted up if and only if that
// entry is compiled in. Written this way rather than as a per-build list of
// literals: the literals drifted from the table the moment both features were
// on in the same binary - D pushed G to 0x10 and V was still spelled 0x10 -
// and G and V then fired together. Derived from the same conditionals the
// table is built from, that cannot happen again.
static const uint8_t kToggleKeyP = 0x01;
#ifdef __ENABLE_DEBUG__
static const uint8_t kToggleKeyD = kToggleKeyP << 1;
static const uint8_t kToggleKeyF = kToggleKeyD << 1;
#else
static const uint8_t kToggleKeyF = kToggleKeyP << 1;
#endif
static const uint8_t kToggleKeyG = kToggleKeyF << 1;
static const uint8_t kToggleKeyN = kToggleKeyG << 1;
#ifdef __ENABLE_SOUND__
static const uint8_t kToggleKeyV = kToggleKeyN << 1;
static const uint8_t kToggleKeyT = kToggleKeyV << 1;
#else
static const uint8_t kToggleKeyT = kToggleKeyN << 1;
#endif
static_assert(sizeof(kToggleKeys) <= 8, "toggle mask is one byte");
// F, G and N are adjacent bits and their inputs adjacent values, so one loop
// passes each edge on as its input, in that order.
static_assert(FLIGHT_INPUT_TOGGLE_GEAR == FLIGHT_INPUT_TOGGLE_FLAP + 1 &&
                  FLIGHT_INPUT_TOGGLE_NAV == FLIGHT_INPUT_TOGGLE_FLAP + 2,
              "the toggle inputs follow kToggleKeys");

// The held-down flight and view keys, one scan code per entry in the order of
// flight_input_t (entries 0..8 -> ROLL_LEFT..BRAKE = 1..9) followed by
// view_state_t (entries 9..12 -> VIEW_LEFT..VIEW_BACK = 1..4). Polled once
// before bm_model_start() for the first step, and again before each extra step
// under time compression.
static const uint8_t kHeldKeys[] = {
    KSCAN_J, KSCAN_L,    KSCAN_K,     KSCAN_I, KSCAN_A,
    KSCAN_S, KSCAN_PLUS, KSCAN_MINUS, KSCAN_B, KSCAN_1,
    KSCAN_2, KSCAN_3,    KSCAN_4,
};
static const uint8_t kHeldFlightCount = 9;
static_assert(FLIGHT_INPUT_ROLL_LEFT == 1 && FLIGHT_INPUT_ROLL_RIGHT == 2 &&
                  FLIGHT_INPUT_PITCH_UP == 3 && FLIGHT_INPUT_PITCH_DOWN == 4 &&
                  FLIGHT_INPUT_YAW_LEFT == 5 && FLIGHT_INPUT_YAW_RIGHT == 6 &&
                  FLIGHT_INPUT_THROTTLE_UP == 7 &&
                  FLIGHT_INPUT_THROTTLE_DOWN == 8 &&
                  FLIGHT_INPUT_BRAKE == kHeldFlightCount,
              "kHeldKeys 0..8 follow flight_input_t");
static_assert(VIEW_LEFT == 1 && VIEW_CENTER == 2 && VIEW_RIGHT == 3 &&
                  VIEW_BACK == 4,
              "kHeldKeys 9..12 follow view_state_t");

static __noinline void _poll_held(void) {
  for (uint8_t i = 0; i < sizeof(kHeldKeys); ++i) {
    if (keys_pressed((enum KeyScanCode)kHeldKeys[i])) {
      if (i < kHeldFlightCount) {
        flight_input((enum flight_input_t)(i + 1));
      } else {
        view_update_view((view_state_t)(i - (kHeldFlightCount - 1)));
      }
    }
  }
}

// T's messages. The 2X one is patched to 4X in place, as flight.cc does its
// navpoint number.
static const char kTimeOffMsg[] = "TIME COMPRESSION OFF";
static char sim_time_msg[] = "TIME COMPRESSION 2X";
static const uint8_t kTimeMsgDigit = 17;

void sim_run(uint8_t selected_mission) {
  _enter_simulation(selected_mission);

  static uint8_t prev_toggles;
  prev_toggles = 0;

  while (1) {
    keys_poll();

    // Simulation loop
    const uint8_t toggle_edges =
        keys_edges(keys_scan(kToggleKeys, kToggleKeyCount), &prev_toggles);

    // The map is a modal page, like the help screen: the simulation is
    // frozen and nothing but the exit keys responds. That is partly because
    // there is no instrument feedback to fly on, and partly because the map
    // owns the character set, both screen buffers and the panel bitmap that
    // most of the other keys write to. The edges above are still collected
    // first, so a toggle pressed over the map is consumed rather than saved
    // up to fire on the way out.
    if (map_mode) {
      if (!_map_poll_exit()) {
        gfx_wait_vsync();
      }
      continue;
    }

#ifdef __ENABLE_DEBUG__
    if (toggle_edges & kToggleKeyD) {
      mem_switch_debug(!mem_debug_enabled);
    }
#endif
    if (toggle_edges & kToggleKeyP) {
      flight_paused = !flight_paused;
      // Pausing and resuming both end time compression. The pilot who pauses
      // is usually stopping to deal with something, and resuming at 4x with
      // nothing on screen to say so - the message is long gone - would be a
      // trap. It also makes T over the PAUSED banner harmless, which is why
      // T needs no test of its own: the banner swallows its message, and the
      // P that resumes undoes it.
      sim_time_steps = 1;
      if (flight_paused) {
        msg_show("PAUSED", MSG_FOREVER, true);
      } else {
        msg_clear();
      }
    }
#ifdef __ENABLE_SOUND__
    // The message is the whole reason this key is worth confirming: the
    // failure mode of audio is silence, so a V pressed by accident is
    // indistinguishable from the sound having broken. It also has to name the
    // step, since with three of them the key alone no longer says where you
    // ended up. msg_show() declines to overwrite the permanent PAUSED banner,
    // which is the right outcome here - pausing is already silent, so changing
    // the volume under it does nothing audible.
    if (toggle_edges & kToggleKeyV) {
      sound_cycle_volume();
      // The label moved to sound.cc when V became a key on every screen that
      // makes noise, rather than a flight-loop key with a table beside it.
      msg_show(sound_volume_label());
    }
#endif

    // Time compression: 2x, 4x, off, round again.
    if (toggle_edges & kToggleKeyT) {
      uint8_t steps = (uint8_t)(sim_time_steps << 1);
      if (steps > 4) {
        steps = 1;
      }
      sim_time_steps = steps;
      if (steps == 1) {
        msg_show(kTimeOffMsg);
      } else {
        sim_time_msg[kTimeMsgDigit] = (char)('0' + steps);
        msg_show(sim_time_msg);
      }
    }

    // fpilot goes by its debrief, which waits for Q again (debrief.h);
    // ppilot's is nothing, and Q goes straight back to the menu.
    if (key_pressed(KSCAN_Q)) {
      debrief_run();
      keys_wait_release(KSCAN_Q);
      return;
    }

    if (key_pressed(KSCAN_R)) {
      _start_mission(flight_active_mission_idx);
      view_update_view(VIEW_CENTER);
      mem_switch_debug(false);
    }
    uint8_t edges = toggle_edges;
    uint8_t toggle = FLIGHT_INPUT_TOGGLE_FLAP;
    do {
      if (edges & kToggleKeyF) {
        flight_input((enum flight_input_t)toggle);
      }
      edges >>= 1;
    } while (++toggle <= FLIGHT_INPUT_TOGGLE_NAV);
    // Only the opening half of the toggle lives here; the map's own key
    // handling above closes it, M included.
    if (key_pressed(KSCAN_M)) {
      map_enter();
      keys_wait_release(KSCAN_M);
      continue;
    }
    if (key_pressed(KSCAN_H)) {
      help_run();
      screen_restore_simulation();
      keys_wait_release(KSCAN_H);
    }
    _poll_held();
#ifdef __FPILOT__
    // Space fires (docs/projectiles.md); held, a round from each wing in turn.
    const bool trigger = keys_pressed(KSCAN_SPACE);
#else
    const bool trigger = false;
#endif

    bm_model_start();
    // One step per rendered frame, and never two unless T asked for them. The
    // model carries no elapsed time, so the aircraft covers the same ground
    // per frame however long the frame took and airspeed through the world
    // moves with what is on screen - about 13% between the runway and cruise
    // on a stock C64 (docs/framerate.md). That unevenness is accepted
    // deliberately; what it buys is a model cost that is bounded by
    // construction, so a heavy scene cannot also be the frame that owes the
    // model two steps. docs/flight.md section 8 has the reasoning and the
    // measurements on both sides of it.
    //
    // Time compression is the exception the pilot chooses: 2 or 4 whole steps
    // a frame, each the world's - the aircraft, the opponents and every round,
    // so waypoints, crashes, landings and hits are all still tested on each
    // step and nothing can be stepped over. The held controls are part of the
    // step too: _poll_held() ran once above for the first step and runs again
    // before each extra step, so a held roll, the throttle and the brakes do
    // 2 or 4 times as much a frame, exactly as much per step as they always
    // did, and each sees the state the step before left (the ground, the
    // stall). The view keys ride along in the same table; past the first step
    // they find the view already set. The toggles were taken once above, and
    // anything that runs in the player's time below - the message timers, the
    // hit flash, the panel and the sound - stays once a frame.
    //
    // The sound reads flight_events once a frame, and each step's publish
    // replaces the last one's, so a gear thump or a hit from any but the last
    // step would be lost. The frame's steps are ORed together for it instead.
    uint8_t events = 0;
    uint8_t steps = sim_time_steps;
    for (;;) {
      flight_advance();
      opponent_step();
      gun_step(trigger);
      events |= flight_events;
      if (--steps == 0) {
        break;
      }
      _poll_held();
    }
    flight_events = events;
    if (flight_crashed()) {
      // A crash is the end of the flight, so its message stays up until R or
      // Q. Completing a mission is not: flight.cc announces it for a few
      // seconds and the simulation carries on, which is why only crashes are
      // handled here.
      const char *msg = flight_status_text(flight_status, true);
      msg_show(msg, MSG_FOREVER, true);
    }
    msg_update();
    damage_frame();
    bm_model_end(630, "MDL:");

    bm_start();
    view_update_cam();
    world_update_roll_state();
    world_update_objects();

    render_snap_center_chars();
    msg_restore_color();
    render_fill_sky_ground();
    box_prepare();
    box_draw();
    world_render_grid();
    msg_render();

    // Everything above draws the viewport, so it has to finish before
    // mem_switch_buffer() puts that buffer on screen. The panel does not: its
    // bitmap at $F000 and the sprite registers are single-buffered and belong
    // to the raster handlers, not to either screen buffer. Running it here
    // rather than before the render spends it in the gap between the last
    // drawing call and the raster reaching the flip window, which is time
    // mem_switch_buffer() would otherwise spin away.
    //
    // Note it still reads this iteration's flight state - world_update_roll_state()
    // above produced roll_angle - so nothing is pipelined and no input gets a
    // frame staler. Moving flight_advance() or view_update_cam() down here
    // would fill the gap more completely, but only by rendering the previous
    // iteration's state, and at a ~10 Hz frame rate that added latency costs
    // more at the controls than the cycles are worth.
    panel_update_instruments();
    panel_maybe_print_debug();

    // Same gap, and for the same reason: sound_update() writes only
    // sound_shadow, which belongs to the raster handler rather than to either
    // screen buffer, so it has no business holding up the flip.
    sound_update();

    bm_total(990, "TOT:");
    mem_switch_buffer();
  }
}
