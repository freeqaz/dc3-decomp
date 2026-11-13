# Lane notes: `src/system/hamobj/RhythmDetector.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?AnalyzeData@?A0x5c754947@@YAXABV?$vector@UFrame@RhythmDetector@@V?$StlNodeAlloc@UFrame@RhythmDetector@@@stlpmtx_std@@@stlpmtx_std@@AAM11M_NVSymbol@@2PAVDebugGraph@@HPAVTextStream@@@Z`

`AnalyzeData` -- moved verbatim from `src/system/hamobj/RhythmDetector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 126-131):

```text
                        // Z-score middle section with sliding window
                        // w16-b: the plain loop (MSVC rotates it and
                        // strength-reduces i into the byte IV r7 itself) gives
                        // the image's preheader order `li r7, 0x18 / li r4, 0x1
                        // / subi r29, r30, 0x6`; the hand-rotated do-while with
                        // three named counters (w7-bf) emitted them rotated.
```


## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/hamobj/RhythmDetector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

File-level (was lines 359-367; nearest following definition: `SetupFrame`):

```text
// w21-aw: the image's .rdata holds this table at 0x8204CD70..0x8204CDBF,
// directly ahead of gRev, and unnamed in ham_xbox_r.map (so a file-scope
// `static`, not an anonymous-namespace const, which the map would name).
// Nothing addresses it with lis/addi, so the image only used it in folded
// form; MSVC emits a const array for a constant-index use but NOT for an
// unreferenced one or a sizeof-only one (both measured).  Its presence ahead
// of gRev is what makes ASSERT_REVS anchor on gAltRev (see INIT_REVS in
// obj/Object.h).  The one use, in SetupFrame's resize just below, is our
// spelling; it folds to the same constant 20.
```


## `?ProcessFrames@RhythmDetector@@AAAXXZ`

`RhythmDetector::ProcessFrames` -- moved verbatim from `src/system/hamobj/RhythmDetector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 847-879):

```text
// w7-bv: 94.37 -> 100.0 canonical (99.66 raw, one commutative add row).  The
// w7-ai note above this function blamed the image's extra callee-saved register
// (bl __savegprlr_18, frame 0x140, `addi r21, r29, 0x4` at 0x824D5858) on a
// `Frame &cur = mCurrentFrame;` reference, which measured 93.4 / 93.7.  The
// second pointer is not a reference: it is the STRUCT assignment
// `mCurrentFrame = *it;` (Frame::operator= inlined: float copy + the
// vector<Vector3>::operator= call at 0x824D5948), which addresses both fields
// off the two Frame bases (`addi r3, r29, 0x4` / `addi r4, r30, 0x4`) instead of
// deriving them from this+0x20 and the node as two member assignments do.
// `mCurrentFrame = localHistory.back();` is the same lever for the tail
// (0x824D59F4-0x824D5A0C).  That alone was 94.37 -> 95.2; the rest:
//   - stlport vector::erase(first, last) inlines `if (first == last) return`,
//     so the old `begin() != trimEnd + 1` guard was redundant and added a dead
//     home store of begin(); dropped.
//   - `hadBlendedFrames = true` at the END of the inner loop body is LICM'd
//     LAST into the preheader (`li r18, 1` at 0x824D58D8, after the other two
//     invariants); before the loop or at the body start it is emitted first,
//     after the loop it stays after the loop.
//   - naming the trim bound (`float threshold = ...`) computes the fsubs
//     before the loop-entry beq (0x824D5BB8 / 0x824D5BBC) and reorders the
//     end() home stores to match; inline in the condition it lands after.
//   - the second FindArray's Symbol is passed as an explicit copy,
//     `Symbol(analyzePeriodCount)`: that is the only spelling that materialises
//     the argument temporary in memory (`stw r4, 0x80(r31)` at 0x824D5AFC),
//     and the temp slot it claims is what also moves the AnalyzeData
//     stack-argument stores into the image's order (0x824D5C3C-0x824D5C60):
//     both clusters (8 rows, 97.7 -> 100.0) went together.  Inert for that
//     store: `->Int(1)` on the FindArray result (full-expression lifetime),
//     nullptr vs 0 for the pointer args.
//   - the windowSize product wants NAMED float operands, declared periods
//     then beats: that yields the image's `fmuls f13, f12, f13` at
//     0x824D5B84; every unnamed permutation of the three factors gives
//     f13, f12.
```

Inside the body (was lines 905-932):

```text
                    // RESIDUAL (w7-bv): 0x824D58DC is `add r11, r28(j), r27`
                    // and we emit `add r11, r27, r28` -- one commutative
                    // register row, 100.0 canonical / 99.66 raw.  Inert:
                    // prevTick + j + 1, j + 1 + prevTick, j + (prevTick + 1),
                    // an in-body `int tick = prevTick + 1` (same row), and a
                    // named `nextTick` ABOVE the loop, which does flip the
                    // operands but is no longer LICM'd: its addi lands before
                    // j's `mr r28, r19` instead of after it and drags eight
                    // registers with it (96.3 raw).
                    // c2 key (w25-gu, C2RS-BRIDGE 8.7): the operands are the
                    // LICM'd temp prevTick+1 (sid 959, key 0x1efc0) and the
                    // variable j (sid 13, key 0x101a0); higher key first, so
                    // the temp leads.  j can only lead if the temp's sid moves
                    // to 1024..1030 (key wraps below 0x101a0) or the hoisted
                    // value becomes a named variable with a lower sid than j
                    // (flips the row, but changes COLOR priorities as above).
                    // Open: what mints the LICM temp's sid.
                    // w26-n: the CQS dump confirms the pair exactly
                    //   CQS V959/k02s03{0001efc0} V13/k02s04{000101a0}
                    // and four more source orders around it
                    // (prevTick + (j + 1), j + prevTick + 1, j + 1 + prevTick,
                    // 1 + prevTick + j) are all byte-identical, which is the
                    // sort canonicalising the expression rather than a source
                    // knob.  The base-reference lever does not apply to this
                    // row: neither operand is a memory operand, and the only
                    // way across is the temp's sid reaching 1024..1030 (+65..71
                    // symbols minted before it, the same crossing CharBones
                    // RotateBy needs) -- a symbol-count question, not a binding.
```
