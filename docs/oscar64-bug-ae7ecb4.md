# oscar64 regression ae7ecb4 — wrong ZP byte loaded into Y for a 16-bit compare cascade

Bad commit: `ae7ecb4c8cd2c7f0c362ce27f4228a155d00cfb7` — "Optimize switch branch cascade", 2026-07-15.
Still present at `933f203` (2026-08-04). Parent `c020aff` is good.

## Symptom in ppilot

`vec_normalize()` (vec.cc:330) returns wrong-length vectors. Since `vec_orthonormalize()`
runs on the attitude matrix every frame, the matrix stops being orthonormal and the
flight model diverges — the "erratic behaviour".

Only two functions change size (`_limit_vec` -1, `vec_normalize` +2), so the 142-byte
shrink elsewhere is unrelated; this is the only wrong one found so far.

## Repro

`oscar64-normalize-repro.cc` (self-contained, no ppilot headers):

```
oscar64 -O2 -e oscar64-normalize-repro.cc
```

Expected (parent commit): unit vectors normalize to ±256.

```
0 0 17  -> 0 0 272
0 0 100 -> 0 0 261
0 0 256 -> 0 0 256
```

Actual (ae7ecb4 and later):

```
0 0 17  -> 0 0 136
0 0 100 -> 0 0 129
0 0 256 -> 0 0 4096
```

Reproduces at `-O1`, `-O2` and `-O3`; no `-Op/-Oa/-Oi/-Oz/-Oo` needed.

## Cause

The integer sqrt starts with

```c
L2 <<= 6;
uint16_t bit = (uint16_t)1 << 14;
while (bit > L2) bit >>= 2;
```

`bit > L2` is a 16-bit compare cascade: compare high bytes, then low bytes. The
compiler keeps `L2` in Y (high) and X (low) across the loop.

**Good (`c020aff`)** — Y is loaded from the high byte *after* the shift stores the
new low byte:

```asm
; 338  L2 <<= 6
TXA
LSR
ROR T0 + 1
ROR
ROR T0 + 1
AND #$80
ROR
STA T0 + 0        ; new low byte
; 342  while (bit > L2)
LDY T0 + 1        ; Y = high byte   <-- correct
CPY #$40
BCS  exit
.s17:
TAX               ; X = low byte
.l6:
LSR T2 + 1
ROR T2 + 0
LSR T2 + 1
ROR T2 + 0
CPY T2 + 1        ; Y used as high byte
BCC .l6
BNE .l7
CPX T2 + 0
BCC .l6
```

**Bad (`ae7ecb4`)** — the load is hoisted above the store *and* reads offset `+0`
instead of `+1`, so Y holds the **stale low byte** while the loop still uses it as
the high byte:

```asm
; 338  L2 <<= 6
LDA T0 + 1
LSR
ROR T0 + 1
ROR
ROR T0 + 1
AND #$80
ROR
LDY T0 + 0        ; Y = OLD low byte  <-- BUG, should be LDY T0 + 1 after the STA
STA T0 + 0
; 340
LDA #$40
STA T2 + 1
; 342  while (bit > L2)
LDA T0 + 1        ; this one reads the right byte
CMP #$40
BCC .s17
JMP exit
.s17:
LDX T0 + 0        ; X = new low byte, correct
.l6:
LSR T2 + 1
ROR T2 + 0
LSR T2 + 1
ROR T2 + 0
CPY T2 + 1        ; compares against the stale low byte
BCC .l6
BNE .l7
CPX T2 + 0
BCC .l6
```

The entry test (`LDA T0+1 / CMP #$40`) was rewritten correctly; only the
loop-carried copy in Y got the wrong zero-page offset. A wrong starting `bit`
makes the sqrt return a wrong `res`, hence the wrong scale factor.

## Workaround until upstream fixes it

Build with `c020aff68f6637a86db62ce28e72548db04c29a8` (2026-07-12), the last good
commit, or rewrite the loop so the 16-bit compare isn't a cascade, e.g.

```c
uint8_t sh = 14;
while (((uint16_t)1 << sh) > L2) sh -= 2;
uint16_t bit = (uint16_t)1 << sh;
```

(untested — verify against the repro).
