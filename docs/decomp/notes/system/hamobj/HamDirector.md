# Lane notes: `src/system/hamobj/HamDirector.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `Dc3W8bProbe()`

`Dc3W8bProbe` -- moved verbatim from `src/system/hamobj/HamDirector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Attribution: no function named `Dc3W8bProbe` in report.json (unit); filed under the source name

Above the definition (was lines 112-130):

```text
// w8-b: orphan-instantiation anchor.  ham_xbox_r.map attributes eleven COMDATs to
// hamobj:HamDirector.obj that nothing in the shipped HamDirector body calls --
//   ?deallocate@?$StlNodeAlloc@UDancerFrame@@...     0x82466BD8 (map 42301)
//   ?allocate@?$StlNodeAlloc@UDancerFrame@@...       0x82467888 (map 42320)
//   ??1DancerFrame@@QAA@XZ                           0x82469108 (map 42355)
//   ??0DancerSkeleton@@QAA@ABV0@@Z                   0x82469860 (map 42383)
//   ??1?$vector@UDancerFrame@@...                    0x82470650 (map 42542)
//   ??$__uninitialized_fill_n@PAUDancerFrame@@...    0x82470A38 (map 42549)
//   ?_M_insert_overflow_aux@?$vector@UDancerFrame@@  0x82470FB0 (map 42557)
//   ??$MakeString@PBDVSymbol@@V1@V1@@@...            0x82467758 (map 42316)
//   ??$MakeString@PBDVSymbol@@V1@@@...               0x824677C8 (map 42317)
//   ??$MakeString@PBDHH@@...                         0x82467828 (map 42318)
//   ??1ArchiveSkeleton@@UAA@XZ                       0x82466B48 (map 42296, via Skeleton.obj's copy)
// -- i.e. the odr-use that instantiated them was compiled into this TU and then
// dropped by /OPT:REF, leaving the instantiations behind.  Reproducing that needs
// an unreferenced function with EXTERNAL linkage.  `static` does not work -- MSVC
// discards a static before it instantiates anything the static mentions; measured
// on the sibling probe in HamMove.cpp, where `static` cost exactly its one row
// (31246/5546312 -> 31245/5546220).  Removing this anchor takes all eleven to 0%.
```

Inside the body (was lines 133-152):

```text
    // w8-b FLOOR: this push_back is what emits
    // ?_M_insert_overflow_aux@?$vector@UDancerFrame@@... (0x82470FB0, 452 B target).
    // Measured 98.23009 match_percent_normalized (97.65487 fuzzy), up from 0.0 on
    // both rulers -- the row now pairs at all, and the residual
    // is inside the inlined _M_clear_after_move()'s reverse _Destroy_Moved_Range
    // loop: our 460 B body carries two extra instructions the image does not have
    // (`subi r5, r11, 0x2d8` + `stw r5, 0x5c(r31)`, MSVC EH sub-object bookkeeping)
    // plus 13 r10<->r11 swaps across one register pair (normalization discounts
    // those: 98.23 normalized vs 97.65 fuzzy on the same rows).
    // REFUTED, do not re-derive: (a) the "wrong callee" row objdiff reports for
    // ??$__uninitialized_copy@PBUDancerFrame@@ vs @PAUDancerFrame@@ is an ICF
    // naming artifact -- ham_xbox_r.map 43136/43137 are BOTH 0x8249AAA0, so the two
    // spellings are one function and no source change can separate them;
    // (b) the only remaining lever is respelling the shared stlport
    // _M_clear_after_move()/_Destroy_Moved_Range path, and that is DECLINED rather
    // than untried: every inlined use in the binary moves with it, which is exactly
    // the shape w7-bk measured on the neighbouring vector::size() (see the REFUTED
    // block at stl/_vector.h:180 -- 31171 matched -> 30851 / 30554 for its two
    // spellings, hundreds of rows down, to cross a handful).  452 B is not worth
    // gambling the binary on; re-measure whole-binary before touching it.
```


## `?CollideList@HamDirector@@UAAXABVSegment@@AAV?$list@UCollision@RndDrawable@@V?$StlNodeAlloc@UCollision@RndDrawable@@@stlpmtx_std@@@stlpmtx_std@@@Z`

`HamDirector::CollideList` -- moved verbatim from `src/system/hamobj/HamDirector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 493-502):

```text
    // w18-b BEHAVIOUR FIX: the venue is asked for CollideListSubParts, not
    // CollideList.  The image dispatches `lwz r11, 0x0(r3)` / `lwz r11,
    // 0x2c(r11)` on the mVenue pointer -- slot 11 of WorldDir's PRIMARY
    // (ObjectDir) vtable, which is RndDir::CollideListSubParts
    // (build/373307D9/asm/system/world/Dir.s, ??_7WorldDir@@6BObjectDir@@@).
    // Calling CollideList went through the RndDrawable subobject (+0x9c) and
    // RndDir::CollideList's proxy test, which the image skips.  The old
    // w8-i note here read the slot as "CollideList through the primary
    // vtable" and blamed a RndDir base-order difference; the RTTI says
    // RndDrawable is at +0x9c in the image too (??_R4WorldDir@@6BRndDrawable@@@).
```


## `?SetDircut@HamDirector@@IAAXVSymbol@@V?$vector@UPropertyFilter@CameraManager@@V?$StlNodeAlloc@UPropertyFilter@CameraManager@@@stlpmtx_std@@@stlpmtx_std@@@Z`

`HamDirector::SetDircut` -- moved verbatim from `src/system/hamobj/HamDirector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 817-825):

```text
        // 100% (w8-n).  `SafeName(mNextShot)` takes `Hmx::Object *`, so the
        // HamCamShot -> Hmx::Object conversion happens at the CALL and MSVC has to
        // null-guard the virtual-base adjustment: `addic. r11, r11, 0x4` + a second
        // `beq` to the "NULL" block, then `lwz r11, 0x20(r11)`.  Testing the DERIVED
        // pointer first lets the adjustment be unguarded and folded into the
        // displacement -- the image is `lwz r11, 0x24(r11)` at 0x8247163C with no
        // `addic.` at all -- and the twice-mentioned `mNextShot` supplies the image's
        // two dead home stores of the pointer at 0x50(r31) (0x82471624, 0x82471630).
        // 94.6289 -> 100.0, 97 of 97 instructions equal.
```


## `?OnLoadSong@HamDirector@@IAA?AVDataNode@@PAVDataArray@@@Z`

`HamDirector::OnLoadSong` -- moved verbatim from `src/system/hamobj/HamDirector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 1097-1103):

```text
// w20-l: normalized 100 / fuzzy 99.39. 23 rows, all ONE consistent callee-saved
// rotation: image this=r26, &TheDebug=r25 (reused for the literal 1 stored to
// mLoadedNewSong and the EH state), kAssertStr hi=r23, file name=r24; ours
// this=r23, r26, r24, r25. Every store/load (0x2fc crews, 0x304, 0x336, 0x330,
// 0xe0, 0x11c) and branch target checked against the image; the MakeString rows
// are ICF-folded instantiations. Regalloc priority, no value divergence. A
// `Symbol crew = hpd->Crew();` local in the loop drops to 98.7 -- reverted.
```


## `?ShotsDisabled@HamDirector@@IAA_NXZ`

`HamDirector::ShotsDisabled` -- moved verbatim from `src/system/hamobj/HamDirector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 1549-1554):

```text
// w17-b: 98.04 -> 100.  ONE boolean expression, `A || (B && C) || (D && !E)`.
// The image's `li r11, 0/1` + `clrlwi r3, r11, 24` tail is the 0/1
// materialisation of a whole short-circuit expression; every earlier spelling
// (w8-n, w9-d: if/return chains, a `disabled` flag, a trailing `return D && !E`
// under the `if (!mDisablePicking)` wrapper) only made part of it an
// expression.  Same truth table as the old if-chain.
```


## `?ClosestMove@HamDirector@@IAA?AVSymbol@@XZ`

`HamDirector::ClosestMove` -- moved verbatim from `src/system/hamobj/HamDirector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 1636-1650):

```text
// w13-b: the image's frame is 0x1e0, ours was 0x1d0 with every slot paired
// (0x50 playerDiff/Symbol temp, 0x58 DataNode, 0x60 Symbol, 0x70 buf), so the
// extra 0x10 sits above buf: buf is longer than 256.  The frame only bounds it
// (260 still gives 0x1d0; 272 gives 0x1e0), so 272 is the smallest 16-byte
// multiple that reproduces it, not a recovered constant.  99.33 -> 99.35; the
// one remaining row is the redundant `clrlwi r11, r11, 24` below.
// w21-aj (still 99.35, same single clrlwi row at target 0x824744A0): a
// binary-wide scan of the target listings finds `clrlwi rX,rX,24` directly
// followed by `extsb` only 6 times, and this is the only one outside the
// XDK/CRT (ccodec x2, identityprivateapi, LIBCMT input/inputs), so there is
// no matched game function to copy the spelling from.  8 more standalone
// probe spellings, all clrlwi-free: char/uchar-returning inline accessors
// (At/UAt), an inline IsEnd(char) predicate, assignment-in-condition
// `if (!(bufCh = buf[..]))`, const uchar -> const char copy, `(bool)c ==
// false`, `!(unsigned char)c` with a (char)(uchar) argument.
```

Inside the body (was lines 1669-1673):

```text
                    // 0x82474450 `li r25, -0x1` precedes 0x82474454
                    // `li r24, 0x0`: maxScore is initialised first.
                    // w16-b: plain loops (for over i, `while (cand[m] && buf[m])`
                    // over the match) move the zero test onto `extsb.` and lose
                    // the image's cmplwi + redundant clrlwi: 3 rows vs 1.
```

Inside the body (was lines 1692-1725):

```text
                                    // RESIDUAL (w7-aq, 99.3 canonical): the
                                    // image emits an extra, provably redundant
                                    // `clrlwi r11, r11, 24` between the zero
                                    // test and this sign-extension. Refuted:
                                    // a named `char bufChar = bufCh;`
                                    // intermediate, and widening bufCh to
                                    // `unsigned int` -- both byte-inert.
                                    // w21-m (still 99.35, the one clrlwi row):
                                    // ~35 spellings in a standalone cl.exe
                                    // probe, none emit clrlwi+extsb: char /
                                    // int / unsigned int / unsigned short
                                    // locals, (char)(unsigned char) chains,
                                    // `& 0xff`, inline Lc(char)/LcU(uchar)/
                                    // const char& helpers, a second load
                                    // through (unsigned char *)buf, og's
                                    // p[buf - candidate] index, an
                                    // outer-scope bufCh, and indexed
                                    // for/while loops (MSVC strength-reduces
                                    // them to this exact pointer form).  The
                                    // only clrlwi seen was tolower(uchar)
                                    // into r3 with no extsb.
                                    // w26-o: 12 more spellings, same one row.
                                    // The mask can only survive where c2 cannot
                                    // prove the value's range, and every form
                                    // here feeds it a byte load (lbzx/lbz), which
                                    // c2 range-analyses to 0..255 and then folds
                                    // `(char)` to a bare extsb.  unsigned int,
                                    // int and unsigned short locals, a
                                    // `const unsigned int &` binding of the
                                    // materialised temporary, and an explicit
                                    // `& 0xff` are either byte-inert or replace
                                    // the image's cmplwi with cmpwi / extsb.
                                    // Mechanism: a c1 cast node that survives into
                                    // the IL, not a reachable source shape.
```


## `?UpdatePostProcOverlay@HamDirector@@IAAXPBDPBVRndPostProc@@1M@Z`

`HamDirector::UpdatePostProcOverlay` -- moved verbatim from `src/system/hamobj/HamDirector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 1800-1805):

```text
    // The unchanged case still restores the reflect (w19-x bug fix: the image's
    // blend-compare `beq` lands on the `stw` that puts the old TextStream back).
    // One shared SetReflect(reflect) tail, as the image has it (w25-gh): with an
    // early `return` the restore was written twice, which raised the reflect's
    // colour priority (pri 1 -> -12 once shared) and rotated procB / reflect /
    // &sPostProcBlend through r25-r27.
```


## `?GetPracticeFrames@HamDirector@@QAA_NAAPAV?$Key@VSymbol@@@@0@Z`

`HamDirector::GetPracticeFrames` -- moved verbatim from `src/system/hamobj/HamDirector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 2001-2014):

```text
            // w25-gf open row (r10/r11 in the second loop and the tail): the
            // second-loop version of idx (sym 8 v2, pri 18 tie 77) ties the three
            // tail address pieces (code-band mints, pri 18 tie 0) and wins on
            // tie, so it takes r11; the image needs it after them (r10, offset
            // r11). Needs idx v2 <= 17 or one more GPR candidate referenced in
            // the success block (pieces 2*P). ~45 spellings (own loop vars,
            // break/flag forms, explicit guard do-while, named key/value/begin
            // locals, merged return, nested ifs) left the keys unchanged.
            // w26-f5 re-tested the C-3 prediction (one more GPR candidate
            // referenced in the success block): naming the start pointer alone
            // and the end pointer alone are both inert, and naming both
            // regresses canonical 100.0 -> 90.6.  The 18/18 tie is unchanged,
            // so the second-loop idx v2 still takes r11 while the image wants
            // it in r10.
```


## `?PoseIconMan@HamDirector@@QAAXPAVCharClip@@MPAVRndTex@@_N0MM@Z`

`HamDirector::PoseIconMan` -- moved verbatim from `src/system/hamobj/HamDirector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 2100-2105):

```text
// w21-aa: 97.19 -> 100 (modulo register permutation).  The clip1 weight
// `1.0f - blendFrac` is computed into its own local BEFORE the call: the
// image loads 1.0 (r11) ahead of 0.0 (r10) in the blend arm, so the two
// arms disagree on registers and MSVC cannot hoist the `lis` pair above
// `beq` (the residual w7-q / w20-s filed as an artifact).  Same float
// subtraction, same arguments.
```

Inside the body (was lines 2114-2123):

```text
        // Spell the weights as literals, NOT as named `float` locals.  Two
        // measurements (2026-09-14, w7-q):
        //   * `float fOne/fZero` declared above the `if` -- the spelling that
        //     used to be here -- also cost a commutative-operand row on the
        //     `curXfm.v += *pos` 130 instructions further down (idx 198,
        //     `fadds f0,f13,f0` where the image has `fadds f0,f0,f13`).
        //   * Scoping those same locals INTO the blend arm does not move the
        //     constant anchors either, and brings that distant row back.
        // Literals close idx 198.  (The `lis` residual this comment used
        // to call uncontrollable closed via `weight1` below -- w21-aa.)
```


## `?Reteleport@HamDirector@@QAAXXZ`

`HamDirector::Reteleport` -- moved verbatim from `src/system/hamobj/HamDirector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 2525-2533):

```text
            // NOTE (w7-av): BEHAVIOURAL FIX -- this is the clip's END beat that
            // GetClipStartAndEndBeats just wrote, not the stream beat.  The
            // image never stores the MsToBeat result into the slot it hands to
            // GetClipStartAndEndBeats: it reuses `s`'s dead slot for the
            // out-parameter (0x50) and reads it back only here,
            //   82479BFC  lfs   f2, 0x50(r31)
            //   82479C08  fsubs f1, f2, __real@40800000
            //   82479C0C  bl    ?PredictDeltaPos@ClipPredict@@QAAXMM@Z
            // which a slot holding a live `beat` could never be.
```


## `?ReactToCollision@HamDirector@@IAA_NM@Z`

`HamDirector::ReactToCollision` -- moved verbatim from `src/system/hamobj/HamDirector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 2543-2554):

```text
// w21-m: CLOSED (99.18 -> 100, all 258 rows equal). The residuals were a
// named `nextIdx2` local and an explicit `return true` in the
// InsertRealShot arm, found with a standalone cl.exe probe.
// NOTE (w7-ai, superseded): residual at 99.2%.  What is left is 22 rows of one
// callee-saved swap -- retail puts propKeys in r29 (recycling the register
// that held the `shot` static's address) and keyIdx in r28, we do the
// opposite -- plus the cross-jump direction of the two
// ReactToCollision_InsertRealShot call sites (retail branches BACKWARD from
// the second to the first at 0x8247A550; we branch forward) and the two
// argument-setup instructions of the second FrameFromIndex, which are the
// same scheduling decision.  Splitting `int keyIdx;` from its initialiser to
// reorder the allocation is byte-identical, measured 2026-09-14.
```

Inside the body (was lines 2613-2625):

```text
        // beatSum only exists on the else side: retail keeps the sum in the
        // volatile f0, compares, and only copies it into a callee-saved FPR
        // once beat2 is dead (fmr f31,f0 at 0x8247A428).  Materialising it
        // before the test keeps four FPRs live at once and turns the three
        // inline stfd of the prologue into bl __savefpr_28.
        // w14-b (still 99.18): the image tail-merges the shared
        // InsertRealShot call into THIS arm and the abort arm branches to it;
        // we merge it the other way. Hoisting the call below the if/else with
        // an early `return true` on the MoveShot(beat) path costs 96.79.
        // w21-m: the explicit `return true` here is what puts the shared
        // InsertRealShot call in THIS arm (0x8247A41C) with the abort arm
        // branching back to it (b at 0x8247A550); without it MSVC
        // cross-jumps the other way.
```


## `?OnPopulateMoves@HamDirector@@IAAXXZ`

`HamDirector::OnPopulateMoves` -- moved verbatim from `src/system/hamobj/HamDirector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 2939-2944):

```text
// w21-bm: 99.637 -> 100.  w21-aj had stopped here on 46 frame-slot rows it read as
// MSVC spill-slot packing; they were all one cause: `transName =
// Symbol(MakeString(...))` read the new Symbol back through the ctor's returned
// pointer, where the image (0x8247B41C `lwz r27, 0xe4(r31)`) reads the implicit
// temporary from its frame slot -- `transName = MakeString(...)` does that, and
// every spill slot falls into place.  The last row was the name lookup below.
```


## `?DrawIconMan@HamDirector@@QAAXVSymbol@@00MMPAVRndTex@@@Z`

`HamDirector::DrawIconMan` -- moved verbatim from `src/system/hamobj/HamDirector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 3126-3145):

```text
// w17-b (stopped at 99.269 canonical, unchanged): the key searches now compare
// `moveName == key.value` (the image's `cmplw cr6, r24, r8` operand order; a
// register-only change, canonical-neutral).  Remaining: the image's frame is
// 0x10 larger and it stores the vector's _M_start to 0x60(r31) on EVERY search
// iteration (`stw r11, 0x60(r31)` at 0x82474D0C, never read back) -- some
// address-taken temporary in the original search that we do not have; the
// String/Symbol locals below are shifted by +0x20 as a result.  Not found.
// w21-aa (99.27 -> 99.27 canonical; fuzzy up): BEHAVIOUR FIX -- the image
// tests `keyBeat + beatExtra < 0` and zeroes beatExtra (0x82474E64 `fadds
// f0,f1,f29`, 0x82474E78 `fmr f29,f30`; f29 = 2nd float param, 0x82474BD0
// `fmr f29,f2`); ours tested/zeroed beatOffset.  The beat sum is keyBeat +
// beatOffset + beatExtra (0x82474E88/8C).  Normalized could not see this: it
// read as an f28<->f29 permutation.  MILO_NOTIFY arg is const char*
// (MakeString<PBD>, name_check row).  Store probes for the dead
// `stw r11,0x60(r31)` (r11 = _M_start, per iteration): `keys->at(i)` DOES
// produce it and the image's frame size, but also a range check + a second
// store of the element address (95.1); a named `Key<Symbol> &` in the loop
// stores the ELEMENT address, not begin; `(keys->begin()+i)->value` and a
// `size()` loop bound: no store.  Remaining: that store x2, frame +0x10, the
// r28<->r30 / r10<->r11 renaming it implies.
```

Inside the body (was lines 3160-3168):

```text
    // w26-o: 99.269 canonical.  The image runs a 0x10 larger frame because it
    // spills the Keys vector's begin pointer (`stw r11, 0x60(r31)`) inside both
    // of these loops; we keep it in a register across them.  A `Keys&` local, a
    // per-iteration `const Key<Symbol>&` element binding and int/unsigned index
    // and count spellings are byte-inert or worse (a named element pointer is
    // 98.02).  The r28/r30 and r10/r11 pairs are one colour decision each, and
    // the lwzx base/index order is a mechanism the c2 tap has not read yet
    // (C2RS-BRIDGE 8.7, open questions) -- there is no commutative tuple here
    // for the sort key to reach.
```


## `?DrawIconMan@HamDirector@@QAAXW4Difficulty@@MMMMPAVRndTex@@@Z`

`HamDirector::DrawIconMan` -- moved verbatim from `src/system/hamobj/HamDirector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 3227-3232):

```text
// w21-aa: 99.43 -> 100 (modulo register permutation).  The w8-n residual
// (we kept clipIdx*8 in callee-saved r28 and read the next key as
// `lwz r5, 0x8(r8)`; the image recomputes `slwi r10, r10, 3` / `lwzx`)
// closed by naming `nextIdx = clipIdx + 1` before the bound test: MSVC then
// no longer CSEs (clipIdx+1)*8 into clipIdx*8 + 8.  Same unsigned compare
// (int converted to size_type), same index.
```


## `?GetClipStartAndEndBeats@HamDirector@@IAAPAVCharClip@@VSymbol@@AAM1PAU?$pair@MM@stlpmtx_std@@@Z`

`HamDirector::GetClipStartAndEndBeats` -- moved verbatim from `src/system/hamobj/HamDirector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 3352-3361):

```text
            // BEHAVIOURAL FIX (w7-aq): this seeds from the CLIP key's frame,
            // not the practice key's.  The image keeps &clipSymbols[clipKeyIdx]
            // in r29 (`add r29, r10, r11`, target idx 141 -- the same r10/r11
            // the adjacent `lwzx r4, r10, r11` uses for .value) and reads this
            // frame as `lfs f0, 0x4(r29)` at idx 149.  The practice key's frame
            // is read separately, off r28, at idx 176.  We wrote
            // practiceSymbols[foundIdx].frame here, the image reads
            // clipSymbols[clipKeyIdx].frame -- which is also what the maths
            // wants: loopAdjust has to be measured from where the CLIP starts
            // in the song, not from where the practice section starts.
```

Inside the body (was lines 3376-3406):

```text
            // clip->StartBeat() is spelled at each of its three uses: the image
            // reloads `lwz r11, 0x40(r30)` / `lfs 0x0(r11)` at target idx
            // 171/180/191.  Binding a `clipStartBeat` local caches it in f30
            // and deletes two of the three.  And startBeat is ONE store --
            // `startBeat = x - adjust + clip->StartBeat()`, target idx 182-184;
            // the `startBeat = clipStartBeat; startBeat += ...` form emits a
            // dead first store the image does not have.
            // CLOSED (w7-bp, 95.2 -> 98.0 -> 100.0 canonical, 200/200 equal).
            // w7-aq had this at 95.2 and correctly diagnosed the cause: the
            // image computes `adjust = beat1 - loopAdjust` AFTER the first
            // SecondsToBeat call (`fsubs f30, f29, f30`, target idx 169), which
            // forces BOTH beat1 and loopAdjust to live across that call in
            // callee-saved FPRs (f29/f30) and costs it an extra `fmr f30, f1`
            // at idx 164.  Written as one expression -- inline or with a named
            // `adjust` -- MSVC hoists the call-invariant subtraction IN FRONT
            // of the call, keeps loopAdjust in f1 (Mod's return register), and
            // then loads clip->StartBeat() before the call instead of after.
            // w7-aq's recorded negative (`- (beat1 - loopAdjust)` spelled at
            // both use sites, byte-inert) is correct and does not fix it.
            //
            // THE LEVER IS STATEMENT SPLITTING, not re-spelling the expression:
            // give each SecondsToBeat call its own statement and its own named
            // local, and everything downstream of it is scheduled after it.
            //   one expression, named `adjust`        95.2  (18 rows)
            //   `startSecs` split out, endBeat inline 98.0  ( 5 rows)
            //   both split out                        100.0 ( 0 rows)
            // The second split is what moves the endBeat `lwz r11, 0x40(r30)` /
            // `lfs f13, 0x0(r11)` to AFTER the call (target idx 180/182); with
            // endBeat still written as one expression we loaded StartBeat early
            // into callee-saved f29 and reused it, which the image does not do.
            // Do not fold these four statements back into two.
```


## `?Poll@HamDirector@@UAAXXZ`

`HamDirector::Poll` -- moved verbatim from `src/system/hamobj/HamDirector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 3452-3468):

```text
// w21-ad (98.53 -> 99.4): two levers -- a fresh, call-scoped out-param pair
// per GetPracticeFrames call (closed the 0x50/0x58 swap) and `overlayB`
// declared nullptr at FUNCTION scope (after player1): the image's `li r28, 0`
// at the top IS overlayB's web, so the post-proc block no longer needs the
// extra `mr` of the zero across.  Remaining: 41 register rows (zero r28 vs
// r25, songAnim r25 vs r26, p1anim, backupClipPlayer, &TheTaskMgr) and the
// fmuls/fmadds operand choice (3 rows).  Inert at 99.4: overlayA hoisted too;
// overlayB placed after songAnim; both orders of the noise sum and a
// constant-first `dt * frame`.  Not tried on purpose: `frame / 300.0f` -- the
// image multiplies by the 0x3b5a740e reciprocal, so a division would move
// native rounding away from it.
// RESIDUAL (w7-aq, 98.3 canonical): 40 of the remaining 52 rows are one
// callee-saved 4-cycle -- the image colours {0-const: r28, player1: r27,
// songAnim: r25, p1anim: r26} where we get {r25, r28, r26, r27}, and it keeps
// &TheTaskMgr in r26 where we reuse the TheHamWardrobe base register r24.
// First-definition order is identical on both sides, so this is MSVC's
// spill-weight ranking, not a source ordering we can spell.
```

Inside the body (was lines 3492-3501):

```text
                // Deliberately uninitialised: GetPracticeFrames writes both
                // through references. The image emits no zero-store for either
                // slot (0x50/0x58), and `= nullptr` adds two it does not have.
                // w21-ad: each GetPracticeFrames call has its OWN pair of
                // out-params, scoped to that call.  With one shared pair the
                // image's (0x50, 0x58) then (0x58, 0x50) argument slots came
                // out swapped (MSVC keyed the slots on call position: renaming
                // the arguments moved nothing); a fresh pair per call lands
                // them where the image has them.  Unobservable either way --
                // both outputs are discarded.
```

Inside the body (was lines 3541-3569):

```text
                        // RESIDUAL (w7-aq, 98.3 canonical): the image makes
                        // `(float)backupIdx * freq` the standalone fmuls and
                        // `frame * dt` the fmadds multiply; we get the reverse,
                        // which also swaps which constant lands in f29 vs f30
                        // (3 rows).  Refuted: swapping the two terms, and
                        // binding the idx product to its own local -- both
                        // byte-inert.
                        // RE-CONFIRMED (w7-bp, at 98.5): doing BOTH at once --
                        // `float idxTerm = (float)backupIdx * sBackupDriftFreq;`
                        // as its own statement AND the frame product written
                        // first -- is also byte-inert, same 55 rows.  Statement
                        // splitting, which is what broke the analogous floor in
                        // GetClipStartAndEndBeats, does not reach this one:
                        // there the split moved work across a CALL boundary,
                        // here both products are call-free and MSVC canonicalises
                        // the fmadds operand choice regardless of source shape.
                        // w26-o: a plain scalar local IS folded away by copy
                        // propagation, so every spelling above was inert -- but a
                        // REFERENCE binding is not.  `const float &idxTerm = ...`
                        // materialises the product as a real temporary, which
                        // gives the fadds a variable-leaf operand in the c2
                        // commutative sort (C2RS-BRIDGE 8.7): the frame product
                        // then sorts first, so `frame * dt` is the fused
                        // multiply and `idx * freq` is the fmadds addend, as the
                        // image has it.  99.384 -> 99.944 canonical, 98.754 ->
                        // 99.314 fuzzy.  The remaining rows are the f29/f30
                        // constants' assignment order (freq vs dt) and the
                        // ObjRefConcrete<FlowLabel>/<RndPostProc> CopyRef pair,
                        // which the ICF map lists at one address.
```

Inside the body (was lines 3601-3611):

```text
            // RESIDUAL (w7-bp, Poll is 98.25 -> 98.5 canonical / 97.9 raw,
            // 1428 B, 360/360 instructions).  One cluster left that is not
            // already refuted above: target indices 229-232, where our build
            // emits an extra `mr r28, r25` and forms `addi r27, "force"@l`
            // one row earlier than the image.  Both sides zero overlayA and
            // overlayB at index 16 (`li r28, 0x0` there, `li r25, 0x0` here);
            // the image then keeps that null in r28 for the rest of the
            // function while we hold it in r25 and have to copy it across.
            // That is a consequence of the r25 <-> r28 permutation (18 of the
            // 40 register rows), not a separate defect, and canonical forgives
            // the permutation itself -- only the extra `mr` is charged.
```

Inside the body (was lines 3635-3643):

```text
                    // Redundant by value -- `blend` is still 1.0f here -- but
                    // the image EMITS it: `fmr f30, f31` at target index 278,
                    // between the CopyRef and the `addi r30, "song authoring -
                    // 2 equiv"`, in addition to the shared `fmr f30, f31` at
                    // index 234.  The sibling arm below writes
                    // mPostProcInterpBlend, so without this assignment the two
                    // arms share one initialisation and MSVC emits a single
                    // `fmr`.  w7-bp: 98.25 -> 98.5 canonical.  Do not delete it
                    // as dead -- it is the same cross-jump tell as FileLocalize.
```
