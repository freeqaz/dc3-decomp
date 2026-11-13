# Lane notes: `src/system/gesture/BaseSkeleton.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?LimbNormPos@BaseSkeleton@@IBAXW4SkeletonCoordSys@@W4SkeletonJoint@@_NABVVector3@@AAV4@@Z`

`BaseSkeleton::LimbNormPos` -- moved verbatim from `src/system/gesture/BaseSkeleton.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 105-157):

```text
// RESIDUAL (w7-bl, 98.28 canonical, 5 of 175 rows; w7-br held): two register-allocator
// decisions, both downstream of identical arithmetic.
//  (1) `bone1` -- the image leaves the ternary's result in the scratch r7 in
//      BOTH branches (`addi r7, r7, 0xb` / `addi r7, r7, 0x6`) and pays a
//      `mr r4, r7` at the BoneLength call site; MSVC here coalesces that move
//      into the addi (`addi r4, r7, 0xb`), so our code is ONE instruction
//      shorter (692 vs 696 bytes).  r4 is only clobbered on the MILO_FAIL path,
//      which branches away before the call, so the coalesce is legal for both.
//      All six sibling ternaries land in the same registers as the image.
//  (2) The second `xori rX, rY, 0x1` (bone3's negated side) is scheduled four
//      slots later in the image, after the three clrlwi/clrrwi; MSVC packs the
//      two xori adjacently.  Same instructions, same registers, same order of
//      the seven `addi` results (indices 60-66).
// NEGATIVE RESULT (w7-br): computing bone3 before bone1/bone2 (so its xori
// temp is dead before bone1's addi) is 98.3 -> 98.2, 15 rows: the two xori
// swap clrlwi/clrrwi partners and all three bone addi's recolour, and the
// `mr r4, r7` at 0x8243459C is still not coalesced.  Both branches of the
// image define bone1 in r7 and every other ternary result lands in the
// register we use, so the un-coalesced move is a hint the allocator did not
// take, not a live range we can shorten from source.
// Both MakeString rows in the Function Call Diff are ICF folds (the assert
// format string and the "Unsupported joint %i" one), not wrong callees.
// w21-w (98.28 canonical, 5 of 175 rows, unchanged): behaviour re-read against
// the image (index ternaries, root/joint compare chain, MILO_FAIL path branches
// to the epilogue, `normalize && len > 0` guard on the fdivs) -- ours agrees;
// the source is token-identical to og-dc3-decomp's.  Measured byte-inert:
// early `if (joint == rootJoint) return;`, bone locals declared first, int
// bone locals cast at the call, `totalLength = 0; totalLength += ...` (not
// shippable anyway: -0.0f -> +0.0f).
// w21-ao (98.28 canonical, same 5 of 175 rows: the bone1 `mr r4, r7`
// coalesce and the second xori slot): measured in a real-flag cl.exe probe,
// all no better -- the 21 single-statement moves of bone1/bone3/joint3
// within both ternary blocks (current order is the unique minimum), bones
// declared before joints, `left`/`!right` ternaries, no bool local, int or
// const bool, a function-scope `bool right`, `result = pos` hoisted, a
// separate bone1Len local, `result *= totalLength`.  Not a source-shape
// lead left that I can see.
// w26-t: `c2rs stage cands --regasg` names the five rows' cause.  bone1 is
// candidate id 12 (pri 24, tie 75) and bone2 is id 13 (pri 18, tie 74); BOTH
// carry `cost r4:-1`, a copy preference for r4, because each is the first
// argument of its own BoneLength call.  bone1 pops first on the higher
// priority and takes r4, so its `addi` writes the argument register directly
// (`addi r4, r7, 0xb`).  The image leaves r4 free and keeps bone1 in r7 with a
// `mr r4, r7` at the call, i.e. it did NOT take the preference -- so the two
// builds disagree on whether r4 is free when id 12 is popped, which is an
// interference-set difference, not a spelling one.  The `xori r5, r4, 0x1`
// row is the same allocator's slot fill (ours emits it back to back with the
// first xori, the image puts it 4 slots later).
// Measured inert in the replay harness (98.2184 canon / 98.1609 fuzzy =
// baseline): a separate bone1Len local, a const SkeletonBone & alias for
// bone1, `totalLength = totalLength + ...` instead of `+=`, three flat ifs
// instead of the nested pair, a named `SkeletonCoordSys camera`.  A named
// bone3Len inside the innermost arm is worse on fuzzy (98.0460).
```


## `?MakeCameraToPlayerXfm@BaseSkeleton@@SAXW4SkeletonCoordSys@@AAVTransform@@PBVVector3@@ABV4@@Z`

`BaseSkeleton::MakeCameraToPlayerXfm` -- moved verbatim from `src/system/gesture/BaseSkeleton.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 216-226):

```text
// w21-ao: 95.93 -> 100 (probe).  The joints are two BLOCK-scoped working
// copies, near and far, and the limb direction is written component-wise as
// far - near into limbDir; origin is copied first.  Block scope is what lets
// MSVC pack far into limbDir's frame slot (0x60) and near into crossDir's
// (0x80): the image's round-robin limb/near/origin store order and its
// separate kUnk5 subtraction (z, y, x into near's registers, then `b` to the
// shared stfs triple at 0x82434288) both follow from it.  The previous
// spelling (limbDir holding far and subtracting in place) computed the same
// values -- every component is one fsubs of the same two operands -- so this
// is a pure shape change.  Earlier lanes' scheduling-floor notes (w7-bl,
// w7-br, w15-a, w20-p, w21-w) are superseded.
```
