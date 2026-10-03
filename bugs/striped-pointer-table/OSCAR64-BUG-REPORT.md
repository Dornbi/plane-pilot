# A `__striped` table initialised with `&object` loses its high bytes

**STATUS: OPEN** against 1.32.272 (the build in use) and 1.32.273
(oscar64-main `b86277f`).

**Version:** oscar64 1.32.272 and 1.32.273 (built from source, macOS arm64)
**Severity:** silent wrong data
**Affects:** every optimisation level, `-O0` included. The wrong bytes are in
the emitted data, not in the code that reads it.

## Summary

In a `__striped` array of pointers, an entry initialised with an address-of
expression - `&object` or `&array[n]` - is written as an ordinary little-endian
word at the entry's low-byte slot, instead of having its low byte at slot `i`
and its high byte at slot `i + N`. The high byte then sits in the low-byte slot
of entry `i + 1`, and that entry's own low byte overwrites it. The striped
high-byte half is left as zeroes, except for whatever the last entries' high
bytes spill into.

The code reads the table as striped, which is correct. So every pointer except
the first comes back as `$00xx`.

The same table initialised with `array` or `array + n` is emitted correctly.
So is a table of integer constants.

| initializer | emitted (N = 2) | correct |
| --- | --- | --- |
| `{&b0, &b1}` (const struct) | `d4 d6 08 00` | `d4 d6 08 08` |
| `{&m0, &m1}` (non-const struct) | `e5 e7 08 00` | `e5 e7 08 08` |
| `{&buf[0], &buf[8]}` | `e9 f1 08 00` | `e9 f1 08 08` |
| `{buf, buf}` | `e9 e9 08 08` | same, correct |
| `{buf + 0, buf + 16, ...}` | correct | |

Found in `c64o/boxdefs.cc`. Commit 5ce3cb5 striped its two tables,
`main_boxes[60]` and `alt_boxes[60]`, both `{&box_..._def, ...}`, and the
horizon boxes turned into garbage across the whole viewport.
- `main_boxes` linked with every high byte 0.
- `alt_boxes` has a `NULL` between most entries, so nothing overwrote the
  misplaced words and it read as an unstriped table.

Nothing at build time noticed. The program linked, every link check in
`c64o/Makefile` passed, and only a screenshot showed it.

## Reproduction

`oscar64_bug_striped_ptr.c` (attached). It has three striped tables of four
entries: `&object`, `&object` with zero gaps, and `buf + n`. Each entry is read
back through a `volatile` index into `out[]`.

```
oscar64 -ii=<oscar64>/include -g -O2 -Op -Oa -Oi -Oz -Oo -o=s.prg oscar64_bug_striped_ptr.c
tools/vice_dump.sh s.prg @spin out 24
```

With `b0..b3` at `$08dc..$08e2` and `buf` at `$0918`:

| table | read back | expected |
| --- | --- | --- |
| `tab = {&b0, &b1, &b2, &b3}` | `08dc 00de 00e0 00e2` | `08dc 08de 08e0 08e2` |
| `gaps = {&b0, 0, &b2, 0}` | `00dc 0008 00e0 0008` | `08dc 0000 08e0 0000` |
| `rows = {buf + 0, ..., buf + 48}` | `0918 0928 0938 0948` | same |

The emitted bytes show the mechanism directly:

```
tab:  dc de e0 e2 08 00 00 00     (should be dc de e0 e2 08 08 08 08)
gaps: dc 08 e0 08 00 00 00 00     (should be dc 00 e0 00 08 00 08 00)
rows: 18 28 38 48 09 09 09 09     (correct)
```

`-O0` emits the same `tab` and `gaps`.

## Workaround

Do not stripe a table of `&object` initializers. `lib/find_boxes.py` now
writes `main_boxes` and `alt_boxes` without `__striped`, with a comment
pointing here.
