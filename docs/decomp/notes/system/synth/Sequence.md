# Lane notes: `src/system/synth/Sequence.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `??0RandomGroupSeqInst@@QAA@PAVRandomGroupSeq@@@Z`

`RandomGroupSeqInst::RandomGroupSeqInst` -- moved verbatim from `src/system/synth/Sequence.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 626-647):

```text
// w14-e (99.10, 27 register rows + the image's `stw r29, 0x50(r31)` of
// childrenSize just before the NextIndex call, at an EH-table label).  The map
// lists RandomGroupSeq::AddToPlayedHistory as `f i`; marking it inline (it is
// reached through PickNextIndex) is byte-inert for this ctor, so not kept.
// w18-e (99.10): `next % children.size()` reloads the size after NextIndex
// (98.2); std::min(mNumSeqs, childrenSize) homes childrenSize before the
// compare and selects addresses (95.3).  The image's min is a value select.
// w21-av (99.10 normalized, fuzzy 97.66 -> 99.10): a separate `childrenLeft`
// counter for the reservoir loop (rb3-xenon's shape) fixed every register row.
// One row left: the image's dead `stw r29,0x50(r31)` of childrenSize just
// before `bl NextIndex` (8274C4F4; 0x50 is the else-arm's (float)childrenLeft
// conversion slot). Inert: the rb3-xenon ternary for numSeqs; `const int &`
// childrenSize. Worse: childrenLeft declared at function scope (registers
// rotate back, 97.7 fuzzy).
// w21-bf (still 99.10, the same one row): `int target = seq->NextIndex() %
// childrenSize;` (no `next` local) is inert; function-scope childrenLeft
// re-measured at 98.20 canonical (adds `mr r26, r29`, permutes r26/r28/r29).
// w-homeslot (2026-11-13, still 99.10, the same one row): routing the modulo
// through an inline `int f(int a, const int &b) { return a % b; }` called as
// f(seq->NextIndex(), childrenSize) -- right-to-left argument evaluation, so a
// const-ref home of childrenSize would land just before `bl NextIndex` -- is
// byte-inert: MSVC folds the reference through the inline and emits no store.
```
