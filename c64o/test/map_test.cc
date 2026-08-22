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

    uint8_t row = ((wx + 4) >> 3) & kWorldMapHeightMask;
    uint8_t col = ((wy + 4) >> 3) & kWorldMapWidthMask;
    WorldMapType map_type = kWorldMap[row][col];

    printf("Checking Waypoint %2d (X=0x%02X, Y=0x%02X) -> Map Tile [%2d][%2d] = %d (Constraint %d)... ",
           i, wx, wy, row, col, (int)map_type, (int)constraint);

    assert(map_type != MAP_NOTHING);

    if (i == 1 || i == 5 || i == 6 || i == 7 || i == 11 || i == 15) {
      assert(map_type == MAP_OBJ_RUNWAY);
    } else if (i >= 2 && i <= 4) {
      assert(map_type == MAP_OBJ_LAKE || map_type == MAP_OBJ_POND);
    } else if (i >= 8 && i <= 10) {
      assert(map_type == MAP_OBJ_TOWN || map_type == MAP_OBJ_CITY);
    } else if (i >= 12 && i <= 14) {
      assert(map_type == MAP_OBJ_FIELD || map_type == MAP_OBJ_FIELD_SPARSE ||
             map_type == MAP_OBJ_FIELD_YELLOW || map_type == MAP_OBJ_FIELD_YELLOW_SPARSE);
    }

    printf("PASS\n");
    checked++;
  }

  printf("\nALL %d WAYPOINTS SUCCESSFULLY MATCHED WORLD MAP OBJECTS!\n", checked);
  return 0;
}
