#ifndef BENCH_H
#define BENCH_H

// Full wall-clock frame timing for tools/bench.sh. See tools/bench.cc.

void bench_end(void);

#pragma compile("bench.cc")

#endif
