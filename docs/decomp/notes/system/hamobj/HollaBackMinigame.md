# Lane notes: `src/system/hamobj/HollaBackMinigame.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetDefaultShot@HollaBackMinigame@@QAAXXZ`

`HollaBackMinigame::SetDefaultShot` -- moved verbatim from `src/system/hamobj/HollaBackMinigame.cpp` on 2026-11-13 (lane w-five-clean; line numbers are as of the commit before the move)

Above the definition (was lines 440-469):

```text
// Lane w7-ag, 2026-09-14: the 4 remaining inserts are one MSVC anchoring
// choice, repeated once per branch.  The image keeps only the HIGH half of
// ?TheHamDirector@@3PAVHamDirector@@A in r30 and loads through it everywhere,
// including the GetVenueWorld() call after the if/else:
//   build/373307D9/asm/system/hamobj/HollaBackMinigame.s
//     824EA530  lis r30, ?TheHamDirector@@...@ha
//     824EA540  lwz r11, ?TheHamDirector@@...@l(r30)     ; in-branch
//     824EA78C  lwz r3,  ?TheHamDirector@@...@l(r30)     ; after the join
// We materialise the FULL address instead -- `lis r11, @ha; addi r30, r11, @l`
// in every one of the four branches, then `lwz r3, 0x0(r30)` at the join --
// so each branch carries one extra addi.  Splitting the tail into
// `WorldDir *venue = TheHamDirector->GetVenueWorld();` + Find<> is completely
// inert (identical 19 rows), and there is no expression in this function whose
// spelling chooses between the @ha anchor and the full address: the four
// branch bodies and the join already read the global exactly once each, which
// is what the image does.  Permuter-class residual; 98.09524.
// w21-k (still 98.10, same 19 rows: 4x lis/addi/lwz anchor triples + the
// join `lwz r3, 0x0(r30)` + one f1/f2 fmr order pair in the Animate call):
// behaviour re-checked against the listing (same four shots, same
// Handle slot through the virtual base, Animate(0, false, 0, nullptr,
// kEaseLinear, 0, false)).  decomp-synth beam search (3 depths, ~310
// builds, 1480 s) found nothing above baseline; a `static inline
// ForceShot(const char *)` helper was not inlined (26.6).  Stopped here.
// w21-au (still 98.10, same 19 rows): six more spellings, all byte-inert
// (98.09524): a __forceinline ForceShot helper (it does inline -- same
// four-block shape, same full-address PRE into r30); the tail in a
// __forceinline helper; `HamDirector *&dir = TheHamDirector;` for every
// access; `RndPropAnim *anim;` hoisted to function scope; the whole tail
// chained in one expression (98.095 now, not the 90.2 w16-b measured).
// Behaviour re-checked: same four shots, same Animate args.  Stopped.
```

Inside the body (was lines 488-519):

```text
    // w16-b (98.10, 19 rows): the image keeps only `lis r30, TheHamDirector@h`
    // and loads `@l(r30)` where we materialise the full address in r30, in all
    // four Handle arms. Chaining this lookup without the `anim` local is 90.2.
    // w27-e (still 98.095, the same 19 rows, 13 spellings): mechanism -- the two
    // forms c2 can pick are `lis @h` into a callee-saved GPR with a symbol-
    // relative `lwz sym@l(rN)` at each use (the image), and a MATERIALISED
    // address (`lis` + `addi`) that the allocator rematerialises in every block
    // that spans a call (ours; the `addi` is dead in three of the four arms and
    // the fourth feeds `lwz r3, 0(r30)` at the GetVenueWorld site).  Inert on
    // all of it: reference-binding the global pointer at function scope, at the
    // arm scope, and just before the final lookup; a named copy (-0.9pp);
    // `((HamDirector *)TheHamDirector)`, `TheHamDirector[0]`, a named venue
    // pointer, and hoisting `anim`'s declaration.  Open question: which IL shape
    // makes c2 keep the @h half symbolic instead of materialising the address.
    // w28-a: the delta is exactly four `addi r30, r11, TheHamDirector@l` plus the
    // `lwz 0(r30)` form at the GetVenueWorld site (census: surplus addi:4, lwz:1).
    // MEASURED CAUSE -- they are PRE-inserted on the four arms because the TAIL's
    // `TheHamDirector` needs an address at the merge point: replacing the tail
    // with `RndPropAnim *anim = nullptr;` deletes all four and leaves the arms in
    // the image's `lis @h` + `lwz @l(rN)` form (in a volatile register, since
    // nothing then needs @h across the arms -- the image's r30 differs).  The
    // image keeps the @h half in r30 across the arms and loads `@l(r30)` at the
    // tail, so it needs no addi at all.  Splitting the tail at a statement
    // boundary (`WorldDir *venue = TheHamDirector->GetVenueWorld();`) is
    // byte-identical; a `HamDirector *dir` copy there is -0.5pp and at function
    // scope -8.8pp.
    // w-this-adjust (2026-11-13): POINTER AUDIT, NOT A BUG.  The four
    // `addi r3, r31, 0x78/0x88/0x98/0xa8` (r31 = frame pointer: the Handle
    // return DataNode temp) are on BOTH sides -- objdiff aligned ours one row
    // later.  The ours-only instruction is `addi r30, r11, TheHamDirector@l`,
    // the w28-a anchor.  Handle's receiver is the same vbase-converted
    // TheHamDirector on both sides.  98.1 / equal 195, diff_arg 15, ins 4.
```

Added after the move (lane w-five-clean):

```text
// w-five-clean (2026-11-13): BEHAVIOUR VERIFIED by listing.  Player(1)->
// IsPlaying() false -> p1 shot; Player(0)->IsPlaying() false -> p2; Side()
// == 1 (kSkeletonRight) -> p21, else p12; each Handle(Message("force_shot",
// shot), true) through the vbase (slot 0x14, r6 = 1).  Tail: Find<RndPropAnim>
// ("bid_start_character_faded_out.anim", true) then Animate with f1=f2=f3=0,
// r5 = false, r7 = nullptr, r8 = kEaseLinear, r10 = false -- the only diff
// there is the order of two `fmr` copies of the same 0.0.  Unicorn h4, fresh:
// EQUIVALENT/high via the zero-fill path (IsPlaying stub -> p1), both sides
// then fault identically in the virtual Handle; weak evidence.  No native
// test (needs TheGameData + TheHamDirector).  Code unchanged.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: `lwz
r3,TheHamDirector@l(r30)` vs `lwz r3,0(r30)` after a full lis+addi; same address.

## `Dc3W8bHollaProbe()`

`Dc3W8bHollaProbe` -- moved verbatim from `src/system/hamobj/HollaBackMinigame.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Attribution: no function named `Dc3W8bHollaProbe` in report.json (unit); filed under the source name

Above the definition (was lines 36-45):

```text
// w8-b: orphan-instantiation anchor, and the body MUST be an ASSIGNMENT.
// 0x824EAD60 is an ICF fold of two _M_allocate_and_copy spellings: map 45453 has
// the PAVSymbol (non-const source) one in hamobj:HollaBackMinigame.obj, map 45452
// the PBVSymbol (const source) one in hamobj:HamNavList.obj, both at 0x824EAD60.
// symbols.txt names the folded address with the PBV spelling, so this object has
// to define the CONST-source instantiation to pair at all.  Refuted spelling:
// `std::vector<Symbol> copy(src);` emits only the PAV variant and left the row at
// 0%.  stl/_vector.c:170 is why -- `operator=` is the one path that reaches
// _M_allocate_and_copy through __CONST_CAST(const_pointer, __x._M_start);
// reserve() and the copy ctor both pass the non-const _M_start.
```


## `?BeginMinigame@HollaBackMinigame@@QAAXPAVDataArray@@@Z`

`HollaBackMinigame::BeginMinigame` -- moved verbatim from `src/system/hamobj/HollaBackMinigame.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 217-248):

```text
// Measured spellings (each one edit, rebuilt, name_check ruler; baseline
// equal=850 diff_arg=2 total=852 fuzzy=99.97652). Nothing beat baseline:
//   B+C -> erase(begin(),end())       830 eq / 24 rows, 852->854 (+2 ins +4 del), 99.12676
//   B only -> erase(begin(),end())    840 eq / 13 rows, 853 (+1 ins +2 del), 99.55164
//   C only -> erase(begin(),end())    840 eq / 13 rows, 853 (+1 ins +2 del), 99.54578
//   swap the two top-level clears     826 eq / 26 rows, 99.85798
//   mWinCamCuts.clear() first         826 eq / 26 rows, 99.85798
//   braces { } around B, around C,    INERT: 850 eq / 2 rows, 99.97652 (both)
//     and around both
//   hoist shootoutArr/camCutArr       INERT: 850 eq / 2 rows, 99.97652
//     out of if(a) to function scope
//   this->mWinShoutouts at B/C        INERT: 850 eq / 2 rows, 99.97652
//   (&mWinShoutouts)[0] at B/C        INERT: 850 eq / 2 rows, 99.97652
//   `unsigned numShoutouts`           848 eq / 4 rows (2 replace), 99.83568
//   swap the two static Symbol decls  842 eq / 10 rows, 99.92958
//   move camcuts clear+push_back     749 eq / 123 rows, 872 (+20 ins +20 del), 94.48474
//     below the static Symbol decls
//   move both clears above the       731 eq / 131 rows, 862, 97.30282
//     static Symbol decls
//   local vector refs at B/C          837 eq / 15 rows (-1 ins), 99.78873
// decomp-synth (permuter), --jobs 1, two runs, ledger checked: pass 1 with
// declaration_reorder/declaration_movement/statement_reorder/scope_narrowing/
// scope_widening/commutative_swap/member_ref_bind/value_address_caching/
// temp_elimination/reference_elimination/assignment_reorder/copy_member_reorder/
// loop_var_hoist/member_readback (313 viable variants), then the FULL 112-pattern
// set (549 viable variants). 862 viable variants total, 0 improved on baseline
// (0 in every depth of both runs). Source left untouched by both.
//
// The only remaining lever would be rewriting stlport's _M_erase to flip the
// dead add's operand order in the header -- which would move every vector clear
// in the binary, and would not reproduce the image's own 3/1 split anyway.
// Behaviour is unaffected either way: both rows are in code that never runs.
```

Inside the body (was lines 272-281):

```text
        // Open (w25-rm): the four inlined clear() copy loops here and in the
        // `if (a)` arm emit `lwzx rD, base, diff` or `lwzx rD, diff, base`.
        // The image has diff first in the first three, base first in the last;
        // we have diff first only in the first. Same inlined source in all four;
        // both operands are colour candidates, so the order is set by context,
        // not spelling. In our dump the first loop is the one whose __n-like k04
        // local is dropped by the negative-benefit pass (it has one colour
        // candidate fewer); hypothesis: the image drops it in loops two and
        // three as well. Inert: local refs to either vector, unused named
        // locals before the loop (symbol ordinals unchanged).
```


## `?SetMoveState@HollaBackMinigame@@QAAXHVSymbol@@@Z`

`HollaBackMinigame::SetMoveState` -- moved verbatim from `src/system/hamobj/HollaBackMinigame.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 599-632):

```text
                    // RESIDUAL (w13-b, 99.40): the image reloads
                    // mRoutineMoves._M_start (lwz 0x488(r30)) at the top of every
                    // iteration of this search; we keep it live from the size().
                    // Same shape as MoveGraph::FindVariantPair.  Equivalent.
                    // w21-u (99.40, same 6 rows): `i < mRoutineMoves.size()`
                    // in the condition is 98.8 (spills a temp to 0x50); a
                    // `std::vector<HamMove *> &moves = mRoutineMoves;` binding
                    // is 97.8 (dead `addi r8, r30, 0x488` + spill); `int i` is
                    // byte-inert.  Behaviour re-checked against the listing:
                    // same size snapshot, same break target, same Message args.
                    // w21-au (still 99.40, same 6 rows): 16 more spellings, none
                    // above it -- inert: begin()[i], a const-vector cast on the
                    // read, `mRoutineMoves[i] == move`, an element local, `u32 i`
                    // outside the for, `for (i = 0, n = size(); ...)`, a while
                    // loop, `continue` on mismatch, `(int)i` in the Message.
                    // Worse: end()-begin() 98.8, int count/index 99.0, a const&
                    // binding 97.8, size() before SetShowing 96.4 (the image
                    // stores mShowing first), a find-then-test split 98.4, a
                    // panel local 98.2, size() in the condition 98.8 (the
                    // _M_start reload DOES appear, but with a stw spill to 0x50).
                    // Closest miss: size() through the member and the read
                    // through a `(const) std::vector<HamMove *> &` bound before
                    // the loop (or before SetShowing) stops the CSE with the
                    // size load, but MSVC then hoists the reference load above
                    // the loop (98.80, 5 rows: the lwz sits before `li r11, 0`
                    // instead of at the loop head); bound inside the loop body
                    // 98.1.
                    // w26-o: the reload is a CSE decision, not operand order --
                    // the loop body stores nothing, so c2 is free to keep the
                    // _M_start that size() already loaded.  A vector reference
                    // re-bound at the loop head is 97.7 (a dead `addi` plus a
                    // spill), a per-iteration `const HamMove *const &` element
                    // binding is 98.0, and `const u32 &numMoves`, a `const u32
                    // idx = i` and a plain element local are byte-inert.
```


## `?OnBeat@HollaBackMinigame@@QAAXXZ`

`HollaBackMinigame::OnBeat` -- moved verbatim from `src/system/hamobj/HollaBackMinigame.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 703-754):

```text
    // Our build spills this pointer into the 0x50 temp slot (`stw r10, 0x50(r31)`)
    // and never reloads it -- the one extra instruction in this function.  It is the
    // dead home-slot store (see memory: pattern_dead_home_slot_store).  Refuted here:
    // reference binding, `const`, declaring it after `songPos`/`theMoveDir`, and
    // dropping the local entirely (that last one also drops the `addi r10, r30, 0x78`
    // the target does emit, so it scores worse: 99.856 -> 99.855).
    // w16-b: `master->TotalBeat2()` in the test (no local) deletes the image's
    // `addi r10, r30, 0x78` too; a `const SongPos &` local is identical to
    // this pointer. Same one-row residual either way.
    // w21-ax: stopped at 99.85694 (2 rows: this store + the add below). The
    // target DOES materialise `addi r10, r30, 0x78` (824EAEEC) and folds both
    // compare loads onto r30 (lfs 0x7c/0x64), so this local existed; only its
    // home store is ours. Also inert/worse: declaring it after the
    // GetCurrLoopBeats call / after currentBeat (99.5, store moves later), a
    // by-value SongPos copy (99.0, memcpy), `master->SongPos2().GetTotalBeat()`
    // in the test with no local (drops the addi). `currentBeat =
    // master->TotalBeat1()` makes the volatile re-reads below unnecessary (the
    // r29-based reload then differs by expression) but costs 17 regswap rows.
    // w26-g canonical decomposition: report.json charges exactly ONE unit of
    // 100 -- an insert, 100 of 69900 (99.85694) -- and it is the store below.
    // 0x50(r31) is written twice by the base (index 57 and 641) and READ
    // nowhere, so it cannot be a behaviour difference. The `add` operand swap
    // at index 194 is register-only and the canonical ruler forgives it. That
    // makes this the closest row in the 95-100 band: one dead store.
    // Refuted this lane, every one BIT-IDENTICAL (700 insns, canonical
    // 99.85694): `SongPos *a = &.., *b = &..;` on one declarator line (both
    // orders), `SongPos *const`, separate declaration + assignment, a
    // file-static helper returning &m->SongPos2(), an explicit cast, and
    // pointer-returning accessors added to HamMaster.h as a probe (reverted).
    // The store is a home INITIALISATION, not a spill: it survives with the
    // GetCurrLoopBeats call deleted entirely. Mechanism = the dead home-slot
    // store of an address-of-sub-object temporary (memory:
    // pattern_dead_home_slot_store) -- our c1 homes the `&master->SongPos2()`
    // temp, the image's c1 keeps it in a register and its copy is already dead
    // (`addi r10, r30, 0x78` has no reader in the image either), so this is the
    // inverse of the Spotlight::SyncProperty residual, where the IMAGE homes it
    // and we do not.
    // w26-n3 (9 more variants, canonical measured per-variant on the per-object
    // path, which is exact on this unit -- baseline 99.85694 reproduced):
    //   swap the two SongPos declarators            99.47639 (much worse)
    //   const SongPos *                             99.85694 (bit-identical)
    //   &TheMaster->SongPos2() instead of &master-> 99.85694 (bit-identical)
    //   separate declarator + assignment            99.85694 (bit-identical)
    //   implicit SongPos reference + `.` spelling   build error (the two uses
    //       below go through `songPos`, not `prevSongPos`; the binding itself
    //       was refuted by w26-g)
    //   a named `float prevBeat` before the branch  99.77110 (one extra row)
    // So the store is not reachable by declaring, binding or re-rooting the
    // local: our c1 gives the `&master->SongPos2()` temp a home even though the
    // value is already in a register and every read of it folds back to
    // 0x7c(r30).  The image emits the same dead `addi r10, r30, 0x78` and no
    // home, i.e. the difference is the home allocation itself, not the local.
```

Inside the body (was lines 794-799):

```text
        // Target has `add r11, r11, r10`, ours `add r11, r10, r11`.  MSVC canonicalises
        // the commutative operands: `= subStateIdx + (...)`, a named delta local, and
        // `-= subBeatInMeasure - currentBeatInMeasure` all emit byte-identical code.
        // Plain 2-term same-register swap = backend floor.
        // w21-ax: `mSubStateIndex += delta` (99.5, cmp/blt reshuffle) and
        // `mSubStateIndex = (cur - sub) + subStateIdx` (identical) also tried.
```
