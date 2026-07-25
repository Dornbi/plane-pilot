#ifndef BCD_H
#define BCD_H

#include <stdint.h>

extern uint8_t bcd_result[4];
void bcd_convert32(uint32_t value);

#pragma compile("bcd.cc")

#endif