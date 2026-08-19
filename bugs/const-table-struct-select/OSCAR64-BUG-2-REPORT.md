# oscar64: two differently typed const struct tables selected by an `if` still miscompile

**STATUS: FIXED** in `v1.32.272-117-ga7305f9` — commit 31d4595, *"Fix const table
value range calculation with negative base offsets"*. Verified three ways: the
reduced case below prints `PASS` at every optimisation level; the standalone
reduction with the project's real struct layouts is correct at runtime in VICE;
and the real `clouds.cc` written the branch way now renders pixel-identically to
the flat-table version in the emulator, where the pre-fix compiler rendered the
half clouds. Kept here as the record of the second report.

Follow-up to `../const-array-biased-index/OSCAR64-BUG-REPORT.md`, which was fixed one commit family earlier
in `v1.32.272-113-g5638ec5`.

## Symptom

When the two arms of an `if` read from **two `const` struct arrays of different
types**, and the index comes from a bounded search loop, only the *first* field
survives. The remaining fields are folded to the constant that the *other* arm
assigns.

Reduced case, `oscar64_bug_struct_select.c`:

```c
typedef struct { uint8_t a; int8_t px; int8_t py; } m1_t;             /* 3 bytes */
typedef struct { uint8_t a; uint8_t b; int8_t px; int8_t py; } m2_t;  /* 4 bytes */

__noinline void f(int16_t depth) {
  uint8_t rung = 0;
  while (rung < 9 && depth <= tbl[rung + 1]) { ++rung; }   /* rung = 7 here */

  uint8_t a, b; int8_t px, py;
  if (rung < 5) {
    const m1_t *m = &t1[rung];
    a = m->a;  b = 0xff;   px = m->px;  py = m->py;
  } else {
    const m2_t *m = &t2[rung - 5];
    a = m->a;  b = m->b;   px = m->px;  py = m->py;
  }
  sink(a, b, px, py);
}
```

`rung` is 7, so the taken arm is the `t2` one and `t2[2] = {22, 32, 15, 16}`.

| build | `v1.32.272-113` | `v1.32.272-117` |
|---|---|---|
| `-O0` | `22 32 15 16  PASS` | `22 32 15 16  PASS` |
| `-O1`, `-O2`, `-O3`, `-Os` | `22 255 255 255  FAIL` | `22 32 15 16  PASS` |

`b`, `px` and `py` all came back as `0xff` — the constant from the arm that was
*not* taken. Only `a`, the field at offset 0 in both structs, was right.

## In the generated code (pre-fix)

At `-O2` the two arms were merged into a single load through one pointer. The
`t2` arm computed `&t2[rung - 5]` correctly, then stored `#$ff` into the
temporaries holding `px` and `py`, and the caller's `b` argument was a plain
`LDA #$ff` on both paths:

```asm
.s7:                        ; the rung >= 5 arm
  ASL / ASL / CLC / ADC #$84 / STA T0+0     ; &t2[rung-5]   - correct
  LDA #$19 / ADC #$00 / STA T0+1
  LDA #$ff / STA T5+0 / STA T2+0            ; px, py  <-- the other arm's 0xff
.s10:                       ; the rung < 5 arm
  ... LDA t1[..].py / STA T2+0
  ... LDA t1[..].px / STA T5+0              ; correct here
.s8:
  LDY #$00 / LDA (T0),y / STA T4+0          ; only field 0 is loaded
  ...
  LDA #$ff / STA P9                         ; b, unconditionally
```

In the real program the same fold appeared one level out, in the
`sprites_stack_add@proxy` thunk oscar64 builds for arguments it believes are
constant at every call site:

```asm
sprites_stack_add@proxy:
  LDA #$0c / STA P6      ; pivot_x - genuinely 12 in every table entry, correct
  LDA #$ff / STA P9      ; bitmap2 - wrong, this varies per rung
```

After the fix the proxy folds only `P6`, and `P9` is stored per rung at the call
site.

## What did and did not trigger it

Each of these is the reduced case with exactly one thing changed:

| variant | result |
|---|---|
| as above | **FAIL** |
| both arms index arrays of the **same** struct type | ok |
| `rung` comes from a `volatile`, bounded by `if (rung >= 10) return;` instead of the search loop | ok |
| the call is inside a `for` loop rather than straight-line | FAIL (makes no difference) |

So it needed *both* the search loop that gives `rung` its range **and** the two
different struct types.

## Files

* `oscar64_bug_struct_select.c` — self-checking, prints `PASS` / `FAIL`.
  Build with `oscar64 -O2 oscar64_bug_struct_select.c` and run in VICE.
