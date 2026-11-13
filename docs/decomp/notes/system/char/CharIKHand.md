# Lane notes: `src/system/char/CharIKHand.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@CharIKHand@@UAAXXZ`

`CharIKHand::Poll` -- moved verbatim from `src/system/char/CharIKHand.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 280-289):

```text
    // Poll() is entered through the CharPollable sub-object; naming the full
    // object once keeps the adjusted pointer in a callee-saved register.
    // w21-ba (99.797 -> 100): the mScalable test goes through `self` too.  With
    // `mScalable` read off the adjusted `this`, MSVC materialised the `false`
    // for the mHandChanged clear (`li r28, 0x0`) before `subi r27, r3, 0x28`;
    // reading both flags through `self` gives the image's order.  (Earlier
    // inert spellings: an extra bool local, the declaration hoisted to the top
    // or moved below destPos/destQuat, `CharIKHand &self`; removing the local
    // is 99.4.  Clearing the flag through `this` instead is worse -- the store
    // moves to r29.)
```


## `?IKElbow@CharIKHand@@IAAXPAVRndTransformable@@0@Z`

`CharIKHand::IKElbow` -- moved verbatim from `src/system/char/CharIKHand.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 473-485):

```text
// Two charged rows remain, both commutative operand order (c2 §8.7 cqlo3
// sort), and both live inside an inlined callee, so no spelling in this .cpp
// reaches them:
//   [367] `Add(axisProj, mWorldDst, axisProj)` (Vec.h): the image orders the
//     x component mWorldDst-first and y/z axisProj-first; ours is uniform.
//   [550] `MakeRotMatrix(rotQuat, rotMat)` (Rot.cpp): one fadds inside the
//     rotation-matrix build, again only the x row.
// w26-m measured inert here: a named `const Vector3 &mwd = mWorldDst`, a named
// `float mwx` / `float apx`, `axisProj += mWorldDst`, three per-component
// statements, swapped Add arguments, `ap_decl` reorder, and `const Hmx::Quat &q`
// / `Hmx::Matrix3 &rm` / `Hmx::Quat q(...)` bindings at the MakeRotMatrix call
// site.  The per-component rewrites also change the emitted code badly (333/756),
// so the Add spelling is fixed by the header body, not by the caller.
```

Inside the body (was lines 591-620):

```text
                // w21-am (96.24 -> 100.0 canonical, fuzzy 99.97): the four quat
                // products are written out here in the IMAGE's association
                // instead of through math/Mtx.h's Multiply(Quat, Quat, Quat).
                // Read off the target (0x823863BC-0x823864F0): all 16 r*d
                // products are plain fmuls, then each component is summed as
                // ((p1 + p2) + p3) - p4 with no fneg, and the two w terms
                // `rw*dw - rx*dx` are computed TWICE (no CSE between d*r and
                // r*d).  Through Mtx.h's `-(a - (b + c + d))` spelling MSVC
                // re-sorts the flat sums, shares partial sums between d*r and
                // r*d and fuses them into fmadds/fmsubs + fneg (96.24).  The
                // r*d w term is spelled `-(rx*dx) + rw*dw` (== rw*dw - rx*dx
                // exactly) only to keep MSVC from CSE-ing it with d*r's w, as
                // the image does not; every component is the same product set
                // and sign as Multiply(), and the parenthesised association is
                // what the image computes, so native now rounds like the image.
                // Remaining 2 rows are commutative fadds operand order (idx 367
                // Add(axisProj, mWorldDst) x, idx 550 rz*dy + rw*dx): swapping
                // the source operands is inert.
                // Open (c2 §8.7 sort key, w25-hb): both are leaf-vs-leaf.
                //   idx 367: axisProj.x is var V1424 {0x1b200}, mWorldDst.x a
                //   load temp V2541 {0x17b40}; the image puts the load first
                //   on x only, while y (V1423 vs V2533) and z (V1133 vs V2523)
                //   keep axisProj first. No uniform sid shift satisfies all
                //   three (x needs temp sid mod 1024 > 712, y < 712, z < 567).
                //   idx 550: both products are CSE temps (V2674 = rw*dx,
                //   V2669 = rz*dy) and the higher sid sorts first. The image
                //   minted rz*dy later. Operand swaps in either quat group,
                //   `+=` and Add(mWorldDst, axisProj) are inert or change code.
                // (w21-j's earlier stop note: a separate result quat for the
                // r*d chain was 95.8 -- REFUTED as the lever; association was.)
```
