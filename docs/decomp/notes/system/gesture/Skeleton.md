# Lane notes: `src/system/gesture/Skeleton.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/gesture/Skeleton.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

File-level (was lines 34-45; nearest following definition: `XMVector3Transform`):

```text
// FILE scope, not function-local scope: the image materialises ONE base
// (lbl_82F0BFD0 = sJointRemap) and reaches the other two tables by
// displacement off it -- `addi r8, r3, 0xa0` (0xa0 == sizeof sJointRemap)
// for sTrackingMap, `subi r7, r3, 0x4` for sJointTrackingMap -- which is
// only possible if the three live contiguously in one .rdata section.
// MSVC puts each FUNCTION-LOCAL static in its own COMDAT, so as locals they
// emitted three separate lis/addi pairs against three mangled
// `?sJointRemap@?1??Create@...` symbols and could never be folded.
// NEGATIVE RESULT (w7-an, 2026-09-14): merely REORDERING the three
// function-local statics to match the image's data order was byte-identical
// inert (71.9 both ways, same 58/14/19/25 row counts) -- the COMDAT split is
// what blocks the fold, not the order.  Moving them to file scope: 71.9 -> 74.0.
```


## `XMVector3Transform()`

`XMVector3Transform` -- moved verbatim from `src/system/gesture/Skeleton.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Attribution: no function named `XMVector3Transform` in report.json (unit); filed under the source name

Above the definition (was lines 60-128):

```text
// XNAMath's XMVector3Transform, inlined.  The image's splat order (Z, Y, X)
// and its multiply-add order (Z*r[2]+r[3], then Y*r[1], then X*r[0]) are that
// function's body verbatim, in both of Create's loops, which is why it is
// spelled as a helper here rather than open-coded twice.
// RESIDUAL (w7-an, 74.0 canonical): the image RELOADS all four mat.r[] rows
// inside each loop body -- `addi r5, r1, 0x90` / `lvx128 v63, r0, r5` and
// three more, re-executed every iteration in the joint loop and again at the
// top of the skeleton loop -- while MSVC hoists ours into the preheaders
// (`lvx128 v0/v13/v12/v11`).  That is ~22 of the 99 remaining rows.
// NEGATIVE RESULT (w7-an, 2026-09-14): neither passing the matrix by
// `const XMMATRIX &` (this spelling) nor by `const XMMATRIX *` through a
// named `const XMMATRIX *pmat = &mat;` blocks the hoist: all three spellings
// produce a BYTE-IDENTICAL diff (74.0 canonical, same 45/14/16/24 row
// counts).  MSVC fully forwards the inlined parameter back to the local, so
// the loads are provably invariant whatever the indirection.
// NEGATIVE RESULT (w7-ay, 2026-09-14, floor 74.0 held): two more spellings
// aimed at the same hoist were byte-for-byte inert (74.0, same 45/14/16/24
// rows): (1) `const XMMATRIX M` BY VALUE -- XNAMath's Xbox CXMMATRIX -- the
// 64-byte copy is elided and the loads still hoist; (2) reading the rows
// through the `__lvx(&M.r[i], 0)` intrinsic -- MSVC hoists the intrinsic
// loads exactly like the field reads.  Since even an intrinsic load hoists,
// the image's per-iteration `lvx128 v63, r0, r5` (0x82435B54) and the
// per-skeleton reload at 0x82435C94 are not an aliasing effect of any
// spelling of the transform; they are consistent with the allocator
// rematerialising the four rows from their stack slots (which is also why
// they land in v59-v63 and force the `vmaddcfp128`/`vmaddfp128` encodings at
// 0x82435B84 rather than our 4-operand `vmaddfp` on v0-v13).  The hip-centre
// store order below (y, x, z -- the joint loop's order) is the one of the
// three tried (z,y,x / x,y,z / y,x,z) that reproduces the image's z, y, x
// stores at 0x82435CDC-0x82435CF0; the remaining rows there are which
// element is held in f13 across the other two, scheduling only.
// w21-f (74.05, unchanged) -- MECHANISM FOUND, lever not: the hoist is a
// CROSS-LOOP CSE, not loop-invariant motion.  Compiled standalone with /FAs
// (same cl.exe, same flags), deleting ONLY the hip-centre transform in the
// second loop makes the first loop come out exactly as the image's: four
// per-iteration `addi rN, r1, 0x60..0x90` + `lvx128 v63/v61/v60/v59` inside
// the CTR loop and the vmaddcfp128/vmaddfp128 forms.  A one-loop scratch TU
// (real XMMATRIX header) does the same; adding a second loop that transforms
// with the same matrix makes MSVC CSE the two loops' row loads into
// v0/v13/v12/v11 above loop 1.  So in the image the two loops' matrix reads
// were NOT the same expressions to MSVC.  Also inert here (scratch TU or
// objdiff): `const XMMATRIX &mat =` binding the returned temp; a matrix
// copy for the second loop; a by-value matrix with a user copy ctor;
// __forceinline on the helper; storing loop-1 results through __stvx;
// letting &mat escape to an external call (still hoisted -- MSVC does no
// store-based kill here).  Note src/xdk/LIBCMT/vectorintrinsics.h's __lvx /
// __stvx are plain inline functions: this cl.exe rejects them as intrinsics
// (C4163) and `extern` declarations become real `bl __stvx` calls.
// w21-n (74.05 canonical, rows unchanged apart from one): the second loop's
// bound is a SIGNED compare in the image -- `cmpwi cr6, r30, 0x78` at the
// loop-2 latch (s*20 < 120) -- where `s < (unsigned long)6` emitted cmplwi;
// now `s < 6` (same trip count, same behaviour).  The first loop's latch IS
// unsigned (`cmplwi cr6, r9, 0xad`), so its cast stays.  Behaviour re-checked
// against the image end to end: the joint-remap/tracking tables at
// lbl_82F0BFD0 match ours word for word, field offsets and the hip
// z*r2+r3 / y*r1 / x*r0 chain agree.  Cross-loop CSE (w21-f) re-attacked in a
// scratch TU (/FAs, same cl.exe): still hoisted with &mat escaping to an
// external call, the hip transform moved to the top of loop 2, loop 1 moved
// into a static helper taking `const XMMATRIX &`, a pointer-aliased output
// buffer, a `const XMMATRIX *` for the hip only, and `hipResult` assigned
// after declaration.  Deleting the hip transform alone still restores the
// image's per-iteration reloads, so the trigger is the second USE being
// anticipated on every path (MSVC hoists a very-busy expression to the
// common dominator); no faithful spelling found that makes it non-busy.
// w21-an (74.05, unchanged): five more scratch-TU spellings (w21-n's probe)
// all still CSE the row loads above loop 1: `XMMATRIX mat; mat = ...`
// (split decl/assign), `transformed` declared before `mat`, `const XMMATRIX
// &mat =` bound to the returned temp, the hip read through
// nui_frame.SkeletonData[s] instead of the nuiSkel reference, `++s` latch.
```


## `??0Skeleton@@QAA@XZ`

`Skeleton::Skeleton` -- moved verbatim from `src/system/gesture/Skeleton.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 210-282):

```text
// RESIDUAL (w8-i, 89.87 canonical / 88.94736 fuzzy): 10 rows of 39, 152 B both
// sides.  MEASURED, not attacked -- recorded so the next lane starts from the
// anatomy rather than re-deriving it.  The three member stores and the Init()
// call are INTERLEAVED in the image and sequential in ours: the image
// materialises `addi r10, r30, 0xac8` (idx 21) and `li r8, -0x1` (idx 23)
// before storing anything, and only lands mTrackingID with `stw r8, 0xaac(r30)`
// at idx 28, after the 0xac8 block is already under way; we store mTracking to
// 0xaa0 at idx 23 and have Init()'s `this` set up (`mr r3, r30`) by idx 26.
// That is why idx 25 reads as a wrong-field row (target 0xaa0 mTracking vs our
// 0xaac mTrackingID) -- it is the same two stores in the other order, not a
// swapped member.
// The obvious lever is CLOSED BY CONSTRUCTION: MSVC initialises in DECLARATION
// order whatever the init list says, and declaration order is pinned by the
// target's own offsets (mTracking 0xaa0, mTrackingID 0xaac, unkac4 0xac4), so
// permuting the list below cannot move these stores.  The reachable question is
// what sits at 0xac8 and whether the image inlines the head of Init().
// Refuted (w12-d): assigning the three members in the ctor BODY instead of the
// init list -- 79.3, the vector is then built first and every store moves.
// w18-c (89.605 canonical, unchanged): the whole residual is EH, measured.  The
// image's ctor has an unwind funclet that destroys mCamDisplacements (r31+0x94
// -> this, +0xac8, ??1vector) and ours has none: MSVC inferred that Init() --
// the only call after the vector is built -- cannot throw, so it needs no state
// for the vector.  Probe: adding ANY possibly-throwing call to Init() (an
// external `Symbol s("x")`) makes this ctor 100.0 with all 38 rows equal, and the
// funclet appears.  So the image's Init() was NOT provably nothrow when this
// ctor was compiled, while its code (100.0, calls only memset and the
// __false_type _M_erase, which calls only memcpy) is identical to ours.  Not the
// cause, each tested: source order (ctor moved after Poll, as in the image's
// address order); `inline` Init; /EHs and /EHa on this TU (EHa adds funclets
// everywhere); `resize(0)` for clear() (Init then builds a CameraDisplacement
// temp, frame +0x140, 89.8).  Stronger probe: a CONSTANT-FOLDED throwing
// statement in Init -- `MILO_ASSERT(sizeof(mCamBoneLengths) > 0, ...)`, no code of
// its own -- also restores the funclet, so MSVC decides throw-ability before
// folding.  The original Init very likely had such a statement (a folded assert,
// or an inline call whose throwing arm dies); see DxShader::Compile
// (rnddx9/ShaderMgr.cpp, w18-c) for one that was found.  Not landed: unknown.
// w21-f (89.605, unchanged): re-checked, not re-attacked.  rb3-xenon and
// og-dc3-decomp carry the same ctor and the same Init body; the target
// object's string-literal set (??_C@) equals ours apart from our extra
// utl/StlAlloc.h path (dtk may pool that one elsewhere), so no folded
// MILO_ASSERT left a Skeleton-specific literal behind to name.
// Still no evidence for WHICH throwing statement Init had, and the brief
// forbids inventing one, so the 10-row EH residual stays.
// w21-w (89.605, unchanged; not re-attacked beyond reading): the image's own
// Init (0x824368C8) and _M_erase are call-for-call ours (memset; _M_erase ->
// memcpy only), and the target object's compile order (_M_erase, Init, Poll,
// ctor) is the same as ours, so neither the bodies nor the definition order
// explain why the original ctor treated Init() as may-throw.  Behaviour of the
// ctor itself (vtable, 20x ??_H TrackedJoint init, mTracking=0,
// mTrackingID=-1, unkac4=0.0f, empty vector, Init()) agrees with the image.
// w21-ak (89.605 -> 89.868 canonical, by the alias-map fix only: the two
// lis/addi rows naming OnlyReturns vs ??0PaddedJointPos@@QAA@XZ are now
// forgiven -- see the note above SkeletonFrame in Skeleton.h).  The 10-row EH
// residual is untouched.  One more datum for whoever retries it: the target
// object's section order is IsValid..KneeJoint, CamBoneLengths,
// CamJointPositions, ..., Init, Poll, ctor, IsTracked, ElapsedMs, ??_G -- so
// Init was compiled BEFORE the ctor in the original too (compile order cannot
// be why it was may-throw), and IsTracked/ElapsedMs are emitted only after the
// ctor references the vtable, i.e. they were most likely header-inline in the
// original while QualityFlags (emitted early) was in this .cpp.
// w21-bd (89.868, unchanged, 10 rows): not re-attacked beyond one datum --
// MSVC's nothrow inference here is whole-TU (our Init is defined AFTER this
// ctor in the source, line order notwithstanding, and is still proven nothrow),
// so no definition-order spelling can bring the funclet back; it needs the
// original's may-throw construct inside Init, which nothing in the image names.
// w34-a (2026-11-04, behaviour verdict): AGREES.  The field stores match the
// image exactly -- mTracking 0xaa0 = 0, mTrackingID 0xaac = -1, unkac4 0xac4 =
// 0.0f, the three mCamDisplacements words 0xac8/0xacc/0xad0 = 0, and the same
// 0x50(r31) spill of &mCamDisplacements -- as do the vtable store, the ??_H
// array-construction loop (counter 0x13, stride 0x74, count 6 per call), and
// both sides emit the same frame size and the same two `mr r3, r30` argument
// setups.  Nothing in the block carries a different value, field offset or
// call, so the charged rows are placement alone.
```


## `?Poll@Skeleton@@QAAXHABUSkeletonFrame@@@Z`

`Skeleton::Poll` -- moved verbatim from `src/system/gesture/Skeleton.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 492-567):

```text
// RESIDUAL (w7-br, 97.5 canonical, 49 of 209 rows; was 91.13 under w7-bl):
// ONE induction-variable choice left in the kNumJoints loop, and the flat
// register renumbering it forces.  The image keeps i*0x10 as a plain OFFSET
// (r27, `mr r27, r20` zeroed at 0x82436BD0, `addi r27, r27, 0x10` at
// 0x82436C5C) and adds it to TWO hoisted bases -- `add r26, r27, r22` at the loop
// head (0x82436BEC) (r22 = data+0x144, &mJointPositions[0]) and `add r10, r23, r27` in the
// tail (0x82436C50; r23 = data+4, &mRawPositions[0]).  MSVC here strength-reduces the
// same two uses into two WALKING POINTERS (`addi r26, r26, 0x10` /
// `addi r28, r28, 0x10`), one register lighter: that one register is why the
// image saves twelve callee-saved GPRs (`bl __savegprlr_20`, frame 0xe0) and
// we save eleven (`__savegprlr_21`, 0xd0), and every remaining register-swap
// row is that cascade (r21<->r23, r20<->r22, r27<->r28).
// LEVERS (w7-br), each measured with run_objdiff in the worktree:
//   * `TrackedJoint &tj = mTrackedJoints[i];` bound at the loop HEAD but
//     used only for the two tail stores (the inner loop keeps the
//     `mTrackedJoints[i].mJointPos[j]` subscript): 91.13 -> 95.6.  This is
//     what gives MSVC the image's i*0x74 offset IV (r29) with the two bases
//     `this` (inner loop) and `this+4` (r24, head `add r28, r24, r29` at 0x82436BE4), and
//     the free trip test `cmpwi cr6, r29, 0x910` at 0x82436C60 -- w7-bl's
//     note recorded the same binding as neutral, but it had measured it at
//     the TAIL, after the inner loop, where it is.
//   * `switch (mTracking)` with `case kSkeletonNotTracked: Init()` and
//     `case kSkeletonTracked:` instead of the if/else-if pair: 95.9 -> 96.5.
//     The image's dispatch is `cmpwi r11, 0x0` on cr0 (0x82436B1C) / `beq` at
//     0x82436B24 then `cmpwi cr6, r11, 0x2` / `bne`; the nested ifs put
//     both compares on cr6.  Same behaviour (kSkeletonPositionOnly does
//     nothing on both spellings).
//   * a `const Vector3 *jointPositions` local declared AFTER the
//     MakeCameraToPlayerXfm loop (whose args stay inline) and used inside
//     the joint loop: 96.1 -> 97.5.  Declared BEFORE that loop it is 96.5
//     (the image materialises floorNormal, then data+0x144, then mPlayerXfms
//     -- the call's right-to-left argument order -- after `li r29, 1`, so
//     the pointer is not a pre-loop local); with no local at all MSVC
//     derives &mJointPositions[i] from the mRawPositions walker
//     (`addi r26, r29, 0x140`), 96.1.
// NEGATIVE RESULTS (w7-br), all exactly inert or worse: a matching
// `const Vector3 *rawPositions` local for the tail copy (91.1 -- MSVC then
// walks the raw pointer AND re-derives the joint address from it); binding
// `const Vector3 &pos = jointPositions[i];` at the head instead of the two
// inline subscripts (identical 49 rows); `if (mTracking)` for the dispatch
// (identical to the != spelling).
// w21-n (97.53 canonical, unchanged; now __savegprlr_22 vs the image's _20):
// behaviour re-read against the image -- dispatch, the j==0 copy vs
// MultiplyTranspose, mJointConf then mSmoothedPos from mRawPositions[i], the
// hip-height average plus clip-plane w -- all agree.  Still walkers, not the
// image's shared i*16 offset: inert (96.22 fuzzy) were a cast-pointer
// subscript for the raw copy, a rawPositions local before the Xfm loop or
// beside jointPositions, and a head-bound `const Vector3 &` to the joint;
// re-reaching the raw array through frame.mSkeletonDatas[skel_idx] is worse
// (92.94).
// w21-ah (97.53 canonical / 96.27 fuzzy, unchanged; same 49 rows): a
// standalone recompile of this TU reproduces our rows, and ~30 more spellings
// were inert or worse: both arrays via data (43 probe rows vs 46 -- joint is
// then derived from the raw walker, still two walkers), a rawPositions /
// PaddedJointPos* / array-reference local, a `const int *` state local, a
// Transform* / TrackedJoint* member binding (61-67), the jointPositions local
// moved to feed the MakeCameraToPlayerXfm loop (42, still walkers), tj as a
// pointer, every tj-vs-subscript mix for the two tail stores (97-109), a
// separate `k` counter for either array, a byte-offset raw read, a guarded
// do/while, `j != 0` arm order, and the decomp-synth permuter (497 variants,
// best +0.06 fuzzy via member_ref_bind -- not taken).  The clip-plane copy at
// the tail (image z,w,x,y order) is a scheduling knock-on of the same
// register cascade.
// The three MakeString rows in the Function Call Diff are ICF folds (the assert
// format strings), not wrong callees.  Control flow is faithful: the `beq` at
// 0x82436B24 goes to Init() and the `bne` two instructions later returns.
// w26-y: behaviour re-read value by value against the image and unchanged --
// dispatch, Reset's (-1, false), mElapsedMs, the five mHipCenter/Tracking words,
// the EnrollmentIndex test and SetEnrollmentIndex(mClippedFlags), the
// MakeCameraToPlayerXfm and BoneLength loops, the clip-plane z,w,x,y reorder
// (an alignment artifact of the same cascade, the same four words land in the
// same four slots) and the kNumJoints copy.  On the MakeString rows: all the
// involved instantiations ARE in one ICF group in icf_aliases.map
// (0x824d1870), yet name_check charges these three and scores the identical
// construction in Skeleton::RequestIdentity EQUAL -- so part of this row's
// residual is a measurement asymmetry in the ICF aliasing, not source.
```

Inside the body (was lines 605-650):

```text
        // The inner loop must keep the `mTrackedJoints[i].mJointPos[j]`
        // subscript rather than go through `tj`: the image re-forms that
        // address from `this` every iteration -- `slwi r11, r31, 4` /
        // `add r11, r11, r29` (i*0x74) / `add r11, r11, r30` (this) /
        // `addi r5, r11, 0x4` at 0x82436BF0 -- and that is what keeps
        // i*0x74 an offset IV.  `tj` itself is bound here, at the head,
        // so that `&mTrackedJoints[i]` is `add r28, r24, r29` (r24 = this+4)
        // and stays live across the inner loop's calls.
        // w16-c: indexing data.mJointPositions[i] directly instead of
        // through this pointer is 96.7 (the image's extra callee-saved r20
        // holds an i*16 OFFSET IV added to two bases; neither spelling gets it).
        // w27-a (baseline 97.5266 canonical / 96.2222 fuzzy, 44 of 209 rows):
        // the mechanism is c2's induction-variable SHAPE, and it is the root of
        // the whole r20-r31 rotation.  The image keeps ONE i*16 accumulator
        // (r27, `addi r27, r27, 0x10`) shared by `jointPositions[i]` and
        // `data.mRawPositions[i]` and re-forms both addresses every outer
        // iteration (`add r26, r27, r22` / `add r10, r23, r27`); we
        // strength-reduce each array into its own walking pointer
        // (`addi r26/r28, +0x10`).  That is one more live callee-saved value
        // (image saves r20..r31 and reserves 0xe0 of frame, we save r22..r31
        // and reserve 0xd0), and 0/frame move up into r20/r21, shifting every
        // downstream register.  Behaviour is identical on both sides: every
        // load/store resolves to the same field of the same element.
        // Measured on the per-object path (EXACT for this unit): hoisting
        // mRawPositions into a named pointer 91.0, PaddedJointPos* base 96.22
        // (= baseline), indexing through the cast in place 95.7, explicit
        // `(char*)base + i*16` byte arithmetic 91.0.  Open question: which
        // source form makes c2 keep the shared index instead of forming
        // pointer IVs per base.
        // w28-c: attacked that open question with C2RS-BRIDGE 12.6's
        // "the IV COUNT is source-visible" lever, six spellings, all measured
        // on the per-object path (EXACT here -- it reproduces 97.52657 /
        // 96.22222 to the digit).  NONE produce the accumulator, and every one
        // is WORSE than the hoisted `Vector3 *` above: a shared `int
        // jointOffset` and `(const char *)base + jointOffset` for both arrays
        // 92.28503; the same with a PaddedJointPos element index 92.28503;
        // no hoist at all 96.74880; two PaddedJointPos * locals indexed by i
        // 96.95169; one of each 96.95169.  So the walking pointers are not an
        // artefact of the hoist -- spelling the shared index explicitly makes
        // c2 materialise base+offset with a real `add` per use, costing more
        // than the extra callee-saved register it saves.  The image's
        // accumulator form is a different strength-reduction choice, not a
        // source shape this TU can express.  Open question now narrower: what
        // makes c2 pick an offset accumulator over a pointer IV for a 16-byte
        // element (the neighbouring `int` array at stride 4 is an `lwzu`
        // walk on BOTH sides, so stride alone does not decide it).
```

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/gesture/Skeleton.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

File-level (was lines 143-157; no definition follows it):

```text
// size 0x11c8
// w8-i: the implicit ctor `??0SkeletonFrame@@QAA@XZ` is 100.0 under the
// normalized ruler and 99.20 under name_check (100 B, 21/25 equal) -- and the
// gap is a TOOLING gap, not a source defect, so do not spend a lane on it.  All
// four charged rows are the two `lis`/`addi` symbol pairs at idx 7/9 and 13/15:
// our object names `??0PaddedJointPos@@QAA@XZ` where the image names
// `OnlyReturns@0x823e3b70`, which is the same code -- the ICF fold that
// collapsed every trivially-returning ctor.  build/373307D9/icf_aliases.map is
// missing that membership (group 348 in scripts/symbol_aliases.json), so the
// name_check ruler charges a fold it should forgive.  Fix the alias map, not
// this header; hand-editing the map was deliberately NOT done here.
// w21-ak: DONE -- ??0PaddedJointPos@@QAA@XZ added to the OnlyReturns@0x823e3b70
// group in scripts/symbol_aliases.json on body-test evidence (both bodies are the
// single word 4e800020, blr; record under that group's "body_extended").  This
// ctor now reads 100.0 normalized and fuzzy; ??0Skeleton@@QAA@XZ 89.605 -> 89.868.
```


## `?Create@SkeletonFrame@@QAAXABU_NUI_SKELETON_FRAME@@H@Z`

lane C / w-low-gesture-char (2026-11-13):

```text
// Behaviour audit (name_check listing, 74.048 canonical / 72.364 fuzzy): NOT A BUG.
// Loop 1: r9 5..145 step 28 (cmplwi 0xad, six skeletons), positions at
// nui+0x50+16*(28s+j), transformed[20s+j]; Z*r2+r3, Y*r1, X*r0 on both sides.
// Loop 2: sTrackingMap[eTrackingState] -> +0x0, mQualityFlags <- nui+0x1b0,
// raw z/y/x <- SkeletonPositions[src] (0x28/0x24/0x20), joint y/x/z <-
// transformed[20s+src], tracking <- sJointTrackingMap[state[src]] (nui+0x160)
// into +0x284+4*dst; trackingID <- +4, mClippedFlags <- +8; hip from +0x10.
// Residual is the cross-loop CSE of the four matrix rows (see w21-f) only.
```
