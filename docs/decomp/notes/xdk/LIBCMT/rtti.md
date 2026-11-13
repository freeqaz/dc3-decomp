# Lane notes: `src/xdk/LIBCMT/rtti.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `__RTtypeid()`

`__RTtypeid` -- moved verbatim from `src/xdk/LIBCMT/rtti.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 40b6979b6)

Attribution: no function named `__RTtypeid` in report.json (unit); filed under the source name

Above the definition (was lines 63-86):

```text
// RESIDUAL w7-ax, 80.9 canonical, 188 B -- but the BODY is instruction-for-
// instruction identical to the image (47 of 47 real instructions equal).  The
// only 9 rows objdiff charges are the __except filter, which the image carries
// as its own 0x24-byte symbol fn_8299DD7C at 0x8299DD7C (immediately after
// __RTtypeid's 0xBC extent) and MSVC emits inside our COMDAT.  Same shape as
// the fn_8299E398 row recorded on __RTDynamicCast below; it is a symbols.txt
// extent question, not a source one.
//
// The lever that closed the body (78.53 -> 80.9): the image's single dead home
// store `stw r3, 0x50(r31)` at 0x8299DCF8, plus the 8-byte frame shift it
// implies (0x80 frame / exception object at r31+0x58, against our 0x70 / 0x50),
// is MSVC homing a CSE temp -- `pCompleteLocator->pTypeDescriptor` is read
// TWICE in the image's source (once for the null test, once for the return),
// not read once into a named local.  Naming the local costs the store and the
// slot.  MEASURED NEGATIVES on the same function: routing the return through a
// `void *pResult` assigned inside the __try does create the slot but un-sinks
// the NULL-inptr throw block to the top (78.53 -> 41.4), in both the
// `if (!p) throw;` and `if (p) ... else throw;` spellings; and moving the
// declarations into the __try scope is byte-identical, reproducing w7-at's
// result for __RTDynamicCast.
//
// The `bl ??0bad_cast@std@@QAA@PBD@Z` at 0x8299DD54 is NOT a wrong callee: the
// throw info beside it is _TI2?AVbad_typeid@std@@, and the two ctors are an ICF
// fold (icf_aliases.map), so name_check charges no row for it.
```


## `?FindMITargetTypeInstance@@YAPBU_s_RTTIBaseClassDescriptor@@PAXPBU_s_RTTICompleteObjectLocator@@PAUTypeDescriptor@@H2@Z`

`FindMITargetTypeInstance` -- moved verbatim from `src/xdk/LIBCMT/rtti.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 40b6979b6)

Above the definition (was lines 173-196):

```text
// w14-e: 93.77 -> 95.2 canonical by testing the source sub-object's offset
// through the CRT's PMDtoOffset helper (as FindVITargetTypeInstance and
// __RTDynamicCast do) instead of open-coding the vbase read: that alone
// removed the volatile r8/r9 naming swap through the scan loop and the
// `clrrwi` the open-coded adjustment produced.  Still open: `add r11, r10,
// r11` (image) vs `add r11, r11, r10` for `RetOff += mdisp`, plus the two
// placement rows below.  Tail-duplicating the pBase NOTVISIBLE check into
// both arms (so the else arm could merge back into the first copy, the way
// 0x8299DF50's `beq` jumps back up) measured 85.7 -- reverted.
// RESIDUAL w7-at, 93.77 canonical, 440 B.  The arithmetic and the control
// flow are already the image's; what is left is block placement plus a
// volatile r8/r9 naming swap through the whole scan loop.  Two placement
// rows: (a) the image falls out of the `i < numBaseClasses` loop into a
// SHARED `return 0` epilogue at 0x8299DF50, where we tail-duplicate
// `li r3, 0x0` + `b __restgprlr_25` inline; (b) at 0x8299DF04 it tests
// `iTarget != 0` with `bne` straight to the return and falls through to
// the pBase NOTVISIBLE check, where we emit `beq` to the check plus an
// unconditional `b`.  Hoisting the BCD_HASPCHD arm to a positive early
// return (so the `iTarget != 0` test becomes the fallthrough) was measured
// and is WORSE: 93.77 -> 88.3.
// w20-r branch-landing scan (row 108, image 0x8299DF4C `beq` -> 0x8299DF0C):
// ARTIFACT, shared vs duplicated tail. Both sides return 0 when pTargetBase
// has NOTVISIBLE|AMBIGUOUS and otherwise test pBase NOTVISIBLE; the image
// jumps back to the one copy of that test, we fall into our own copy.
```


## `FindCompleteObject()`

`FindCompleteObject` -- moved verbatim from `src/xdk/LIBCMT/rtti.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 40b6979b6)

Attribution: no function named `FindCompleteObject` in report.json (unit); filed under the source name

Above the definition (was lines 343-350):

```text
// w7-bu: __RTDynamicCast 87.7 -> 90.0 canonical by spelling the __try body
// the way the CRT does -- two static helpers, both inlined.  The locator is
// read here AND again by the caller; MSVC CSEs the two loads into one r11
// and homes the caller's copy (`stw r11, 0x58(r31)`, 0x8299E274) between
// `subf r30, r8, r3` and the cdOffset test.  A single named locator in the
// caller (the old shape) never produced that store.  Marking the helpers
// `static inline` is byte-inert and still emits the out-of-line copies
// (sections 34/35 of our obj), so the plain CRT spelling is kept.
```


## `__RTDynamicCast()`

`__RTDynamicCast` -- moved verbatim from `src/xdk/LIBCMT/rtti.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 40b6979b6)

Attribution: no function named `__RTDynamicCast` in report.json (unit); filed under the source name

Inside the body (was lines 396-403):

```text
            // RESIDUAL w7-bu, 90.0 canonical: all 90 body rows match.  The 9
            // rows left are the __except filter (lis/ori 0xC0000005, two lwz,
            // subf, cntlzw, extrwi, nop, blr), which the image carries as its
            // own symbol fn_8299E398 (0x8299E230 + 0x168, size 0x24) and MSVC
            // emits inside our COMDAT right after the body -- a symbols.txt
            // extent question, not a source one.  Inert here: const vs
            // non-const locator, pCompleteObject/myoffset in or out of the
            // __try scope, `inptr -= VfDelta` vs a pvfptr local.
```
