#ifndef CIA_H
#define CIA_H

#include <stdint.h>

#ifdef __OSCAR64__
#include <c64/cia.h>
#else
typedef uint8_t byte;
typedef uint16_t word;
struct CIA {
  volatile byte pra, prb;
  volatile byte ddra, ddrb;
  volatile word ta, tb;
  volatile byte todt, tods, todm, todh;
  volatile byte sdr;
  volatile byte icr;
  volatile byte cra, crb;
};

#define cia1 (*((struct CIA *)0xdc00))
#define cia2 (*((struct CIA *)0xdd00))

// inline, not plain static: cpu_test includes cia.h for the register struct
// and never calls this, and -Wall warns about an unused static function. An
// inline one is exempt, and this is a stand-in for oscar64's own routine
// rather than something a host build ever wants to run.
static inline void cia_init() {}
#endif

#endif