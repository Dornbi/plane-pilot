<!-- STATUS: OPEN against v1.32.272-117-ga7305f9 and b86277f. Not yet filed. -->
# A swap through a temporary inside a loop loses the temporary's assignment

**Versions:** `v1.32.272-117-ga7305f9` and current main `b86277f` (1.32.273),
built from source on macOS arm64
**Affects:** `-O1` and above. `-O0` is correct.
**Severity:** silent wrong code

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
  out_a[2 * i] = a;
  out_a[2 * i + 1] = b;
}
```

compiles as if `b = t` were not there: `b` keeps its old value, so after the
swap both variables hold the smaller one. Written the other way round
(`t = b; b = a; a = t;`), it is `a` that keeps its old value. Either way, the
variable assigned from the temporary is lost.

The generated code shows it (`b86277f -O2`, `case_a` from the attached file).
`a` lives in `ACCU + 2` and is updated on the swap path. `b` is never stored
anywhere: where it is used, the table is read again, as if `b` still held its
initial value.

```
.l5:
096b : b9 e4 19 LDA $19e4,y ; (ys[0] + 0)      a = ys[i]
096e : 85 1d __ STA ACCU + 2
0970 : a5 1c __ LDA ACCU + 1
0972 : 29 03 __ AND #$03
0974 : a8 __ __ TAY
0975 : b9 e4 19 LDA $19e4,y ; (ys[0] + 0)      b = ys[(i + 1) & 3]
0978 : c5 1d __ CMP ACCU + 2
097a : b0 02 __ BCS $097e ; (case_a.s7 + 0)
.s6:
097c : 85 1d __ STA ACCU + 2                   a = b
.s7:
097e : a5 1b __ LDA ACCU + 0
0980 : 0a __ __ ASL
0981 : aa __ __ TAX
0982 : a5 1d __ LDA ACCU + 2
0984 : 9d f4 19 STA $19f4,x ; (out_a[0] + 0)   a
0987 : b9 e4 19 LDA $19e4,y ; (ys[0] + 0)      b, read again: b = t is gone
098a : 9d f5 19 STA $19f5,x ; (out_a[0] + 1)
```

What it takes:

- **A loop.** The same swap in straight-line code is correct.
- **Nothing else that I could find.** The table does not have to be `const`.
  The values can be `int16_t`. Two swaps with a temporary each fail the same
  way as two swaps sharing one temporary.
- **Inlining can create it.** A function that swaps its own parameters is
  correct out of line. Inlined into a caller's loop, it has the bug (case B
  in the attached file). This is how it showed up in a polygon filler, whose
  edge tracer swaps an edge's end points into top-to-bottom order: every
  upward edge collapsed to a point, and the polygons drew as outlines.

## Reproduction

`oscar64_bug_swap_loop.c` (attached). It puts the end points of the four edges
of a closed path in top-to-bottom order, two of them running upwards. Case A
is the loop above; case B swaps the parameters of an `inline` function called
from a loop.

```
gcc -O2 -o chk oscar64_bug_swap_loop.c && ./chk     # PASS
oscar64 -O0 -e oscar64_bug_swap_loop.c              # PASS
oscar64 -O2 -e oscar64_bug_swap_loop.c              # FAIL
```

```
FAIL
A: 2 6 6 10 6 6 2 2
B: 20 2 24 6 24 6 20 10 16 6 16 6 20 2 20 2
```

The expected values are `A: 2 6 6 10 6 10 2 6` and
`B: 20 2 24 6 24 6 20 10 16 6 20 10 20 2 16 6`. `-O1`, `-O2` and
`-O2 -Op -Oa -Oi -Oz -Oo` all fail, on both versions above.

## Workaround

Pick the values instead of swapping them, so that no variable is assigned from
a temporary:

```c
uint8_t a, b;
if (ya > yb) {
  a = yb;
  b = ya;
} else {
  a = ya;
  b = yb;
}
```
