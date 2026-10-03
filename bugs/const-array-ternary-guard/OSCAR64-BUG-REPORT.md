<!-- STATUS: OPEN against v1.32.272-117-ga7305f9 and b86277f. Not yet filed. -->
# A `?:` guarding a biased index into a `const` array loses its guard

**Versions:** `v1.32.272-117-ga7305f9` and current main `b86277f` (1.32.273),
built from source on macOS arm64
**Affects:** every optimisation level, `-O0` included
**Severity:** silent wrong code. No diagnostic, and the bad read is inside the
program, so it returns data rather than crashing.

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

For `row < K` that reads the `K` bytes in front of the array, machine code in
both builds below, and passes them on as if they were table entries. The true
arm's `0` never appears anywhere in the output.

## Reproduction

`oscar64_bug_ternary_guard.c` (attached). It fills a 21-row sprite bitmap in
which the first 13 rows are blank and the rest take their widths from `tb`.

```
gcc -O2 -o chk oscar64_bug_ternary_guard.c && ./chk     # PASS
oscar64 -O0 -e oscar64_bug_ternary_guard.c              # FAIL row 0: 24 not 0 ...
oscar64 -O2 -e oscar64_bug_ternary_guard.c              # FAIL row 11: 10 not 0 ...
```

(The outputs shown are from `b86277f`.)

Which rows fail depends on the bytes in front of `tb`, so it varies with the
build. Every build I tried fails: `-O0`, `-O1`, `-O2` and
`-O2 -Op -Oa -Oi -Oz -Oo`, on both versions above.

The generated `fill()` on `b86277f`, against the address of `tb`:

| build | `tb` | the loop's load | offset |
| --- | --- | --- | ---: |
| `-O0` | `$1e33` | `LDA $1e26,x` | −13 |
| `-O2 -Op -Oa -Oi -Oz -Oo` | `$18d6` | `LDA $18c9,x` | −13 |

`K` is 13, so both are `tb[row - 13]` with no comparison against 13 anywhere in
the loop. The only compare in `fill()` is the loop bound. The 21 bytes the
`-O2` build reads:

```
$18c9: 91 0f e6 1b 60 a4 52 91 0f e6 52 60 00 | 02 04 06 06 08 0a 0a 0c
       \______________ code bytes ___________/   \________ tb ________/
```

## What hides it

The store has to be one the compiler cannot precompute. Writing

```c
out[row] = row < K ? 0 : tb[row - K];
```

instead makes oscar64 evaluate the whole loop at compile time into a correct
21-byte table and copy that, so the bug does not appear. That is why the
reproduction goes through a function that builds three bytes of bitmap per
row.

## Workaround

Index every `const` array from zero. Here that means two passes over the rows:
clear all of them, then draw the inked ones from `tb[0]` on.

```c
for (uint8_t row = 0; row < ROWS; ++row) {
  put(out + row * 3, 0);
}
for (uint8_t i = 0; i < ROWS - K; ++i) {
  put(out + (K + i) * 3, tb[i]);
}
```
