# Lane notes: `src/system/hamobj/PracticeSection.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SequenceForDetection@PracticeSection@@QAAPAVDancerSequence@@VSymbol@@0@Z`

`PracticeSection::SequenceForDetection` -- moved verbatim from `src/system/hamobj/PracticeSection.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 182-215):

```text
// RESIDUAL (w8-i, 99.90 canonical / 97.21212 fuzzy): 15 rows of 33, ONE cause --
// the four volatile registers are allocated one slot rotated from the image's.
// Image: r6=idx, r7=gNullStr, r8=mSteps.end(), r9=it->mStart.  Ours: r6=gNullStr,
// r7=end, r8=mStart, r9=idx.  Everything else follows, including the OFFSET_SWAP
// at idx 17/18, which is just the two induction bumps in the opposite order
// (image `addi r6,r6,0x1` then `addi r11,r11,0x18`).
// REFUTED, five spellings, every one BIT-IDENTICAL to this body (same 15 rows,
// same registers): (a) `++idx, ++it` instead of `++it, ++idx`; (b) hoisting the
// iterator out of the for-init so `int idx = 0` is the for-init declaration;
// (c) moving `idx++` to the END of the loop body, which is the source position
// that would put the idx bump first if the bump order were reachable at all;
// (d) `unsigned int idx`; (e) binding `PracticeStep &step = *it;` at the top of
// the body -- the lever that closed DetectFrame::Reset, inert here because `it`
// is already an induction variable rather than a body-derived address.
// The permutation is a whole-set rotation, i.e. an allocator tie-break upstream
// of anything the source can say.
// BEHAVIOURAL FIX (w9-a): `idx` indexes mSeqs by the count of steps that have
// BOTH symbols set, not by the raw step position.  The image increments it inside
// the non-null test, and only on the path where the start/end match FAILS:
//   824CD1BC  lwz r9, 0x4(r11)      ; it->mStart
//   824CD1C0  cmplw cr6, r9, r7     ; == gNullStr?
//   824CD1C4  beq cr6, 824CD1E8     ; -> straight to ++it, SKIPPING ++idx
//   824CD1C8  lwz r10, 0x8(r11)     ; it->mEnd
//   824CD1D0  beq cr6, 824CD1E8     ; -> same
//   824CD1D4  cmplw cr6, r9, r4     ; mStart == start?
//   824CD1D8  bne cr6, 824CD1E4     ; -> ++idx
//   824CD1DC  cmplw cr6, r10, r5    ; mEnd == end?
//   824CD1E0  beq cr6, 824CD1FC     ; -> found, idx NOT incremented
//   824CD1E4  addi r6, r6, 0x1      ; ++idx
//   824CD1E8  addi r11, r11, 0x18   ; ++it
// A step with an unset mStart or mEnd therefore does not consume an mSeqs slot.
// The old `++it, ++idx` in the for-increment counted every step, so any section
// holding a partially-filled step returned the WRONG DancerSequence (or a
// spurious null once idx ran past mSeqs.size()) for every later step.
```
