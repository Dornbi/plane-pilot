#include <assert.h>
#include <stdio.h>
#include <stdint.h>

#include "mission.h"
#include "world.h"

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  printf("=== WORLD MAP WAYPOINT VALIDATION SUITE ===\n\n");

  uint8_t checked = 0;
  for (uint8_t i = 0; i < kMissionWpCount; ++i) {
    uint8_t wx = kMissionWpX[i];
    uint8_t wy = kMissionWpY[i];
    MissionWaypointConstraint constraint = kMissionWpConstraint[i];

    if (wx == 0 && wy == 0) {
      continue; // Skip generic altitude waypoints without location
    }

    uint8_t row = (wy >> 3) & kWorldMapHeightMask;
    uint8_t col = (wx >> 3) & kWorldMapWidthMask;
    WorldMapType map_type = kWorldMap[row][col];

    printf("Checking Waypoint %2d (X=0x%02X, Y=0x%02X) -> Map Tile [%2d][%2d] = %d (Constraint %d)... ",
           i, wx, wy, row, col, (int)map_type, (int)constraint);

    assert(map_type != MAP_NOTHING);

    printf("PASS\n");
    checked++;
  }

  printf("\nALL %d WAYPOINTS SUCCESSFULLY MATCHED WORLD MAP OBJECTS!\n", checked);
  return 0;
}
