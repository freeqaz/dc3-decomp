# Lane notes: `src/system/hamobj/DetectFrame.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Reset@DetectFrame@@QAAXPBVFilterVersion@@MPBVMoveFrame@@PBUDancerFrame@@W4MoveMirrored@@@Z`

`DetectFrame::Reset` -- moved verbatim from `src/system/hamobj/DetectFrame.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 33-42):

```text
            // w8-i: the `Vector3 &w` hoist is load-bearing, and it is a SCHEDULING
            // lever, not a codegen-size one -- the 47 instructions were already
            // identical apart from the two induction-variable bumps, emitted in the
            // opposite order (image: `addi r31,r31,0x10` then `addi r30,r30,0x4`;
            // ours: r30 first).  Subscripting mNodeComponentWeights[i] three times
            // makes the weights pointer a strength-reduction result DERIVED inside
            // the body, so MSVC sinks its bump below the nodes[] bump; binding the
            // element to a reference at the top of the body makes it an induction
            // variable in its own right, created in source order, and the two bumps
            // come out in the image's order.  99.96 -> 100.0 (188 B).
```


## `?LimbPSNR@DetectFrame@@QBAMPBVFilterVersion@@H@Z`

`DetectFrame::LimbPSNR` -- moved verbatim from `src/system/hamobj/DetectFrame.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 111-124):

```text
            // The image's dot product is ((err.z*w.z + err.x*w.x) + err.y*w.y)
            // (82531984 `fmuls f0, err.z, w.z`, then `fmadds` x, then y); the
            // parentheses give native the same association (w21-at).
            //
            // The weight components are named, z first, for the Length sum
            // (w25-mc). c2 sorts the three squares of that flat sum by a key
            // (c2rs W-STAGETAP-11, C2RS-BRIDGE 8.7) built from each operand's
            // sid, and emits the list's SECOND entry innermost. Reading
            // nodeWeight inline left three CSE temps minted in the dot's
            // z, x, y order (sids 267, 272, 281), so the key order was
            // y > x > z and we squared w.x first. Named in z, y, x order the
            // keys run x > y > z and the image's (y*y + x*x) + z*z follows,
            // with w.y loaded first (lfs f11, 4(r3)). Native value unchanged:
            // the sum is still (x*x + y*y) + z*z, exactly Length()'s.
```
