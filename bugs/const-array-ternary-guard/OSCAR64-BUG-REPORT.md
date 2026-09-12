# A `?:` guarding a biased index into a `const` array loses its guard

**STATUS: OPEN** against `v1.32.272-117-ga7305f9`, which is the build that fixed
the two reports beside this one.

**Version:** oscar64 `v1.32.272-117-ga7305f9` (built from source, macOS arm64)
**Severity:** silent wrong code — no diagnostic, and the bad read is in bounds
of the program, so it returns data rather than crashing
**Affects:** `-O0` and `-O2 -Op -Oa -Oi -Oz -Oo`, i.e. both ends of the range

## Summary

In

```c
put(dst, row < K ? 0 : tb[row - K]);
```

where `tb` is a `const uint8_t[]` and `row` is a loop variable the compiler
knows is `0..N`, the condition is discarded and the whole expression compiles to
the false arm's load alone:

```
LDA tb-K,x        ; x = row, unguarded
```

For `row < K` that reads the `K` bytes *in front of* the array — in both builds
below, machine code — and passes them on as if they were table entries. The
true arm's `0` never appears anywhere in the output.

This is the third face of the same corner as the two fixed reports beside it:
a `const` array read at an index carrying a constant offset. Those two folded
the *taken* arm away to a constant. This one keeps the *untaken* arm and throws
the condition out.

## Reproduction

`oscar64_bug_ternary_guard.c` (attached). It builds and runs on the host as
well, where it prints `PASS` — which is what says the C is right.

```
gcc -O2 -o chk oscar64_bug_ternary_guard.c && ./chk        # PASS
oscar64 -ii=<oscar64>/include -g -O0 -o=b.prg oscar64_bug_ternary_guard.c
oscar64 -ii=<oscar64>/include -g -O2 -Op -Oa -Oi -Oz -Oo -o=b.prg oscar64_bug_ternary_guard.c
```

Both `.prg`s print `FAIL` in VICE, naming the rows that came out non-zero.

The generated `fill()` in each, against the address of `tb` from the `.lbl`:

| build | `tb` | the loop's load | offset |
| --- | --- | --- | ---: |
| `-O0` | `$1e5c` | `LDA $1e4f,x` | −13 |
| `-O2 …` | `$18f6` | `LDA $18e9,x` | −13 |

`K` is 13, so both are `tb[row - 13]` with no comparison anywhere in the loop.
Dumping the 21 bytes the `-O2` build reads shows what reaches `put()`:

```
$18e9: 9f c6 45 60 29 e0 09 01 8d b1 9f 60 00 | 02 04 06 06 08 0a 0a 0c
       \______________ code bytes ___________/   \________ tb ________/
```

## What hides it

The store has to be one the compiler cannot precompute. Writing

```c
out[row] = row < K ? 0 : tb[row - K];
```

instead makes oscar64 evaluate the whole loop at compile time into a correct
21-byte table and copy that, so the bug does not appear — which is why the
reduction goes through a function that builds three bytes of bitmap, as the real
code does.

## Where it was found

plane-pilot `c64o/sprites.cc`, drawing the tail fin's sprite bitmap. The rows
above the fin's taper are meant to be blank and came out as full-width bars,
which on screen is a grey slab across the top of the tail. Nothing else in the
program was affected and no other symptom pointed at it.

## Workaround

Index every `const` array from zero. In the real code that meant two passes over
the rows — clear all of them, then paint the taper over the ones that have it —
which removes the bias rather than the branch:

```c
for (uint8_t row = 0; row < kSpriteHeightPixels; ++row) {
  _sprites_draw_fin_row(tip, 0);
  ...
}
uint8_t *ink = kFinSpriteData + kSpriteFinTipFirstRow * 3;
for (uint8_t i = 0; i < kSpriteFinTipRows; ++i) {
  _sprites_draw_fin_row(ink, kSpriteFinTipWidths[i]);
  ink += 3;
}
```
