# A negated return value loses its high byte (regression in 1616138)

**STATUS: OPEN** against 1.32.273 (`b86277f`, current upstream HEAD).
Introduced by `1616138` "Bitfield init and sign change optimizations". Its
parent `9408778` is correct, and so is `v1.32.272-117-ga7305f9`.

**Version:** oscar64 `1616138` through `b86277f` (built from source, macOS arm64)
**Severity:** silent wrong code
**Affects:** `-O2` and up. `-O0` is correct.

## Summary

A function that returns either a value or its negation, where the value comes
back from another call,

```c
static __noinline int16_t div8p8(int16_t a, int16_t b) {
  int16_t m = a < 0 ? (int16_t)-a : a;
  int16_t q = m == b ? 256 : (int16_t)frac8(m, b);
  return a < 0 ? (int16_t)-q : q;
}
```

returns with the high byte of the result still in A, never stored to
`ACCU + 1`. Two paths do it:

- **Negating (`a < 0`):** the low byte of `-q` is right and the high byte is
  whatever `q`'s was. -128 comes back as 128, and -1 as 255.
- **`m == b`, positive `a`:** the constant 256 is built with its high byte in A.
  256 comes back as 0.

Found when `c64o/test/target_test.cc` went from 0 to 1,658 failures of 13,079
on 1.32.273. `planes.cc`'s `_div8p8` has exactly this shape: the aircraft's
screen centre came out 256 pixels off. The game's map screen also showed
viewport debris on the same compiler. I did not trace that one, and it may be
another instance of this bug.

## Reproduction

`oscar64_bug_negate_tail.c` (attached): `div8p8(a, 200)` for six values of `a`,
into `out[]`.

```
oscar64 -ii=<oscar64>/include -g -O2 -o=n.prg oscar64_bug_negate_tail.c
tools/vice_dump.sh n.prg @spin out 12
```

| build | `out[]` |
| --- | --- |
| expected (gcc on the host, and oscar64 `-O0`) | `128 -128 256 -256 1 -1` |
| `9408778 -O2` (parent) | `128 -128 256 -256 1 -1` |
| `1616138 -O2` | `128 128 0 0 1 255` |
| `b86277f -O2` (1.32.273) | `128 128 0 0 1 255` |

`div8p8` has to stay out of line (`__noinline` here; in planes.cc it has three
callers). Inlined into `main` it is correct.

The tail of `div8p8` on `b86277f -O2`:

```
.s8:
08cd STA T0 + 1
08cf SEC
08d0 LDA #$00
08d2 SBC T0 + 0
08d4 STA ACCU + 0       ; low byte of -q stored
08d6 LDA #$00
08d8 SBC T0 + 1         ; high byte of -q left in A ...
.s3:
08da RTS                ; ... and never stored to ACCU + 1
.s11:
08db RTS
.s7:                    ; m == b
08dc STA T0 + 0
08de STA ACCU + 0
08e0 LDA #$01           ; high byte of 256 in A
08e2 BIT P3 ; (a + 1)
08e4 BMI $08cd
08e6 RTS                ; ACCU + 1 never written
```

On `9408778` both paths end with `STA ACCU + 1`.

## Workaround

None in the source. This project stays on 1.32.272 (`a7305f9`) until it is
fixed. `make test` catches it through target_test.
