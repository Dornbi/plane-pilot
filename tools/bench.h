#ifndef BENCH_H
#define BENCH_H

// Render-work timing for tools/bench.sh. See tools/bench.cc.

void bench_begin(void);
void bench_end(void);

#pragma compile("bench.cc")

#endif
