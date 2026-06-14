#include "view.h"

#include "model.h"
#include "vec.h"
#include "world.h"

view_state_t view_state = VIEW_CENTER;

void view_update_cam() {
  if (view_state == VIEW_CENTER) {
    world_cam.front = model_cam.front;
    world_cam.left = model_cam.left;
  } else if (view_state == VIEW_LEFT) {
    world_cam.front = model_cam.left;
    world_cam.left = model_cam.front;
    vec_negate(&world_cam.left);
  } else {
    world_cam.front = model_cam.left;
    vec_negate(&world_cam.front);
    world_cam.left = model_cam.front;
  }
  world_cam.up = model_cam.up;
}
