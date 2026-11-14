# Lane notes: `src/system/char/CharEyes.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/char/CharEyes.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

File-level (was lines 34-42; nearest following definition: `pow`):

```text
// w15-r: the image's TU .rdata is [30.0f, 3.0f, 1.0f (0x820108E8..F0), gRev,
// gAltRev] -- three named file-scope consts, none loaded by name (every use
// folds).  30 is the eye-cone half-angle: the ctor's cos() argument
// 0x3FE0C15236000000 is exactly 30.0f * DEG2RAD multiplied in double (measured:
// the ctor is unchanged; the explicit (double) keeps the native build's cos()
// argument identical to the old literal instead of a float-rounded product).  3 and 1 are Poll's default look times: as named consts
// the `: 1.0f` default is no longer CSE'd with Clamp's 1.0f (Poll 98.01 ->
// 98.95, the four "real rows" below).  With gRev off offset 0 MSVC anchors
// Load's ASSERT_REVS pair on gAltRev like the image (99.29 -> 100).
```

File-level (was lines 1736-1769; no definition follows it):

```text
// w8-c: `merged_8237A7E8` (84 B) holds at 0.0000 and is NOT work.
// Same class as CharSignalApplier's merged_823AAA20: an unscoreable ICF
// pairing artifact, not a missing body.
//   - config/373307D9/symbols.txt:120181 binds the address with a synthetic
//     placeholder: `merged_8237A7E8 = .text:0x8237A7E8; // type:function
//     size:0x54 scope:global`, so the target-side COMDAT carries no mangled
//     name and nothing on our side can pair with it.
//   - build/373307D9/icf_aliases.map:11008-11011 names the fold group:
//     0x8237A7E8 = `_Copy_Construct<CharEyes::CharInterestState>` +
//     `_Param_Construct<CharEyes::CharInterestState>`. The two stlport
//     helpers have identical machine code, so /Gy folded them.
//   - We DO emit both real bodies. `strings build/373307D9/src/system/char/
//     CharEyes.obj` shows
//     `??$_Copy_Construct@UCharInterestState@CharEyes@@@stlpmtx_std@@YAXPAU
//      CharInterestState@CharEyes@@ABU12@@Z` and
//     `??$_Param_Construct@UCharInterestState@CharEyes@@U12@@stlpmtx_std@@
//      YAXPAUCharInterestState@CharEyes@@ABU12@@Z`.
//   - Target body: CharEyes.s:9856-9880. It is the placement copy-construct
//     of CharInterestState -- `bl "??0?$ObjOwnerPtr@VCharInterest@@@@QAA@ABV0
//     @@Z"` followed by `lfs f0, "__real@bf800000"@l(r11)` / `stfs f0,
//     0x14(r30)` (the -1.0f initialiser for the state's timer field), which
//     is exactly what our CharInterestState copy ctor compiles to. Called at
//     CharEyes.s:11179 and CharEyes.s:11239.
//   - No source lever exists FOR THE ROW: adding, moving or explicitly
//     instantiating the helper changes nothing, because the failure is in the
//     target-side NAME, not in whether we emit the code. Closing it requires
//     renaming the address in config/373307D9/symbols.txt.
//   - But reading it was NOT wasted: the `stfs f0, 0x14(r30)` of -1.0f at
//     0x8237A820 is a STORE, not a copy of 0x14(r4), which proved the image's
//     CharInterestState copy ctor RESETS mRefractoryTime. We had no
//     user-declared copy ctor, so the implicit memberwise one copied it. Fixed
//     in CharEyes.h; the fix crossed
//     `?_M_fill_insert_aux@?$vector@UCharInterestState@CharEyes@@...`
//     (444 B) from 99.0991 to 100.0000.
```


## `?Enter@CharEyes@@UAAXXZ`

`CharEyes::Enter` -- moved verbatim from `src/system/char/CharEyes.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 81-89):

```text
    // w7-by (87.35955 -> 100.0, 0 mismatch rows): the statement order is RB3's
    // CharEyes::Enter member for member (mBlinkActive is RB3's mTargetTooClose,
    // the lone byte store right above the filter-flags copy at 0x823772A0),
    // and the flags copy is the inline ClearInterestFilterFlags() call.  The
    // inlined call is a scheduling barrier: spelled as a plain member
    // assignment MSVC hoists `lwz r10, 0x60(r31)` above every store, where
    // the image loads it at 0x823772B8, immediately before the `stw` at
    // 0x823772BC.  That hoist is what made every mBlinkActive placement read
    // as a loss (w7-ap measured 85.7 for exactly this order without it).
```


## `?EnforceMinimumTargetDistance@CharEyes@@IAAXABVVector3@@0AAV2@@Z`

`CharEyes::EnforceMinimumTargetDistance` -- moved verbatim from `src/system/char/CharEyes.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 747-755):

```text
// w13-o: 99.87 -> 100.0 (fuzzy 97.46 -> 100.0). The difference vector is built
// with the three-float constructor -- the same idiom GenerateDartOffset uses
// just below -- not with Subtract(). The y, z, x component order the image
// shows is NOT a property of math/Vec.h's Length/Subtract bodies: the flat sum
// in Length is canonicalised by /fp:fast (any written term order is
// byte-identical), and RndFlare::CalcScale, whose source is RB3's verbatim, is
// at 100 with the current Length. What moves the order is how the operand
// vector was produced. Measured inert or worse here: Distance(v2, v1),
// `diff = v2; diff -= v1` (55.5), moving the mBlinkActive store above Length.
```


## `?NextLook@CharEyes@@IAAXXZ`

`CharEyes::NextLook` -- moved verbatim from `src/system/char/CharEyes.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 892-1006):

```text
        // NEGATIVE RESULT -- the one unexplained STRUCTURE left in this function.
        // The image makes a SECOND 16-byte Vector3 local here, a copy of
        // facingDir: `addi r10, r31, 0x70` (facingDir) / four `lwz` off r10 /
        // `addi r11, r31, 0x60` / four `stw` off r11, interleaved with the three
        // `facingDir - mLastFacing` fsubs below (CharEyes.s, the ten
        // instructions at diff idx 91-105).  It then reads the extrapolated
        // facing's y and z back out of THAT copy after the tan() call --
        // `lfs f0, 0x64(r31)` / `lfs f13, 0x68(r31)` -- while taking x from the
        // register f27 that still holds facingDir.x.  That reload is why the
        // image only saves f26-f31 where we save f24-f31: we keep all three
        // facingDir components pinned in callee-saved FPRs across tan() and
        // RandomFloat() instead.
        //
        // Spelling it as `Vector3 newFacing = facingDir;` and reading the three
        // components out of `newFacing` is BYTE-FOR-BYTE INERT: MSVC copy-
        // propagates the local-to-local copy away (510 instructions, 91.0
        // canonical, identical row set), even though facingDir's address has
        // already escaped into Normalize().  Whatever the image is copying from
        // is reached through a pointer MSVC cannot see through -- note that both
        // sides of that copy use computed address registers, where the other two
        // Vector3 copies in this function (oldTarget at 0x90, facingDir at 0x70)
        // use a computed address only for the destination.
        //
        // w7-ay addendum (2026-09-14, at 96.7 canonical): `memcpy(&newFacing,
        // &facingDir, sizeof)` with y/z read from the copy is ALSO inert (same
        // 76/4/1/13 rows), and so is the plain copy re-tested on top of the
        // levers below.  The residual is exactly this copy (`addi r11, r31,
        // 0x60` at 0x823799FC, reloads at 0x82379A6C/0x82379A74), the f26-vs-
        // f24 save (`bl __savefpr_26` at 0x8237989C) and the frame it implies
        // (`stwu r1, -0x130(r1)` at 0x823798A4 vs our -0x140), plus register
        // naming.  What moved 91.0 -> 96.7 was spelling, not structure:
        // `Set(...)` for the two projected targets (loads all three sources
        // before any store, as the image does), `mData = dartOverride->mData`
        // instead of memcpy (no pre-hoisted `addi &mData` above the null test),
        // the direct-member tail copies `mHeadForward = mTarget; mTarget =
        // oldTarget` (a reference there made the copy word-serialised), a named
        // `headPos` reference so the loop loads go through the pointer register,
        // and a Transform reference for Dir()'s WorldXfm().
        //
        // w7-bn (96.7 -> 99.6): FOUND.  The copy is `newFacing`, and what keeps
        // MSVC from propagating it away is a MEMBER operator on it that returns
        // `*this` -- `newFacing += Vector3(dx, dy, dz)`.  With that spelling the
        // 16-byte copy appears at 0x823799F8..0x82379A30, the `&mLastFacing` home
        // store sinks to 0x82379A18, y/z are reloaded from the copy at
        // 0x82379A6C/0x82379A74, and the prologue drops to `bl __savefpr_26` /
        // `stwu r1, -0x130(r1)` exactly.  The same copy is ELIMINATED (96.7,
        // full ninja each) by every spelling that reads or writes the fields
        // directly: `newFacing.x += dx` (93.5), `newFacing.Set(newFacing.x + dx,
        // dy + newFacing.y, dz + newFacing.z)`, `Add(Vector3(dx, dy, dz),
        // newFacing, newFacing)`.  A modified copy for the DELTA instead
        // (`Vector3 extrap = facingDir; extrap -= lastFacing; extrap *= 45.0f`)
        // keeps a copy but puts the fsubs on it and the post-tan reloads on
        // facingDir, the mirror image of the target (95.5).
        //
        // Residual at 99.6: eight commutative operand orders (`fmuls f30, f30,
        // f0` vs ours `f30, f0, f30` at 0x82379A60/64, the post-tan y/z fadds,
        // and the projection/Set rows at 0x82379AB0..0x82379AD0) that source
        // operand order does NOT drive -- `dy = scale * dy` vs `dy = dy * scale`
        // is inert on them -- plus the tail's oldDir load order (the image loads
        // oldTarget x/y/z before headXfm.v at 0x82379F4C; `Subtract(oldTarget,
        // headXfm.v, oldDir)` homes a reference instead, 98.0) and the
        // `mTarget.z < dirXfm.v.z` load order (`dirXfm.v.z > mTarget.z` flips
        // the branch to ble, 98.0).
        // w21-ai (99.57, 62 rows, 60 of them register permutation r22/r23/r24
        // = headXfm/zero/guard-base in the image vs zero/guard/headXfm ours):
        // RB3's `Vector3 extrap(...)` with member updates and `newFacing +=
        // extrap` (99.2, adds two load/fsubs rows); `const Vector3 &headPos`
        // bound at function top (99.2).  The (0x4,0x8) load-order rows at the
        // first `mCurrentInterest = 0` (bestState == 0 arm) are the inlined
        // ObjRef::Release unlink; the second, identical `mCurrentInterest = 0`
        // already matches, so it is the surrounding allocation, not the source.
        // w26-i (99.55 canonical, 62 rows): every row was re-read against the
        // target and none of them is a different immediate, call or field.  The
        // lbl_82F5E80C rows at idx 183/188/374/391 are the SAME address as our
        // ?$S6@?BF@??NextLook guard -- they cost only because a register rides
        // along with the placeholder name, and objdiff forgives the name alone
        // (cf. idx 195, where only the name differs and nothing is charged).
        // 18 declaration-order variants (dx/dy/dz, projX/projY/projZ,
        // sx/sy/sz) and the 8.7 named-vs-inline spellings of the scale product
        // are inert on canonical.  The dominant r22/r23/r24 permutation
        // (headXfm base, the __real@00000000 constant, the guard base) is the
        // tie-0 hoisted-address shape of C2RS-BRIDGE 8.4, where the source
        // lever is the code band's C-F count (how many demotions precede the
        // band), not anything spelled here.
        //
        // w27-l (2026-11-02, 99.57085 canonical / 98.72065 fuzzy, 495 rows,
        // 62 mismatch rows): re-adjudicated against the listing; NOT a
        // behaviour difference. There is no opcode change, no immediate change,
        // no different callee and no branch polarity change anywhere in the 62
        // rows. What they are:
        //   * 8 pure relocation names -- `lbl_82F5E80C` is our
        //     `?$S6@?BF@??NextLook` guard, `lbl_82F5E808`/`lbl_82F5E804` are
        //     `interestCheat`/`blinkCheat`; those three addresses carry no name
        //     in config/373307D9/symbols.txt, and CopyRef/SetObjConcrete over
        //     FlowLabel/SpotlightDrawer vs CharInterest are ICF aliases
        //     (0x823EFAA0 and 0x8283F540 in icf_aliases.map). Nothing in source
        //     can move a name the target does not have.
        //   * the r22<->r24 rotation (head base) plus the commutative operand
        //     orders that ride it -- `fmuls f30,f30,f0` vs `f30,f0,f30`, the
        //     post-tan fadds, the projection/Set rows.
        //   * one DISPLACED instruction only: `fsubs f0, f0, f31` (idx 437/439).
        // Re-derived by hand because a row-aligned read can invent a field swap
        // (w27-h): target idx 163-173 is `mTarget.z < dirXfm.v.z` compared with
        // the two loads assigned the other way round, and both sides' `fdivs`
        // is (dirXfm.v.z - headXfm.v.z) / (mTarget.z - headXfm.v.z). Same
        // operands, same result.
        // MEASURED AND REJECTED (C2RS-BRIDGE 8.4 free-count lever): binding one
        // more named pointer/reference to `dirTrans` INSIDE the `if (dirTrans)`
        // block, before the WorldXfm() call, moves fuzzy 98.72065 -> 98.74089
        // (canonical unchanged at 99.57085, 1976 B unchanged) -- reproducibly,
        // for both `RndTransformable *dirT = dirTrans;` and
        // `RndTransformable &dirT = *dirTrans;`. The same binding placed one
        // statement earlier, outside the null test, is inert, so it is a
        // scheduling/priority shift and nothing else. It is a redundant alias
        // that no original source would contain; not landed.
```

Inside the body (was lines 1113-1121):

```text
                // w20-o: no clear here.  When interests exist but every one sits
                // at the head (maxDistSq <= 0), the image keeps mCurrentInterest
                // and mData: `ble cr6, 0x4234` (in-function, after the distance
                // loop) and the loop-skip `beq cr6, 0x4234` both land on the
                // state-reset stores (`lis r10, __real@7149f2ca@h` ...), past the
                // `mCurrentInterest = 0; ClearToDefaults` block at 0x4200, which
                // only the size()/sDisableInterestObjects/cheat failures reach
                // (`beq/bne 0x4200` x3).  The earlier `goto stateReset` spelling
                // (also RB3's) cleared on that path.
```


## `?LidTrackAndClampingUpdate@CharEyes@@IAAXAAUEyeDesc@1@M@Z`

`CharEyes::LidTrackAndClampingUpdate` -- moved verbatim from `src/system/char/CharEyes.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 1169-1231):

```text
// RESIDUAL at 98.35 canonical (w7-at, 2026-09-14). ~100 of the 111 diff_arg rows
// are ONE frame-slot permutation, not 100 causes. Both sides allocate the same eight
// 16-byte slots at 0x50..0xc0 and the same sharing groups; only the order differs:
//   target  0x50 srcPos  0x60 lidPos  0x70 Symbol-temp  0x80 upperDir
//           0x90 upperBlinkPos  0xa0 sourcePos  0xb0 lowerBlinkPos  0xc0 lowerDir
//   ours    0x50 srcPos  0x60 Symbol-temp  0x70 upperDir  0x80 lidPos
//           0x90 lowerDir  0xa0 upperBlinkPos  0xb0 sourcePos  0xc0 lowerBlinkPos
// i.e. exactly two moves: lidPos up two places, lowerDir to the top. The relative
// order of the (lowerBlinkPos, sourcePos, upperBlinkPos) trio ALREADY matches. The
// slots are shared with later variables (newLowerPos / origDir / newDir / the debug
// Color temps), so this is a graph-colouring result, not a declaration-order one.
//
// NEGATIVE RESULTS, all measured in this worktree, none of which moved canonical:
//  - `Vector3 lidPos;` as a bare declaration ahead of srcPos, assigned in place:
//    98.3 -> 98.3 (and two extra rows at idx 644-650). Confirms the brief's "a bare
//    declaration claims its slot at first STORE" -- it does not claim it earlier.
//  - flipping the fmuls operand order in BOTH lid-rotate arms
//    (`negEyeRot * (cond ? up : down)`): byte-identical output, MSVC canonicalises it.
//  - RB3's per-arm spelling (`if (eyeRot >= 0) angle = -eyeRot * up; else ...`):
//    98.3 -> 98.0, one extra insert/delete pair and a second FPR swap pair. The
//    current scoped-negation spelling is the better of the two.
// Remaining non-slot rows: idx 100/102 (our `fneg` lands before the `fcmpu`, the
// image's between the `fcmpu` and the `blt`, with f0/f13 roles swapped), idx 179-209
// (we hoist `lbz 0xbd(rN)` -- RndTransformable's WorldXfm dirty flag -- ~10
// instructions earlier than the image in both blink-position blocks), and idx 627-650
// (a 16-byte Transform copy whose three word loads/stores are scheduled in a rotated
// order; the "wrong field" story in the resolved-offsets block is the base-register
// defect noted in CLAUDE.md, not a finding).
// w21-j: 98.35 -> 99.6. The upper/lower blink directions are Subtract() calls:
// the image re-tests the next WorldXfm dirty byte (`lbz 0xbd`) only AFTER each
// 16-byte position copy, i.e. the positions are address-taken; the field-by-
// field spelling let MSVC hoist that lbz ~10 instructions (the idx 179-209 rows
// above are gone). The stack-slot permutation remains (~95 offset rows; now
// 0x60/0x70 lidPos<->Symbol-temp and lowerDir/upperBlinkPos/sourcePos/
// lowerBlinkPos one slot off). REFUTED this pass: Distance(lidPos, srcPos) for
// the lid distance (inert/one row worse), declaring lowerDir before upperDir
// (inert), Dot(srcXfm.m.x, cross) for notLidsOK (4 rows worse).
// w21-ai (99.57, same rows): `Vector3 srcPos, lidPos` declared once at function
// scope and reused by the final llidnorm block grows the frame by 0x20 (99.4);
// `(cond ? up : down) * -eyeRot` without the negEyeRot local lets /fp:fast fold
// it into fneg(sel * eyeRot) and moves the slot set (99.3).
// w26-i (99.5686 after the reorder below; 99.5656 before): a 72-variant
// declaration-order sweep through the c2-rs tap (the 6 permutations of
// dx/dy/dz x the 6 of sourcePos/upperBlinkPos/lowerBlinkPos x two associations
// of the sqrt sum) closes two rows and nothing more -- every other order scores
// 95.2-99.4, so the liveness-ordered slot map is what is left (~95 offset
// rows).  The shipped `dz; dy; dx;` order is the measured best of the 72: it
// moves the two 16-byte Vector3 temps at idx 71-83 (lfs 0x50/0x54/0x58 vs
// 0x58/0x54/0x50).  Hoisting the two dist-block Vector3 declarations (or the
// three blink-position ones) to function scope while leaving the assignments
// where they are measured byte-identical at the pre-reorder baseline: MSVC
// assigns slots by liveness, not by declaration position.
//
// w32-b: the whole residual is an EQUIVALENCE CLASS, measured not argued.
// Both sides are 662 instructions; with registers, stack displacements and
// branch targets abstracted, the opcode/shape/constant multisets are
// IDENTICAL.  So every `diff_arg` row is one of: which physical FPR carries a
// value, the operand order of a commutative float op, or a stack
// displacement.  No wrong constant, wrong field, wrong callee, or missing
// instruction -- no behaviour hangs on any of the 99 rows.  What is left is
// c2 first-fit FPR assignment, the commutative sort key (C2RS-BRIDGE 8.7),
// and MSVC's liveness-ordered frame slots; the 72-variant declaration sweep
// above is the measurement that the slot map has no source handle left.
```


## `?Poll@CharEyes@@UAAXXZ`

`CharEyes::Poll` -- moved verbatim from `src/system/char/CharEyes.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 1491-1520):

```text
// w15-r: 98.01 -> 98.95 -- the first bullet below (the 1.0f CSE) is closed by the
// named kDefaultMinLookTime/kDefaultMaxLookTime consts (see INIT_REVS).
// RESIDUAL at 98.0 canonical / 97.1 raw (w7-ax, 2026-09-14).  53 of the 63
// remaining rows are one register permutation the canonical ruler forgives.
// The four real rows, each verified against build/373307D9/asm/system/char/CharEyes.s:
//  - We CSE the `1.0f` of `Clamp(-1.0f, 1.0f, cang)` into the callee-saved f31
//    and reuse it as the `: 1.0f` default of minLookTime; the image keeps only
//    the literal-pool ANCHOR in a callee-saved GPR (`lis r30,
//    "__real@3f800000"@ha` at 0x8237AB74) and issues a SECOND
//    `lfs f30, "__real@3f800000"@l(r30)` at 0x8237ABFC for the default.
//    Because our else-arm is then empty, MSVC fuses the minLookTime and
//    maxLookTime diamonds into one, costing the image's `b .L_8237AC00`
//    (0x8237ABF8), that `lfs`, and the second `cmpwi cr6, r10, 0x0` / `beq cr6`
//    pair at 0x8237AC00/0x8237AC04 -- 4 rows.
//  - `addi r24, r3, 0x8` (diff idx 8): we pin &mEyes in a callee-saved GPR for
//    the whole function; the image re-derives it, as `r25 + 0x30` off the
//    CharEyes* at the two interior sites and as `r31 + 0x8` at the final loop,
//    and pays one extra `addi r11, r25, 0x30` (0x8237AE38) before re-reading
//    begin() for mEyes[0].
//  - The cam fallback chain: the image's first null test is SIGNED
//    (`cmpwi cr6, r30, 0x0` at 0x8237AE90) and its second uses cr6; we emit
//    `cmplwi cr6` and then `cmplwi r30, 0x0` on cr0.  Control flow is identical.
// NEGATIVE RESULTS, all measured in this worktree, none of which moved canonical:
//  - `float cang = Clamp(-1.0f, 1.0f, Dot(facingDir, targetDir));` as one
//    statement: byte-inert.
//  - minLookTime as an if/else statement pair instead of a ternary: inert.
//  - `mEyes.begin()->mEye` instead of `mEyes[0].mEye` (both sites): inert.
//  - `RndCam *cam = TheWorld ? TheWorld->Cam() : 0;` instead of the zero-init +
//    `if (TheWorld)` block: 98.0 -> 97.2 (three extra inserts, one extra
//    delete, and it moves the `lwz 0x0(r11)` reload onto the wrong register).
```

Inside the body (was lines 1572-1582):

```text
    // w25-gx (c2rs tap, C2RS-BRIDGE 8.7): the one residual row is the x term
    // of this inlined Dot, image `fmadds f12, f11, f10` (facing.x first), ours
    // targetDir.x first. The product's operands are variable leaves keyed by
    // sid: facing.x sid 617 < targetDir.x sid 621 (the y/z fields, sids
    // 756..759, already sort facing first). Inert (~20 variants): Dot argument
    // order, declaration order of the two vectors, facingDir by assignment,
    // renaming, a named dot / inline Clamp (move only the y/z group), and a
    // dummy local anywhere (shifts every sid by one). Hypothesis: the x fields
    // are numbered in the overflow-local group (headXfm 614 .. cang 624) by a
    // rule other than declaration position; the image needs facing.x numbered
    // after targetDir.x. Open: what orders that group.
```

Inside the body (was lines 1643-1661):

```text
    // w19-c (98.95 -> 99.58): GetTarget() and EitherEyeClamped() as CALLS, as
    // RB3 writes them.  Inlined, their member reads go through the FULL object
    // pointer (`0x30(r25)`), where the open-coded versions read through the
    // CharPollable subobject Poll() is entered on and cost a callee-saved
    // register for &mEyes.  Left: one commutative fmadds, and the
    // TheWorld->Cam() test is a signed `cmpwi` in the image (ours `cmplwi`;
    // RB3's do/while(false) camera chain is byte-inert).
    // w21-l (value-scan row, ARTIFACT): the image's non-null Cam() branch lands
    // on the redundant `cmplwi r30,0` before the TheRnd fallback; same cam value.
    // w21-ai (99.58 -> 100 modulo one row): the camera chain is rb3-xenon's
    // `if (TheWorld && TheWorld->Cam()) cam = TheWorld->Cam(); else if ...`.
    // Testing Cam() in the condition and re-reading it gives the image's signed
    // `cmpwi cr6, r30, 0` and its unthreaded branch onto the final null test;
    // the `cam = 0; if (TheWorld) cam = Cam(); if (!cam) ...` chain (also with
    // the two fallbacks nested) threaded that branch straight to the use.
    // Same behaviour: Cam() is a plain member read.  Remaining: the Dot()
    // x-term `fmadds` operand order (image fx*tx, ours tx*fx); Dot(targetDir,
    // facingDir) is byte-inert, and open-coding the sum (image association
    // fy*ty + (fx*tx + fz*tz), or x-swapped) reorders the six loads instead.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.977, 1 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.

## `CharEyes::CharInterestState::CharInterestState()`

`CharEyes::CharInterestState::CharInterestState` -- moved verbatim from `src/system/char/CharEyes.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Attribution: no function named `CharEyes::CharInterestState::CharInterestState` in report.json (whole report (header)); filed under the source name

Above the definition (was lines 49-59):

```text
        // w8-c: the image's copy ctor RESETS mRefractoryTime rather than
        // copying it. Proof is the folded stlport helper at
        // CharEyes.s:9856-9880 (`merged_8237A7E8`, the ICF fold of
        // _Copy_Construct/_Param_Construct<CharInterestState>): after
        // `bl "??0?$ObjOwnerPtr@VCharInterest@@@@QAA@ABV0@@Z"` at 0x8237A814
        // it does `lfs f0, "__real@bf800000"@l(r11)` / `stfs f0, 0x14(r30)`
        // at 0x8237A81C-0x8237A820 -- a -1.0f STORE into mRefractoryTime
        // (offset 0x14), not a `lfs 0x14(r4)` / `stfs 0x14(r30)` copy. The
        // implicit copy ctor we relied on before copied the source's value,
        // so a std::vector growth of CharEyes::mInterests preserved every
        // interest's refractory countdown where the image clears it.
```

## `?NormalizeScale@@YAXABVVector3@@MAAV1@@Z`

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.583, 1 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.
