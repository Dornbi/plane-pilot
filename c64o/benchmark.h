#ifndef BENCHMARK_H
#define BENCHMARK_H

#include "bool.h"
#include "print.h"
#include <stdint.h>

#if defined(__DEBUG_CYCLES__) || defined(__DEBUG_HORIZON__) ||                 \
    defined(__DEBUG_MODEL__)

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

#ifdef __DEBUG_HORIZON__
void bm_horiz_start(void);
void bm_horiz_end(uint16_t pos, const char *label);
#else
inline void bm_horiz_start(void) {}
inline void bm_horiz_end(uint16_t pos, const char *label) {}
#endif

#ifdef __DEBUG_MODEL__
void bm_model_start(void);
void bm_model_end(uint16_t pos, const char *label);
#else
inline void bm_model_start(void) {}
inline void bm_model_end(uint16_t pos, const char *label) {}
#endif

#endif