# Lane notes: `src/system/rndobj/Overlay.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Draw@RndOverlay@@AAAMM@Z`

`RndOverlay::Draw` -- moved verbatim from `src/system/rndobj/Overlay.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 73-93):

```text
    // Residual (w25-fb, fuzzy 99.94, 1 row): the image emits this fadds as
    // `topY + sDrawFloats[1]` (fadds f0, f31, f0), ours `sDrawFloats[1] + topY`.
    // Both operands are colour candidates: topY is the formal (id 3, f31) and the
    // sDrawFloats[1] load is a promoted static-memory symbol (k07, id 18, f0).
    // c2 lists the higher candidate id first, and a formal's id is fixed at 3
    // (arena order: k0d, this, formals, locals, memory symbols), so no local
    // spelling can put topY ahead.  Measured inert (~100 variants): source
    // operand order, `pos.y += topY` / `pos.y = topY; pos.y += ...`, a pointer to
    // the array, named x/y/height/now/mat/bottom locals.  Hypothesis: in the
    // original topY reaches this add through a later-minted symbol (a k03 temp
    // or split piece); open question is which IL shape does that.
    // w26-f14 (c2rs tap, `CQT cqlo3`): the use-list keys are V34{0x10440} (the
    // promoted static-memory element, sid 34) and V2{0x10040} (the formal), so
    // the element sorts first.  topY can only lead if the element leaves the
    // variable-leaf class -- a memory leaf on a base with sid%4 == 0 keys
    // 0x10008 -- or if topY arrives as a subexpression (SU<<24 beats every leaf);
    // a formal's key is fixed and no local can outrank sid 34.  Re-measured inert
    // (exact, per-object + patch chain): the static as a `Vector2` object,
    // pointer-to-array and pointer-to-element aliases, a scalar reference to the
    // element, a reference to the formal, `pos.y = topY; pos.y += sDrawFloats[1]`,
    // and a named intermediate.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.937, 1 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.
