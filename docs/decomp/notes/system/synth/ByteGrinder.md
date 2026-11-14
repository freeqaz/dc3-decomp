# Lane notes: `src/system/synth/ByteGrinder.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?op6@@YA?AVDataNode@@PAVDataArray@@@Z`

`op6` -- moved verbatim from `src/system/synth/ByteGrinder.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 210-222):

```text
// op6 stop note (w25-ga2, c2rs 8.7 commutative key): the image emits
// `xor r11, r10, r11`, u8(operand) first. Ours is the other way round. At the
// last sort (cqlo3) the !w side is the eq-0 compare with the Int(2) call
// folded in (SU+7, key 0x100e040f) and u8(operand) is zext of the saved
// Int(1) result (0x00030098). The u8(operand) side can never outrank a tree
// that holds a call, so the image's compare must see a LEAF (key 0x1xxxx), as
// op0's zext does once `w` is narrowed at its declaration. No 32-bit-exact
// spelling found that keeps the call out of the compare. Tried: named
// bool/u8/u32 results of !w and of `== 0`, a const ref to the result, int vs
// u32 vs unsigned long for both locals, `w == 0` and `w < 1`, explicit
// DataNode(kDataInt, ...), and inline-call orders (which swap the two Int
// calls). Narrowing w itself changes the semantics: !w tests all 32 bits
// (cntlzw on r3).
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.615, 1 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.

## `?op59@@YA?AVDataNode@@PAVDataArray@@@Z`

`op59` -- moved verbatim from `src/system/synth/ByteGrinder.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 696-701):

```text
    // BEHAVIOUR FIX (w11-d): the image computes ((w >> 2) ^ 0x0F) |
    // (((w & 3) << 6) ^ 0x19) -- `extrwi r9,r11,8,22; xori r9,r9,0xf` and
    // `clrlslwi r11,r11,30,6; xori r11,r11,0x19` at 0x8276C938.  The previous
    // spelling had the two xor constants swapped between the halves
    // ((w ^ 0x65) | ((w << 8) ^ 0x3C)) >> 2, which differs on 224 of 256 byte
    // values; this form also agrees with the native nop59 for all 256.
```


## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/synth/ByteGrinder.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

File-level (was lines 1154-1168; no definition follows it):

```text
// w8-j 2026-09-15 -- FLOOR at 99.933% for ?op59@@YA?AVDataNode@@PAVDataArray@@@Z
// (120 B, 28 of 30 instructions equal).  The two residual rows are one swapped
// pair:
//     target   xori r9, r9, 0xf    /  xori r11, r11, 0x19
//     ours     xori r9, r9, 0x19   /  xori r11, r11, 0xf
// MSVC distributed the `>> 2` over the OR, so 0x3C>>2 = 0xf is the
// `(w << 8) ^ 0x3C` half and 0x65>>2 = 0x19 is the `w ^ 0x65` half.  The image
// evaluates the shifted half into r9 and the plain half into r11; we do the
// reverse.  Both operand-order levers are REFUTED, one full ninja each:
//   (a) `(working3 | working2) >> 2` instead of `(working2 | working3) >> 2`
//       -- byte identical, MSVC canonicalises the commutative OR.
//   (b) swapping the DECLARATION order of working2 and working3 as well
//       -- byte identical again.
// The value is correct either way (the final u8 truncation makes the halves
// interchangeable); only the register assignment differs.  Do not re-derive.
```
