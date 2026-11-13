# Lane notes: `src/system/world/CameraShot.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?GetCam@CamShot@@QAAPAVRndCam@@XZ`

`CamShot::GetCam` -- moved verbatim from `src/system/world/CameraShot.cpp` on 2026-11-13 (lane w-five-clean; line numbers are as of the commit before the move)

Above the definition (was lines 1959-1985):

```text
// w17-b correction (still 98.91): except_data_828161A0 is SetFrame's EH prefix
// (it sits at 0x82816198, immediately BEFORE SetFrame at 0x828161A0), not
// GetCam's.  GetCam's own is except_data_82816028 (before 0x82816028): its
// funcinfo (0x820E4138) has 3 unwind states, ALL with null actions
// (lbl_820E4120 = {-1,0},{0,0},{1,0}), no try blocks, one ip-map entry.  So the
// original had objects whose destruction needed no code at all -- the frame
// pointer is the price of that EH, not of a real cleanup.  A one-NOTIFY_ONCE
// function (SpotlightDrawer::DrawShowing) has NO EH in the image, so the
// statics alone do not explain it.
// RESIDUAL (w12-b, 98.91 canonical): the image runs this with a frame pointer
// (`subi r31, r1, 0x90`) and EH data (except_data_828161A0 right after the
// function), i.e. one more callee-saved GPR, and every other row is that
// register shift. Nothing here has a destructor on our side, so the EH state
// must come from something we do not see (likely how the two
// MILO_NOTIFY_ONCE statics are constructed); not chased further.
// w21-ad (98.91): behaviour re-read vs 82816028..82816194 -- agrees (ret
// defaults to 0, Cam() at 0x210, the two notify arms tail-merged). The two
// NOTIFY_ONCE statics are NOT the EH source: FracToSample@CharBonesSamples
// builds two `_dw` with the identical std/ld/std/stw/stw/atexit sequence and
// has no frame pointer and no EH. Whatever owns the two null-action states is
// something with an empty destructor whose lifetime spans no call.
// w-this-adjust (2026-11-13): POINTER AUDIT, NOT A BUG -- `subi r31, r1, 0x90`
// is the EH frame pointer, not a this-adjustment.  Every pointer argument
// matches: the inlined GetCrowdDir (0x1b0 test, vbase hop to Dir() at +0x28,
// __RTDynamicCast to WorldDir), Cam() at 0x210, and both PathName arguments
// via the same Hmx::Object vbase conversion.  Re-measured 98.9 canonical,
// equal 57 / diff_arg 34 / del 1; every diff_arg is the r31 frame shift.
```

Added after the move (lane w-five-clean):

```text
// w-five-clean (2026-11-13): BEHAVIOUR VERIFIED.  Listing: cam = 0;
// GetCrowdDir inlined (mParentDir 0x1b0 else Dir(), __RTDynamicCast to
// WorldDir); cam = 0x210 (PanelDir::mCam's pointer); null -> NOTIFY_ONCE
// "paneldir but no cam" with PathName(crowdDir); no dir -> "no worlddir"
// with PathName(this); returns cam.  Unicorn h4, fresh: DIVERGENT
// data_layout, stable_divergent (RTTI arguments of the cast).  Native:
// CopyPollSemanticsTest.CamShotGetCamReturnsCrowdDirCamera (WorldDir with and
// without a cam, plain ObjectDir, mParentDir override; ignoring mParentDir
// fails it).  ret -> cam, `== 0` -> `!cam`: byte-identical.
```

## `?BuildTransform@CamShotFrame@@QBAXPAVRndCam@@AAVTransform@@_N@Z`

`CamShotFrame::BuildTransform` -- moved verbatim from `src/system/world/CameraShot.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 372-382):

```text
    // w25-mb, rows 74/75 (open): the image stores this 16-byte copy as words
    // 0,4,c,8 (ours 0,4,8,c) around the assert's `lfs` reload of .x.  Both sides
    // load the words in the same order.  Hypothesis: the two stores tie on
    // scheduler priority and the tie goes by tuple index, so the image's copy
    // lowering numbers the 0xc pair before the 0x8 pair.  Inert: const_cast at
    // the store, a Vector3& to the member, a named Vector3 copy, Interp
    // through `me`, `me` declared first or last, and the copy duplicated into
    // both arms of the kHugeFloat test (which also breaks the register
    // assignment), dt inlined, screenPos declared first, `me->` in the
    // kHugeFloat test, the if without braces.  Worse: an Interp temp output,
    // a `targetPos(0, 0, 0)` constructor.
```

Inside the body (was lines 424-435):

```text
        // w25-mb: localParent is copy-constructed from *parentXfm (it was
        // default-constructed, then assigned).  That closed rows 192/193, the
        // y/z fadds of `tf.v +=` below, whose operands came out the other way
        // round.  c2 sorts a commutative add's operands by key (C2RS-BRIDGE
        // section 8.7); for y/z the tf.v side is a memory leaf keyed by its base
        // temp's sid mod 4, and the old spelling's two base temps (V628, V632)
        // were both 0 mod 4, so the scalar-replaced localParent.v.y/.z, keyed
        // 0x13140/0x131a0, went first.  The copy constructor mints two more
        // temps upstream, so the bases become V630/V634 (2 mod 4, key 0x18008)
        // and tf.v goes first, as in the image (cqlo3 dump, read before and
        // after).  Rows 74/75 are still open; see the note at the
        // mLastTargetPos copy above.
```


## `?Interp@CamShotFrame@@QAAXABV1@MMPAVRndCam@@@Z`

`CamShotFrame::Interp` -- moved verbatim from `src/system/world/CameraShot.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 589-630):

```text
        // Residual (99.687 canonical): the image spends ONE fmr here, we spend two.
        // In the image the __real@00000000 register IS thisFocalDist (f30), so
        // `thisFocalDist = 0` is a deleted self-move and only `otherFocalDist = 0`
        // costs an instruction; our build colours the constant f28 and both inits
        // become real moves, which also rotates f28/f29/f30 for the whole function.
        // Refuted spellings (all measured, all neutral or worse): swapping the two
        // declarations (neutral, 99.687); moving `otherFocalDist` below the focus
        // block (99.0, +0x10 stack frame); `float otherFocalDist = thisFocalDist;`
        // (constant-propagated back to `= 0`, bit-identical).
        // w21-av (stopped at 99.687, same 22 rows: f28/f29/f30 rotation from
        // row 14, plus rows 281/283/285 here): thisFocalDist's `= 0` is a dead
        // store on every path (each path reassigns it before the Interp), which
        // is why the image can colour it onto the constant. Refuted: dropping
        // the `= 0` (97.1, MSVC spills the uninitialised value to the frame);
        // `else if (hasTarget) ... else thisFocalDist = 0;` (99.4, +0x10 frame,
        // f23 save). Both inits must stay up front; declaring them at the top
        // of the doDOF block is bit-identical.
        //
        // w27-l (2026-11-02, 99.68689 canonical / 99.46184 fuzzy, 512 rows):
        // the whole charged residual is ONE instruction, and it is the
        // CONSTANT'S COLOUR, not this block's spelling. Read both listings at
        // the doDOF entry: the image loads __real@00000000 into f30 and never
        // reloads it (`lfs f30` at idx 14), so `thisFocalDist = 0` is a
        // self-move the allocator deletes; ours loads the same constant into
        // f28 (idx 14), so BOTH inits become real moves (`fmr f30, f28` at idx
        // 281, `fmr f29, f28` at 283) and the image's `cmplwi cr6, r26, 0`
        // (the `if (focus)` test) slides two slots later.  The f28/f29/f30
        // rotation through both interpolator blocks is downstream of that one
        // choice: the constant is live across an IDENTICAL range on both sides
        // (last use `stfs <const>, 0x54(r31)`, i.e. screenVec.y = 0.0f, at idx
        // 233), so no spelling of the body changes the priority order.
        // Measured this round: `0.0f` literals (canonical and fuzzy inert);
        // one-line `float a = 0, b = 0;` (inert); `b = 0; a = b;` and the
        // swapped declaration order (canonical inert, fuzzy 99.46184 ->
        // 99.45206 -- do not land either).
        // The three entries under objdiff's "Function Call Diff" are ICF
        // aliases, not source differences: MakeString<_D3DFORMAT> and
        // MakeString<CamShotFrame::BlendEaseMode> are the same body
        // (0x82610090), and the two `$$BY...` asserts are one pointer-shaped
        // group (994 MakeString aliases in build/373307D9/icf_aliases.map).
        // objdiff already scores all three rows EQUAL; the block is display
        // only (C2RS-BRIDGE 9).
```


## `?SyncProperty@CamShot@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`

`CamShot::SyncProperty` -- moved verbatim from `src/system/world/CameraShot.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Attribution: definition is macro-generated (BEGIN_PROPSYNCS); symbol matched by the name the macro expands to

Above the definition (was lines 994-999):

```text
// w20-g: normalized 100.0, fuzzy 99.97 (name_check). Every operand and branch
// target checked: no divergence. The 4 charged rows (0xa208/0xa210, 0xa28c/
// 0xa294) are only the NAMES of the `disabled` / `anims` `static Symbol _s`:
// symbols.txt (since 46d4ade86, not in the map) calls them ?_s@?HP@ / ?_s@?IK@,
// we mangle ?JD@ / ?JO@ -- MSVC's per-function scope ordinal, which here moves
// with how many scopes SYNC_PROP_BITFIELD expands to. Same guard bits/strings.
```


## `?Load@CamShot@@UAAXAAVBinStream@@@Z`

`CamShot::Load` -- moved verbatim from `src/system/world/CameraShot.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Attribution: definition is macro-generated (BEGIN_LOADS); symbol matched by the name the macro expands to

Inside the body (was lines 1176-1220):

```text
        // The whole 88-row residual on this function is ONE cause: our frame is
        // 0x490 where the target's is 0x470. The target merges this array's slot
        // (0x90) with `tempDraws` far below -- disjoint scopes, so MSVC may
        // overlap them -- and that in turn lets the seven dead floats pack
        // contiguously at 0xf8..0x114. Ours puts vec at 0x110, which both costs
        // its 0x10 and splits the float block, and every remaining row is a
        // frame-slot offset shifted by the resulting +0x20. Splitting this into
        // two Vector2 scalars was measured: 88 rows -> 87, and the scalars still
        // do not merge with tempDraws, so it is not array-vs-scalar. The read
        // order and every field offset already match the target exactly.
        // w21-av (stopped at 99.89, same 88 frame-offset rows): declaring
        // `vec` first in this block is bit-identical (MSVC orders these
        // slots itself, not by declaration). Target slots for reference:
        // vec 0x90/0x98 (= tempDraws 0x90), parentPtr 0xa0, targetList 0x120,
        // tf 0x150..0x1c0, frame2 0x1d0, frame1 0x2f0, iter 0x140,
        // oldAnimSym 0x80, targetCount 0x84 (ours: 0x84/0x80).
        // w26-g canonical decomposition: report.json charges 88 units of 82400
        // (99.8932) and all 88 are IMMEDIATE diffs -- one point each (objdiff
        // PENALTY_IMM_DIFF=1; a register diff would cost 5 and is folded into
        // `arg_diff_score`, i.e. forgiven). So the "88 rows" the diff prints
        // are not 88 issues: they are the ONE frame delta above, charged at 88
        // different displacements. Repro: objdiff-cli diff -1 <target.obj> -2
        // <our.obj> with functionRelocDiffs=name_check, combineDataSections=true,
        // combineTextSections=true, ppc.calculatePoolRelocations=false
        // --map-file build/373307D9/icf_aliases.map, then read
        // `canonical_match_percent` (NOT `normalized_match_percent`, which is
        // the fuzzy score relabelled). The map file is not optional: without it
        // 11 rows here are charged on ICF-folded names (`>>` for Key<float>,
        // CopyRef<FlowLabel> vs CopyRef<RndTransformable>, SetObjConcrete<...>,
        // vector<UILabel*>::erase, ObjPtrList<RndMesh>::pop_back) and the same
        // function reads 99.826 instead of 99.893.
        // Refuted this lane: declaring `vec` at function scope, at the head of
        // the `else` block, and first in the block -- all three BIT-IDENTICAL,
        // which confirms slot assignment here is liveness/allocator-driven and
        // not reachable by declaration placement.
        // w26-n3, four more (canonical, per-object path exact here -- baseline
        // 99.8932 reproduced): moving `ObjPtr<RndTransformable> parentPtr(this)`
        // down to its assignment (to free 0x90 for `vec`) is 96.74151, far
        // worse; `Vector2 vecA, vecB` plus a reconstructed `Vector2 vec[2]`
        // initialiser is 98.43811; the two read-into-one-slot spellings do not
        // compile because both elements are read later (frame1/frame2
        // mScreenOffset).  So the +0x20 is a slot-allocation cascade rooted in
        // `vec` (16-byte, aligned) landing at 0x110 instead of 0x90, not a
        // removable local: ours puts parentPtr at 0x90 first and then cannot fit
        // vec, the image puts vec at 0x90 and parentPtr at 0xa0.
```


## `?Shake@CamShot@@IAAXMMABVVector2@@AAVVector3@@1@Z`

`CamShot::Shake` -- moved verbatim from `src/system/world/CameraShot.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 1679-1683):

```text
// w25-gt: the named copies below (cosine, len, velFps, angFps, angPowed) and the
// inline emulate-FPS ternary emit no code of their own.  They move the sids of
// the address temps c2 mints during lowering, and a memory operand's commutative
// sort key is its base temp's sid mod 4 (C2RS-BRIDGE 8.7); this combination puts
// mShakeVelocity.x's base in class 0, so its fadds/fmuls take spring/powed first.
```
