# Lane notes: `src/system/gesture/SkeletonUpdate.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?InsertFakeArmPos@SkeletonUpdate@@AAAXAAUSkeletonData@@@Z`

`SkeletonUpdate::InsertFakeArmPos` -- moved verbatim from `src/system/gesture/SkeletonUpdate.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 241-264):

```text
// w26-k (2026-11-02): mechanism for the 91.32 residual (61 rows, 604 B), from
// the c2 colour-allocator tap.  Unlike the FP rows in BlendVert/Poll, this
// function HAS FP colour candidates and they all share priority 9: id12
// tie 56 -> f0, id14 tie 51 -> f13, id16 tie 47 -> f12.  A second block of
// eight candidates ties at priority 6 with ties 145/110/104/71/68/65/59/5 and
// those take f0/f13/f12 in tie order.  So the f0<->f12 and f12<->f13 pairs are
// a TIE-KEY decision (the key is the candidate's program position at its last
// visit), which is the lever to move next: shift one use of a pri-6 value
// across a block boundary.  f10 and f9 do not appear in the candidate list at
// all, so the f0<->f10 and f10<->f9 rows are regasg first-fit over sched3 and
// have no key.  Behaviour re-read against the image: the offset-swap rows are
// the same padData mSticks[1][0] / mTriggers[] loads and the same
// mJointPositions stores, every arithmetic value equal; objdiff diff_op none;
// unicorn EQUIVALENT at harness h4.  No defect found.
// w47-a (2026-11-06): re-derived arm 1 value by value (0x1d4/0x1d8/0x1dc =
// elbowRight x/y/z, 0x1e4/0x1e8/0x1ec = wristRight x/y/z).  The BASE-ONLY
// opcode+displacement multiset is EMPTY: every store offset we emit exists in
// the image, so no joint is written to a wrong place.  The residual is exactly
// THREE instructions -- `lfs 0x1d8` and `lfs 0x1dc` (arm 1, where the image
// RELOADS elbow.y/.z and c2 forwards our stores instead; same values either
// way) and one dead `addi r11, r31, 0x1e4` in arm 3.  Direct reads at the
// wrist stores measure 88.13 (z,x,y) / 88.13 (z,y,x) / 85.99 (x,y,z), all
// worse; dropping the arm-3 `wrist` reference is inert; wrist-before-hand is
// 90.20.
```

Inside the body (was lines 269-285):

```text
        // w7-by (82.132454 -> 89.5): the w7-an note below called the image's
        // reloads of elbowRight.y/.z (lfs f12, 0x1d8/0x1dc(r31) at
        // 0x8242CEA4/0x8242CEB0) a forwarding floor.  They are a spelling: the
        // wrist stores go through a plain `PaddedJointPos &` local, which MSVC
        // treats as an opaque object -- stores through it block forwarding of
        // the later direct loads, and the materialised reference survives as
        // the image's dead `addi r11, r31, 0x1e4` at 0x8242CE84.  The elbow
        // stores stay direct so the shoulder loads still forward.
        // Measured, all canonical: HEAD's shoulderX/Y/Z + elbowRightX locals
        // and all-direct-no-locals are byte-identical (82.1); Vector3& refs
        // for all three joints 83.3; PaddedJointPos& for shoulder+elbow+wrist
        // 83.2; Vector3& wrist only 87.7; wrist stores in x,y,z order 85.7;
        // `wrist.Set(...)` 86.4; `hand = wrist = rightPos` chain 87.8 but
        // copies in the wrong order; Vector3 struct copies 75.8.
        // Residual at 89.5: the image forwards elbow.x and reloads y/z where
        // we forward z and reload x/y; sx/sy load order; arm-3 word order of
        // the two 16-byte copies (see below).
```

Inside the body (was lines 290-299):

```text
        // w21-f (89.45 -> 89.9 canonical): the three wrist values are read
        // into locals BEFORE the first store through `wrist`, as the image
        // reads elbow.y/x/z (0x1d8, forwarded x, 0x1dc) ahead of its first
        // wrist store at 0x1ec.  Storing wrist.z first (HEAD) made the later
        // elbow loads follow an opaque-reference store.  Remaining here: the
        // image still RELOADS elbow.y/z (lfs 0x1d8/0x1dc) and forwards only x;
        // we now forward all three.  Measured, worse: an `elbow` reference too
        // (85.1); wrist stores x,z,y (86.4); a function-scope `wrist` ref also
        // used by the trigger arm (82.4).  Flipping every `a + b` to `b + a`
        // in the trigger/left arms is byte-inert (MSVC canonicalises).
```

Inside the body (was lines 318-351):

```text
            // The image computes each component and stores it before touching
            // the next (fsubs -> stfs 0x58, fadds -> stfs 0x54, fnmsubs/fadds
            // -> stfs 0x50), so this is written as direct field assignment
            // rather than through rightZ/rightY/rightX locals.
            // NEGATIVE RESULT (w7-an, 2026-09-14): that rewrite, `x + -(e)`
            // vs `-(e) + x`, and flipping `elbow.y + unk5398` to
            // `unk5398 + elbow.y` are ALL byte-identical here (82.1, same
            // 52/6/10/13 rows).  MSVC normalises the temporaries away and
            // still folds `x + -(rt*0.5f - 0.1f)` to fmsubs+fsubs where the
            // image keeps fnmsubs+fadds, and still loads unk5398 before the
            // joint field.
            // w7-by: `x + (0.1f - rt * 0.5f)` DOES reproduce the image's
            // `fnmsubs f0, f13, f11, f0` at 0x8242CF98 (c - a*b is the fnmsubs
            // idiom; x + -(a*b - c) and x - (a*b - c) both canonicalise to
            // fmsubs+fsubs).  The unk5398 load order is still the image's.
            // w21-ak (89.861 -> 91.318 canonical): both temps are built with
            // PaddedJointPos::Set.  MSVC evaluates Set's arguments right to
            // left, which reproduces the left arm's y, z, x compute-and-store
            // order exactly (three field assignments in that order do not), and
            // is byte-identical to a probe 3-float ctor.  Here the image
            // computes z first; field assignments z, y, x reproduce that block
            // but lose more in the two copies below (91.19).  Remaining, 91.318:
            // (1) every `field + other` fadds has the joint field as the image's
            // FIRST operand and ours second -- flipping the spelling, float
            // locals for the other operand, and a `const Vector3 &` elbow are all
            // inert; only routing the other operand through a stored temp
            // (`Vector3 off(...)`, then `elbow.y + off.y`) flips it, at 88.5
            // overall; (2) word order of the 16-byte copies in arms 3 and 4;
            // (3) the arm-1 forwarding pattern: wrist stores x, y, z through the
            // `wrist` ref reproduce the image's forward-x / reload-y,z exactly
            // but emit the stores in source order (88.1).  Also measured worse:
            // `elbow` + `wrist` refs in arm 1 (83.1); `wrist` ref declared before
            // the Set (inert); wrist copied before hand (90.2); hand before wrist
            // in arm 4 (89.85).
```

Inside the body (was lines 358-367):

```text
            // RESIDUAL (w7-an, 82.1 canonical): both sides assign handRight
            // from the first materialised `addi rN, r1, 0x50` and wristRight
            // from the second -- same registers, same eight words -- but the
            // image schedules the two interleaved 16-byte copies in a
            // different word order (0x200, 0x1f8, 0x1f4, 0x1fc, ... vs our
            // 0x1f0, 0x200, 0x1f4, 0x1f8, ...).  That is backend scheduling,
            // not a source shape: 39 of the 81 rows are the r9/r10 pairing.
            // w21-f: a block-scoped `wrist` reference (the image materialises a
            // dead `addi r11, r31, 0x1e4` in this arm too) -- +0.1, the addi
            // itself still does not appear in ours.
```


## `?UpdateCallbacks@SkeletonUpdate@@AAAXXZ`

`SkeletonUpdate::UpdateCallbacks` -- moved verbatim from `src/system/gesture/SkeletonUpdate.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 426-431):

```text
        // w19-c (96.69 -> 98.89): a plain index loop (MSVC derives the old
        // hand-kept `revBit = 1 - i` counter itself, byte-identical), and the
        // offset built as a zero Vector3 BEFORE the side select with x
        // assigned after it: that is what lets the image store offset.y/.z
        // (0x74/0x78) ahead of the select.  Writing a hand-stepped
        // `j++, skel++` loop below as an index loop is worse (97.79).
```

Inside the body (was lines 484-499):

```text
    // w19-c: the image stores mHistory (0x8c) before mCameraInput (0x90); we
    // store them the other way round.  Inert: field stores in either order,
    // an aggregate initialiser, and the constructor (which closed PostUpdate).
    // w21-w (99.989, these 2 rows are all that is left): also inert -- a
    // `CameraInput *cam = mCameraInput;` local, array-decay arguments, the
    // ctor with a null camera then `data.mCameraInput = ...`, an explicit
    // iterator loop instead of FOREACH.
    // w25-mb: also inert -- static_cast<SkeletonHistory *>(this), a const
    // local, hoisting `cam` above the mSkeletonsRight loop (much worse: it
    // stays live across the loop and recolours 20 rows).  The camera store is
    // only promoted when its value is a load in the SAME block.  Mechanism
    // hypothesis (c2 P_DAG): both stores are equal-height leaves before the
    // FOREACH test, so the pick is the tuple-index tie key; the `this` ->
    // SkeletonHistory* argument (second base, hoisted to offset 0) is
    // materialised as its own copy tuple, which may land after the camera
    // load's store.  Open question: whether run 4 (sched0) re-breaks that tie.
```


## `?PostUpdate@SkeletonUpdate@@AAAXXZ`

`SkeletonUpdate::PostUpdate` -- moved verbatim from `src/system/gesture/SkeletonUpdate.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 564-569):

```text
    // w19-c (99.988 -> 100): built through SkeletonUpdateData's five-argument
    // constructor (Skeleton.h).  Field-by-field stores, in either order, or an
    // aggregate `= { ... }` initialiser issue the 0x68 (&mSkeletonFrame) and
    // 0x70 (mCameraInput) stores transposed; the constructor gives the image's
    // order.  (UpdateCallbacks above keeps its own 2-row transposition of the
    // 0x8c/0x90 pair either way.)
```

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/gesture/SkeletonUpdate.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

File-level (was lines 106-110; no definition follows it):

```text
    // w21-w: SIX slots, not two plus four pad ints.  UpdateCallbacks writes
    // all six (`stwu r9, 0x4(r8)` x6 from 0x5368 up to 0x537c) and every
    // SkeletonUpdateData consumer indexes it as a 6-entry slot array.  The old
    // [2]+pad shape had the same PPC size but under LP64 the six 8-byte writes
    // ran over mSkeletonTrackingIDs/unk5388/unk538c.
```
