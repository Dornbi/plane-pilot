# A swap through a temporary inside a loop loses the temporary's assignment

**STATUS: OPEN** against `v1.32.272-117-ga7305f9` and oscar64-main `b86277f`
(1.32.273).

**Version:** oscar64 `v1.32.272-117-ga7305f9` and `b86277f` (built from source,
macOS arm64)
**Severity:** silent wrong code
**Affects:** `-O1` and above. `-O0` is correct.

## Summary

The textbook conditional swap of two locals, in a loop body,

```c
for (uint8_t i = 0; i < 4; i++) {
  uint8_t a = ys[i];
  uint8_t b = ys[(i + 1) & 3];
  if (a > b) {
    uint8_t t = a;
    a = b;
    b = t;
  }
  ...
}
```

compiles as if `b = t` were not there: `b` keeps its old value, so after the
"swap" both variables hold the smaller one. Write the swap the other way round
(`t = b; b = a; a = t;`) and it is `a` that keeps its old value. Either way,
the variable assigned from the temporary is lost.

The generated code shows it. `a` is kept in a zero page temporary and updated
on the swap path. `b` is never stored anywhere: where it is used, the compiler
loads `ys[(i + 1) & 3]` from the table again, as if `b` still held what it was
initialised with.

```
.l5:
0888 : b9 b7 08 LDA $08b7,y ; ys[i]            a = ys[i]
088b : 85 1d __ STA ACCU + 2
088d : a5 1c __ LDA ACCU + 1
088f : 29 03 __ AND #$03
0891 : a8 __ __ TAY
0892 : b9 b7 08 LDA $08b7,y ; ys[(i+1)&3]      b
0895 : c5 1d __ CMP ACCU + 2
0897 : b0 02 __ BCS $089b
0899 : 85 1d __ STA ACCU + 2                   a = b
089b : ...
089f : a5 1d __ LDA ACCU + 2
08a1 : 9d bb 08 STA $08bb,x ; out[2i]          a
08a4 : b9 b7 08 LDA $08b7,y ; ys[(i+1)&3]      b, reloaded: b = t is gone
08a7 : 9d bc 08 STA $08bc,x ; out[2i+1]
```

What it takes:

- **A loop.** The same swap in straight-line code, with the two values read
  through `volatile` indices, is correct.
- **Nothing else.** The table does not have to be `const`. The two halves do
  not have to share a temporary: two swaps with a temporary each fail the same
  way. The loop body can be an inlined function whose parameters are swapped
  (below).

## Where it was found

Twice in this project.

1. `c64o/planes.cc`'s `_fill_poly()` swapped each edge's end points in its edge
   loop, with one temporary reused for `x` and then `y`. Every edge on one side
   of every polygon vanished, so each silhouette drew as its left edge only. The
   g++ build matched `lib/planes.py` byte for byte over four thousand cases,
   which is how it got past the host tests. `c64o/test/target_test.cc` now runs
   planes.cc on the 6510 as well (test 6). This report first blamed the reused
   temporary; that turned out to be incidental.
2. `c64o/poly.cc`'s `_poly_trace_edge_bresenham()` swaps its four parameters,
   with a temporary each. Out of line it is correct, because there is no loop
   around the swap. Inlined into `poly_fill()`'s loop over the edges (marking it
   `inline`, or marking `_poly_scan_lines2` `inline`, which tips oscar64 into
   inlining both), every upward edge collapsed to one point. Each scanline then
   got a single edge, and the terrain polygons drew as outlines.

## Reproduction

`oscar64_bug_swap_loop.c` (attached) is the 20-line form above: the four edges
of a closed path, two of them running upwards. Each edge's `a` and `b` land in
`out[]`.

```
oscar64 -ii=<oscar64>/include -g -O2 -o=s.prg oscar64_bug_swap_loop.c
tools/vice_dump.sh s.prg @spin out 8
```

| build | `out[]` | edges 2, 3 |
| --- | --- | --- |
| expected | `2 6 6 10 6 10 2 6` | |
| oscar64 `-O0` | `2 6 6 10 6 10 2 6` | correct |
| oscar64 `-O1`, `-O2`, `-O2 -Op -Oa -Oi -Oz -Oo` | `2 6 6 10 6 6 2 2` | `b` lost |

Both compiler versions above give the same results.

`oscar64_bug_swap_temp.c` (attached) is the original planes.cc form: `int16_t`
end points, one temporary for both swaps. It fails the same way with two
temporaries.

## Workaround

Pick the end points instead of swapping them, so that no variable is assigned
from a temporary:

```c
int8_t x1, x2;
uint8_t y1, y2;
if (ya > yb) {
  x1 = xb; y1 = yb; x2 = xa; y2 = ya;
} else {
  x1 = xa; y1 = ya; x2 = xb; y2 = yb;
}
```

`c64o/planes.cc` switched to this; its fill has since moved to assembly in
`planes_asm.cc`. For `poly.cc` it costs
15 bytes out of line, and inlining becomes safe: inlined with this form, the
frames are pixel-identical to the out-of-line build.
