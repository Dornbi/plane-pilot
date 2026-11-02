#include "mission.h"

// fpilot's missions. The tables are pmission.cc's, with the same names and the
// same encoding - see mission.h - so everything that reads them is shared.
//
// Four interceptions, the fourth the scramble fpilot started with, and a
// head-on meeting with two hunting fighters. 06-10 are placeholders, so that
// the menu, the tables and the RAM they cost are there for ten: each starts
// parked on runway 1, as 02 and 04 do, and sends nothing, so its goals are
// met at once and landing completes it. What each sends against the
// player is opponent.cc's kOpponentPaths, and what fails it, opponent.cc
// too. The goal is the same for all five, so they share one slice of the
// waypoints: every aircraft down (WP_TARGETS_DOWN), then home.
// Navpoint 1 is on the targets, wherever they are - the one, or midway
// between two - and navpoint 2 is runway 1. flight.cc names them by number
// when they are selected, TARGET and HOME BASE, so every mission keeps that
// order. The targets go down to the guns (gun.cc), three hits each.

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
    kWaypointTargets, // 04
    kWaypointTargets, // 05
    kWaypointTargets, // 06
    kWaypointTargets, // 07
    kWaypointTargets, // 08
    kWaypointTargets, // 09
    kWaypointTargets, // 10
};
const uint8_t kMissionWpEnd[kMissionCount] = {
    kWaypointLanded1 + 1, // 01
    kWaypointLanded1 + 1, // 02
    kWaypointLanded1 + 1, // 03
    kWaypointLanded1 + 1, // 04
    kWaypointLanded1 + 1, // 05
    kWaypointLanded1 + 1, // 06
    kWaypointLanded1 + 1, // 07
    kWaypointLanded1 + 1, // 08
    kWaypointLanded1 + 1, // 09
    kWaypointLanded1 + 1, // 10
};

const char *const kMissionTitles[kMissionCount] = {
    "01 RECON INTERCEPT", // 01
    "02 BOMBER RAID",     // 02
    "03 AMBUSH",          // 03
    "04 SCRAMBLE",        // 04
    "05 HEAD ON",         // 05
    "06 RESERVED",        // 06
    "07 RESERVED",        // 07
    "08 RESERVED",        // 08
    "09 RESERVED",        // 09
    "10 RESERVED",        // 10
};

// One copy for every placeholder: oscar64 keeps each literal apart.
static const char kMissionReserved[] = "NOT WRITTEN YET.";

const char *const kMissionDesc[kMissionCount] = {
    "A RECON PLANE IS COMING IN HIGH.\n"  // 01
    "INTERCEPT IT BEFORE IT ESCAPES.",    //
    "BOMBERS ARE RAIDING THE CITY!\n"     // 02
    "CATCH THEM BEFORE THEY DO DAMAGE.",  //
    "AMBUSH THE ENEMY BASE AND DESTROY\n" // 03
    "THE PARKED PLANES.",                 //
    "BANDITS ARE ATTACKING THE FIELD.\n"  // 04
    "DEFEND IT AS QUICKLY AS YOU CAN!",   //
    "TWO FIGHTERS ARE COMING STRAIGHT\n"  // 05
    "AT YOU. SHOOT THEM DOWN!",           //
    kMissionReserved,                     // 06
    kMissionReserved,                     // 07
    kMissionReserved,                     // 08
    kMissionReserved,                     // 09
    kMissionReserved,                     // 10
};

// Where each starts: close enough to its targets that they are on screen,
// if only as the dot, within 10 s of flying straight on - 20 s of the
// lift-off in 02 - and within 1 km inside ~90 s (test/mission_test.cc). 01
// and 03 in the air at cruise - throttle 18, whose level trim is ~2,170
// (0x88 << 4). 01 at 2,500 ft heading north, 4.5 km south of the north edge,
// under the recon's way in; 03 at 1,000 ft heading east, 1.8 km west of the
// parked aircraft at runway 2, midway between them. 02 and 04 scramble: on
// runway 1 heading north, engine at idle, the bombers coming south towards
// it in 02, the bandits already about in 04. 05 in the air at cruise at
// 3,000 ft heading north, the hunters coming south at it from 4.6 km. The
// placeholders as 02 and 04.
const uint8_t kMissionStartX[kMissionCount] = {
    0x5C, // 01
    0x1C, // 02
    0x5D, // 03
    0x1C, // 04
    0x30, // 05
    0x1C, // 06
    0x1C, // 07
    0x1C, // 08
    0x1C, // 09
    0x1C, // 10
};

const uint8_t kMissionStartY[kMissionCount] = {
    0x5F, // 01
    0x3F, // 02
    0xCE, // 03
    0x3F, // 04
    0x90, // 05
    0x3F, // 06
    0x3F, // 07
    0x3F, // 08
    0x3F, // 09
    0x3F, // 10
};

const uint8_t kMissionStartTurns[kMissionCount] = {
    0, // 01
    0, // 02
    3, // 03
    0, // 04
    0, // 05
    0, // 06
    0, // 07
    0, // 08
    0, // 09
    0, // 10
};

const uint8_t kMissionStartZ[kMissionCount] = {
    0x05, // 01
    0x00, // 02
    0x02, // 03
    0x00, // 04
    0x06, // 05
    0x00, // 06
    0x00, // 07
    0x00, // 08
    0x00, // 09
    0x00, // 10
};

const uint8_t kMissionStartSpeed[kMissionCount] = {
    0x88, // 01
    0x00, // 02
    0x88, // 03
    0x00, // 04
    0x88, // 05
    0x00, // 06
    0x00, // 07
    0x00, // 08
    0x00, // 09
    0x00, // 10
};

const uint8_t kMissionStartThrottle[kMissionCount] = {
    0x12, // 01
    0x00, // 02
    0x12, // 03
    0x00, // 04
    0x12, // 05
    0x00, // 06
    0x00, // 07
    0x00, // 08
    0x00, // 09
    0x00, // 10
};

// About 15 minutes at full throttle. The longest way home, from runway 2 in
// 03, is ~18 km: ~40,000 of the 139,000 at a cruise of throttle 18.
const uint8_t kMissionStartFuel[kMissionCount] = {
    0x22, // 01
    0x22, // 02
    0x22, // 03
    0x22, // 04
    0x22, // 05
    0x22, // 06
    0x22, // 07
    0x22, // 08
    0x22, // 09
    0x22, // 10
};

const uint8_t kMissionWindX[kMissionCount] = {
    0x00, // 01
    0x00, // 02
    0x00, // 03
    0x00, // 04
    0x00, // 05
    0x00, // 06
    0x00, // 07
    0x00, // 08
    0x00, // 09
    0x00, // 10
};

const uint8_t kMissionWindY[kMissionCount] = {
    0x00, // 01
    0x00, // 02
    0x00, // 03
    0x00, // 04
    0x00, // 05
    0x00, // 06
    0x00, // 07
    0x00, // 08
    0x00, // 09
    0x00, // 10
};

uint8_t mission_completed[kMissionCount] = {
    0, // 01
    0, // 02
    0, // 03
    0, // 04
    0, // 05
    0, // 06
    0, // 07
    0, // 08
    0, // 09
    0, // 10
};
