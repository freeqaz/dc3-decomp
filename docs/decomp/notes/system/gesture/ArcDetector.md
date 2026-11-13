# Lane notes: `src/system/gesture/ArcDetector.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Draw@ArcDetector@@QAAXABVSkeleton@@AAVSkeletonViz@@@Z`

`ArcDetector::Draw` -- moved verbatim from `src/system/gesture/ArcDetector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 122-129):

```text
            // CLOSED (w26-f8, fuzzy 99.77778 -> 100.0, canonical 100.0).  The
            // three residual rows were `Vector3 pos(jpos.x + off.x, ...)`
            // emitting the offset leaf first (image: jpos first).  Both are
            // memory leaves under a named reference, so C2RS-BRIDGE 8.7/8.8
            // key them on the base reference's symbol id -- declaring `off`
            // before `jpos` puts jpos's id higher and it sorts first.  Swapping
            // the operands in the source (off.x + jpos.x) is INERT, as 8.7
            // predicts for a canonicalised commutative sort.
```


## `?GetPathLength@ArcDetector@@ABAMXZ`

`ArcDetector::GetPathLength` -- moved verbatim from `src/system/gesture/ArcDetector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 201-205):

```text
                // w20-c: Subtract then Length at the call site gives the
                // image's term order (fsubs ez first, fmuls ez*ez, fmadds ey
                // then ex); hand-written ex/ey/ez gave a y-first sum (fuzzy
                // 99.33, f0/f13 swap).  Distance(p2, p1) -- the same two
                // helpers one inline level deeper -- is WORSE (6 rows).
```


## `?GetPathError@ArcDetector@@ABAMXZ`

`ArcDetector::GetPathError` -- moved verbatim from `src/system/gesture/ArcDetector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 241-256):

```text
        // RESIDUAL (w7-az, 93.55 canonical): the image squares the literal
        // __real@00000000 (`fmuls f12, f13, f13`) and keeps a dead
        // `fmr f4, f13` at 0x82E00B98.
        // KNOWN 100% SPELLING, DELIBERATELY NOT USED (w21-n, coordinator
        // decision 2026-10-03): `Vector3 err(arcZBase, pt.y, dx);
        // err -= Vector3(arcY, mSwipeExtentY, dx); err.y /= sZErrorScale;
        // error += LengthSquared(err);` reads 100 normalized / 99.68 fuzzy and
        // makes GetSwipeAmount byte-identical, but native (clang -O2, IEEE)
        // then really computes (dx - dx)^2 -- NaN for inf/NaN dx where the
        // image adds 0.  The image never computes dx - dx; we do not ship a
        // decompilation-introduced NaN path for a match point.  Every literal-
        // zero spelling (0.0f sides, zero local, err.z = 0, Subtract, direct
        // ctor, named arcPt, Zero() then -=) folds 0*0 at compile time (91.61
        // fuzzy) or is worse (68.2-88.3); finite-safe non-literal zeros (int
        // mSide, mSwipeExtentX on both sides) read 84.8 / 86.0.  Also inert
        // (w12-d): zero-initialised `Vector3 err(0, 0, 0)` + LengthSquared.
```


## `?GetSwipeAmount@ArcDetector@@QBAMXZ`

`ArcDetector::GetSwipeAmount` -- moved verbatim from `src/system/gesture/ArcDetector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 267-275):

```text
    // RESIDUAL (w12-d, canonical 100 modulo register permutation): `powered`
    // lives in f2 in the image and f3 here across the GetPathError() call.
    // MSVC picks a volatile FPR the same-TU callee does not touch, so this row
    // follows GetPathError's own register use (that function is 93.5), not
    // anything in this body.  CONFIRMED (w21-n): GetPathError's one-error-vector
    // spelling (not used, see there) makes this function byte-identical (f2).
    // w26-f13 re-measured the local side: a named `double` for the pow result,
    // a named copy of `powered`, and naming the two pow arguments are all
    // inert (the last one costs 14.4 fuzzy, so it is not a candidate either).
```


## `?IsPathAcceptable@ArcDetector@@ABA_NXZ`

`ArcDetector::IsPathAcceptable` -- moved verbatim from `src/system/gesture/ArcDetector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 349-365):

```text
        // w26-f re-measured: still fuzzy 99.0 (canonical 100.0), and 8 of the
        // 12 diff_arg rows are the f0<->f12 dx/dy swap.  Image: dx f0, dy f12,
        // invDy f13.  Ours: dy f0, dx f12, invDy f0.  The tap reproduces the
        // w25-gf reading exactly -- invDy is pri 13 tie 74 while dx (tie 30)
        // and dy (tie 27) are both pri 11 -- so the image's order needs dx and
        // dy lifted above a value referenced in blocks they are not.  Measured
        // inert with nsearch (canonical 100.0 held): `diff.x * sign` for
        // `sign * diff.x`, dx declared before dy/diffZ, and a redundant
        // `invDy * 1.0f`.  Measured WORSE: const-reference dy/diffZ (fuzzy
        // 98.944) and folding diffZ away (98.944); folding dy away drops
        // canonical to 93.33.
        // w25-gf open FP rows: invDy (pri 13 tie 74) pops before dx and dy
        // (pri 11), takes f0 and pushes dx to f12. The image order needs dx
        // first: dx > invDy and dx > dy, e.g. two more FP candidates
        // referenced in the entry block (dx +4, dy +2, invDy +0, pressure is
        // per class). dx/dy, (1/dy)*x, named threshold/slopes, split ifs,
        // dropping the dy/diffZ/dx locals: keys unchanged or code changed.
```


## `?Update@ArcDetector@@QAAXABVSkeleton@@H@Z`

`ArcDetector::Update` -- moved verbatim from `src/system/gesture/ArcDetector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 404-443):

```text
// w-arc-update (2026-11-10, LOSE at 98.74779 fuzzy / 99.07964 canonical / 209
// equal / 16 diff_arg / 1 insert / 1 delete, score 283): the insert+delete pair
// is NOT an independent producer.  It is the alignment artifact of ONE scheduler
// slot.  Target rows 143-149 emit `fsubs dy`, `fmuls dy*dy`, `fsubs dz`,
// `fsubs dx`, fmadds, fmadds; ours emit `fsubs dy`, `fsubs dz`, `fmuls dy*dy`,
// `fsubs dx`, fmadds, fmadds.  What objdiff charges as insert+delete is dz's
// fsubs re-aligning past the sunk fmuls.  The dependency graph is provably
// identical: same 6-instruction multiset, same operands (dy = f30 - *, dz = f29
// - *, dx = f31 - *), and both accumulate dz^2 + dy^2 then dx^2 (verified by
// reading the fmadds operands, not assumed).  All 15 register swaps live inside
// rows 140-149, i.e. downstream of that phase (f11/f12/f13 are a 3-cycle); the
// 4-word frontPt copy at rows 132-139 is the same cyclic phase in GPRs -- target
// w,y,x,z vs ours z,x,w,y with identical r9/r8/r7/r11 and identical src->dst
// pairs, only rotated.  Vector3 is 12 bytes, so the 4th word is MSVC's 16-byte
// block-copy rounding: the copy order is a scheduler choice, not a type artifact.
// Stack layout is NOT a lever -- 9/9 user slots MATCH, frame 0xe0 on both sides,
// 5 callee-saved GPRs + 3 FPRs on both sides.
// Measured this round (name_check, worktree w-arc-update; 227 insns / 904 B both
// sides unless noted).  equal is 209 in every row but the first:
//   statement order, frontPt before mArcOffset (legal -- GetCurveStart is const)
//     fuzzy 85.24336 / score 3335 / equal 186, 17 diff_arg, 2 replace,
//     10 insert, 21 delete, base 860 B  -- breaks the upstream schedule too
//   float distSq = distY*distY; distSq += distZ*distZ; distSq += distX*distX;
//     if (distSq > 0.0001f)
//     fuzzy 98.71681 / 290 / 209 eq / 16 da / 1 ins / 1 del (adds a 0x74<->0x78
//     lfs offset swap)
//   Vector3 frontPt = mJointPath.front();   [INERT]
//     fuzzy 98.74779 / 283 / 209 eq / 16 da / 1 ins / 1 del, 18-row list
//     identical to baseline
//   front() plus a named `float distYSq = distY*distY;` after the three dists
//     fuzzy 98.71681 / 290 / 209 eq / 16 da / 1 ins / 1 del
//   fold distX/distZ away, squares read frontPt directly in the condition
//     fuzzy 98.69026 / 296 / 209 eq / 16 da / 1 ins / 1 del (+5 rows of extra
//     register/offset noise)
// Mechanism: both sides give MSVC an identical dependency graph for this block,
// so the residual is a tie-break in the scheduler's ready list at the
// dy*dy / dz-fsubs pair; no spelling that preserves the block's inputs moves it.
// w21-n / w21-ao / w26-q already covered 84 copy spellings and 16 base-reference
// variants on the same reading.  Give up unless MSVC's 16-byte copy order (or
// this scheduler phase) becomes understood; do not re-spell the expression.
```

Inside the body (was lines 484-523):

```text
            // w21-n: push_front, not insert(begin(), ...) -- one inline level
            // deeper.  The image reloads the head node (`lwz r11, 0x0(r30)`)
            // right before the insert because push_front forms its own
            // begin(); with the spelled-out begin() MSVC CSE'd it with the
            // frontPt copy's head load and kept r11 live (the w7-an cluster
            // (1)).  97.18 -> 98.72 fuzzy; distX, distY, distZ declared in
            // that order then gives the image's z, x, y float loads (98.75).
            // STOP (w21-n, 98.75 fuzzy, 18 rows): the 16-byte frontPt copy's
            // load/store order (image w,y,x,z; ours z,x,w,y) and the image's
            // `fmuls` of distY straight after its fsubs, with f11/f12/f13
            // permuted.  Inert/worse here: copy-ctor, assignment, outer-scope
            // frontPt, iterator local (all 98.75), a `const Vector3 &` then
            // copy (96.82), Vector3 dist + LengthSquared (98.72),
            // DistanceSquared(boneVec, frontPt) (98.53), the y,z,x / z,x,y
            // declaration orders (98.69).
            // w21-ao (99.08 canonical / 98.75 fuzzy, same 18 rows): values
            // re-checked -- the image sums x + (z + y) with distY squared first,
            // exactly as ours; only the frontPt copy order and the fsubs
            // registers differ.  Probe-measured (real cl.exe flags, 84
            // spellings): frontPt via ctor, assignment, iterator,
            // const_iterator, front(), a const& alias, x 6 declaration orders
            // x 5 sum groupings -- the copy order (image w,y,x,z) never moves.
            // w26-q (98.74779 fuzzy / 99.07964 canonical, 18 rows): the 16-byte
            // `frontPt` copy is rows 132-139 as a load/store ORDER -- the image
            // runs w,y,x,z and we run z,x,w,y, every src-dst pair being the same
            // address -- and rows 140-149 are the squared-sum FPR permutation
            // plus the image folding its second `fsubs` in place. Value-checked
            // against the target: both sides square the same three components
            // with the same f29/f30/f31 operands and accumulate
            // (0x70^2 + (0x78^2 + 0x74^2)); only the emission order differs, so
            // no behaviour differs. 16 variants of the C2RS-BRIDGE 8.8
            // base-reference lever this round -- `const PaddedJointPos &` bound
            // straight off TrackedJoints()[mSecondaryJoint].mJointPos[0] (the
            // form TryToStartSwipe uses), the same with the TrackedJoint
            // reference kept, the arm* and mSwipeExtentY reads bound the same
            // way, and a dz/dx/dy declaration order -- all tie the replay
            // baseline exactly (99.0354 canonical there; the replay sits 0.044
            // under report.json only because its __FILE__ is an absolute path),
            // so this is not a commutative-key row.
            // Open handle: what sets the order of MSVC's 16-byte struct copy.
```


## `?UpdateOverlay@ArcDetector@@QAAMPAVRndOverlay@@M@Z`

`ArcDetector::UpdateOverlay` -- moved verbatim from `src/system/gesture/ArcDetector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 548-567):

```text
// 100% canonical (w7-bm, 2026-09-15; was 95.5).  The 2026-09-14 diagnosis of
// "two extra 16-byte Hmx::Color temporaries" was wrong: both sides allocate
// exactly 15 by-reference Color slots.  The +0x20 of frame was one extra
// Vector2 temporary plus two address-taken float HOMES, and every remaining row
// was argument-temporary shape.  What the image encodes, and how it is spelled:
//   - MakeString's const float& binds to a TEMPORARY for dx and arcY (a single
//     `stfs` into the 0x50 call scratch right before the call, never a home
//     store at the definitions), so those arguments are prvalues: `+dx`, `+arcY`.
//     `+zd` is worse -- zd's copy already sits at 0x70 on both sides.
//   - Every Vector2 whose stores precede the Color stores of its own call is a
//     NAMED local declared just before the call (arcPt, heightPt/basePt in that
//     order, curPt, curHeightPt); curPt is passed to both the circle and the
//     line, which is why the image reuses one slot (0xa0) for both.
//   - No `const TrackedJoint *joints` base local: indexing TrackedJoints()
//     directly leaves the two dead `addi r9, rN, 0x4` at 82E021DC, as in
//     TryToStartSwipe / Update.
//   - The else-branch copy of `front` reads z, y, x in that order.
// Each lever alone measured BELOW 95.5 (93.6-95.0) because the static
// DebugMeter Color-copy schedule (~82E024xx) reshuffles under any upstream
// change; only the full set lands it.  Do not evaluate these one at a time.
```
