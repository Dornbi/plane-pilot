# Reusing one temporary for two swaps loses the second swap's temporary

**STATUS: OPEN** against `v1.32.272-117-ga7305f9`.

**Version:** oscar64 `v1.32.272-117-ga7305f9` (built from source, macOS arm64)
**Severity:** silent wrong code
**Affects:** `-O2 -Op -Oa -Oi -Oz -Oo`. `-O0` is correct.

## Summary

Two swaps in a row through the same temporary,

```c
if (yb < ya) {
  int16_t t = xa0;
  xa0 = xb0;
  xb0 = t;
  t = ya;
  ya = yb;
  yb = t;
}
```

inside a loop over a polygon's edges, compile as if the second `t = ya` were
not there: `yb` comes out equal to the *new* `ya`, so an edge running bottom to
top is turned into one of zero height. The `x` swap is right.

Found in `c64o/planes.cc`, whose `_fill_poly()` swapped each edge's end points
exactly like this. Every edge on one side of every polygon vanished, so each
silhouette drew as its left edge only. The g++ build of the same file matched
`lib/planes.py` byte for byte over four thousand cases, which is how it got
past the host tests; `c64o/test/target_test.cc` now runs planes.cc on the
6510 as well (test 6).

## Reproduction

`oscar64_bug_swap_temp.c` (attached). Four edges of a rectangle; the fourth,
from (10, 9) to (10, 0), runs bottom to top and needs the swap. Each edge's `ya`
and `yb * 100 + xa0` land in `out[]`.

```
oscar64 -ii=<oscar64>/include -g -O0 -o=s.prg oscar64_bug_swap_temp.c
oscar64 -ii=<oscar64>/include -g -O2 -Op -Oa -Oi -Oz -Oo -o=s.prg oscar64_bug_swap_temp.c
tools/vice_dump.sh s.prg @spin out 16
```

| build | `out[]` | edge 3 |
| --- | --- | --- |
| gcc `-O2` (host) | `0 0 0 920 0 0 0 910` | `yb` 9 |
| oscar64 `-O0` | `0 0 0 920 0 0 0 910` | `yb` 9 |
| oscar64 `-O2 …` | `0 0 0 920 0 0 0 10` | `yb` **0** |

## Workaround

Pick the end points once instead of swapping them, with no temporary to reuse:

```c
int16_t xa0, ya, xb0, yb;
if (y[j] < y[i]) {
  xa0 = x[j]; ya = y[j]; xb0 = x[i]; yb = y[i];
} else {
  xa0 = x[i]; ya = y[i]; xb0 = x[j]; yb = y[j];
}
```

That is what `c64o/planes.cc` does now, with a comment pointing here.
