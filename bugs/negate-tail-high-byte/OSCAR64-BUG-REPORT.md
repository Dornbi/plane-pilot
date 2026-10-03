<!-- STATUS: OPEN against b86277f. Not yet filed. -->
# A negated return value loses its high byte (regression in 1616138)

**Versions:** introduced by `1616138` ("Bitfield init and sign change
optimizations") and still present in current main `b86277f` (1.32.273). Its
parent `9408778` is correct, and so is `v1.32.272-117-ga7305f9`. Built from
source on macOS arm64.
**Affects:** `-O1` and above. `-O0` is correct.
**Severity:** silent wrong code

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

returns with the high byte of its result still in A, never stored to
`ACCU + 1`. Two paths do it:

- **Negating (`a < 0`):** the low byte of `-q` is right, and the high byte is
  whatever was left in `ACCU + 1`, here 0. -128 comes back as 128, and -1 as
  255.
- **`m == b`, positive `a`:** the constant 256 is built with its high byte in
  A. 256 comes back as 0.

`div8p8` has to stay out of line. Made `inline`, the same program passes.

## Reproduction

`oscar64_bug_negate_tail.c` (attached): `div8p8(a, 200)` for six values of `a`.

```
gcc -O2 -o chk oscar64_bug_negate_tail.c && ./chk     # PASS
oscar64 -O2 -e oscar64_bug_negate_tail.c              # FAIL 128 128 0 0 1 255
```

| build | result |
| --- | --- |
| expected (gcc, and oscar64 `-O0` on every version) | `128 -128 256 -256 1 -1` |
| `a7305f9`, `-O1` and `-O2` | `128 -128 256 -256 1 -1` |
| `9408778`, `-O1` and `-O2` | `128 -128 256 -256 1 -1` |
| `1616138`, `-O1` and `-O2` | `128 128 0 0 1 255` |
| `b86277f`, `-O1` and `-O2` | `128 128 0 0 1 255` |

## Generated code

The tail of `div8p8` on `b86277f -O2`:

```
0949 : 24 10 __ BIT P3 ; (a + 1)
094b : 10 14 __ BPL $0961 ; (div8p8.s11 + 0)
.s12:
094d : a5 1b __ LDA ACCU + 0
094f : 85 43 __ STA T0 + 0
0951 : a5 1c __ LDA ACCU + 1
.s8:
0953 : 85 44 __ STA T0 + 1
0955 : 38 __ __ SEC
0956 : a9 00 __ LDA #$00
0958 : e5 43 __ SBC T0 + 0
095a : 85 1b __ STA ACCU + 0          low byte of -q stored
095c : a9 00 __ LDA #$00
095e : e5 44 __ SBC T0 + 1            high byte of -q left in A ...
.s3:
0960 : 60 __ __ RTS                   ... and never stored to ACCU + 1
.s11:
0961 : 60 __ __ RTS
.s7:                                  m == b
0962 : 85 43 __ STA T0 + 0
0964 : 85 1b __ STA ACCU + 0
0966 : a9 01 __ LDA #$01              high byte of 256 in A
0968 : 24 10 __ BIT P3 ; (a + 1)
096a : 30 e7 __ BMI $0953 ; (div8p8.s8 + 0)
096c : 60 __ __ RTS                   ACCU + 1 never written
```

On `9408778` both paths end with `STA ACCU + 1`:

```
096d : e5 44 __ SBC T0 + 1
096f : 85 1c __ STA ACCU + 1
.s3:
0971 : 60 __ __ RTS
.s10:
0972 : 85 43 __ STA T0 + 0
0974 : 85 1b __ STA ACCU + 0
0976 : a9 01 __ LDA #$01
0978 : e6 44 __ INC T0 + 1
097a : 85 1c __ STA ACCU + 1
```

## Older compilers fail at `-O1` with other spellings

The same lost high byte shows up before `1616138` too, at `-O1` only, when the
last line is spelled differently:

```c
if (a < 0) {
  return (int16_t)-q;
}
return q;
```

or `if (a < 0) { q = (int16_t)-q; } return q;`. Both print
`FAIL 128 128 0 0 1 255` at `-O1` on `9408778` and on `a7305f9`, and pass at
`-O0`, `-O2`, `-O3` and `-Os` there. So the underlying defect seems older, and
`1616138` exposed it at `-O2` and for the `?:` form.

## Workarounds

Each of these passes at `-O1`, `-O2` and `-O2 -Op -Oa -Oi -Oz -Oo` on
`a7305f9`, `1616138` and `b86277f`:

```c
// Negate as ~q + 1.
return a < 0 ? (int16_t)(~q + 1) : q;

// Branchless sign flip.
int16_t s = a >> 15;
return (int16_t)((q ^ s) - s);

// Call frac8 unconditionally, then override the 256 case.
int16_t q = (int16_t)frac8(m, b);
if (m == b) {
  q = 256;
}
return a < 0 ? (int16_t)-q : q;
```

Writing the sign into a `bool` first, computing `q` as `uint16_t`, or negating
as `0 - q` or `0u - (uint16_t)q` all still fail.
