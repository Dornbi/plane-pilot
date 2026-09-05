// Target code size probe: builds the prototype for the 6510 so the byte cost
// of the AoA model can be a measurement rather than an estimate.
//
//   make size
//
// It is deliberately the smallest program that cannot have the model
// optimized away: `main` touches every input and reads every output, so
// oscar64 has to emit the whole step.
#include "aoa.h"

int main(void) {
  aoa_init();
  for (unsigned i = 0; i < 8; ++i) {
    aoa_throttle = (uint8_t)i;
    aoa_flap = (uint8_t)(i & 1);
    aoa_gear = (uint8_t)(i & 1);
    aoa_input((enum aoa_input_t)(i & 5));
    aoa_advance();
  }
  return (int)(aoa_speed + aoa_vspeed + aoa_gamma + aoa_alpha16 + aoa_lift +
               aoa_lift_z + aoa_stall + (int16_t)aoa_eye_z);
}
