# Lane notes: `src/xdk/nuispeech/mmio.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `mmioSetBuffer()`

`mmioSetBuffer` -- moved verbatim from `src/xdk/nuispeech/mmio.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 40b6979b6)

Attribution: no function named `mmioSetBuffer` in report.json (unit); filed under the source name

Inside the body (was lines 188-237):

```text
    // RESIDUAL (w7-am, 88.4 canonical; w7-br 89.0, 16 rows), three groups:
    //   * 82AA3800 `b .L_82AA3808` / 82AA3804 `li r4, 0x10`: the image puts the
    //     MMIO_EMPTYBUF re-arm in a block reached ONLY by the back edge, so the
    //     preheader has to jump over it.  Our do/while leaves the assignment in
    //     the body (executed, dead, on the last trip).  `for (;; fuFlush =
    //     MMIO_EMPTYBUF)` puts it on the back edge but rotates the whole loop
    //     the other way -- compute-and-test above the flush, exit on `bge` --
    //     and that spelling measured 79.4.
    //     NEGATIVE (w7-bq, 2026-09-15), two further spellings of that same
    //     back-edge placement, both 88.4 -> 79.4, byte-for-byte the SAME
    //     result as the `for (;; fuFlush = ...)` one above (100 instructions,
    //     19 diff_arg / 2 diff_op / 1 replace / 9 insert / 9 delete, two
    //     blt<->bge inversions, r28<->r29):
    //       (a) `for (;;) { ...; if (cchBuffer >= cchNext) break;
    //           fuFlush = MMIO_EMPTYBUF; }` -- the assignment placed AFTER the
    //           exit test, so it is on the back edge and nowhere else;
    //       (b) the image's CFG written out literally, `goto firstFlush;` over
    //           a `do { fuFlush = MMIO_EMPTYBUF; firstFlush: ... } while (...)`,
    //           which is exactly `b .L_82AA3808` / `.L_82AA3804: li r4, 0x10`.
    //     Three different sources, one output: MSVC rotates this loop before
    //     it lays out blocks, so the placement of the re-arm is not
    //     source-reachable.  The do/while below is the 88.4 form and stands.
    //     Cost of the rotation is only 3 of the 23 residual rows (idx 18/19
    //     delete `b`/`li r4,0x10`, idx 25 insert `li r4,0x10`); the other 20
    //     are the two groups below.
    //   * 82AA3838 `clrrwi r3, r11, 0` (the `bl LocalReAlloc` is 82AA3840):
    //     the image keeps pchOld in r11 across the loop test and truncates it
    //     into r3 at the call -- the 64-bit-tracked-pointer idiom (NewHandle
    //     at 82AA32F8 shows the same rldicl/clrrwi pair); we load straight
    //     into r3 (7 rows: idx 24/26-29 colouring + idx 33 delete).
    //     NEGATIVE (w7-br), all exactly inert at the same rows: re-reading
    //     `info->pchBuffer` at the call instead of pchOld; `(void *)(LONG)
    //     pchOld` at the call; dropping the pchOld local entirely and reading
    //     the field at all three uses.  The truncation is not a source cast.
    //   * 82AA3918: the LocalAlloc failure block is sunk PAST the epilogue
    //     (`beq .L_82AA3918` at 82AA38E0, and the success arm falls through
    //     into the common tail at 82AA38F4).  MSVC block sinking.
    //     LEVER (w7-br, 88.4 -> 89.0 canonical, 23 -> 16 rows): spelling the
    //     null test with the failure as the THEN arm (`if (pchNew == nullptr)
    //     { fail } else { success }`) drops the `b` over the failure block and
    //     the colouring it forced (7 rows).  What remains is the polarity
    //     itself (idx 75 `beq` vs our `bne`) plus the 3+3 rows of the sunk
    //     `li r28,0x102 / li r30,0 / b` block, which we still emit inline.
    //     NEGATIVE (w7-br): the literal CFG -- `goto allocFailed;` to a label
    //     placed AFTER `return ret;` that sets ret/cchBuffer and jumps back to
    //     the stores -- measures byte-for-byte the same 89.0 / 16 rows.  MSVC
    //     re-lays the block from the CFG, so the placement is not
    //     source-reachable; same finding as the loop re-arm above.
    //     Assigning the LocalAlloc result straight to `pchBuffer` (no pchNew
    //     local) is worse: 87.2, 18 rows.
```
