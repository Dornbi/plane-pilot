#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdint.h>

#include "bool.h"
#include "print.h"

#if defined(__DEBUG_CYCLES__) || defined(__ENABLE_DEBUG__) ||                    \
    defined(__DEBUG_POLY__)

void bm_init(void);
void bm_start(void);
void bm_end(uint16_t pos, const char *label);
void bm_total(uint16_t pos, const char *label);
#pragma compile("benchmark.cc")
#else
inline void bm_init(void) {}
inline void bm_start(void) {}
inline void bm_end(uint16_t pos, const char *label) {}
inline void bm_total(uint16_t pos, const char *label) {}
#endif

#ifdef __ENABLE_DEBUG__
void bm_view_start(void);
void bm_view_end(uint16_t pos, const char *label);
void bm_model_start(void);
void bm_model_end(uint16_t pos, const char *label);
#else
inline void bm_view_start(void) {}
inline void bm_view_end(uint16_t pos, const char *label) {}
inline void bm_model_start(void) {}
inline void bm_model_end(uint16_t pos, const char *label) {}
#endif

// The slot numbers stay outside the guard: world.cc names them at every call
// site and is compiled into polydemo too, which has no __ENABLE_DEBUG__ and so
// takes the inline no-ops below.
#define BM_SUB_POLY 0
#define BM_SUB_CLOUDS 1
#define BM_SUB_SPRITES 2
#define BM_SUB_COUNT 3

#ifdef __ENABLE_DEBUG__
// Sub-stage counters, for work that runs *inside* one of the stages above and
// may run several times in a frame.
//
// Separate from bm_start()/bm_end() for three reasons. They keep their own
// start register, so wrapping a call that sits inside GRD or UPD does not
// clobber the enclosing measurement. They accumulate rather than print, so a
// routine called once per object in the grid walk reports what it cost over
// the whole frame rather than what the last call cost. And they deliberately
// do not feed bm_total(): those cycles are already inside an enclosing stage,
// and adding them again would stop TOT being the sum of the stages.
//
// So the numbers they show are a breakdown of the stages above, not extra
// terms beside them. PLY is part of GRD; CLD and SPR are parts of UPD.
//
// One start register serves all three because they never nest inside each
// other - PLY runs in the grid walk, CLD and SPR in world_update_objects().
void bm_sub_start(void);
void bm_sub_end(uint8_t slot);
// Prints the frame's accumulated total for one slot and clears it.
void bm_sub_show(uint8_t slot, uint16_t pos, const char *label,
                 uint8_t num_digits = 6);
#else
inline void bm_sub_start(void) {}
inline void bm_sub_end(uint8_t slot) {}
inline void bm_sub_show(uint8_t slot, uint16_t pos, const char *label,
                        uint8_t num_digits = 6) {}
#endif

#ifdef __DEBUG_POLY__
void bm_poly_start(void);
void bm_poly_end(uint16_t pos, const char *label);
#else
inline void bm_poly_start(void) {}
inline void bm_poly_end(uint16_t pos, const char *label) {}
#endif

#endif