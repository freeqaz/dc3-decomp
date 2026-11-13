# Lane notes: `src/system/hamobj/SuperEasyRemixer.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `RoutineMeasureCount()`

`RoutineMeasureCount` -- moved verbatim from `src/system/hamobj/SuperEasyRemixer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Attribution: no function named `RoutineMeasureCount` in report.json (unit); filed under the source name

Above the definition (was lines 17-26):

```text
// The image computes `mRoutine.size()` by loading `_M_start` BEFORE
// `_M_finish` (824F6FF0 lwz r11,0x0(r31) / 824F6FF8 lwz r10,0x4(r31) / subf
// r10,r11,r10 / divw. r10,r10,r26) and keeps `_M_start` live in r11 for
// `mRoutine[0].preferred` right after.  stlport's `size()` as spelled
// (`_M_finish - _M_start`) makes MSVC load 0x4 first.  Naming both pointers in
// declaration order reproduces the image's load order; this is the
// function-scoped form of the header experiment w7-bk measured and refuted
// tree-wide (720 regressions) -- do NOT move it into stlport.  Measured on
// SaveSuperEasyMoveParents only (98.44 -> 100.0, 195/195 rows); the same
// helper makes LoadAllVariants worse, see the note there.
```


## `?DumpSongLayout@SuperEasyRemixer@@IAAXXZ`

`SuperEasyRemixer::DumpSongLayout` -- moved verbatim from `src/system/hamobj/SuperEasyRemixer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 141-165):

```text
    // RESIDUAL (w7-bv, 97.9 canonical, 17 rows): the image carries `(i-1)*4`
    // as the register induction variable (`li r25, -0x4` in the preheader at
    // 824F6198, `addi r27, r25, 0x4` for the `[i]` index at 824F61F4) where we
    // reduce `i*4` (`slwi`) and subtract 4 for `[i-1]`.  Every spelling that
    // names `i - 1` makes it worse, not better: `int prev = i - 1` with
    // `[prev]`/`[i]` is 94.8 (prev and i both get home slots, frame +0x10);
    // `[prev]`/`[prev + 1]` does produce the `li -4` IV but keeps the homed
    // locals, 96.7.  The two `>= 0` / `+ 2` uses of `i - 1` (r23) are CSE'd
    // by MSVC on its own from the anonymous form.
    // w21-bd (97.93714, same 17 rows: li r25,-4 IV, addi r27,r25,4 vs our
    // slwi, and the r25/r26 + r27/r28 colouring that follows).  Inert or
    // worse, re-measured: `int prev = i - 1` at the loop top with `[prev]`
    // only (95.1, frame +0x10 -- register pressure spills the "<" literal
    // address, not a homed local); `[prev]`/`[prev + 1]` everywhere (96.7,
    // reproduces w7-bv: the IV appears but prev and i both stay live);
    // `[i + -1]`, `[i - 1 + 1]` (folded by the front end, identical bytes);
    // the Name() element bound to a local first (identical).
    // The image carries (i-1)*4 as its own induction variable (`li r25, -0x4`
    // in the preheader, `addi r27, r25, 0x4` for the `[i]` index, and
    // `addi r25, r25, 0x4` on the back edge) and derives i*4 from it. A
    // second counter that is exactly `i - 1` is what produces that: indexing
    // the two former `i - 1` sites by `prev` lets c2 carry their (i-1)*4 as
    // the byte IV, leaving the `[i]` sites to be derived from it. Spelling
    // those sites `i - 1` instead reduces `i*4` per iteration (`slwi` plus
    // `subi`) and adds the `i`-minus-one live range. (w26-v, 97.94 -> 100.)
```

Inside the body (was lines 179-186):

```text
                    // Each pair member is a named local: the image evaluates the
                    // FIRST argument's lookup first and holds the element itself
                    // (not the vector's begin) across the second call
                    // (`lwzx r14, r25, r10` at 824F628C before the second bctrl).
                    // Nested in the call, MSVC evaluates right-to-left and defers
                    // the element load past the other call, and &TheDebug then
                    // stays in r14 instead of being home-stored to 0x58(r31)
                    // (w7-ak's class (2)).
```

Inside the body (was lines 194-200):

```text
                    // BUG FIX (w7-bv): the second pair is (easier[i-1], harder[i]),
                    // not (harder[i], easier[i-1]).  At 824F62FC..824F631C the
                    // image passes r4 = the d-row element at (i-1)*4 (r28, loaded
                    // from the FIRST GetMoveParentsByDifficulty(d) call) and
                    // r5 = the next-row element at i*4.  FindVariantPair is
                    // directional (p1 -> p2), so the swapped order asked the
                    // wrong question for the '>' marker.
```


## `?SaveSuperEasyMoveParents@SuperEasyRemixer@@IAAXXZ`

`SuperEasyRemixer::SaveSuperEasyMoveParents` -- moved verbatim from `src/system/hamobj/SuperEasyRemixer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 240-255):

```text
        // NEGATIVE RESULT (w7-bg): LoadAllVariants floors at 99.57% canonical,
        // 4 charged rows of 239.  Two of them are the vector-size load order:
        // the image loads `_M_start` (0x0) and then `_M_finish` (0x4)
        //   824F6D5C lwz r11, 0x0(r28) / 824F6D64 lwz r10, 0x4(r28)
        //           / subf r10, r11, r10 / divw r10, r10, r25
        // while `size()` as spelled in stl/_vector.h (`_M_finish - _M_start`)
        // makes MSVC load 0x4 first.  The third is an extra `lwz r11, 0x0(r29)`
        // at the top of the loop body: the image keeps `_M_start` live in r11
        // from the bottom-of-loop size computation and reuses it for
        // `mRoutine[i]`, we reload it.
        // Refuted: `i < data->mRoutine.end() - data->mRoutine.begin()`,
        // which was the obvious way to flip the operand order -> 98.46%, worse.
        // The SAME (0x0,0x4) signature shows up on SaveSuperEasyMoveParents
        // (98.44%), GetRows in net_ham/ChallengeSystemJobs (98.97%) and
        // MoveGraph::FindVariantPair (98.10%), so it is a property of how
        // stlport's `size()` is spelled, not of this call site.
```


## `?LoadAllVariants@SuperEasyRemixer@@SAXXZ`

`SuperEasyRemixer::LoadAllVariants` -- moved verbatim from `src/system/hamobj/SuperEasyRemixer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 313-317):

```text
// (w21-ae: closed to 100 -- see the `routine` reference in the mRoutine loop.)
// w20-t (99.6, branch-scan row 219 adjudicated ARTIFACT): the image keeps the
// layouts vector's _M_start in r11 from the loop-condition block (lwz r11,
// 0x0(r28)) where we re-read it at the body top (lwz r11, 0x0(r29)); both reads
// follow the same InsertVariants/Notify calls, so the value is identical.
```

Inside the body (was lines 370-396):

```text
        // NEGATIVE RESULT (w7-bg): LoadAllVariants floors at 99.57% canonical,
        // 4 charged rows of 239.  Two of them are the vector-size load order:
        // the image loads `_M_start` (0x0) and then `_M_finish` (0x4)
        //   824F6D5C lwz r11, 0x0(r28) / 824F6D64 lwz r10, 0x4(r28)
        //           / subf r10, r11, r10 / divw r10, r10, r25
        // while `size()` as spelled in stl/_vector.h (`_M_finish - _M_start`)
        // makes MSVC load 0x4 first.  The third is an extra `lwz r11, 0x0(r29)`
        // at the top of the loop body: the image keeps `_M_start` live in r11
        // from the bottom-of-loop size computation and reuses it for
        // `mRoutine[i]`, we reload it.
        // Refuted: `i < data->mRoutine.end() - data->mRoutine.begin()`,
        // which was the obvious way to flip the operand order -> 98.46%, worse.
        // The SAME (0x0,0x4) signature shows up on SaveSuperEasyMoveParents
        // (98.44%), GetRows in net_ham/ChallengeSystemJobs (98.97%) and
        // MoveGraph::FindVariantPair (98.10%), so it is a property of how
        // stlport's `size()` is spelled, not of this call site.
        // w7-bm: RoutineMeasureCount (above) fixes this signature on
        // SaveSuperEasyMoveParents (-> 100) but NOT here: it trades the 4 rows
        // for 2 inserted `stw r11, 0x68(r31)` home stores of `_M_start` right
        // after each size computation (99.57 -> 99.2), exactly the 99.16 the
        // header experiment (c) recorded for this function.  Left as size().
        // w21-ae: CLOSED (99.57 -> 100, 238/238 rows, raw 100 too): bind the
        // vector to a local reference ONCE and index/size() through it.  With
        // `data->mRoutine` re-spelled at each use MSVC reloaded `_M_start` at
        // the body top and loaded `_M_finish` first at the latch; through the
        // reference it keeps `_M_start` live in r11 like the image.  The
        // floor notes above are superseded.
```

Inside the body (was lines 399-413):

```text
            // BEHAVIOURAL FIX 2026-09-14 (w7-q): this read `.second` (offset
            // 0x4, "MoveVariant to use for transition OUT of measure").  The
            // image reads offset 0x8 -- `.preferred`, "Preferred MoveVariant
            // for this measure".  build/373307D9/asm/system/hamobj/
            // SuperEasyRemixer.s, inside the mRoutine loop (element stride
            // 0xc, so the three Symbols are at 0x0/0x4/0x8):
            //     mulli r10, r29, 0xc
            //     add   r8,  r10, r11
            //     lwz   r30, 0x8(r8)      ; .preferred   <-- not 0x4
            //     cmplw cr6, r30, r9      ; vs gNullStr
            //     bne   cr6, ...
            //     lwzx  r30, r10, r11     ; .first  (offset 0x0) fallback
            // This is the same preferred -> first order the sibling loader
            // above documents for this very function.  Reading `.second` made
            // every supereasy measure request the transition-out variant.
```
