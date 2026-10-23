#include "mission.h"

// fpilot's missions. The tables are pmission.cc's, with the same names and the
// same encoding - see mission.h - so everything that reads them is shared.
//
// Three interceptions, all from runway 1. What each sends against the player
// is opponent.cc's kOpponentPaths, and what fails it, opponent.cc too. The
// goal is the same for all three, so they share one slice of the waypoints:
// every aircraft down (WP_TARGETS_DOWN), then home. Navpoint 1 is on the
// targets, wherever they are - the one, or midway between two - and
// navpoint 2 is runway 1. flight.cc names them by number when they are
// selected, TARGET and HOME BASE, so every mission keeps that order.
// Nothing can shoot yet, so for now no mission can be completed - only
// failed, where the mission says so.

const uint8_t kWaypointDefault = 1;
static const uint8_t kWaypointTargets = 0;
static const uint8_t kWaypointLanded1 = 1;

const uint8_t kMissionWpX[kMissionWpCount] = {
    0x00, // 00 (Targets down, anywhere; the navpoint follows them)
    0x20, // 01 (Runway 1)
};

const uint8_t kMissionWpY[kMissionWpCount] = {
    0x00, // 00 (Targets down, anywhere; the navpoint follows them)
    0x3F, // 01 (Runway 1)
};

const MissionWaypointConstraint kMissionWpConstraint[kMissionWpCount] = {
    WP_TARGETS_DOWN, // 00 (Targets down)
    WP_LANDED,       // 01 (Runway 1)
};

const uint8_t kMissionWpBegin[kMissionCount] = {
    kWaypointTargets, // 01
    kWaypointTargets, // 02
    kWaypointTargets, // 03
};
const uint8_t kMissionWpEnd[kMissionCount] = {
    kWaypointLanded1 + 1, // 01
    kWaypointLanded1 + 1, // 02
    kWaypointLanded1 + 1, // 03
};

const char *const kMissionTitles[kMissionCount] = {
    "01 RECON",       // 01
    "02 BOMBER RAID", // 02
    "03 AMBUSH",      // 03
};

const char *const kMissionDesc[kMissionCount] = {
    "THE ENEMY IS COLLECTING RECON.\n"    // 01
    "INTERCEPT AND DESTROY IT.",          //
    "BOMBERS ARE RAIDING THE CITY!\n"     // 02
    "CATCH THEM BEFORE THEY DO DAMAGE.",  //
    "AMBUSH THE ENEMY BASE AND DESTROY\n" // 03
    "THE PARKED PLANES.",                 //
};

// Where each starts: close to its targets, within ~90 s of them in every
// mission (test/mission_test.cc), heading north as every start does
// (flight_init_from_mission()). 01 and 03 in the air at cruise - throttle 18,
// whose level trim is ~2,170 (0x88 << 4) - 01 at 2,500 ft, 9 km south of the
// north edge, under the recon's way in; 03 at 1,000 ft 8 km short of runway
// 2, lined up beside it. 02 scrambles: on runway 1, engine at idle, the bombers coming
// south towards it.
const uint8_t kMissionStartX[kMissionCount] = {
    0x5C, // 01
    0x1C, // 02
    0x3D, // 03
};

const uint8_t kMissionStartY[kMissionCount] = {
    0x5F, // 01
    0x3F, // 02
    0xC0, // 03
};

const uint8_t kMissionStartZ[kMissionCount] = {
    0x05, // 01
    0x00, // 02
    0x02, // 03
};

const uint8_t kMissionStartSpeed[kMissionCount] = {
    0x88, // 01
    0x00, // 02
    0x88, // 03
};

const uint8_t kMissionStartThrottle[kMissionCount] = {
    0x12, // 01
    0x00, // 02
    0x12, // 03
};

// About 15 minutes at full throttle. The longest way home, from runway 2 in
// 03, is ~37 km: ~40,000 of the 139,000 at a cruise of throttle 18.
const uint8_t kMissionStartFuel[kMissionCount] = {
    0x22, // 01
    0x22, // 02
    0x22, // 03
};

const uint8_t kMissionWindX[kMissionCount] = {
    0x00, // 01
    0x00, // 02
    0x00, // 03
};

const uint8_t kMissionWindY[kMissionCount] = {
    0x00, // 01
    0x00, // 02
    0x00, // 03
};

uint8_t mission_completed[kMissionCount] = {
    0, // 01
    0, // 02
    0, // 03
};
