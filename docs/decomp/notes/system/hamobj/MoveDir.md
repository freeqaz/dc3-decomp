# Lane notes: `src/system/hamobj/MoveDir.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DrawDetectedBar@?A0xe50ea9df@@YAMMPBDMMM_N1@Z`

`DrawDetectedBar` -- moved verbatim from `src/system/hamobj/MoveDir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 102-163):

```text
            // The image rebuilds each colour from its STATIC, not from the local
            // it just copied (824FCE64.. loads off r11/r10/r7, the addresses of
            // sDarkerGray/sGreen/sLightGray), scaling r/g/b and re-storing alpha
            // (824FCEC8: 0xc(r11) straight to 0x7c).  `*= 0.5f` on the locals
            // reads the copies back (86.8); a `Hmx::Color(...)` temporary
            // assigned over the local costs a 16-byte copy (57.5).
            // RESIDUAL (94.5): the image also parks each alpha in the temp slot
            // 0x50(r31) before storing it (824FCEB0, 824FCEBC, 824FCEE4) and
            // schedules the nine products b,g-first; binding alpha to a
            // `const float &` through an inline helper is byte-identical to
            // this spelling, so that home store is not a by-ref temp.
            // UPDATE (w7-bq, 2026-09-15): 94.5 -> 94.9.  The three `Set(r,g,b,a)`
            // calls were one inline level too many (fixable-inline-boundary.md,
            // "target assigns the components directly").  Writing the twelve
            // fields directly removes the `replace` row at idx 71 outright
            // (22 diff_arg / 1 replace / 2 ins / 5 del -> 30 diff_arg / 0
            // replace / 2 ins / 5 del) and drops OFFSET_SWAP to 3.
            // NEGATIVE, same day: permuting the per-colour assignment order to
            // blue,green,red,alpha -- the order the image ISSUES its loads --
            // also reads 94.9 but with OFFSET_SWAP 7 and 10 PERMUTED slots
            // (vs 3 and 8), i.e. MSVC schedules this block independently of
            // statement order.  The r,g,b,a spelling below is the better of
            // the two at the same score, so it stands.
            // RESIDUAL (94.9): target 140 instructions vs base 137, and the gap is
            // exactly the three dead `stfs fN, 0x50(r31)` alpha parks -- 0x50 is
            // the shared float-temp slot that `MakeString(": %.2f%%", detected *
            // 100.0f)` takes the address of at idx 96.  Nothing ever reads those
            // three stores back (no `addi rX, r31, 0x50` between them), so they
            // are dead materialisations of a float temporary.  The rest of the
            // residual is 30 diff_arg rows that are pure FPR renaming
            // (f11<->f12, f7<->f8, ...) plus load scheduling inside this one
            // block; regions 0-56 and 90-141 are 100%.
            // w21-bl (94.9, same 30 diff_arg / 2 ins / 5 del): standalone
            // cl.exe probe of this function. darkColor.Set(r*.5f, g*.5f, b*.5f,
            // a) reproduces the image's LOAD order exactly up to the red
            // loads (b,g per colour first -- args evaluate right to left) but
            // also gives none of the three 0x50 alpha parks; Set with
            // const float & params, a (float) cast rvalue alpha, an inline
            // Halve(out, in) helper, and `x = Hmx::Color(...)` (three 16-byte
            // temp copies) all give no park either. The parks share the
            // MakeString temp's slot, which is address-exposed, so they are
            // stores of a float temp that survived until stack packing; no
            // spelling found that creates one. Field writes stay (best score).
            // w29-b (94.88611, base 137 vs target 140): re-derived the delta --
            // the image's 3 extra instructions are exactly `fmuls f8,f8,f0` and
            // `fmuls f7,f7,f0` plus the third park, and the whole colour block
            // sits 4 bytes lower in the image (0x74..0x8c) than in ours
            // (0x70..0x88), i.e. the image has one MORE 4-byte float local
            // between 0x50 and the colours -- which is what the parks are
            // storing to.  So the missing thing is a named float local that the
            // image assigns three times, not a temp; c2's frame allocator will
            // not invent one, and the parks share slot 0x50 with the address-
            // exposed MakeString temp, so the two are told apart only by the
            // separate 0x70 store.  Field writes stay.
            // w43-b (2026-11-06): 94.88571 -> 94.90000 canonical, 93.52857 -> 94.11429 fuzzy,
            // diff_arg 30 -> 21, EQUAL rows 105 -> 114, body unchanged at 137 (image 140), from the
            // ASSIGNMENT ORDER alone.  A named float assigned three times -- the "named float
            // local" hypothesis above -- is byte-inert; a 64-way search over where each colour's
            // alpha assignment sits is what moves it, and this placement (alpha first for the dark
            // colour, alpha third for the other two) is the best of the 64.  MSVC schedules this
            // block independently of statement order, so the order is a register-pressure handle.
            // The three parks are still missing, so the body stays 3 short of the image.
```


## `?RecordClipName@@YA?AVString@@PBDH@Z`

`RecordClipName` -- moved verbatim from `src/system/hamobj/MoveDir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 254-281):

```text
    // w20-e (93.05 -> 99.9): the image calls ToCode, then copies
    // TheGameData->mSong into a stack temp (`lwz r10, 0x30(r11)` /
    // `stw r10, 0x58(r31)`), and only THEN calls Player(0) -- with the
    // dancer String address taken straight off r3 (`addi r8, r3, 0x2c`)
    // before r3 becomes the format string.  Named locals in that order give
    // exactly that; as inline MakeString arguments MSVC evaluated Player(0)
    // before GetSong().  Residual 5 rows: the image sets up r9 (&cc) LAST,
    // we set it first; same values (pure argument-setup schedule).
    // w21-aw (stop at 99.94, same 5 addi rows): Player(0)->CurrentDancer()
    // inline in the argument list -> 98.8 (adds a mr r11,r3 save and moves
    // the format-string lis); a `const char *name = cc;` alias -> 96.4 (new
    // slot, cc no longer homed).  name_check charges no relocation here.
    // w26-d (99.94, same 5 addi rows): re-read both listings -- the
    // register->slot mapping is identical (r4 0x54, r5 0x5c, r6 0x58, r7 0x50,
    // r9 0xbc); only the emission order differs, the image deferring `&cc` (r9)
    // past the four stack temps we set up in reverse-argument order.  c2
    // mechanism: &cc is the one argument whose leaf is a parameter home rather
    // than a temp, so it is emitted in the parameter's prop order.  Re-measured
    // inert on the current tool: `(const char *)cc`, `(cc)`.
    // w28-a (still 99.943): the residual census calls this row IDENTICAL
    // (order=opcode-order), and the five rows are ONE ROTATION of the same five
    // address computations.  The image emits r7,r6,r5,r4 then r9 (&cc); we emit
    // r9 first and then r7..r4.  That is exactly the c2rs 12.3 first-store
    // deferral performed by the image and not by us, and `&cc` is the only
    // element of the run that is a parameter home rather than a MakeString
    // stack temp, which is what makes it deferrable.  Reordering the
    // `code`/`song`/`dancer` declarations is much worse (90-94), so the
    // argument list itself, not the store order, is the local optimum.
```


## `?Poll@MoveDir@@UAAXXZ`

`MoveDir::Poll` -- moved verbatim from `src/system/hamobj/MoveDir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 717-765):

```text
// w21-aj (stopped at 98.24, 50 rows; one register-pressure choice: ours
// hoists `lis TheMaster@h` into r14 and therefore spills the &TheTaskMgr
// anchor to 0x50, the image keeps &TheTaskMgr in r14 and rematerialises
// TheMaster@h per iteration; 17 callee-saved values vs our 18).  Inert in a
// /FAs probe: a `TaskMgr &taskMgr = TheTaskMgr` local, a dead `float frac =
// 0` hoisted to loop scope, parenthesised / inverted ternary condition, a
// do/while loop.  Worse: reading mCurMoveNormalizedResult[i] into a local
// before Smooth (97.0 probe vs 97.7 probe baseline).
// w26-t: the "17 vs 18" is now measured, not inferred.  `c2rs stage cands
// --regasg` on this function prints 22 code-band mints and 17 demotions
// (C-F = 5) and exactly 18 candidates whose allowed set is nonvolatile-only,
// against 17 allocatable callee-saved registers (r31 is the frame base, so the
// pool is r14..r30).  Pop order, highest first, with the register each took:
//   id 2  pri 120 tie 14  (this)        -> r30
//   id 12 pri  38 tie  0  code#85       -> r29
//   id 40 pri  27 tie 108 arena#41      -> r28  <- &TheTaskMgr, spills to 0x50
//   id 9  pri  27 tie  0  code#87       -> r27
//   id 8  pri  27 tie  0  code#88       -> r26
//   id 4  pri  25 tie 214 arena#5       -> r25
//   id 45 pri  14 tie 228 arena#46      -> r30 (reuse)
//   id 5  pri  -5 tie  0  code#89       -> r24
//   id 27 pri -22 tie 19  arena#28      -> r23
//   id 14 pri -25 tie  0  code#83       -> r22
//   id 1  pri -41 tie  0  k0d#0         -> r21
//   id 48 pri -44 tie 27  arena#49      -> r20
//   id 50 pri -44 tie 25  arena#51      -> r19
//   id 43 pri -46 tie 30  arena#44      -> r18
//   id 47 pri -48 tie 28  arena#48      -> r17
//   id 49 pri -48 tie 26  arena#50      -> r16
//   id 42 pri -52 tie  0  code#78       -> r15
//   id 13 pri -54 tie  0  code#84       -> r14  <- TheMaster@h
// w26-d saw the same candidate from the allocation tap, which adds its fields:
//   `CAND sel fn 974 id 13 pri -54 tie 0 ben 3 ref 2 allowed { r14 }`
// -- a single-register allowed set, i.e. it takes the last free register and
// leaves nothing for &TheTaskMgr, which must then go to 0x50.
// So the mechanism hypothesis is: `TheMaster@h` is the last-popped code-band
// half (lowest priority) and takes the last register, which pushes the pool
// one over budget; the image drops that ONE candidate and rematerialises the
// `lis` per iteration instead.  Reaching it means removing one cross-call
// value (or making the code band stop minting that half) -- not a spelling of
// the ternary.
// Measured inert in the replay harness (97.2402 fuzzy = baseline): a named
// `TaskMgr &taskMgr` binding, a named ternary result feeding Smooth, a named
// result plus a named delta.  Worse: a named `HamMaster *master` (97.72 canon),
// a named mCurMoveNormalizedResult[i] (97.55), a named bool for the beat test
// (96.04), and calling DeltaUISeconds once with a `*=` arm (93.42).
// w26-d re-measured the section 8.6 "+1 demotable named local" lever here:
// a named `HamMove *curMove = mCurMove[i]` costs 96.72 -> 95.28 in the replay,
// and dropping `oldMove` or the curPlayerData reference were inert or worse.
```

Inside the body (was lines 778-789):

```text
                // REFUTED (w9-d, BYTE-IDENTICAL): binding the element to a
                // `const HamMoveKey &key` first, to coax out the image's
                // `stw r11, 0x50(r31)` home store of mMoveKeys._M_start where we
                // emit a plain register move `clrrwi r10, r9, 0`, leaves the
                // object unchanged -- canonical 97.9607, fuzzy 96.41048, base
                // 924 B, the same 62 rows (56 diff_arg / 1 diff_op / 1 replace /
                // 1 delete / 3 insert) before and after.  The two charged rows
                // in that window are the LOAD ORDER: the image reads _M_start
                // (0x20(r10)) before _M_finish (0x24(r10)) and then reuses
                // _M_start for the indexed load; we read _M_finish first and pay
                // a move to get _M_start into place.  Not reachable from the
                // subscript spelling.
```

Inside the body (was lines 808-849):

```text
            // w21-o: binding `std::vector<HamMoveKey> &keys` above fixed the
            // _M_start-first load order and produced the image's _M_start home
            // store (it lands at 0x54 because our TaskMgr spill still takes
            // 0x50).  Left: the TheMaster@h hoist / TaskMgr spill below and the
            // f30/f31 swap of 4.0f/0.0f, i.e. one register-pressure choice.
            // Inert: the ternary in a `float dt` local; curPlayerData bound
            // before oldMove.  Worse: no curPlayerData (97.8).  decomp-synth
            // beam depth 1 (19 proposals, 16 built): none above baseline.
            // REFUTED (w7-i): binding TheMaster to a local `master` here costs
            // 0.56pp -- it flips the null-check branch polarity (2 extra
            // beq<->beq replace rows) without touching the two rows that
            // actually differ: our base-only `stw r28, 0x50(r31)` spill of the
            // &TheTaskMgr anchor and our base-only `lis r14, TheMaster@h`
            // loop-invariant hoist, which the image rematerialises per
            // iteration at 82500B4C.
            // w27-e (still 98.245, canonical unchanged, 9 spellings): mechanism --
            // two loop-invariant addresses compete for callee-saved GPRs,
            // `&TheTaskMgr` (code-band `addi`) and `TheMaster@h` (code-band `lis`).
            // The image keeps `&TheTaskMgr` in r14 and rematerialises the `lis`
            // per iteration; we keep `TheMaster@h` in r14, put `&TheTaskMgr` in
            // r28 and pay a `stw r28, 0x50(r31)` / `lwz r28, 0x50(r31)` spill
            // pair around DetectFracToRatingFrac.  That single choice is the
            // whole residual: every other row is the register renaming it forces.
            // Inert on it: reference-binding `TheTaskMgr` (c2rs 8.8 global-object
            // lever) at function and at if-block scope, reference-binding
            // `TheMaster`, and naming the DeltaUISeconds result.  Worse: naming
            // the ternary predicate (-2.3pp).
            // w28-a (same 50 rows, 6 more spellings; confirmed from the c2rs tap):
            // the two loop-invariant addresses are the last two candidates popped
            // and each is allowed only one register --
            //   id 13 pri -54 tie 0 ben 3 ref 2 sym 78/k03  allowed { r14 } -> r14 (cost r14:500)
            //   id 52 pri -61 tie 20 ben 3 ref 4 split1#96  allowed { r28 } -> r28
            // id 13 is the hoisted `lis TheMaster@h`: our scheduler sinks that lis
            // to the top of the loop body (row 36) ahead of its use (row 110), and
            // the calls between force it into a callee-saved register.  The image
            // emits it at the use and keeps nothing.  Freeing id 13 would hand r14
            // to &TheTaskMgr and delete the `stw`/`lwz r28, 0x50(r31)` spill pair,
            // which is exactly the structural delta the residual census reports
            // (surplus ours lwz:1 stw:1; every other row is the renaming it
            // forces).  Inert this lane: `TheMaster != nullptr`, and
            // `HamMaster *&master = TheMaster;` / `TaskMgr &tm = TheTaskMgr;`
            // bound at the Smooth call site (all byte-equal).
```


## `?Enter@MoveDir@@UAAXXZ`

`MoveDir::Enter` -- moved verbatim from `src/system/hamobj/MoveDir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 873-896):

```text
// w21-aj (still 99.63, the dead `stw r11, 0x50(r31)` row): /FAs shows 0x50
// is the second loop's `i` (homed for MakeString's const T&) and the fctiwz
// temp; the image stores numKeys there on the Easy path only.  Inert:
// sharing ONE function-scope int between keyIdx and that loop's i (all its
// keyIdx stores still dead-eliminated), MaxEq(i13, numKeys), i13 = Max(..),
// `int tmp = numKeys;` hoisted above the while and reassigned by the
// conversion, and reusing numKeys itself as the conversion result.
// w21-o: 99.27 -> 1 row left (the filters block is SetFiltersEnabled(true),
// which /O1's /Ob2 inlines -- the image's `subi r11, this, 0x1ac` / `mr r3, r11`
// is the inlined callee's this copy; closing it removed 35 rows).  Last row:
// the image's dead `stw r11, 0x50(r31)` of numKeys right after `subic. r28`
// (keyIdx = numKeys - 1); 0x50 is later the fctiwz temp and MakeString's `i`
// home.  Measured inert: plain `for (keyIdx = numKeys - 1; keyIdx > 0;
// keyIdx--)`, `keyIdx = hamMoveKeys.size()`, a `(int)` cast in place of
// `tmp`.  Worse: std::max for i13 (97.1), indexing without the curKey ref
// (98.5).
// w26-d (99.63): the other five name_check rows are ICF folds, not wrong
// callees -- MakeString<W4_D3DFORMAT> and MakeString<H> share 82610090, and
// RadFree / ??3 / ??_V / OggFree share 82E21268 in icf_aliases.map.  So the
// dead store above is the whole residual: the image gives the loop counter a
// stack home at 0x50 (a home the fctiwz temp later reuses, so the store is
// dead there too), our c2 keeps it in r28 with no home.  Re-measured inert:
// numKeys and/or keyIdx declared in the outer block or at function scope,
// reusing numKeys as the counter, for-loop pre-decrement.
```


## `?PostUpdate@MoveDir@@UAAXPBUSkeletonUpdateData@@@Z`

`MoveDir::PostUpdate` -- moved verbatim from `src/system/hamobj/MoveDir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 1025-1034):

```text
                // BEHAVIOURAL FIX (w8-n).  mSkeletonsRight is a `Skeleton **`
                // (gesture/Skeleton.h:128), so the array of pointers is what it
                // POINTS AT.  Casting the member lvalue to a reference-to-array
                // handed GetSkeleton `&data->mSkeletonsRight` -- the struct field
                // itself -- so it read mSkeletonsRight/mFrame/mHistory/
                // mCameraInput and two words PAST the 0x14-byte struct as if they
                // were Skeleton pointers.  The image loads the pointer's VALUE:
                // `lwz r31, 0x4(r30)` / `mr r4, r31` at 0x8250532C..0x82505334,
                // where we emitted `addi r4, r30, 0x4`.  Game.cpp:192,194 already
                // use the correct pointer-to-array-then-deref idiom.
```

Inside the body (was lines 1045-1056):

```text
    // BEHAVIOURAL FIX (w9-d).  We had `!mFiltersEnabled || (mCurMove &&
    // mCurMove->IsRest())`, which SKIPS the reset when filters are enabled and
    // there is no current move.  The image resets there: at 0x82505370 it does
    // `cmplwi cr6, r3, 0x0` / `beq cr6, .L_82505384` -- and 0x82505384 is the
    // mFeedback/ResetErrors block, not the loop bottom.  Only the IsRest test
    // at 0x8250537C..0x82505380 branches PAST it (to .L_82505394).  So the
    // condition is `!mFiltersEnabled || !mCurMove || mCurMove->IsRest()`: a
    // player with no move for this measure gets its error accumulators cleared.
    // Naming the pointer in a local also reproduces the image's unconditional
    // load at the top of the loop body (0x82505364, ahead of the
    // mFiltersEnabled compare) and the UNSIGNED `cmplwi` pointer test we were
    // emitting as a signed `cmpwi`.
```


## `?FinalPoseStateMachine@MoveDir@@QAAXXZ`

`MoveDir::FinalPoseStateMachine` -- moved verbatim from `src/system/hamobj/MoveDir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 1355-1383):

```text
    // RESIDUAL (w7-ba, 99.95): the image updates the loop's three memory-homed
    // walkers other_player, &mMovePlayerData[i].mFeedbackMode, &other.mFeedbackMode
    // (82503FB0..82503FD0: 0x54, 0x50, 0x58); we update 0x54, 0x58, 0x50.  Inert:
    // other_player declared after `move` or at its first use, `[1 - i]` in place
    // of `[other_player]`.  Do NOT bind `mMovePlayerData[i]` to a reference --
    // it then lives in a register and the walker base moves to 0x318 (95.6).
    // w21-aw (stop at 99.95, same 6 rows 270-278): a `MovePlayerData &other =
    // mMovePlayerData[other_player];` declared at the top of the body and used
    // in the inner test is inert.  name_check's MakeString<char[19],int,char[5]>
    // vs our <char[12],int,char[46]> call row is an ICF fold, not a bug: both
    // names sit at 824d1870 in ham_xbox_r.map, and the args (r17 = "MoveDir.cpp",
    // r16 = the other_player assert text) are identical on both sides.
    // w26-d (99.95, rows 270-278): the six rows are one emission-order swap of
    // two independent memory-homed walkers -- the image does `lwz 0x50 / addi
    // +0x3c` then `lwz 0x58 / subi -0x3c`, ours the reverse, with r9/r10
    // swapped to match.  Each slot receives the same update on both sides, so
    // the loop state is identical.  c2 orders the pair by candidate id; both
    // hoisted lis loads are `allowed { r14 }`, and the source shapes that could
    // reorder them (other_player's declaration position, a MovePlayerData
    // reference for the other player) are inert -- re-measured this lane.
    // w28-a (still 99.951): the residual census reads STRUCTURAL, but the
    // opcode multiset is IDENTICAL (surplus == deficit: addi 3, bl 2, lfs 1,
    // lis 6, lwz 4, stw 3), so the six rows are pure emission ORDER.  The
    // latch updates the i-walker (0x50, +0x3c) and the other_player-walker
    // (0x58, -0x3c) in the opposite order and r9/r10 follow; each slot gets
    // the same delta on both sides.  Byte-identical: moving `other_player`
    // below its uses, and an uninitialised decl plus a later assignment.
    // Worse: `i ^ 1` (89.5 -- the walker needs a linear induction variable),
    // and both counters in the for-increment (99.68).
```


## `?ResetDetectFrames@MoveDir@@QAAXHW4Difficulty@@@Z`

`MoveDir::ResetDetectFrames` -- moved verbatim from `src/system/hamobj/MoveDir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 1535-1539):

```text
                // w21-m: binding `moveKeys` once (before prevCapacity, used
                // for every mMoveKeys access) closes both scheduling ties w7-i
                // recorded here -- the capacity()-before-size() preheader and
                // the back-edge that keeps _M_start live into the loop head.
                // Binding it only at the loop left the preheader tie (99.1).
```


## `?ClosestMoveFrame@MoveDir@@AAAPAVMoveFrame@@XZ`

`MoveDir::ClosestMoveFrame` -- moved verbatim from `src/system/hamobj/MoveDir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 1758-1764):

```text
    // w8-b: the local class MUST be declared here, after the null check, not at
    // the top of the body.  MSVC mangles a local class with the function's
    // lexical-scope counter, which starts at 2 for the body and is bumped +2 by
    // the unbraced `if (...) return nullptr;` above -- so the image's
    // min_element instantiation is ...@UFilterFrameDist@?3??ClosestMoveFrame...
    // (?3 = 4) at 0x824FC160, while declaring it first gives ?1 (= 2) and the
    // instantiation never pairs.  See docs/decomp/patterns/fixable-scope-index.md.
```


## `?EnqueueDetectFrames@MoveDir@@QAAXMHAAV?$vector@VDetectFrame@@V?$StlNodeAlloc@VDetectFrame@@@stlpmtx_std@@@stlpmtx_std@@PBVFilterVersion@@@Z`

`MoveDir::EnqueueDetectFrames` -- moved verbatim from `src/system/hamobj/MoveDir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 1857-1863):

```text
// w25-gi: no `if (range.first != range.second)` around the loop. The loop's
// own entry test is the range test (the empty-range path still reaches the
// end with best == nullptr). With the wrapper, player (r25) and filterVer
// (r26) and range.second (r27) / &filterVer->mScaleOp (r29) came out swapped
// against the image: the extra join block lowered range.second's colour
// priority from 16 to -3, below the mScaleOp temp, and left player at -15
// under filterVer's -11.
```


## `?DetectFrac@MoveDir@@QAAMHH@Z`

`MoveDir::DetectFrac` -- moved verbatim from `src/system/hamobj/MoveDir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 2039-2046):

```text
// w19-b: 93.3553 -> 100.  The w8-n "block sinking, believed unfixable" note is
// refuted.  The image's zero block sits between the tests and the success path
// because the original selects the move first -- `move = nullptr`, set from
// keys[beat] only when beat is in range -- and THEN tests `if (!move)`.  MSVC
// jump-threads the two range failures straight into the null arm, which is the
// fall-through of the null test (`bne` to the DetectRange path).  Both a single
// `||` condition with the zero arm first (92.0) and the w8-n early-return
// spelling (92.0) still sink the zero block to the tail.
```


## `?UpdateOverlay@MoveDir@@UAAMPAVRndOverlay@@M@Z`

`MoveDir::UpdateOverlay` -- moved verbatim from `src/system/hamobj/MoveDir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 2102-2162):

```text
    // NOTE (measured 2026-08-22, name_check ruler): the shipped image calls these two
    // in the OPPOSITE order to this source. Target asm proves it: `bl CurrentMoveMode`
    // sits here (idx 84, its result spilled to 0x68(r31) and reloaded as the `mode`
    // argument of NodeWeightHam1 at idx 630), while `bl ClosestMoveFrame` sits ~380
    // instructions later, immediately before `bl HamMove::GetMoveFrames` (idx 465, kept
    // in r16). Applying that order DOES close the reciprocal WRONG_CALLEE pair and
    // aligns the whole call sequence -- but MSVC then flips the basic-block layout of
    // the `mergeVal` if/else below (the non-merged branch stops being sunk past the
    // `if (closest)` block) and the function falls 88.685 -> 70.7. Isolated: hoisting
    // CurrentMoveMode alone is what flips it (69.6); sinking ClosestMoveFrame alone is
    // 87.6; hoist + inverting the mergeVal if/else to try to recover the layout is 64.1.
    // All four reverted. Do not retry any of those four; a recovery needs a different
    // mechanism for the block-sinking heuristic.
    //
    // w26-j (2026-11-01) behaviour verdict re-confirmed: unicorn records EQUIVALENT for
    // this symbol (harness v4, high confidence), so the reciprocal ClosestMoveFrame /
    // CurrentMoveMode WRONG_CALLEE pair is codegen, not a swapped side effect. No wrong
    // constant either -- the single `__real@3f7d70a4` vs `__real@40800000` row at idx
    // 154 is an alignment slip, both literals are present one row apart. All 55 offset
    // rows resolve to frame slots or a different base register.
    //
    // w29-b (2026-11-02): 88.233 / fuzzy 86.762, target 1268 instructions vs base
    // 1258 -- we are TEN SHORT, so bridge 14 says any remaining gain must be
    // checked against the count, not just the score.  The c2-rs candidate tap
    // attributes a large share of the diff_arg rows to one candidate:
    // `CAND id 4 pri -130 tie 0 ben 16 ref 24 sym 3/k0d val k07 #00000000
    // allowed { r14 }` -- the INTEGER ZERO is hoisted to a function-wide
    // callee-saved candidate in r14 (one `li r14, 0` at idx 155, then 25 uses:
    // Vector3/Color zero-stores at 0x60/0xd0/0xe0/0xf0, `mr rN,r14` pointer
    // inits, `stb` for a bool).  It displaces `fv` (move->FilterVer()), whose
    // FORMu[0] index is SPLIT (`split1#461`, sym 10/k04, pri -9 -> r22) and
    // home-stored to 0x7c, then reloaded at idx 561/612/809/962 -- that is the
    // 4-byte frame shift (sret 0xb4 here vs 0xb0 in the image, which keeps the
    // value in a register and rematerialises its zero 34 times instead of
    // hoisting it: the target has 34 `li rN, 0`, we have 28 of which one is the
    // shared r14).  This is bridge 15(b): a loop-invariant value our scheduler
    // hoists to the block top becomes a callee-saved candidate and displaces
    // another.  There is no source-level handle for "do not hoist constant 0" --
    // replacing nullptr with 0 at all 21 sites leaves the tap byte-identical.
    //
    // w-md-overlay (2026-11-10): 89.592 / fuzzy 88.38171, 723 equal of 1318, target
    // 5072 B vs base 5032 B (was 88.185 / 86.715, 681 equal of 1326).  Landed:
    // the `while (it != endFrame)` scan (the image compares the lower_bound
    // iterator with `beq`/`bne` cr6, which c2 only emits for an equality loop),
    // dropping the `_ref0` alias and the `playClip` local, folding `idx++` into
    // the for-increment, `yBase` declared before `height`, and `minHeight` as the
    // literal `(0.2f < 1.0f - y) ? ... : 0.2f` (exactly Max(0.2f, 1.0f - y) for
    // every input including NaN, but it is what moves 14 rows).
    //
    // Refuted since, each measured alone against the state above:
    //  - `(&frame - closest) != 0` for `&frame == closest` (to steer the image's
    //    `subf r11,r16,r28` + `cntlzw`/`extrwi.` + `beq`): 88.14, equal 702.
    //  - `(0.2f - (1.0f - y) >= 0.0f) ? 0.2f : (1.0f - y)`: this DOES reproduce
    //    the image's lone `fsel f30,f12,f0,f13` (idx 826, closes 4 rows there)
    //    but loses 15 elsewhere -- 88.93, equal 696.  The branch form scores
    //    better than the image's own local shape, so keep it.
    //  - swapping the multiply operands of `frameF` (and hoisting `beatOffset *
    //    0.25f` into its own local) to pull it ahead of `bl GetDancerFrames`
    //    like the image: byte-identical output -- c2 canonicalises commutative
    //    operands for CSE, so there is no source handle for the ordering.
    //  - `fv`/`handle` declaration swap: equal 693 vs 695.
```

Inside the body (was lines 2639-2644):

```text
        // BEHAVIOUR FIX (w22-a00): the rotation line goes at the y the latency
        // line's DrawStringScreen RETURNS, not at debugRect.y + 0.02f.  Image:
        // `bl DrawStringScreen` / `mr r30, r3` (0x82502308/0x8250230C), then
        // `lfs f0, 0x4(r30)` (0x82502318) stored as the new Vector2's y
        // (`stfs f0, 0x194(r31)`, 0x82502334); x is debugRect.x (0x110(r31)).
        // There is no 0.02f constant in the image's body.
```

w-low-ham-math (2026-11-13), lane D:

```text
BEHAVIOUR FIX, five values (read off 0x82500FC8):
  - Ham1 per-node boxes: UtilDrawRect2D(v1 = (x - halfWidth, top), v2 =
    (x + halfWidth, top + line)), v1 at 0xf0(r31) with v1.y advanced per row.
    We passed (bx1, bx2) / (colY + line, colY), mixing x and y.
  - Beat lines 0..4 are sGray (r26, the same register as the "%i" label).
  - startSeconds = BeatToSeconds((float)measureStart - rangeOffset)
    (fsubs f1, f19, f0), not beatOffset - rangeOffset.
  - Yellow beat label prints beatOffset (0x80(r31) = f17), not totalBeat.
  - Viz rect and debug rect backgrounds are sDarkGray (r24 <- 0x60(r31) =
    lbl_82F0EB18), not sLightGray.
Unicorn's EQUIVALENT verdict never saw any of these: they are all draw-call
arguments. Then codegen-only: 3-arg lineColor.Set (the image never writes
alpha; native pins it to 1) and textPos = DrawStringScreen("asyc") (dead
store in the image). Net 89.640 -> 90.0 canonical, equal 723 -> 746.
Checked and matching: asserts, mirrored, sLineHeight init, play clip,
version label, threshold lines, detected bar args, timer bar, Ham1 headers,
marker colours, detect-range cells and errColor, merge-branch frame index,
error curve, node index text, grid layout, latency/rotation text, return.
```
