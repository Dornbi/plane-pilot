#ifndef SID_H
#define SID_H

#ifdef __OSCAR64__
#include <c64/sid.h>

// The register block as flat bytes, which is how sound.cc and sound_blit()
// address it: 25 consecutive stores, no struct layout assumptions.
#define SID_REGS ((volatile uint8_t *)0xD400)

#else
#include <stdint.h>

// Host fallback, same shape as oscar64's <c64/sid.h>, so sound.cc can be
// compiled and tested off the C64 (see test/). Only what sound.cc needs is
// mirrored - the note tables and the PAL/NTSC frequency macros are not.
typedef uint8_t byte;

struct SID {
  struct Voice {
    volatile unsigned freq;
    volatile unsigned pwm;
    volatile byte ctrl;
    volatile byte attdec;
    volatile byte susrel;
  } voices[3];

  volatile unsigned ffreq;
  volatile byte resfilt;
  volatile byte fmodevol;

  volatile byte potx;
  volatile byte poty;
  volatile byte random;
  volatile byte env3;
};

// Where the host build's "chip" lives. The test defines it and points it at a
// 29-byte buffer, so a write-through is an array store rather than a segfault
// at address 0xD400, and the test can assert on what reached the chip as well
// as on what is in the shadow.
extern struct SID *sid_host;
#define sid (*sid_host)
#define SID_REGS ((volatile uint8_t *)sid_host)

#define SID_CTRL_GATE 0x01
#define SID_CTRL_SYNC 0x02
#define SID_CTRL_RING 0x04
#define SID_CTRL_TEST 0x08
#define SID_CTRL_TRI 0x10
#define SID_CTRL_SAW 0x20
#define SID_CTRL_RECT 0x40
#define SID_CTRL_NOISE 0x80

#define SID_FILTER_1 0x01
#define SID_FILTER_2 0x02
#define SID_FILTER_3 0x04
#define SID_FILTER_X 0x08

#define SID_FMODE_LP 0x10
#define SID_FMODE_BP 0x20
#define SID_FMODE_HP 0x40
#define SID_FMODE_3_OFF 0x80

#endif

#endif // SID_H
