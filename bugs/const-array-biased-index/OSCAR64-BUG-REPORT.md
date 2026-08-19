# Reading a `const` array of structs at a biased index yields 0 and the table is dropped

**STATUS: FIXED** in `v1.32.272-113-g5638ec5` ("Fix constant value range
analysis for linear combination index values and a base offset"). The reduced
matrix below goes 0-of-9 miscompiled and `oscar64_bug_selfcheck.c` prints PASS.
The `volatile` finding at the end is *not* fixed, and a related case that the
same real code still hits is written up in `../const-table-struct-select/OSCAR64-BUG-2-REPORT.md`.


**Version:** oscar64 1.32.272 (built from source, Linux aarch64)
**Severity:** silent wrong code — no diagnostic, correct-looking program
**Affects:** every optimisation level, `-O0` through `-O3` and `-Os`

## Summary

When a `const` array whose element stride is greater than 1 byte is indexed by a
runtime value that carries a non-zero constant offset, the load is replaced by
an immediate `0` and the array is not emitted into the binary at all. The offset
does not have to be written by hand — a preceding comparison that tells the
compiler the index is at least *K* is enough to trigger it on an unbiased
`tb[i]`.

Making the array non-`const` produces correct code, which is what makes this
easy to miss: the table looks like ordinary read-only data and is silently
replaced by a constant.

## Minimal reproduction

`min.c` (attached). Every access is in bounds and the code is well-defined C —
the subtraction only ever executes on the branch where `i >= 4`.

```c
#include <stdint.h>

struct b_t {
  uint8_t pad[2];
  uint8_t v;
};

const struct b_t tb[4] = {
    {{0, 0}, 20}, {{0, 0}, 21}, {{0, 0}, 22}, {{0, 0}, 23},
};

volatile uint8_t out;

void f(uint8_t i) {  // i is 0..7
  uint8_t v;
  if (i < 4) {
    v = 99;
  } else {
    v = tb[i - 4].v;  // index is 0..3 here: always in bounds
  }
  out = v;
}

int main(void) {
  f(*(volatile uint8_t *)0xD012 & 7);  // raster line, so i is unknowable
  return 0;
}
```

```
oscar64 -ii=<oscar64>/include -O2 -Op -Oa -Oi -Oz -Oo -o=min.prg min.c
```

**Expected** in the `else` arm: an indexed load from `tb`, e.g. `LDA
__multab3L,x` / `TAX` / `LDA tb+2,x`.

**Actual** (`min.asm`):

```asm
main: ; main()->i16
0880 : ad 12 d0 LDA $d012
0883 : 29 07 __ AND #$07
0885 : c9 04 __ CMP #$04
0887 : b0 08 __ BCS $0891 ; (main.s5 + 0)
.s6:
0889 : a9 63 __ LDA #$63      ; v = 99, correct
088b : 85 f7 __ STA $f7 ; (out + 0)
088d : a9 00 __ LDA #$00
088f : 90 04 __ BCC $0895 ; (main.s3 + 0)
.s5:
0891 : a9 00 __ LDA #$00      ; <-- should be tb[i - 4].v, i.e. 20..23
0893 : 85 f7 __ STA $f7 ; (out + 0)
.s3:
0895 : 85 1b __ STA ACCU + 0
0897 : 85 1c __ STA ACCU + 1
0899 : 60 __ __ RTS
```

`grep '^tb:' min.asm` finds nothing — the table is not in the binary.

`selfcheck.c` (attached) is the same thing as a runnable program that prints
expected against actual and ends in `PASS` or `FAIL`. It also includes the
non-`const` control case, which is correct, so one run distinguishes the two.
Indices come from a `volatile` array so the compiler cannot know them.

```
i   A(exp) got   B(exp) got   C(exp) got
4      20  0       20  0       20  20
5      21  0       21  0       21  21
6      22  0       22  0       22  22
7      23  0       23  0       23  23
FAIL
```

Columns A and B are the two shapes below; C is the non-`const` control.

## What does and does not trigger it

`tb` is `const struct { uint8_t pad[2]; uint8_t v; } tb[]`, `i` unknown at
compile time, all accesses in bounds.

| Shape | Result |
| :--- | :--- |
| `out = tb[i].v;` | ok |
| `out = tb[i - 4].v;` | **folded to 0, table dropped** |
| `out = tb[i + 1].v;` | ok |
| `uint8_t j = i - 4; out = tb[j].v;` | ok |
| `const struct b_t *p = tb + i - 4; out = p->v;` | ok |
| `out = tb[i - 0].v;` | ok |
| `if (i < 4) v = 99; else v = tb[i].v;` | **folded to 0, table dropped** |
| `if (i >= 4) v = 99; else v = tb[i].v;` | ok |
| any of the above with `tb` not `const` | ok |

Two conditions appear to be jointly necessary:

1. **The array is `const`.** Dropping the qualifier fixes every case.
2. **The effective index has a non-zero constant subtracted from it**, either
   written explicitly or inferred. The last two rows are the interesting pair:
   the same expression `tb[i].v` compiles correctly when the branch bounds `i`
   from *above* and miscompiles when it bounds `i` from *below*, which suggests
   value-range analysis rebases a known-`>= K` index into the biased form and
   then hits the same path.

Element stride matters:

| Stride | Result |
| ---: | :--- |
| 1 (`const uint8_t tb[]`, or a one-byte struct) | ok |
| 2 | **miscompiled** |
| 3 | **miscompiled** |
| 4 | **miscompiled** |

Hoisting the subtraction into its own variable, or doing the arithmetic on a
pointer rather than an index, both produce correct code — presumably because
the range information is lost at that point. Those are also the workarounds, if
anyone needs one before a fix; a third is to index a single flat table so no
offset is ever needed.

## How it showed up

A C64 flight simulator draws clouds from a ten-rung size ladder. The five small
rungs are one hardware sprite, the five large ones are a pair, and the metadata
lived in two `const` tables with the second indexed as
`kSpriteDefCloud2Sprite[rung - kRungStacked]`:

```c
if (rung < kRungStacked) {
  const sprite_cloud1_meta_t *m = &kSpriteDefCloud1Sprite[rung];
  bitmap = m->bitmap_idx; bitmap2 = kNoBitmap;
  pivot_x = m->pivot_x;   pivot_y = m->pivot_y;
} else {
  const sprite_cloud2_meta_t *m = &kSpriteDefCloud2Sprite[rung - kRungStacked];
  bitmap = m->top_bitmap_idx; bitmap2 = m->bot_bitmap_idx;
  pivot_x = m->pivot_x;       pivot_y = m->pivot_y;
}
sprites_stack_add(depth, x, y, pivot_x, pivot_y, bitmap, bitmap2, colour, flags);
```

The `bitmap` load through the first table was compiled correctly — oscar64
folded each branch's field offset into the pointer so the join could do one
`LDA (ptr),0`, which is a nice optimisation. The other three fields in the
`else` arm became:

```asm
LDA #$ff        ; the *other* branch's constant
STA T18         ; -> pivot_x
STA T17         ; -> pivot_y
```

and `bitmap2` was never stored at all, so the ninth argument reached the callee
holding whatever the previous call had left in `P9`. On screen every large
cloud drew its upper sprite and a blank lower one.

Two things about this are worth noting for anyone triaging it. The wrong values
are plausible — `0xFF` is a real sentinel elsewhere in the same call — so the
result looked like a logic error rather than a codegen one. And the host build
of the same source (compiled with `g++` for unit tests) is correct and always
was, so no amount of testing off-target could have found it.

## Second, unrelated finding: a `volatile` global is constant-folded

Noticed while reducing the case above. `vol.c`:

```c
#include <stdint.h>
volatile uint8_t which = 7;
volatile uint8_t out;
int main(void) { out = which + 1; return 0; }
```

compiles to

```asm
0886 : a9 08 __ LDA #$08      ; 7 + 1, folded
0888 : 85 f7 __ STA $f7 ; (out + 0)
```

`which` is never read, and the symbol is not emitted. Reading a `volatile`
object should always produce a load. Filed here only because it was found in
the same session; happy to split it into its own issue.

## Attached

| File | |
| :--- | :--- |
| `oscar64_bug_min.c` | smallest reproduction; verify by reading the `.asm` |
| `oscar64_bug_selfcheck.c` | runnable, prints expected vs actual and `PASS`/`FAIL` |
| `oscar64_bug_volatile.c` | the second finding above |
