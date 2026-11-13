# Lane notes: `src/system/utl/CacheMgr_Xbox.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?PollSearch@CacheMgrXbox@@AAAXXZ`

`CacheMgrXbox::PollSearch` -- moved verbatim from `src/system/utl/CacheMgr_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 379-389):

```text
// w17-d (99.911): `unsigned int res;` declared first then assigned is inert;
// numFound hoisted to function scope 97.4; DWORD res + `(unsigned int)res` in the
// FAIL makes the 0x50 store a call-site temp (98.7) -- the image's res is a real
// home stored straight after XGetOverlappedResult.
// w24-slot (99.911, unchanged): listing counts are numFound 3 (stw, addi,
// lwz) against at most 2 for every member of the pooled slot (res, the three
// assert-line temps, the new temp, enumRes).  So the slot rule (count desc,
// then last reference asc) predicts OUR order, numFound first.  The image's
// order would need the pooled slot's first member to outrank numFound.
// Adding an IL-only reference to res (`else if (res == 0 && numFound != 0)`)
// is byte-inert, probably folded before slot assignment.
```

Inside the body (was lines 391-404):

```text
    // w8-l: 99.911390 normalized, the only function keeping this unit from
    // 100% (23/24).  Known residual, 14 rows, all one slot apart and all the
    // SAME decision: the image puts `res` -- and every MakeString temp that
    // shares the temp pool with it -- at 0x50(r31), and the address-taken
    // `numFound` at 0x54.  We allocate them the other way round.  Refuted:
    // swapping the two declarations is byte-inert, so is scoping them into
    // this block, and so is spelling `res` as `unsigned int` (below).
    // Nothing else in the frame moves, so it is a single one-slot tie.
    //
    // FIXED here though: `res` and `enumRes` are `unsigned int`, not DWORD.
    // The image calls MakeString<unsigned int> (??$MakeString@I@@) at the two
    // %u sites; with DWORD (unsigned long) we emitted MakeString<unsigned
    // long> (??$MakeString@K@@).  The canonical ruler does not charge a
    // relocation name, but name_check did: 16 rows before, 14 after.
```
