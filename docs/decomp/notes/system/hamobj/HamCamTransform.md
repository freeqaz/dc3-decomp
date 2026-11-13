# Lane notes: `src/system/hamobj/HamCamTransform.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Save@TransformCrowd@@QBAXAAVBinStream@@@Z`

`TransformCrowd::Save` -- moved verbatim from `src/system/hamobj/HamCamTransform.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 152-160):

```text
// w10-e: 100.0, and the template operator<<(BinStream&, vector<TransformCrowd>)
// with it (72.115 -> 100.0, Save is no longer inlined there).  The image loads
// mCrowdRotate BEFORE the first call (`lwz r31, 0x14(r11)` at 0x824AC144, into a
// callee-save), and stores it into the WriteEndian home slot only AFTER that call
// (0x824AC14C), then writes through the RETURNED stream (r3 carried into
// WriteEndian).  That is exactly a value read up front plus a write through the
// returned reference.  Chained `bs << mCrowd << mCrowdRotate` let MSVC sink the
// home store above the call (no callee-save, 77.39) and made Save small enough to
// inline into the vector template; two plain statements lose the r3 reuse (71.0).
```
