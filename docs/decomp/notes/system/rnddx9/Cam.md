# Lane notes: `src/system/rnddx9/Cam.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Select@DxCam@@UAAXXZ`

`DxCam::Select` -- moved verbatim from `src/system/rnddx9/Cam.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 72-107):

```text
        // w7-bs (2026-09-15) 84.130 -> 98.8 canonical (82.971 -> 98.0 fuzzy),
        // 138 rows: 115 equal / 21 diff_arg / 1 replace / 1 delete.  Two
        // levers, both refuted-as-impossible by earlier notes and both real:
        //
        //  1. `mgr`, a reference bound for THIS call only.  The image holds
        //     TheShaderMgr in r31 and its vtable in r29 across the Matrix4
        //     ctor (8261EBB0/EBB4) and never reloads it; every other call
        //     site in the block reloads the global.  A block-wide reference
        //     moves those four sites the wrong way (w7-bo); one scoped to the
        //     call gives exactly the image's __savegprlr_29 prologue and the
        //     `mr r3, r31` after the ctor.  12 rows (prologue/epilogue + 4).
        //
        //  2. RectToVector4() above.  The image reads the ScreenRect() result
        //     through the POINTER the sret call returns (`mr r11, r3` at
        //     8261EBE4, lfs 0xc/0x8/0x4/0x0 descending) and builds the Vector4
        //     in a temp it passes straight to Set{V,P}Constant.  That is an
        //     inlined function whose `const Hmx::Rect &` parameter is the call
        //     result: a reference bound to the temp in THIS function reads
        //     the slot, a parameter reads r3.  The two Vector4 temps take
        //     0x60 / 0x50 and the shared sret temp 0x70, as in the image --
        //     the SLOT TRIPLE w7-bo filed as MSVC's own order was this.
        //     w7-ap's three refuted spellings all named a local; none passed
        //     the call result to a parameter.  MSVC also emits an out-of-line
        //     COMDAT copy of the helper (36 B, unreferenced -- /OPT:REF drops
        //     it; 15 other TUs already carry `static inline` helpers).
        //
        //  Residual, 23 rows, no lever found:
        //   - the image RELOADS mTargetTex->mType for the setClear mask
        //     (`lwz r11, 0x54(r11)` at 8261EAF4) while keeping the first load
        //     in r9 for both == compares; we CSE all three reads into one.
        //     Spelling the mask through TargetTex(), or `type` through a
        //     local RndTex*, is inert.  1 replace + 1 delete + the r8/r9/r10
        //     relabelling that follows (16 rows).
        //   - TheShaderMgr@h in r30 (image) vs r29 (ours), with the vtable in
        //     the other; both are the third callee-saved GPR the `mgr` lever
        //     introduced.  7 rows.
```

## `?ProjectZ@DxCam@@UAAIM@Z`

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 98.462, 5 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.
