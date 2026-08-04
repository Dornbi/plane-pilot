#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdint.h>

#include "bool.h"
#include "print.h"

#if defined(__DEBUG_CYCLES__) || defined(__DEBUG_VIEW__) ||                    \
    defined(__DEBUG_MODEL__) || defined(__DEBUG_POLY__)

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

#ifdef __DEBUG_VIEW__
void bm_view_start(void);
void bm_view_end(uint16_t pos, const char *label);
#else
inline void bm_view_start(void) {}
inline void bm_view_end(uint16_t pos, const char *label) {}
#endif

#ifdef __DEBUG_MODEL__
void bm_model_start(void);
void bm_model_end(uint16_t pos, const char *label);
#else
inline void bm_model_start(void) {}
inline void bm_model_end(uint16_t pos, const char *label) {}
#endif

#ifdef __DEBUG_POLY__
void bm_poly_start(void);
void bm_poly_end(uint16_t pos, const char *label);
#else
inline void bm_poly_start(void) {}
inline void bm_poly_end(uint16_t pos, const char *label) {}
#endif

#endif