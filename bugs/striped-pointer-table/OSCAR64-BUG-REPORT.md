<!-- STATUS: OPEN against v1.32.272-117-ga7305f9 and b86277f. Not yet filed. -->
# A `__striped` table initialised with `&object` is emitted unstriped

**Versions:** `v1.32.272-117-ga7305f9` and current main `b86277f` (1.32.273),
built from source on macOS arm64
**Affects:** every optimisation level, `-O0` included. The wrong bytes are in
the emitted data, not in the code that reads it.
**Severity:** silent wrong data

## Summary

In a `__striped` array of pointers, an entry initialised with an address-of
expression, `&object` or `&array[n]`, is written as an ordinary little-endian
word at the entry's low-byte slot. It should have its low byte at slot `i` and
its high byte at slot `i + N`. The high byte lands in the low-byte slot of
entry `i + 1`, where that entry's own low byte then overwrites it. The
high-byte half of the table is left as zeroes, apart from whatever the last
entries' high bytes spill into.

The code reads the table as striped, which is correct. So every pointer except
the first comes back as `$00xx`, and the first gets the high byte of the last.

The same table initialised with `array` or `array + n` is emitted correctly.
So is a table of integer constants.

## Reproduction

`oscar64_bug_striped_ptr.c` (attached). It has three striped tables of four
pointers: `&object`, `&object` with null entries between, and `buf + n`. Each
entry is read back through a `volatile` index and compared with the address it
was initialised from.

```
gcc -O2 -o chk oscar64_bug_striped_ptr.c && ./chk     # PASS
oscar64 -O2 -e oscar64_bug_striped_ptr.c              # FAIL
```

On `b86277f -O2`:

```
tab[0]: 19fe not 13fe
gaps[0]: 00fe not 13fe
tab[1]: 00d3 not 19d3
gaps[1]: 0013 not 0000
tab[2]: 00d5 not 19d5
gaps[2]: 00d5 not 19d5
tab[3]: 00d7 not 19d7
gaps[3]: 0019 not 0000
FAIL
```

The emitted tables, with `b0` at `$13fe`, `b1..b3` at `$19d3..$19d7`, and `buf`
at `$1a18`:

```
tab:  fe d3 d5 d7 19 00 00 00    should be fe d3 d5 d7 13 19 19 19
gaps: fe 13 d5 19 00 00 00 00    should be fe 00 d5 00 13 00 19 00
rows: 18 28 38 48 1a 1a 1a 1a    correct
```

In `tab`, each entry's word overwrote the previous entry's high byte, and the
last entry's high byte spilled into slot 4. In `gaps`, the null entries left
the words intact, so the table reads as if it were not striped.

`-O0` fails the same way on both versions.

## Workaround

Do not stripe a table of `&object` initializers.
