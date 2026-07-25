
#include "bcd.h"

uint8_t bcd_result[4];

void bcd_convert32(uint32_t value) {
  __asm {
    sed;

    lda #0; // Initialize BCD results to 0
    sta bcd_result;
    sta bcd_result+1;
    sta bcd_result+2;
    sta bcd_result+3;

    ldx #24; // Loop for 24 bits
  L1:
    asl value; // Shift input value left
    rol value+1;
    rol value+2;

    // Shift the carry into the BCD bytes and add to self (doubling)
    // In decimal mode, ADC performing (N + N + Carry) effectively
    // handles the "If > 4, add 3" logic of Double Dabble automatically.
    lda bcd_result+3;
    adc bcd_result+3;
    sta bcd_result+3;

    lda bcd_result+2;
    adc bcd_result+2;
    sta bcd_result+2;

    lda bcd_result+1;
    adc bcd_result+1;
    sta bcd_result+1;

    lda bcd_result;
    adc bcd_result;
    sta bcd_result;

    dex;
    bne L1;

    cld;
  }
}
