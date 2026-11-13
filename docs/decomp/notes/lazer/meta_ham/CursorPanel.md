# Lane notes: `src/lazer/meta_ham/CursorPanel.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@CursorPanel@@UAAXXZ`

`CursorPanel::Poll` -- moved verbatim from `src/lazer/meta_ham/CursorPanel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 24-29):

```text
// w16-e: `CursorPanel *self = this;` (calls through self) is inert.
// w20-t (96.6, branch-scan rows 129/269 adjudicated ARTIFACT): row 129 is the
// image hoisting `addi r3, r3, 0x40` (&pMat->mDiffuseTex) above `bne` on
// GetMiscArt(), both arms reach the same SetObjConcrete with the same r3/r4;
// row 269's back-edge lands one row earlier in ours only because of the
// `lwz r29, 0x1a4(r31)` reload of `this` described below. No behaviour change.
```

Inside the body (was lines 31-56):

```text
    // RESIDUAL (w7-ao, 96.52 canonical): 3 of the remaining rows are one extra
    // spill of `this`. The image keeps it in r14 for the whole function; we
    // keep it in r29 AND store it to 0x1a4(r31), then reload it with an extra
    // `b` + `lwz r29, 0x1a4(r31)` at the top of the loop. The image spends its
    // one frame word on the ui_crown_player Symbol instead (`stw r30, 0x5c(r31)`
    // / `lwz r15, 0x5c(r31)`), which we hold in r14 with no store -- so this is
    // one register too few, not a missing statement. The rest is the r16..r21
    // constant-pointer permutation and the `trans.m.x *= 4` scheduling below.
    // w21-e: the `trans.m.x *= 4` block is closed (Scale, below).  Re-tested
    // the `this` spill: still there, and `Skeleton *skeleton` hoisted to
    // function scope is byte-identical.  Left: the spill/split (rows 6-12,
    // 82-83, 184-188), the r16..r21 constant permutation and the r28..r30
    // permutation that follows from it -- all register-only, behaviour
    // checked: every call gets the same values as the image.
    // w21-aa (96.57, not reworked): re-read every branch target and argument
    // against the image (crown-loss ||, MILO_LOG arg = &sCrownPlayerIndex,
    // SetProperty(ui_crown_player, -1) via the 0x5c spill, ScreenPos joints
    // 3/2, MakeRotMatrix(v, m, true), Scale x4): no behaviour difference.
    // The three name rows (MakeString<char[19],int,char[5]>, <_D3DFORMAT>,
    // SetObjConcrete<AnimTask>) are ICF aliases of ours.  Left as w21-e left it.
    // w24-c1 (96.52, unchanged; same 43 rows: the `this` home-slot spill
    // 6-12/82-83/184-188 and the r16..r21 / r28..r30 permutations).
    // Re-read the crown-loss branch targets (8292F8FC..) against the image:
    // same truth table.  Measured: reading `mDir` directly instead of
    // LoadedDir() (one inline level fewer) at all three Find sites is
    // byte-inert.  og-dc3-decomp spells the function identically.
```

Inside the body (was lines 74-85):

```text
        // w27-h: the choice is spelled through its own named local.  Same values
        // and same branch as `if (!tex) tex = pBufferLeftTex;`, but the extra
        // nonvolatile pointer changes c2's free list by one, which is the 8.4
        // free-count lever: measured 95.89963 -> 96.04833 fuzzy, canonical
        // 96.56877 unchanged.  The row's remaining difference is a colour
        // decision, not a value: the tap gives `this` (sym 4/k05) pri -18 tie 9
        // and colours it last as split1#134 into the only register left (r29),
        // so we home-store it to 0x1a4(r31) and reload it at the top of the loop,
        // while the image pops it first into r14 and spends its one frame word
        // on the ui_crown_player Symbol instead.  Named re-bindings of
        // LoadedDir()/pBufferLeftTex/pMat and an extra DataNode or Skeleton
        // pointer were all measured inert (96.04833 at best).
```

Inside the body (was lines 127-133):

```text
            // w21-e: the image's scrambled 9-load/9-store block (loads 0xa8,0xa4,
            // 0x90,0xb8,...; stores 0x90,0x94,0x98,0xa0,0xb0,...) is the math/Mtx.h
            // Scale(const Vector3 &, const Matrix3 &, Matrix3 &) inline with a
            // (4,4,4) vector -- closes all 18 rows.  Same values as scaling each
            // row by 4.  (Scale(trans.m, Vector3(4,4,4), trans.m), the other
            // overload, is far worse: 85.1.)  Earlier refuted spellings:
            // `trans.m.x *= 4` x3 (95.5 reversed), Scale(trans.m.x, 4, ...) x3.
```
