# Lane notes: `src/system/hamobj/HamCharacter.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SyncProperty@HamCharacter@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`

`HamCharacter::SyncProperty` -- moved verbatim from `src/system/hamobj/HamCharacter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Attribution: definition is macro-generated (BEGIN_PROPSYNCS); symbol matched by the name the macro expands to

Inside the body (was lines 134-140):

```text
    // w21-u: the getter is a small inline bool member (CrewCardShowing, name
    // ours).  The image masks the materialised 0/1 with `clrlwi r11, r11, 24`
    // at 82490B88 before storing it into the DataNode -- the bool-return
    // normalisation of an INLINED call, the same mask the GetPropShowing(n)
    // rows apply to an out-of-line return.  Open-coded `mCrewCardMesh &&
    // mCrewCardMesh->Showing()` drops it; w9-f's ternary emitted it but merged
    // the arms (97.71).  Closes SyncProperty to 100.
```


## `BlinkClipsOk()`

`BlinkClipsOk` -- moved verbatim from `src/system/hamobj/HamCharacter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Attribution: no function named `BlinkClipsOk` in report.json (unit); filed under the source name

Above the definition (was lines 215-221):

```text
// w17-b: the blink-clip predicate is a separate INLINE bool function.  Its
// result is materialised as 0/1 in r11 at the call boundary and the outer `&&`
// re-tests it (`clrlwi. r11, r11, 24` / `li r4, 1` / `bne`, sharing the
// `li r4, 0` with the null-servo arm, 0x824918B4..C0), which is the
// "re-normalisation" every open-coded spelling missed (w8-n table below).
// Name and home are ours -- nothing in the map says where it lived (a
// CharFaceServo inline member is as likely); the predicate is the image's.
```


## `?SyncObjects@HamCharacter@@UAAXXZ`

`HamCharacter::SyncObjects` -- moved verbatim from `src/system/hamobj/HamCharacter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 244-264):

```text
        // BEHAVIOURAL FIX (w8-n).  We had `&&` between the two Null() tests; the
        // image's predicate is `!left.Null() || right.Null()`.  0x82491884..
        // 0x824918C4: `beq` on a null servo goes to `li r4, 0`; `bne` on
        // `left != gNullStr` (0xa8) goes STRAIGHT to `li r11, 0x1`, which is a
        // short-circuit TRUE, not a fall-through into the second test; only the
        // left==null path reads 0xb0, and there `bne` (right != gNullStr) selects
        // `li r11, 0x0` while the fall-through selects `li r11, 0x1`.
        // CLOSED (w17-b, 98.30 -> 100): see BlinkClipsOk above.
        // Former RESIDUAL (w8-n, 98.2990 canonical): 1 cause, 3 rows.  After the `||`
        // merges r11 the image RE-NORMALISES it -- `clrlwi. r11, r11, 24` / `li r4, 0x1` /
        // `bne .L_824918C4`, sharing the `li r4, 0x0` at 0x824918C0 with the else
        // arm -- where we mask straight into the argument (`clrlwi r4, r11, 24`)
        // and jump over it.
        // Measured, as run_objdiff's canonical headline (1 d.p. as printed):
        //     bool blinking; if (servo) {...} else { blinking = false; }  98.3 <- this
        //     SetBlinking(servo && (...))                                96.3
        //     bool blinking = servo && (...)                             95.2
        //     SetBlinking(servo ? (...) : false)                         95.2
        //     bool blinking = false; if (servo) blinking = ...           95.8
        //     if (!servo) blinking = false; else blinking = ...          94.7
        //     ... with an extra `bool namesOk = ...; blinking = namesOk;` INERT
```


## `?SetCampaignVo@HamCharacter@@QAAXPBD@Z`

`HamCharacter::SetCampaignVo` -- moved verbatim from `src/system/hamobj/HamCharacter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 549-563):

```text
// 100% (w8-n).  Three levers, in the order they paid:
//  1. `int hasVO = !mCampaignVO.empty(); if (hasVO)` materialises the bool
//     (`subic`/`subfe.`) where the image just tests it: 94.4615 -> 96.9.
//  2. DirLoader::LoadObjects takes `const FilePath&`; letting the `const char*`
//     convert implicitly makes MSVC re-materialise the temp's address
//     (`addi r3, r31, 0x60`) after the ctor already returned it in r3.  Writing
//     the temporary explicitly reuses the ctor's r3: 96.9 -> 98.5.
//  3. RELEASE(x) is `(delete x, x = null)`; MSVC folds the comma form's null
//     test into `delete`'s own and drops the pointer's home store.  The image
//     has `stw r3, 0x50(r31)` at 0x824904E0, i.e. one inline level more, which
//     the SPELT-OUT `if (x) { delete x; x = nullptr; }` restores: 98.5 -> 100.0.
//     REFUTED on the way: dropping `auto &_ref1` for three direct
//     `mCampaignVOBank` reads is 95.4, and binding the deleted pointer to its
//     own local (`Hmx::Object *bank = _ref1; if (bank) ...`) stalls at 98.5 --
//     the null test has to read the REFERENCE, not a copy of it.
```


## `?SongAnimation@HamCharacter@@QAAHXZ`

`HamCharacter::SongAnimation` -- moved verbatim from `src/system/hamobj/HamCharacter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 605-613):

```text
        // w21-ah: MILO_ASSERT_IF (no do/while(0) wrapper), not MILO_ASSERT.
        // The four address constants both asserts share are hoisted to
        // function entry; the image materialises "main" FIRST (lis r11/addi
        // r25 = "main", then TheDebug r29, __FILE__ r28, #cond r27).  With the
        // do/while form here MSVC orders "main" last (4 rows).  MILO_ASSERT_EXPR
        // also fixes the order but moves the Symbol temps from 0x50(r1) to
        // 0x54(r1) (4 other rows); MILO_ASSERT_IF gives both.  No
        // function-local statics here, so the scope-count difference between
        // the forms is moot.  Same behaviour: identical condition and Fail.
```

Inside the body (was lines 618-662):

```text
    // -1 is the FALL-THROUGH result, not an `else if` arm. In the image the two
    // failing tests inside the InClipTest() arm -- `beq cr6, .L_8248E3EC` after
    // `cmplwi cr6, r30, 0x0` (clip null) and after `cmplw cr6, r11, r31` (clip's
    // dir is this) -- both land on the same `li r3, -0x1` that the
    // mUseCameraSkeleton/clip tests reach with `bne`. Spelled as
    // `if (InClipTest() && (c && ...)) ... else if (mUseCameraSkeleton || c)`,
    // a null clip under InClipTest() falls into the SongDriver() arm, which the
    // image can never do. Writing the -1 as an explicit `return -1;` inside the
    // InClipTest() arm fixes the branches but makes MSVC keep the cross-jumped
    // Property()/Int() tail at the SECOND call site instead of the first
    // (96.5 -> 87.6, measured twice: -1-after-Property and -1-before-Property).
    // Inverting the else-if so -1 is the function tail gets both.
    // REFUTED again (w8-n, third spelling): flattening the else-if entirely --
    // `return -1;` at the end of the InClipTest() arm AND a second
    // `if (mUseCameraSkeleton || c) return -1;` with `return 0;` as the function
    // tail -- reproduces the image's BLOCK ORDER (the -1 block early, `return 0`
    // falling through to the epilogue) and still costs 96.1062 -> 87.2, because
    // the shared Property()/Int() tail moves to the second call site.  The four
    // branch-destination rows and the 63/64-vs-111/112 insert/delete pair are
    // therefore ONE cause with the cross-jump, not two independent ones.
    // w21-g (stopped at 96.5 canonical / 96.1 fuzzy, 12 rows): MSVC places a
    // `return -1` block at the function tail in EVERY spelling measured, even
    // when the statement sits textually between the two arms -- the og-dc3
    // chain (`if (InClipTest() && (c && ...)) ... else if (mUseCameraSkeleton
    // || c) return -1; ... return 0;`, which is ALSO behaviourally wrong: a
    // null clip under InClipTest() reaches the SongDriver() arm) still emits
    // `li r3,-1` last, 96.5.  Measured: two `return -1`s (if/else-if, flat
    // ifs, or a `?:` in the InClipTest() arm) all 87.6 -- the merged -1 block
    // goes to the tail and the cross-jump flips to site 2; `if (!InClipTest())`
    // first 7.9; `if (Driver()) c = Driver()->FirstClip()` turns the driver
    // test signed (cmpwi), 95.9; drv/c declaration swap no change.  A cached
    // `bool inClip` with `else if (inClip || mUseCameraSkeleton || c) return
    // -1;` reads 97.2 but only because a redundant `cmplwi r9; bne` re-test
    // the image does NOT have replaces the 63/64 delete pair -- rejected as
    // further from the image, not closer.  The 4 lis/addi rows (image hoists
    // "main" first) moved with none of these.
    // w21-ah (96.5 -> 100 with the assert form above): the image's block
    // order -- `li r3,-1; b` right after the InClipTest() arm's Property() tail
    // (0x8248E3EC), `return 0` last, the shared Property()/Int() tail kept at
    // site 1 -- comes from an explicit `return -1` closing the InClipTest()
    // arm plus TWO separate early-outs `if (mUseCameraSkeleton) return -1;
    // if (c) return -1;`.  The combined `mUseCameraSkeleton || c` test (w21-g's
    // flat-ifs spelling) moves the merged -1 block to the tail and flips the
    // cross-jump to site 2.  Behaviour identical to the previous else-if form:
    // InClipTest() with a null clip or a clip in this dir returns -1.
```


## `?GetPropShowing@HamCharacter@@IAA_NH@Z`

`HamCharacter::GetPropShowing` -- moved verbatim from `src/system/hamobj/HamCharacter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 683-687):

```text
// w21-u: 100.  The element is read twice through ObjPtrVec::operator[], which
// returns the stored pointer by reference (see its comment in obj/Object.h):
// the image's `cmplwi r10 / beq / clrrwi r11, r10, 0 / lbz 0x8(r11)` is that
// double read.  Earlier lanes (w8-i, w21-g) measured the double read only with
// the by-value subscript, where it gives a signed cmpwi (92.38).
```


## `HamCharacter::SetPropShowing()`

`HamCharacter::SetPropShowing` -- moved verbatim from `src/system/hamobj/HamCharacter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Attribution: no function named `HamCharacter::SetPropShowing` in report.json (unit); filed under the source name

Inside the body (was lines 694-699):

```text
    // w21-u: element read twice through the by-reference subscript; this is
    // what takes HamCharacter::SyncProperty (four inlined copies) from 97.93
    // to 99.8.  The image's `lwz r10 / cmplwi r10 / beq / clrrwi r3, r10, 0`
    // per copy is the re-read copied into the argument register, so only
    // `bl SetShowing; b` is cross-jumped (w9-f read that mechanism correctly
    // but measured the double read with the by-value subscript: 97.43).
```


## `?GetNeutralSkeleton@HamCharacter@@QAAPAVObjectDir@@XZ`

`HamCharacter::GetNeutralSkeleton` -- moved verbatim from `src/system/hamobj/HamCharacter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 776-829):

```text
// RESIDUAL (w7-bs, 98.8 canonical / 98.4 raw, 652 B, 9 rows, target
// 0x82491900-82491B8C).  Was 91.66 (w7-bp, 25 rows).  Two of w7-bp's three
// "backend" clusters were source shapes:
//
//  (1) EPILOGUE HOST (was 6 rows).  The image hosts the epilogue in the
//      `return this` block at 0x8249197C-8C and its `return mNeutralSkelDir`
//      is ONE block at the very end (0x82491B84: `lwz r3, 0x334(r30)` /
//      `b .L_82491980`) reached by fall-through from the else arm and by
//      `b .L_82491B84` from the shared map-destructor site 0x82491AF8.  MSVC
//      hosts the epilogue in the return block that the LAST source-order
//      fall-through reaches, so `return mNeutralSkelDir;` as the final
//      statement (every previous spelling) pins it there: 95.1.  Writing
//      the success paths as `goto neutral;` onto a single
//      `neutral: return mNeutralSkelDir;` INSIDE the else arm's success
//      block, with `return this;` as the function's last statement, gives
//      the image's host and its single return block: 98.8.  Measured
//      alternatives: per-arm `return mNeutralSkelDir;` with a final
//      `return this;` hosts the epilogue right but leaves TWO return-value
//      blocks (96.6; 97.5 once each map's scope is closed before the
//      return, because the image reads 0x334 AFTER the dtor); inverting the
//      timing test to `if (clipMap.size() != 0)` flips 0x82491970 to beq
//      (88.3); explicit `return this;` on the else arm's null/flag tests
//      plus a final `return this;` breaks the clear cross-jump (93.8); a
//      trailing `no_clip: return this;` reached by goto is inert (95.1).
//
//  (2) THE clipTimingMap LOOP GUARD (was 6 rows, "refuted" by w7-af as a
//      per-loop backend rotation decision).  It was the function-scope
//      `bones = ...; bones->Zero();` pair on that path: a local assigned and
//      immediately consumed there kept MSVC from cross-jumping the guard into
//      the latch.  `mSkeletonBones->Zero()` on that one path restores the
//      image's `lwz r29, 0x68(r31)` / `b .L_824919CC` at 0x8249199C-A0.
//      Dropping `bones` on ALL paths loses the hoisted `lwz 0x338 /
//      addi r3, r11, 0x10` above the `beq` at 0x82491A00-0C (92.6), so the
//      goto path keeps it.
//
//  Remaining 9 rows, both still open:
//  (3) r26 <-> r27, 7 rows, mClipWeightMap loop only: the image holds the
//      iterator in r27 and `sSkeletonClips` in r26 (`mr r27, r11` at
//      0x82491A34, `addi r26, r11, sSkeletonClips@l` at 0x82491A4C); we hold
//      them the other way round.  Hoisting the iterator declaration out of
//      the `for` is inert; indexing with the inline `Property()->Int()`
//      expression costs a 7th callee-saved register (91.9).
//  (4) 2 rows at zero_and_scale: the image orders the null-select
//      `addi r4, r11, 0x10` BEFORE `cmplwi cr6, r11, 0` (0x82491B2C-30) while
//      the same select at 0x82491A9C-AA0 has cmplwi first (and matches).
//      Ternary, implicit derived-to-base, and inline
//      `*static_cast<CharBones *>(mSkeletonBones)` spellings are all
//      byte-identical.
//  w21-ax (stopped at 99.23313, same 10 rows): re-measured the ternary at
//  zero_and_scale (98.8: the image's addi-then-null-select shape, but cmplwi
//  scheduled first -- 2-row insert/delete); for (3) all inert: `it++`,
//  `weight` declared before `clipEntry`, `totalWeight` declared before the
//  map copy; dropping the clipEntry/weight locals (`it->first`/`it->second`
//  inline) costs a 4th CSR and an FPR (94.5).
```

Inside the body (was lines 923-960):

```text
            // w16-b (98.77, 9 rows: r26/r27 swap + the cmplwi/addi order at
            // 139/141): `*static_cast<CharBones *>(mSkeletonBones)` inline and
            // a plain `CharBones *skBones = mSkeletonBones;` are byte-identical
            // to this spelling.
            // w21-r (98.77 -> 99.2): an if-form null select (`skBones = nullptr;
            // if (mSkeletonBones) skBones = mSkeletonBones;`) puts the cmplwi
            // where the image has it; the image still computes `addi r4, r11,
            // 0x10` speculatively and patches `li r4, 0` on the null arm, we
            // do the reverse (3 rows instead of the ternary's 2-row
            // insert/delete, cheaper on the ruler).  Same value either way.
            // Also measured: `!mSkeletonBones ? nullptr : ...` (same as the
            // if-form), a CharBonesMeshes* local (byte-identical to the
            // ternary), the gender index hoisted into a local first (90.4).
            // The r26/r27 rotation in the mClipWeightMap loop (7 rows) is
            // untouched.
            //
            // w26-p (99.2331, 10 rows; comment only -- no spelling moved it):
            // the residue is one colour swap, r26 <-> r27, between the
            // mClipWeightMap ITERATOR (image: r27) and the `&sSkeletonClips`
            // base temp (image: r26); we have them the other way round, plus
            // the cmplwi/addi order at 139/141 that follows from it.  c2rs
            // `--regasg` says the swap is a TIE-BREAK, not a priority
            // difference: the iterator (cand id 7, pri -5, tie 140) and the
            // sSkeletonClips temp (id 41, pri -4, tie 101) are BOTH in the
            // demote/negative-benefit set, so C2RS-BRIDGE 8.4/8.6's
            // "F = CDEMOTE + negative-benefit frees" ordering -- not the §8.7
            // commutative key -- decides which keeps the lower register.  The
            // key lever was swept and does not move it (re-measured under the
            // pinned name_check ruler after the 2026-11-02 vsearch fix):
            // baseline is 99.2331 canonical / 99.0184 fuzzy, an if-form null
            // select that computes the `addi r4, r11, 0x10` offsets ties it,
            // and every other null-select spelling is WORSE -- the ternary
            // forms drop to 98.7730, `skBones = mSkeletonBones; if
            // (!mSkeletonBones) skBones = nullptr;` to 95.7055, the +0x10
            // variant of that to 91.1656.  Forcing the gender index through a
            // helper is worse again (90.4, spills the map iterator).  Closing
            // this needs an IL-shape change that adds or removes a demote
            // candidate in the loop prologue, not an operand re-key.
```


## `?SetFaceOverrideClip@HamCharacter@@QAAXVSymbol@@_N@Z`

`HamCharacter::SetFaceOverrideClip` -- moved verbatim from `src/system/hamobj/HamCharacter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 1026-1034):

```text
            // w17-b: 96.63 -> 100 (and BlendInFaceOverrideClip 96.73 -> 100).
            // A ternary whose arms are each CONVERTED to ObjectDir* --
            // `opts ? (ObjectDir *)opts : (ObjectDir *)clips`.  The image's
            // cr0 test + dead `stw r11, 0x54(r31)` + `mr r4, r11` is MSVC
            // CSE-ing the ObjPtr conversion written twice (condition and
            // true arm) and still homing it at the second textual site.  The
            // un-cast ternary (w8-n, 92.6) yields an ObjPtr LVALUE and
            // materialises each ObjPtr's address instead; the if-assign form
            // (96.63) never writes the read twice.
```


## `?Poll@HamCharacter@@UAAXXZ`

`HamCharacter::Poll` -- moved verbatim from `src/system/hamobj/HamCharacter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 1153-1158):

```text
    // w22-hc: native runs this same body now. The deleted HX_NATIVE shadow body
    // force-showed a hidden character on EVERY poll (gate reverted, unexplained,
    // in a37908240); the image gates it on mPollWhenHidden (82491400 lbz
    // r31,-0x108(r29); bne; 8249140C lbz r11,0x16c(r29); beq; 82491420 bl
    // SetShowing), so a hidden character (iconman) only polls inside
    // HamDirector::PoseIconMan's SetPollWhenHidden(true) bracket.
```

Inside the body (was lines 1233-1259):

```text
    // CLOSED (w37-c, 99.13386 -> 100.0 canonical AND fuzzy, 1020 B -> 1016 B).
    // The w9-f residual was that at both Find sites the image emits the
    // RECORD-FORM move `mr. r4, r3` (8249167C and 82491698), copying the result
    // into r4 -- SetDiffuseTex's argument register, since the call inlines to
    // `SetObjConcrete(&mat->..., tex)` with r3 = 0x40(mat) -- and testing it in
    // the same instruction, while we tested in place with `cmplwi r3, 0x0` and
    // deferred ONE `mr r4, r3` to just before the call (255 instructions against
    // the image's 254).  The cause is the MERGE: `tex` was one variable with two
    // definitions joined at SetDiffuseTex, so c2 gave the merged value r3 and
    // copied once at the join.  Duplicating the use per arm -- one
    // `mat->SetDiffuseTex(tex)` on each successful Find, with the fallback Find
    // and the notify nested under the failure arm -- removes the merge variable,
    // each arm homes the result in r4 itself, and c2 cross-jumps the two calls
    // back into the image's single block.  Exactly one SetDiffuseTex call per
    // successful Find on both spellings, same order, same arguments: behaviour
    // unchanged.
    // REFUTED (w9-f): folding the two finds into one condition,
    //   if (!(tex = Find(texName)) && !(tex = Find("base.tex"))) notify; else set;
    // is far worse -- 99.13386 -> 94.4, 3 rows -> 17 -- because the && chain moves
    // the whole SetDiffuseTex block past the MILO_NOTIFY_ONCE block and inverts two
    // branches.  Keep the nested-if form.
    // (Aside, worth knowing: the target names that callee
    // SetObjConcrete<AnimTask, ObjectDir> and we name it
    // SetObjConcrete<RndTex, ObjectDir>.  That is an ICF fold -- the two bodies are
    // identical -- and OUR name is the correct one for this call site.)
    // w16-b: inverting the last test (`if (!tex) notify; else set;`) is 93.5,
    // 15 structural rows -- refuted.
```
