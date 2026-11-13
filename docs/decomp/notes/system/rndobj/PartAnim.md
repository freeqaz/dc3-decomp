# Lane notes: `src/system/rndobj/PartAnim.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Load@RndParticleSysAnim@@UAAXAAVBinStream@@@Z`

`RndParticleSysAnim::Load` -- moved verbatim from `src/system/rndobj/PartAnim.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Attribution: definition is macro-generated (BEGIN_LOADS); symbol matched by the name the macro expands to

Inside the body (was lines 108-119):

```text
        // w16-a: the image calls the std::vector BinStreamRev reader DIRECTLY
        // for each operand and, as for any nested free-function call chain,
        // evaluates the arguments right to left -- materialising the third
        // and second operand addresses into r29/r30 before the first call
        // (subi r29,r30,0x24 / subi r30,r30,0x30 / bl >> / mr r4,r30 / bl >>
        // / mr r4,r29 / bl >>).  Our math/Key.h routes Keys through an inline
        // `operator>>(BinStreamRev &, Keys &)` wrapper (it is needed to beat
        // BinStreamRev's generic member template, which the retail header
        // evidently did not have), and inlining it sequences each call before
        // the next operand is formed.  Binding the vector base directly is
        // exactly what that wrapper does, so behaviour is unchanged; it closes
        // the 5-row residual w7-w recorded here (99.0 -> 100).
```
